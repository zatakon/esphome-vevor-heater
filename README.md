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
- **Multiple Control Modes**: 
  - **Off**: Heater completely disabled
  - **Manual**: Direct power level control by user
  - **Semi-Auto**: Automatic power adjustment, user controls on/off
  - **Full-Auto**: Complete automatic control (power + on/off)
  - **Antifreeze**: Temperature-based freeze protection
- **Smart Automatic Control**: Temperature-based power adjustment with configurable thresholds
- **Antifreeze Protection**: Automatically adjusts heater power based on temperature thresholds to prevent freezing
- **Integrated Controls**: Single control mode selector for all operating modes
- **Hysteresis**: Prevents rapid power cycling with smart temperature control
- **Smart On/Off Control**: Default 80% power level on startup (configurable)
- **External Temperature Sensor Support**: Use any ESPHome temperature sensor (e.g., AHT10, DHT22, BMP280)
- **Protocol Handling**: Robust communication with proper error handling
- **Safety Features**: Communication timeout detection, low voltage protection, and failsafe mechanisms
- **Comprehensive Monitoring**: Temperature, voltage, fan speed, pump frequency, fuel consumption, and more
- **Easy Control**: Simple unified control mode selector
- **Climate Platform**: Full thermostat functionality for Home Assistant (coming soon)

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

## Versioning

This library uses semantic versioning. The version is embedded in your ESPHome device configuration:

```yaml
esphome:
  name: ${friendly_name}
  project:
    name: "zatakon.vevor-heater"
    version: "1.3.0"  # Current library version
```

The version number appears in:
- Home Assistant device information
- ESPHome dashboard
- Device logs

**Current Version: 1.3.0** - Automatic mode with Semi-Auto and Full-Auto control

### Using Different Versions

**Latest stable release (recommended):**
```yaml
external_components:
  - source: github://zatakon/esphome-vevor-heater
    components: [vevor_heater]
```

**Specific version (for stability):**
```yaml
external_components:
  - source: github://zatakon/esphome-vevor-heater@v1.3.0
    components: [vevor_heater]
```

**Development branch (latest features, may be unstable):**
```yaml
external_components:
  - source: github://zatakon/esphome-vevor-heater@develop
    components: [vevor_heater]
```

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
  default_power_percent: 80      # Starts at 80% power when turned on
  
  # Integrated control mode selector
  control_mode_select:
    name: "Heater Control Mode"
  power_level_number:
    name: "Heater Power Level"
  target_temperature_number:
    name: "Target Temperature"
```

#### Automatic Modes (Semi-Auto and Full-Auto)

The library provides two automatic control modes for intelligent temperature-based heating:

**Semi-Auto Mode:**
- Automatically adjusts power level every 20 seconds based on temperature difference
- **You control when heater turns on/off** (via Home Assistant or automations)
- Power adjusts to match heating needs
- Requires external temperature sensor

**Full-Auto Mode:**
- Complete automatic control - both power level AND on/off
- Heater turns on when temperature drops below target by configured threshold (default: -3°C)
- Heater turns off when temperature rises above target by configured threshold (default: +2°C)
- Power adjusts every 20 seconds to match heating needs
- Requires external temperature sensor

**Power Adjustment Logic (Both Modes):**
- When heater starts, initial power is set based on current temperature difference
- Every 20 seconds, power adjusts by ±10% maximum
- Power levels based on temperature difference from target:
  - < 1°C difference → 10% power
  - 1-2°C difference → 20% power
  - 2-3°C difference → 40% power
  - 4-5°C difference → 60% power
  - 5-6°C difference → 80% power
  - > 6°C difference → 90% power

**Control Mode Options:**
- **Off**: Heater completely disabled
- **Manual**: User controls on/off and power manually
- **Semi-Auto**: Power adjusts automatically, you control on/off
- **Full-Auto**: Complete automatic control (power + on/off)
- **Antifreeze**: Temperature-based freeze protection (see below)

#### Antifreeze Mode (Temperature-Based Protection)

In antifreeze mode, the heater automatically adjusts power based on ambient temperature to prevent freezing:
- Automatically turns on and adjusts power when temperature drops
- External temperature sensor is **MANDATORY**
- Configurable temperature thresholds for different power levels
- Hysteresis prevents rapid power cycling

**Power Levels by Temperature:**
- Below 2°C: 80% power (max heating)
- 6°C and above: 50% power
- 8°C and above: 20% power (minimal heating)
- 9°C and above: Heater off

```yaml
# I2C for temperature sensor
i2c:
  sda: GPIO7
  scl: GPIO8

# External temperature sensor (REQUIRED for antifreeze mode)
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

# Heater with antifreeze mode
vevor_heater:
  id: my_heater
  uart_id: heater_uart
  external_temperature_sensor: room_temp  # MANDATORY for antifreeze mode
  
  # Optional: Customize antifreeze thresholds (defaults shown)
  antifreeze_temp_on: 2.0        # Start heating below this temp (80% power)
  antifreeze_temp_medium: 6.0    # Switch to 50% power above this
  antifreeze_temp_low: 8.0       # Switch to 20% power above this
  antifreeze_temp_off: 9.0       # Turn off above this temp
  
  # Optional: Add integrated controls
  control_mode_select:
    name: "Heater Control Mode"
  power_switch:
    name: "Heater Power"
  power_level_number:
    name: "Heater Power Level"
  reset_total_consumption_button:
    name: "Reset Total Fuel Consumption"
```

**Integrated Controls:**
- **Control Mode Select**: Switch between Manual and Antifreeze modes in Home Assistant
- **Power Switch**: Turn heater on/off (only works in Manual mode)
- **Power Level Number**: Adjust power 10-100% (only works in Manual mode)
- **Reset Button**: Reset total fuel consumption counter

**Hysteresis:** The heater uses 0.4°C hysteresis to prevent rapid on/off cycling at temperature boundaries.

### Comparison of Control Modes

| Mode | On/Off Control | Power Control | External Sensor | Use Case |
|------|----------------|---------------|-----------------|----------|
| **Off** | Disabled | - | No | Heater completely off |
| **Manual** | User | User (10-100%) | No | Full manual control |
| **Semi-Auto** | **User** | **Automatic** | **Yes** | You decide when to heat, heater optimizes power |
| **Full-Auto** | **Automatic** | **Automatic** | **Yes** | Set target temp and forget |
| **Antifreeze** | Automatic | Automatic | **Yes** | Freeze protection mode |

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

When `auto_sensors: true` (default), these sensors are automatically created:

| Sensor                       | Description                   | Unit | Device Class |
|------------------------------|-------------------------------|------|--------------|
| Heat Exchanger Temperature   | Main heater temperature       | °C   | Temperature  |
| Input Voltage                | Heater input voltage          | V    | Voltage      |
| State                        | Current heater state          | -    | -            |
| Power Level                  | Current power level           | %    | Power        |
| Fan Speed                    | Combustion fan speed          | RPM  | -            |
| Pump Frequency               | Fuel pump frequency           | Hz   | -            |
| Glow Plug Current            | Glow plug current draw        | A    | Current      |
| State Duration               | Time in current state         | s    | Duration     |
| Cooling Down                 | Cooling down status           | -    | -            |
| Hourly Consumption           | Instantaneous fuel rate       | ml/h | -            |
| Daily Consumption            | Total fuel consumed today     | ml   | -            |
| Total Consumption            | Cumulative fuel consumption   | L    | -            |

### Fuel Consumption Tracking

The library includes accurate fuel consumption monitoring with persistent storage:

- **Hourly Consumption**: Shows instantaneous fuel consumption rate (ml/h) based on current pump frequency
- **Daily Consumption**: Accumulates total fuel consumed since midnight, persists across reboots
- **Total Consumption**: Cumulative fuel consumption with manual reset capability
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
  
  # Optional: Add reset button for total consumption
  reset_total_consumption_button:
    name: "Reset Total Fuel Consumption"
```

The fuel counter automatically resets daily consumption at midnight and saves total consumption data to flash memory to survive reboots.

## Configuration Options

### Antifreeze Mode Configuration

Configure temperature thresholds for antifreeze protection:

```yaml
vevor_heater:
  id: my_heater
  uart_id: heater_uart
  external_temperature_sensor: room_temp
  
  # Antifreeze thresholds (all optional, defaults shown)
  antifreeze_temp_on: 2.0        # Start heating below this (80% power)
  antifreeze_temp_medium: 6.0    # Switch to 50% power above this
  antifreeze_temp_low: 8.0       # Switch to 20% power above this
  antifreeze_temp_off: 9.0       # Turn off above this
```

**How Antifreeze Mode Works:**

Antifreeze mode provides automatic freeze protection by dynamically adjusting heater power based on ambient temperature. It's designed to prevent freezing conditions while minimizing fuel consumption.

**Temperature Zones and Power Levels:**

The heater operates in different power zones based on temperature:

| Temperature Range | Power Level | Behavior |
|------------------|-------------|----------|
| Below `antifreeze_temp_on` (< 2.0°C) | **80%** | Maximum heating - Critical freeze protection |
| `antifreeze_temp_on` to `antifreeze_temp_medium` (2.0-6.0°C) | **80%** | High power zone |
| `antifreeze_temp_medium` to `antifreeze_temp_low` (6.0-8.0°C) | **50%** | Medium power zone |
| `antifreeze_temp_low` to `antifreeze_temp_off` (8.0-9.0°C) | **20%** | Low power zone |
| Above `antifreeze_temp_off` (≥ 9.0°C) | **OFF** | No heating needed |

**Hysteresis Behavior:**

To prevent rapid power cycling when temperature oscillates, the system uses **0.4°C hysteresis** when *increasing* power:

- **20% → 50%**: Power increases only when temperature drops below `antifreeze_temp_low - 0.4°C` (e.g., < 7.6°C with defaults)
- **50% → 80%**: Power increases only when temperature drops below `antifreeze_temp_medium - 0.4°C` (e.g., < 5.6°C with defaults)

When temperature *rises*, power decreases immediately without hysteresis. This ensures quick response to warming conditions while preventing excessive power cycling during temperature drops.

**Automatic Startup:**

The heater automatically turns ON at 80% power when temperature falls below `antifreeze_temp_on`. Once the temperature rises above `antifreeze_temp_off`, it automatically turns OFF.

**Example Configuration for Garage Protection:**

```yaml
vevor_heater:
  id: garage_heater
  uart_id: heater_uart
  control_mode: antifreeze
  external_temperature_sensor: garage_temp
  
  # Custom thresholds for garage (keep above freezing)
  antifreeze_temp_on: 1.0        # Start at 80% below 1°C
  antifreeze_temp_medium: 3.0    # Drop to 50% above 3°C
  antifreeze_temp_low: 5.0       # Drop to 20% above 5°C
  antifreeze_temp_off: 7.0       # Turn off above 7°C
```

**Important Notes:**

- External temperature sensor is **MANDATORY** for antifreeze mode
- If the sensor fails or becomes unavailable, the heater will automatically shut down for safety
- In antifreeze mode, manual power controls are disabled - all operation is temperature-driven
- The system continuously monitors temperature and adjusts power levels automatically

### Integrated Control Components

Add direct control components for mode selection and power control:

```yaml
vevor_heater:
  id: my_heater
  uart_id: heater_uart
  
  # Optional control components
  control_mode_select:
    name: "Control Mode"           # Manual/Antifreeze selector
  power_switch:
    name: "Power"                  # On/off control (manual mode only)
  power_level_number:
    name: "Power Level"            # 10-100% control (manual mode only)
  reset_total_consumption_button:
    name: "Reset Total Consumption"
```

**Note:** Power switch and power level only function in Manual mode. In Antifreeze mode, these controls are disabled.

### Low Voltage Protection

Configure voltage thresholds for safe operation:

```yaml
vevor_heater:
  id: my_heater
  uart_id: heater_uart
  
  # Optional voltage thresholds (defaults shown)
  min_voltage_start: 10.5        # Minimum voltage to allow starting
  min_voltage_operate: 10.0      # Minimum voltage during operation
```

The heater will refuse to start below `min_voltage_start` and will shut down if voltage drops below `min_voltage_operate`.

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
- **Low Voltage Protection**: Prevents starting or operation below configurable voltage thresholds
- **Antifreeze Protection**: Temperature-based automatic control with hysteresis to prevent freezing
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

## Support the Project

For issues and questions:
1. Check existing [GitHub Issues](https://github.com/zatakon/esphome-vevor-heater/issues)
2. Create a new issue with:
   - ESPHome configuration
   - Debug logs
   - Hardware setup details
3. Join the discussion in the ESPHome community

If you find this library helpful and want to support its development, consider buying me a coffee! ☕

Your support helps me dedicate more time to improving this project, adding new features, and maintaining compatibility with the latest ESPHome releases. Every contribution, no matter how small, is greatly appreciated! 🙏

[![Sponsor](https://img.shields.io/badge/Sponsor_via_Revolut-❤️_Buy_me_a_coffee-ff69b4?style=for-the-badge)](https://revolut.me/zatakon)

### Interested in the Protocol?

Check out the [**vevor_heater_control**](https://github.com/zatakon/vevor_heater_control) repository for reverse engineering details, protocol documentation, and help with adding new functions to this library. Contributions and discoveries are always welcome! 🔧

Thank you for using this library and being part of the community! 🎉

---

**Made with ❤️ for the ESPHome and Home Assistant community**
