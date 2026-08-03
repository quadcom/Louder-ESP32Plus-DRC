#include "tas58xx_helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include <cmath>
#include <cstdint>
#include <cstring>

namespace esphome::tas58xx_helpers {

#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERBOSE
  static constexpr const char* HELPER_TAG = "tas58xx.helper";
#endif

  int32_t gain_to_f9_23_(int8_t gain) {
    static constexpr float TAS58XX_LINEAR_GAIN_MAX = 255.999999f;
    static constexpr float TAS58XX_LINEAR_GAIN_MIN = -256.0f;

    float linear = powf(10.0f, ((float)gain) / 20.0f);
    if (linear > TAS58XX_LINEAR_GAIN_MAX) linear = TAS58XX_LINEAR_GAIN_MAX;
    if (linear < TAS58XX_LINEAR_GAIN_MIN) linear = TAS58XX_LINEAR_GAIN_MIN;

    int32_t fixed_9_23 = static_cast<int32_t>(linear * (1 << 23));
    int32_t little_endian = byteswap(fixed_9_23);

    ESP_LOGV(HELPER_TAG, "Gain:%ddb  Fixed 9.23: 0x%08X  Little Endian: 0x%08X", gain, fixed_9_23, little_endian);
    return little_endian;
  }

//// DRC coefficient encoders

namespace {

// Round-to-nearest fixed point with saturation. frac_bits selects the format:
// 23 -> 9.23, 27 -> 5.27, 30 -> 2.30, 31 -> 1.31.
int32_t to_fixed(double value, int frac_bits) {
  // Clamp in the double domain and return the integer bounds directly. Comparing
  // against a float 2147483647.0f would not work: that value is not
  // representable as a float and rounds up to 2^31, so the cast back to int32_t
  // would overflow. This matters in practice - a 1.31 pass-through B0 of 1.0
  // scales to exactly 2^31 and must come back as 0x7FFFFFFF.
  static constexpr double LIMIT_HIGH = 2147483648.0;   // 2^31, first value too big
  static constexpr double LIMIT_LOW  = -2147483648.0;  // -2^31, INT32_MIN exactly

  const double scaled = round(value * static_cast<double>(1u << frac_bits));

  if (scaled >= LIMIT_HIGH) return INT32_MAX;
  if (scaled <= LIMIT_LOW)  return INT32_MIN;

  return static_cast<int32_t>(scaled);
}

}  // namespace

int32_t slope_to_f9_23(float slope) {
  int32_t fixed = to_fixed(slope, 23);
  ESP_LOGV(HELPER_TAG, "Slope:%.6f  Fixed 9.23: 0x%08X", slope, fixed);
  return byteswap(fixed);
}

int32_t db_to_f9_23(float db) {
  int32_t fixed = to_fixed(db, 23);
  ESP_LOGV(HELPER_TAG, "dB:%.3f  Fixed 9.23: 0x%08X", db, fixed);
  return byteswap(fixed);
}

int32_t log_units_to_f9_23(float units) {
  int32_t fixed = to_fixed(units, 23);
  ESP_LOGV(HELPER_TAG, "Log units:%.6f  Fixed 9.23: 0x%08X", units, fixed);
  return byteswap(fixed);
}

int32_t linear_db_to_f9_23(float db) {
  float linear = powf(10.0f, db / 20.0f);
  int32_t fixed = to_fixed(linear, 23);
  ESP_LOGV(HELPER_TAG, "Gain:%.3fdB linear:%.6f  Fixed 9.23: 0x%08X", db, linear, fixed);
  return byteswap(fixed);
}

int32_t raw_to_wire(uint32_t raw) {
  return byteswap(static_cast<int32_t>(raw));
}

int32_t time_constant_to_f1_31(float tau_ms, uint32_t sample_rate) {
  static constexpr int32_t ALPHA_MAX = 0x7FFFFFFF;  // alpha 1.0, instantaneous

  if (tau_ms <= 0.0f || sample_rate == 0) return byteswap(ALPHA_MAX);

  const float samples = (static_cast<float>(sample_rate) * tau_ms) / 1000.0f;
  if (samples <= 1.0f) return byteswap(ALPHA_MAX);

  int32_t fixed = to_fixed(1.0f / samples, 31);
  if (fixed < 1) fixed = 1;                  // never a dead filter
  if (fixed > ALPHA_MAX) fixed = ALPHA_MAX;

  ESP_LOGV(HELPER_TAG, "Tau:%.3fms fs:%u samples:%.1f  Fixed 1.31: 0x%08X",
           tau_ms, sample_rate, samples, fixed);
  return byteswap(fixed);
}

float ratio_to_slope(float ratio) {
  static constexpr float SLOPE_FLOOR = -0.999f;  // k must stay > -1

  if (ratio <= 1.0f) return 0.0f;                // 1:1, no gain change

  float slope = (1.0f / ratio) - 1.0f;
  if (slope < SLOPE_FLOOR) slope = SLOPE_FLOOR;
  return slope;
}

//// Crossover biquad design

namespace {

static constexpr double BUTTERWORTH_Q = 0.7071067811865476;  // 1/sqrt(2)

// M_PI is POSIX, not standard C++. It happens to be defined under ESP-IDF but
// relying on that breaks any other toolchain, so spell it out.
static constexpr double TWO_PI = 6.283185307179586;

// Shared denominator terms for a 2nd-order Butterworth section.
struct SectionTerms {
  double cos_w0, a0, a1, a2;
};

SectionTerms section_terms(float corner_hz, uint32_t sample_rate) {
  const double w0 = TWO_PI * static_cast<double>(corner_hz) / static_cast<double>(sample_rate);
  const double cos_w0 = cos(w0);
  const double alpha = sin(w0) / (2.0 * BUTTERWORTH_Q);

  return { cos_w0, 1.0 + alpha, -2.0 * cos_w0, 1.0 - alpha };
}

// Apply the PPC3 normalisation from SLOA263A Table 3 / SLAA786A Table 6.
// Note the sign inversion on A1/A2 and the factor of two on B1/A1.
BiquadCoefficients normalise(double b0, double b1, double b2, const SectionTerms& t) {
  return { b0 / t.a0,
           b1 / (2.0 * t.a0),
           b2 / t.a0,
           -t.a1 / (2.0 * t.a0),
           -t.a2 / t.a0 };
}

}  // namespace

BiquadCoefficients design_butterworth_lowpass(float corner_hz, uint32_t sample_rate) {
  const SectionTerms t = section_terms(corner_hz, sample_rate);
  const double k = 1.0 - t.cos_w0;

  return normalise(k / 2.0, k, k / 2.0, t);
}

BiquadCoefficients design_butterworth_highpass(float corner_hz, uint32_t sample_rate) {
  const SectionTerms t = section_terms(corner_hz, sample_rate);
  const double k = 1.0 + t.cos_w0;

  return normalise(k / 2.0, -k, k / 2.0, t);
}

BiquadCoefficients biquad_passthrough() {
  return { 1.0, 0.0, 0.0, 0.0, 0.0 };
}

void encode_biquad(const BiquadCoefficients& bq, bool mixed_format, uint8_t* out) {
  // TAS5805M crossover biquads mix formats within one biquad: B0/B2/A2 are 1.31
  // and B1/A1 are 2.30. TAS5825M uses 5.27 throughout.
  // 1.31 saturates just below 1.0, so a pass-through B0 of exactly 1.0 encodes
  // as 0x7FFFFFFF - which is what TI documents as the default.
  const int full_scale_bits = mixed_format ? 31 : 27;
  const int halved_bits     = mixed_format ? 30 : 27;

  const int32_t encoded[5] = {
    byteswap(to_fixed(bq.b0, full_scale_bits)),
    byteswap(to_fixed(bq.b1, halved_bits)),
    byteswap(to_fixed(bq.b2, full_scale_bits)),
    byteswap(to_fixed(bq.a1, halved_bits)),
    byteswap(to_fixed(bq.a2, full_scale_bits)),
  };

  memcpy(out, encoded, sizeof(encoded));
}

}  // namespace esphome::tas58xx_helpers
