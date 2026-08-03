#pragma once

#include "esphome/core/hal.h"

namespace esphome::tas58xx_helpers {

int32_t gain_to_f9_23_(int8_t gain);

//// DRC coefficient encoders.
//
// Every encoder returns the value already byte-swapped, so writing it as raw
// memory on the little-endian ESP32 puts it on the wire MSB first, matching
// gain_to_f9_23_ and the rest of this component.
//
// These round rather than truncate. Truncation misses TI's own documented
// constants by an LSB (e.g. -3 dB linear becomes 0x005A9DF7 instead of
// 0x005A9DF8), which breaks readback verification.

// A dimensionless signed quantity in 9.23 - the DRC K region slopes.
int32_t slope_to_f9_23(float slope);

// A plain dB quantity in 9.23 - DRC gain offsets. NOT a linear gain.
int32_t db_to_f9_23(float db);

// A quantity converted into the detector's own log units, in 9.23 - the DRC
// thresholds, and only those. The detector tracks log2 of the mean square, so a
// threshold is divided by DRC_THRESHOLD_DB_PER_UNIT before it gets here; see
// tas58xx_drc.h. The caller does the division, so this is deliberately just the
// fixed-point step, identical to db_to_f9_23 but named for what it carries.
int32_t log_units_to_f9_23(float units);

// A linear gain expressed in dB, encoded in 9.23 - band mixer gains.
int32_t linear_db_to_f9_23(float db);

// A raw 9.23/1.31/5.27 constant that is already in DSP form, byte-swapped for
// the wire. Used for the documented reset values.
int32_t raw_to_wire(uint32_t raw);

// A DRC/AGL time constant in 1.31.
//
// TI's own defaults are generated with the simple reciprocal alpha = 1/(fs*tau),
// not the exponential form: 100 ms @ 96 kHz gives 0x000369D0 exactly, matching
// the documented AGL attack default, where 1 - exp(-1/(fs*tau)) is off by 11.
int32_t time_constant_to_f1_31(float tau_ms, uint32_t sample_rate);

// n:1 compression ratio to a DRC K slope, per SLOA148 §5.2: k = 1/n - 1.
// ratio 1 -> 0 (no compression), 2 -> -0.5, 4 -> -0.75, ->inf -> -1 (limiter).
// Clamped to k > -1; k <= -1 inverts the transfer curve.
float ratio_to_slope(float ratio);

//// Crossover biquad design

// Normalised, PPC3-convention biquad coefficients ready to encode.
//   B0 = b0/a0, B1 = b1/(2*a0), B2 = b2/a0, A1 = -a1/(2*a0), A2 = -a2/a0
// Note the sign inversion on A1/A2 and the factor of two on B1/A1
// (SLOA263A Table 3 / SLAA786A Table 6).
// double, not float: a 5.27 coefficient near unity needs more than float's
// 24-bit mantissa to land on the right LSB, and float storage was costing 3-8
// LSB against the reference implementation. This is design-time math that runs
// only when the crossover changes, so the software-double cost is irrelevant.
struct BiquadCoefficients {
  double b0, b1, b2, a1, a2;
};

// One 2nd-order Butterworth (Q = 1/sqrt(2)) section. Cascade two identical
// sections for a 4th-order Linkwitz-Riley crossover slope.
BiquadCoefficients design_butterworth_lowpass(float corner_hz, uint32_t sample_rate);
BiquadCoefficients design_butterworth_highpass(float corner_hz, uint32_t sample_rate);

// Pass-through: B0 = unity, everything else zero. Used to restore the crossover.
BiquadCoefficients biquad_passthrough();

// Encode a biquad into the 20 wire bytes for one DSP biquad slot.
// mixed_format selects the TAS5805M crossover layout (B0,B2,A2 in 1.31 and
// B1,A1 in 2.30) instead of 5.27 throughout, which is what the TAS5825M uses.
void encode_biquad(const BiquadCoefficients& bq, bool mixed_format, uint8_t* out);

}  // namespace esphome::tas58xx_helpers
