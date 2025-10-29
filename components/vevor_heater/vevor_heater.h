#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/hal.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/climate/climate.h"
#include <vector>
#include <map>
#include <string>

namespace esphome {
namespace vevor_heater {

static const char *const TAG = "vevor_heater";

// Control modes
enum class ControlMode : uint8_t {
  MANUAL = 0,
  AUTOMATIC = 1
};

// Heater states from protocol analysis
enum class HeaterState : uint8_t {
  OFF = 0x00,
  GLOW_PLUG_PREHEAT = 0x01,
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

// Climate preset modes
enum class ClimatePreset : uint8_t {
  ECO = 0,      // 30% power
  COMFORT = 1,  // 60% power
  NORMAL = 2,   // 80% power
  BOOST = 3     // 100% power
};

// Communication constants
static const uint8_t FRAME_START = 0xAA;
static const uint8_t CONTROLLER_ID = 0x66;
static const uint8_t HEATER_ID = 0x77;
static const uint8_t CONTROLLER_FRAME_LENGTH = 0x0B;
static const uint8_t HEATER_FRAME_LENGTH = 0x33;
static const uint32_t COMMUNICATION_TIMEOUT_MS = 5000;
static const uint32_t SEND_INTERVAL_MS = 1000;

class VevorHeater : public PollingComponent, public uart::UARTDevice {
 public:
  // Configuration methods
  void set_target_temperature(float temperature) { target_temperature_ = temperature; }
  void set_power_level(uint8_t level) { 
    power_level_ = std::max(1, std::min(10, (int)level)); 
  }
  void set_control_mode(ControlMode mode) { control_mode_ = mode; }
  void set_default_power_percent(float percent) { default_power_percent_ = percent; }
  
  // External temperature sensor
  void set_external_temperature_sensor(sensor::Sensor *sensor) { external_temperature_sensor_ = sensor; }
  
  // Sensor setters
  void set_temperature_sensor(sensor::Sensor *sensor) { temperature_sensor_ = sensor; }
  void set_input_voltage_sensor(sensor::Sensor *sensor) { input_voltage_sensor_ = sensor; }
  void set_state_sensor(text_sensor::TextSensor *sensor) { state_sensor_ = sensor; }
  void set_power_level_sensor(sensor::Sensor *sensor) { power_level_sensor_ = sensor; }
  void set_fan_speed_sensor(sensor::Sensor *sensor) { fan_speed_sensor_ = sensor; }
  void set_pump_frequency_sensor(sensor::Sensor *sensor) { pump_frequency_sensor_ = sensor; }
  void set_glow_plug_current_sensor(sensor::Sensor *sensor) { glow_plug_current_sensor_ = sensor; }
  void set_heat_exchanger_temperature_sensor(sensor::Sensor *sensor) { heat_exchanger_temperature_sensor_ = sensor; }
  void set_state_duration_sensor(sensor::Sensor *sensor) { state_duration_sensor_ = sensor; }
  void set_cooling_down_sensor(binary_sensor::BinarySensor *sensor) { cooling_down_sensor_ = sensor; }
  
  // Control methods
  void turn_on();
  void turn_off();
  void set_power_level_percent(float percent);
  
  // Climate-specific control methods
  void set_climate_target_temperature(float temperature);
  void set_climate_preset(const std::string &preset);
  bool is_climate_control_active() const { return climate_control_active_; }
  void set_climate_control_active(bool active) { climate_control_active_ = active; }
  
  // Control mode management
  bool is_automatic_mode() const { return control_mode_ == ControlMode::AUTOMATIC; }
  bool is_manual_mode() const { return control_mode_ == ControlMode::MANUAL; }
  float get_external_temperature() const { return external_temperature_; }
  bool has_external_sensor() const { 
    return external_temperature_sensor_ != nullptr && 
           !std::isnan(external_temperature_); 
  }
  
  // Status getters
  HeaterState get_heater_state() const { return current_state_; }
  float get_current_temperature() const { return current_temperature_; }
  float get_target_temperature() const { return target_temperature_; }
  uint8_t get_power_level() const { return power_level_; }
  float get_power_level_percent() const { return power_level_ * 10.0f; }
  bool is_heating() const { 
    return current_state_ == HeaterState::GLOW_PLUG_PREHEAT || 
           current_state_ == HeaterState::HEATING_UP || 
           current_state_ == HeaterState::STABLE_COMBUSTION; 
  }
  bool is_connected() const { return last_received_time_ + COMMUNICATION_TIMEOUT_MS > millis(); }
  bool is_enabled() const { return heater_enabled_; }
  
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
  
  // Climate helper methods
  float preset_to_power_percent(const std::string &preset);
  std::string power_percent_to_preset(float percent);
  
  // Communication state
  std::vector<uint8_t> rx_buffer_;
  uint32_t last_received_time_{0};
  uint32_t last_send_time_{0};
  bool frame_sync_{false};
  
  // Control state
  bool heater_enabled_{false};
  uint8_t power_level_{8};  // 1-10 scale, default 80%
  float target_temperature_{20.0};
  HeaterState current_state_{HeaterState::OFF};
  ControlMode control_mode_{ControlMode::MANUAL};
  float default_power_percent_{80.0};
  bool climate_control_active_{false};
  
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
  
  // Sensor pointers
  sensor::Sensor *temperature_sensor_{nullptr};
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
};

// Enhanced Climate integration class
class VevorClimate : public climate::Climate, public Component {
 public:
  void set_vevor_heater(VevorHeater *heater) { heater_ = heater; }
  void set_min_temperature(float temperature) { min_temperature_ = temperature; }
  void set_max_temperature(float temperature) { max_temperature_ = temperature; }
  void set_temperature_step(float step) { temperature_step_ = step; }
  void set_power_control_enabled(bool enabled) { power_control_enabled_ = enabled; }
  void set_supports_presets(bool supports) { supports_presets_ = supports; }
  void set_default_preset(const std::string &preset) { default_preset_ = preset; }
  
  void setup() override;
  void control(const climate::ClimateCall &call) override;
  climate::ClimateTraits traits() override;
  void update();
  
  // Power control methods
  void set_power_percent(float percent);
  float get_power_percent() const;
  
 protected:
  VevorHeater *heater_{nullptr};
  float min_temperature_{5.0};
  float max_temperature_{35.0};
  float temperature_step_{1.0};
  bool power_control_enabled_{true};
  bool supports_presets_{true};
  std::string default_preset_{"Normal"};
  
  // Preset definitions
  std::map<std::string, float> preset_power_map_ = {
    {"Eco", 30.0f},
    {"Comfort", 60.0f},
    {"Normal", 80.0f},
    {"Boost", 100.0f}
  };
};

// Automation triggers and actions
template<typename... Ts> class HeaterTurnOnAction : public Action<Ts...> {
 public:
  explicit HeaterTurnOnAction(VevorHeater *heater) : heater_(heater) {}
  void play(Ts... x) override { heater_->turn_on(); }
  
 protected:
  VevorHeater *heater_;
};

template<typename... Ts> class HeaterTurnOffAction : public Action<Ts...> {
 public:
  explicit HeaterTurnOffAction(VevorHeater *heater) : heater_(heater) {}
  void play(Ts... x) override { heater_->turn_off(); }
  
 protected:
  VevorHeater *heater_;
};

template<typename... Ts> class HeaterSetPowerAction : public Action<Ts...> {
 public:
  explicit HeaterSetPowerAction(VevorHeater *heater) : heater_(heater) {}
  TEMPLATABLE_VALUE(float, power_level)
  void play(Ts... x) override { heater_->set_power_level_percent(this->power_level_.value(x...)); }
  
 protected:
  VevorHeater *heater_;
};

template<typename... Ts> class HeaterSetTemperatureAction : public Action<Ts...> {
 public:
  explicit HeaterSetTemperatureAction(VevorHeater *heater) : heater_(heater) {}
  TEMPLATABLE_VALUE(float, temperature)
  void play(Ts... x) override { heater_->set_climate_target_temperature(this->temperature_.value(x...)); }
  
 protected:
  VevorHeater *heater_;
};

}  // namespace vevor_heater
}  // namespace esphome