#include "hc05_uart.h"
#include "sdkconfig.h"

esp_err_t hc05_uart_init(QueueHandle_t *events)
{
    const uart_config_t config = {
        .baud_rate = CONFIG_HC05_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_param_config(HC05_UART, &config);
    if (err != ESP_OK) return err;
    err = uart_set_pin(HC05_UART, CONFIG_HC05_TX_GPIO, CONFIG_HC05_RX_GPIO,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) return err;
    return uart_driver_install(HC05_UART, 4096, 4096, 32, events, 0);
}
