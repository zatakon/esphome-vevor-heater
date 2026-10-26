# Changelog

All notable changes to the ESPHome Vevor Heater library will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **Semi-Auto and Full-Auto modes**: temperature-based power control (Semi-Auto) and power plus on/off control (Full-Auto) with an external temperature sensor; `control_mode: semi_auto` / `full_auto` in YAML
- **Start Lockout**: no automatic restart after the heater stops on its own (failed ignition, flame-out), new "Start Lockout" problem binary sensor
- **Low voltage restart delay**: after a low voltage stop, automatic modes wait `low_voltage_restart_delay` (default 30 min) and 0.3 V above `min_voltage_start` before starting again
- **External temperature timeout**: a value that stops updating counts as unavailable after `external_temperature_timeout` (default 30 min)
- **Saved settings**: control mode, target temperature, Manual power and `injected_per_pulse` changed from Home Assistant survive a reboot ("Manual" is restored as "Off")

### Fixed
- Builds with `mqtt:` failed with "Component ID was not declared" (#7)
- Daily consumption was reset on every reboot
- Semi-Auto increased power when the room was above target
- Frames with a bad checksum were accepted
- Values were only parsed when their sensor was configured (no fuel counting without the pump frequency sensor)
- Select and number entities started as "unknown" in Home Assistant
- Automatic modes started at `default_power_percent` instead of their computed power
- Low Voltage Error was cleared after one second
- Total pulse counter lost precision as a float (stored data is migrated)

### Changed
- **Daily consumption** is reported in L with device class `gas` (was ml)
- **`power_switch` removed**: use the "Manual" / "Off" options of `control_mode_select` or `turn_on()` / `turn_off()`
- Selecting "Manual" turns the heater on at the last Manual power
- Daily reset at local midnight uses the configured time zone; without a time source the daily counter is not reset

### Planned
- Complete climate entity integration
- Additional heater models support

## [1.3.1] - 2026-01-12

### Added
- **Antifreeze mode** with configurable temperature thresholds and hysteresis
- **Integrated controls**: control mode select, power switch, power level number and reset total consumption button
- **Total fuel consumption** sensor
- **Low voltage protection** with `min_voltage_start` / `min_voltage_operate`
- Configurable `polling_interval` for status requests while the heater is off

### Fixed
- Heat exchanger temperature with negative values (signed reading)
- Values are reported during the cooldown phase
- Daily consumption reset uses calendar time

### Changed
- Climate integration removed for now

## [1.2.0] - 2025-10-30

### Fixed
- **Daily Consumption Sensor "Unknown" State**: Sensor now retains its last value when heater is off
  - Value is loaded from flash on boot and published immediately
  - Sensor always shows a valid number instead of becoming "unknown"
  - Proper initialization in `setup()` and `load_fuel_consumption_data()`

### Changed
- **Configuration Simplification**: `injected_per_pulse_number` is now optional in YAML
  - Users don't need to include it unless they want to expose it as a configurable number in Home Assistant
  - Default values (min: 0.001, max: 1.0, step: 0.001) are set in Python configuration
  - Reduces YAML configuration complexity for basic usage

### Improved
- Better persistent storage handling for fuel consumption data
- Enhanced documentation for fuel consumption tracking

## [1.1.0] - 2025-10-29

### Added
- **Fuel Consumption Tracking**: Accurate monitoring based on pump frequency
  - Hourly consumption sensor showing instantaneous rate in ml/h
  - Daily consumption sensor with automatic midnight reset
  - Persistent storage saves data to flash every 30 seconds
  - Survives reboots by loading saved values from flash memory
  - Configurable `injected_per_pulse` parameter (default 0.022 ml)
  - Optional Home Assistant number entity for runtime calibration

### Fixed
- **Temperature Sensor Issues**:
  - Removed duplicate temperature sensor (only "Heat Exchanger Temperature" remains)
  - Fixed temperature scaling bug (was 10x too small - changed from `/100.0f` to `/10.0f`)
  - Proper temperature readings now displayed

### Changed
- Improved sensor naming consistency
- Enhanced logging for fuel consumption tracking
- Updated documentation with fuel tracking details

## [1.0.0] - 2025-10-15

### Added
- Initial release of ESPHome Vevor Heater library
- **Core Features**:
  - UART communication with Vevor diesel heaters (4800 baud)
  - Manual control mode with power level adjustment (10-100%)
  - Automatic sensor creation with sensible defaults
  - Home Assistant integration
  
- **Sensors**:
  - Heat Exchanger Temperature
  - Input Voltage
  - Heater State (text sensor)
  - Power Level
  - Fan Speed
  - Pump Frequency
  - Glow Plug Current
  - State Duration
  - Cooling Down (binary sensor)

- **Control Methods**:
  - `turn_on()` / `turn_off()`
  - `set_power_level_percent()`
  - Template switch and number components for Home Assistant

- **Safety Features**:
  - Communication timeout detection
  - Frame validation and checksum verification
  - Proper state management
  - Failsafe shutdown on errors

- **Configuration Options**:
  - Automatic sensor creation (auto_sensors: true by default)
  - Manual sensor override capability
  - Configurable default power level
  - UART pin configuration with inversion support

### Documentation
- Comprehensive README with examples
- Hardware connection diagrams
- Quick start guide
- API reference
- Troubleshooting section

---

## Version History Summary

- **v1.3.1** - Antifreeze mode, integrated controls, low voltage protection
- **v1.2.0** - Daily consumption sensor fix and configuration simplification
- **v1.1.0** - Fuel consumption tracking and temperature sensor fixes
- **v1.0.0** - Initial public release

---

## Breaking Changes

### From 1.0.0 to 1.1.0
- **Temperature Sensor Removed**: Duplicate temperature sensor was removed. Use "Heat Exchanger Temperature" sensor instead.
- **Temperature Values Changed**: Temperature readings are now 10x larger (correct scale). Update any automations that depend on temperature thresholds.

### From 1.1.0 to 1.2.0
- **No Breaking Changes**: This is a bug fix release. All existing configurations remain compatible.

### From 1.3.1 to the next release
- **`power_switch` removed**: remove it from YAML, use `control_mode_select` ("Manual" / "Off") or a template switch with `turn_on()` / `turn_off()`.
- **Daily consumption unit changed from ml to L**: Home Assistant starts new long-term statistics for this sensor; update automations that compare the value.

---

## Migration Guides

### Migrating to v1.2.0
No migration needed - fully backward compatible with v1.1.0.

### Migrating to v1.1.0 from v1.0.0

**Temperature Sensor Changes:**
```yaml
# Old (v1.0.0) - REMOVE THIS
sensor:
  - platform: vevor_heater
    temperature:
      name: "Heater Temperature"

# New (v1.1.0+) - Already created automatically
# Use "Heat Exchanger Temperature" sensor
# No configuration needed - it's auto-created
```

**Temperature Threshold Updates:**
If you have automations using temperature:
```yaml
# Old threshold (v1.0.0)
below: 5.0  # This was actually 50°C

# New threshold (v1.1.0+)
below: 50.0  # Correct value
```

---

## Support

For bug reports and feature requests, please open an issue on [GitHub](https://github.com/zatakon/esphome-vevor-heater/issues).

For protocol details and reverse engineering, see [vevor_heater_control](https://github.com/zatakon/vevor_heater_control).
