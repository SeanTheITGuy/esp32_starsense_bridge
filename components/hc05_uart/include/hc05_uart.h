#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#define HC05_UART UART_NUM_1
/* Caller owns event consumption. Only UART1 carries test bytes. */
esp_err_t hc05_uart_init(QueueHandle_t *events);
