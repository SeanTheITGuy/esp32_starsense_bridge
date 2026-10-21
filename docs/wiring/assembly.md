# Assembly and commissioning

## Assemble

1. Identify the actual board's pins and power path using [power.md](power.md).
   Disconnect power while soldering.
2. Wire JDY-31 TXD→16, RXD←17, STATE→18, GND and suitable VCC.
3. Identify the cut mini-B cable conductors by continuity. Wire D−→19, D+→20,
   GND→GND and VBUS→verified host 5 V. Leave ID unconnected.
4. Check continuity against [connections.md](connections.md), inspect joints,
   insulate them and add strain relief. Verify the 5 V supply before attaching
   the handset. Do not rely on wire colors or a generic board's header layout.
5. Use the separate **COM** connector to flash/debug. The soldered handset cable
   occupies native USB; leave that board connector unused.

The handset remains intact. Its normal mount cable stays in its normal handset
socket on the mount. There is no bridge connection to the AUX bus.

## Firmware settings

Build the **bridge**, not the standalone UART echo image, for normal operation.
The independent UART image returns bytes to the Bluetooth client and does not
control the handset. File and option names containing HC05 are historical.

| Setting | Value in `sdkconfig.bridge.defaults` |
| --- | --- |
| Application | `CONFIG_BRINGUP_BRIDGE=y` |
| JDY UART baud | `CONFIG_HC05_BAUD=115200` |
| UART TX | `CONFIG_HC05_TX_GPIO=17` |
| UART RX | `CONFIG_HC05_RX_GPIO=16` |
| Connected STATE | `CONFIG_HC05_STATE_GPIO=18` |
| Handset baud | `CONFIG_PL2303_TEST_BAUD=9600` |
| DTR/RTS | `CONFIG_PL2303_TEST_CONTROL_LINES=0` |
| Handset settle time | `CONFIG_BRIDGE_STARTUP_MS=10000` |
| Payload hex logging | Disabled |

Both serial interfaces are 8N1 with flow control disabled. UART baud and handset
baud intentionally differ. The firmware does not set the JDY's UART baud for you.

Install ESP-IDF/toolchains as described in the [project README](../../README.md).
From the project root, with the local installation used for this project:

```bash
export IDF_TOOLS_PATH="$PWD/.toolchains/espressif"
. .toolchains/esp-idf-v6.1/export.sh
idf.py -B build-bridge -D IDF_TARGET=esp32s3 \
  -D SDKCONFIG="$PWD/sdkconfig.bridge" \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.bridge.defaults' build
idf.py -B build-bridge -p /dev/serial/by-id/YOUR_COM_DEVICE flash monitor
```

Use `ls -l /dev/serial/by-id/` to find the actual COM port. The tested board's
identifier was `usb-1a86_USB_Single_Serial_585A098289-if00`. Existing SDKCONFIG
values override defaults: inspect them or use menuconfig if reusing an older
build. In particular, an old HC-05 build may still select 9600 for the JDY UART.
The console is UART0 via COM at 115200; diagnostics never go to the JDY data UART.
Opening COM was observed to reset this board, so allow initialization to finish.

## JDY-31 configuration

The working module is JDY-31 V1.34, named CELESTRON, UART115200, PIN1234,
with UART status reports disabled. A new module needs its own identity and baud
verified. These are **module UART AT commands**, not commands to send through
Bluetooth or to the handset. Disconnect Bluetooth for configuration.
Every command ends in actual CR LF bytes (`\r\n`), not the four printed characters.

| Operation | UART command | Verified result / target |
| --- | --- | --- |
| Identify | `AT+VERSION` | This unit: `+VERSION=JDY-31-V1.34,Bluetooth V3.0` |
| Address | `AT+LADDR` | This unit: `+LADDR=DD13040F4774` |
| Read baud | `AT+BAUD` | This unit: `+BAUD=8` = 115200 |
| Set baud if necessary | `AT+BAUD8` | Documented 115200 setting; this unit already used it, so no baud write was needed |
| Disable status reports | `AT+ENLOG0` | `+OK`, then verify `AT+ENLOG` → `+ENLOG=0` |
| Set name | `AT+NAMECELESTRON` | `+OK`, then verify `AT+NAME` → `+NAME=CELESTRON` |
| Restart module | `AT+RESET` | Applies the advertised name; remote name became CELESTRON |

If changing baud on a new module, start at its verified current baud, then switch
the configuring UART to 115200 and verify the setting. Do not factory-reset a
working module: that can restore unwanted baud/status settings. Disabling status
reports is essential because generated connection text could otherwise enter
the transparent telescope stream. STATE gating remains enabled as well.

The project provides an isolated diagnostic mode to avoid sending AT text into
the handset. Build it separately using the same toolchain:

```bash
idf.py -B build-hc05-diag -D IDF_TARGET=esp32s3 \
  -D SDKCONFIG="$PWD/sdkconfig.hc05-diag" \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.hc05-diag.defaults' menuconfig
```

In the project options, confirm the Bluetooth diagnostic mode. Enable
`JDY31_DISABLE_STATUS` to disable reports and `JDY31_RENAME_CELESTRON` to rename
and restart after successful JDY identification. Both options default off in a
fresh configuration; inspect existing local configuration before flashing.
Then run `idf.py -B build-hc05-diag build` and flash through COM. The diagnostic
probes UART rates and never starts USB host. Its source is
[main/hc05_diag.c](../../main/hc05_diag.c). It does not implement automatic baud
reconfiguration for a new module. **Restore the bridge image afterward.**

Reference: [JDY-31 AT command manual](https://myosuploads3.banggood.com/products/20190517/20190517042108JDY31V1.3.pdf).

## Bring-up checks

1. Confirm normal handset startup and COM messages identifying HXD, completed
   initialization at 9600 8N1, then `BRIDGE READY` after the ten-second guard.
2. Pair the module on Linux with BlueZ. Use its actual MAC; this unit's name is
   CELESTRON and PIN is 1234. In `bluetoothctl`, use `power on`, `agent on`,
   `default-agent`, `scan on`, `pair MAC`, and `trust MAC`. Respond to its PIN
   prompt, then stop scanning. Determine the SPP channel; this unit uses 1.
3. With the bridge ready and no other client using the Bluetooth link:

   ```bash
   python3 tools/handset_echo.py --bluetooth DD:13:04:0F:47:74 --channel 1 --rounds 20
   ```

   Expect exactly `x#` for each `Kx`. Close/reopen the connection and repeat.
   COM should show Bluetooth STATE connected/disconnected, `fault=0`, and
   matching USB/UART byte counts. GPIO18 should change from low when disconnected
   to a 3.3 V logic high when connected.
4. Test handset USB removal/reconnection, wait for `BRIDGE READY` again and repeat
   echo. Independent HXD driver hotplug/echo previously passed; full bridge
   hotplug was not repeated after the permanent wiring change.
5. With the mount safely positioned and powered, test manual motion and stop
   through an appropriate telescope client while observing the hardware. Physical
   azimuth motion was confirmed at rate 7 for four seconds, approximately 4°.
   Lower-rate tests were too small to notice. Movement is not part of the echo
   checker, and a successful protocol reply alone is not visual confirmation.
6. For final telescope use, connect the StarSense camera, complete normal handset
   alignment, then configure Ekos/INDI **Celestron GPS** with `/dev/rfcomm0`:

   ```bash
   sudo rfcomm bind 0 DD:13:04:0F:47:74 1
   ```

   Check coordinates before a small controlled GoTo. Actual aligned GoTo and
   alignment preservation have not yet been verified on this assembly.

Never run `tools/hc05_roundtrip.py` against the bridge/handset: it sends arbitrary
binary test bytes and belongs only to the independent UART echo firmware with
the handset disconnected.

## Troubleshooting from the observed failures

| Symptom | Check |
| --- | --- |
| About 1 V on expected 5 V rail | Board power path and VIN/VBUS jumper; do not attach handset until actual VBUS is verified |
| No USB device | Common ground, VBUS, D−19/D+20 mapping, cable continuity and native-host firmware |
| Bluetooth connects but no handset echo | JDY actual baud versus firmware115200; crossed UART TX/RX; STATE18 high; BRIDGE READY |
| Next Bluetooth session fails | ENLOG must read 0; STATE must actually change; do not filter arbitrary payload bytes |
| Handset shows abnormal boot/debug screen | Disconnect and recheck all cable mappings; earlier miswiring coincided with abnormal boot, but no specific causal circuit diagnosis was made |
| COM says `waiting for download` | Release BOOT and power-cycle with COM closed; early reset behavior on this board was inconsistent |
| Name remains CHORUS after saving CELESTRON | Restart JDY with AT+RESET; Linux may also cache discovery names |
| Module never asserts STATE | Verify module pin/firmware; original HC-05 stayed at 0 V even when connected, whereas the replacement JDY-31 works |
