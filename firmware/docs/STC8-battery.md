# STC8 battery interface

Based on the user-supplied `bsp_stc8h1kxx.c` and `.h` from Downloads, which
are byte-identical to the corresponding files in the local
`ESP32-P4-Adcance-brookesia_phone_inch10_1` BSP. The hardware schematic also
connects STC8 battery sensing to VBAT through R187 (39k) and R188 (100k).

The board component owns and reuses the touch bus on SDA45/SCL46; it does not
create a second bus on those pins. The STC8 uses 7-bit address 0x2F at 400 kHz.
Each read transmits one register index and receives one byte with a repeated
start. Registers 0x00–0x0B are read individually as in the supplied driver.

| Registers | Interpretation |
| --- | --- |
| 0x00–0x03 | ADC millivolts, unsigned 32-bit little-endian |
| 0x04–0x07 | Battery millivolts, unsigned 32-bit little-endian |
| 0x08 | Reported percentage, 0–100 |
| 0x09 | Charge state: 0 idle, 1 charging, 2 full, 3 not charging, 4 error |
| 0x0A | LED state: 0 idle, 1 charging, 2 full, 3 not charging, 4 low power |
| 0x0B | Trailing padding in the supplied 12-byte C structure; ignored |

The implementation decodes bytes explicitly and rejects implausible voltage,
percentage, or status values. Valid controller data is not proof that a battery
is physically attached: the protocol has no independently verified pack-present
bit. Calibration should be checked against a meter under USB and battery power.
Polling runs in a worker task; an LVGL timer consumes the latest result and
updates the home indicator and Setting page. GPIO/PWM control registers are
never written by the battery monitor.

First hardware check: the V1.0 board acknowledged 0x2F and returned ADC
3283 mV, battery 4563 mV, 100%, charge state 2. This establishes protocol
communication, not correct cell voltage or pack presence. The application applies the user-requested power-source rule: readings above
4300 mV show USB power on Home and USB power connected in Setting. At exactly
4300 mV or below it shows the battery reading and charge state. This rule is a
voltage-based heuristic; the protocol does not independently identify USB presence.
