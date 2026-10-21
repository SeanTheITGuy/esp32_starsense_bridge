#include <string.h>
#include <inttypes.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "hc05_uart.h"
#include "sdkconfig.h"
#include "byte_bridge.h"

#define CHUNK_SIZE 64
#define QUEUE_DEPTH 128
static const char *TAG = "byte_bridge";
typedef struct { uint32_t generation; size_t size; uint8_t data[CHUNK_SIZE]; } chunk_t;
static QueueHandle_t uart_events, from_usb, to_usb;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static bool active, faulted, tx_busy;
static uint32_t generation;
static uint64_t uart_rx, uart_tx, offline_dropped;
static bool previous_link;

static bool link_up(void)
{
#if CONFIG_HC05_STATE_GPIO >= 0
    return gpio_get_level(CONFIG_HC05_STATE_GPIO) != 0;
#else
    return false;
#endif
}

static bool snapshot(uint32_t *epoch)
{
    portENTER_CRITICAL(&lock);
    bool result = active;
    *epoch = generation;
    portEXIT_CRITICAL(&lock);
    return result && link_up();
}

static void fault(const char *reason)
{
    portENTER_CRITICAL(&lock);
    faulted = true;
    active = false;
    portEXIT_CRITICAL(&lock);
    ESP_LOGE(TAG, "%s; delivery cannot be guaranteed, USB session will stop", reason);
}

static void uart_rx_task(void *arg)
{
    (void)arg;
    uart_event_t event;
    for (;;) {
        if (xQueueReceive(uart_events, &event, portMAX_DELAY) != pdTRUE) continue;
        if (event.type == UART_DATA) {
            size_t remaining = event.size;
            while (remaining) {
                chunk_t chunk;
                bool was_active = snapshot(&chunk.generation);
                size_t wanted = remaining < CHUNK_SIZE ? remaining : CHUNK_SIZE;
                int n = uart_read_bytes(HC05_UART, chunk.data, wanted, pdMS_TO_TICKS(100));
                if (n <= 0) break; /* Session reset may have flushed old RX events. */
                chunk.size = n;
                remaining -= n;
                uint32_t current;
                bool still_active = snapshot(&current);
                portENTER_CRITICAL(&lock);
                uart_rx += n;
                if (!was_active || !still_active || current != chunk.generation) offline_dropped += n;
                portEXIT_CRITICAL(&lock);
                if (was_active && still_active && current == chunk.generation &&
                    xQueueSend(to_usb, &chunk, 0) != pdTRUE)
                    fault("UART-to-USB queue overflow");
            }
        } else if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL ||
                   event.type == UART_FRAME_ERR || event.type == UART_PARITY_ERR ||
                   event.type == UART_BREAK) {
            fault("UART receive overflow or signal error");
            uart_flush_input(HC05_UART);
            xQueueReset(uart_events);
        }
    }
}

static void uart_tx_task(void *arg)
{
    (void)arg;
    chunk_t chunk;
    for (;;) {
        if (xQueueReceive(from_usb, &chunk, portMAX_DELAY) != pdTRUE) continue;
        portENTER_CRITICAL(&lock);
        bool send = active && generation == chunk.generation && link_up();
        if (send) tx_busy = true;
        portEXIT_CRITICAL(&lock);
        if (!send) continue;
        int n = uart_write_bytes(HC05_UART, chunk.data, chunk.size);
        /* Limit committed driver TX to one chunk. Previously transmitted wire
         * bytes cannot be recalled on disconnect; no software queue is replayed.
         */
        unsigned wait_ms = (CHUNK_SIZE * 10000U + CONFIG_HC05_BAUD - 1) / CONFIG_HC05_BAUD + 200;
        esp_err_t err = uart_wait_tx_done(HC05_UART, pdMS_TO_TICKS(wait_ms));
        portENTER_CRITICAL(&lock);
        if (n > 0) uart_tx += n;
        tx_busy = false;
        portEXIT_CRITICAL(&lock);
        if (n != (int)chunk.size || err != ESP_OK) fault("UART transmit incomplete or timed out");
    }
}

void byte_bridge_init(void)
{
#if CONFIG_HC05_STATE_GPIO >= 0
    ESP_ERROR_CHECK(GPIO_IS_VALID_GPIO(CONFIG_HC05_STATE_GPIO) &&
                    CONFIG_HC05_STATE_GPIO != CONFIG_HC05_RX_GPIO &&
                    CONFIG_HC05_STATE_GPIO != CONFIG_HC05_TX_GPIO &&
                    CONFIG_HC05_STATE_GPIO != 19 && CONFIG_HC05_STATE_GPIO != 20
                    ? ESP_OK : ESP_ERR_INVALID_ARG);
    const gpio_config_t input = {
        .pin_bit_mask = 1ULL << CONFIG_HC05_STATE_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&input));
    ESP_LOGI(TAG, "HC-05 STATE GPIO%d, active high; disconnected traffic is discarded",
             CONFIG_HC05_STATE_GPIO);
#else
    ESP_LOGE(TAG, "STATE input not configured; forwarding disabled");
#endif
    previous_link = link_up();
    from_usb = xQueueCreate(QUEUE_DEPTH, sizeof(chunk_t));
    to_usb = xQueueCreate(QUEUE_DEPTH, sizeof(chunk_t));
    ESP_ERROR_CHECK(from_usb && to_usb ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(hc05_uart_init(&uart_events));
    ESP_ERROR_CHECK(xTaskCreate(uart_rx_task, "hc05_rx", 3072, NULL, 8, NULL) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(uart_tx_task, "hc05_tx", 3072, NULL, 8, NULL) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_LOGI(TAG, "UART1 RX=%d TX=%d %d 8N1; queues=%d chunks of %d bytes each way",
             CONFIG_HC05_RX_GPIO, CONFIG_HC05_TX_GPIO, CONFIG_HC05_BAUD, QUEUE_DEPTH, CHUNK_SIZE);
}

void byte_bridge_stop(void)
{
    portENTER_CRITICAL(&lock);
    active = false;
    generation++;
    portEXIT_CRITICAL(&lock);
    xQueueReset(from_usb);
    xQueueReset(to_usb);
    uart_flush_input(HC05_UART);
    xQueueReset(uart_events);
}

void byte_bridge_start(void)
{
    byte_bridge_stop();
    portENTER_CRITICAL(&lock);
    faulted = false;
    active = true;
    portEXIT_CRITICAL(&lock);
}

bool byte_bridge_active(void)
{
    uint32_t epoch;
    return snapshot(&epoch);
}

bool byte_bridge_tx_idle(void)
{
    portENTER_CRITICAL(&lock);
    bool idle = !tx_busy;
    portEXIT_CRITICAL(&lock);
    return idle;
}

bool byte_bridge_faulted(void)
{
    portENTER_CRITICAL(&lock);
    bool result = faulted;
    portEXIT_CRITICAL(&lock);
    return result;
}

bool byte_bridge_rx_room(void)
{
    return uxQueueSpacesAvailable(from_usb) > 0;
}

bool byte_bridge_from_usb(const uint8_t *bytes, size_t length)
{
    chunk_t chunk;
    if (!snapshot(&chunk.generation)) return true; /* Startup/offline bytes are discarded. */
    if (length > CHUNK_SIZE) { fault("USB RX chunk too large"); return false; }
    if (!length) return true;
    chunk.size = length;
    memcpy(chunk.data, bytes, length);
    if (xQueueSend(from_usb, &chunk, 0) != pdTRUE) {
        fault("USB-to-UART queue overflow");
        return false;
    }
    return true;
}

size_t byte_bridge_to_usb(uint8_t *bytes)
{
    chunk_t chunk;
    uint32_t current;
    if (!snapshot(&current)) return 0;
    while (xQueueReceive(to_usb, &chunk, 0) == pdTRUE) {
        if (chunk.generation != current) continue;
        memcpy(bytes, chunk.data, chunk.size);
        return chunk.size;
    }
    return 0;
}

void byte_bridge_log_status(void)
{
    portENTER_CRITICAL(&lock);
    uint64_t rx = uart_rx, tx = uart_tx, dropped = offline_dropped;
    bool enabled = active, error = faulted;
    portEXIT_CRITICAL(&lock);
    ESP_LOGI(TAG, "enabled=%d BT=%d fault=%d UART RX=%" PRIu64 " TX=%" PRIu64
             " offline-discard=%" PRIu64 " queued-to-USB=%u queued-to-UART=%u",
             enabled, link_up(), error, rx, tx, dropped,
             (unsigned)uxQueueMessagesWaiting(to_usb), (unsigned)uxQueueMessagesWaiting(from_usb));
}

void byte_bridge_poll_link(void)
{
    bool connected = link_up();
    if (connected == previous_link) return;
    previous_link = connected;
    ESP_LOGI(TAG, "Bluetooth STATE %s", connected ? "connected" : "disconnected");
    if (!connected) {
        /* Keep USB readiness enabled. A new STATE-high connection can accept
         * its first UART bytes immediately, even before this polling task next
         * runs. Both UART worker paths also check the physical STATE level.
         */
        portENTER_CRITICAL(&lock);
        generation++;
        portEXIT_CRITICAL(&lock);
        xQueueReset(from_usb);
        xQueueReset(to_usb);
        uart_flush_input(HC05_UART);
        xQueueReset(uart_events);
    }
}
