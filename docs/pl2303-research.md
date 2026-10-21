# PL2303 host research and implementation gate

## Required evidence

Before choosing vendor initialization, connect the unmodified handset directly to Linux and collect:

```bash
lsusb
sudo lsusb -v -d 067b:
sudo dmesg | tail -50
```

If its VID differs, repeat descriptor collection using its actual VID:PID. Record bcdUSB, bcdDevice, bDeviceClass, bMaxPacketSize0, interface numbers/alternate settings, endpoint types/addresses/max packet sizes/intervals, and kernel version/driver identification. VID:PID alone may not distinguish revisions. Provide board model/schematic or clear board identification and measured VBUS topology separately. Serial baud and control-line requirements also need confirmation; descriptor data does not establish them.

## Findings and sources

- [Espressif USB Host Library](https://docs.espressif.com/projects/esp-usb/en/latest/esp32s3/usb_host.html): use native OTG and the host/client event model. Plan a library daemon task and client event task; keep callbacks short. Claim the selected interface and allocate transfers via host APIs. Inspect the release-specific USB component packaging before adding dependencies to the v6.1 project. No generic CDC assumption is made.
- [Linux pl2303.c](https://github.com/torvalds/linux/blob/master/drivers/usb/serial/pl2303.c): current code has H, HX, TA, TB, HXD, HXN and MP type handling. Detection and quirks matter; HXN uses different vendor requests and reset/flow-control behavior. Review `pl2303_detect_type`, startup/open, line configuration and notification processing against the recorded descriptors. A universal legacy initialization sequence is inappropriate.
- [USB Host Shield cdcprolific.cpp](https://github.com/felis/USB_Host_Shield_2.0/blob/master/cdcprolific.cpp) and [header](https://github.com/felis/USB_Host_Shield_2.0/blob/master/cdcprolific.h): `PL2303_COMPAT` enables descriptor heuristics and a legacy sequence of vendor reads/writes with an HX-specific branch. It does not establish compatibility with every Prolific variant. The Arduino host-controller implementation is not directly usable on native ESP-IDF.
- [Espressif VCP service](https://components.espressif.com/components/espressif/usb_host_vcp): provides a driver registration abstraction. Its presence is not evidence that it supports this handset's Prolific chip; check available drivers and exact revision support before selecting dependencies.

Linux and USB Host Shield source have GPL-2.0 licensing notices. No driver source has been copied into this project. Preserve applicable licensing and attribution if adapting code; settle licensing before incorporating a derivative driver.

## Proposed driver scope after identification

1. Allowlist verified device/type; log and reject unsupported or ambiguous devices. Parse descriptors rather than hardcode endpoint addresses. Require usable bulk IN/OUT; inspect any interrupt notification endpoint.
2. Perform type-specific control transfers, checking completion status and actual lengths. Configure verified baud/8N1, flow control and control lines. Treat 9600 as a test candidate pending evidence.
3. Keep bulk IN submitted; deliver actual received byte counts, including short packets, without parsing telescope data. Submit ordered bulk OUT chunks and retain buffers through completion. Service status notifications separately if required.
4. On disconnect, stop submissions, cancel/drain transfers according to the selected stack API, release interfaces/device only once callbacks are finished, discard stale session data, and return to enumeration. Recover transfer failures with bounded retries; never blindly retransmit an OUT transfer with uncertain delivery.
5. Define explicit finite-buffer/backpressure behavior. USB IN can pause; HC-05 UART without RTS/CTS cannot be throttled indefinitely. Detect/report overflow rather than claim lossless operation under unlimited input. Optional STATE can later provide module-specific connection information after electrical verification.

The initial stage gated PL2303 initialization on handset identification.
That gate is now satisfied: the HXD echo implementation and its hardware
results are recorded below. Full bridge forwarding is now implemented; final
connection-state hardware verification remains pending.

## Native host integration plan

Keep the UART-only image as the default bring-up application. Enumeration is
implemented as an explicit alternative menuconfig mode, so a UART test does
not enable the host peripheral. The component responsibilities are:

| Component | Responsibility |
| --- | --- |
| `hc05_uart` (present) | UART1 configuration and buffered byte I/O |
| `usb_probe` (present) | Enumeration-only descriptor inspection and hotplug logging |
| `pl2303_host` (present, echo test) | Host lifecycle, descriptor validation, HXD setup, asynchronous transfers |
| `byte_bridge` (present) | Two bounded byte paths, STATE gating, session/error accounting |

For the host implementation, follow the release-specific
[Host Library lifecycle](https://docs.espressif.com/projects/esp-usb/en/latest/esp32s3/usb_host.html):
install with `usb_host_install`; service `usb_host_lib_handle_events` in a daemon
task; register a client and service `usb_host_client_handle_events` in its task.
Callbacks should record completion/events and return without blocking on UART
or waiting for another USB completion in that same task.

Use a state machine: waiting → open/inspect → claim → initialize → ready →
draining → waiting. Open on NEW_DEV, obtain device/active configuration
descriptors, and claim the validated interface. Allocate control/bulk/interrupt
transfer buffers through `usb_host_transfer_alloc`. Control setup occupies the
first eight buffer bytes; submit with `usb_host_transfer_submit_control`.
Use `usb_host_transfer_submit` for bulk and notification endpoints. Retain each
buffer until completion. Size bulk IN requests to endpoint packet multiples;
forward only actual received payload lengths. A zero-length completion is not
a disconnected serial session.

On DEV_GONE or a fatal session error, stop resubmission, halt and flush claimed
endpoints as supported by the selected host release, and keep servicing events
until every submitted transfer is accounted for. Only then release interfaces,
close the device, and free transfer objects. Increment a session generation so
old completions and queued commands cannot leak into the next handset session.
The host API currently documents asynchronous transfers without transfer
timeouts: application deadlines require a cancellation/cleanup policy, not
freeing an in-flight buffer when a timer expires.

The stored descriptors suggest HXD, but firmware must inspect the actual device
again. Compare Linux `pl2303_detect_type`, `pl2303_startup`, `pl2303_open`, and
line/status handling before implementing the selected control sequence. Keep
HXN requests out of the HXD path. Treat GET_LINE stalling as a possible known
clone quirk only after recovering EP0 correctly; do not ignore unrelated control
errors. `PL2303_COMPAT` is a compile-time behavior in the Arduino library, not
an ESP-IDF configuration option.

Acceptance before connecting the two transports: repeated native-host hotplug
enumeration and independent handset echo; repeated HC-05 binary echo including
reconnect; recorded board VBUS and pin verification. No full bridge is enabled
by this plan.

## Connected handset evidence

[Captured handset descriptors](hardware/handset-usb.md) identify 067b:2303, bcdUSB
0x0110, bcdDevice 0x0400 and EP0 size 64. Linux's detection logic selects TYPE_HXD.
Interface 0 has interrupt IN 0x81 (10 bytes), bulk OUT 0x02 (64 bytes) and bulk IN
0x83 (64 bytes). The S3 has independently enumerated this layout and passed
enumeration-only hotplug recovery. HC-05 binary/reconnect tests also passed.

## HXD echo implementation and observed result

`components/pl2303_host/pl2303_echo.c` implements a native asynchronous state
machine. The HXD request values were researched from Linux startup/open/termios
and USB Host Shield compatibility handling; no upstream driver code was copied.
The implementation sends the H/HX-family vendor unlock sequence with the
non-legacy HXD value, resets the two data pipes, sets seven-byte line coding,
reads/modifies the flow register to preserve non-flow bits while disabling
flow control, and applies explicitly configured DTR/RTS. It avoids GET_LINE and
BREAK requests altogether, so no clone-stall workaround is needed for this test.
Unexpected setup stalls fail the session rather than being silently ignored.

Actual hardware completed all setup requests and returned exactly `x#` for five
`Kx` requests at 9600 8N1, flow control disabled, DTR/RTS deasserted. Each reply
arrived as two separate one-byte bulk IN completions; the test assembled them
correctly. The handset was powered through USB, displayed its normal screen,
and was not connected to a powered mount. Driver disconnect/drain/reinitialize
and echo passed with the ten-second diagnostic startup guard. The bridge passed
an initial Bluetooth echo session; an observed HC-05 disconnect UART message
required working STATE gating before reconnection acceptance. The replacement
JDY-31 asserts GPIO18 correctly; with its UART at 115200 and status reports
disabled, three Bluetooth sessions passed 60 exact handset echo transactions.
See [replacement hardware results](hardware/jdy31.md).
