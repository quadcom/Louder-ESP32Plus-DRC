#pragma once

#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"
#include "../tas58xx.h"

namespace esphome::tas58xx {

// The component's numeric sensor platform. Named for the fault counter it started
// as; it now carries the rest of the device's readable telemetry too. Everything
// here is served from the cache the component refreshes in its own 'update', so
// this polls memory rather than the I2C bus and its update_interval only controls
// how often Home Assistant hears about it.
class FaultSensor : public PollingComponent, public Parented<Tas58xxComponent> {
 public:
  void dump_config() override;
  void update() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  void set_times_faults_cleared_sensor(sensor::Sensor* sensor) { times_faults_cleared_sensor_ = sensor; }

  void set_pvdd_voltage_sensor(sensor::Sensor* sensor) { pvdd_voltage_sensor_ = sensor; }
  void set_max_output_power_sensor(sensor::Sensor* sensor) { max_output_power_sensor_ = sensor; }
  void set_clip_headroom_sensor(sensor::Sensor* sensor) { clip_headroom_sensor_ = sensor; }
  void set_over_temp_warning_level_sensor(sensor::Sensor* sensor) { over_temp_warning_level_sensor_ = sensor; }
  void set_sample_rate_sensor(sensor::Sensor* sensor) { sample_rate_sensor_ = sensor; }

 protected:
  // Publishes only when the value has moved, matching the fault counter's
  // behaviour. NAN is handled explicitly: it means "no measurement", and it never
  // compares equal to itself, so a plain != would republish it every interval.
  void publish_if_changed_(sensor::Sensor* sensor, float value);

  sensor::Sensor* times_faults_cleared_sensor_{nullptr};

  sensor::Sensor* pvdd_voltage_sensor_{nullptr};
  sensor::Sensor* max_output_power_sensor_{nullptr};
  sensor::Sensor* clip_headroom_sensor_{nullptr};
  sensor::Sensor* over_temp_warning_level_sensor_{nullptr};
  sensor::Sensor* sample_rate_sensor_{nullptr};

  // initialise as large number so first value of first update interval is saved
  uint32_t last_faults_cleared_{100000};
};

}  // namespace esphome::tas58xx
