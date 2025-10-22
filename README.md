# ESPHome Vevor Heater Library

[![ESPHome](https://img.shields.io/badge/ESPHome-Compatible-blue)](https://esphome.io)
[![Home Assistant](https://img.shields.io/badge/Home%20Assistant-Compatible-green)](https://www.home-assistant.io/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

An easy-to-use ESPHome library for controlling Vevor diesel heaters with full Home Assistant integration. Based on the original protocol reverse-engineering work from the [vevor_heater_control](https://github.com/zatakon/vevor_heater_control) project.

## 🌟 Features

- **Plug & Play**: Add just a few lines to your ESPHome configuration
- **Automatic Sensor Creation**: All sensors created automatically with sensible defaults
- **Home Assistant Integration**: Native climate entity support
- **Protocol Handling**: Robust communication with proper error handling
- **Safety Features**: Communication timeout detection and failsafe mechanisms
- **Comprehensive Monitoring**: Temperature, voltage, fan speed, pump frequency, and more
- **Easy Control**: Simple on/off and power level control
- **Climate Platform**: Full thermostat functionality for Home Assistant

## 🔧 Hardware Requirements

### Supported Heaters
- Vevor diesel heaters (2kW, 5kW models tested)
- Other heaters using the same protocol may work

### ESP32 Board
- ESP32-C3, ESP32, ESP32-S2, ESP32-S3
- Minimum 2 GPIO pins for UART communication

### Connection Circuit

⚠️ **Important**: The Vevor bus operates at ~4V with high noise. **DO NOT** connect directly to ESP32 pins!

You need a level shifter/protection circuit:

```
Vevor Bus ----[Protection Circuit]---- ESP32
    |                                    |
   4V                                  3.3V
```

Refer to the original project's [hardware documentation](https://github.com/zatakon/vevor_heater_control#4-hardware) for the protection circuit schematic.

## 🚀 Quick Start

### 1. Add to ESPHome Configuration

Add these lines to your ESPHome YAML:

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
```

That's it! This creates all sensors with automatic names and good defaults.

### 2. Add Manual Controls (Optional)

```yaml
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

### 3. Add Climate Integration (Optional)

```yaml
climate:
  - platform: vevor_heater
    name: "Workshop Climate"
    vevor_heater_id: my_heater
    min_temperature: 5
    max_temperature: 35
```

## 📊 Available Sensors

When `auto_sensors: true` (default), these sensors are automatically created:

| Sensor | Description | Unit | Device Class |
|--------|-------------|------|-------------|
| Temperature | Heat exchanger temperature | °C | Temperature |
| Input Voltage | Heater input voltage | V | Voltage |
| State | Current heater state | - | - |
| Power Level | Current power level | % | Power |
| Fan Speed | Combustion fan speed | RPM | - |
| Pump Frequency | Fuel pump frequency | Hz | - |
| Glow Plug Current | Glow plug current draw | A | Current |
| Heat Exchanger Temperature | Detailed heat exchanger temp | °C | Temperature |
| State Duration | Time in current state | s | Duration |
| Cooling Down | Cooling down status | - | - |

## ⚙️ Configuration Options

### Basic Configuration

```yaml
vevor_heater:
  id: my_heater
  uart_id: heater_uart
  auto_sensors: true          # Default: true
  target_temperature: 20.0    # Default target temp
  min_temperature: 5.0        # Minimum temperature
  max_temperature: 35.0       # Maximum temperature
```

### Custom Sensor Names

```yaml
vevor_heater:
  id: my_heater
  uart_id: heater_uart
  # Override specific sensor names
  temperature:
    name: "Workshop Temperature"
    filters:
      - calibrate_linear:
          - 0.0 -> 0.0
          - 25.0 -> 24.5  # Calibration if needed
  input_voltage:
    name: "Heater Input Voltage"
  state:
    name: "Heater Status"
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

## 🏠 Home Assistant Integration

### Climate Entity
The climate platform creates a native Home Assistant thermostat with:
- Current temperature display
- Target temperature control
- Heat/Off mode switching
- Visual temperature controls

### Sensor Entities
All sensors appear as individual entities with:
- Proper device classes for correct icons
- Historical data logging
- Use in automations and scripts

### Device Information
All entities are grouped under a single device for easy management.

## 🔄 Heater States

The heater reports these states:

| State | Description |
|-------|-------------|
| `Off` | Heater is off |
| `Glow Plug Preheat` | Warming up glow plug |
| `Ignited` | Fuel ignited, warming up |
| `Stable Combustion` | Running normally |
| `Stopping/Cooling` | Shutting down safely |
| `Disconnected` | Communication lost |

## 🔧 API Reference

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

## 🛡️ Safety Features

- **Communication Timeout**: Automatically detects lost communication
- **State Validation**: Verifies all received data
- **Checksum Verification**: Ensures data integrity
- **Failsafe Shutdown**: Safe heater shutdown on errors
- **Over-temperature Protection**: Configurable via automations

## 📜 Examples

### Freeze Protection

```yaml
automation:
  - alias: "Freeze Protection"
    trigger:
      platform: numeric_state
      entity_id: sensor.workshop_temperature
      below: 3.0
    action:
      - lambda: "id(my_heater).turn_on();"
      - lambda: "id(my_heater).set_power_level_percent(30.0);"
```

### Temperature Control

```yaml
automation:
  - alias: "Temperature Control"
    trigger:
      platform: numeric_state
      entity_id: sensor.workshop_temperature
      above: 25.0
    action:
      - lambda: "id(my_heater).turn_off();"
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
```

## 🛠️ Troubleshooting

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

## 📋 Protocol Information

This library implements the Vevor heater communication protocol:
- **Baud Rate**: 4800
- **Frame Format**: Custom binary protocol
- **Communication**: Half-duplex, controller-initiated
- **Update Interval**: 1 second
- **Timeout**: 5 seconds

For detailed protocol information, see the [original project documentation](https://github.com/zatakon/vevor_heater_control).

## 🤝 Contributing

Contributions welcome! Please:
1. Fork the repository
2. Create a feature branch
3. Test thoroughly
4. Submit a pull request

## 📜 License

MIT License - see LICENSE file for details.

## 🙏 Acknowledgments

- Based on protocol reverse-engineering by [zatakon](https://github.com/zatakon)
- Inspired by the ESPHome community
- Thanks to all contributors and testers

## 📞 Support

For issues and questions:
1. Check existing [GitHub Issues](https://github.com/zatakon/esphome-vevor-heater/issues)
2. Create a new issue with:
   - ESPHome configuration
   - Debug logs
   - Hardware setup details
3. Join the discussion in the ESPHome community

---

**Made with ❤️ for the ESPHome and Home Assistant community**