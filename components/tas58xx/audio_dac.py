import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.core import CORE
from esphome.components import i2c
from esphome.components.audio_dac import AudioDac
from esphome import pins

from esphome.const import (
    CONF_ADDRESS,
    CONF_ENABLE_PIN,
    CONF_ID,
    CONF_PLATFORM,
)

CODEOWNERS = ["@mrtoy-me"]
DEPENDENCIES = ["i2c"]

# yaml configuration constants
CONF_ANALOG_GAIN = "analog_gain"
CONF_AUDIO_DAC = "audio_dac"
CONF_DAC_MODE = "dac_mode"
CONF_MODULATION = "modulation"
CONF_TAS58XX_DAC = "tas58xx_dac"
CONF_IGNORE_FAULT = "ignore_fault"
CONF_MIXER_MODE = "mixer_mode"
CONF_LOAD_IMPEDANCE = "load_impedance"
CONF_REFRESH_EQ = "refresh_eq"
CONF_VOLUME_MIN = "volume_min"
CONF_VOLUME_MAX = "volume_max"
CONF_TAS58XX_ID = "tas58xx_id"
CONF_DRC_BANDS = "drc_bands"
CONF_DRC_CROSSOVER_LOW = "drc_crossover_low"
CONF_DRC_CROSSOVER_HIGH = "drc_crossover_high"
CONF_DRC_ENERGY = "drc_energy"
CONF_DRC_DSP_RATE = "drc_dsp_rate"

# used for looking through CORE.config to derive eq configuration
NUMBER_COMPONENT= "number"
SELECT_COMPONENT = "select"
PLATFORM_TAS58XX = "tas58xx"
LEFT_EQ_GAIN_20HZ = "left_eq_gain_20Hz"
RIGHT_EQ_GAIN_20HZ = "right_eq_gain_20Hz"
EQ_PRESET_LEFT_CHANNEL = "eq_preset_left_channel"

# eq mode enum and select index values
EQ_OFF = 0
EQ_15BAND = 1
EQ_BIAMP = 2
EQ_PRESETS = 3

# i2c addresses of dac models
TAS5805M_I2C_ADDR = 0x2D
TAS5825M_I2C_ADDR = 0x4C

tas58xx_ns = cg.esphome_ns.namespace("tas58xx")
Tas58xxComponent = tas58xx_ns.class_("Tas58xxComponent", AudioDac, cg.PollingComponent, i2c.I2CDevice)

EqRefreshMode = tas58xx_ns.enum("EqRefreshMode")
EQ_REFRESH_MODES = {
     "AUTO"  : EqRefreshMode.AUTO,
     "MANUAL": EqRefreshMode.MANUAL,
}

TasDac = tas58xx_ns.enum("TasDac")
TAS_DACS = {
    "TAS5805M" : TasDac.TAS5805M,
    "TAS5825M" : TasDac.TAS5825M,
}

DacMode = tas58xx_ns.enum("DacMode")
DAC_MODES = {
    "BTL"  : DacMode.BTL,
    "PBTL" : DacMode.PBTL,
}

ModulationScheme = tas58xx_ns.enum("ModulationScheme")
MODULATION_SCHEMES = {
    "BD_MODE"   : ModulationScheme.MODE_BD,
    "1SPW_MODE" : ModulationScheme.MODE_1SPW,
}

ExcludeIgnoreMode = tas58xx_ns.enum("ExcludeIgnoreModes")
EXCLUDE_IGNORE_MODES = {
     "NONE"        : ExcludeIgnoreMode.NONE,
     "CLOCK_FAULT" : ExcludeIgnoreMode.CLOCK_FAULT,
}

MixerMode = tas58xx_ns.enum("MixerMode")
MIXER_MODES = {
    "STEREO"         : MixerMode.STEREO,
    "STEREO_INVERSE" : MixerMode.STEREO_INVERSE,
    "MONO"           : MixerMode.MONO,
    "RIGHT"          : MixerMode.RIGHT,
    "LEFT"           : MixerMode.LEFT,
}

DrcBandMode = tas58xx_ns.enum("DrcBandMode")
# Keyed by string so this accepts both a literal 'drc_bands: 3' and a YAML
# substitution, which always arrives as text.
DRC_BAND_MODES = {
    "1" : DrcBandMode.DRC_ONE_BAND,
    "3" : DrcBandMode.DRC_THREE_BAND,
}

def validate_drc_bands(value):
    return cv.enum(DRC_BAND_MODES)(cv.string(value))

ANALOG_GAINS = [-15.5, -15, -14.5, -14, -13.5, -13, -12.5, -12, -11.5, -11, -10.5, -10, -9.5, -9, -8.5, -8,
                 -7.5,  -7,  -6.5,  -6,  -5.5,  -5,  -4.5,  -4,  -3.5,  -3,  -2.5,  -2, -1.5, -1, -0.5,  0]

def validate_config(config):
    if config[CONF_DAC_MODE] == "PBTL" and (config[CONF_MIXER_MODE] == "STEREO" or config[CONF_MIXER_MODE] == "STEREO_INVERSE"):
        raise cv.Invalid("dac_mode: PBTL must have mixer_mode: MONO or RIGHT or LEFT")
    if (config[CONF_VOLUME_MAX] - config[CONF_VOLUME_MIN]) < 9:
        raise cv.Invalid("volume_max must at least 9db greater than volume_min")
    if config[CONF_DRC_CROSSOVER_LOW] >= config[CONF_DRC_CROSSOVER_HIGH]:
        raise cv.Invalid("drc_crossover_low must be below drc_crossover_high")
    drc_energy_ms = config[CONF_DRC_ENERGY].total_milliseconds
    if drc_energy_ms < 1 or drc_energy_ms > 50:
        raise cv.Invalid("drc_energy must be between 1ms and 50ms")
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(Tas58xxComponent),
            cv.Required(CONF_ENABLE_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_TAS58XX_DAC, default="TAS5805M"): cv.enum(
                        TAS_DACS, upper=True
            ),
            cv.Optional(CONF_ANALOG_GAIN, default="-15.5dB"): cv.All(
                        cv.decibel, cv.one_of(*ANALOG_GAINS)
            ),
            cv.Optional(CONF_DAC_MODE, default="BTL"): cv.enum(
                        DAC_MODES, upper=True
            ),
            cv.Optional(CONF_MODULATION, default="BD_MODE"): cv.enum(
                        MODULATION_SCHEMES, upper=True
            ),
            cv.Optional(CONF_IGNORE_FAULT, default="CLOCK_FAULT"): cv.enum(
                        EXCLUDE_IGNORE_MODES, upper=True
            ),
            cv.Optional(CONF_MIXER_MODE, default="STEREO"): cv.enum(
                        MIXER_MODES, upper=True
            ),
            # Nominal speaker impedance in ohms. Nothing is written to the device -
            # it only turns the measured PVDD into a maximum output power figure for
            # the max_output_power sensor.
            cv.Optional(CONF_LOAD_IMPEDANCE, default=8.0): cv.All(
                        cv.resistance, cv.float_range(min=1.0, max=32.0)
            ),
            cv.Optional(CONF_REFRESH_EQ, default="AUTO"): cv.enum(
                        EQ_REFRESH_MODES, upper=True
            ),
            cv.Optional(CONF_VOLUME_MAX, default="24dB"): cv.All(
                        cv.decibel, cv.int_range(-103, 24)
            ),
            cv.Optional(CONF_VOLUME_MIN, default="-103dB"): cv.All(
                        cv.decibel, cv.int_range(-103, 24)
            ),
            # DRC. 1 band is a full-range compressor and leaves the crossover
            # biquads alone; 3 band programs the crossover and sums all three.
            cv.Optional(CONF_DRC_BANDS, default="1"): validate_drc_bands,
            cv.Optional(CONF_DRC_CROSSOVER_LOW, default="300Hz"): cv.All(
                        cv.frequency, cv.float_range(min=40.0, max=12000.0)
            ),
            cv.Optional(CONF_DRC_CROSSOVER_HIGH, default="3000Hz"): cv.All(
                        cv.frequency, cv.float_range(min=40.0, max=12000.0)
            ),
            # RMS estimator window, shared by all bands. Range checked in
            # validate_config so this stays a plain time period.
            cv.Optional(CONF_DRC_ENERGY, default="2ms"): cv.positive_time_period_milliseconds,
            # The DSP internal rate the 1.31 time constants are relative to, NOT
            # the I2S input rate. Process Flow 1 upconverts to 88.2 or 96 kHz.
            cv.Optional(CONF_DRC_DSP_RATE, default=96000): cv.one_of(
                        48000, 88200, 96000, int=True
            ),
        }
    )
    .extend(cv.polling_component_schema("1s"))
    .extend(i2c.i2c_device_schema(TAS5805M_I2C_ADDR))
    .add_extra(validate_config),
    cv.only_on_esp32,
)

def left_eq_gain_exists():
    all_numbers = CORE.config.get(NUMBER_COMPONENT, [])
    for num in all_numbers:
        if num.get(CONF_PLATFORM) == PLATFORM_TAS58XX:
            if LEFT_EQ_GAIN_20HZ in num:
                return True

    return False

def right_eq_gain_exists():
    all_numbers = CORE.config.get(NUMBER_COMPONENT, [])
    for num in all_numbers:
        if num.get(CONF_PLATFORM) == PLATFORM_TAS58XX:
            if RIGHT_EQ_GAIN_20HZ in num:
                return True

    return False

def select_eq_presets_exists():
    all_select = CORE.config.get(SELECT_COMPONENT, [])
    for select in all_select:
        if select.get(CONF_PLATFORM) == PLATFORM_TAS58XX:
            if EQ_PRESET_LEFT_CHANNEL in select:
                return True

    return False

async def to_code(config):
    derived_eq_mode_configuration = EQ_OFF
    if right_eq_gain_exists():
        derived_eq_mode_configuration  = EQ_BIAMP
    else:
      if left_eq_gain_exists():
          derived_eq_mode_configuration = EQ_15BAND
      else:
        if select_eq_presets_exists():
            derived_eq_mode_configuration = EQ_PRESETS

    tas58xx_dac = config.get(CONF_TAS58XX_DAC)
    if tas58xx_dac == "TAS5825M":
        config[CONF_ADDRESS] = TAS5825M_I2C_ADDR

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
    enable = await cg.gpio_pin_expression(config[CONF_ENABLE_PIN])
    cg.add(var.set_enable_pin(enable))
    cg.add(var.config_analog_gain(config[CONF_ANALOG_GAIN]))
    cg.add(var.config_dac_mode(config[CONF_DAC_MODE]))
    cg.add(var.config_modulation_scheme(config[CONF_MODULATION]))
    cg.add(var.config_ignore_fault_mode(config[CONF_IGNORE_FAULT]))
    cg.add(var.config_mixer_mode(config[CONF_MIXER_MODE]))
    cg.add(var.config_load_impedance(config[CONF_LOAD_IMPEDANCE]))
    cg.add(var.config_refresh_eq(config[CONF_REFRESH_EQ]))
    cg.add(var.config_volume_max(config[CONF_VOLUME_MAX]))
    cg.add(var.config_volume_min(config[CONF_VOLUME_MIN]))
    cg.add(var.config_eq_mode(derived_eq_mode_configuration))
    cg.add(var.config_drc_band_mode(config[CONF_DRC_BANDS]))
    cg.add(var.config_drc_crossover(config[CONF_DRC_CROSSOVER_LOW], config[CONF_DRC_CROSSOVER_HIGH]))
    cg.add(var.config_drc_energy_ms(config[CONF_DRC_ENERGY].total_milliseconds))
    cg.add(var.config_drc_dsp_rate(config[CONF_DRC_DSP_RATE]))

    if config[CONF_DAC_MODE] == "PBTL":
        cg.add_define("USE_DAC_MODE_PBTL")

    if tas58xx_dac == "TAS5805M":
        cg.add_define("USE_TAS5805M_DAC")
    else:
        cg.add_define("USE_TAS5825M_DAC")
