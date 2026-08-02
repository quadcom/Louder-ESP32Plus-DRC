// Compiles the real tas58xx_helpers.cpp against stub esphome headers and prints
// the encoder outputs, so they can be diffed against tools/drc_math_check.py.
//
// The values printed are the on-the-wire byte-swapped words, converted back to
// DSP order here so they read the same way the TI tables do.

#include <cstdio>
#include <cstdint>
#include "esphome/core/helpers.h"
#include "tas58xx_helpers.h"

using namespace esphome::tas58xx_helpers;

static uint32_t dsp(int32_t wire) {
  // undo the byteswap the encoders apply for the wire
  return static_cast<uint32_t>(esphome::byteswap(wire));
}

int main() {
  printf("T1_-50            0x%08X\n", dsp(db_to_f9_23(-50.0f)));
  printf("T2_-3             0x%08X\n", dsp(db_to_f9_23(-3.0f)));
  printf("off_0             0x%08X\n", dsp(db_to_f9_23(0.0f)));
  printf("mixer_unity       0x%08X\n", dsp(linear_db_to_f9_23(0.0f)));
  printf("slope_ratio1      0x%08X\n", dsp(slope_to_f9_23(ratio_to_slope(1.0f))));
  printf("slope_ratio2      0x%08X\n", dsp(slope_to_f9_23(ratio_to_slope(2.0f))));
  printf("slope_ratio4      0x%08X\n", dsp(slope_to_f9_23(ratio_to_slope(4.0f))));
  printf("slope_ratio10     0x%08X\n", dsp(slope_to_f9_23(ratio_to_slope(10.0f))));
  printf("agl_attack_100ms  0x%08X\n", dsp(time_constant_to_f1_31(100.0f, 96000)));
  printf("agl_release_1s    0x%08X\n", dsp(time_constant_to_f1_31(1000.0f, 96000)));
  printf("tc_instant        0x%08X\n", dsp(time_constant_to_f1_31(0.0f, 96000)));

  uint8_t bq[20];

  encode_biquad(biquad_passthrough(), false, bq);
  printf("pass5827_B0       0x%02X%02X%02X%02X\n", bq[0], bq[1], bq[2], bq[3]);
  printf("pass5827_B1       0x%02X%02X%02X%02X\n", bq[4], bq[5], bq[6], bq[7]);

  encode_biquad(biquad_passthrough(), true, bq);
  printf("pass131_B0        0x%02X%02X%02X%02X\n", bq[0], bq[1], bq[2], bq[3]);
  printf("pass131_A2        0x%02X%02X%02X%02X\n", bq[16], bq[17], bq[18], bq[19]);

  encode_biquad(design_butterworth_lowpass(300.0f, 96000), false, bq);
  printf("lpf300_5827      ");
  for (int i = 0; i < 20; i += 4)
    printf(" 0x%02X%02X%02X%02X", bq[i], bq[i + 1], bq[i + 2], bq[i + 3]);
  printf("\n");

  encode_biquad(design_butterworth_highpass(3000.0f, 96000), false, bq);
  printf("hpf3000_5827     ");
  for (int i = 0; i < 20; i += 4)
    printf(" 0x%02X%02X%02X%02X", bq[i], bq[i + 1], bq[i + 2], bq[i + 3]);
  printf("\n");

  return 0;
}
