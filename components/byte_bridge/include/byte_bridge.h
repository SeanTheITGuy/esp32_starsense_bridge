#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* All USB-side functions are called by the single host client task. */
void byte_bridge_init(void);
void byte_bridge_start(void);
void byte_bridge_stop(void);
bool byte_bridge_active(void);
bool byte_bridge_tx_idle(void);
bool byte_bridge_faulted(void);
bool byte_bridge_rx_room(void);
bool byte_bridge_from_usb(const uint8_t *bytes, size_t length);
size_t byte_bridge_to_usb(uint8_t *bytes);
void byte_bridge_log_status(void);
void byte_bridge_poll_link(void);
