# Recreating the StarSense Bluetooth adapter

This directory describes the **working, permanently wired JDY-31 assembly**.
The handset is unmodified. A cut USB mini-B cable is soldered to the ESP32-S3
headers; the board's native USB connector is not used for that cable.

Read these in order:

1. [Connections and diagram](connections.md) — exact signals, cable identification, printable net list.
2. [Power and board checks](power.md) — 5 V distribution, the VIN/VBUS jumper, and unresolved board details.
3. [Assembly, configuration and acceptance](assembly.md) — reproduce the firmware/module settings and test safely.

## Parts

| Item | Working configuration |
| --- | --- |
| MCU board | Generic ESP32-S3 N16R8 development board, separate COM and native USB connectors; 16 MB flash / 8 MB PSRAM |
| Bluetooth | JDY-31 UART Bluetooth Classic SPP module/breakout; firmware V1.34 on this unit |
| Handset cable | USB mini-B male cable, cut at the other end, conductors soldered to board headers |
| Handset | Original StarSense AutoAlign USB hand controller, PL2303 HXD `067b:2303`, device revision `0400` |
| Telescope | Celestron NexStar 8SE motorized base, handset connected normally |
| Power | Suitable regulated 5 V source and appropriately protected distribution; see power notes |
| Construction | Insulated joints, short USB data pair, strain relief, common ground |
| Development | Linux PC and cable to the board's separate COM connector |

This particular JDY-31 is named **CELESTRON**, MAC `DD:13:04:0F:47:74`,
SPP channel 1, PIN 1234. Another module will have its own MAC and may have
different initial settings. Source filenames and Kconfig options retaining
`hc05` refer to the UART transport; the final hardware uses JDY-31.

## Verified result

The permanent wiring passed handset USB initialization and **60 exact Kx/x#
echo transactions across three Bluetooth sessions**, with working STATE on
GPIO18 and no reported transport faults. Subsequent tests with the handset
connected to the powered base, without the StarSense camera, sent manual
azimuth movement and stop commands. At rate 7 for four seconds, the mount
reported approximately **4 degrees** of movement; the user confirmed visible
physical movement. Stop was acknowledged and subsequent positions were stable.

This verifies the transport and manual azimuth control. Altitude movement,
StarSense alignment retention, aligned GoTo and KStars/INDI integration remain
acceptance tests. See [test evidence](../hardware/jdy31.md) and the repository's
`diagnostics/` logs. No additional motor controller, AUX connection, internal
handset modification or ESP32 Bluetooth Classic implementation is involved.
