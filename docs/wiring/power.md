# Power and the board jumper

## What was observed on this board

Initially the user measured approximately 1–1.3 V at the expected host supply
points. The user then found a jumper described as **bridging VIN to VBUS**;
after setting it, the relevant pin measured **5 V** and the handset powered
normally. Native USB enumeration, serial traffic and base movement subsequently
worked. Do not treat the earlier low voltage as a usable supply.

This is a user-confirmed result for the tested generic board, **not a confirmed
schematic**. The jumper's physical position, exact board model, final external
5 V entry point, protection circuit, and cable shield termination are not
recorded. Do not copy a jumper location from a different board photograph.

The handset cable is now soldered directly to the headers. It needs its own
VBUS wire to the verified 5 V net; direct D+/D− soldering does not supply power.
Whether a jumper is necessary for a particular header's supply still depends
on that board's circuit.

## Required electrical arrangement

- Regulated **5 V host VBUS** to the handset mini-B VBUS contact.
- A board-supported power input that powers the ESP32 reliably.
- A suitable supply to the JDY-31: a regulated breakout may accept 5 V, while
  the bare module requires a lower voltage. Check the actual assembly's label
  and regulator. Do not apply 5 V to bare-module VCC or any signal pin.
- Common ground between ESP32, JDY-31 and handset cable.
- Appropriate current limiting / overcurrent protection and supply isolation
  according to the actual board circuit. The VIN/VBUS jumper is not evidence
  that these protections exist.

“VBAT” is not a substitute for USB VBUS. Use a verified regulated 5 V rail,
not an unregulated battery or the telescope's higher-voltage supply. No total
adapter current measurement was recorded; size the source for the actual
board, module and USB load with margin, and verify voltage under load.

## Checks before connecting the handset

1. Remove all power. Identify GND, the intended 5 V pad, jumper connections,
   GPIO19 and GPIO20 using board markings, documentation and continuity.
2. Check the cut cable's contact-to-wire mapping. Check for shorts between
   VBUS, ground and data, including stray wire strands.
3. Determine how COM USB power, native VBUS and the board's 5 V input relate.
   Do not assume the jumper isolates them; it may directly join power rails.
4. With the handset disconnected, power the intended arrangement and measure
   VBUS to GND at the mini-B plug. Verify approximately 5 V with correct polarity.
5. Confirm the intended arrangement cannot backfeed a PC USB port or another
   supply. Use an appropriate isolation/power-switch arrangement if the board
   lacks one. Avoid simultaneous sources until their interaction is established.
6. Connect the handset, then check the 5 V rail under load, normal handset boot,
   and successful USB initialization in COM logs.

During development the separate COM port was used for flashing, logs and board
power. For standalone use, select the actual board's supported regulated power
entry; the final external input connection has not been documented. A powered
mount connection is separate from adapter USB power and does not establish
safe isolation between the adapter's power sources.
