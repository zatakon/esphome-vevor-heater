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
  this->antifreeze_active_ = false;
  this->auto_mode_type_ = AutoModeType::OFF;
  this->last_power_adjustment_ = millis();
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
  
  // Send initial status request immediately after boot to get current heater state
  send_controller_frame();
  last_send_time_ = millis();
  ESP_LOGD(TAG, "Initial status request sent");
}

void VevorHeater::update() {
  // Update external temperature reading if sensor is available
  if (external_temperature_sensor_ != nullptr && external_temperature_sensor_->has_state()) {
    external_temperature_ = external_temperature_sensor_->state;
  }
  
  // Check for daily reset
  check_daily_reset();
  
  // Check voltage safety
  check_voltage_safety();
  
  // Handle antifreeze mode logic
  if (control_mode_ == ControlMode::ANTIFREEZE) {
    handle_antifreeze_mode();
  }
  
  // Handle automatic mode logic
  if (control_mode_ == ControlMode::AUTOMATIC) {
    handle_automatic_mode();
  }
  
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
      
      // Update total consumption
      total_consumption_ml_ = total_fuel_pulses_ * injected_per_pulse_;
      
      // Calculate instantaneous consumption rate for logging
      float instantaneous_ml_per_hour = pump_frequency * injected_per_pulse_ * 3600.0f;
      
      ESP_LOGVV(TAG, "Fuel consumption rate: %.2f ml/h, total daily: %.2f ml", 
                instantaneous_ml_per_hour, daily_consumption_ml_);
      
      // Update daily consumption sensor
      if (daily_consumption_sensor_) {
        daily_consumption_sensor_->publish_state(daily_consumption_ml_);
      }
      
      // Update total consumption sensor
      if (total_consumption_sensor_) {
        total_consumption_sensor_->publish_state(total_consumption_ml_ / 1000.0f);  // Convert ml to L
      }
      
      // Update gas consumption sensor (in cubic meters)
      if (gas_consumption_sensor_) {
        gas_consumption_sensor_->publish_state(total_consumption_ml_ / 1000000.0f);  // Convert ml to m³
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
      if (time_sync_warning_shown_) {
        ESP_LOGI(TAG, "Time synced successfully via time component");
        time_sync_warning_shown_ = false;  // Reset flag
      }
      return now.timestamp / (24 * 60 * 60);
    } else {
      ESP_LOGVV(TAG, "Time component present but time not valid yet");
    }
  }
#endif
  
  // Fallback to system time - HomeAssistant API automatically syncs this
  std::time_t now = std::time(nullptr);
  ESP_LOGVV(TAG, "System time value: %ld", (long)now);
  
  // Check if system time is synced (via Home Assistant API or SNTP)
  // System time will be synced once Home Assistant connects
  if (now < 1609459200) {  // If time is before 2021-01-01, it's not synced yet
    // If time is not synced yet, fall back to millis-based calculation
    // Only show info message once
    if (!time_sync_warning_shown_) {
      ESP_LOGI(TAG, "Waiting for time sync (via Home Assistant or time component). Using millis() for now.");
      time_sync_warning_shown_ = true;
    }
    return millis() / (24 * 60 * 60 * 1000UL);
  }
  
  // Time is now synced
  if (time_sync_warning_shown_) {
    ESP_LOGI(TAG, "System time synced successfully via Home Assistant");
    time_sync_warning_shown_ = false;  // Reset flag
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
  
  // Publish total consumption
  total_consumption_ml_ = total_fuel_pulses_ * injected_per_pulse_;
  if (total_consumption_sensor_) {
    total_consumption_sensor_->publish_state(total_consumption_ml_ / 1000.0f);  // Convert ml to L
  }
  
  // Publish gas consumption
  if (gas_consumption_sensor_) {
    gas_consumption_sensor_->publish_state(total_consumption_ml_ / 1000000.0f);  // Convert ml to m³
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

void VevorHeater::reset_total_consumption() {
  ESP_LOGI(TAG, "Manual reset of total consumption counter");
  total_fuel_pulses_ = 0.0f;
  total_consumption_ml_ = 0.0f;
  save_fuel_consumption_data();
  
  if (total_consumption_sensor_) {
    total_consumption_sensor_->publish_state(total_consumption_ml_);
  }
  
  if (gas_consumption_sensor_) {
    gas_consumption_sensor_->publish_state(0.0f);
  }
}

void VevorHeater::check_voltage_safety() {
  bool voltage_error = false;
  
  // Check voltage thresholds based on state
  if ((current_state_ == HeaterState::OFF || current_state_ == HeaterState::POLLING_STATE) && 
      heater_enabled_) {
    // During OFF/POLLING_STATE, check if voltage is sufficient to start
    if (input_voltage_ < min_voltage_start_) {
      ESP_LOGW(TAG, "Low voltage detected during start: %.1fV < %.1fV", 
               input_voltage_, min_voltage_start_);
      voltage_error = true;
      heater_enabled_ = false;  // Prevent starting
    }
  } else if (current_state_ == HeaterState::STABLE_COMBUSTION) {
    // During stable combustion, check if voltage is sufficient to keep running
    if (input_voltage_ < min_voltage_operate_) {
      ESP_LOGW(TAG, "Low voltage detected during operation: %.1fV < %.1fV - Stopping heater", 
               input_voltage_, min_voltage_operate_);
      voltage_error = true;
      heater_enabled_ = false;  // Force stop
    }
  }
  
  // Update error state
  if (voltage_error != low_voltage_error_) {
    low_voltage_error_ = voltage_error;
    if (low_voltage_error_sensor_) {
      low_voltage_error_sensor_->publish_state(low_voltage_error_);
    }
  } else if (!voltage_error && low_voltage_error_) {
    // Clear error if voltage has recovered
    low_voltage_error_ = false;
    if (low_voltage_error_sensor_) {
      low_voltage_error_sensor_->publish_state(false);
    }
  }
}

void VevorHeater::handle_antifreeze_mode() {
  // Antifreeze mode requires external temperature sensor
  if (!has_external_sensor()) {
    ESP_LOGW(TAG, "Antifreeze mode requires external temperature sensor");
    // If antifreeze was active but sensor is lost, turn off
    if (antifreeze_active_) {
      ESP_LOGW(TAG, "Sensor lost, turning off antifreeze heating");
      turn_off();
      antifreeze_active_ = false;
    }
    return;
  }
  
  float temp = external_temperature_;
  float current_power = heater_enabled_ ? (power_level_ * 10.0f) : 0.0f;
  
  // Simple antifreeze logic:
  // ON/OFF: No hysteresis
  //   - Turn ON when temp < antifreeze_temp_on
  //   - Turn OFF when temp >= antifreeze_temp_off
  // Power levels when running:
  //   - 1.8 - 4.0°C: 80%
  //   - 4.0 - 5.0°C: 50%
  //   - 5.0 - 5.8°C: 20%
  // Hysteresis for increasing power (when temp drops):
  //   - 20% -> 50% when temp < low - 0.4 (e.g., < 4.6°C)
  //   - 50% -> 80% when temp < medium - 0.4 (e.g., < 3.6°C)
  
  // Turn OFF if temp >= OFF threshold
  if (temp >= antifreeze_temp_off_) {
    if (heater_enabled_) {
      ESP_LOGI(TAG, "Antifreeze: Temperature %.1f°C >= %.1f°C, turning off", temp, antifreeze_temp_off_);
      turn_off();
      antifreeze_active_ = false;
      last_antifreeze_power_ = 0.0f;
    }
    return;
  }
  
  // Turn ON at 80% if temp < ON threshold
  if (temp < antifreeze_temp_on_) {
    if (!heater_enabled_) {
      ESP_LOGI(TAG, "Antifreeze: Temperature %.1f°C < %.1f°C, turning on at 80%%", temp, antifreeze_temp_on_);
      set_power_level_percent(80.0f);
      turn_on();
      antifreeze_active_ = true;
      last_antifreeze_power_ = 80.0f;
    } else if (current_power != 80.0f) {
      ESP_LOGI(TAG, "Antifreeze: Temperature %.1f°C, setting to 80%%", temp);
      set_power_level_percent(80.0f);
      last_antifreeze_power_ = 80.0f;
    }
    return;
  }
  
  // If heater is OFF and temp >= ON threshold, stay OFF (don't auto-start)
  if (!heater_enabled_) {
    return;
  }
  
  // Heater is ON - manage power levels based on temperature
  // Set power level with hysteresis for increasing power
  if (temp >= antifreeze_temp_low_) {
    // Temperature in 20% zone (5.0 - 5.8°C)
    if (current_power != 20.0f) {
      ESP_LOGI(TAG, "Antifreeze: Temperature %.1f°C, setting to 20%%", temp);
      set_power_level_percent(20.0f);
      last_antifreeze_power_ = 20.0f;
    }
  } else if (temp >= antifreeze_temp_medium_) {
    // Temperature in 50% zone (4.0 - 5.0°C)
    if (current_power == 20.0f) {
      // Coming from 20%, use hysteresis: increase to 50% when temp drops below low - 0.4
      if (temp < antifreeze_temp_low_ - 0.4f) {
        ESP_LOGI(TAG, "Antifreeze: Temperature %.1f°C, increasing from 20%% to 50%%", temp);
        set_power_level_percent(50.0f);
        last_antifreeze_power_ = 50.0f;
      }
    } else if (current_power != 50.0f) {
      // Coming from 80%, set to 50% immediately
      ESP_LOGI(TAG, "Antifreeze: Temperature %.1f°C, setting to 50%%", temp);
      set_power_level_percent(50.0f);
      last_antifreeze_power_ = 50.0f;
    }
  } else {
    // Temperature in 80% zone (1.8 - 4.0°C)
    if (current_power == 50.0f) {
      // Coming from 50%, use hysteresis: increase to 80% when temp drops below medium - 0.4
      if (temp < antifreeze_temp_medium_ - 0.4f) {
        ESP_LOGI(TAG, "Antifreeze: Temperature %.1f°C, increasing from 50%% to 80%%", temp);
        set_power_level_percent(80.0f);
        last_antifreeze_power_ = 80.0f;
      }
    } else if (current_power != 80.0f) {
      // Coming from 20%, set to 80% immediately
      ESP_LOGI(TAG, "Antifreeze: Temperature %.1f°C, setting to 80%%", temp);
      set_power_level_percent(80.0f);
      last_antifreeze_power_ = 80.0f;
    }
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
    case HeaterState::POLLING_STATE: return "Getting state";
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
void VevorHeater::set_control_mode(ControlMode mode) {
  ControlMode old_mode = control_mode_;
  control_mode_ = mode;
  
  // When leaving antifreeze mode, turn off heater if it was active in antifreeze
  if (old_mode == ControlMode::ANTIFREEZE && antifreeze_active_) {
    ESP_LOGI(TAG, "Leaving antifreeze mode, turning off heater");
    turn_off();
    antifreeze_active_ = false;
  }
  
  ESP_LOGI(TAG, "Control mode changed from %d to %d", (int)old_mode, (int)mode);
}

void VevorHeater::turn_on() {
  // Check if automatic mode requires external sensor
  if (control_mode_ == ControlMode::AUTOMATIC) {
    if (!has_external_sensor()) {
      ESP_LOGE(TAG, "Cannot turn on heater: automatic mode requires external temperature sensor!");
      return;
    }
  }
  
  // Check voltage before allowing start
  if (input_voltage_ < min_voltage_start_) {
    ESP_LOGW(TAG, "Cannot start heater: voltage too low (%.1fV < %.1fV)", 
             input_voltage_, min_voltage_start_);
    low_voltage_error_ = true;
    if (low_voltage_error_sensor_) {
      low_voltage_error_sensor_->publish_state(true);
    }
    return;
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

float VevorHeater::calculate_power_for_temp_diff(float temp_diff) {
  // Calculate power based on temperature difference
  float abs_diff = std::abs(temp_diff);
  
  if (abs_diff < 1.0f) {
    return 10.0f;
  } else if (abs_diff < 2.0f) {
    return 20.0f;
  } else if (abs_diff < 3.0f) {
    return 40.0f;
  } else if (abs_diff < 5.0f) {
    return 60.0f;
  } else if (abs_diff < 6.0f) {
    return 80.0f;
  } else {
    return 90.0f;
  }
}

void VevorHeater::handle_automatic_mode() {
  // Automatic mode requires external temperature sensor
  if (!has_external_sensor()) {
    ESP_LOGW(TAG, "Automatic mode requires external temperature sensor");
    // If heater was running in auto mode but sensor is lost, turn off
    if (heater_enabled_ && auto_mode_type_ == AutoModeType::FULL_AUTO) {
      ESP_LOGW(TAG, "Sensor lost, turning off heater");
      turn_off();
    }
    return;
  }
  
  // Check if auto mode is active
  if (auto_mode_type_ == AutoModeType::OFF) {
    // Auto mode disabled - heater controlled by manual controls
    return;
  }
  
  float current_temp = external_temperature_;
  float temp_diff = target_temperature_ - current_temp;
  
  // Full-Auto mode: control both power and on/off
  if (auto_mode_type_ == AutoModeType::FULL_AUTO) {
    // Turn heater on/off based on configurable thresholds
    if (temp_diff >= auto_mode_temp_below_) {
      // Temperature is below threshold - turn on heater if not already on
      if (!heater_enabled_) {
        ESP_LOGI(TAG, "Full-Auto: Turning heater ON (temp: %.1f°C, target: %.1f°C, diff: %.1f°C)", 
                 current_temp, target_temperature_, temp_diff);
        
        // Set initial power based on current temperature difference
        float initial_power = calculate_power_for_temp_diff(temp_diff);
        set_power_level_percent(initial_power);
        
        turn_on();
        last_power_adjustment_ = millis();
        
        ESP_LOGI(TAG, "Full-Auto: Initial power set to %.0f%% based on temp diff %.1f°C", 
                 initial_power, temp_diff);
      }
    } else if (temp_diff <= -auto_mode_temp_above_) {
      // Temperature is above threshold - turn off heater
      if (heater_enabled_) {
        ESP_LOGI(TAG, "Full-Auto: Turning heater OFF (temp: %.1f°C, target: %.1f°C, diff: %.1f°C)", 
                 current_temp, target_temperature_, temp_diff);
        turn_off();
      }
      return;
    }
  }
  
  // Semi-Auto mode: turn on heater automatically and adjust power
  // (heater stays on until mode is changed or manually turned off)
  if (auto_mode_type_ == AutoModeType::SEMI_AUTO) {
    // Turn heater on if not already on
    if (!heater_enabled_) {
      ESP_LOGI(TAG, "Semi-Auto: Turning heater ON (temp: %.1f°C, target: %.1f°C, diff: %.1f°C)", 
               current_temp, target_temperature_, temp_diff);
      
      // Set initial power based on current temperature difference
      float initial_power = calculate_power_for_temp_diff(temp_diff);
      set_power_level_percent(initial_power);
      
      turn_on();
      last_power_adjustment_ = millis();
      
      ESP_LOGI(TAG, "Semi-Auto: Initial power set to %.0f%% based on temp diff %.1f°C", 
               initial_power, temp_diff);
    }
  }
  
  // If heater is not on, nothing more to do
  if (!heater_enabled_) {
    return;
  }
  
  // Adjust power every 20 seconds (both Semi-Auto and Full-Auto)
  uint32_t now = millis();
  if (now - last_power_adjustment_ >= POWER_ADJUSTMENT_INTERVAL_MS) {
    last_power_adjustment_ = now;
    
    // Calculate desired power based on temperature difference
    float desired_power = calculate_power_for_temp_diff(temp_diff);
    float current_power = power_level_ * 10.0f;
    
    // Adjust power by max ±10% from current level
    float new_power = current_power;
    const char* mode_name = (auto_mode_type_ == AutoModeType::SEMI_AUTO) ? "Semi-Auto" : "Full-Auto";
    
    if (desired_power > current_power + 5.0f) {
      // Need more power - increase by 10%
      new_power = std::min(100.0f, current_power + 10.0f);
      ESP_LOGI(TAG, "%s: Increasing power from %.0f%% to %.0f%% (temp diff: %.1f°C, desired: %.0f%%)", 
               mode_name, current_power, new_power, temp_diff, desired_power);
    } else if (desired_power < current_power - 5.0f) {
      // Need less power - decrease by 10%
      new_power = std::max(10.0f, current_power - 10.0f);
      ESP_LOGI(TAG, "%s: Decreasing power from %.0f%% to %.0f%% (temp diff: %.1f°C, desired: %.0f%%)", 
               mode_name, current_power, new_power, temp_diff, desired_power);
    } else {
      ESP_LOGD(TAG, "%s: Power unchanged at %.0f%% (temp diff: %.1f°C, desired: %.0f%%)", 
               mode_name, current_power, temp_diff, desired_power);
    }
    
    // Apply new power level
    if (new_power != current_power) {
      set_power_level_percent(new_power);
    }
  }
}

void VevorHeater::dump_config() {
  ESP_LOGCONFIG(TAG, "Vevor Heater:");
  const char* mode_str = "Manual";
  if (control_mode_ == ControlMode::AUTOMATIC) {
    mode_str = "Automatic";
  } else if (control_mode_ == ControlMode::ANTIFREEZE) {
    mode_str = "Antifreeze";
  }
  ESP_LOGCONFIG(TAG, "  Control Mode: %s", mode_str);
  if (control_mode_ == ControlMode::AUTOMATIC) {
    const char* auto_type_str = "Off";
    if (auto_mode_type_ == AutoModeType::SEMI_AUTO) {
      auto_type_str = "Semi-Auto";
    } else if (auto_mode_type_ == AutoModeType::FULL_AUTO) {
      auto_type_str = "Full-Auto";
    }
    ESP_LOGCONFIG(TAG, "  Auto Mode Type: %s", auto_type_str);
  }
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
  LOG_SENSOR("  ", "Total Consumption", total_consumption_sensor_);
  LOG_BINARY_SENSOR("  ", "Low Voltage Error", low_voltage_error_sensor_);
}

}  // namespace vevor_heater
}  // namespace esphome