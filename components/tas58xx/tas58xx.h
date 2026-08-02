#pragma once

#include "esphome/components/audio_dac/audio_dac.h"
#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/core/hal.h"

#include "tas58xx_defs.h"
#include "tas58xx_eq_common.h"
#include "tas58xx_eq_bands.h"
#include "tas58xx_eq_profiles.h"
#include "tas58xx_drc.h"
#include "tas58xx_helpers.h"

#ifdef USE_TAS58XX_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif

#include <vector>

namespace esphome::tas58xx {

// Defined in number/drc_number.h, which includes this header - so only the
// pointer type is available here.
class DrcNumber;

class Tas58xxComponent : public audio_dac::AudioDac, public PollingComponent, public i2c::I2CDevice {
 public:
  void setup() override;

  void loop() override;
  void update() override;

  void dump_config() override;

  float get_setup_priority() const override { return setup_priority::IO; }

  void set_enable_pin(GPIOPin *enable) { this->enable_pin_ = enable; }

  // optional YAML config

  void config_analog_gain(float analog_gain) { this->tas58xx_analog_gain_ = analog_gain; }

  void config_dac_mode(DacMode dac_mode) {this->tas58xx_dac_mode_ = dac_mode; }

  void config_modulation_scheme(ModulationScheme modulation_scheme) {this->tas58xx_modulation_scheme_ = modulation_scheme; }

  void config_eq_mode(uint8_t configured_eq_mode) { this->configured_eq_mode_ = static_cast<EqMode>(configured_eq_mode); }

  void config_ignore_fault_mode(ExcludeIgnoreMode ignore_fault_mode) {
    this->ignore_clock_faults_when_clearing_faults_ = (ignore_fault_mode == ExcludeIgnoreMode::CLOCK_FAULT);
  }

  void config_mixer_mode(MixerMode mixer_mode) {this->tas58xx_mixer_mode_ = mixer_mode; }

  void config_refresh_eq(EqRefreshMode eq_refresh) { this->eq_refresh_ = eq_refresh; }

  //// DRC configuration from YAML

  void config_drc_band_mode(DrcBandMode mode) { this->drc_band_mode_ = mode; }

  // Crossover corners for DRC_THREE_BAND. Ignored in DRC_ONE_BAND, where the
  // crossover biquads are deliberately left at their pass-through defaults.
  void config_drc_crossover(float low_hz, float high_hz) {
    this->drc_crossover_low_hz_ = low_hz;
    this->drc_crossover_high_hz_ = high_hz;
  }

  void config_drc_energy_ms(float energy_ms) { this->drc_energy_ms_ = energy_ms; }

  // The DSP internal rate, which is what the 1.31 time constants are relative
  // to - not the I2S input rate. See tas58xx_drc.h.
  void config_drc_dsp_rate(uint32_t rate) { this->drc_dsp_rate_ = rate; }

  // configured maximum and minimum with units dB
  void config_volume_max(float volume_max) { this->tas58xx_volume_max_ = (int8_t)(volume_max); }
  void config_volume_min(float volume_min) { this->tas58xx_volume_min_ = (int8_t)(volume_min); }


  #ifdef USE_TAS58XX_BINARY_SENSOR
  void config_exclude_fault(ExcludeIgnoreMode exclude_fault) {
    this->exclude_clock_fault_from_have_faults_ = (exclude_fault == ExcludeIgnoreMode::CLOCK_FAULT);
  }

  SUB_BINARY_SENSOR(have_fault)
  SUB_BINARY_SENSOR(left_channel_dc_fault)
  SUB_BINARY_SENSOR(right_channel_dc_fault)
  SUB_BINARY_SENSOR(left_channel_over_current_fault)
  SUB_BINARY_SENSOR(right_channel_over_current_fault)

  SUB_BINARY_SENSOR(otp_crc_check_error)
  SUB_BINARY_SENSOR(bq_write_failed_fault)
  SUB_BINARY_SENSOR(clock_fault)
  SUB_BINARY_SENSOR(pvdd_over_voltage_fault)
  SUB_BINARY_SENSOR(pvdd_under_voltage_fault)

  SUB_BINARY_SENSOR(over_temperature_shutdown_fault)
  SUB_BINARY_SENSOR(over_temperature_warning)
  #endif

  void enable_dac(bool enable);

  uint8_t get_configured_eq_mode();

  uint8_t get_mixer_mode();
  bool set_mixer_mode(MixerMode mode);

  bool is_eq_configured();

  void refresh_eq_settings();

  bool set_channel_volume(Channels channel, int8_t volume_dB);

  void select_eq_mode(uint8_t select_index);

  bool set_eq_gain(Channels channel, uint8_t band_index, int8_t gain);

  bool set_eq_preset(Channels channel, uint8_t select_preset);

  //// DRC runtime control, used by the DRC number and switch entities

  bool set_drc_threshold(DrcBand band, float threshold_db);
  bool set_drc_ratio(DrcBand band, float ratio);
  bool set_drc_attack(DrcBand band, float attack_ms);
  bool set_drc_release(DrcBand band, float release_ms);
  bool set_drc_makeup(DrcBand band, float makeup_db);

  // Master enable. Off restores the documented reset behaviour: all slopes and
  // offsets zeroed so the DRC is a no-op, band 1 mixer at unity, bands 2 and 3
  // muted. Thresholds and time constants are left alone since they are inert
  // while the slopes are zero.
  bool set_drc_enable(bool enable);
  bool drc_enabled() { return this->drc_enabled_; }

  // Which formula produces the two offset registers. Runtime-switchable because
  // the part's convention is still being established by listening, and a
  // rebuild-and-flash per hypothesis is a slow way to run that experiment.
  // Rewrites every band immediately. See DrcOffsetConvention in tas58xx_drc.h.
  bool set_drc_offset_convention(DrcOffsetConvention convention);
  DrcOffsetConvention drc_offset_convention() { return this->drc_offset_convention_; }

  bool drc_is_three_band() { return this->drc_band_mode_ == DRC_THREE_BAND; }

  // Reads the live DRC block back and logs it. Captures a baseline before
  // anything is written, and verifies writes afterwards.
  void log_drc_registers();

  // Called by each DrcNumber from its setup(), so a reset can drive exactly the
  // controls the YAML defines and no others.
  void register_drc_number(DrcNumber *number);

  // Restores every configured DRC control to its compiled-in default: ratio
  // 1:1, makeup 0dB, and the documented threshold and time constants. Ratio 1:1
  // is a unity gain curve, so this leaves the DRC inert whatever the master
  // switch is doing. States are published and saved, so it survives a reboot.
  void reset_drc_to_defaults();

  bool is_muted() override { return this->is_muted_; }
  bool set_mute_off() override;
  bool set_mute_on() override;

  uint32_t times_faults_cleared();

  bool using_auto_eq_refresh();
  bool using_manual_eq_refresh();

  float volume() override;
  bool set_volume(float value) override;

 protected:
   GPIOPin* enable_pin_{nullptr};

   bool configure_registers_();

   bool get_analog_gain_(uint8_t* raw_gain);
   bool set_analog_gain_(float gain_db);

   bool get_dac_mode_(DacMode* mode);
   bool set_dac_mode_(DacMode mode);

   bool set_deep_sleep_off_();
   bool set_deep_sleep_on_();

   bool get_digital_volume_(uint8_t* raw_volume);
   bool set_digital_volume_(uint8_t new_volume);

   bool get_eq_mode_(EqMode* current_mode);
   bool set_eq_mode_(EqMode new_mode);

   bool set_modulation_scheme_(ModulationScheme modulation);

   bool get_state_(ControlState* state);
   bool set_state_(ControlState state);

   // manage faults
   bool clear_fault_registers_();

   #ifdef USE_TAS58XX_BINARY_SENSOR
   void publish_faults_();
   void publish_channel_faults_();
   void publish_global_faults_();
   #endif

   bool read_fault_registers_();


   //// DRC internals

   // Writes one 32-bit DSP coefficient, already byte-swapped for the wire.
   bool write_drc_coefficient_(uint8_t page, uint8_t sub_addr, int32_t wire_value);

   // Read-only sweep of the crossover region, called by log_drc_registers().
   // Dumps every coefficient slot, flags anything shaped like a resting
   // pass-through biquad, and reports what is actually at each address the
   // DRC_XOVER_* tables claim. Locating the crossover blocks by observation
   // rather than by trusting the reconstructed addresses.
   void log_drc_crossover_scan_();

   bool write_drc_biquad_(const DrcBiquadAddress& address,
                          const tas58xx_helpers::BiquadCoefficients& bq);

   // Writes the whole DRC block in one pass: crossover, then per-band
   // coefficients, then the band mixer gains. Used at boot and whenever the
   // master enable flips, because the intermediate states are audible.
   bool apply_drc_all_();

   // Computes and writes the ten coefficients of one band from its settings.
   bool apply_drc_band_(DrcBand band);

   // Zeroes every slope and offset so the DRC becomes a no-op.
   bool apply_drc_bypass_();

   // Programs the crossover. Must run before the band mixer gains are opened up:
   // bands 2 and 3 carry the full-range signal until their crossover is written.
   bool apply_drc_crossover_();

   // Band mixer gains, which is what actually admits each band into the sum.
   bool apply_drc_mixer_();

   bool drc_band_is_active_(DrcBand band);

   // low level functions
   bool book_and_page_write_(uint8_t book, uint8_t page, uint8_t sub_addr, uint8_t* data, uint8_t number_bytes);

   bool book_and_page_read_(uint8_t book, uint8_t page, uint8_t sub_addr, uint8_t* data, uint8_t number_bytes);

  //  int32_t gain_to_f9_23_(int8_t gain);

   bool tas58xx_read_bytes_(uint8_t a_register, uint8_t* data, uint8_t number_bytes);
   bool tas58xx_write_byte_(uint8_t a_register, uint8_t data);
   bool tas58xx_write_bytes_(uint8_t a_register, uint8_t *data, uint8_t number_bytes);

   //// variables
   EqMode configured_eq_mode_; // derived from YAML

   enum ErrorCode {
     NONE = 0,
     CONFIGURATION_FAILED,
   } error_code_{NONE};

   // configured by YAML
   EqRefreshMode eq_refresh_;  // YAML default 'AUTO' = 0

   #ifdef USE_TAS58XX_BINARY_SENSOR
   bool exclude_clock_fault_from_have_faults_; // YAML default = true
   #endif

   bool ignore_clock_faults_when_clearing_faults_; // YAML default = true

   float tas58xx_analog_gain_; // configured in YAML

   uint8_t tas58xx_channel_preset_[NUMBER_CHANNELS]{0};
   int8_t tas58xx_channel_volume_[NUMBER_CHANNELS]{0};

   ControlState tas58xx_control_state_; // initialised in setup

   DacMode tas58xx_dac_mode_; // configured in YAML

#if defined(USE_TAS58XX_EQ_GAINS) || defined(USE_TAS58XX_EQ_PRESETS)
   bool eq_configured_{true};
#else
   bool eq_configured_{false};
#endif

   int8_t tas58xx_eq_gain_[NUMBER_CHANNELS][NUMBER_EQ_BANDS]{0}; // used if eq gain numbers are defined in YAML

   EqMode tas58xx_eq_mode_{EQ_OFF}; // current selected eq mode = EQ_OFF or EqMode configured_eq_mode_

   MixerMode tas58xx_mixer_mode_; // YAML default = STEREO

   ModulationScheme tas58xx_modulation_scheme_; // YAML default = BD Mode

   uint8_t tas58xx_raw_volume_max_; // maximum volume as digital volume register range 254 to 0
   uint8_t tas58xx_raw_volume_min_; // minimum volume as digital volume register range 254 to 0

   int8_t tas58xx_volume_max_;  // YAML configured maximum volume dB
   int8_t tas58xx_volume_min_;  // YAML configured maximum volume dB

   //// fault processing variables
   bool is_fault_to_clear_{false}; // false so clear fault registers is skipped on first update

   bool is_new_channel_fault_{true};  // conditionally publish binary sensors - initially true so published on first update
   bool is_new_common_fault_{true};
   bool is_new_global_fault_{true};
   bool is_new_over_temperature_issue_{true};

   Tas58xxFault tas58xx_faults_;  // current state of faults

   uint32_t times_faults_cleared_{0}; // counts number of times the faults register is cleared (used for publishing to sensor)

   //// utility variables used by loop, update and dump_config
   bool update_delay_finished_{false}; // use to indicate if delay before starting 'update' starting is complete

   uint8_t i2c_error_{0}; // last i2c error

   uint8_t loop_counter_{0}; // counts number of 'loop' iterations before proceeding

   LoopSetupStage loop_setup_stage_{WAIT_FOR_TRIGGER}; // used for state machine in 'loop'

   uint16_t number_registers_configured_{0}; // number tas58xx registers configured during 'setup'

   uint8_t refresh_band_{0}; // eq band currently being refreshed by 'loop'

   //// DRC state
   DrcBandSettings tas58xx_drc_[NUMBER_DRC_BANDS];

   // Every DRC number the YAML defines, registered from its setup(). Only used
   // by reset_drc_to_defaults(); at most fifteen entries.
   std::vector<DrcNumber *> drc_numbers_;

   bool drc_enabled_{false};   // master enable; DRC is a no-op out of reset

   DrcBandMode drc_band_mode_{DRC_ONE_BAND}; // YAML default = full range, band 1 only

   DrcOffsetConvention drc_offset_convention_{DRC_OFFSET_DEFAULT};

   float drc_crossover_low_hz_{300.0f};   // YAML configured, used when three band
   float drc_crossover_high_hz_{3000.0f};

   float drc_energy_ms_{DRC_ENERGY_DEFAULT_MS}; // RMS estimator window, all bands

   uint32_t drc_dsp_rate_{DRC_DEFAULT_DSP_RATE};

   uint32_t start_time_; // initialised in setup, used for delay in starting 'update'
};

}  // namespace esphome::tas58xx
