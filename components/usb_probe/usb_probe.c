#include <stdint.h>
#include "esp_log.h"
#include "esp_intr_alloc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "usb/usb_host.h"
#include "usb_probe.h"

static const char *TAG = "usb_probe";
static QueueHandle_t events;

static unsigned le16(const uint8_t *p)
{
    return p[0] | ((unsigned)p[1] << 8);
}

static void describe_config(const usb_config_desc_t *config)
{
    const uint8_t *raw = (const uint8_t *)config;
    const unsigned total = config->wTotalLength;
    ESP_LOGI(TAG, "Configuration %u: bytes=%u interfaces=%u attributes=0x%02x max-power=%u mA",
             config->bConfigurationValue, total, config->bNumInterfaces,
             config->bmAttributes, config->bMaxPower * 2);
    for (unsigned offset = 0; offset < total;) {
        if (total - offset < 2 || raw[offset] < 2 || raw[offset] > total - offset) {
            ESP_LOGE(TAG, "Malformed descriptor at offset %u", offset);
            return;
        }
        const uint8_t *d = raw + offset;
        if (d[1] == 4 && d[0] >= 9) {
            ESP_LOGI(TAG, "Interface %u alt=%u endpoints=%u class=%02x/%02x/%02x",
                     d[2], d[3], d[4], d[5], d[6], d[7]);
        } else if (d[1] == 5 && d[0] >= 7) {
            ESP_LOGI(TAG, "Endpoint 0x%02x type=%u max-packet=%u interval=%u",
                     d[2], d[3] & 3, le16(d + 4), d[6]);
        }
        offset += d[0];
    }
}

static void inspect(usb_host_client_handle_t client, uint8_t address)
{
    usb_device_handle_t device;
    esp_err_t err = usb_host_device_open(client, address, &device);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Open address=%u: %s (device may have disconnected)", address, esp_err_to_name(err));
        return;
    }
    const usb_device_desc_t *desc;
    err = usb_host_get_device_descriptor(device, &desc);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Device address=%u VID:PID=%04x:%04x USB=%04x revision=%04x class=%02x EP0=%u",
                 address, desc->idVendor, desc->idProduct, desc->bcdUSB,
                 desc->bcdDevice, desc->bDeviceClass, desc->bMaxPacketSize0);
        if (desc->idVendor == 0x067b && desc->idProduct == 0x2303 &&
            desc->bDeviceClass != 2 && desc->bMaxPacketSize0 == 64 &&
            desc->bcdUSB == 0x0110 && desc->bcdDevice == 0x0400) {
            ESP_LOGI(TAG, "PL2303 HXD descriptor match; serial initialization NOT performed");
        } else {
            ESP_LOGW(TAG, "Device does not match recorded handset HXD descriptors");
        }
    } else {
        ESP_LOGE(TAG, "Device descriptor: %s", esp_err_to_name(err));
    }
    const usb_config_desc_t *config;
    err = usb_host_get_active_config_descriptor(device, &config);
    if (err == ESP_OK) describe_config(config);
    else ESP_LOGE(TAG, "Configuration descriptor: %s", esp_err_to_name(err));
    err = usb_host_device_close(client, device);
    if (err != ESP_OK) ESP_LOGE(TAG, "Device close: %s", esp_err_to_name(err));
}

static void client_event(const usb_host_client_event_msg_t *event, void *arg)
{
    (void)arg;
    if (event->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        uint8_t address = event->new_dev.address;
        if (xQueueSend(events, &address, 0) != pdTRUE)
            ESP_LOGE(TAG, "Enumeration queue full; reconnect device to retry");
    } else if (event->event == USB_HOST_CLIENT_EVENT_DEV_REMOVED) {
        ESP_LOGI(TAG, "Device disconnected: address=%u", event->dev_removed.address);
    }
}

static void daemon_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t flags;
        ESP_ERROR_CHECK(usb_host_lib_handle_events(portMAX_DELAY, &flags));
    }
}

void usb_probe_run(void)
{
    events = xQueueCreate(16, sizeof(uint8_t));
    ESP_ERROR_CHECK(events ? ESP_OK : ESP_ERR_NO_MEM);
    const usb_host_config_t host = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    ESP_ERROR_CHECK(usb_host_install(&host));
    usb_host_client_handle_t client;
    const usb_host_client_config_t config = {
        .is_synchronous = false,
        .max_num_event_msg = 16,
        .flags = {.notify_dev_removed = true},
        .async = {.client_event_callback = client_event, .callback_arg = NULL},
    };
    ESP_ERROR_CHECK(usb_host_client_register(&config, &client));
    ESP_ERROR_CHECK(xTaskCreate(daemon_task, "usb_daemon", 4096, NULL, 10, NULL) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_LOGI(TAG, "USB host initialized; enumeration only; HC-05 UART disabled");
    ESP_LOGI(TAG, "Reconnect to repeat descriptor capture; no serial commands sent");
    TickType_t last_report = xTaskGetTickCount();
    for (;;) {
        esp_err_t err = usb_host_client_handle_events(client, pdMS_TO_TICKS(100));
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) ESP_ERROR_CHECK(err);
        uint8_t address;
        while (xQueueReceive(events, &address, 0) == pdTRUE) inspect(client, address);
        TickType_t now = xTaskGetTickCount();
        if ((TickType_t)(now - last_report) >= pdMS_TO_TICKS(10000)) {
            ESP_LOGI(TAG, "USB enumeration task active; no serial commands sent");
            last_report = now;
        }
    }
}
