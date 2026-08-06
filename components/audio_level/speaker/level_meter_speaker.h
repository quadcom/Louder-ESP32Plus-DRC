#pragma once

#ifdef USE_ESP32

#include "esphome/components/audio/audio.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/speaker/speaker.h"

#include "esphome/core/component.h"

#include <atomic>

namespace esphome::audio_level {

// Sits in a speaker chain and measures what passes through it, forwarding every
// byte and every lifecycle call to the speaker below unchanged.
//
// WHAT THIS MEASURES, PRECISELY: the digital stream at the point it is inserted.
// Placed between the mixer and the I2S output it is the amplifier's digital
// input - which is NOT its output. The TAS58xx applies digital volume, EQ, the
// DRC, PVDD sensing and thermal foldback after this point, and reports none of
// them. Nothing here can be read as acoustic output or as speaker power.
//
// To compare a reading against a DRC threshold you also need the digital volume
// setting, since the DRC sees the signal after that stage. That correction is
// deliberately not applied here: it depends on where volume sits relative to the
// DRC in the loaded DSP process flow, which is not established for Process Flow 1.
// A template sensor adding id(external_dac)->volume() is the place to try it.
class LevelMeterSpeaker final : public PollingComponent, public speaker::Speaker {
 public:
  float get_setup_priority() const override { return esphome::setup_priority::DATA; }
  void setup() override;
  void dump_config() override;

  // Keeps state_ in step with the speaker below. Speaker::is_running() and
  // is_stopped() read a plain member rather than calling a virtual, so the chain
  // above sees this meter's state_ and never the real one - which matters because
  // the mixer and its source speakers both use those to decide when a stream has
  // finished draining.
  void loop() override;

  // Publishes the window that just closed and starts a new one.
  void update() override;

  size_t play(const uint8_t *data, size_t length, TickType_t ticks_to_wait) override;
  size_t play(const uint8_t *data, size_t length) override { return this->play(data, length, 0); }

  void start() override;
  void stop() override;
  void finish() override;

  bool has_buffered_data() const override { return this->output_speaker_->has_buffered_data(); }

  // Everything below is the speaker underneath us - a meter has no opinion on any
  // of it.
  void set_pause_state(bool pause_state) override { this->output_speaker_->set_pause_state(pause_state); }
  bool get_pause_state() const override { return this->output_speaker_->get_pause_state(); }

  void set_mute_state(bool mute_state) override;
  bool get_mute_state() override { return this->output_speaker_->get_mute_state(); }

  void set_volume(float volume) override;
  float get_volume() override { return this->output_speaker_->get_volume(); }

  void set_output_speaker(speaker::Speaker *speaker) { this->output_speaker_ = speaker; }

  void set_left_rms_sensor(sensor::Sensor *sensor) { this->left_rms_sensor_ = sensor; }
  void set_right_rms_sensor(sensor::Sensor *sensor) { this->right_rms_sensor_ = sensor; }
  void set_left_peak_sensor(sensor::Sensor *sensor) { this->left_peak_sensor_ = sensor; }
  void set_right_peak_sensor(sensor::Sensor *sensor) { this->right_peak_sensor_ = sensor; }

  void set_silence_floor(float floor_dbfs) { this->silence_floor_dbfs_ = floor_dbfs; }

 protected:
  // Per channel accumulator. Written from whichever task calls play - the mixer's,
  // not the main loop's - and drained by update on the main loop, so the folds are
  // atomic. Only four atomic operations per buffer: the per-sample work happens in
  // locals first.
  struct Accumulator {
    std::atomic<uint64_t> sum_squares{0};
    std::atomic<uint32_t> peak{0};
  };

  // Reads and zeroes an accumulator, then turns it into dBFS. Returns the silence
  // floor rather than -infinity for a window of pure digital silence.
  void publish_window_(sensor::Sensor *rms_sensor, sensor::Sensor *peak_sensor, Accumulator &accumulator,
                       uint32_t samples);

  // Everything is scaled to a 16 bit magnitude before squaring so the sum cannot
  // overflow a uint64 over any plausible window. On a 32 bit stream that discards
  // the bottom 16 bits, which puts the noise floor of the meter at about -96 dBFS -
  // far below anything a level meter is used to look at.
  void accumulate_(const uint8_t *data, size_t length);

  // Hands our stream format to the speaker below, which cannot receive it any other
  // way. See the comment on the definition - this is the whole reason a pass-through
  // speaker is not simply transparent.
  void forward_stream_info_();

  speaker::Speaker *output_speaker_{nullptr};

  sensor::Sensor *left_rms_sensor_{nullptr};
  sensor::Sensor *right_rms_sensor_{nullptr};
  sensor::Sensor *left_peak_sensor_{nullptr};
  sensor::Sensor *right_peak_sensor_{nullptr};

  Accumulator left_{};
  Accumulator right_{};

  // Samples folded into each accumulator since the last publish. One counter is
  // enough: both channels always advance together.
  std::atomic<uint32_t> samples_per_channel_{0};

  float silence_floor_dbfs_{-100.0f};

  bool published_silence_{false};
  bool unsupported_format_reported_{false};
};

}  // namespace esphome::audio_level

#endif  // USE_ESP32
