# ESPHome Vevor Heater Library

[![ESPHome](https://img.shields.io/badge/ESPHome-Compatible-blue)](https://esphome.io)
[![Home Assistant](https://img.shields.io/badge/Home%20Assistant-Compatible-green)](https://www.home-assistant.io/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

An easy-to-use ESPHome library for controlling Vevor diesel heaters with full Home Assistant integration. Based on the original protocol reverse-engineering work from the [vevor_heater_control](https://github.com/zatakon/vevor_heater_control) project. Please visit that if you are interested - there are still some unknowns in the protocol and I will be glad for help. 

> [!WARNING]
> **This library is under active development!** Major changes are expected, and future versions may introduce breaking changes. Current configurations may not be compatible with upcoming releases. Please pin to a specific version if you need stability.

---

## Features

- **Plug & Play**: Add just a few lines to your ESPHome configuration
- **Automatic Sensor Creation**: All sensors created automatically with sensible defaults
- **Home Assistant Integration**: Native climate entity support
- **Manual & Automatic Modes**: 
  - Manual mode: Direct power level control
  - Automatic mode: Temperature-based control with external sensor*
- **Smart On/Off Control**: Default 80% power level on startup (configurable)
- **External Temperature Sensor Support**: Use any ESPHome temperature sensor (e.g., AHT10, DHT22, BMP280)
- **Protocol Handling**: Robust communication with proper error handling
- **Safety Features**: Communication timeout detection and failsafe mechanisms
- **Comprehensive Monitoring**: Temperature, voltage, fan speed, pump frequency, and more
- **Easy Control**: Simple on/off and power level control
- **Climate Platform**: Full thermostat functionality for Home Assistant

I tested functionality briefly. Please report bugs if you find them. 

*experimental

## Todo
- **Test automatic mode functionality**
- **Implement P controller for maintaining target temperature by adjusting power**
- **Complete climate entity integration**
- **Add anti-freeze mode**

## Hardware Requirements

### Supported Heaters
- Vevor diesel heaters (model XMZ-D2 2 kW, 5 kW tested)
- Other heaters using the same protocol may work

### ESP32 Board
- ESP32-C3, ESP32, ESP32-S2, ESP32-S3
- Minimum 2 GPIO pins for UART communication

### Connection Circuit

**Important**: The Vevor bus operates at ~5V with high noise. **DO NOT** connect directly to ESP32 pins!

Be careful and connect the RX transistor to the 3.3V rail, not 5V. \
**Double check the transistor's** pinout. \
Feel free to use any pin for TX and RX. \
**Put pullup resistor between data line and 5V**

![Connection ESP32 to Vevor](https://github.com/zatakon/vevor_heater_control/blob/main/docs/images/vevor_heater_esp32.PNG?raw=true)

Refer to the original project's [hardware documentation](https://github.com/zatakon/vevor_heater_control#4-hardware) for more informations.

## Quick Start

### 1. Add to ESPHome Configuration

#### Basic Manual Mode (Simple Power Control)
In manual mode, the heater runs at a fixed power level that you control:
- Direct power level control (10-100%)
- No automatic temperature regulation
- External temperature sensor is optional
- Ideal for simple on/off control

```yaml
# Add the library
external_components:
  - source: github://zatakon/esphome-vevor-heater
    components: [vevor_heater]

# UART configuration
uart:
  id: heater_uart
  tx_pin: 
    number: GPIO2
    inverted: true  # Important!
  rx_pin:
    number: GPIO1 
    inverted: true  # Important!
  baud_rate: 4800

# Heater component - creates all sensors automatically!
vevor_heater:
  id: my_heater
  uart_id: heater_uart
  control_mode: manual           # Manual mode (default)
  default_power_percent: 80      # Starts at 80% power when turned on

switch:
  - platform: template
    name: "Heater Power"
    turn_on_action:
      - lambda: "id(my_heater).turn_on();"
    turn_off_action:
      - lambda: "id(my_heater).turn_off();"

number:
  - platform: template
    name: "Heater Power Level"
    min_value: 10
    max_value: 100
    step: 10
    unit_of_measurement: "%"
    set_action:
      - lambda: "id(my_heater).set_power_level_percent(x);"
```

#### Automatic Mode with Temperature Sensor - don't use please, will be modified

*Will be implemented*

In automatic mode, the heater maintains a target temperature:
- Temperature-based control
- External temperature sensor is **MANDATORY**
- Works with climate entity for thermostat control
- Automatically adjusts power based on temperature delta

**Important:** The heater will refuse to turn on in automatic mode if no external temperature sensor is configured or if the sensor has no valid reading.

<!-- ```yaml
# I2C for temperature sensor
i2c:
  sda: GPIO7
  scl: GPIO8

# External temperature sensor (REQUIRED for automatic mode)
sensor:
  - platform: aht10
    temperature:
      name: "Room Temperature"
      id: room_temp
    humidity:
      name: "Room Humidity"

# UART configuration
uart:
  id: heater_uart
  tx_pin: 
    number: GPIO2
    inverted: true
  rx_pin:
    number: GPIO1 
    inverted: true
  baud_rate: 4800

# Heater with automatic temperature control
vevor_heater:
  id: my_heater
  uart_id: heater_uart
  control_mode: automatic        # Automatic temperature control
  default_power_percent: 80      # Default power level
  target_temperature: 20         # Default target
  external_temperature_sensor: room_temp  # MANDATORY for automatic mode
```

That's it! This creates all sensors with automatic names and good defaults. -->

### 2. Add Climate Integration (Optional)

Will be implemented 

<!-- ```yaml
climate:
  - platform: vevor_heater
    name: "Workshop Climate"
    vevor_heater_id: my_heater
    min_temperature: 5
    max_temperature: 35
``` -->

## Available Sensors

*TODO:* Heat Exchanger Temperature is duplicit

When `auto_sensors: true` (default), these sensors are automatically created:

| Sensor                       | Description                   | Unit | Device Class |
|------------------------------|-------------------------------|------|--------------|
| Temperature                  | Heat exchanger temperature    | °C   | Temperature  |
| Input Voltage                | Heater input voltage          | V    | Voltage      |
| State                        | Current heater state          | -    | -            |
| Power Level                  | Current power level           | %    | Power        |
| Fan Speed                    | Combustion fan speed          | RPM  | -            |
| Pump Frequency               | Fuel pump frequency           | Hz   | -            |
| Glow Plug Current            | Glow plug current draw        | A    | Current      |
| Heat Exchanger Temperature   | Detailed heat exchanger temp  | °C   | Temperature  |
| State Duration               | Time in current state         | s    | Duration     |
| Cooling Down                 | Cooling down status           | -    | -            |
| Hourly Consumption           | Instantaneous fuel rate       | ml/h | -            |
| Daily Consumption            | Total fuel consumed today     | ml   | -            |

### Fuel Consumption Tracking

The library includes accurate fuel consumption monitoring:

- **Hourly Consumption**: Shows instantaneous fuel consumption rate (ml/h) based on current pump frequency
- **Daily Consumption**: Accumulates total fuel consumed since midnight, persists across reboots
- **Configurable Calibration**: Default 0.022 ml per pump pulse, adjustable via configuration or HA number entity

```yaml
vevor_heater:
  id: my_heater
  uart_id: heater_uart
  injected_per_pulse: 0.022  # Adjust based on your heater model
  
  # Optional: Expose as adjustable number in Home Assistant
  injected_per_pulse_number:
    name: "Fuel Injected Per Pulse"
    min_value: 0.001
    max_value: 1.0
    step: 0.001
```

The fuel counter automatically resets daily at midnight and saves data to flash memory to survive reboots.

## Configuration Options

### Custom Sensor Names

```yaml
vevor_heater:
  id: my_heater
  uart_id: heater_uart
  # Override specific sensor names
  input_voltage:
    name: "Heater Input Voltage"
    filters:
      - calibrate_linear:
          - 10.0 -> 10.5
          - 16.0 -> 15.5  # Calibration if needed
  state:
    name: "Heater Status"
```

### Supported External Temperature Sensors

You can use any [ESPHome temperature sensor](https://esphome.io/components/#environmental) as the external sensor. 


Then link it to the heater:

```yaml
vevor_heater:
  id: my_heater
  uart_id: heater_uart
  external_temperature_sensor: room_temp
```

### Disable Auto-Sensors (Manual Mode)

```yaml
vevor_heater:
  id: my_heater
  uart_id: heater_uart
  auto_sensors: false
  # Only define the sensors you want
  temperature:
    name: "Temperature"
  state:
    name: "Status"
```

## Home Assistant Integration

### Climate Entity
- Will be implemented
<!-- The climate platform creates a native Home Assistant thermostat with:
- Current temperature display
- Target temperature control
- Heat/Off mode switching
- Visual temperature controls -->

### Sensor Entities
- All sensors appear as individual entities with:
  - Proper device classes for correct icons
  - Historical data logging
  - Use in automations and scripts

### Device Information
- All entities are grouped under a single device for easy management.

## Heater States

The heater reports these states:

| State | Description |
|-------|-------------|
| `Off` | Heater is off |
| `Glow Plug Preheat` | Just started |
| `heating up` | Preparing for ignition |
| `Stable Combustion` | Running normally, fuel injected |
| `Stopping/Cooling` | Shutting down safely |
| `Disconnected` | Communication lost |

## API Reference

### Control Methods

```cpp
// Turn heater on/off
id(my_heater).turn_on();
id(my_heater).turn_off();

// Set power level (10-100%)
id(my_heater).set_power_level_percent(50.0);

// Set target temperature
id(my_heater).set_target_temperature(22.0);
```

### Status Methods

```cpp
// Check heater state
bool heating = id(my_heater).is_heating();
bool connected = id(my_heater).is_connected();
float temp = id(my_heater).get_current_temperature();
```

## Safety Features

- **Communication Timeout**: Automatically detects lost communication
- **State Validation**: Verifies all received data
- **Checksum Verification**: Ensures data integrity
- **Failsafe Shutdown**: Safe heater shutdown on errors
- **Over-temperature Protection**: Configurable via automations

<!-- ## Examples

### Manual Control with Temperature Display

```yaml
# Manual mode with external temperature sensor for display only
vevor_heater:
  id: my_heater
  uart_id: heater_uart
  control_mode: manual
  default_power_percent: 80
  external_temperature_sensor: room_temp

switch:
  - platform: template
    name: "Heater"
    turn_on_action:
      - lambda: "id(my_heater).turn_on();"  # Turns on at 80%
    turn_off_action:
      - lambda: "id(my_heater).turn_off();"

number:
  - platform: template
    name: "Power Level"
    min_value: 10
    max_value: 100
    step: 10
    set_action:
      - lambda: "id(my_heater).set_power_level_percent(x);"
```

### Automatic Temperature Control

```yaml
# Automatic mode with climate entity
vevor_heater:
  id: my_heater
  uart_id: heater_uart
  control_mode: automatic
  default_power_percent: 80
  target_temperature: 20
  external_temperature_sensor: room_temp

climate:
  - platform: vevor_heater
    name: "Workshop Thermostat"
    vevor_heater_id: my_heater
    min_temperature: 5
    max_temperature: 35
```

### Daily Schedule

```yaml
automation:
  - alias: "Morning Warmup"
    trigger:
      platform: time
      at: "07:00:00"
    condition:
      condition: numeric_state
      entity_id: sensor.workshop_temperature
      below: 15.0
    action:
      - lambda: "id(my_heater).turn_on();"
      - lambda: "id(my_heater).set_power_level_percent(70.0);"
``` -->

## Troubleshooting

### Common Issues

**No communication with heater:**
- Check wiring and protection circuit
- Verify UART pins are inverted (`inverted: true`)
- Check baud rate is 4800
- Ensure proper grounding

**Erratic readings:**
- Bus noise - improve protection circuit
- Check power supply stability
- Verify connections are secure

**Heater doesn't respond to commands:**
- Check communication timeout in logs
- Verify heater is powered on
- Ensure ESP32 is receiving data first

### Debug Logging

```yaml
logger:
  level: DEBUG
  logs:
    vevor_heater: VERY_VERBOSE
```

## Protocol Information

This library implements the Vevor heater communication protocol:
- **Baud Rate**: 4800
- **Frame Format**: Custom binary protocol
- **Communication**: Half-duplex, controller-initiated
- **Update Interval**: 1 second
- **Timeout**: 5 seconds

For detailed protocol information, see the [original project documentation](https://github.com/zatakon/vevor_heater_control).

## Contributing

Contributions welcome! Please:
1. Fork the repository
2. Create a feature branch
3. Test thoroughly
4. Submit a pull request

## License

MIT License - see LICENSE file for details.

## Acknowledgments

- Based on protocol reverse-engineering in my [other](https://github.com/zatakon) repo
- Inspired by the ESPHome community
- Thanks to all contributors and testers

## Support

For issues and questions:
1. Check existing [GitHub Issues](https://github.com/zatakon/esphome-vevor-heater/issues)
2. Create a new issue with:
   - ESPHome configuration
   - Debug logs
   - Hardware setup details
3. Join the discussion in the ESPHome community

---

## Support the Project

If you find this library helpful and want to support its development, consider buying me a coffee! ☕

Your support helps me dedicate more time to improving this project, adding new features, and maintaining compatibility with the latest ESPHome releases. Every contribution, no matter how small, is greatly appreciated! 🙏

[![Sponsor](https://img.shields.io/badge/Sponsor_via_Revolut-❤️_Buy_me_a_coffee-ff69b4?style=for-the-badge)](https://revolut.me/zatakon)

### Interested in the Protocol?

Check out the [**vevor_heater_control**](https://github.com/zatakon/vevor_heater_control) repository for reverse engineering details, protocol documentation, and help with adding new functions to this library. Contributions and discoveries are always welcome! 🔧

Thank you for using this library and being part of the community! 🎉

---

**Made with ❤️ for the ESPHome and Home Assistant community**
