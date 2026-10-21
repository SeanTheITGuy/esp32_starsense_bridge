#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hc05_uart.h"
#include "sdkconfig.h"

static const char *TAG = "hc05_diag";
static QueueHandle_t events;

static bool query_match(const char *text, const char *expected)
{
    uint8_t response[256];
    size_t size = 0;
    uart_flush_input(HC05_UART);
    xQueueReset(events);
    ESP_LOGI(TAG, "UART query: %s", text);
    ESP_ERROR_CHECK(uart_write_bytes(HC05_UART, text, strlen(text)) == (int)strlen(text)
                    ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(uart_wait_tx_done(HC05_UART, pdMS_TO_TICKS(500)));
    int64_t deadline = esp_timer_get_time() + 1500000;
    while (esp_timer_get_time() < deadline && size < sizeof(response) - 1) {
        int n = uart_read_bytes(HC05_UART, response + size,
                               sizeof(response) - 1 - size, pdMS_TO_TICKS(50));
        if (n > 0) size += n;
    }
    if (size) {
        response[size] = 0;
        ESP_LOGI(TAG, "UART response (%u bytes): %s", (unsigned)size, (char *)response);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, response, size, ESP_LOG_INFO);
    } else ESP_LOGW(TAG, "No response (module may require hardware AT-mode entry)");
    return size && strstr((char *)response, expected) != NULL;
}

static bool query(const char *text) { return query_match(text, "OK"); }

void hc05_diag_run(void)
{
    ESP_ERROR_CHECK(hc05_uart_init(&events));
#if CONFIG_JDY31_DISABLE_STATUS || CONFIG_JDY31_RENAME_CELESTRON
    ESP_LOGI(TAG, "JDY-31 setup diagnostic; USB host disabled; KEY/EN not driven");
    ESP_LOGI(TAG, "Only selected JDY-31 setup writes after version match; no handset commands");
#else
    ESP_LOGI(TAG, "READ-ONLY Bluetooth UART diagnostic; USB host disabled; KEY/EN not driven");
    ESP_LOGI(TAG, "No module configuration writes or handset commands are sent");
#endif
    vTaskDelay(pdMS_TO_TICKS(3000));
    const int bauds[] = {9600, 38400, 115200, 57600, 19200, 128000};
    bool found = false;
    for (unsigned i = 0; i < sizeof(bauds) / sizeof(bauds[0]); i++) {
        ESP_ERROR_CHECK(uart_set_baudrate(HC05_UART, bauds[i]));
        ESP_LOGI(TAG, "Probing AT query at %d baud", bauds[i]);
        /* JDY-31 identifies with AT+VERSION and need not answer bare AT. */
        if (query_match("AT+VERSION\r\n", "+VERSION=JDY-31")) {
            found = true;
            query_match("AT+LADDR\r\n", "+LADDR=");
            query_match("AT+BAUD\r\n", "+BAUD=");
            query_match("AT+ENLOG\r\n", "+ENLOG=");
#if CONFIG_JDY31_DISABLE_STATUS
            if (!query("AT+ENLOG0\r\n") ||
                !query_match("AT+ENLOG\r\n", "+ENLOG=0")) {
                ESP_LOGE(TAG, "JDY-31 status-report disable/readback FAILED");
            } else ESP_LOGI(TAG, "JDY-31 ENLOG=0 VERIFIED; identity/name/PIN/baud unchanged");
#endif
#if CONFIG_JDY31_RENAME_CELESTRON
            if (!query("AT+NAMECELESTRON\r\n") ||
                !query_match("AT+NAME\r\n", "+NAME=CELESTRON")) {
                ESP_LOGE(TAG, "JDY-31 rename/readback FAILED");
            } else {
                ESP_LOGI(TAG, "JDY-31 name CELESTRON VERIFIED; restarting module to refresh advertised name");
                query("AT+RESET\r\n");
            }
#endif
            break;
        }
        if (!query("AT\r\n")) continue;
        found = true;
        query("AT+VERSION?\r\n");
        query("AT+VERSION\r\n");
        query("AT+POLAR?\r\n");
        query("AT+UART?\r\n");
        query("AT+STATE?\r\n");
        break;
    }
    ESP_ERROR_CHECK(uart_set_baudrate(HC05_UART, 9600));
    ESP_LOGI(TAG, "Diagnostic finished: AT responder=%d. UART restored to 9600; USB remains disabled", found);
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
