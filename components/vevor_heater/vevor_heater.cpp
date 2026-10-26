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
  this->last_power_adjustment_ = millis();
  this->power_level_ = static_cast<uint8_t>(default_power_percent_ / 10.0f);  // Convert % to 1-10 scale
  this->manual_power_level_ = this->power_level_;
  this->last_send_time_ = millis();
  this->last_received_time_ = millis();
  this->external_temperature_ = NAN;
  
  // Track every published value with its time, so a value that stopped
  // updating (e.g. Home Assistant is down) can be detected in update()
  if (this->external_temperature_sensor_ != nullptr) {
    this->external_temperature_sensor_->add_on_state_callback([this](float value) {
      this->external_temperature_ = value;
      this->last_external_temperature_update_ = millis();
      if (this->external_temperature_stale_) {
        ESP_LOGI(TAG, "External temperature sensor updates again");
        this->external_temperature_stale_ = false;
      }
    });
    if (this->external_temperature_sensor_->has_state()) {
      this->external_temperature_ = this->external_temperature_sensor_->state;
      this->last_external_temperature_update_ = millis();
    }
  }
  
  // Restore the mode and settings changed from Home Assistant. Before the fuel
  // data, the total consumption depends on injected_per_pulse_.
  this->yaml_mode_option_ = get_mode_option();
  this->yaml_injected_per_pulse_ = this->injected_per_pulse_;
  this->pref_settings_ = global_preferences->make_preference<PersistedSettings>(fnv1_hash("vevor_settings"));
  load_settings();
  
  // Initialize fuel consumption tracking
  // current_day_ is restored from flash in load_fuel_consumption_data(). The
  // time is usually not synced yet at this point, so the day is only compared
  // later in check_daily_reset().
  this->last_consumption_update_ = millis();
  
  // Setup persistent storage for fuel consumption
  this->pref_fuel_consumption_ = global_preferences->make_preference<FuelConsumptionData>(fnv1_hash("fuel_consumption"));
  load_fuel_consumption_data();
  
  // Initialize hourly consumption sensor with initial value
  if (hourly_consumption_sensor_) {
    hourly_consumption_sensor_->publish_state(0.0f);
  }
  
  // A start lockout does not survive a reboot
  if (start_lockout_sensor_) {
    start_lockout_sensor_->publish_state(false);
  }
  
  ESP_LOGCONFIG(TAG, "Vevor Heater setup completed");
  ESP_LOGCONFIG(TAG, "Control mode: %s", mode_option_to_string(get_mode_option()));
  ESP_LOGCONFIG(TAG, "Default power level: %.0f%%", default_power_percent_);
  ESP_LOGCONFIG(TAG, "Injected per pulse: %.2f ml", injected_per_pulse_);
  ESP_LOGCONFIG(TAG, "Daily consumption: %.2f ml", daily_consumption_ml_);
  
  // Send initial status request immediately after boot to get current heater state
  send_controller_frame();
  last_send_time_ = millis();
  ESP_LOGD(TAG, "Initial status request sent");
}

void VevorHeater::update() {
  // The external temperature is updated by the state callback set in setup().
  // Treat it as unavailable when it stopped updating, otherwise automatic
  // modes would keep controlling the heater on a frozen value.
  if (external_temperature_timeout_ms_ > 0 && !external_temperature_stale_ &&
      !std::isnan(external_temperature_) &&
      millis() - last_external_temperature_update_ > external_temperature_timeout_ms_) {
    ESP_LOGW(TAG, "External temperature not updated for %" PRIu32 " s, treating it as unavailable",
             external_temperature_timeout_ms_ / 1000);
    external_temperature_stale_ = true;
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
        // Total frame size is the length field + 5 (56 bytes heater, 16 bytes controller)
        uint8_t expected_length = (rx_buffer_[3] == HEATER_FRAME_LENGTH) ? HEATER_FRAME_LENGTH + 5
                                                                         : CONTROLLER_FRAME_LENGTH + 5;
        
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
    // A corrupted frame could carry a wrong state, voltage or pump frequency
    // and trigger a stop, a start or bogus fuel counting, so drop it.
    // The checksum algorithm matches all frames in the captured bus log.
    ESP_LOGW(TAG, "Checksum mismatch: calculated 0x%02X, received 0x%02X - frame dropped",
             calculated_checksum, received_checksum);
    return false;
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
      stop_command_sent_ = true;
      last_stop_command_time_ = millis();
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
  
  // Calculate and add checksum (sum of bytes 2-14, so it is computed with the
  // checksum slot already appended)
  frame.push_back(0x00);                    // 15: Checksum
  frame.back() = calculate_checksum(frame);
  
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
      HeaterState old_state = current_state_;
      current_state_ = new_state;
      ESP_LOGD(TAG, "Heater state changed to: %s", state_to_string(current_state_));
      
      // The heater left heating up or combustion on its own (failed ignition,
      // flame-out, heater-side error) while we still request it to run. A stop
      // shortly after our own stop command is expected, e.g. a quick off/on.
      bool was_burning = old_state == HeaterState::HEATING_UP || old_state == HeaterState::STABLE_COMBUSTION;
      bool now_stopped = new_state == HeaterState::STOPPING_COOLING || new_state == HeaterState::OFF;
      bool stop_requested = stop_command_sent_ && (millis() - last_stop_command_time_ < STOP_RESPONSE_WINDOW_MS);
      if (heater_enabled_ && was_burning && now_stopped && !stop_requested) {
        trigger_start_lockout(old_state, new_state);
      }
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
  // Parsed even without a sensor, the voltage protection depends on it
  uint8_t voltage_raw = frame[11];
  if (voltage_raw > 0) {
    input_voltage_ = voltage_raw / 10.0f;
    voltage_valid_ = true;
    if (input_voltage_sensor_) {
      input_voltage_sensor_->publish_state(input_voltage_);
    }
  }
  
  // Glow plug current (byte 13)
  // Values are always parsed and only published when a sensor is configured,
  // so the control logic does not depend on which sensors exist
  uint8_t glow_current_raw = frame[13];
  glow_plug_current_ = glow_current_raw;
  if (glow_plug_current_sensor_) {
    glow_plug_current_sensor_->publish_state(glow_plug_current_);
  }
  
  // Cooling down flag (byte 14)
  uint8_t cooling_flag = frame[14];
  cooling_down_ = (cooling_flag != 0);
  if (cooling_down_sensor_) {
    cooling_down_sensor_->publish_state(cooling_down_);
  }
  
  // Heat exchanger temperature (bytes 16-17) - Signed value for negative temps
  if (frame.size() > 17) {
    // Read as signed int16 to handle negative temperatures correctly
    int16_t temp_raw = static_cast<int16_t>(read_uint16_be(frame, 16));
    heat_exchanger_temperature_ = temp_raw / 10.0f;
    if (heat_exchanger_temperature_sensor_) {
      heat_exchanger_temperature_sensor_->publish_state(heat_exchanger_temperature_);
    }
    
    // Update current temperature for climate control (no duplicate temperature sensor)
    current_temperature_ = heat_exchanger_temperature_;
  }
  
  // State duration (bytes 20-21)
  if (frame.size() > 21) {
    state_duration_ = read_uint16_be(frame, 20);
    if (state_duration_sensor_) {
      state_duration_sensor_->publish_state(state_duration_);
    }
  }
  
  // Pump frequency (byte 23)
  if (frame.size() > 23) {
    uint8_t pump_raw = frame[23];
    float new_pump_frequency = pump_raw / 10.0f;
    
    // Update fuel consumption based on pump frequency change
    update_fuel_consumption(new_pump_frequency);
    
    pump_frequency_ = new_pump_frequency;
    if (pump_frequency_sensor_) {
      pump_frequency_sensor_->publish_state(pump_frequency_);
    }
  }
  
  // Fan speed (bytes 28-29)
  if (frame.size() > 29) {
    fan_speed_ = read_uint16_be(frame, 28);
    if (fan_speed_sensor_) {
      fan_speed_sensor_->publish_state(fan_speed_);
    }
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
        daily_consumption_sensor_->publish_state(daily_consumption_ml_ / 1000.0f);  // Convert ml to L
      }
      
      // Update total consumption sensor
      if (total_consumption_sensor_) {
        total_consumption_sensor_->publish_state(total_consumption_ml_ / 1000.0f);  // Convert ml to L
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
  uint32_t today = get_current_day_id();
  if (today == 0) {
    // Time not known yet - keep adding to the value restored from flash
    return;
  }
  
  if (current_day_ == 0) {
    // First valid time on a device without a stored day - adopt it, no reset
    ESP_LOGI(TAG, "Daily consumption counter assigned to day id %" PRIu32, today);
    current_day_ = today;
    save_fuel_consumption_data();
    return;
  }
  
  if (today != current_day_) {
    ESP_LOGI(TAG, "New day detected, resetting daily consumption counter");
    current_day_ = today;
    daily_consumption_ml_ = 0.0f;
    save_fuel_consumption_data();
    
    if (daily_consumption_sensor_) {
      daily_consumption_sensor_->publish_state(daily_consumption_ml_ / 1000.0f);  // Convert ml to L
    }
  }
}

uint32_t VevorHeater::get_current_day_id() {
  // Day identifier: year * 400 + day_of_year (1-366) in local time, so it
  // changes at local midnight when a timezone is configured. Returns 0 while
  // the time is not known yet.
#ifdef USE_TIME
  // Try to use ESPHome time component if available
  if (time_component_ != nullptr) {
    auto now = time_component_->now();
    if (now.is_valid()) {
      if (time_sync_warning_shown_) {
        ESP_LOGI(TAG, "Time synced successfully via time component");
        time_sync_warning_shown_ = false;  // Reset flag
      }
      return now.year * 400 + now.day_of_year;
    } else {
      ESP_LOGVV(TAG, "Time component present but time not valid yet");
    }
  }
#endif
  
  // Fallback to system time, which is set by any ESPHome time component
  std::time_t now = std::time(nullptr);
  ESP_LOGVV(TAG, "System time value: %ld", (long)now);
  
  if (now < 1609459200) {  // If time is before 2021-01-01, it's not synced yet
    // Only show info message once
    if (!time_sync_warning_shown_) {
      ESP_LOGI(TAG, "Waiting for time sync, daily consumption reset is paused until then");
      time_sync_warning_shown_ = true;
    }
    return 0;
  }
  
  // Time is now synced
  if (time_sync_warning_shown_) {
    ESP_LOGI(TAG, "System time synced successfully");
    time_sync_warning_shown_ = false;  // Reset flag
  }
  // Same scheme as above; localtime follows the TZ set by a time component
  struct tm local_tm;
  localtime_r(&now, &local_tm);
  return (local_tm.tm_year + 1900) * 400 + local_tm.tm_yday + 1;
}

void VevorHeater::save_fuel_consumption_data() {
  FuelConsumptionData data;
  data.daily_consumption_ml = daily_consumption_ml_;
  data.last_reset_day = current_day_;
  data.total_pulses = total_fuel_pulses_;
  
  if (pref_fuel_consumption_.save(&data)) {
    ESP_LOGD(TAG, "Fuel consumption data saved: %.2f ml, day id %" PRIu32,
             data.daily_consumption_ml, data.last_reset_day);
  } else {
    ESP_LOGW(TAG, "Failed to save fuel consumption data");
  }
}

void VevorHeater::load_fuel_consumption_data() {
  FuelConsumptionData data;
  if (pref_fuel_consumption_.load(&data)) {
    // Restore the value together with the day it belongs to. Whether it is
    // still today is decided in check_daily_reset() once the time is synced.
    daily_consumption_ml_ = data.daily_consumption_ml;
    current_day_ = data.last_reset_day;
    ESP_LOGI(TAG, "Loaded fuel consumption data: %.2f ml for day id %" PRIu32,
             daily_consumption_ml_, current_day_);
    total_fuel_pulses_ = data.total_pulses;
  } else {
    ESP_LOGI(TAG, "No fuel consumption data found, starting fresh");
    daily_consumption_ml_ = 0.0f;
    current_day_ = 0;
    total_fuel_pulses_ = 0;
  }
  
  // Publish initial value to sensor so it's not "unknown"
  if (daily_consumption_sensor_) {
    daily_consumption_sensor_->publish_state(daily_consumption_ml_ / 1000.0f);  // Convert ml to L
  }
  
  // Publish total consumption
  total_consumption_ml_ = total_fuel_pulses_ * injected_per_pulse_;
  if (total_consumption_sensor_) {
    total_consumption_sensor_->publish_state(total_consumption_ml_ / 1000.0f);  // Convert ml to L
  }

}

void VevorHeater::reset_daily_consumption() {
  ESP_LOGI(TAG, "Manual reset of daily consumption counter");
  daily_consumption_ml_ = 0.0f;
  save_fuel_consumption_data();
  
  if (daily_consumption_sensor_) {
    daily_consumption_sensor_->publish_state(daily_consumption_ml_ / 1000.0f);  // Convert ml to L
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
}

void VevorHeater::check_voltage_safety() {
  if (!voltage_valid_) {
    return;  // No heater frame with the input voltage yet
  }
  
  // Check voltage thresholds based on state
  if ((current_state_ == HeaterState::OFF || current_state_ == HeaterState::POLLING_STATE) &&
      heater_enabled_) {
    // During OFF/POLLING_STATE, check if voltage is sufficient to start
    if (input_voltage_ < min_voltage_start_) {
      ESP_LOGW(TAG, "Low voltage detected during start: %.1fV < %.1fV",
               input_voltage_, min_voltage_start_);
      heater_enabled_ = false;  // Prevent starting
      set_low_voltage_lockout();
    }
  } else if (current_state_ == HeaterState::STABLE_COMBUSTION && heater_enabled_) {
    // During stable combustion, check if voltage is sufficient to keep running
    if (input_voltage_ < min_voltage_operate_) {
      ESP_LOGW(TAG, "Low voltage detected during operation: %.1fV < %.1fV - Stopping heater",
               input_voltage_, min_voltage_operate_);
      heater_enabled_ = false;  // Force stop
      set_low_voltage_lockout();
    }
  }
  
  // Without load the battery recovers quickly and a restart would pull it down
  // again, so release only after the delay and with some margin above the
  // start threshold
  if (low_voltage_lockout_ && millis() - low_voltage_lockout_since_ >= low_voltage_restart_delay_ms_ &&
      input_voltage_ >= min_voltage_start_ + LOW_VOLTAGE_RESTART_HYSTERESIS) {
    ESP_LOGI(TAG, "Voltage recovered to %.1fV, low voltage lockout released", input_voltage_);
    low_voltage_lockout_ = false;
  }
  
  update_low_voltage_error_sensor();
}

void VevorHeater::set_low_voltage_lockout() {
  if (low_voltage_lockout_) {
    return;
  }
  low_voltage_lockout_ = true;
  low_voltage_lockout_since_ = millis();
  ESP_LOGW(TAG, "Low voltage lockout: automatic starts blocked for at least %" PRIu32 " s and until %.1fV",
           low_voltage_restart_delay_ms_ / 1000, min_voltage_start_ + LOW_VOLTAGE_RESTART_HYSTERESIS);
}

void VevorHeater::update_low_voltage_error_sensor() {
  // The error stays on for the whole lockout, not just for one update cycle
  if (low_voltage_error_ != low_voltage_lockout_) {
    low_voltage_error_ = low_voltage_lockout_;
    if (low_voltage_error_sensor_) {
      low_voltage_error_sensor_->publish_state(low_voltage_error_);
    }
  }
}

void VevorHeater::handle_antifreeze_mode() {
  // No automatic restarts while a start or low voltage lockout is active
  if (start_lockout_ || low_voltage_lockout_) {
    return;
  }
  
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
  // Automatic modes skip their control logic while locked out, so this call
  // comes from the user (select, lambda, switch) and acknowledges the lockout
  clear_start_lockout();
  
  // Check if automatic mode requires external sensor
  if (control_mode_ == ControlMode::AUTOMATIC) {
    if (!has_external_sensor()) {
      ESP_LOGE(TAG, "Cannot turn on heater: automatic mode requires external temperature sensor!");
      return;
    }
  }
  
  // Check voltage before allowing start
  if (!voltage_valid_) {
    ESP_LOGD(TAG, "Cannot start heater yet: no input voltage received from the heater");
    return;
  }
  if (input_voltage_ < min_voltage_start_) {
    ESP_LOGW(TAG, "Cannot start heater: voltage too low (%.1fV < %.1fV)",
             input_voltage_, min_voltage_start_);
    set_low_voltage_lockout();
    update_low_voltage_error_sensor();
    return;
  }
  
  // Automatic modes skip their logic during a low voltage lockout, so this is
  // a user request with enough voltage - end the lockout early
  if (low_voltage_lockout_) {
    ESP_LOGI(TAG, "Low voltage lockout cleared by user turn on at %.1fV", input_voltage_);
    low_voltage_lockout_ = false;
    update_low_voltage_error_sensor();
  }
  
  heater_enabled_ = true;
  // Manual mode starts at the Manual power (default_power_percent until the
  // user changes it). Automatic and antifreeze modes set their power right
  // before calling turn_on(), so keep it.
  if (control_mode_ == ControlMode::MANUAL) {
    power_level_ = manual_power_level_;
  }
  ESP_LOGI(TAG, "Heater turned ON at %d%% power", power_level_ * 10);
}

void VevorHeater::turn_off() {
  heater_enabled_ = false;
  ESP_LOGI(TAG, "Heater turned OFF");
}

void VevorHeater::trigger_start_lockout(HeaterState old_state, HeaterState new_state) {
  // Restarting right away could pump fuel into a hot or flooded combustion
  // chamber again, so keep the heater off until the user acts
  ESP_LOGE(TAG, "Heater stopped on its own (%s -> %s) while requested to run - "
                "start lockout, automatic restarts blocked until the user turns it on again",
           state_to_string(old_state), state_to_string(new_state));
  heater_enabled_ = false;
  start_lockout_ = true;
  if (start_lockout_sensor_) {
    start_lockout_sensor_->publish_state(true);
  }
}

void VevorHeater::clear_start_lockout() {
  if (!start_lockout_) {
    return;
  }
  ESP_LOGI(TAG, "Start lockout cleared by user");
  start_lockout_ = false;
  if (start_lockout_sensor_) {
    start_lockout_sensor_->publish_state(false);
  }
}

const char *VevorHeater::mode_option_to_string(ModeOption option) {
  // Must match the options of the control mode select in __init__.py
  switch (option) {
    case ModeOption::MANUAL: return "Manual";
    case ModeOption::SEMI_AUTO: return "Semi-Auto";
    case ModeOption::FULL_AUTO: return "Full-Auto";
    case ModeOption::ANTIFREEZE: return "Antifreeze";
    default: return "Off";
  }
}

ModeOption VevorHeater::get_mode_option() const {
  if (control_mode_ == ControlMode::ANTIFREEZE) {
    return ModeOption::ANTIFREEZE;
  }
  if (control_mode_ == ControlMode::AUTOMATIC) {
    if (auto_mode_type_ == AutoModeType::SEMI_AUTO) {
      return ModeOption::SEMI_AUTO;
    }
    if (auto_mode_type_ == AutoModeType::FULL_AUTO) {
      return ModeOption::FULL_AUTO;
    }
    return ModeOption::OFF;
  }
  return heater_enabled_ ? ModeOption::MANUAL : ModeOption::OFF;
}

void VevorHeater::apply_mode_option(ModeOption option) {
  switch (option) {
    case ModeOption::OFF:
      set_control_mode(ControlMode::MANUAL);
      set_auto_mode_type(AutoModeType::OFF);
      turn_off();
      break;
    case ModeOption::MANUAL:
      set_control_mode(ControlMode::MANUAL);
      set_auto_mode_type(AutoModeType::OFF);
      turn_on();
      break;
    case ModeOption::SEMI_AUTO:
      set_control_mode(ControlMode::AUTOMATIC);
      set_auto_mode_type(AutoModeType::SEMI_AUTO);
      break;
    case ModeOption::FULL_AUTO:
      set_control_mode(ControlMode::AUTOMATIC);
      set_auto_mode_type(AutoModeType::FULL_AUTO);
      break;
    case ModeOption::ANTIFREEZE:
      set_control_mode(ControlMode::ANTIFREEZE);
      set_auto_mode_type(AutoModeType::OFF);
      break;
  }
  ESP_LOGI(TAG, "Control Mode: %s", mode_option_to_string(option));
}

void VevorHeater::select_mode_option(ModeOption option) {
  // Any explicit mode selection by the user acknowledges a start lockout
  clear_start_lockout();
  apply_mode_option(option);
  save_settings();
}

void VevorHeater::set_manual_power_percent(float percent) {
  manual_power_level_ = static_cast<uint8_t>(std::max(1.0f, std::min(10.0f, percent / 10.0f)));
  set_power_level_percent(percent);
  save_settings();
}

void VevorHeater::save_settings() {
  PersistedSettings settings;
  settings.mode_option = static_cast<uint8_t>(get_mode_option());
  settings.yaml_mode_option = static_cast<uint8_t>(yaml_mode_option_);
  settings.manual_power_level = manual_power_level_;
  settings.yaml_default_power_percent = default_power_percent_;
  settings.target_temperature = target_temperature_;
  settings.injected_per_pulse = injected_per_pulse_;
  settings.yaml_injected_per_pulse = yaml_injected_per_pulse_;
  if (!pref_settings_.save(&settings)) {
    ESP_LOGW(TAG, "Failed to save settings");
  }
}

void VevorHeater::load_settings() {
  PersistedSettings settings;
  if (!pref_settings_.load(&settings)) {
    ESP_LOGD(TAG, "No saved settings, using YAML configuration");
    return;
  }
  
  // A value from YAML that changed since the save wins over the saved one
  if (settings.yaml_mode_option == static_cast<uint8_t>(yaml_mode_option_) &&
      settings.mode_option <= static_cast<uint8_t>(ModeOption::ANTIFREEZE)) {
    ModeOption option = static_cast<ModeOption>(settings.mode_option);
    // Never turn the heater on unattended after a reboot or power loss
    if (option == ModeOption::MANUAL) {
      option = ModeOption::OFF;
    }
    ESP_LOGI(TAG, "Restoring control mode %s", mode_option_to_string(option));
    apply_mode_option(option);
  }
  if (std::isfinite(settings.target_temperature)) {
    target_temperature_ = settings.target_temperature;
  }
  if (settings.yaml_default_power_percent == default_power_percent_ &&
      settings.manual_power_level >= 1 && settings.manual_power_level <= 10) {
    manual_power_level_ = settings.manual_power_level;
  }
  if (settings.yaml_injected_per_pulse == yaml_injected_per_pulse_ &&
      std::isfinite(settings.injected_per_pulse) && settings.injected_per_pulse > 0.0f) {
    injected_per_pulse_ = settings.injected_per_pulse;
  }
}

void VevorHeater::set_power_level_percent(float percent) {
  uint8_t level = static_cast<uint8_t>(std::max(1.0f, std::min(10.0f, percent / 10.0f)));
  if (level != power_level_) {
    power_level_ = level;
    ESP_LOGI(TAG, "Heater power level set to %d (%.0f%%)", level, percent);
  }
}

float VevorHeater::calculate_power_for_temp_diff(float temp_diff) {
  // Calculate power based on temperature difference (target - current).
  // At or above target the heater only needs minimum power. Using the
  // absolute difference here would raise power the more the room overheats.
  if (temp_diff < 1.0f) {
    return 10.0f;
  } else if (temp_diff < 2.0f) {
    return 20.0f;
  } else if (temp_diff < 3.0f) {
    return 40.0f;
  } else if (temp_diff < 5.0f) {
    return 60.0f;
  } else if (temp_diff < 6.0f) {
    return 80.0f;
  } else {
    return 90.0f;
  }
}

void VevorHeater::handle_automatic_mode() {
  // No automatic restarts while a start or low voltage lockout is active
  if (start_lockout_ || low_voltage_lockout_) {
    return;
  }
  
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
  ESP_LOGCONFIG(TAG, "  Min Voltage Start/Operate: %.1fV / %.1fV, restart delay after low voltage: %" PRIu32 " s",
                min_voltage_start_, min_voltage_operate_, low_voltage_restart_delay_ms_ / 1000);
  ESP_LOGCONFIG(TAG, "  Power Level: %d/10", power_level_);
  ESP_LOGCONFIG(TAG, "  Target Temperature: %.1f°C", target_temperature_);
  ESP_LOGCONFIG(TAG, "  Injected per Pulse: %.2f ml", injected_per_pulse_);
  ESP_LOGCONFIG(TAG, "  Daily Consumption: %.2f ml", daily_consumption_ml_);
  if (time_component_ == nullptr) {
    ESP_LOGCONFIG(TAG, "  Time ID: not set - daily reset relies on the system clock being synced");
  }
  ESP_LOGCONFIG(TAG, "  Total Fuel Pulses: %.1f", total_fuel_pulses_);
  
  if (external_temperature_sensor_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  External Temperature Sensor: Configured, timeout %" PRIu32 " s",
                  external_temperature_timeout_ms_ / 1000);
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
  LOG_BINARY_SENSOR("  ", "Start Lockout", start_lockout_sensor_);
}

}  // namespace vevor_heater
}  // namespace esphome