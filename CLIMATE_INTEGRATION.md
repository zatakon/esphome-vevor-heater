# Enhanced Climate Integration for VEVOR Heater

This document describes the enhanced climate sensor integration that provides comprehensive control over your VEVOR heater through Home Assistant's climate platform.

## Overview

The enhanced climate integration transforms your VEVOR heater into a smart thermostat with the following capabilities:

- **Temperature Control**: Set target temperatures and automatic heating control
- **Power Management**: Control heater power levels through fan modes or presets
- **Preset Modes**: Quick access to common power settings (Eco, Comfort, Normal, Boost)
- **Automatic Operation**: Temperature-based on/off control with external sensors
- **Home Assistant Integration**: Full compatibility with HA climate controls

## Features

### Core Climate Features
- ✅ On/Off control
- ✅ Target temperature setting
- ✅ Current temperature display
- ✅ Heating/Idle/Off status indication
- ✅ Temperature range configuration (5-35°C)
- ✅ Configurable temperature steps

### Advanced Features
- ✅ **Power Control via Fan Modes**: Use fan speed settings to control heater power
  - Low (30% power)
  - Medium (60% power) 
  - High (80% power)
  - Max (100% power)

- ✅ **Preset Modes**: Quick power level selection
  - Eco (30% power) - Energy saving mode
  - Comfort (60% power) - Comfortable heating
  - Normal (80% power) - Standard operation
  - Boost (100% power) - Maximum heating

- ✅ **Automatic Temperature Control**: Uses external temperature sensor for room temperature feedback
- ✅ **Manual Override**: Switch between climate control and manual operation
- ✅ **Safety Features**: Automatic shutdown on sensor failures

## Configuration

### Basic Climate Integration

```yaml
# External temperature sensor (required for automatic mode)
sensor:
  - platform: dht
    pin: GPIO2
    temperature:
      id: external_temp
      name: "Room Temperature"
    humidity:
      name: "Room Humidity"
    update_interval: 30s

# Main heater component
vevor_heater:
  id: my_heater
  uart_id: heater_serial
  control_mode: automatic
  external_temperature_sensor: external_temp
  target_temperature: 22.0
  min_temperature: 10.0
  max_temperature: 30.0
  auto_sensors: true

# Climate integration
climate:
  - platform: vevor_heater
    name: "VEVOR Heater Climate"
    vevor_heater_id: my_heater
    min_temperature: 10.0
    max_temperature: 30.0
    temperature_step: 0.5
    power_control: true      # Enable power control via fan modes
    supports_presets: true   # Enable preset modes
    default_preset: "Normal"
```

### Configuration Options

#### Climate Platform Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `vevor_heater_id` | ID | Required | Reference to the main heater component |
| `min_temperature` | Float | 5.0 | Minimum settable temperature (°C) |
| `max_temperature` | Float | 35.0 | Maximum settable temperature (°C) |
| `temperature_step` | Float | 1.0 | Temperature adjustment step size |
| `power_control` | Boolean | true | Enable power control via fan modes |
| `supports_presets` | Boolean | true | Enable preset mode support |
| `default_preset` | String | "Normal" | Default preset on startup |

#### Main Component Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `control_mode` | String | "manual" | `manual` or `automatic` |
| `external_temperature_sensor` | ID | - | External temperature sensor for automatic mode |
| `target_temperature` | Float | 20.0 | Default target temperature |
| `default_power_percent` | Float | 80.0 | Default power level (10-100%) |

## Usage in Home Assistant

### Climate Card
The heater will appear as a standard thermostat in Home Assistant:

- **Temperature Control**: Set target temperature with +/- buttons
- **Mode Selection**: Off/Heat modes
- **Preset Selection**: Eco/Comfort/Normal/Boost presets
- **Fan Control**: Low/Medium/High/Max power levels
- **Current Temperature**: Shows room temperature from external sensor

### Automation Examples

#### Basic Temperature Control
```yaml
automation:
  - alias: "Heat Living Room"
    trigger:
      - platform: time
        at: "07:00:00"
    action:
      - service: climate.set_temperature
        target:
          entity_id: climate.vevor_heater_climate
        data:
          temperature: 22
          hvac_mode: heat
```

#### Preset-Based Scheduling
```yaml
automation:
  - alias: "Eco Mode at Night"
    trigger:
      - platform: time
        at: "22:00:00"
    action:
      - service: climate.set_preset_mode
        target:
          entity_id: climate.vevor_heater_climate
        data:
          preset_mode: "Eco"
```

#### Power Level Control
```yaml
automation:
  - alias: "Boost Heating When Cold"
    trigger:
      - platform: numeric_state
        entity_id: sensor.room_temperature
        below: 18
    action:
      - service: climate.set_fan_mode
        target:
          entity_id: climate.vevor_heater_climate
        data:
          fan_mode: "Max"
```

## Control Modes

### Manual Mode
In manual mode, the climate integration provides:
- On/off control
- Power level control via presets or fan modes
- Target temperature setting (for display only)

### Automatic Mode
In automatic mode, the climate integration provides:
- Full thermostat functionality
- Automatic on/off based on room temperature
- Power level adjustment
- External temperature sensor integration

**Note**: Automatic mode requires an external temperature sensor to be configured.

## Safety Features

### Temperature Sensor Monitoring
- Automatic heater shutdown if external sensor becomes unavailable
- Configurable timeout periods
- Fallback to internal heater temperature sensor

### Communication Monitoring
- Heater status monitoring via UART communication
- Automatic retry on communication failures
- Status reporting in Home Assistant

### Power Level Limits
- Power levels constrained to safe ranges (10-100%)
- Gradual power adjustments to prevent thermal shock
- Overheat protection via built-in heater sensors

## Troubleshooting

### Common Issues

**Climate not responding to commands**
- Check UART wiring and baud rate (4800)
- Verify heater is powered and responding
- Check ESPHome logs for communication errors

**Temperature not updating**
- Verify external temperature sensor is configured
- Check sensor wiring and update interval
- Ensure sensor ID matches configuration

**Presets not working**
- Confirm `supports_presets: true` in configuration
- Check that preset names match exactly
- Verify heater responds to power level changes

### Debug Logging

Enable debug logging to troubleshoot issues:

```yaml
logger:
  level: DEBUG
  logs:
    vevor_heater: DEBUG
```

## Integration with Home Assistant Energy

Add power consumption tracking:

```yaml
sensor:
  - platform: template
    name: "Heater Power Consumption"
    unit_of_measurement: "W"
    device_class: power
    lambda: |-
      if (!id(my_heater).is_heating()) return 0.0;
      float power_percent = id(my_heater).get_power_level_percent();
      return (power_percent / 100.0) * 2000.0;  // Assuming 2kW max
```

## Advanced Customization

### Custom Preset Definitions
Modify preset power levels by editing the preset_power_map in the C++ code:

```cpp
std::map<std::string, float> preset_power_map_ = {
  {"Eco", 25.0f},     // Custom eco mode
  {"Comfort", 65.0f}, // Custom comfort mode
  {"Normal", 85.0f},  // Custom normal mode
  {"Boost", 100.0f}   // Maximum power
};
```

### Temperature Hysteresis
Implement temperature hysteresis in automation:

```yaml
automation:
  - alias: "Heater On with Hysteresis"
    trigger:
      - platform: numeric_state
        entity_id: sensor.room_temperature
        below: 21.0  # 1°C below target
    condition:
      - condition: numeric_state
        entity_id: climate.vevor_heater_climate
        attribute: temperature
        above: 21.5  # Only if target is above threshold
    action:
      - service: climate.turn_on
        target:
          entity_id: climate.vevor_heater_climate
```

## Migration from Basic Integration

To upgrade from the basic manual integration:

1. Update your external component source:
   ```yaml
   external_components:
     - source: github://zatakon/esphome-vevor-heater@enhanced-climate-integration
   ```

2. Add climate platform configuration
3. Configure external temperature sensor
4. Update automations to use climate services
5. Test thoroughly before deploying

## Support

For issues, feature requests, or contributions, please visit the [GitHub repository](https://github.com/zatakon/esphome-vevor-heater/tree/enhanced-climate-integration).
