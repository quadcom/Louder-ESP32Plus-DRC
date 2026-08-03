// 3-band DRC control for the TAS5805M / TAS5825M.
//
// Address maps and the reasoning behind the coefficient math live in
// tas58xx_drc.h and docs/tas58xx-drc-reference.md.

#include "tas58xx.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

// For reset_drc_to_defaults(), which needs the complete DrcNumber type -
// tas58xx.h only forward-declares it. Safe to include unconditionally for the
// same reason the definitions below are unguarded: number/drc_number.cpp is
// always compiled, so this header is always part of the build.
#include "number/drc_number.h"

#include <cstdio>   // snprintf, for the crossover scan dump

namespace esphome::tas58xx {

// Deliberately NOT wrapped in '#ifdef USE_TAS58XX_DRC'. ESPHome compiles every
// source file in the component directory regardless of which platforms the YAML
// uses, so number/drc_number.cpp is always built and always references these
// symbols. Guarding the definitions here would break the link for any config
// that omits the DRC entities.
//
// Nothing is written to the DSP in that case: STAGE_AFTER_EQ in tas58xx_defs.h
// skips the DRC_SETUP stage entirely unless USE_TAS58XX_DRC is defined, so the
// cost is a few KB of flash and no I2C traffic.

#ifdef USE_TAS5805M_DAC
static constexpr const char* TAG = "tas5805m.drc";
#else
static constexpr const char* TAG = "tas5825m.drc";
#endif

static constexpr const char* ERROR = "Error";

using tas58xx_helpers::BiquadCoefficients;
using tas58xx_helpers::design_butterworth_highpass;
using tas58xx_helpers::design_butterworth_lowpass;
using tas58xx_helpers::linear_db_to_f9_23;
using tas58xx_helpers::log_units_to_f9_23;
using tas58xx_helpers::raw_to_wire;
using tas58xx_helpers::ratio_to_slope;
using tas58xx_helpers::slope_to_f9_23;
using tas58xx_helpers::time_constant_to_f1_31;

//// public setters
//
// Each caches the value and, once 'loop' has reached DRC_SETUP, rewrites that
// band. Before then the value is only stored, exactly as the EQ and volume
// setters behave - the DSP will not accept coefficients until it has seen a
// valid I2S clock.

bool Tas58xxComponent::set_drc_threshold(DrcBand band, float threshold_db) {
  if (threshold_db < DRC_THRESHOLD_MIN_DB || threshold_db > DRC_THRESHOLD_MAX_DB) {
    ESP_LOGE(TAG, "Invalid %s band DRC threshold: %.1fdB", DRC_BAND_TEXT[band], threshold_db);
    return false;
  }
  this->tas58xx_drc_[band].threshold_db = threshold_db;
  return this->apply_drc_band_(band);
}

bool Tas58xxComponent::set_drc_ratio(DrcBand band, float ratio) {
  if (ratio < DRC_RATIO_MIN || ratio > DRC_RATIO_MAX) {
    ESP_LOGE(TAG, "Invalid %s band DRC ratio: %.1f:1", DRC_BAND_TEXT[band], ratio);
    return false;
  }
  this->tas58xx_drc_[band].ratio = ratio;
  return this->apply_drc_band_(band);
}

bool Tas58xxComponent::set_drc_attack(DrcBand band, float attack_ms) {
  if (attack_ms < DRC_ATTACK_MIN_MS || attack_ms > DRC_ATTACK_MAX_MS) {
    ESP_LOGE(TAG, "Invalid %s band DRC attack: %.1fms", DRC_BAND_TEXT[band], attack_ms);
    return false;
  }
  this->tas58xx_drc_[band].attack_ms = attack_ms;
  return this->apply_drc_band_(band);
}

bool Tas58xxComponent::set_drc_release(DrcBand band, float release_ms) {
  if (release_ms < DRC_RELEASE_MIN_MS || release_ms > DRC_RELEASE_MAX_MS) {
    ESP_LOGE(TAG, "Invalid %s band DRC release: %.1fms", DRC_BAND_TEXT[band], release_ms);
    return false;
  }
  this->tas58xx_drc_[band].release_ms = release_ms;
  return this->apply_drc_band_(band);
}

bool Tas58xxComponent::set_drc_makeup(DrcBand band, float makeup_db) {
  if (makeup_db < DRC_MAKEUP_MIN_DB || makeup_db > DRC_MAKEUP_MAX_DB) {
    ESP_LOGE(TAG, "Invalid %s band DRC makeup: %.1fdB", DRC_BAND_TEXT[band], makeup_db);
    return false;
  }
  this->tas58xx_drc_[band].makeup_db = makeup_db;

  // Makeup lives in the band mixer gain, not in the band's own coefficients.
  if (this->loop_setup_stage_ < DRC_SETUP) return true;
  return this->apply_drc_mixer_();
}

bool Tas58xxComponent::set_drc_enable(bool enable) {
  this->drc_enabled_ = enable;

  if (this->loop_setup_stage_ < DRC_SETUP) {
    ESP_LOGV(TAG, "Save DRC enable: %s", ONOFF(enable));
    return true;
  }

  return this->apply_drc_all_();
}

bool Tas58xxComponent::apply_drc_all_() {
  // Order matters in both directions:
  //   - the crossover must be in place before the mixer admits bands 2 and 3,
  //     or they sum three copies of the full-range signal
  //   - the mixer must come last so makeup gain is never applied while the
  //     slopes are still flat, which would be an audible level jump
  bool ok = this->apply_drc_crossover_();

  if (this->drc_enabled_) {
    for (uint8_t band = 0; band < NUMBER_DRC_BANDS; band++) {
      if (!this->apply_drc_band_(static_cast<DrcBand>(band))) ok = false;
    }
  } else {
    if (!this->apply_drc_bypass_()) ok = false;
  }

  if (!this->apply_drc_mixer_()) ok = false;

  ESP_LOGD(TAG, "DRC %s - %s", ONOFF(this->drc_enabled_),
           this->drc_is_three_band() ? "3 band" : "full range, band 1 only");
  return ok;
}

//// internals

// In one-band mode only band 1 carries audio; bands 2 and 3 stay muted.
bool Tas58xxComponent::drc_band_is_active_(DrcBand band) {
  return this->drc_is_three_band() || band == DRC_LOW;
}

bool Tas58xxComponent::write_drc_coefficient_(uint8_t page, uint8_t sub_addr, int32_t wire_value) {
  return this->book_and_page_write_(TAS58XX_AUDIO_CTRL_BOOK, page, sub_addr,
                                    reinterpret_cast<uint8_t*>(&wire_value), sizeof(wire_value));
}

bool Tas58xxComponent::apply_drc_band_(DrcBand band) {
  if (this->loop_setup_stage_ < DRC_SETUP) {
    ESP_LOGV(TAG, "Save %s band DRC settings", DRC_BAND_TEXT[band]);
    return true;
  }

  const DrcBandAddresses& addr = DRC_ADDRESS[band];
  const DrcBandSettings& s = this->tas58xx_drc_[band];

  if (!this->drc_enabled_) {
    ESP_LOGV(TAG, "DRC disabled, not writing %s band", DRC_BAND_TEXT[band]);
    return true;
  }

  if (!this->drc_band_is_active_(band)) {
    ESP_LOGW(TAG, "%s band DRC set while in one band mode - band is muted, setting has no effect",
             DRC_BAND_TEXT[band]);
  }

  // Single knee at T1. Regions 2 and 3 share one slope so the curve above the
  // knee is a straight line; T2 only exists because the hardware insists on
  // three regions, and it is pinned just below full scale (zero and positive
  // thresholds are disallowed).
  const float t1_db = s.threshold_db;
  const float t2_db = DRC_T2_DB;
  const float slope = ratio_to_slope(s.ratio);

  // off1 governs region 1, BELOW the knee - not region 2, which is what SLOA148's
  // numbering suggests and what this code assumed until it was measured. Region 1
  // already has a zero slope, so any offset there is a pure constant attenuation
  // on exactly the quiet material the DRC is supposed to leave alone. It has to
  // be zero.
  //
  // off2 governs region 2 and is the y-intercept that places the knee at T1: the
  // part computes gain = k*x + O and does not subtract the threshold itself. It
  // must not be left at zero, or the region unanchors into a boost of -k*x.
  //
  // See the offsets note in tas58xx_drc.h for the measurements behind both.
  const float off1_db = 0.0f;
  const float off2_db = -slope * t1_db;

  // Ascending address order, matching how the biquad blocks must be written.
  bool ok = true;
  if (!this->write_drc_coefficient_(addr.energy.page, addr.energy.sub_addr,
                                    time_constant_to_f1_31(this->drc_energy_ms_, this->drc_dsp_rate_))) ok = false;
  if (!this->write_drc_coefficient_(addr.attack.page, addr.attack.sub_addr,
                                    time_constant_to_f1_31(s.attack_ms, this->drc_dsp_rate_))) ok = false;
  if (!this->write_drc_coefficient_(addr.decay.page, addr.decay.sub_addr,
                                    time_constant_to_f1_31(s.release_ms, this->drc_dsp_rate_))) ok = false;

  // Everything above is in dB, which is the only sane way to reason about a
  // compressor. The registers are not: see the DB_PER_UNIT constants in the
  // header for the measurement that established the DSP's actual log domain.
  const float slope_raw = slope / DRC_SLOPE_POWER_FACTOR;
  const float t1_raw = t1_db / DRC_THRESHOLD_DB_PER_UNIT;
  const float t2_raw = t2_db / DRC_THRESHOLD_DB_PER_UNIT;
  const float off1_raw = off1_db / DRC_OFFSET_DB_PER_UNIT;
  const float off2_raw = off2_db / DRC_OFFSET_DB_PER_UNIT;

  // Region 1 is below the knee and must stay unity. A zero slope needs no
  // scaling, but it goes through the same conversion so the three reads alike.
  if (!this->write_drc_coefficient_(addr.k0.page, addr.k0.sub_addr, slope_to_f9_23(0.0f))) ok = false;
  if (!this->write_drc_coefficient_(addr.k1.page, addr.k1.sub_addr, slope_to_f9_23(slope_raw))) ok = false;
  if (!this->write_drc_coefficient_(addr.k2.page, addr.k2.sub_addr, slope_to_f9_23(slope_raw))) ok = false;

  if (!this->write_drc_coefficient_(addr.t1.page, addr.t1.sub_addr, log_units_to_f9_23(t1_raw))) ok = false;
  if (!this->write_drc_coefficient_(addr.t2.page, addr.t2.sub_addr, log_units_to_f9_23(t2_raw))) ok = false;

  if (!this->write_drc_coefficient_(addr.off1.page, addr.off1.sub_addr, log_units_to_f9_23(off1_raw))) ok = false;
  if (!this->write_drc_coefficient_(addr.off2.page, addr.off2.sub_addr, log_units_to_f9_23(off2_raw))) ok = false;

  if (!ok) {
    ESP_LOGW(TAG, "%s writing %s band DRC coefficients", ERROR, DRC_BAND_TEXT[band]);
    return false;
  }

  // Both the dB intent and the DSP units go in the log, so a measurement can be
  // checked against what was actually written without decoding a register dump.
  ESP_LOGD(TAG, "Set %s band DRC: %.1fdB %.1f:1 attack %.1fms release %.0fms "
                "(k=%.4f off1=%.2fdB off2=%.2fdB) "
                "raw: k=%.4f t1=%.4f t2=%.4f off1=%.4f off2=%.4f",
           DRC_BAND_TEXT[band], t1_db, s.ratio, s.attack_ms, s.release_ms, slope,
           off1_db, off2_db,
           slope_raw, t1_raw, t2_raw, off1_raw, off2_raw);
  return true;
}

bool Tas58xxComponent::apply_drc_bypass_() {
  bool ok = true;

  for (uint8_t band = 0; band < NUMBER_DRC_BANDS; band++) {
    const DrcBandAddresses& addr = DRC_ADDRESS[band];

    // Zero slopes and offsets is exactly the documented reset state, and makes
    // the gain-adjustment curve flat regardless of the thresholds.
    if (!this->write_drc_coefficient_(addr.k0.page, addr.k0.sub_addr,
                                      raw_to_wire(DRC_DEFAULT_SLOPE))) ok = false;
    if (!this->write_drc_coefficient_(addr.k1.page, addr.k1.sub_addr,
                                      raw_to_wire(DRC_DEFAULT_SLOPE))) ok = false;
    if (!this->write_drc_coefficient_(addr.k2.page, addr.k2.sub_addr,
                                      raw_to_wire(DRC_DEFAULT_SLOPE))) ok = false;
    if (!this->write_drc_coefficient_(addr.off1.page, addr.off1.sub_addr,
                                      raw_to_wire(DRC_DEFAULT_OFFSET))) ok = false;
    if (!this->write_drc_coefficient_(addr.off2.page, addr.off2.sub_addr,
                                      raw_to_wire(DRC_DEFAULT_OFFSET))) ok = false;
  }

  if (!ok) ESP_LOGW(TAG, "%s bypassing DRC", ERROR);
  return ok;
}

bool Tas58xxComponent::write_drc_biquad_(const DrcBiquadAddress& address, const BiquadCoefficients& bq) {
  uint8_t coefficients[BIQUAD_SIZE];
  tas58xx_helpers::encode_biquad(bq, DRC_XOVER_MIXED_FORMAT, coefficients);

  // book_and_page_write_ carries a biquad across at most one page boundary,
  // which is what low BQ1 and mid BQ3 need on the TAS5825M and mid BQ2 on the
  // TAS5805M.
  return this->book_and_page_write_(TAS58XX_EQ_CTRL_BOOK, address.page, address.sub_addr,
                                    coefficients, BIQUAD_SIZE);
}

bool Tas58xxComponent::apply_drc_crossover_() {
  if (!this->drc_is_three_band()) {
    // Deliberately leave the crossover biquads at their pass-through defaults.
    // Band 1 then sees the full range, which is what one-band mode wants.
    ESP_LOGV(TAG, "One band DRC - crossover left at pass-through");
    return true;
  }

  const float low_hz = this->drc_crossover_low_hz_;
  const float high_hz = this->drc_crossover_high_hz_;
  const uint32_t fs = this->drc_dsp_rate_;

  if (low_hz >= high_hz) {
    ESP_LOGE(TAG, "DRC crossover corners cross over: low %.0fHz is not below high %.0fHz",
             low_hz, high_hz);
    return false;
  }

  // Two cascaded Butterworth sections per corner give a 4th-order
  // Linkwitz-Riley slope, which sums flat through the crossover region.
  const BiquadCoefficients low_lpf = design_butterworth_lowpass(low_hz, fs);
  const BiquadCoefficients high_hpf = design_butterworth_highpass(high_hz, fs);
  const BiquadCoefficients mid_hpf = design_butterworth_highpass(low_hz, fs);
  const BiquadCoefficients mid_lpf = design_butterworth_lowpass(high_hz, fs);

  bool ok = true;

  for (uint8_t i = 0; i < DRC_XOVER_LOW_SECTIONS; i++) {
    if (!this->write_drc_biquad_(DRC_XOVER_LOW[i], low_lpf)) ok = false;
  }

  for (uint8_t i = 0; i < DRC_XOVER_HIGH_SECTIONS; i++) {
    if (!this->write_drc_biquad_(DRC_XOVER_HIGH[i], high_hpf)) ok = false;
  }

  // Mid is a bandpass: highpass at the low corner cascaded with lowpass at the
  // high corner. Cascade order does not change the response.
  for (uint8_t i = 0; i < DRC_XOVER_MID_SECTIONS; i++) {
    const BiquadCoefficients& section = (i < DRC_XOVER_MID_SECTIONS / 2) ? mid_hpf : mid_lpf;
    if (!this->write_drc_biquad_(DRC_XOVER_MID[i], section)) ok = false;
  }

  if (!ok) {
    ESP_LOGW(TAG, "%s writing DRC crossover", ERROR);
    return false;
  }

  ESP_LOGD(TAG, "Set DRC crossover: %.0fHz / %.0fHz LR4 at %uHz", low_hz, high_hz,
           static_cast<unsigned>(fs));
  return true;
}

bool Tas58xxComponent::apply_drc_mixer_() {
  bool ok = true;

  for (uint8_t i = 0; i < NUMBER_DRC_BANDS; i++) {
    const DrcBand band = static_cast<DrcBand>(i);
    int32_t gain;

    if (!this->drc_band_is_active_(band)) {
      // One band mode: bands 2 and 3 stay muted, as they are out of reset.
      gain = raw_to_wire(DRC_MIXER_MUTE);
    } else if (!this->drc_enabled_) {
      // Bypassed. Every active band sits at unity. In three band mode that is
      // an LR4 sum of all three, which is flat - NOT the reset state, because
      // the crossover is programmed and muting bands 2 and 3 here would leave
      // band 1 lowpass-only and strip the highs from the output.
      gain = raw_to_wire(DRC_MIXER_UNITY);
    } else {
      gain = linear_db_to_f9_23(this->tas58xx_drc_[band].makeup_db);
    }

    if (!this->write_drc_coefficient_(DRC_ADDRESS[band].mixer_gain.page,
                                      DRC_ADDRESS[band].mixer_gain.sub_addr, gain)) {
      ok = false;
    }
  }

  if (!ok) {
    ESP_LOGW(TAG, "%s writing DRC band mixer gains", ERROR);
    return false;
  }

  ESP_LOGD(TAG, "Set DRC band mixer gains");
  return true;
}

void Tas58xxComponent::register_drc_number(DrcNumber *number) {
  if (number != nullptr) this->drc_numbers_.push_back(number);
}

void Tas58xxComponent::reset_drc_to_defaults() {
  if (this->drc_numbers_.empty()) {
    ESP_LOGW(TAG, "No DRC numbers configured - nothing to reset");
    return;
  }

  // Each control writes its own band through, so a full reset re-writes each
  // band once per parameter rather than once. Wasteful, but this is a manual
  // button and correctness beats saving a few dozen I2C transactions: routing
  // it through control() is what keeps the entity state, the saved preference
  // and the DSP in agreement.
  for (DrcNumber *number : this->drc_numbers_) {
    number->reset_to_default();
  }

  ESP_LOGI(TAG, "DRC reset to defaults: %.0fdB %.1f:1 attack %.1fms release %.0fms makeup %.0fdB",
           DRC_THRESHOLD_DEFAULT_DB, DRC_RATIO_DEFAULT, DRC_ATTACK_DEFAULT_MS,
           DRC_RELEASE_DEFAULT_MS, DRC_MAKEUP_DEFAULT_DB);
  ESP_LOGI(TAG, "  %d control(s) restored. Ratio 1:1 is unity, so the DRC is now inert.",
           static_cast<int>(this->drc_numbers_.size()));
}

void Tas58xxComponent::log_drc_registers() {
  ESP_LOGI(TAG, "DRC register readback (book 0x%02X):", TAS58XX_AUDIO_CTRL_BOOK);

  for (uint8_t i = 0; i < NUMBER_DRC_BANDS; i++) {
    const DrcBandAddresses& a = DRC_ADDRESS[i];

    // Same order as the struct so the log lines up with the memory map tables.
    const DrcAddress fields[] = {a.mixer_gain, a.energy, a.attack, a.decay,
                                 a.k0, a.k1, a.k2, a.t1, a.t2, a.off1, a.off2};
    static constexpr const char* NAMES[] = {"mix", "energy", "attack", "decay",
                                            "k0", "k1", "k2", "t1", "t2", "off1", "off2"};

    for (uint8_t f = 0; f < sizeof(fields) / sizeof(fields[0]); f++) {
      uint8_t raw[4] = {0};
      if (!this->book_and_page_read_(TAS58XX_AUDIO_CTRL_BOOK, fields[f].page, fields[f].sub_addr,
                                     raw, sizeof(raw))) {
        ESP_LOGW(TAG, "  %s %-6s p%02X/%02X read failed",
                 DRC_BAND_TEXT[i], NAMES[f], fields[f].page, fields[f].sub_addr);
        continue;
      }

      // The wire is MSB first, so reassemble in that order.
      const uint32_t value = (static_cast<uint32_t>(raw[0]) << 24) |
                             (static_cast<uint32_t>(raw[1]) << 16) |
                             (static_cast<uint32_t>(raw[2]) << 8) | raw[3];

      ESP_LOGI(TAG, "  %-4s %-6s p%02X/%02X = 0x%08X", DRC_BAND_TEXT[i], NAMES[f],
               fields[f].page, fields[f].sub_addr, static_cast<unsigned>(value));
    }
  }

  this->log_drc_crossover_scan_();
}

//// Crossover region sweep
//
// Written to locate the TAS5825M crossover addresses, which were reconstructed
// and once suspected wrong. It confirmed them instead - see the comment on
// DRC_XOVER_* in tas58xx_drc.h. Out of reset every crossover biquad is a
// pass-through, which has an unmistakable signature: one non-zero word followed
// by four zeros. Finding those signatures locates the blocks without writing
// anything.
//
// Still worth running on a new board or after changing the address tables. In
// three band mode it doubles as a write check: the eight addresses must have
// stopped being pass-throughs.

// Which flat slot a (page, sub_addr) maps to, or -1 if outside the swept range.
static int32_t drc_scan_slot_of(uint8_t page, uint8_t sub_addr) {
  if (page < DRC_SCAN_PAGE_FIRST || page >= DRC_SCAN_PAGE_FIRST + DRC_SCAN_PAGE_COUNT) return -1;
  if (sub_addr < DRC_SCAN_SLOT_FIRST || sub_addr > 0x7C) return -1;
  if ((sub_addr - DRC_SCAN_SLOT_FIRST) % 4 != 0) return -1;

  return (page - DRC_SCAN_PAGE_FIRST) * DRC_SCAN_SLOTS_PAGE +
         (sub_addr - DRC_SCAN_SLOT_FIRST) / 4;
}

static uint8_t drc_scan_page_of(uint16_t slot) {
  return DRC_SCAN_PAGE_FIRST + static_cast<uint8_t>(slot / DRC_SCAN_SLOTS_PAGE);
}

static uint8_t drc_scan_sub_of(uint16_t slot) {
  return DRC_SCAN_SLOT_FIRST + static_cast<uint8_t>((slot % DRC_SCAN_SLOTS_PAGE) * 4);
}

void Tas58xxComponent::log_drc_crossover_scan_() {
  uint8_t buffer[DRC_SCAN_TOTAL_BYTES];
  bool slot_valid[DRC_SCAN_TOTAL_SLOTS];

  for (uint16_t i = 0; i < DRC_SCAN_TOTAL_BYTES; i++) buffer[i] = 0;
  for (uint16_t i = 0; i < DRC_SCAN_TOTAL_SLOTS; i++) slot_valid[i] = false;

  ESP_LOGI(TAG, "DRC crossover scan, book 0x%02X pages 0x%02X-0x%02X:", TAS58XX_EQ_CTRL_BOOK,
           DRC_SCAN_PAGE_FIRST, DRC_SCAN_PAGE_FIRST + DRC_SCAN_PAGE_COUNT - 1);

  //// read
  for (uint8_t p = 0; p < DRC_SCAN_PAGE_COUNT; p++) {
    const uint8_t page = DRC_SCAN_PAGE_FIRST + p;

    for (uint8_t c = 0; c < DRC_SCAN_PAGE_BYTES / DRC_SCAN_CHUNK; c++) {
      const uint8_t sub_addr = DRC_SCAN_SLOT_FIRST + c * DRC_SCAN_CHUNK;
      const uint16_t offset = p * DRC_SCAN_PAGE_BYTES + c * DRC_SCAN_CHUNK;

      if (!this->book_and_page_read_(TAS58XX_EQ_CTRL_BOOK, page, sub_addr, buffer + offset,
                                     DRC_SCAN_CHUNK)) {
        ESP_LOGW(TAG, "  p%02X/%02X read failed - %d slots unknown", page, sub_addr,
                 DRC_SCAN_CHUNK / 4);
        continue;
      }
      for (uint8_t s = 0; s < DRC_SCAN_CHUNK / 4; s++) {
        slot_valid[p * DRC_SCAN_SLOTS_PAGE + c * (DRC_SCAN_CHUNK / 4) + s] = true;
      }
    }
  }

  // The wire is MSB first.
  auto word_at = [&buffer](uint16_t slot) -> uint32_t {
    const uint8_t* b = buffer + static_cast<uint16_t>(slot) * 4;
    return (static_cast<uint32_t>(b[0]) << 24) | (static_cast<uint32_t>(b[1]) << 16) |
           (static_cast<uint32_t>(b[2]) << 8) | b[3];
  };

  //// raw dump, six words per line
  static constexpr uint8_t PER_LINE = 6;
  for (uint16_t slot = 0; slot < DRC_SCAN_TOTAL_SLOTS; slot += PER_LINE) {
    // 6 words * 9 chars + terminator fits with room to spare, but clamp anyway
    // so a truncating snprintf can never underflow the remaining size.
    char line[80];
    size_t used = 0;
    line[0] = '\0';

    for (uint8_t i = 0; i < PER_LINE && (slot + i) < DRC_SCAN_TOTAL_SLOTS; i++) {
      if (used + 1 >= sizeof(line)) break;

      const int written = slot_valid[slot + i]
                              ? snprintf(line + used, sizeof(line) - used, " %08X",
                                         static_cast<unsigned>(word_at(slot + i)))
                              : snprintf(line + used, sizeof(line) - used, " --------");
      if (written <= 0) break;

      used += static_cast<size_t>(written);
      if (used >= sizeof(line)) {
        used = sizeof(line) - 1;
        break;
      }
    }

    ESP_LOGI(TAG, "  p%02X/%02X%s", drc_scan_page_of(slot), drc_scan_sub_of(slot), line);
  }

  //// pass-through signatures
  ESP_LOGI(TAG, "  pass-through candidates (B0 non-zero, B1/B2/A1/A2 zero):");
  uint8_t found = 0;

  for (uint16_t slot = 0; slot + 4 < DRC_SCAN_TOTAL_SLOTS; slot++) {
    bool all_valid = true;
    for (uint8_t i = 0; i < 5; i++) {
      if (!slot_valid[slot + i]) all_valid = false;
    }
    if (!all_valid) continue;

    const uint32_t b0 = word_at(slot);
    if (b0 == 0) continue;
    if (word_at(slot + 1) || word_at(slot + 2) || word_at(slot + 3) || word_at(slot + 4)) continue;

    const char* format = "unrecognised unity";
    if (b0 == DRC_UNITY_F5_27) format = "unity in 5.27";
    else if (b0 == DRC_UNITY_F2_30) format = "unity in 2.30";
    else if (b0 == DRC_UNITY_F1_31) format = "unity in 1.31";

    ESP_LOGI(TAG, "    p%02X/%02X B0=0x%08X (%s)", drc_scan_page_of(slot), drc_scan_sub_of(slot),
             static_cast<unsigned>(b0), format);
    found++;
  }

  // What counts as healthy inverts with the mode: at reset the crossover IS the
  // pass-throughs, once programmed it must not be.
  const bool three_band = this->drc_is_three_band();

  if (found == 0) {
    if (three_band) {
      ESP_LOGI(TAG, "    none - correct, three band mode has programmed the crossover");
    } else {
      ESP_LOGW(TAG, "    none - crossover may be outside the swept pages, or already programmed");
    }
  } else if (three_band) {
    ESP_LOGI(TAG, "    %d candidate(s), all from unrelated banks - the crossover is programmed",
             found);
  } else {
    ESP_LOGI(TAG, "    %d candidate(s). Eight CONTIGUOUS ones are the crossover at reset; any "
                  "others belong to unrelated banks.", found);
  }

  //// verdict on the configured addresses
  ESP_LOGI(TAG, "  configured DRC_XOVER_* addresses:");

  struct ScanGroup {
    const char* label;
    const DrcBiquadAddress* addresses;
    uint8_t count;
  };
  const ScanGroup groups[] = {
      {"low ", DRC_XOVER_LOW, DRC_XOVER_LOW_SECTIONS},
      {"mid ", DRC_XOVER_MID, DRC_XOVER_MID_SECTIONS},
      {"high", DRC_XOVER_HIGH, DRC_XOVER_HIGH_SECTIONS},
  };

  for (const ScanGroup& group : groups) {
    for (uint8_t i = 0; i < group.count; i++) {
      const DrcBiquadAddress& address = group.addresses[i];
      const int32_t slot = drc_scan_slot_of(address.page, address.sub_addr);

      if (slot < 0) {
        ESP_LOGW(TAG, "    %s BQ%d p%02X/%02X outside swept range", group.label, i + 1,
                 address.page, address.sub_addr);
        continue;
      }

      // A biquad needs five slots and may straddle a page, which is contiguous
      // in this buffer - but it must not run off the end of the swept region.
      if (static_cast<uint16_t>(slot) + 4 >= DRC_SCAN_TOTAL_SLOTS) {
        ESP_LOGW(TAG, "    %s BQ%d p%02X/%02X runs past the swept range", group.label, i + 1,
                 address.page, address.sub_addr);
        continue;
      }

      const uint32_t b0 = word_at(slot);
      const bool tail_zero = !word_at(slot + 1) && !word_at(slot + 2) && !word_at(slot + 3) &&
                             !word_at(slot + 4);
      const bool passthrough = (b0 != 0) && tail_zero;

      // In three band mode a surviving pass-through means the write never
      // landed; at reset the opposite is the surprise.
      const bool unexpected = three_band ? passthrough : !passthrough;
      const char* verdict;
      if (three_band) {
        verdict = passthrough ? "still pass-through - CROSSOVER WRITE DID NOT LAND"
                              : "= programmed";
      } else {
        verdict = passthrough ? "= pass-through, as expected at reset"
                              : "not pass-through - unexpected in one band mode";
      }

      if (unexpected) {
        ESP_LOGW(TAG, "    %s BQ%d p%02X/%02X B0=0x%08X %s", group.label, i + 1, address.page,
                 address.sub_addr, static_cast<unsigned>(b0), verdict);
      } else {
        ESP_LOGI(TAG, "    %s BQ%d p%02X/%02X B0=0x%08X %s", group.label, i + 1, address.page,
                 address.sub_addr, static_cast<unsigned>(b0), verdict);
      }
    }
  }
}

}  // namespace esphome::tas58xx
