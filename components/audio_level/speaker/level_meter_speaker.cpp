#include "level_meter_speaker.h"

#ifdef USE_ESP32

#include "esphome/core/log.h"

#include <cmath>

namespace esphome::audio_level {

static constexpr const char *TAG = "audio_level.speaker";

// Full scale for the 16 bit magnitude everything is scaled to before squaring.
static constexpr float FULL_SCALE = 32768.0f;

void LevelMeterSpeaker::setup() {
  // Frames pass through untouched, so the timing the speaker below reports is
  // also ours. Without forwarding this, anything upstream that tracks playback
  // position - the media player's progress, in practice - stops being told.
  this->output_speaker_->add_audio_output_callback(
      [this](uint32_t new_frames, int64_t write_timestamp) { this->audio_output_callback_(new_frames, write_timestamp); });
}

void LevelMeterSpeaker::dump_config() {
  ESP_LOGCONFIG(TAG, "Audio Level Meter:");
  ESP_LOGCONFIG(TAG, "  Silence Floor: %.0f dBFS", this->silence_floor_dbfs_);
  LOG_SENSOR("  ", "Left RMS", this->left_rms_sensor_);
  LOG_SENSOR("  ", "Right RMS", this->right_rms_sensor_);
  LOG_SENSOR("  ", "Left Peak", this->left_peak_sensor_);
  LOG_SENSOR("  ", "Right Peak", this->right_peak_sensor_);
  LOG_UPDATE_INTERVAL(this);
}

void LevelMeterSpeaker::loop() {
  if (this->output_speaker_->is_running()) {
    this->state_ = speaker::STATE_RUNNING;
  } else if (this->output_speaker_->is_stopped()) {
    this->state_ = speaker::STATE_STOPPED;
  } else if (this->state_ != speaker::STATE_STOPPING) {
    // Mid transition. Only running and stopped are distinguishable from outside, so
    // anything else just has to read as neither - but keep a stop we were told about
    // rather than reporting it as a start.
    this->state_ = speaker::STATE_STARTING;
  }
}

void LevelMeterSpeaker::forward_stream_info_() {
  // Speaker::set_audio_stream_info() is NOT virtual, so the mixer's call to it landed
  // on this meter and stopped there - the speaker below never heard about the format.
  // Push it down while that speaker is stopped, which is the only point it is safe to
  // change and the point it matters: i2s_audio::play() auto-starts, and the task then
  // configures the I2S driver from whatever stream info it holds. Without this it
  // holds the AudioStreamInfo default - 16 bit, 1 channel, 16 kHz - and a 48 kHz
  // stereo stream comes out as a slow warble with the channels interleaved wrongly.
  if (this->output_speaker_->is_stopped()) {
    this->output_speaker_->set_audio_stream_info(this->audio_stream_info_);
  }
}

size_t LevelMeterSpeaker::play(const uint8_t *data, size_t length, TickType_t ticks_to_wait) {
  this->forward_stream_info_();

  const size_t written = this->output_speaker_->play(data, length, ticks_to_wait);

  // Measure what was actually accepted, not what was offered. A short write means
  // the tail was dropped and never reached the amplifier.
  if (written > 0) this->accumulate_(data, written);

  return written;
}

// The state each of these leaves behind is provisional: loop() replaces it with the
// speaker below as soon as that speaker settles. Reading the speaker here as well
// just avoids reporting a transition that already finished.
void LevelMeterSpeaker::start() {
  this->forward_stream_info_();
  this->output_speaker_->start();
  this->state_ = this->output_speaker_->is_running() ? speaker::STATE_RUNNING : speaker::STATE_STARTING;
}

void LevelMeterSpeaker::stop() {
  this->output_speaker_->stop();
  this->state_ = this->output_speaker_->is_stopped() ? speaker::STATE_STOPPED : speaker::STATE_STOPPING;
}

void LevelMeterSpeaker::finish() {
  this->output_speaker_->finish();
  this->state_ = this->output_speaker_->is_stopped() ? speaker::STATE_STOPPED : speaker::STATE_STOPPING;
}

void LevelMeterSpeaker::set_mute_state(bool mute_state) {
  this->mute_state_ = mute_state;
  this->output_speaker_->set_mute_state(mute_state);
}

void LevelMeterSpeaker::set_volume(float volume) {
  this->volume_ = volume;
  this->output_speaker_->set_volume(volume);
}

void LevelMeterSpeaker::accumulate_(const uint8_t *data, size_t length) {
  const uint8_t bits_per_sample = this->audio_stream_info_.get_bits_per_sample();
  const uint8_t channels = this->audio_stream_info_.get_channels();

  if ((bits_per_sample != 16 && bits_per_sample != 32) || channels == 0 || channels > 2) {
    if (!this->unsupported_format_reported_) {
      ESP_LOGW(TAG, "Cannot meter %u bit, %u channel audio - passing it through unmeasured", bits_per_sample, channels);
      this->unsupported_format_reported_ = true;
    }
    return;
  }

  // Locals first, one fold into the atomics at the end. play() runs on the audio
  // task and this is the hot path.
  uint64_t sum_left = 0, sum_right = 0;
  uint32_t peak_left = 0, peak_right = 0;
  uint32_t frames = 0;

  const bool stereo = (channels == 2);

  if (bits_per_sample == 16) {
    const int16_t *samples = reinterpret_cast<const int16_t *>(data);
    const size_t count = length / sizeof(int16_t);

    for (size_t i = 0; i + (stereo ? 1 : 0) < count; i += channels) {
      const uint32_t left = (uint32_t) std::abs((int32_t) samples[i]);
      sum_left += (uint64_t) left * left;
      if (left > peak_left) peak_left = left;

      if (stereo) {
        const uint32_t right = (uint32_t) std::abs((int32_t) samples[i + 1]);
        sum_right += (uint64_t) right * right;
        if (right > peak_right) peak_right = right;
      }
      frames++;
    }
  } else {
    const int32_t *samples = reinterpret_cast<const int32_t *>(data);
    const size_t count = length / sizeof(int32_t);

    for (size_t i = 0; i + (stereo ? 1 : 0) < count; i += channels) {
      // Down to a 16 bit magnitude so the squares stay in range.
      const uint32_t left = (uint32_t) std::abs(samples[i] >> 16);
      sum_left += (uint64_t) left * left;
      if (left > peak_left) peak_left = left;

      if (stereo) {
        const uint32_t right = (uint32_t) std::abs(samples[i + 1] >> 16);
        sum_right += (uint64_t) right * right;
        if (right > peak_right) peak_right = right;
      }
      frames++;
    }
  }

  if (frames == 0) return;

  // Mono is metered as the left channel and mirrored, so a mono stream does not
  // leave the right sensor stuck at its last stereo value.
  if (!stereo) {
    sum_right = sum_left;
    peak_right = peak_left;
  }

  this->left_.sum_squares.fetch_add(sum_left, std::memory_order_relaxed);
  this->right_.sum_squares.fetch_add(sum_right, std::memory_order_relaxed);
  this->samples_per_channel_.fetch_add(frames, std::memory_order_relaxed);

  // No atomic fetch_max before C++26, so compare and exchange until it sticks.
  uint32_t current = this->left_.peak.load(std::memory_order_relaxed);
  while (peak_left > current && !this->left_.peak.compare_exchange_weak(current, peak_left, std::memory_order_relaxed)) {
  }
  current = this->right_.peak.load(std::memory_order_relaxed);
  while (peak_right > current &&
         !this->right_.peak.compare_exchange_weak(current, peak_right, std::memory_order_relaxed)) {
  }
}

void LevelMeterSpeaker::update() {
  const uint32_t samples = this->samples_per_channel_.exchange(0, std::memory_order_relaxed);

  // An idle device would otherwise publish four unchanging states twice a second
  // forever. Report the drop to the floor once, then go quiet until audio returns -
  // holding the last reading instead would show a stopped stream as still playing.
  if (samples == 0) {
    if (this->published_silence_) return;
    this->published_silence_ = true;
  } else {
    this->published_silence_ = false;
  }

  this->publish_window_(this->left_rms_sensor_, this->left_peak_sensor_, this->left_, samples);
  this->publish_window_(this->right_rms_sensor_, this->right_peak_sensor_, this->right_, samples);
}

void LevelMeterSpeaker::publish_window_(sensor::Sensor *rms_sensor, sensor::Sensor *peak_sensor,
                                        Accumulator &accumulator, uint32_t samples) {
  const uint64_t sum_squares = accumulator.sum_squares.exchange(0, std::memory_order_relaxed);
  const uint32_t peak = accumulator.peak.exchange(0, std::memory_order_relaxed);

  if (rms_sensor != nullptr) {
    float rms_dbfs = this->silence_floor_dbfs_;
    if (samples > 0 && sum_squares > 0) {
      const float mean_square = (float) ((double) sum_squares / (double) samples);
      // 10*log10 of a mean square is 20*log10 of an amplitude - no sqrt needed.
      rms_dbfs = 10.0f * log10f(mean_square / (FULL_SCALE * FULL_SCALE));
      if (rms_dbfs < this->silence_floor_dbfs_) rms_dbfs = this->silence_floor_dbfs_;
    }
    rms_sensor->publish_state(rms_dbfs);
  }

  if (peak_sensor != nullptr) {
    float peak_dbfs = this->silence_floor_dbfs_;
    if (peak > 0) {
      peak_dbfs = 20.0f * log10f((float) peak / FULL_SCALE);
      if (peak_dbfs < this->silence_floor_dbfs_) peak_dbfs = this->silence_floor_dbfs_;
    }
    peak_sensor->publish_state(peak_dbfs);
  }
}

}  // namespace esphome::audio_level

#endif  // USE_ESP32
