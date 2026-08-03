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

// How the offsets are derived
// ---------------------------
// The part computes gain = k*x + O, with the thresholds only selecting which
// region's k and O apply - it does NOT subtract the threshold itself. So an
// offset is the line's y-intercept, and O = -k*T1 is what places the knee at T1:
//
//   region 1  x <  T1   gain = k0*x + O1,  k0 = 0, O1 = 0     -> transparent
//   region 2  x >= T1   gain = k *x + O2,  O2 = -k*T1         -> 0 dB at T1
//   region 3  x >= T2   same slope, so the line continues through T2
//
// where x is the detector level in dBFS, T1 the knee, T2 pinned at -1 dBFS, and
// k = 1/ratio - 1, so k is negative for compression. See §3 of
// docs/tas58xx-drc-reference.md for the measurements this rests on.
//
// The register numbering is the trap: **off1 belongs to region 1, not region 2**.
// Measured 2026-08-03 - with both offsets at -k*T1 = -10 dB, the four plateaus
// below the knee came back a flat 9.2 dB down where they must be untouched, and
// the knee itself showed a 10.5 dB discontinuity where 3.5 dB was due. Both
// numbers are that -10 dB landing where nothing should be, and region 1's slope
// is genuinely zero, so an offset there is pure constant attenuation on exactly
// the quiet material a compressor exists to leave alone.
//
// This was briefly a runtime-selectable choice of three conventions, because the
// offsets could not be settled from the datasheets and each guess otherwise cost
// a flash cycle. The other two are gone: both leave an offset at zero, which
// under the equation above unanchors its region into a boost of -k*x, and both
// tripped the amp's protection on the bench (2026-08-03). Nothing diagnostic was
// lost - it was that boost, at +18.6 dB where +9.0 was predicted, that revealed
// the doubled slope and with it the log domain.

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

// Diagnostic sweep range - covers every page the crossover tables above name.
static constexpr uint8_t DRC_SCAN_PAGE_FIRST = 0x2A;
static constexpr uint8_t DRC_SCAN_PAGE_COUNT = 5;   // 0x2A .. 0x2E

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

// CONFIRMED BY READBACK - Louder ESP32-S3 Plus, TAS5825M, 2026-08-02.
// Only reached when drc_bands: 3.
//
// Memory order here is low, HIGH, mid. Addresses were reconstructed from the
// offset sequence because SLAA786A's page column is mis-transcribed in this
// section; see the reference doc. They were previously flagged as probably
// wrong. The DRC Register Dump sweep disproved that:
//
//   - All eight read back as unity pass-through biquads out of reset:
//     B0 = 0x08000000 (unity in 5.27), B1/B2/A1/A2 = 0.
//   - They form ONE contiguous bank, p07/78 + n*0x14 for n = 0..7, with nothing
//     interleaved. Eight biquads is exactly what a 3-band LR4 split needs:
//     2 low + 4 mid + 2 high.
//   - The bank is bounded at both ends. Before it, p07/64..0x74 holds
//     non-biquad data (00800000 00800000 3FFFFFFF 3FFFFFFF 3FFFFFFF). After it,
//     p09/14 + 0x14 lands exactly on a metadata block at p09/28
//     (02DEAD00 74013901 0020C49B). The bank cannot extend in either direction.
//
// The earlier objection was that these are not on the EQ grid, and it was right
// about the fact but wrong about the conclusion. EQ biquads sit on 0x08 + n*0x14,
// six per page, the sixth ending exactly at 0x7F and never crossing a page. The
// crossover sits on 0x14 + n*0x14, so {0x07,0x78} and {0x08,0x78} each straddle a
// page. The readback settles which grid is real: p08/08, p08/1C and p08/30 - the
// EQ-grid slots - all read 0x00000000. That is a null biquad, not a pass-through.
// So the two banks genuinely use different alignments on this part, and
// "tas5825m has biquads aligned to page boundaries" in book_and_page_write_ holds
// for the EQ only.
//
// Straddling is safe here: book_and_page_write_ splits a 20-byte write at the
// page boundary and resumes at the next page's 0x08 - the path it already takes
// for the TAS5805M. See tas58xx.cpp:955.
//
// Books 0x8C and 0xAA alias on this part, or book select is a no-op for these
// DSP pages: the sweep reads book 0xAA p07/08..0x1C and gets back exactly what
// was written to book 0x8C via DRC_ADDRESS. Harmless, because every read and
// write pair uses one book consistently - but it means the DRC parameter block
// and this crossover bank share one address space. They do not overlap: the DRC
// block ends at p07/60 and the crossover starts at p07/78.
static constexpr DrcBiquadAddress DRC_XOVER_LOW[DRC_XOVER_LOW_SECTIONS]   = { {0x07, 0x78}, {0x08, 0x14} };
static constexpr DrcBiquadAddress DRC_XOVER_HIGH[DRC_XOVER_HIGH_SECTIONS] = { {0x08, 0x28}, {0x08, 0x3C} };
static constexpr DrcBiquadAddress DRC_XOVER_MID[DRC_XOVER_MID_SECTIONS]   = { {0x08, 0x50}, {0x08, 0x64},
                                                                             {0x08, 0x78}, {0x09, 0x14} };

// TAS5825M crossover biquads are 5.27 throughout, like its EQ biquads.
static constexpr bool DRC_XOVER_MIXED_FORMAT = false;

// Diagnostic sweep range - covers every page the crossover tables above name.
// Kept after those addresses were confirmed: it is still the quickest way to
// see what the DSP actually holds, and it re-verifies the bank on any board
// before three-band mode writes to it.
static constexpr uint8_t DRC_SCAN_PAGE_FIRST = 0x07;
static constexpr uint8_t DRC_SCAN_PAGE_COUNT = 3;   // 0x07 .. 0x09

#endif

//// Diagnostic sweep geometry (identical on both parts)
//
// A DSP page carries coefficients at 0x08..0x7F - 120 bytes, 30 four-byte
// slots - and that space is contiguous across pages. The TAS5805M's own proven
// EQ layout demonstrates it: a biquad starting at 0x7C continues at the next
// page's 0x08. So concatenating each page's 0x08..0x7F gives a flat coefficient
// array, and a biquad that straddles a page boundary is contiguous within it.
//
// 0x7F is the book-select register, but book changes are only honoured from page
// zero, so on a coefficient page it is ordinary data. The proven EQ tables write
// it as the last byte of every sixth biquad.

static constexpr uint8_t DRC_SCAN_SLOT_FIRST  = 0x08;
static constexpr uint8_t DRC_SCAN_PAGE_BYTES  = 0x78;  // 0x7F - 0x08 + 1 = 120
static constexpr uint8_t DRC_SCAN_SLOTS_PAGE  = DRC_SCAN_PAGE_BYTES / 4;  // 30

// Read granularity. 20 bytes is one biquad and divides 120 exactly, so each
// read is a whole candidate biquad and no read spans a page boundary.
static constexpr uint8_t DRC_SCAN_CHUNK       = 20;

static constexpr uint16_t DRC_SCAN_TOTAL_SLOTS =
    static_cast<uint16_t>(DRC_SCAN_PAGE_COUNT) * DRC_SCAN_SLOTS_PAGE;
static constexpr uint16_t DRC_SCAN_TOTAL_BYTES =
    static_cast<uint16_t>(DRC_SCAN_PAGE_COUNT) * DRC_SCAN_PAGE_BYTES;

// Values a resting pass-through biquad's B0 can hold, depending on the format
// TI used for that block. Everything else in the biquad reads zero.
static constexpr uint32_t DRC_UNITY_F5_27 = 0x08000000;
static constexpr uint32_t DRC_UNITY_F2_30 = 0x40000000;
static constexpr uint32_t DRC_UNITY_F1_31 = 0x7FFFFFFF;  // 1.0 is unrepresentable

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

//// The DSP's log domain is not dB
//
// Measured 2026-08-03 with a UMIK-1: at ratio 2 (k = -0.5) with both offsets
// zero, a -18 dBFS tone came out 18.6 dB LOUDER, where gain = k*x predicts
// +9 dB. Twice the authority, to within 0.6 dB. Separately, an offset written as
// -10 dB buried a six-plateau staircase at least 46 dB into the noise floor, and
// a -20 dB threshold never engaged at all - no plateau from -6 to -36 dBFS was
// left flat.
//
// One model accounts for all three. The detector tracks log2 of the mean square,
// and the resulting gain is applied as a power-of-two multiplier on amplitude:
//
//   u       = log2(P)      = level_dB / 3.0103      (10*log10 2)
//   gain_dB = 6.0206 * gain_u                       (20*log10 2)
//   gain_u  = k * u + O
//        =>  gain_dB = 2 * k * level_dB + 6.0206 * O
//
// So every dB quantity needs dividing by the dB-per-unit of whichever side of
// that equation it lands on. Thresholds are compared against u; offsets are
// added to gain_u; the slope picks up a bare factor of two from the
// power-to-amplitude conversion, being the one dimensionless quantity of the
// three. That asymmetry is why ratio-only changes always behaved plausibly while
// every attempt to add an offset overshot into silence.
//
// A -20 dB threshold written raw put the knee at -60 dB, which is why the whole
// staircase sat in one region and nothing was ever flat.
//
// The threshold and offset scales survived measurement. The slope factor did not:
// the model predicts 2.0, and with every plateau held above the knee (threshold
// -40 dB, ratio 2, k written as -0.2500) five consecutive 6 dB input steps came
// out 4.271 +/- 0.098 dB apart. That is a gain slope of -0.288 per dB of level,
// so one unit of the slope register buys 1.152 +/- 0.065 dB of gain per dB of
// level, not 2. The measurement is sound - that run sat between 60 and 81 dB SPL,
// clear of both the room floor and the level where the speaker compresses - so
// the model is simply wrong about this one relationship, and the slope and offset
// domains are not related the way it claims.
//
// 1.152 is therefore empirical and provisional, not a derived constant, and it is
// the least certain number here. Its own prediction is easy to check: at the
// corrected value the same run should give 3.00 dB steps, where the model's 2.0
// would give 3.40 and no scaling at all would give 4.50.
static constexpr float DRC_THRESHOLD_DB_PER_UNIT = 3.0103f;  // 10*log10 2, knee position
static constexpr float DRC_OFFSET_DB_PER_UNIT    = 6.0206f;  // 20*log10 2, knee step
static constexpr float DRC_SLOPE_GAIN_PER_UNIT   = 1.152f;   // measured, see above

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
