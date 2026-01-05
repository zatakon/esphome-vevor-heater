#include "vevor_heater.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "esphome/core/time.h"
#ifdef USE_TIME
#include "esphome/components/time/real_time_clock.h"
#endif
#include <cinttypes>
#include <ctime>

namespace esphome {
namespace vevor_heater {

// Helper function for checksum calculation
uint8_t calculate_checksum(const std::vector<uint8_t> &frame) {
  if (frame.size() < 4) {
    return 0;
  }
  uint32_t sum = 0;
  // Sum all bytes from index 2 to second-to-last byte
  for (size_t i = 2; i < frame.size() - 1; ++i) {
    sum += frame[i];
  }
  return static_cast<uint8_t>(sum % 256);
}

void VevorHeater::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Vevor Heater...");
  
  if (!this->parent_) {
    ESP_LOGE(TAG, "UART parent not set!");
    this->mark_failed();
    return;
  }
  
  // Initialize state
  this->current_state_ = HeaterState::OFF;
  this->heater_enabled_ = false;
  this->power_level_ = static_cast<uint8_t>(default_power_percent_ / 10.0f);  // Convert % to 1-10 scale
  this->last_send_time_ = millis();
  this->last_received_time_ = millis();
  this->external_temperature_ = NAN;
  
  // Initialize fuel consumption tracking
  this->last_consumption_update_ = millis();
  this->current_day_ = get_days_since_epoch();
  
  // Setup persistent storage for fuel consumption
  this->pref_fuel_consumption_ = global_preferences->make_preference<FuelConsumptionData>(fnv1_hash("fuel_consumption"));
  load_fuel_consumption_data();
  
  // Initialize hourly consumption sensor with initial value
  if (hourly_consumption_sensor_) {
    hourly_consumption_sensor_->publish_state(0.0f);
  }
  
  ESP_LOGCONFIG(TAG, "Vevor Heater setup completed");
  ESP_LOGCONFIG(TAG, "Control mode: %s", control_mode_ == ControlMode::AUTOMATIC ? "Automatic" : "Manual");
  ESP_LOGCONFIG(TAG, "Default power level: %.0f%%", default_power_percent_);
  ESP_LOGCONFIG(TAG, "Injected per pulse: %.2f ml", injected_per_pulse_);
  ESP_LOGCONFIG(TAG, "Daily consumption: %.2f ml", daily_consumption_ml_);
}

void VevorHeater::update() {
  // Update external temperature reading if sensor is available
  if (external_temperature_sensor_ != nullptr && external_temperature_sensor_->has_state()) {
    external_temperature_ = external_temperature_sensor_->state;
  }
  
  // Check for daily reset
  check_daily_reset();
  
  // Always check for incoming data, regardless of state
  check_uart_data();
  
  // Determine if we should send frames
  // Send frames at different intervals based on heater state:
  // - When heating or in non-OFF state: send every SEND_INTERVAL_MS (1 second)
  // - When OFF and not enabled: send polling requests every polling_interval_ms_ (default 1 minute)
  bool is_heating_or_active = heater_enabled_ || (current_state_ != HeaterState::OFF);
  
  uint32_t now = millis();
  uint32_t send_interval = is_heating_or_active ? SEND_INTERVAL_MS : polling_interval_ms_;
  
  if (is_heating_or_active) {
    // Handle communication timeout when actively controlling
    if (!is_connected()) {
      handle_communication_timeout();
    }
  }
  
  // Send controller frame at appropriate intervals
  if (now - last_send_time_ >= send_interval) {
    send_controller_frame();
    last_send_time_ = now;
  }
  
  // Update instantaneous hourly consumption rate (ml/h) based on current pump frequency
  if (hourly_consumption_sensor_) {
    // Calculate instantaneous consumption rate: Hz * ml/pulse * 3600 seconds/hour
    float instantaneous_consumption_ml_per_hour = pump_frequency_ * injected_per_pulse_ * 3600.0f;
    hourly_consumption_sensor_->publish_state(instantaneous_consumption_ml_per_hour);
  }
}

void VevorHeater::check_uart_data() {
  while (this->available()) {
    uint8_t byte;
    this->read_byte(&byte);
    
    // Look for frame start
    if (!frame_sync_ && byte == FRAME_START) {
      rx_buffer_.clear();
      rx_buffer_.push_back(byte);
      frame_sync_ = true;
      ESP_LOGVV(TAG, "Frame start detected");
      continue;
    }
    
    if (frame_sync_) {
      rx_buffer_.push_back(byte);
      this->last_received_time_ = millis();
      
      // Check if we have enough bytes to determine frame length
      if (rx_buffer_.size() >= 4) {
        uint8_t expected_length = (rx_buffer_[3] == HEATER_FRAME_LENGTH) ? 56 : 15;
        
        if (rx_buffer_.size() >= expected_length) {
          // Frame complete, process it
          // First check if this is a controller frame echo (should be silently ignored)
          if (rx_buffer_[1] == CONTROLLER_ID) {
            ESP_LOGVV(TAG, "Ignoring controller frame echo");
            rx_buffer_.clear();
            frame_sync_ = false;
            continue;
          }
          
          if (validate_frame(rx_buffer_, expected_length)) {
            process_heater_frame(rx_buffer_);
          } else {
            ESP_LOGW(TAG, "Invalid frame received");
          }
          
          rx_buffer_.clear();
          frame_sync_ = false;
        } else if (rx_buffer_.size() > expected_length + 10) {
          // Frame too long, reset
          ESP_LOGW(TAG, "Frame too long, resetting");
          rx_buffer_.clear();
          frame_sync_ = false;
        }
      }
    }
  }
  
  // Timeout check for incomplete frames
  if (frame_sync_ && (millis() - last_received_time_) > 100) {
    ESP_LOGV(TAG, "Frame timeout, resetting");
    rx_buffer_.clear();
    frame_sync_ = false;
  }
}

bool VevorHeater::validate_frame(const std::vector<uint8_t> &frame, uint8_t expected_length) {
  if (frame.size() != expected_length) {
    ESP_LOGV(TAG, "Frame length mismatch: expected %d, got %d", expected_length, frame.size());
    return false;
  }
  
  if (frame[0] != FRAME_START) {
    ESP_LOGV(TAG, "Invalid frame start: 0x%02X", frame[0]);
    return false;
  }
  
  // Don't validate device ID - accept any device ID (like the working version does)
  // Controller frame echoes are filtered out before calling this function
  
  // Verify checksum
  uint8_t calculated_checksum = calculate_checksum(frame);
  uint8_t received_checksum = frame[frame.size() - 1];
  
  if (calculated_checksum != received_checksum) {
    ESP_LOGD(TAG, "Checksum mismatch: calculated 0x%02X, received 0x%02X", 
             calculated_checksum, received_checksum);
    // Don't reject on checksum mismatch - just log it
    // The working version doesn't validate checksums either
  }
  
  return true;
}

void VevorHeater::send_controller_frame() {
  std::vector<uint8_t> frame;
  
  // Build controller frame
  frame.push_back(FRAME_START);           // 0: Start byte
  frame.push_back(CONTROLLER_ID);         // 1: Controller ID
  
  // Determine command based on current state and desired state
  if (!heater_enabled_) {
    if (current_state_ != HeaterState::OFF && current_state_ != HeaterState::STOPPING_COOLING) {
      frame.push_back(0x06);              // 2: Stop command
    } else {
      frame.push_back(0x02);              // 2: Status request
    }
  } else {
    if (current_state_ == HeaterState::OFF) {
      frame.push_back(0x06);              // 2: Start command
    } else {
      frame.push_back(0x02);              // 2: Status request
    }
  }
  
  frame.push_back(CONTROLLER_FRAME_LENGTH); // 3: Frame length
  frame.push_back(0x00);                    // 4: Unknown
  frame.push_back(0x00);                    // 5: Unknown
  frame.push_back(0x00);                    // 6: Unknown
  frame.push_back(0x00);                    // 7: Unknown
  frame.push_back(power_level_);            // 8: Power level (1-10)
  
  // Set requested state
  if (!heater_enabled_) {
    if (current_state_ != HeaterState::OFF && current_state_ != HeaterState::STOPPING_COOLING) {
      frame.push_back(0x05);              // 9: Set off
    } else {
      frame.push_back(0x02);              // 9: Off
    }
  } else {
    if (current_state_ == HeaterState::OFF) {
      frame.push_back(0x06);              // 9: Start
    } else {
      frame.push_back(0x08);              // 9: Running
    }
  }
  
  frame.push_back(0x00);                    // 10: Unknown
  frame.push_back(0x00);                    // 11: Unknown
  frame.push_back(0x00);                    // 12: Unknown
  frame.push_back(0x00);                    // 13: Unknown
  frame.push_back(0x00);                    // 14: Unknown
  
  // Calculate and add checksum
  uint8_t checksum = calculate_checksum(frame);
  frame.push_back(checksum);                // 15: Checksum
  
  // Send frame
  this->write_array(frame.data(), frame.size());
  
  ESP_LOGD(TAG, "Sent controller frame: enabled=%s, power=%d, state=0x%02X", 
           YESNO(heater_enabled_), power_level_, frame[9]);
}

void VevorHeater::process_heater_frame(const std::vector<uint8_t> &frame) {
  if (frame[3] == HEATER_FRAME_LENGTH && frame.size() >= 56) {
    // Long frame from heater
    ESP_LOGV(TAG, "Processing heater status frame");
    
    // Parse heater state (byte 5)
    uint8_t state_raw = frame[5];
    HeaterState new_state = static_cast<HeaterState>(state_raw);
    
    if (new_state != current_state_) {
      current_state_ = new_state;
      ESP_LOGD(TAG, "Heater state changed to: %s", state_to_string(current_state_));
    }
    
    // Update all sensors
    update_sensors(frame);
    
  } else if (frame[3] == CONTROLLER_FRAME_LENGTH && frame.size() >= 15) {
    // Short frame (controller echo)
    ESP_LOGVV(TAG, "Received controller frame echo");
    // Usually just an echo of our own transmission
  }
}

void VevorHeater::update_sensors(const std::vector<uint8_t> &frame) {
  // State sensor
  if (state_sensor_) {
    state_sensor_->publish_state(state_to_string(current_state_));
  }
  
  // Power level (byte 6)
  uint8_t power_level_raw = frame[6];
  if (power_level_sensor_ && power_level_raw > 0 && power_level_raw <= 10) {
    power_level_sensor_->publish_state(power_level_raw * 10);
  }
  
  // Input voltage (byte 11)
  uint8_t voltage_raw = frame[11];
  if (input_voltage_sensor_ && voltage_raw > 0) {
    input_voltage_ = voltage_raw / 10.0f;
    input_voltage_sensor_->publish_state(input_voltage_);
  }
  
  // Glow plug current (byte 13)
  uint8_t glow_current_raw = frame[13];
  if (glow_plug_current_sensor_) {
    glow_plug_current_ = glow_current_raw;
    glow_plug_current_sensor_->publish_state(glow_plug_current_);
  }
  
  // Cooling down flag (byte 14)
  uint8_t cooling_flag = frame[14];
  if (cooling_down_sensor_) {
    cooling_down_ = (cooling_flag != 0);
    cooling_down_sensor_->publish_state(cooling_down_);
  }
  
  // Heat exchanger temperature (bytes 16-17) - Signed value for negative temps
  if (heat_exchanger_temperature_sensor_ && frame.size() > 17) {
    // Read as signed int16 to handle negative temperatures correctly
    int16_t temp_raw = static_cast<int16_t>(read_uint16_be(frame, 16));
    heat_exchanger_temperature_ = temp_raw / 10.0f;
    heat_exchanger_temperature_sensor_->publish_state(heat_exchanger_temperature_);
    
    // Update current temperature for climate control (no duplicate temperature sensor)
    current_temperature_ = heat_exchanger_temperature_;
  }
  
  // State duration (bytes 20-21)
  if (state_duration_sensor_ && frame.size() > 21) {
    uint16_t duration_raw = read_uint16_be(frame, 20);
    state_duration_sensor_->publish_state(duration_raw);
  }
  
  // Pump frequency (byte 23)
  if (pump_frequency_sensor_ && frame.size() > 23) {
    uint8_t pump_raw = frame[23];
    float new_pump_frequency = pump_raw / 10.0f;
    
    // Update fuel consumption based on pump frequency change
    update_fuel_consumption(new_pump_frequency);
    
    pump_frequency_ = new_pump_frequency;
    pump_frequency_sensor_->publish_state(pump_frequency_);
  }
  
  // Fan speed (bytes 28-29)
  if (fan_speed_sensor_ && frame.size() > 29) {
    fan_speed_ = read_uint16_be(frame, 28);
    fan_speed_sensor_->publish_state(fan_speed_);
  }
}

void VevorHeater::update_fuel_consumption(float pump_frequency) {
  uint32_t current_time = millis();
  uint32_t time_delta = current_time - last_consumption_update_;
  
  // Only update if the heater is in a state where fuel is being consumed
  if (current_state_ == HeaterState::STABLE_COMBUSTION || 
      current_state_ == HeaterState::HEATING_UP) {
    
    // If pump frequency > 0, fuel is being injected
    if (pump_frequency > 0.0f && time_delta > 0) {
      // Calculate fuel consumption based on pump frequency and time
      // pump_frequency is in Hz (pulses per second)
      float time_seconds = time_delta / 1000.0f;
      float pulses = pump_frequency * time_seconds;
      float consumed_ml = pulses * injected_per_pulse_;
      
      // Update daily consumption counter
      daily_consumption_ml_ += consumed_ml;
      total_fuel_pulses_ += pulses;  // Keep as float for precision
      
      // Calculate instantaneous consumption rate for logging
      float instantaneous_ml_per_hour = pump_frequency * injected_per_pulse_ * 3600.0f;
      
      ESP_LOGVV(TAG, "Fuel consumption rate: %.2f ml/h, total daily: %.2f ml", 
                instantaneous_ml_per_hour, daily_consumption_ml_);
      
      // Update daily consumption sensor
      if (daily_consumption_sensor_) {
        daily_consumption_sensor_->publish_state(daily_consumption_ml_);
      }
      
      // Save data periodically (every 30 seconds to reduce flash wear)
      static uint32_t last_save = 0;
      if (current_time - last_save > 30000) {
        save_fuel_consumption_data();
        last_save = current_time;
      }
    }
  }
  
  last_pump_frequency_ = pump_frequency;
  last_consumption_update_ = current_time;
}

void VevorHeater::check_daily_reset() {
  uint32_t today = get_days_since_epoch();
  if (today != current_day_) {
    ESP_LOGI(TAG, "New day detected, resetting daily consumption counter");
    current_day_ = today;
    daily_consumption_ml_ = 0.0f;
    save_fuel_consumption_data();
    
    if (daily_consumption_sensor_) {
      daily_consumption_sensor_->publish_state(daily_consumption_ml_);
    }
  }
}

uint32_t VevorHeater::get_days_since_epoch() {
#ifdef USE_TIME
  // Try to use ESPHome time component if available
  if (time_component_ != nullptr) {
    auto now = time_component_->now();
    if (now.is_valid()) {
      // Calculate days since epoch using ESPHome time
      // This will properly handle midnight in the configured timezone
      return now.timestamp / (24 * 60 * 60);
    }
  }
#endif
  
  // Fallback to system time
  std::time_t now = std::time(nullptr);
  if (now < 1609459200) {  // If time is before 2021-01-01, it's not synced yet
    // If time is not synced yet, fall back to millis-based calculation
    // This ensures we don't reset randomly before time sync
    ESP_LOGW(TAG, "Time not synced yet, using millis() for day calculation");
    return millis() / (24 * 60 * 60 * 1000UL);
  }
  
  // Calculate days since Unix epoch (1970-01-01)
  // This will reset at midnight UTC
  return now / (24 * 60 * 60);
}

void VevorHeater::save_fuel_consumption_data() {
  FuelConsumptionData data;
  data.daily_consumption_ml = daily_consumption_ml_;
  data.last_reset_day = current_day_;
  data.total_pulses = total_fuel_pulses_;
  
  if (pref_fuel_consumption_.save(&data)) {
    ESP_LOGD(TAG, "Fuel consumption data saved: %.2f ml, day %d", 
             data.daily_consumption_ml, data.last_reset_day);
  } else {
    ESP_LOGW(TAG, "Failed to save fuel consumption data");
  }
}

void VevorHeater::load_fuel_consumption_data() {
  FuelConsumptionData data;
  if (pref_fuel_consumption_.load(&data)) {
    // Check if it's the same day
    uint32_t today = get_days_since_epoch();
    if (data.last_reset_day == today) {
      daily_consumption_ml_ = data.daily_consumption_ml;
      ESP_LOGI(TAG, "Loaded fuel consumption data: %.2f ml for today", daily_consumption_ml_);
    } else {
      daily_consumption_ml_ = 0.0f;
      ESP_LOGI(TAG, "New day detected, starting with 0 ml consumption");
    }
    total_fuel_pulses_ = data.total_pulses;
  } else {
    ESP_LOGI(TAG, "No fuel consumption data found, starting fresh");
    daily_consumption_ml_ = 0.0f;
    total_fuel_pulses_ = 0;
  }
  
  // Publish initial value to sensor so it's not "unknown"
  if (daily_consumption_sensor_) {
    daily_consumption_sensor_->publish_state(daily_consumption_ml_);
  }
}

void VevorHeater::reset_daily_consumption() {
  ESP_LOGI(TAG, "Manual reset of daily consumption counter");
  daily_consumption_ml_ = 0.0f;
  save_fuel_consumption_data();
  
  if (daily_consumption_sensor_) {
    daily_consumption_sensor_->publish_state(daily_consumption_ml_);
  }
}

void VevorHeater::handle_communication_timeout() {
  static uint32_t last_timeout_log = 0;
  uint32_t now = millis();
  
  if (now - last_timeout_log > 10000) {  // Log every 10 seconds
    ESP_LOGW(TAG, "Communication timeout - heater not responding");
    last_timeout_log = now;
  }
  
  // Reset sensors to unknown state
  if (state_sensor_) {
    state_sensor_->publish_state("Disconnected");
  }
}

const char* VevorHeater::state_to_string(HeaterState state) {
  switch (state) {
    case HeaterState::OFF: return "Off";
    case HeaterState::GLOW_PLUG_PREHEAT: return "Glow Plug Preheat";
    case HeaterState::HEATING_UP: return "Heating Up";
    case HeaterState::STABLE_COMBUSTION: return "Stable Combustion";
    case HeaterState::STOPPING_COOLING: return "Stopping/Cooling";
    default: return "Unknown";
  }
}

uint16_t VevorHeater::read_uint16_be(const std::vector<uint8_t> &data, size_t offset) {
  if (offset + 1 >= data.size()) {
    return 0;
  }
  return (static_cast<uint16_t>(data[offset]) << 8) | data[offset + 1];
}

float VevorHeater::parse_temperature(const std::vector<uint8_t> &data, size_t offset) {
  uint16_t raw = read_uint16_be(data, offset);
  return raw / 100.0f;
}

float VevorHeater::parse_voltage(const std::vector<uint8_t> &data, size_t offset) {
  if (offset >= data.size()) {
    return 0.0f;
  }
  return data[offset] / 10.0f;
}

// Public control methods
void VevorHeater::turn_on() {
  // Check if automatic mode requires external sensor
  if (control_mode_ == ControlMode::AUTOMATIC) {
    if (!has_external_sensor()) {
      ESP_LOGE(TAG, "Cannot turn on heater: automatic mode requires external temperature sensor!");
      return;
    }
  }
  
  heater_enabled_ = true;
  // Set to default power level on turn on
  power_level_ = static_cast<uint8_t>(default_power_percent_ / 10.0f);
  ESP_LOGI(TAG, "Heater turned ON at %.0f%% power", default_power_percent_);
}

void VevorHeater::turn_off() {
  heater_enabled_ = false;
  ESP_LOGI(TAG, "Heater turned OFF");
}

void VevorHeater::set_power_level_percent(float percent) {
  uint8_t level = static_cast<uint8_t>(std::max(1.0f, std::min(10.0f, percent / 10.0f)));
  if (level != power_level_) {
    power_level_ = level;
    ESP_LOGI(TAG, "Heater power level set to %d (%.0f%%)", level, percent);
  }
}

void VevorHeater::dump_config() {
  ESP_LOGCONFIG(TAG, "Vevor Heater:");
  ESP_LOGCONFIG(TAG, "  Control Mode: %s", control_mode_ == ControlMode::AUTOMATIC ? "Automatic" : "Manual");
  ESP_LOGCONFIG(TAG, "  Default Power Level: %.0f%%", default_power_percent_);
  ESP_LOGCONFIG(TAG, "  Power Level: %d/10", power_level_);
  ESP_LOGCONFIG(TAG, "  Target Temperature: %.1f°C", target_temperature_);
  ESP_LOGCONFIG(TAG, "  Injected per Pulse: %.2f ml", injected_per_pulse_);
  ESP_LOGCONFIG(TAG, "  Daily Consumption: %.2f ml", daily_consumption_ml_);
  ESP_LOGCONFIG(TAG, "  Total Fuel Pulses: %.1f", total_fuel_pulses_);
  
  if (external_temperature_sensor_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  External Temperature Sensor: Configured");
    if (has_external_sensor()) {
      ESP_LOGCONFIG(TAG, "    Current Reading: %.1f°C", external_temperature_);
    } else {
      ESP_LOGCONFIG(TAG, "    Current Reading: No data");
    }
  } else {
    ESP_LOGCONFIG(TAG, "  External Temperature Sensor: Not configured");
    if (control_mode_ == ControlMode::AUTOMATIC) {
      ESP_LOGW(TAG, "  WARNING: Automatic mode requires external temperature sensor!");
    }
  }
  
  // Removed LOG_SENSOR for the duplicate temperature_sensor_
  LOG_SENSOR("  ", "Input Voltage", input_voltage_sensor_);
  LOG_TEXT_SENSOR("  ", "State", state_sensor_);
  LOG_SENSOR("  ", "Power Level", power_level_sensor_);
  LOG_SENSOR("  ", "Fan Speed", fan_speed_sensor_);
  LOG_SENSOR("  ", "Pump Frequency", pump_frequency_sensor_);
  LOG_SENSOR("  ", "Glow Plug Current", glow_plug_current_sensor_);
  LOG_SENSOR("  ", "Heat Exchanger Temperature", heat_exchanger_temperature_sensor_);
  LOG_SENSOR("  ", "State Duration", state_duration_sensor_);
  LOG_BINARY_SENSOR("  ", "Cooling Down", cooling_down_sensor_);
  LOG_SENSOR("  ", "Hourly Consumption", hourly_consumption_sensor_);
  LOG_SENSOR("  ", "Daily Consumption", daily_consumption_sensor_);
}

// VevorClimate implementation
void VevorClimate::setup() {
  if (!heater_) {
    ESP_LOGE(TAG, "Heater not set for climate component");
    this->mark_failed();
    return;
  }
}

climate::ClimateTraits VevorClimate::traits() {
  auto traits = climate::ClimateTraits();
  traits.set_supports_current_temperature(true);
  traits.set_supports_two_point_target_temperature(false);
  traits.set_visual_min_temperature(min_temperature);
  traits.set_visual_max_temperature(max_temperature);
  traits.set_visual_temperature_step(1.0f);
  traits.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_HEAT});
  return traits;
}

void VevorClimate::control(const climate::ClimateCall &call) {
  if (call.get_mode().has_value()) {
    climate::ClimateMode mode = *call.get_mode();
    
    switch (mode) {
      case climate::CLIMATE_MODE_OFF:
        heater_->turn_off();
        break;
      case climate::CLIMATE_MODE_HEAT:
        heater_->turn_on();
        break;
      default:
        break;
    }
    
    this->mode = mode;
  }
  
  if (call.get_target_temperature().has_value()) {
    this->target_temperature = *call.get_target_temperature();
    heater_->set_target_temperature(this->target_temperature);
  }
  
  this->publish_state();
}

void VevorClimate::update() {
  if (heater_) {
    // Use external temperature if available in automatic mode, otherwise internal
    if (heater_->is_automatic_mode() && heater_->has_external_sensor()) {
      this->current_temperature = heater_->get_external_temperature();
    } else {
      this->current_temperature = heater_->get_current_temperature();
    }
    
    // Update mode based on heater state
    if (heater_->is_heating()) {
      this->mode = climate::CLIMATE_MODE_HEAT;
    } else {
      this->mode = climate::CLIMATE_MODE_OFF;
    }
    
    this->publish_state();
  }
}

}  // namespace vevor_heater
}  // namespace esphome