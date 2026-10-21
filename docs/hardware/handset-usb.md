# Connected handset USB evidence

Captured from Linux sysfs on 2026-10-21. Direct lsusb failed because the environment has no /dev/bus/usb; dmesg access is denied.

Device: 067b:2303, Prolific Technology Inc., USB-Serial Controller D
bcdDevice: 0x0400; speed: 12 Mbit/s; interface driver: pl2303; tty: ttyUSB0

Raw descriptor bytes:

```text
12 01 10 01 00 00 00 40 7b 06 03 23 00 04 01 02 00 01 09 02 27 00 01 01 00 80 32 09 04 00 00 03 ff 00 00 00 07 05 81 03 0a 00 01 07 05 02 02 40 00 00 07 05 83 02 40 00 00
```

Decoded descriptors:

- Device: bcdUSB=0x0110, class=0x00, EP0 max packet=64, configurations=1
- Configuration: total length=39, interfaces=1, value=1, attributes=0x80, declared max power=100 mA
- Interface: number=0, alternate=0, endpoints=3, class/subclass/protocol=ff/00/00
- Endpoint: address=0x81, attributes=0x03, max packet=10, interval=1
- Endpoint: address=0x02, attributes=0x02, max packet=64, interval=0
- Endpoint: address=0x83, attributes=0x02, max packet=64, interval=0

This identifies USB transport descriptors, not the handset serial baud or control-line requirements. No serial commands were sent.

Linux's current `pl2303_detect_type()` maps bcdUSB 0x0110 plus bcdDevice 0x0400 and EP0 size 64 to TYPE_HXD. This is a descriptor-based HXD identification, not physical chip inspection; Linux also has an HXD clone quirk detected by a GET_LINE request stalling. That quirk has not been tested here.

Source: https://github.com/torvalds/linux/blob/master/drivers/usb/serial/pl2303.c

## User-supplied kernel confirmation (2026-10-21)

The user provided Linux kernel messages confirming a full-speed device with
VID:PID `067b:2303`, `bcdDevice=4.00`, manufacturer `Prolific Technology Inc.`,
product `USB-Serial Controller D`, interface `1-6:1.0`, and successful binding
of the `pl2303` driver to `ttyUSB0`. No serial number is advertised.
This independently confirms the identification above. The full `lsusb -v`
output is still requested to corroborate the stored raw descriptors; the kernel
messages alone do not prove the HXD subtype, serial settings, or communication.

Driver planning can now target the HXD path rather than HXN: legacy vendor request 0x01 read/write initialization, followed by verified line configuration, control lines and pipe reset behavior. The actual ESP32 USB implementation remains pending; HC-05 and host power checks remain independent requirements.
