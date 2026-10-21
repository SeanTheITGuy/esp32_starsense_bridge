# StarSense USB-to-Bluetooth adapter

This ESP-IDF project implements a transparent **PL2303 HXD ↔ Bluetooth SPP bridge**
plus independent UART binary echo, USB enumeration, and handset echo modes.
The current Bluetooth module is a **JDY-31 named CELESTRON**, address
`DD:13:04:0F:47:74`, with UART RX16/TX17 at **115200 8N1** and working
active-high STATE on GPIO18. The handset remains at **9600 8N1**.
JDY-31 unsolicited UART reports were disabled with verified `AT+ENLOG0`.
All 60 safe handset echo transactions passed across three Bluetooth sessions.
See [JDY-31 hardware results](docs/hardware/jdy31.md).

The former HC-05 emitted disconnect text and its STATE output stayed low;
the replacement resolves those observed module issues. Manual azimuth movement and stopping have been physically confirmed on the
powered base. Aligned GoTo and KStars/INDI operation remain untested. A fresh configuration defaults to the independent UART test;
use the bridge configuration below for forwarding.

## Build and flash (Ubuntu)

For isolated module diagnosis, select the `sdkconfig.hc05-diag.defaults`
configuration with build directory `build-hc05-diag` and SDKCONFIG
`sdkconfig.hc05-diag`, using the same pattern as the bridge command below.
The diagnostic probes JDY-31 identity and bare AT at several UART rates, then
queries the identified module. Its default configuration is read-only, does not
drive KEY/EN, and never starts USB host. The optional `JDY31_DISABLE_STATUS`
setting permits only `AT+ENLOG0` after a JDY-31 version match and verifies readback.
It preserves module baud, name, PIN, and pairing. The diagnostic restores the
ESP32 UART to 9600 afterward; it does not change the module's baud.

For the transparent bridge image after wiring verification:

```bash
export IDF_TOOLS_PATH="$PWD/.toolchains/espressif"
. .toolchains/esp-idf-v6.1/export.sh
idf.py -B build-bridge -D IDF_TARGET=esp32s3 \
  -D SDKCONFIG="$PWD/sdkconfig.bridge" \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.bridge.defaults' build
idf.py -B build-bridge -p /dev/serial/by-id/usb-1a86_USB_Single_Serial_585A098289-if00 flash monitor
```

Wait for `BRIDGE READY` (ten seconds after USB serial setup) before opening
the client. The flashed bridge and normal defaults disable payload hex logging.

The bridge uses 128 queued chunks of up to 64 bytes in each direction, in
addition to UART driver buffering. USB IN pauses when its destination queue is
full; UART overflow stops the USB session explicitly because this HC-05 wiring
has no RTS/CTS throttling. Bytes are copied without delimiters or interpretation.
Queues are invalidated on USB loss or Bluetooth STATE falling. Bytes already
committed to a physical endpoint cannot be recalled. Traffic before USB readiness
or while STATE is low is discarded; clients must reconnect/retry their own
transactions. An OUT transfer with uncertain delivery is never auto-replayed.

STATE gating prevents forwarding while Bluetooth is disconnected. It defaults
to unconfigured/fail-closed unless the bridge defaults select GPIO18. The JDY-31
has been observed asserting STATE during RFCOMM connections. Module-generated
UART reports must also be disabled: reports emitted while STATE is high could
otherwise corrupt telescope traffic. Payload filtering is deliberately absent.

Once ready, test only the safe echo request through Bluetooth:

```bash
python3 tools/handset_echo.py --bluetooth DD:13:04:0F:47:74 --channel 1 --rounds 20
```

The direct RFCOMM socket avoids the privileged `/dev/rfcomm0` binding step during
development. For INDI, bind the verified SPP channel with:

```bash
sudo rfcomm bind 0 DD:13:04:0F:47:74 1
```

Then select Celestron GPS and `/dev/rfcomm0` in Ekos/INDI after normal StarSense
alignment on a powered mount. Coordinate queries, controlled GoTo, and alignment
preservation are still hardware acceptance tests, not verified results. Never
run `hc05_roundtrip.py` against the handset/bridge: its arbitrary binary payloads
belong only to the independent UART echo image with the handset disconnected.

For a workspace-local ESP-IDF/toolchain installation and a dedicated HC-05-only
build, install Ubuntu prerequisites from Espressif's installation guide, then
run the helper in your normal PC terminal:

```bash
bash tools/setup_hc05.sh /dev/ttyACM0
```

Confirm the port belongs to the ESP32's **COM** connector first. The helper
downloads ESP-IDF v6.1 and the S3 toolchain under `.toolchains`, builds with
UART1 RX16/TX17 at 9600, then flashes and opens the monitor. Omit the port for
installation/build only. It regenerates a separate `sdkconfig.hc05` and uses
`build-hc05`; USB host mode is disabled. Network access and serial-device
permissions are required. The helper successfully installed the toolchain and
built the HC-05 image on 2026-10-21 after host access was restored.

Use ESP-IDF v6.1, the current stable documentation target at preparation time. See [Espressif installation instructions](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/get-started/index.html). Install its Ubuntu prerequisites and ESP32-S3 toolchain; for a manual checkout:

```bash
git clone --branch v6.1 --recursive https://github.com/espressif/esp-idf.git ~/esp-idf
cd ~/esp-idf
./install.sh esp32s3
. ./export.sh
cd /path/to/usb_starsense_bridge
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Choose the actual **COM** serial port, not the native USB connector. Menuconfig → StarSense HC-05 UART test sets baud/pins. Defaults: UART1 RX16/TX17, 9600 8N1, no flow control, 4096-byte RX/TX buffers. Console diagnostics use UART0 through COM at the ESP-IDF default console baud (115200). Exit monitor with Ctrl-]. Flash size is 16 MB; PSRAM is unused until the exact board is confirmed. Confirm actual board memory specification before flashing.

ESP-IDF v6.1 and its ESP32-S3 toolchain are installed locally in `.toolchains`.
The HC-05 image compiled successfully and was flashed with all image hashes
verified on 2026-10-21. After a full power cycle, a direct Bluetooth RFCOMM test
echoed all 256 byte values correctly, verifying application operation and UART
RX16/TX17 at 9600 8N1. See [board bring-up evidence](docs/hardware/esp32-bringup.md).
Automatic reset after flashing left this board in ROM download mode; physical
RESET/EN did not clear it in the observed tests. A complete power cycle with
COM closed restored application execution. The exact reset-circuit cause is
not yet established. Avoid assuming a successful flash also proves app startup.

Project layout: `main/main.c` selects the bring-up application;
`main/hc05_test.c` runs binary echo; `components/usb_probe` enumerates USB;
`components/hc05_uart` owns UART1; `tools/hc05_roundtrip.py` checks Bluetooth
round trips; `tools/handset_echo.py` checks the handset directly through Linux;
`docs/pl2303-research.md` records the native USB implementation plan.
User-supplied kernel output confirms `067b:2303`, revision `4.00`, bound to
Linux's `pl2303` driver. Stored descriptors indicate HXD; see
[hardware evidence](docs/hardware/handset-usb.md).

## Wiring and power

For the current permanent JDY-31 assembly, use the dedicated
[wiring documentation](docs/wiring/README.md): direct-soldered mini-B cable,
GPIO connections, power/jumper checks, module settings and acceptance tests.


| HC-05 ZS-040 | ESP32-S3 / supply |
| --- | --- |
| TXD | GPIO16 / UART1 RX |
| RXD | GPIO17 / UART1 TX |
| GND | Common ground |
| VCC | Verified suitable 5 V supply for this breakout |
| STATE | GPIO18 for bridge mode; verify active-high 3.3 V output |
| KEY/EN | Unconnected |

UART signals are 3.3 V. Confirm GPIO16/17 are exposed and unused on the actual board. Confirm the breakout's regulator and supply labeling; do not apply 5 V to UART pins. Use ordinary SPP data mode. The current JDY-31 uses 115200 8N1; the original HC-05 passed independent tests at 9600. Changing firmware baud does not reconfigure the module. No unsolicited bytes or logging are sent to UART1.

Keep the telescope disconnected during UART testing. The board's USB connector pinout and VBUS power path are unknown. Before USB host testing, obtain its schematic or measure continuity with power removed; identify native D−19/D+20, VBUS, any protection/switching, and the relationship between COM power, board 5 V and native VBUS. Verify the intended supply under load and verify there is no path backfeeding another power source. Use an appropriately current-limited/protected host VBUS supply with reverse-current protection as required by the actual circuit. This repository does not prescribe an unverified board power connection.

## Linux HC-05 test

Pair with BlueZ using `bluetoothctl`: `power on`, `agent on`, `default-agent`, `scan on`, then `pair MAC` and `trust MAC` for the identified HC-05. Use the module's configured PIN. These are manual commands; replace MAC. Determine the advertised SPP RFCOMM channel (for example `sdptool browse MAC` if available); do not assume channel 1. With BlueZ RFCOMM utilities installed:

```bash
sudo rfcomm bind 0 MAC CHANNEL
python3 -m venv .venv
. .venv/bin/activate
pip install pyserial
python tools/hc05_roundtrip.py /dev/rfcomm0
sudo rfcomm release 0
```

Grant your user serial-device access according to Ubuntu's device permissions (commonly the dialout group, followed by logout/login). Binding alone does not establish the link: opening the device triggers connection. Utilities and pairing behavior depend on Ubuntu/BlueZ version. The RFCOMM serial baud argument does not program the HC-05's physical UART baud.

The checker sends all 256 byte values including NUL, XON/XOFF and high-bit bytes, then deterministic varied payloads. Firmware returns exactly the received bytes. PASS verifies both UART directions through Bluetooth for this stop-and-wait test. Repeat after closing/reopening the port and reconnecting Bluetooth; power-cycle the module and repeat. Inspect COM for overflow or framing errors. The UART-only test does not use STATE. The full bridge requires a working STATE input because this module emits UART text when Bluetooth disconnects. The standalone test does not certify continuous throughput or USB backpressure behavior.

## Staged acceptance

1. Record board model/schematic, available pins, verified power topology, HC-05 settings, and handset Linux descriptors (see research notes).
2. Build/flash UART test; require repeated binary PASS and reconnect PASS with no COM errors.
3. Add USB enumeration-only firmware after power verification; log device/configuration/interface/endpoint descriptors and repeat hotplug/power-cycle tests.
4. Select and implement only the identified PL2303 variant; verify initialization/line settings and independent `Kx` → `x#` with the powered, ready handset. Confirm actual baud, flow-control and DTR/RTS requirements.
5. Only after both interfaces pass, replace test echo with independent buffered USB↔UART paths. Verify bytes, ordering, short packets, disconnect cancellation, bounded buffers and explicit overflow/error reporting. Never replay stale commands after reconnect.
6. Verify `Kx` → `x#` through RFCOMM, then align normally with StarSense and use INDI **Celestron GPS**, serial port `/dev/rfcomm0`. Confirm coordinates before any controlled small GoTo, watch clearance, and repeat after reconnect. The handset maintains alignment; do not access AUX or replace its alignment model.

The UART echo image must be replaced before stages 4–6. Full bridge functionality is not yet implemented.

## USB enumeration-only image (stage 3)

After verifying the board's VBUS supply and protection, select menuconfig →
StarSense bring-up → Independent interface test → USB descriptor enumeration
only. Build and flash through COM as above. The native USB connector then serves
as the host; HC-05 UART is not initialized in this mode. The USB dependency is
pinned to Espressif `usb` 1.6.0 for ESP-IDF 6.1; the component manager needs
network access on the build machine to resolve/download dependencies. Commit
the generated dependency lockfile when a real build has resolved it.

Alternatively, use the dedicated enumeration build configuration:

```bash
export IDF_TOOLS_PATH="$PWD/.toolchains/espressif"
. .toolchains/esp-idf-v6.1/export.sh
idf.py -B build-usb-enum -D IDF_TARGET=esp32s3 \
  -D SDKCONFIG="$PWD/sdkconfig.usb-enum" \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.usb-enum.defaults' build
idf.py -B build-usb-enum -p /dev/serial/by-id/usb-1a86_USB_Single_Serial_585A098289-if00 flash monitor
```

This configuration selects enumeration only and does not replace the separate
HC-05 build configuration. An activity message repeats every ten seconds on
COM so application execution can be confirmed even if its startup log was
missed. If flashing leaves the board in the ROM downloader, close COM and
fully power-cycle the board before reopening the monitor.

Expect `USB host initialized`, then device VID:PID, USB/device revisions, EP0
size, configuration, interfaces, and endpoint details. For the stored handset
descriptors, expect `067b:2303`, revision `0400`, interface 0, interrupt IN `81`,
bulk OUT `02`, bulk IN `83`, and an HXD descriptor-match message. This match is
diagnostic only, not evidence of working serial initialization. Disconnect and
reconnect repeatedly, including power cycling the ready handset; each connection
should produce a fresh descriptor dump and each removal a disconnection log.
Save the COM output with the board/power verification record.

The application opens devices for inspection and closes them immediately;
it does not claim interfaces or submit serial data transfers. Normal USB
enumeration still issues the standard USB requests needed to configure a device.
Host installation does not provide an external 5 V supply or configure an
unknown board-specific VBUS switch. Return to HC-05 mode to repeat stage 2.

Enumeration and UART modes have both been built and flashed. The handset
successfully enumerated as `067b:2303`, HXD
descriptor match, with all three endpoints matching the Linux capture. Hotplug
removal/reconnection also passed without restarting the S3. PL2303 serial
communication now passes in the separate HXD echo image described below.

## PL2303 HXD independent echo image (stage 4)

```bash
export IDF_TOOLS_PATH="$PWD/.toolchains/espressif"
. .toolchains/esp-idf-v6.1/export.sh
idf.py -B build-pl2303-echo -D IDF_TARGET=esp32s3 \
  -D SDKCONFIG="$PWD/sdkconfig.pl2303-echo" \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.pl2303-echo.defaults' build
idf.py -B build-pl2303-echo -p /dev/serial/by-id/usb-1a86_USB_Single_Serial_585A098289-if00 flash monitor
```

This mode accepts only the measured HXD descriptor family, parses/claims its
interface, checks all initialization transfer results, sets 9600 8N1 with
hardware/software flow control off and DTR/RTS deasserted, and receives bulk
data and interrupt status independently. It sends five `Kx` echo commands per
connection, checks for exactly `x#` (including replies split across transfers),
then stops sending commands. HC-05 UART is disabled. A new USB connection starts
a fresh test. Baud, control lines, and round count are menuconfig options under
StarSense PL2303 echo test; changing them requires rebuilding this configuration.
The initial wait after USB setup is configurable with
`CONFIG_PL2303_TEST_SETTLE_MS` (default 10000 ms) to allow a USB-powered handset
to finish booting. This delay belongs only to the diagnostic; it is not added
to individual serial transfers.

On 2026-10-21 all five requests passed with the handset on its normal screen,
powered through USB and disconnected from the mount. This verifies those serial
settings for this handset's echo command. It does not verify mount control or
alignment. The implementation retains transfer buffers through completion;
disconnect/error stops submissions and drains endpoints before releasing them.
It does not automatically retry OUT transfers with uncertain delivery, probe
other baud rates, issue GET_LINE/BREAK clone probes, or support HXN/other variants.
A stuck control request can require physical removal; deadlines never free
in-flight buffers. See `diagnostics/pl2303-echo.log` and the hardware record.
