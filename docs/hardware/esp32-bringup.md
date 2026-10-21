# ESP32-S3 bring-up evidence

Read directly through the COM interface on 2026-10-21:

- Stable serial path: `/dev/serial/by-id/usb-1a86_USB_Single_Serial_585A098289-if00`
- Current kernel serial node: `/dev/ttyACM0`
- esptool 5.4.0 identifies ESP32-S3 QFN56 revision v0.2.
- Embedded PSRAM: 8 MB AP_3v3; crystal: 40 MHz.
- Detected flash: 16 MB, manufacturer `85`, device `2018`.
- Flash eFuse configuration: quad data lines, 3.3 V.
- MAC: `64:e8:33:54:52:f8`.

These reads confirm the processor and memory identification. They do not
identify the generic development board's schematic or verify GPIO availability,
external power input, VBUS supply, or reverse-current protection. USB host
connection to the handset remains pending electrical verification.

## Firmware installation and flash

ESP-IDF v6.1.0 was installed under `.toolchains/esp-idf-v6.1`, with compiler and
Python dependencies under `.toolchains/espressif`. `tools/setup_hc05.sh` built
successfully with UART1 RX16/TX17 at 9600 8N1, UART0 console at 115200, and USB
host disabled. The resolved USB component is 1.6.0 (see `dependencies.lock`).

Bootloader, partition table, and application were written through the stable
COM path using esptool 5.4.0 at 460800 baud. All three flash hashes verified.
Application image: `build-hc05/starsense_bridge.bin`, 193264 bytes, SHA256:
`11485b1242b1b46c23b05edf50b233ec31b035a1075aa4e89c9affb9bb8654d5`.

Automatic RTS reset did not yield console data. A subsequent bootloader
connection also received no serial data. Runtime startup is not yet verified;
manual RESET/EN has been requested, with output capture in
`diagnostics/hc05-boot.log`.

A subsequent physical RESET/EN while continuously monitoring confirmed:
`rst:0x1 (POWERON),boot:0x0 (DOWNLOAD(USB/UART0))`, followed by
`waiting for download`. Thus the chip is entering the ROM downloader rather
than starting the application. A clean power cycle with COM closed is the
next isolation step; board boot-strap and auto-reset wiring remain unverified.

Bluetooth discovery identifies HC05 at `98:D3:31:F5:A8:D7`, paired/bonded/trusted,
with SPP service `SPP Dev` on RFCOMM channel 1. A direct RFCOMM socket connected,
but a binary echo request timed out. This does not verify the UART baud or
wiring. Creating `/dev/rfcomm0` requires privileges unavailable without sudo
authentication; direct RFCOMM testing does not require that device node.

## Successful application verification after power cycling

After the user fully power-cycled the ESP32/HC-05 with COM closed, a direct
RFCOMM socket to the verified SPP channel echoed `bytes(range(256))` exactly.
No COM device was opened during this test. This verifies that the flashed
application runs, the UART path on GPIO16/17 works in both directions at 9600
8N1, and all byte values survive the Bluetooth/UART path. The earlier echo
timeout was observed while the chip was in the ROM downloader.

The precise reason automatic and physical EN reset retained download mode
has not been determined. Full power cycling resolved it; no board boot wiring
fault has been established. Historical ROM output remains in
`diagnostics/hc05-boot.log`; it is not a current application startup log.

Repeated test: two independent RFCOMM sessions, 20 transactions per session,
2859 + 2358 = 5217 bytes echoed exactly, including an all-byte-values payload in
each session. No unexpected trailing bytes were observed. Closing the socket,
waiting two seconds, and reconnecting succeeded. Full results are in
`diagnostics/hc05-bluetooth.log`. This verifies stop-and-wait traffic and one
Bluetooth reconnection; it does not certify unlimited sustained throughput.

## USB enumeration image

With the handset disconnected, the separate `build-usb-enum` image was built
and flashed successfully on 2026-10-21. All flash hashes verified. Application
size: 220560 bytes; SHA256:
`63aec8b7714cd633195bb62a2af46c9f7dc0d1e892501f31bde078242c2ef738`.

COM capture confirmed normal `SPI_FAST_FLASH_BOOT`, application startup,
`USB host initialized; enumeration only; HC-05 UART disabled`, and the message
that no serial commands are sent. Output is in `diagnostics/usb-enum.log`.
This flash booted successfully after automatic reset, unlike the earlier UART
bring-up observation. The underlying cause of that earlier behavior remains
unconfirmed. Handset enumeration is the next hardware test.

The user reports verifying the power path, setting a VIN-to-VBUS jumper to
obtain 5 V, correcting reversed wiring, and restoring normal handset startup.
Those electrical checks are user-reported; no board schematic has been supplied.

## Successful handset enumeration through ESP32-S3

The user connected the handset to the native host connection. At host uptime
581342 ms, the S3 logged device address 1, VID:PID `067b:2303`, bcdUSB `0110`,
bcdDevice `0400`, class `00`, and EP0 size 64. The HXD descriptor match passed.
Configuration 1 declares 39 bytes, one interface, attributes `80`, and 100 mA.
Interface 0, alternate 0, class/subclass/protocol `ff/00/00` has:

| Endpoint | Transfer | Maximum packet | Interval |
| --- | --- | --- | --- |
| `81` | Interrupt IN | 10 bytes | 1 |
| `02` | Bulk OUT | 64 bytes | 0 |
| `83` | Bulk IN | 64 bytes | 0 |

These agree with the Linux descriptors. No enumeration errors were present in
the capture. This verifies initial native-host enumeration after application
startup; disconnect/reconnect, handset power-cycle recovery, and PL2303 serial
communication are not yet verified. No PL2303 initialization or serial command
was sent. Raw output is retained in `diagnostics/usb-enum.log`.

Reopening COM caused a new S3 startup, with the handset still attached. It
enumerated again at uptime 672 ms with identical descriptors. Enumeration is
therefore observed both for connection after boot and for an attached handset
at host startup. Cable disconnect/reconnect without resetting the S3 remains
to be tested.

Cable hotplug passed without an S3 restart: at uptime 32112 ms the client
reported removal of device address 1; at 36102 ms the handset enumerated as
address 2 with identical HXD/interface/endpoint descriptors. Periodic activity
logs continued at 30302 and 40332 ms, with no intervening boot log or USB error.
This verifies one removal/reconnection cycle for the enumeration-only image.
PL2303 initialization and serial I/O remain unimplemented and unverified.

## Successful independent HXD serial test

The separate HXD echo image built/flashed with all hashes verified. Image size:
230496 bytes; SHA256:
`7c3acac04348bc6e374922ad5d6872116804b736d8c6e1b9ab567efc0a857eef`.

All 17 initialization control transfers completed with expected lengths. The
handset returned exactly `x#` to five consecutive `Kx` requests at 9600 8N1,
flow control disabled, DTR/RTS deasserted. Responses were split into one-byte
bulk completions, handled successfully. No unexpected serial bytes or transfer
errors were observed. Log: `diagnostics/pl2303-echo.log`.

The user confirmed the handset displayed its normal screen and was not
connected to a powered mount. This verifies USB-only echo, not telescope/mount
operation. HC-05 UART is disabled in this image. Driver-level disconnect/drain
and initialization after reconnect remain to be tested.

First driver hotplug attempt: disconnect at uptime 68959 ms drained all
outstanding transfers and released the session. Device address 2 was opened at
72799 ms, and all setup requests completed at 72919 ms without an S3 restart.
The first echo at 73429 ms timed out at 76429 ms; error cleanup also released
the session. Thus resource cleanup/reinitialization passed, but serial echo
after that reconnect did not. A possible cause is handset application startup
after USB power cycling: the original test waited only 500 ms after setup.
This explanation is a hypothesis, not yet established. The diagnostic now has
a configurable initial settle interval, default 10000 ms, for a controlled
repeat. It still never retries an OUT transfer with uncertain delivery.

The updated diagnostic was rebuilt and flashed with all hashes verified:
230576 bytes, SHA256
`2138ab82c54e11ac6b95c94d567528b60d8673515789b49d7916036a83d726a3`.
With the handset already attached, all five echoes passed after the new
10000 ms settle interval. Log: `diagnostics/pl2303-settle.log`. A fresh cable
hotplug with this interval is still needed to test the startup-timing hypothesis.

The repeat passed: disconnect at 32319 ms released the session; address 2 was
opened at 37489 ms and configured at 37609 ms. Following the 10000 ms settle,
all five echoes passed between 47619 and 48659 ms. No S3 restart or transfer
error occurred. This supports (but does not independently measure) handset
startup timing as the reason for the earlier 500 ms test failure. Native HXD
serial communication and one complete driver hotplug/echo cycle are verified.

## Original HC-05 bridge bring-up (historical)

The bridge was implemented, built and flashed with bounded bidirectional queues,
USB receive backpressure, UART error reporting, and session queue invalidation.
The initial image passed 20 `Kx`/`x#` transactions through Linux RFCOMM, HC-05,
UART, S3 USB host and the handset. The next Bluetooth session timed out.

COM payload tracing identified a 19-byte UART message when the module
disconnected: `+DISC:SUCCESS\r\nOK\r\n`. The initial ungated bridge forwarded this
to the handset. This is observed module behavior, not an ESP32-generated log
or a guessed protocol framing convention. No payload filtering was added.

The user connected HC-05 STATE to GPIO18. A new build checks that active-high
input in both UART paths and clears queued session bytes on a falling state.
The UART status text is now discarded while STATE is low. Current image size:
265104 bytes; SHA256
`fd4d197d4ed399496809690fe1444d5d87d183cfd56ed9056aa3bf0df15b2236`.
All flash hashes verified. Development COM hex tracing is temporarily enabled.

However, GPIO18 remained low through a confirmed RFCOMM connection, and the
user measured 0 V on both the HC-05 STATE pad and the ESP32 GPIO18 header pin
while Linux still reported Connected=yes and the test socket was held open.
GPIO18 also remained low after explicit Bluetooth disconnection. No inverted
polarity has been established. Forwarding currently fails closed. Module
pin/wiring/firmware identification and an unloaded STATE measurement are needed
before further reconnect or mount testing. Logs: `diagnostics/bridge-trace.log`,
`diagnostics/bridge-state.log`, and `diagnostics/bridge-state-echo.log`.

With STATE disconnected at the ESP32 end, the user measured the HC-05 STATE
pin at 0 V both during a held, confirmed RFCOMM connection and after explicit
Bluetooth disconnect. This does not establish inverted polarity; the output
has not been observed to change at all. The ESP32 load is not responsible for
the connected-state low reading. Module identification/pin routing/configuration
remain possible causes, rather than a diagnosed defective module.

An isolated read-only diagnostic was then built/flashed with all image hashes
verified. It does not initialize USB host or drive KEY/EN. `AT\r\n` received no
response at either 9600 or 38400 in the current hardware mode; no identity or
configuration queries were sent afterward, and no module settings were changed.
The ESP32 UART was restored to 9600. At that stage, firmware was the diagnostic
rather than the bridge. Output: `diagnostics/hc05-at.log`. Exact module photos/identification
are requested before instructing hardware AT-mode entry or configuration writes.


## Current JDY-31 bridge image

The HC-05 was replaced with a JDY-31 named CHORUS, address
`DD:13:04:0F:47:74`, firmware V1.34. Its actual UART baud is 115200
(`AT+BAUD` reports 8), and its STATE output works on GPIO18. Isolated
provisioning disabled unsolicited UART reports with `AT+ENLOG0` and verified
`+ENLOG=0`. Name, PIN, baud and pairing were preserved.

The bridge was rebuilt/flashed with UART115200, handset9600 8N1,
DTR/RTS deasserted, STATE18 and ten-second startup guard. Payload tracing is
disabled. All flash hashes verified. Three separate RFCOMM sessions passed
20 exact Kx/x# transactions each: 60 total, 120 bytes in each direction.
COM shows every STATE transition and no reported UART/bridge faults.
This supersedes the old HC-05 STATE blocker and diagnostic image above.

See [JDY-31 results](jdy31.md), `diagnostics/jdy31-setup.log`,
`diagnostics/jdy31-final-bridge.log`, and `diagnostics/jdy31-reconnect-echo.log`.
The handset remains disconnected from a powered mount. INDI coordinate queries,
GoTo and retention of StarSense alignment still require hardware acceptance.
