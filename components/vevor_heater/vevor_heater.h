#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/hal.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/number/number.h"
#include "esphome/components/button/button.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/preferences.h"
#include <vector>

namespace esphome {

// Forward declaration for optional time component
namespace time { class RealTimeClock; }

namespace vevor_heater {

static const char *const TAG = "vevor_heater";

// Fuel consumption constants
static const float INJECTED_PER_PULSE = 0.022f; // ml per fuel pump pulse

// Control modes
enum class ControlMode : uint8_t {
  MANUAL = 0,
  AUTOMATIC = 1,
  ANTIFREEZE = 2
};

// Heater states from protocol analysis
enum class HeaterState : uint8_t {
  OFF = 0x00,
  POLLING_STATE = 0x01,  // Used for status polling (was GLOW_PLUG_PREHEAT)
  HEATING_UP = 0x02,
  STABLE_COMBUSTION = 0x03,
  STOPPING_COOLING = 0x04,
  UNKNOWN = 0xFF
};

// Controller command states
enum class ControllerState : uint8_t {
  CMD_OFF = 0x02,
  CMD_START = 0x06,
  CMD_RUNNING = 0x08
};

// Communication constants
static const uint8_t FRAME_START = 0xAA;
static const uint8_t CONTROLLER_ID = 0x66;
static const uint8_t HEATER_ID = 0x77;
static const uint8_t CONTROLLER_FRAME_LENGTH = 0x0B;
static const uint8_t HEATER_FRAME_LENGTH = 0x33;
static const uint32_t COMMUNICATION_TIMEOUT_MS = 5000;
static const uint32_t SEND_INTERVAL_MS = 1000;
static const uint32_t DEFAULT_POLLING_INTERVAL_MS = 300000; // 1 minute when not heating

// Fuel consumption tracking structure for persistence
struct FuelConsumptionData {
  float daily_consumption_ml;
  uint32_t last_reset_day;
  float total_pulses;  // Keep as float to avoid precision loss
};

class VevorHeater : public PollingComponent, public uart::UARTDevice {
 public:
  // Configuration methods
  void set_target_temperature(float temperature) { target_temperature_ = temperature; }
  void set_power_level(uint8_t level) { 
    power_level_ = std::max(1, std::min(10, (int)level)); 
  }
  void set_control_mode(ControlMode mode);
  void set_default_power_percent(float percent) { default_power_percent_ = percent; }
  void set_injected_per_pulse(float ml_per_pulse) { injected_per_pulse_ = ml_per_pulse; }
  float get_injected_per_pulse() const { return injected_per_pulse_; }
  void set_polling_interval(uint32_t interval_ms) { polling_interval_ms_ = interval_ms; }
  void set_min_voltage_start(float voltage) { min_voltage_start_ = voltage; }
  void set_min_voltage_operate(float voltage) { min_voltage_operate_ = voltage; }
  void set_antifreeze_temp_on(float temp) { antifreeze_temp_on_ = temp; }
  void set_antifreeze_temp_medium(float temp) { antifreeze_temp_medium_ = temp; }
  void set_antifreeze_temp_low(float temp) { antifreeze_temp_low_ = temp; }
  void set_antifreeze_temp_off(float temp) { antifreeze_temp_off_ = temp; }
  
  // Time component setter
  void set_time_component(time::RealTimeClock *time) { time_component_ = time; }
  
  // Number component setter
  void set_injected_per_pulse_number(number::Number *num) { injected_per_pulse_number_ = num; }
  
  // External temperature sensor
  void set_external_temperature_sensor(sensor::Sensor *sensor) { external_temperature_sensor_ = sensor; }
  
  // Sensor setters - removed duplicate set_temperature_sensor
  void set_input_voltage_sensor(sensor::Sensor *sensor) { input_voltage_sensor_ = sensor; }
  void set_state_sensor(text_sensor::TextSensor *sensor) { state_sensor_ = sensor; }
  void set_power_level_sensor(sensor::Sensor *sensor) { power_level_sensor_ = sensor; }
  void set_fan_speed_sensor(sensor::Sensor *sensor) { fan_speed_sensor_ = sensor; }
  void set_pump_frequency_sensor(sensor::Sensor *sensor) { pump_frequency_sensor_ = sensor; }
  void set_glow_plug_current_sensor(sensor::Sensor *sensor) { glow_plug_current_sensor_ = sensor; }
  void set_heat_exchanger_temperature_sensor(sensor::Sensor *sensor) { heat_exchanger_temperature_sensor_ = sensor; }
  void set_state_duration_sensor(sensor::Sensor *sensor) { state_duration_sensor_ = sensor; }
  void set_cooling_down_sensor(binary_sensor::BinarySensor *sensor) { cooling_down_sensor_ = sensor; }
  void set_hourly_consumption_sensor(sensor::Sensor *sensor) { hourly_consumption_sensor_ = sensor; }
  void set_daily_consumption_sensor(sensor::Sensor *sensor) { daily_consumption_sensor_ = sensor; }
  void set_total_consumption_sensor(sensor::Sensor *sensor) { total_consumption_sensor_ = sensor; }
  void set_low_voltage_error_sensor(binary_sensor::BinarySensor *sensor) { low_voltage_error_sensor_ = sensor; }
  
  // Control methods
  void turn_on();
  void turn_off();
  void set_power_level_percent(float percent);
  void reset_daily_consumption();
  void reset_total_consumption();
  
  // Control mode management
  bool is_automatic_mode() const { return control_mode_ == ControlMode::AUTOMATIC; }
  bool is_manual_mode() const { return control_mode_ == ControlMode::MANUAL; }
  bool is_antifreeze_mode() const { return control_mode_ == ControlMode::ANTIFREEZE; }
  float get_external_temperature() const { return external_temperature_; }
  bool has_external_sensor() const { 
    return external_temperature_sensor_ != nullptr && 
           !std::isnan(external_temperature_); 
  }
  
  // Status getters
  HeaterState get_heater_state() const { return current_state_; }
  float get_current_temperature() const { return current_temperature_; }
  bool is_heating() const { 
    return current_state_ == HeaterState::POLLING_STATE || 
           current_state_ == HeaterState::HEATING_UP || 
           current_state_ == HeaterState::STABLE_COMBUSTION; 
  }
  bool is_connected() const { return last_received_time_ + COMMUNICATION_TIMEOUT_MS > millis(); }
  bool has_low_voltage_error() const { return low_voltage_error_; }
  
  // Fuel consumption getters
  float get_daily_consumption() const { return daily_consumption_ml_; }
  float get_instantaneous_consumption_rate() const { return pump_frequency_ * injected_per_pulse_ * 3600.0f; }
  
  // Component lifecycle
  void setup() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  // Communication handling
  void send_controller_frame();
  void process_heater_frame(const std::vector<uint8_t> &frame);
  void check_uart_data();
  bool validate_frame(const std::vector<uint8_t> &frame, uint8_t expected_length);
  
  // Data parsing helpers
  uint16_t read_uint16_be(const std::vector<uint8_t> &data, size_t offset);
  float parse_temperature(const std::vector<uint8_t> &data, size_t offset);
  float parse_voltage(const std::vector<uint8_t> &data, size_t offset);
  const char* state_to_string(HeaterState state);
  
  // State management
  void update_sensors(const std::vector<uint8_t> &frame);
  void handle_communication_timeout();
  void check_voltage_safety();
  void handle_antifreeze_mode();
  
  // Fuel consumption tracking
  void update_fuel_consumption(float pump_frequency);
  void save_fuel_consumption_data();
  void load_fuel_consumption_data();
  void check_daily_reset();
  uint32_t get_days_since_epoch();
  
  // Communication state
  std::vector<uint8_t> rx_buffer_;
  uint32_t last_received_time_{0};
  uint32_t last_send_time_{0};
  bool frame_sync_{false};
  uint32_t polling_interval_ms_{DEFAULT_POLLING_INTERVAL_MS};
  
  // Control state
  bool heater_enabled_{false};
  uint8_t power_level_{8};  // 1-10 scale, default 80%
  float target_temperature_{20.0};
  HeaterState current_state_{HeaterState::OFF};
  ControlMode control_mode_{ControlMode::MANUAL};
  float default_power_percent_{80.0};
  float injected_per_pulse_{INJECTED_PER_PULSE};
  float min_voltage_start_{12.3f};      // Minimum voltage to allow starting
  float min_voltage_operate_{11.4f};    // Minimum voltage to keep running
  float antifreeze_temp_on_{2.0f};      // Temperature to turn on at 80% power
  float antifreeze_temp_medium_{6.0f};  // Temperature to set 50% power
  float antifreeze_temp_low_{8.0f};     // Temperature to set 20% power
  float antifreeze_temp_off_{9.0f};     // Temperature to turn off
  static constexpr float ANTIFREEZE_HYSTERESIS = 0.4f;  // Hysteresis in °C to prevent rapid cycling
  float last_antifreeze_power_{0.0f};   // Track last power level for hysteresis logic
  bool antifreeze_active_{false};       // Track if antifreeze is actively heating
  
  // Parsed sensor values
  float current_temperature_{0.0};
  float external_temperature_{NAN};
  float input_voltage_{0.0};
  float heat_exchanger_temperature_{0.0};
  uint16_t fan_speed_{0};
  float pump_frequency_{0.0};
  float glow_plug_current_{0.0};
  uint16_t state_duration_{0};
  bool cooling_down_{false};
  bool low_voltage_error_{false};
  
  // Fuel consumption tracking
  float last_pump_frequency_{0.0};
  uint32_t last_consumption_update_{0};
  float daily_consumption_ml_{0.0};
  uint32_t current_day_{0};
  float total_fuel_pulses_{0.0};  // Keep as float to avoid precision loss
  float total_consumption_ml_{0.0};  // Lifetime total consumption
  ESPPreferenceObject pref_fuel_consumption_;
  
  // Time component pointer
  time::RealTimeClock *time_component_{nullptr};
  bool time_sync_warning_shown_{false};
  
  // Sensor pointers - removed duplicate temperature_sensor_
  sensor::Sensor *external_temperature_sensor_{nullptr};
  sensor::Sensor *input_voltage_sensor_{nullptr};
  text_sensor::TextSensor *state_sensor_{nullptr};
  sensor::Sensor *power_level_sensor_{nullptr};
  sensor::Sensor *fan_speed_sensor_{nullptr};
  sensor::Sensor *pump_frequency_sensor_{nullptr};
  sensor::Sensor *glow_plug_current_sensor_{nullptr};
  sensor::Sensor *heat_exchanger_temperature_sensor_{nullptr};
  sensor::Sensor *state_duration_sensor_{nullptr};
  binary_sensor::BinarySensor *cooling_down_sensor_{nullptr};
  sensor::Sensor *hourly_consumption_sensor_{nullptr};
  sensor::Sensor *daily_consumption_sensor_{nullptr};
  sensor::Sensor *total_consumption_sensor_{nullptr};
  binary_sensor::BinarySensor *low_voltage_error_sensor_{nullptr};
  number::Number *injected_per_pulse_number_{nullptr};
};

// Number component for injected per pulse configuration
class VevorInjectedPerPulseNumber : public number::Number, public Component {
 public:
  void set_vevor_heater(VevorHeater *heater) { heater_ = heater; }
  
  void setup() override {
    if (heater_) {
      float value = heater_->get_injected_per_pulse();
      this->publish_state(value);
    }
  }
  
 protected:
  void control(float value) override {
    if (heater_) {
      heater_->set_injected_per_pulse(value);
      this->publish_state(value);
    }
  }
  
  VevorHeater *heater_{nullptr};
};

// Button component for resetting total consumption
class VevorResetTotalConsumptionButton : public button::Button, public Component {
 public:
  void set_vevor_heater(VevorHeater *heater) { heater_ = heater; }
  
 protected:
  void press_action() override {
    if (heater_) {
      heater_->reset_total_consumption();
    }
  }
  
  VevorHeater *heater_{nullptr};
};

// Switch component for heater power control (Manual mode only)
class VevorHeaterPowerSwitch : public switch_::Switch, public Component {
 public:
  void set_vevor_heater(VevorHeater *heater) { heater_ = heater; }
  
 protected:
  void write_state(bool state) override {
    if (heater_) {
      // Only allow control in manual mode
      if (!heater_->is_manual_mode()) {
        ESP_LOGW("vevor_heater", "Power switch only works in Manual mode");
        // Restore previous state
        this->publish_state(!state);
        return;
      }
      
      if (state) {
        heater_->turn_on();
      } else {
        heater_->turn_off();
      }
      this->publish_state(state);
    }
  }
  
  VevorHeater *heater_{nullptr};
};

// Number component for power level control (Manual mode only)
class VevorHeaterPowerLevelNumber : public number::Number, public Component {
 public:
  void set_vevor_heater(VevorHeater *heater) { heater_ = heater; }
  
  void setup() override {
    if (heater_) {
      this->publish_state(80.0f);  // Default power level
    }
  }
  
 protected:
  void control(float value) override {
    if (heater_) {
      // Only allow control in manual mode
      if (!heater_->is_manual_mode()) {
        ESP_LOGW("vevor_heater", "Power level only works in Manual mode");
        return;
      }
      
      heater_->set_power_level_percent(value);
      this->publish_state(value);
    }
  }
  
  VevorHeater *heater_{nullptr};
};

// Select component for control mode
class VevorControlModeSelect : public select::Select, public Component {
 public:
  void set_vevor_heater(VevorHeater *heater) { heater_ = heater; }
  
  void setup() override {
    // Set initial value based on heater's mode
    if (heater_) {
      if (heater_->is_manual_mode()) {
        this->publish_state("Manual");
      } else if (heater_->is_antifreeze_mode()) {
        this->publish_state("Antifreeze");
      }
      // Automatic mode commented out for now
    }
  }
  
 protected:
  void control(const std::string &value) override {
    if (heater_) {
      if (value == "Manual") {
        heater_->set_control_mode(ControlMode::MANUAL);
      } else if (value == "Antifreeze") {
        heater_->set_control_mode(ControlMode::ANTIFREEZE);
      }
      // else if (value == "Automatic") {
      //   heater_->set_control_mode(ControlMode::AUTOMATIC);
      // }
      this->publish_state(value);
    }
  }
  
  VevorHeater *heater_{nullptr};
};

}  // namespace vevor_heater
}  // namespace esphome