#pragma once

#include <cmath>

namespace esphome::tas58xx {

enum ControlState : uint8_t {
    CTRL_DEEP_SLEEP = 0x00, // Deep Sleep
    CTRL_SLEEP      = 0x01, // Sleep
    CTRL_HI_Z       = 0x02, // Hi-Z
    CTRL_PLAY       = 0x03, // Play
   };

enum DacMode : uint8_t {
  BTL  = 0, // Bridge tied load
  PBTL = 1, // Parallel load
};

enum ModulationScheme : uint8_t {
  MODE_BD = 0,
  MODE_1SPW = 1,
};

enum EqRefreshMode : uint8_t {
    AUTO   = 0,
    MANUAL = 1,
};

enum ExcludeIgnoreMode : uint8_t {
    NONE        = 0,
    CLOCK_FAULT = 1,
};

enum LoopSetupStage : uint8_t {
    WAIT_FOR_TRIGGER = 0,
    RUN_DELAY_LOOP,
    INPUT_MIXER_SETUP,
    LR_VOLUME_SETUP,
    EQ_BANDS_SETUP,
    EQ_PRESETS_SETUP,
    DRC_SETUP,
    SETUP_COMPLETE,
};

// Ordering is load-bearing: setters compare against their own stage to decide
// whether to write now or only cache until 'loop' has reached that stage.
// DRC_SETUP must therefore stay after the EQ stages and before SETUP_COMPLETE.
#ifdef USE_TAS58XX_DRC
static constexpr LoopSetupStage STAGE_AFTER_EQ = DRC_SETUP;
#else
static constexpr LoopSetupStage STAGE_AFTER_EQ = SETUP_COMPLETE;
#endif
struct Tas58xxFault {
  uint8_t channel_fault{0};                  // individual faults extracted when publishing
  uint8_t global_fault{0};                   // individual faults extracted when publishing

  bool clock_fault{false};
  bool temperature_fault{false};
  bool temperature_warning{false};

  bool is_fault_except_clock_fault{false};   // fault conditions combined except clock fault

#ifdef USE_TAS58XX_BINARY_SENSOR
  bool have_fault{false};                    // combined binary sensor - any fault found but does not include clock fault if excluded
#endif
};

// Everything the part will report about itself that is not a fault.
//
// This is the whole list. The TAS5825M has no level meter of any kind - no peak,
// RMS or dBFS register anywhere in the control port map - and no readback of the
// attenuation its DRC, AGL or thermal foldback is applying. PVDD_ADC is the only
// analog measurement on the device: there is no output current, output power or
// load impedance diagnostic, and no numeric die temperature, only the four OTW
// threshold flags. Anything level related has to be measured before the stream
// reaches the DAC.
struct Tas58xxStatus {
  uint8_t power_state{0xFF};  // POWER_STATE 0x68, ControlState encoding. 0xFF = not read yet
  // AUTOMUTE_STATE 0x69, bit0 left / bit1 right, 1 = output is zero. Starts with
  // both muted so the boot state publishes as "no signal" rather than claiming
  // signal on a channel nothing has looked at yet.
  uint8_t automute{0x03};
  uint8_t fs_mon{0};          // FS_MON 0x37, bits 3-0 = detected sample rate
  uint8_t warning{0};         // WARNING 0x73, OTW levels in bits 3-0 and CBC warnings in bits 5-4
  uint8_t pvdd_raw{0};        // PVDD_ADC 0x5E, only meaningful in play

  // Derived in read_status_registers_. NAN until PVDD has been read in play,
  // which publishes as unavailable rather than as a misleading zero.
  float pvdd_volts{NAN};
  float max_output_power_w{NAN};
  float clip_headroom_db{NAN};
};

static constexpr float TAS58XX_MIN_ANALOG_GAIN         = -15.5;
static constexpr float TAS58XX_MAX_ANALOG_GAIN         = 0.0;

// PVDD_ADC counts per volt: "PVDD Voltage = PVDD_ADC[7:0] / 8.428 (V)".
static constexpr float TAS58XX_PVDD_ADC_PER_VOLT       = 8.428;

// AGAIN 0x54 code 0 is "0 dB (29.5V peak voltage)", stepping -0.5dB per code, so
// the peak output voltage at 0dBFS is 29.5 * 10^(gain/20).
//
// The per-step voltages tabulated in the datasheet and quoted in the device YAML
// sit up to ~2% away from that curve - those are measured typicals, while this is
// the register definition. Close enough for a headroom figure either way.
static constexpr float TAS58XX_PEAK_VOLTS_AT_0DB       = 29.5;

// set book and page registers
static constexpr uint8_t TAS58XX_PAGE_SET              = 0x00;
static constexpr uint8_t TAS58XX_BOOK_SET              = 0x7F;
static constexpr uint8_t TAS58XX_BOOK_ZERO             = 0x00;
static constexpr uint8_t TAS58XX_PAGE_ZERO             = 0x00;

// tas58x5m registers
static constexpr uint8_t TAS58XX_DEVICE_CTRL_1         = 0x02;
static constexpr uint8_t TAS58XX_DEVICE_CTRL_2         = 0x03;
static constexpr uint8_t TAS58XX_FS_MON                = 0x37;
static constexpr uint8_t TAS58XX_BCK_MON               = 0x38;
static constexpr uint8_t TAS58XX_DIG_VOL_CTRL          = 0x4C;
static constexpr uint8_t TAS58XX_ANA_CTRL              = 0x53;
static constexpr uint8_t TAS58XX_AGAIN                 = 0x54;
static constexpr uint8_t TAS58XX_PVDD_ADC              = 0x5E;  // TAS5825M only - no equivalent in the TAS5805M map
static constexpr uint8_t TAS58XX_POWER_STATE           = 0x68;
static constexpr uint8_t TAS58XX_AUTOMUTE_STATE        = 0x69;

// TAS58XX FAULT constants
static constexpr uint8_t TAS58XX_CHAN_FAULT            = 0x70;
static constexpr uint8_t TAS58XX_GLOBAL_FAULT1         = 0x71;
static constexpr uint8_t TAS58XX_GLOBAL_FAULT2         = 0x72;
static constexpr uint8_t TAS58XX_OT_WARNING            = 0x73;
static constexpr uint8_t TAS58XX_FAULT_CLEAR           = 0x78;
static constexpr uint8_t TAS58XX_ANALOG_FAULT_CLEAR    = 0x80;

}  // namespace esphome::tas58xx
