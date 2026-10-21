/* Native ESP-IDF HXD bring-up state machine.
 * USB request values were researched in Linux pl2303.c and USB Host Shield
 * cdcprolific.cpp; see docs/pl2303-research.md. No Linux/Arduino driver code
 * is incorporated. This deliberately supports only the measured handset.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_intr_alloc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb/usb_host.h"
#include "sdkconfig.h"
#include "pl2303_host.h"
#include "byte_bridge.h"

static const char *TAG = "pl2303_test";

typedef struct {
    usb_transfer_t *usb;
    bool pending;
    bool done;
} io_t;

static struct {
    usb_host_client_handle_t client;
    usb_device_handle_t device;
    uint8_t next_address;
    uint8_t iface, in_ep, out_ep, irq_ep;
    unsigned in_mps, irq_mps;
    bool claimed, stop, cancelling, gone, ready, expecting, passed;
    bool bridge;
    uint64_t usb_rx_bytes, usb_tx_bytes;
    unsigned step, round, reply_size;
    uint8_t reply[64], flow_register, uart_state;
    int64_t control_deadline, echo_deadline, next_echo, heartbeat;
    io_t control, rx, tx, irq;
} state;

/* H/HX-family unlock requests; HXD uses the non-legacy final value 0x44.
 * Request tuple: direction, wValue, wIndex. Vendor reads return one byte.
 */
static const struct { bool read; uint16_t value, index; } unlock[] = {
    {true,  0x8484, 0}, {false, 0x0404, 0},
    {true,  0x8484, 0}, {true,  0x8383, 0},
    {true,  0x8484, 0}, {false, 0x0404, 1},
    {true,  0x8484, 0}, {true,  0x8383, 0},
    {false, 0, 1}, {false, 1, 0}, {false, 2, 0x44},
};

static void fail(const char *why)
{
    if (!state.stop) ESP_LOGE(TAG, "%s; session stopped, unplug/reconnect to retry", why);
    state.stop = true;
    state.ready = false;
}

static void completed(usb_transfer_t *transfer)
{
    io_t *io = transfer->context;
    io->pending = false;
    io->done = true;
}

static void allocate(io_t *io, size_t bytes, uint8_t endpoint)
{
    ESP_ERROR_CHECK(usb_host_transfer_alloc(bytes, 0, &io->usb));
    io->usb->device_handle = state.device;
    io->usb->bEndpointAddress = endpoint;
    io->usb->callback = completed;
    io->usb->context = io;
    io->done = false;
    io->pending = false;
}

static bool submit(io_t *io, int length, bool control)
{
    io->usb->num_bytes = length;
    io->done = false;
    io->pending = true;
    esp_err_t err = control ? usb_host_transfer_submit_control(state.client, io->usb)
                            : usb_host_transfer_submit(io->usb);
    if (err != ESP_OK) {
        io->pending = false;
        ESP_LOGE(TAG, "Submit endpoint=%02x: %s", io->usb->bEndpointAddress, esp_err_to_name(err));
        fail("USB transfer submission failed");
        return false;
    }
    return true;
}

static void control(uint8_t type, uint8_t request, uint16_t value,
                    uint16_t index, const uint8_t *payload, uint16_t length)
{
    usb_setup_packet_t *setup = (usb_setup_packet_t *)state.control.usb->data_buffer;
    *setup = (usb_setup_packet_t){.bmRequestType = type, .bRequest = request,
        .wValue = value, .wIndex = index, .wLength = length};
    if (payload && length) memcpy(state.control.usb->data_buffer + 8, payload, length);
    ESP_LOGI(TAG, "Setup step=%u type=%02x request=%02x value=%04x index=%04x length=%u",
             state.step, type, request, value, index, length);
    if (submit(&state.control, 8 + length, true))
        state.control_deadline = esp_timer_get_time() + 2000000;
}

static void next_setup(void)
{
    const unsigned count = sizeof(unlock) / sizeof(unlock[0]);
    if (state.step < count) {
        unsigned i = state.step;
        control(unlock[i].read ? 0xc0 : 0x40, 1, unlock[i].value,
                unlock[i].index, NULL, unlock[i].read ? 1 : 0);
        return;
    }
    switch (state.step - count) {
    case 0: control(0x40, 1, 8, 0, NULL, 0); break;
    case 1: control(0x40, 1, 9, 0, NULL, 0); break;
    case 2: {
        uint32_t baud = CONFIG_PL2303_TEST_BAUD;
        uint8_t line[7] = {baud, baud >> 8, baud >> 16, baud >> 24, 0, 0, 8};
        control(0x21, 0x20, 0, state.iface, line, sizeof(line));
        break;
    }
    case 3: control(0xc0, 1, 0x80, 0, NULL, 1); break;
    case 4: control(0x40, 1, 0, state.flow_register & 0x0f, NULL, 0); break;
    case 5: control(0x21, 0x22, CONFIG_PL2303_TEST_CONTROL_LINES, state.iface, NULL, 0); break;
    default:
        state.ready = true;
        unsigned settle = state.bridge ? CONFIG_BRIDGE_STARTUP_MS : CONFIG_PL2303_TEST_SETTLE_MS;
        state.next_echo = esp_timer_get_time() + (int64_t)settle * 1000;
        ESP_LOGI(TAG, "HXD initialization completed: %d 8N1, flow off, DTR=%d RTS=%d",
                 CONFIG_PL2303_TEST_BAUD, !!(CONFIG_PL2303_TEST_CONTROL_LINES & 1),
                 !!(CONFIG_PL2303_TEST_CONTROL_LINES & 2));
        ESP_LOGI(TAG, "Waiting %u ms for handset startup before %s", settle,
                 state.bridge ? "forwarding" : "echo");
        submit(&state.rx, state.in_mps, false);
        if (!state.stop) submit(&state.irq, state.irq_mps, false);
        break;
    }
}

static bool endpoints(const usb_config_desc_t *config)
{
    const uint8_t *raw = (const uint8_t *)config;
    unsigned size = config->wTotalLength;
    if (config->bNumInterfaces != 1 || size < 9 || size > 512) return false;
    bool iface = false;
    unsigned found = 0;
    for (unsigned pos = 0; pos < size;) {
        if (size - pos < 2 || raw[pos] < 2 || raw[pos] > size - pos) return false;
        const uint8_t *d = raw + pos;
        if (d[1] == 4) {
            if (d[0] < 9 || iface || d[3] != 0 || d[4] != 3 ||
                d[5] != 0xff || d[6] || d[7]) return false;
            iface = true;
            state.iface = d[2];
        } else if (d[1] == 5) {
            if (!iface || d[0] < 7 || !(d[2] & 0x0f)) return false;
            unsigned mps = d[4] | ((unsigned)d[5] << 8);
            uint8_t type = d[3] & 3;
            if (type == 2 && (d[2] & 0x80) && !state.in_ep && mps == 64) {
                state.in_ep = d[2]; state.in_mps = mps;
            } else if (type == 2 && !(d[2] & 0x80) && !state.out_ep && mps == 64) {
                state.out_ep = d[2];
            } else if (type == 3 && (d[2] & 0x80) && !state.irq_ep && mps == 10 && d[6] == 1) {
                state.irq_ep = d[2]; state.irq_mps = mps;
            } else return false;
            found++;
        }
        pos += d[0];
    }
    return iface && found == 3 && state.in_ep && state.out_ep && state.irq_ep;
}

static void open_device(uint8_t address)
{
    esp_err_t err = usb_host_device_open(state.client, address, &state.device);
    if (err != ESP_OK) {
        state.device = NULL;
        ESP_LOGW(TAG, "Device open: %s", esp_err_to_name(err));
        return;
    }
    state.stop = state.cancelling = state.gone = state.ready = state.expecting = state.passed = false;
    state.step = state.round = state.reply_size = 0;
    state.in_ep = state.out_ep = state.irq_ep = 0;
    state.uart_state = 0;
    const usb_device_desc_t *desc;
    err = usb_host_get_device_descriptor(state.device, &desc);
    if (err != ESP_OK || desc->idVendor != 0x067b || desc->idProduct != 0x2303 ||
        desc->bcdUSB != 0x0110 || desc->bcdDevice != 0x0400 ||
        desc->bDeviceClass != 0 || desc->bMaxPacketSize0 != 64) {
        fail("Unsupported USB descriptor family (only measured 067b:2303 HXD allowed)");
        return;
    }
    const usb_config_desc_t *config;
    err = usb_host_get_active_config_descriptor(state.device, &config);
    if (err != ESP_OK || !endpoints(config)) {
        fail("Unsupported or malformed interface/endpoint layout");
        return;
    }
    err = usb_host_interface_claim(state.client, state.device, state.iface, 0);
    if (err != ESP_OK) { fail("Interface claim failed"); return; }
    state.claimed = true;
    ESP_LOGI(TAG, "HXD address=%u interface=%u bulk IN=%02x OUT=%02x status=%02x",
             address, state.iface, state.in_ep, state.out_ep, state.irq_ep);
    allocate(&state.control, 72, 0);
    allocate(&state.rx, state.in_mps, state.in_ep);
    allocate(&state.tx, 64, state.out_ep);
    allocate(&state.irq, state.irq_mps, state.irq_ep);
    next_setup();
}

static void client_event(const usb_host_client_event_msg_t *event, void *arg)
{
    (void)arg;
    if (event->event == USB_HOST_CLIENT_EVENT_NEW_DEV && !state.next_address)
        state.next_address = event->new_dev.address;
    if (event->event == USB_HOST_CLIENT_EVENT_DEV_GONE &&
        event->dev_gone.dev_hdl == state.device) {
        ESP_LOGI(TAG, "Handset disconnected; draining outstanding transfers");
        state.gone = state.stop = true;
        state.ready = false;
    }
}

static void handle_control(void)
{
    io_t *io = &state.control;
    if (!io->done) return;
    io->done = false;
    if (state.stop) return;
    if (io->usb->status != USB_TRANSFER_STATUS_COMPLETED ||
        io->usb->actual_num_bytes != io->usb->num_bytes) {
        ESP_LOGE(TAG, "Control step=%u status=%d length=%d/%d", state.step,
                 io->usb->status, io->usb->actual_num_bytes, io->usb->num_bytes);
        fail("PL2303 control request failed (no automatic clone workaround)");
        return;
    }
    if (state.step == sizeof(unlock) / sizeof(unlock[0]) + 3)
        state.flow_register = io->usb->data_buffer[8];
    state.step++;
    next_setup();
}

static void handle_rx(void)
{
    io_t *io = &state.rx;
    if (!io->done) return;
    io->done = false;
    if (state.stop) return;
    if (io->usb->status != USB_TRANSFER_STATUS_COMPLETED) {
        ESP_LOGE(TAG, "Bulk IN status=%d", io->usb->status);
        fail("USB receive failed"); return;
    }
    int length = io->usb->actual_num_bytes;
    if (state.bridge) {
        state.usb_rx_bytes += length;
#if CONFIG_BRIDGE_TRACE_HEX
        if (length) {
            ESP_LOGI(TAG, "USB -> UART: %d bytes", length);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, io->usb->data_buffer, length, ESP_LOG_INFO);
        }
#endif
        if (!byte_bridge_from_usb(io->usb->data_buffer, length)) {
            fail("Bridge cannot accept received bytes"); return;
        }
        /* The client task resumes IN only when a whole next packet fits. */
        return;
    }
    if (length) {
        ESP_LOGI(TAG, "USB RX %d bytes", length);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, io->usb->data_buffer, length, ESP_LOG_INFO);
        if (state.expecting) {
            if (length > (int)sizeof(state.reply) - (int)state.reply_size) {
                fail("Echo response overflow"); return;
            }
            memcpy(state.reply + state.reply_size, io->usb->data_buffer, length);
            state.reply_size += length;
            if (state.reply_size > 2 ||
                (state.reply_size > 0 && state.reply[0] != 'x') ||
                (state.reply_size > 1 && state.reply[1] != '#')) {
                fail("Unexpected echo response"); return;
            }
        } else if (state.round) {
            fail("Unsolicited bytes between echo transactions"); return;
        } else ESP_LOGW(TAG, "Discarding pre-test serial bytes");
    }
    submit(io, state.in_mps, false);
}

static void handle_irq(void)
{
    io_t *io = &state.irq;
    if (!io->done) return;
    io->done = false;
    if (state.stop) return;
    if (io->usb->status != USB_TRANSFER_STATUS_COMPLETED) {
        fail("USB status endpoint failed"); return;
    }
    if (io->usb->actual_num_bytes >= 9) {
        uint8_t status = io->usb->data_buffer[8];
        if (status != state.uart_state)
            ESP_LOGI(TAG, "UART status=%02x (CTS/DSR/etc.; no flow gating)", status);
        state.uart_state = status;
        if (status & 0x74) { fail("PL2303 UART break/framing/parity/overrun indication"); return; }
    }
    submit(io, state.irq_mps, false);
}

static void echo_tick(int64_t now)
{
    if (!state.ready || state.stop) return;
    if (state.tx.done) {
        state.tx.done = false;
        if (state.tx.usb->status != USB_TRANSFER_STATUS_COMPLETED ||
            state.tx.usb->actual_num_bytes != 2) {
            fail("Echo OUT completion failed; uncertain delivery, not retrying"); return;
        }
    }
    if (state.expecting && state.reply_size == 2 && !state.tx.pending) {
        state.expecting = false;
        state.round++;
        state.next_echo = now + 250000;
        ESP_LOGI(TAG, "Echo %u/%d PASS: Kx -> x#", state.round, CONFIG_PL2303_TEST_ROUNDS);
    }
    if (state.expecting && now >= state.echo_deadline) {
        fail("Echo timed out; baud/control lines/handset state remain unverified"); return;
    }
    if (!state.expecting && !state.passed && now >= state.next_echo) {
        if (state.round == CONFIG_PL2303_TEST_ROUNDS) {
            state.passed = true;
            ESP_LOGI(TAG, "PASS: independent USB serial echo; reconnect to repeat; HC-05 disabled");
            return;
        }
        state.reply_size = 0;
        state.expecting = true;
        state.echo_deadline = now + 3000000;
        memcpy(state.tx.usb->data_buffer, "Kx", 2);
        ESP_LOGI(TAG, "USB TX: 4b 78 (Kx only)");
        submit(&state.tx, 2, false);
    }
}

static bool pending(void)
{
    return state.control.pending || state.rx.pending || state.tx.pending || state.irq.pending;
}

static void bridge_tick(int64_t now)
{
    if (!state.ready || state.stop) return;
    if (!state.passed && now >= state.next_echo && byte_bridge_tx_idle()) {
        byte_bridge_start();
        state.passed = true;
        ESP_LOGI(TAG, "BRIDGE READY: transparent USB <-> HC-05; no generated commands");
    }
    if (state.passed && byte_bridge_faulted()) {
        fail("UART/bridge transport fault"); return;
    }
    if (state.tx.done) {
        state.tx.done = false;
        if (state.tx.usb->status != USB_TRANSFER_STATUS_COMPLETED ||
            state.tx.usb->actual_num_bytes != state.tx.usb->num_bytes) {
            fail("USB OUT failed; uncertain delivery, no automatic replay"); return;
        }
        state.usb_tx_bytes += state.tx.usb->actual_num_bytes;
    }
    if (!state.rx.pending && !state.rx.done && byte_bridge_rx_room())
        submit(&state.rx, state.in_mps, false);
    if (!state.stop && state.passed && !state.tx.pending && !state.tx.done) {
        size_t length = byte_bridge_to_usb(state.tx.usb->data_buffer);
        if (length) {
#if CONFIG_BRIDGE_TRACE_HEX
            ESP_LOGI(TAG, "UART -> USB: %u bytes", (unsigned)length);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, state.tx.usb->data_buffer, length, ESP_LOG_INFO);
#endif
            submit(&state.tx, length, false);
        }
    }
}

static void cleanup(void)
{
    if (!state.device || !state.stop) return;
    if (!state.cancelling) {
        state.cancelling = true;
        if (state.bridge) byte_bridge_stop();
        io_t *ios[] = {&state.rx, &state.tx, &state.irq};
        for (unsigned i = 0; i < 3; i++) {
            io_t *io = ios[i];
            if (!io->usb || !io->pending) continue;
            esp_err_t err = usb_host_endpoint_halt(state.device, io->usb->bEndpointAddress);
            if (err == ESP_OK) err = usb_host_endpoint_flush(state.device, io->usb->bEndpointAddress);
            if (err != ESP_OK) ESP_LOGW(TAG, "Cancel ep=%02x: %s; waiting for completion",
                                       io->usb->bEndpointAddress, esp_err_to_name(err));
        }
    }
    /* Never release buffers while DMA/callback ownership is outstanding.
     * A stuck EP0 request may require physical removal; deadlines do not free it.
     */
    if (pending()) return;
    io_t *ios[] = {&state.control, &state.rx, &state.tx, &state.irq};
    for (unsigned i = 0; i < 4; i++) {
        ESP_ERROR_CHECK(usb_host_transfer_free(ios[i]->usb));
        memset(ios[i], 0, sizeof(*ios[i]));
    }
    if (state.claimed) {
        ESP_ERROR_CHECK(usb_host_interface_release(state.client, state.device, state.iface));
        state.claimed = false;
    }
    ESP_ERROR_CHECK(usb_host_device_close(state.client, state.device));
    state.device = NULL;
    ESP_LOGI(TAG, "Session released; waiting for handset reconnect");
}

static void daemon(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t flags;
        ESP_ERROR_CHECK(usb_host_lib_handle_events(portMAX_DELAY, &flags));
    }
}

static void run(bool bridge)
{
    state.bridge = bridge;
    if (bridge) byte_bridge_init();
    const usb_host_config_t host = {.skip_phy_setup = false, .intr_flags = ESP_INTR_FLAG_LEVEL1};
    ESP_ERROR_CHECK(usb_host_install(&host));
    const usb_host_client_config_t client = {.is_synchronous = false, .max_num_event_msg = 16,
        .async = {.client_event_callback = client_event, .callback_arg = NULL}};
    ESP_ERROR_CHECK(usb_host_client_register(&client, &state.client));
    ESP_ERROR_CHECK(xTaskCreate(daemon, "usb_daemon", 4096, NULL, 10, NULL) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_LOGI(TAG, "%s", bridge ? "Transparent HXD bridge active; waiting for USB; no generated commands"
                              : "USB HXD echo test active; HC-05 disabled; only Kx commands are sent");
    for (;;) {
        esp_err_t err = usb_host_client_handle_events(state.client, pdMS_TO_TICKS(10));
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) ESP_ERROR_CHECK(err);
        if (bridge) byte_bridge_poll_link();
        if (!state.device && state.next_address) {
            uint8_t address = state.next_address;
            state.next_address = 0;
            open_device(address);
        }
        handle_control();
        handle_rx();
        handle_irq();
        int64_t now = esp_timer_get_time();
        if (state.device && state.control.pending && !state.stop && now > state.control_deadline)
            fail("Control deadline exceeded; retain transfer until completion/removal");
        if (bridge) bridge_tick(now);
        else echo_tick(now);
        cleanup();
        if (now - state.heartbeat >= 10000000) {
            ESP_LOGI(TAG, "Active: device=%d ready=%d stopped=%d rounds=%u pending=%d",
                     !!state.device, state.ready, state.stop, state.round, pending());
            state.heartbeat = now;
            if (bridge) {
                ESP_LOGI(TAG, "USB RX=%" PRIu64 " TX=%" PRIu64, state.usb_rx_bytes, state.usb_tx_bytes);
                byte_bridge_log_status();
            }
        }
    }
}

void pl2303_echo_run(void) { run(false); }
void pl2303_bridge_run(void) { run(true); }
