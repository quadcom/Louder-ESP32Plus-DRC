#include "esphome/core/log.h"
#include "fault_sensor.h"

namespace esphome::tas58xx {

static constexpr const char* TAG = "tas58xx.sensor";

void  FaultSensor::dump_config() {
  ESP_LOGCONFIG(TAG, "Tas58xx Sensor:");
  LOG_SENSOR("  ", "Times Faults Cleared", this->times_faults_cleared_sensor_);
  LOG_SENSOR("  ", "PVDD Voltage", this->pvdd_voltage_sensor_);
  LOG_SENSOR("  ", "Maximum Output Power", this->max_output_power_sensor_);
  LOG_SENSOR("  ", "Clip Headroom", this->clip_headroom_sensor_);
  LOG_SENSOR("  ", "Over Temperature Warning Level", this->over_temp_warning_level_sensor_);
  LOG_SENSOR("  ", "Detected Sample Rate", this->sample_rate_sensor_);
  LOG_UPDATE_INTERVAL(this);
}

void  FaultSensor::update() {
  if (this->times_faults_cleared_sensor_ != nullptr) {
    // only publish if different to last value but will publish first value
    uint32_t current_faults_cleared = this->parent_->times_faults_cleared();
    if (current_faults_cleared != this->last_faults_cleared_) {
      this->times_faults_cleared_sensor_->publish_state(current_faults_cleared);
      this->last_faults_cleared_ = current_faults_cleared;
    }
  }

  this->publish_if_changed_(this->pvdd_voltage_sensor_, this->parent_->pvdd_voltage());
  this->publish_if_changed_(this->max_output_power_sensor_, this->parent_->max_output_power());
  this->publish_if_changed_(this->clip_headroom_sensor_, this->parent_->clip_headroom());
  this->publish_if_changed_(this->over_temp_warning_level_sensor_, (float) this->parent_->otw_level());
  this->publish_if_changed_(this->sample_rate_sensor_, (float) this->parent_->detected_sample_rate());
}

void FaultSensor::publish_if_changed_(sensor::Sensor* sensor, float value) {
  if (sensor == nullptr) return;

  if (!sensor->has_state()) {
    sensor->publish_state(value);
    return;
  }

  // NAN never equals itself, so it has to be compared as a state rather than as a
  // value - otherwise an unavailable sensor republishes on every interval.
  const bool was_nan = std::isnan(sensor->state);
  const bool is_nan = std::isnan(value);
  if (was_nan || is_nan) {
    if (was_nan != is_nan) sensor->publish_state(value);
    return;
  }

  if (sensor->state != value) sensor->publish_state(value);
}

}  // namespace esphome::tas58xx
