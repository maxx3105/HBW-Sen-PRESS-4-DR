# HBW-Sen-PRESS-DR

HomeMatic Wired Pressure Sensor Module for DIN Rail mounting

## Overview

This is a HomeMatic Wired (RS485) device for monitoring hydraulic pressure using industrial pressure sensors. It provides 4 analog pressure sensor inputs (A0–A3). The PCB carries 8 ADC pins, but the CRMB2 enclosure only exposes 4 sensor connections. Channels that are not populated are switched off in the CCU via `Sensortyp = NICHT_BELEGT`.

`NUMBER_OF_CHAN` in the sketch and `count` in the XML **must** match. A dynamic
channel count via `count_from_sysinfo` does not exist on the wired side —
`hs485d` does not implement the attribute (verified in the binary: neither
`count_from_sysinfo` nor any equivalent, while `rfd` has it). Without `count`
the CCU creates zero channels. More channels would require a second device type
with its own type byte, as done for `HBW-LC-RGBWW-3` / `-6`.

### Based on:
- **HBWired** by Thorsten Pferdekaemper: https://github.com/ThorstenPferdekaemper/HBWired
- **HB-UNI-Sen-PRESS** by jp112sdl: https://github.com/jp112sdl/HB-UNI-Sen-PRESS

## Repository layout

```
HBW-Sen-PRESS/
├── HBW-Sen-PRESS-DR/            Arduino sketch (folder name must match the .ino)
│   ├── HBW-Sen-PRESS-DR.ino
│   ├── HBWAnalogPRESS.h
│   └── HBWAnalogPRESS.cpp
├── hbw-sen-press-dr.xml         CCU device definition (hs485types)
├── HBW-Sen-PRESS-4_Platine1/    KiCad project, PCB revision 1 (+ Gerber)
├── HBW-Sen-PRESS-4_Platine2/    KiCad project, PCB revision 2 (+ Gerber)
├── BUGFIXES.md                  what was broken in v0.01–v0.03 and why
└── CLAUDE.md                    compact project status / handover notes
```

## Hardware

### Supported Sensors
- **0.5 MPa** hydraulic pressure sensor (0-5 bar / 0-72.5 PSI)
- **1.2 MPa** hydraulic pressure sensor (0-12 bar / 0-174 PSI)

Both sensors output: **0.5V - 4.5V** analog signal
- 0.5V = 0 bar (minimum pressure)
- 4.5V = max pressure (5 or 12 bar depending on sensor type)

### Components
- **ATmega328P-A** microcontroller (or Arduino Nano)
- **MAX487E** RS485 transceiver
- **MC34063AD** step-down converter (24V bus → 5V)
- Analog inputs: A0-A3 in use (the PCB routes A0-A7, but the CRMB2 enclosure exposes only 4)

### PCB Features
- DIN rail mounting
- RS485 bus connection (A, B, GND, +24V)
- Screw terminals for sensor connections
- ISP programming header
- Status LED
- Factory reset button

## Pin Configuration

| Pin | Function |
|-----|----------|
| A0-A3 | Pressure sensor analog inputs (A4-A7 present on PCB, unused) |
| D2 | RS485 TX (or TXD for software serial) |
| D3 | RS485 TXEN |
| D4 | RS485 RX (software serial only) |
| D8 | Factory reset button |
| D13 | Status LED |

## Software Features

### Per-Channel Configuration (in CCU/FHEM)

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Sensortyp** | 0.5MPa / 1.2MPa / NICHT_BELEGT | NICHT_BELEGT | Select sensor type or disable channel |
| **SEND_DELTA_VALUE** | 0.0-2.54 bar | 0.1 bar | Send update when pressure changes by this amount |
| **OFFSET** | -1.27 to +1.27 bar | 0.0 bar | Calibration offset (signed, stored with bias 127) |
| **UPDATE_INTERVAL** | 1-254 sec | 30 sec | Pause between measurement cycles |
| **SEND_MIN_INTERVAL** | 10-3600 sec | 30 sec | Minimum time between transmissions |
| **SEND_MAX_INTERVAL** | 10-3600 sec | 600 sec | Maximum time between transmissions (force send) |

The upper limits avoid the all-ones value (`0xFF` / `0xFFFF`), which marks an
erased EEPROM cell and is mapped to the default instead.

### Logic Flow

1. **Measurement**: one ADC sample every SAMPLE_INTERVAL (3 s), averaged over
   MAX_SAMPLES (4) — no blocking `delay()`, so the RS485 receive path stays
   responsive. After a complete reading the channel pauses for UPDATE_INTERVAL.
2. **Conversion**: ADC → Voltage → Pressure (based on sensor type)
3. **Calibration**: Apply OFFSET
4. **Transmission decision** (evaluated every loop pass, independent of the
   measurement cycle):
   - Wait at least SEND_MIN_INTERVAL since last send
   - Send if pressure changed by ≥ SEND_DELTA_VALUE
   - Force send after SEND_MAX_INTERVAL regardless of change
   - On `BUS_BUSY` the value is kept pending instead of being discarded

## Installation

### 1. Arduino IDE Setup
```bash
# Install HBWired library
git clone https://github.com/ThorstenPferdekaemper/HBWired
# Copy to Arduino/libraries/HBWired
```

### 2. Compile & Upload

The sketch lives in the `HBW-Sen-PRESS-DR/` subfolder (folder name must match
the `.ino` name).

- Open `HBW-Sen-PRESS-DR/HBW-Sen-PRESS-DR.ino` in Arduino IDE
- Select board: **Arduino Nano** (ATmega328P)
- For debug: Leave `USE_HARDWARE_SERIAL` commented out
- For production: Uncomment `#define USE_HARDWARE_SERIAL`
- Upload via ISP or bootloader

Or from the project root:

```
arduino-cli compile --fqbn arduino:avr:nano:cpu=atmega328 HBW-Sen-PRESS-DR
arduino-cli compile --fqbn arduino:avr:nano:cpu=atmega328 --upload --programmer usbasp HBW-Sen-PRESS-DR
```

### 3. CCU/RaspberryMatic Setup
1. Copy `hbw-sen-press-dr.xml` to CCU
2. Import device definition in FHEM or install as addon
3. Connect device to RS485 bus
4. Press reset button for 3 seconds to enter pairing mode
5. Add device in CCU interface

## EEPROM Memory Map

`HBWDevice` reads the config struct starting at EEPROM address `0x01`
(see `readConfig()` in HBWired.cpp), so struct offset 6 lands on `0x07`.

```
0x00      : Device type (0x50)
0x01      : Logging time
0x02-0x05 : Central address
0x06      : Direct link deactivate flag
0x07      : Channel 1 config start (8 bytes)
  +0: send_delta_value
  +1: offset (bias 127: 0 = -1.27 bar, 127 = 0.00 bar, 254 = +1.27 bar)
  +2-3: send_max_interval (16-bit little endian)
  +4-5: send_min_interval (16-bit little endian)
  +6: update_interval
  +7: pressure_sensor_type
0x0F/0x17/0x1F : Channel 2..4 config start
0x3FC-0x3FF : OWN_ADDRESS (bus address, E2END-3 for the 1 kB EEPROM of the 328P)
```

The layout is enforced at compile time by three `static_assert` in
`HBW-Sen-PRESS-DR/HBW-Sen-PRESS-DR.ino` — struct size, channel start offset and the distance to
`OWN_ADDRESS` must match the XML.

## Troubleshooting

### No readings / all zeros
- Check sensor wiring (3-wire: VCC, GND, Signal)
- Verify sensor is outputting 0.5-4.5V
- Check if channel is enabled (Sensortyp != "Disabled")

### Wrong pressure values
- Verify correct sensor type selected (0.5 MPa vs 1.2 MPa)
- Use OFFSET parameter for calibration
- VCC does *not* need to be exactly 5.0 V: these sensors are ratiometric, their
  output scales with their supply. As long as sensor and ADC share the same 5 V
  rail, the regulator's deviation cancels out — which is why the default (VCC)
  ADC reference is used and not the internal 1.1 V reference. What does matter
  is that sensor and MCU are supplied from the *same* rail.

### CCU shows the device but no pressure value
- Check the XML is the current one (v0.03+): device type `0x50`, frame
  `INFO_LEVEL` as `type="#i" channel_field="10"`, payload at index `11.0`
- After replacing the XML: delete it, restart, install the new one, restart,
  then re-adopt the device from the inbox

### Device not communicating
- Check RS485 wiring (A, B correct polarity)
- Verify bus termination (120Ω resistors at both ends)
- Check TX/RX LED activity
- Test with DEBUG mode enabled (Serial monitor at 115200 baud)

## Debug Mode

Uncomment `//#define USE_HARDWARE_SERIAL` to enable debug output via USB:

```
HBW-Sen-PRESS-DR v2
Free RAM: 1234 bytes
Channels: 4
ADC Ch0: 512 (2500 mV)
ADC Ch1: 102 (500 mV)
ADC Ch2: 920 (4500 mV)
ADC Ch3: 0 (0 mV)
=== Setup complete ===
```

## License

Creative Commons BY-NC-SA 3.0 AT
http://creativecommons.org/licenses/by-nc-sa/3.0/at/

## Credits

- Thorsten Pferdekaemper - HBWired framework
- Dirk Hoffmann - HBWired contributions
- jp112sdl - Original HB-UNI-Sen-PRESS concept
- maxx3105 - HBW-Sen-PRESS-DR implementation

## Changelog

### v0.04
- Channel count 8 → 4, matching what the CRMB2 enclosure exposes
- Confirmed at the bus that `count_from_sysinfo` is not usable on wired:
  `hs485d` parses the rest of the XML (device type, firmware, device
  parameters) but creates **zero** channels without a `count` attribute

### v0.03
- **XML rebuilt against the HMW frame layout.** The `<frames>` block was still
  AskSin/radio syntax inherited from HB-UNI-Sen-PRESS (`type="0x53"`,
  `channel_field="11"`, payload at `12.0`). HBWired sends `0x69` ('i') with the
  channel in byte 10 and the payload from byte 11 — the CCU could never have
  matched the info messages to the datapoint.
- `Sensortyp` moved from `interface="config" list="1"` (radio) to
  `interface="eeprom"` — hs485d would never have written that byte, leaving
  every channel permanently disabled.
- Added `LEVEL_GET` frame (`#S`) and `<get>` so the CCU can poll the value;
  HBWired already answers 'S'.
- Replaced `count_from_sysinfo` (radio-only, and 3 bits cannot hold 8) with a
  fixed `count="8"`; the channel-count byte at 0x17 and its padding are gone,
  channel configs now start at 0x07.
- Added `OWN_ADDRESS` at 0x03FC (E2END-3 of the 328P's 1 kB EEPROM).
- Removed the blocking `delay(2)` from the ADC sampling loop — 8 ms per
  measurement was enough to drop RS485 frames on SoftwareSerial.
- `sendInfoMessage()` return code is evaluated; on `BUS_BUSY` the reading is no
  longer treated as sent.
- Send logic decoupled from the measurement cycle.
- OFFSET now stored with bias 127, so -0.01 bar no longer collides with the
  "erased EEPROM" marker 0xFF.
- Channel count 4 → 8; EEPROM layout guarded by `static_assert`.

### v0.02 (2024-04-06)
- Fixed double channel initialization bug
- Fixed EEPROM address mapping (address_step = 8)
- Added offset support in code
- Fixed send_min_interval logic (no early return breaking timing)
- Changed send_min_interval to 16-bit (was 8-bit)
- Improved ADC oversampling with delays
- Added comprehensive debug output

### v0.01 (2024-04-02)
- Initial version based on HB-UNI-Sen-PRESS
- Basic pressure measurement
- CCU integration via XML
