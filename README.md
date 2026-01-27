# Vevor Diesel Heater PWM Controller

ESPHome-based controller for Vevor diesel heaters using PWM communication protocol.

## Features
- ESP32-based controller for Vevor diesel heaters
- PWM communication protocol implementation
- Web interface for monitoring and control
- Heater status monitoring (on/off, power levels, faults)
- Cooldown timer management
- Battery voltage monitoring (standby mode)
- MQ-2 gas sensor integration
- DS18B20 temperature sensor support
- Automatic packet transmission
- Manual control via buttons
- Real-time status display
- Runtime tracking and statistics

## Hardware Requirements
- ESP32 Dev Board
- Vevor Diesel Heater with PWM interface
- MQ-2 Gas Sensor (optional)
- DS18B20 Temperature Sensor (optional)
- LED for status indication
- Button for manual control
- Logic level converter if needed (3.3V ↔ 5V)
- Power supply (12V for heater control, 5V/3.3V for ESP32)

## Pin Configuration
| GPIO | Function | Description |
|------|----------|-------------|
| 16   | RX       | PWM input from heater |
| 17   | TX       | PWM output to heater |
| 15   | Output   | Heater status LED |
| 13   | Input    | Button (with internal pull-up) |
| 33   | Input    | MQ-2 Digital output |
| 36   | ADC      | MQ-2 Analog input (ADC1_CH0) |
| 4    | OneWire  | DS18B20 Temperature sensor data |
| 2    | UART TX  | Serial debug (optional) |
| 0    | UART RX  | Serial debug (optional) |

## Communication Protocol
The heater uses a custom PWM protocol where:
- 8-bit commands are sent to the heater
- 16-bit status packets are received from the heater
- Bit timing: ~4ms for logic 1, ~8ms for logic 0
- Frame period: ~12.1ms
- Start pulse: 30ms HIGH
- Optional pre-pulse: 1-3ms LOW (for 16-bit frames)

### Command Bits (8-bit transmission)
| Bit | Name | Function | Default |
|-----|------|----------|---------|
| 7   | bit7 | Reserved | OFF |
| 6   | bit6 | Reserved | ON |
| 5   | bit5 | Reserved | OFF |
| 4   | bit4 | Reserved | ON |
| 3   | bit3 | Reserved | ON |
| 2   | bit2 | Power On/Off | OFF |
| 1   | bit1 | Power Level Toggle | OFF |
| 0   | bit0 | Reserved | ON |

### Status Codes (16-bit reception)
| Hex Value | Status | Description |
|-----------|--------|-------------|
| 0x9FF0-0x9FF6 | Idle | Standby with battery voltage indication |
| 0x933F | Shutdown | Turning off sequence |
| 0x963F | Flame Clear | Clearing flame |
| 0x973F | Cooldown | Cooling down |
| 0x97BA/0x96BA/0x92BA | Startup | Starting up sequence |
| 0x93x0-0x93xF | Active | Operating (last nibble = power level) |
| 0x7FFD | Fault | Error condition |
| 0xFA6C | Ready | Cooled down and ready |
| 0x7000-0x7FFF | Error | Various error states |

### Power Levels (when heater is active)
| Last Nibble | Power Level | Letter |
|-------------|-------------|--------|
| 0xA | Level 6 | A |
| 0xB | Level 5 | B |
| 0xC | Level 4 | C |
| 0xD | Level 3 | D |
| 0xE | Level 2 | E |
| 0xF | Level 1 | F |
