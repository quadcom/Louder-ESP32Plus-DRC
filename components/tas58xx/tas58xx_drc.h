#pragma once

// 3-band DRC (Dynamic Range Control) address maps, limits and coefficient math.
//
// Address sources, per variant:
//   TAS5825M  Process Flow 1 (Base/Pro, 96 kHz, 2.0) - SLAA786A Table 9, pp.65-72
//   TAS5805M  2.0 96 kHz mode                        - SLOA263A Table 5, pp.20-21 and Table 6, p.25
//
// See docs/tas58xx-drc-reference.md for the derivation, the documentation errata
// and which formulas from SLOA148 do NOT apply to these parts.

namespace esphome::tas58xx {

enum DrcBand : uint8_t {
  DRC_LOW  = 0,
  DRC_MID  = 1,
  DRC_HIGH = 2,
};

static constexpr uint8_t NUMBER_DRC_BANDS = 3;

static constexpr const char* DRC_BAND_TEXT[NUMBER_DRC_BANDS] = {"Low", "Mid", "High"};

// How many DRC bands are actually summed into the output.
// ONE_BAND leaves the crossover biquads at their pass-through defaults and bands
// 2/3 muted, which is a full-range single-band compressor - the safe default.
// THREE_BAND requires the crossover to be programmed, see the trap in §4 of the
// reference doc: unmuting bands 2/3 with pass-through crossovers sums three
// copies of the full-range signal.
enum DrcBandMode : uint8_t {
  DRC_ONE_BAND    = 0,
  DRC_THREE_BAND  = 1,
};

// One 32-bit DSP coefficient location within a book.
struct DrcAddress {
  uint8_t page;
  uint8_t sub_addr;
}__attribute__((packed));

// The eleven per-band coefficient locations. Ten DRC parameters plus the band's
// post-DRC mixer gain.
struct DrcBandAddresses {
  DrcAddress mixer_gain;   // 9.23  post-DRC, pre-sum band gain
  DrcAddress energy;       // 1.31  RMS estimator time constant
  DrcAddress attack;       // 1.31
  DrcAddress decay;        // 1.31  (release)
  DrcAddress k0;           // 9.23  region 1 slope
  DrcAddress k1;           // 9.23  region 2 slope
  DrcAddress k2;           // 9.23  region 3 slope
  DrcAddress t1;           // 9.23  threshold 1, plain dB
  DrcAddress t2;           // 9.23  threshold 2, plain dB
  DrcAddress off1;         // 9.23  gain curve value at T1, plain dB
  DrcAddress off2;         // 9.23  gain curve value at T2, plain dB
}__attribute__((packed));

// Biquad: five consecutive 32-bit coefficients B0,B1,B2,A1,A2 starting here.
// A page wrap between coefficients is handled by book_and_page_write_.
struct DrcBiquadAddress {
  uint8_t page;
  uint8_t sub_addr;
}__attribute__((packed));

// Crossover biquad slots, in the order the DSP cascades them.
// low/high are 2 sections each (4th order Linkwitz-Riley); mid is 4 sections
// (highpass at the low corner, then lowpass at the high corner).
static constexpr uint8_t DRC_XOVER_LOW_SECTIONS  = 2;
static constexpr uint8_t DRC_XOVER_HIGH_SECTIONS = 2;
static constexpr uint8_t DRC_XOVER_MID_SECTIONS  = 4;

#ifdef USE_TAS5805M_DAC
//// TAS5805M - SLOA263A Table 5 (book 0x8C) and Table 6 (book 0xAA)

static constexpr DrcBandAddresses DRC_ADDRESS[NUMBER_DRC_BANDS] = {
  // band 1 / low
  { {0x2E, 0x08},                                     // mixer gain
    {0x2B, 0x34}, {0x2B, 0x38}, {0x2B, 0x3C},         // energy, attack, decay
    {0x2B, 0x40}, {0x2B, 0x44}, {0x2B, 0x48},         // k0, k1, k2
    {0x2B, 0x4C}, {0x2B, 0x50},                       // t1, t2
    {0x2B, 0x54}, {0x2B, 0x58} },                     // off1, off2
  // band 2 / mid
  { {0x2E, 0x0C},
    {0x2D, 0x30}, {0x2D, 0x34}, {0x2D, 0x38},
    {0x2D, 0x3C}, {0x2D, 0x40}, {0x2D, 0x44},
    {0x2D, 0x48}, {0x2D, 0x4C},
    {0x2D, 0x50}, {0x2D, 0x54} },
  // band 3 / high
  // NOTE 0x2D/0x6C is printed 'k1_3' in SLOA263A, duplicating the region 2
  // name. Its DESCRIPTION column reads "DRC3 Region 3 Slope", so it is k2_3.
  { {0x2E, 0x10},
    {0x2D, 0x58}, {0x2D, 0x5C}, {0x2D, 0x60},
    {0x2D, 0x64}, {0x2D, 0x68}, {0x2D, 0x6C},
    {0x2D, 0x70}, {0x2D, 0x74},
    {0x2D, 0x78}, {0x2D, 0x7C} },
};

// Memory order here is low, mid, high.
static constexpr DrcBiquadAddress DRC_XOVER_LOW[DRC_XOVER_LOW_SECTIONS]   = { {0x2A, 0x34}, {0x2A, 0x48} };
static constexpr DrcBiquadAddress DRC_XOVER_MID[DRC_XOVER_MID_SECTIONS]   = { {0x2A, 0x5C}, {0x2A, 0x70},
                                                                             {0x2E, 0x40}, {0x2E, 0x54} };
static constexpr DrcBiquadAddress DRC_XOVER_HIGH[DRC_XOVER_HIGH_SECTIONS] = { {0x2B, 0x0C}, {0x2B, 0x20} };

// TAS5805M crossover biquads use MIXED formats within a single biquad:
// B0, B2, A2 are 1.31 while B1 and A1 are 2.30 (the halved coefficients).
static constexpr bool DRC_XOVER_MIXED_FORMAT = true;

#else
//// TAS5825M - SLAA786A Table 9

static constexpr DrcBandAddresses DRC_ADDRESS[NUMBER_DRC_BANDS] = {
  // band 1 / low - straddles the page 0x06 -> 0x07 boundary
  { {0x06, 0x58},
    {0x06, 0x64}, {0x06, 0x68}, {0x06, 0x6C},
    {0x06, 0x70}, {0x06, 0x74}, {0x06, 0x78},
    {0x06, 0x7C}, {0x07, 0x08},
    {0x07, 0x0C}, {0x07, 0x10} },
  // band 2 / mid
  { {0x06, 0x5C},
    {0x07, 0x14}, {0x07, 0x18}, {0x07, 0x1C},
    {0x07, 0x20}, {0x07, 0x24}, {0x07, 0x28},
    {0x07, 0x2C}, {0x07, 0x30},
    {0x07, 0x34}, {0x07, 0x38} },
  // band 3 / high
  // NOTE 0x07/0x50 is printed 'k1_3' in SLAA786A - the same errata as SLOA263A.
  // Its DESCRIPTION column reads "DRC3 Region 3 Slope", so it is k2_3.
  { {0x06, 0x60},
    {0x07, 0x3C}, {0x07, 0x40}, {0x07, 0x44},
    {0x07, 0x48}, {0x07, 0x4C}, {0x07, 0x50},
    {0x07, 0x54}, {0x07, 0x58},
    {0x07, 0x5C}, {0x07, 0x60} },
};

// Memory order here is low, HIGH, mid. Addresses reconstructed from the offset
// sequence because SLAA786A's page column is mis-transcribed in this section;
// see the reference doc. low BQ1 and mid BQ3 each wrap a page mid-biquad, which
// book_and_page_write_ handles.
static constexpr DrcBiquadAddress DRC_XOVER_LOW[DRC_XOVER_LOW_SECTIONS]   = { {0x07, 0x78}, {0x08, 0x14} };
static constexpr DrcBiquadAddress DRC_XOVER_HIGH[DRC_XOVER_HIGH_SECTIONS] = { {0x08, 0x28}, {0x08, 0x3C} };
static constexpr DrcBiquadAddress DRC_XOVER_MID[DRC_XOVER_MID_SECTIONS]   = { {0x08, 0x50}, {0x08, 0x64},
                                                                             {0x08, 0x78}, {0x09, 0x14} };

// TAS5825M crossover biquads are 5.27 throughout, like its EQ biquads.
static constexpr bool DRC_XOVER_MIXED_FORMAT = false;

#endif

//// Documented reset values (identical on both parts)

static constexpr uint32_t DRC_DEFAULT_TIME_CONSTANT = 0x7FFFFFFF; // alpha 1.0 -> instantaneous
static constexpr uint32_t DRC_DEFAULT_SLOPE         = 0x00000000; // no gain change
static constexpr uint32_t DRC_DEFAULT_OFFSET        = 0x00000000;
static constexpr uint32_t DRC_DEFAULT_T1            = 0xE7000000; // -50.0 dB
static constexpr uint32_t DRC_DEFAULT_T2            = 0xFE800000; //  -3.0 dB
static constexpr uint32_t DRC_MIXER_UNITY           = 0x00800000; // 9.23 1.0
static constexpr uint32_t DRC_MIXER_MUTE            = 0x00000000;

//// Control ranges exposed to YAML / Home Assistant

static constexpr float DRC_THRESHOLD_MIN_DB = -60.0f;
// T1 must stay strictly below T2, and zero/positive thresholds are explicitly
// disallowed by SLOA148, so the knee cannot reach 0 dB.
static constexpr float DRC_THRESHOLD_MAX_DB =  -2.0f;

// Upper threshold. Fixed just below full scale: non-zero (required) and above
// any permitted knee, so regions 2 and 3 form one continuous slope.
static constexpr float DRC_T2_DB            =  -1.0f;

static constexpr float DRC_RATIO_MIN        =   1.0f;   // 1:1 = no compression
static constexpr float DRC_RATIO_MAX        =  20.0f;   // approaching a limiter

static constexpr float DRC_ATTACK_MIN_MS    =   0.1f;
static constexpr float DRC_ATTACK_MAX_MS    = 500.0f;

static constexpr float DRC_RELEASE_MIN_MS   =   1.0f;
static constexpr float DRC_RELEASE_MAX_MS   = 3000.0f;

static constexpr float DRC_MAKEUP_MIN_DB    =   0.0f;
static constexpr float DRC_MAKEUP_MAX_DB    =  24.0f;

// Defaults. Ratio 1:1 makes the DRC inert whatever the other settings are, so a
// fresh install is a no-op until the user asks for compression. Keep these in
// step with number/__init__.py.
static constexpr float DRC_THRESHOLD_DEFAULT_DB = -20.0f;
static constexpr float DRC_RATIO_DEFAULT        =   1.0f;
static constexpr float DRC_ATTACK_DEFAULT_MS    =  10.0f;
static constexpr float DRC_RELEASE_DEFAULT_MS   = 200.0f;
static constexpr float DRC_MAKEUP_DEFAULT_DB    =   0.0f;

static constexpr float DRC_ENERGY_MIN_MS    =   0.1f;
static constexpr float DRC_ENERGY_MAX_MS    =  50.0f;
static constexpr float DRC_ENERGY_DEFAULT_MS =  2.0f;

static constexpr float DRC_CROSSOVER_MIN_HZ =  40.0f;
static constexpr float DRC_CROSSOVER_MAX_HZ = 12000.0f;

// PF1 upconverts via SRC to 88.2 or 96 kHz. FS_MON cannot distinguish the two
// (44.1 and 48 share a code), but the resulting error on a time constant is at
// most 96/88.2 = 8.8%, which is inaudible for attack/release. Default to 96 kHz
// and allow a YAML override.
static constexpr uint32_t DRC_DEFAULT_DSP_RATE = 96000;

//// Per-band runtime state

struct DrcBandSettings {
  float threshold_db{DRC_THRESHOLD_DEFAULT_DB};
  float ratio{DRC_RATIO_DEFAULT};
  float attack_ms{DRC_ATTACK_DEFAULT_MS};
  float release_ms{DRC_RELEASE_DEFAULT_MS};
  float makeup_db{DRC_MAKEUP_DEFAULT_DB};
};

// Which per-band control a DrcNumber entity drives.
enum DrcParameter : uint8_t {
  DRC_PARAM_THRESHOLD = 0,
  DRC_PARAM_RATIO,
  DRC_PARAM_ATTACK,
  DRC_PARAM_RELEASE,
  DRC_PARAM_MAKEUP,
};

static constexpr uint8_t NUMBER_DRC_PARAMS = 5;

static constexpr const char* DRC_PARAM_TEXT[NUMBER_DRC_PARAMS] = {
  "Threshold", "Ratio", "Attack", "Release", "Makeup",
};

}  // namespace esphome::tas58xx
