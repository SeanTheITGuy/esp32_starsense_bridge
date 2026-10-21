# JDY-31 replacement

The replacement module identifies as **JDY-31 V1.34, Bluetooth V3.0**, Bluetooth
name **CELESTRON** (formerly CHORUS), address **DD:13:04:0F:47:74**. It is paired/bonded/trusted on this
PC using PIN 1234. SDP exposes Serial Port Profile on RFCOMM channel 1.

Measured UART configuration is `AT+BAUD` → `+BAUD=8`, meaning **115200 baud**.
Firmware uses UART1 RX16/TX17, 8N1, no flow control. The handset USB serial side
remains at its independently verified **9600 8N1**. GPIO18 receives active-high
STATE; COM logs confirm connection and disconnection transitions.

The isolated provisioning diagnostic matched the JDY-31 version, read
`+ENLOG=1`, sent `AT+ENLOG0`, received `+OK`, and verified `+ENLOG=0`.
This disables module-generated boot/connect/disconnect UART messages without
filtering bridge payloads. Name, PIN, baud and pairing were not changed.
Evidence: `diagnostics/jdy31-baud.log`, `diagnostics/jdy31-setup.log`.

The bridge was rebuilt and flashed with UART115200, STATE18, handset9600,
ten-second handset startup guard, and payload hex tracing disabled.
Flash hashes verified. Runtime evidence: `diagnostics/jdy31-final-bridge.log`.
All **60 exact Kx → x# transactions across three separate RFCOMM sessions**
passed. USB and UART totals were exactly 120 bytes in each direction, with
STATE transitions on every connection/disconnection and no reported faults.
End-to-end reconnect test evidence: `diagnostics/jdy31-reconnect-echo.log`.
The handset displays its normal screen and is not on a powered mount;
coordinate queries, GoTo and alignment preservation remain untested.

Reference: [JDY-31 manual](https://myosuploads3.banggood.com/products/20190517/20190517042108JDY31V1.3.pdf).
Its backplane supply rating differs from the bare module: verify the actual
breakout regulator/label before applying 5 V. UART and STATE use 3.3 V logic.

User-requested rename: `AT+NAMECELESTRON` returned `+OK`;
`AT+NAME` read back `+NAME=CELESTRON`. The setup diagnostic restarts
the module with `AT+RESET` after successful readback to refresh its advertised
name. Evidence: `diagnostics/jdy31-rename.log` and
`diagnostics/jdy31-rename-reset.log`.

After the module restart, a Linux remote-name request (`hcitool name
DD:13:04:0F:47:74`) returned **CELESTRON**. The bridge image was then
restored with flash hashes verified.

## Permanent wiring retest

After the user replaced temporary wiring with a more permanent assembly,
the existing firmware booted, enumerated the handset, completed HXD setup at
9600 8N1, and reached BRIDGE READY. Remote Bluetooth name remains CELESTRON.
All 60 exact Kx/x# transactions passed across three fresh RFCOMM connections.
GPIO18 tracked each connection and disconnection. No USB/UART warnings,
overflows or bridge faults were reported during the test. No motion commands
were sent and no firmware or module configuration was changed.
Evidence: `diagnostics/permanent-wiring-bridge.log` and
`diagnostics/permanent-wiring-echo.log`. Physical USB hotplug and powered-mount
INDI acceptance were not repeated in this test.

## Powered-base movement test

With the handset connected to the powered base, and the StarSense camera absent,
a user-authorized manual movement test passed over the existing Bluetooth bridge.
Following INDI's handset serial protocol, azimuth positive fixed rate 1 was
commanded for 0.75 seconds, then stopped (P 02 10 24 01 00 00 00;
stop P 02 10 24 00 00 00 00). Both commands returned #. Precise azimuth/altitude
query z changed from 00000000,00000000# to 00004F00,00000000#:
azimuth +6.10256 arcseconds, altitude unchanged. A second stop was acknowledged;
subsequent position samples two seconds apart were identical, and final echo
passed. No GoTo, tracking configuration, alignment or altitude movement was sent.
Evidence: diagnostics/mount-motion-test.json and diagnostics/mount-stopped-check.log.
This verifies a small azimuth motor movement and stop through Bluetooth, UART,
USB, handset and base; aligned KStars/INDI operation remains untested.
Protocol reference: https://github.com/indilib/indi/blob/master/drivers/telescope/celestrondriver.cpp
(start_motion, stop_motion, get_azalt).


A subsequent rate-7 azimuth command for four seconds produced approximately
4 degrees of reported movement. The user confirmed visible physical motion;
stop was acknowledged and subsequent positions were stable. Evidence:
`diagnostics/mount-rate7-motion-test.json`. See the consolidated
[wiring recreation guide](../wiring/README.md).
