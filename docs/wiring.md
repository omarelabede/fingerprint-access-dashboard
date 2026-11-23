# Hardware wiring

## Prototype scope

This bench build uses an Arduino Uno, an AS608 optical fingerprint sensor, a
single-channel 5 V relay module, and two LEDs. Arduino D8 controls the relay
input. A green LED connected through the relay's COM/NO contacts is the
low-voltage substitute load that simulates the electronic-lock activation
signal. A red LED connected directly to D9 represents denial, a system error,
or an active lockout.

The photographed electronic lock and 12 V adapter are not connected in this
bench configuration. The prototype verifies the relay-control signal and visual
indication only; it does not test a physical door, lock current, inductive-load
protection, emergency egress, or fail-safe/fail-secure behavior.

## Required parts

- Arduino Uno and USB cable
- AS608 fingerprint-sensor module
- One single-channel 5 V relay module
- One green LED
- One red LED
- Two 220–330 Ω resistors, one for each LED
- Breadboard and jumper wires

## Connection table

| Device lead | Arduino connection | Notes |
|---|---|---|
| AS608 TX | D2 | SoftwareSerial RX; UART lines are crossed |
| AS608 RX | D3 | SoftwareSerial TX; UART lines are crossed |
| AS608 VCC | Supply supported by the exact module | Check the module label/datasheet before applying power |
| AS608 GND | GND | All components must share Arduino ground |
| Relay IN | D8 | Local simulated-lock command from Arduino |
| Relay VCC | Arduino 5 V | Verify the exact module's control-supply requirement |
| Relay GND | Arduino GND | Common control-side ground |
| Relay COM | Arduino 5 V for this bench test only | Low-voltage supply to the switched green circuit |
| Relay NO | 220–330 Ω resistor, then green LED anode | Green turns on only while COM and NO are closed |
| Green LED cathode | GND | Longer LED leg is normally the anode |
| Red LED anode | D9 through its own 220–330 Ω resistor | Denial, error, and lockout indication |
| Red LED cathode | GND | Do not omit the resistor |

Equivalent output paths:

    Arduino D8 → Relay IN
    Arduino 5 V → Relay COM → Relay NO → 220–330 Ω resistor → green LED → GND
    Arduino D9 → 220–330 Ω resistor → red LED → GND

Use the terminal labels printed on the actual module. Do not infer the NO, COM,
and NC order from a generic photograph because terminal order varies.

## Relay trigger polarity and safe startup

All three sketches define one setting:

    constexpr bool RELAY_ACTIVE_LOW = true;

`true` selects an active-low input; `false` selects an active-high input. The
included value matches the active-low configuration used in Chapter IV, but it
must be verified with `HardwareSelfTest` on the actual module before normal
operation. The sketches preload the configured inactive level before changing
D8 to an output, reducing the risk of a software-created startup pulse. Software
cannot guarantee the electrical state before `setup()` begins; if the module
does not hold its input inactive during reset, use the pull-up or pull-down
recommended by its manufacturer.

## UART and voltage cautions

1. Disconnect USB power before changing wiring.
2. Keep the 12 V adapter and electronic lock disconnected from this LED-based
   bench circuit. Never connect 12 V to Arduino 5 V, an I/O pin, or either LED.
3. Confirm the supported VCC range of the actual AS608 breakout. Modules sold
   under the same name do not always expose identical regulators or logic-level
   protection.
4. Confirm whether the sensor RX input is 5 V tolerant. If its documentation
   requires 3.3 V UART logic, level-shift or divide the Arduino D3 transmit
   signal as the module manufacturer specifies.
5. Connect control-side grounds before troubleshooting sensor or relay input.
6. The sketches use 57,600 baud for the sensor and 9,600 baud for USB Serial.

## Template assignments

Enrollment is deliberately limited to:

| Template ID | Authorized identity |
|---:|---|
| 1 | Administrator |
| 2 | Technician |

Use the SecureEnrollment sketch to store these templates inside the AS608.
Then upload FingerprintLedAccess for normal operation. The enrollment sketch
and the normal-access sketch are separate operating modes and must not be
combined.

## Safe first-power procedure

1. Keep the electronic lock and 12 V adapter disconnected.
2. Inspect the crossed UART connections, relay terminal labels, LED polarity,
   and both resistor values.
3. Set `RELAY_ACTIVE_LOW` to the expected module type in all three sketches.
4. Power the Arduino over USB.
5. Upload `HardwareSelfTest` and open Serial Monitor at 9,600 baud.
6. First confirm that the relay is inactive and the green LED is off. Then
   confirm the requested relay/green activation, release, and independent red
   LED test. If the relay starts active, answer N, disconnect power, reverse the
   polarity setting in all sketches, and repeat.
7. Record the sensor, relay/green, and red PASS/FAIL results.
8. Power down before correcting any wiring.
9. Only after the self-test is satisfactory, upload `SecureEnrollment` and
   enroll IDs 1 and 2. The relay must remain inactive throughout enrollment.
10. Upload `FingerprintLedAccess` for the authentication tests.

The self-test asks the operator to confirm the physical relay and LEDs because
an Arduino output cannot prove contact movement, LED polarity, or illumination.
