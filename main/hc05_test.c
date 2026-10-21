#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include "esp_log.h"
#include "sdkconfig.h"
#include "hc05_uart.h"

static const char *TAG = "hc05_test";

void hc05_test_run(void)
{
    QueueHandle_t events;
    ESP_ERROR_CHECK(hc05_uart_init(&events));
    ESP_LOGI(TAG, "UART1 TX=%d RX=%d %d 8N1; binary echo test; USB host disabled",
             CONFIG_HC05_TX_GPIO, CONFIG_HC05_RX_GPIO, CONFIG_HC05_BAUD);
    uint64_t received = 0;
    uint8_t bytes[256];
    uart_event_t event;
    for (;;) {
        if (xQueueReceive(events, &event, portMAX_DELAY) != pdTRUE) continue;
        if (event.type == UART_DATA) {
            size_t remaining = event.size;
            while (remaining) {
                size_t wanted = remaining < sizeof(bytes) ? remaining : sizeof(bytes);
                int count = uart_read_bytes(HC05_UART, bytes, wanted, pdMS_TO_TICKS(100));
                if (count <= 0) {
                    ESP_LOGE(TAG, "RX event could not be drained; restart test");
                    break;
                }
                int sent = uart_write_bytes(HC05_UART, bytes, count);
                if (sent != count) {
                    ESP_LOGE(TAG, "TX incomplete (%d/%d); test failed", sent, count);
                    abort();
                }
                remaining -= count;
                received += count;
            }
        } else if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL) {
            ESP_LOGE(TAG, "RX overflow after %" PRIu64 " bytes; test failed; restart firmware", received);
            /* Do not hide data loss by silently resuming. */
            abort();
        } else if (event.type == UART_PARITY_ERR || event.type == UART_FRAME_ERR ||
                   event.type == UART_BREAK) {
            ESP_LOGE(TAG, "UART signal error type=%d; check baud/wiring; test failed", event.type);
            abort();
        }
    }
}
