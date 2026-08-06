import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_VOLTAGE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_DECIBEL,
    UNIT_HERTZ,
    UNIT_VOLT,
    UNIT_WATT,
)

CONF_FAULTS_CLEARED = "faults_cleared"
CONF_PVDD_VOLTAGE = "pvdd_voltage"
CONF_MAX_OUTPUT_POWER = "max_output_power"
CONF_CLIP_HEADROOM = "clip_headroom"
CONF_OVER_TEMP_WARNING_LEVEL = "over_temp_warning_level"
CONF_SAMPLE_RATE = "sample_rate"

from ..audio_dac import CONF_TAS58XX_DAC, CONF_TAS58XX_ID, Tas58xxComponent, tas58xx_ns

FaultSensor = tas58xx_ns.class_("FaultSensor", cg.PollingComponent)

# PVDD_ADC (0x5E) and everything derived from it are TAS5825M only - the register
# does not exist in the TAS5805M control port map.
PVDD_SENSORS = [CONF_PVDD_VOLTAGE, CONF_MAX_OUTPUT_POWER, CONF_CLIP_HEADROOM]

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(FaultSensor),
            cv.GenerateID(CONF_TAS58XX_ID): cv.use_id(Tas58xxComponent),

            cv.Optional(CONF_FAULTS_CLEARED): sensor.sensor_schema(
                    accuracy_decimals=0,
                    state_class=STATE_CLASS_MEASUREMENT,
            ),

            # Measured supply voltage. Unavailable unless the DAC is in play -
            # PVDD_ADC reads zero otherwise.
            cv.Optional(CONF_PVDD_VOLTAGE): sensor.sensor_schema(
                    unit_of_measurement=UNIT_VOLT,
                    device_class=DEVICE_CLASS_VOLTAGE,
                    accuracy_decimals=2,
                    state_class=STATE_CLASS_MEASUREMENT,
            ),

            # Derived from the measured rail, the analog gain and load_impedance.
            cv.Optional(CONF_MAX_OUTPUT_POWER): sensor.sensor_schema(
                    unit_of_measurement=UNIT_WATT,
                    device_class=DEVICE_CLASS_POWER,
                    accuracy_decimals=1,
                    state_class=STATE_CLASS_MEASUREMENT,
                    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),

            # Negative means analog_gain is set above what the rail can deliver.
            cv.Optional(CONF_CLIP_HEADROOM): sensor.sensor_schema(
                    unit_of_measurement=UNIT_DECIBEL,
                    accuracy_decimals=1,
                    state_class=STATE_CLASS_MEASUREMENT,
                    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),

            # 0 to 4: 1 = 112C, 2 = 122C, 3 = 134C, 4 = 146C. Anything above 0
            # means thermal foldback is attenuating.
            cv.Optional(CONF_OVER_TEMP_WARNING_LEVEL): sensor.sensor_schema(
                    accuracy_decimals=0,
                    state_class=STATE_CLASS_MEASUREMENT,
                    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),

            # Sample rate the device detects on its own input, not what was
            # configured. 0 means an FS error or a reserved code.
            cv.Optional(CONF_SAMPLE_RATE): sensor.sensor_schema(
                    unit_of_measurement=UNIT_HERTZ,
                    accuracy_decimals=0,
                    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
        }
    ).extend(cv.polling_component_schema("60s"))
)


def _validate_pvdd_is_tas5825m(config):
    """PVDD_ADC only exists on the TAS5825M, so reject these on a TAS5805M rather
    than silently publishing nothing."""
    requested = [key for key in PVDD_SENSORS if key in config]
    if not requested:
        return config

    full_config = fv.full_config.get()
    parent_path = full_config.get_path_for_id(config[CONF_TAS58XX_ID])[:-1]
    parent = full_config.get_config_for_path(parent_path)

    if parent[CONF_TAS58XX_DAC] != "TAS5825M":
        raise cv.Invalid(
            f"{', '.join(requested)} requires tas58xx_dac: TAS5825M - "
            "the PVDD ADC register does not exist on the TAS5805M"
        )
    return config


FINAL_VALIDATE_SCHEMA = _validate_pvdd_is_tas5825m


async def to_code(config):
    tas58xx_component = await cg.get_variable(config[CONF_TAS58XX_ID])
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await cg.register_parented(var, tas58xx_component)

    if clear_faults_config := config.get(CONF_FAULTS_CLEARED):
      sens = await sensor.new_sensor(clear_faults_config)
      cg.add(var.set_times_faults_cleared_sensor(sens))

    if pvdd_config := config.get(CONF_PVDD_VOLTAGE):
      sens = await sensor.new_sensor(pvdd_config)
      cg.add(var.set_pvdd_voltage_sensor(sens))

    if power_config := config.get(CONF_MAX_OUTPUT_POWER):
      sens = await sensor.new_sensor(power_config)
      cg.add(var.set_max_output_power_sensor(sens))

    if headroom_config := config.get(CONF_CLIP_HEADROOM):
      sens = await sensor.new_sensor(headroom_config)
      cg.add(var.set_clip_headroom_sensor(sens))

    if otw_config := config.get(CONF_OVER_TEMP_WARNING_LEVEL):
      sens = await sensor.new_sensor(otw_config)
      cg.add(var.set_over_temp_warning_level_sensor(sens))

    if fs_config := config.get(CONF_SAMPLE_RATE):
      sens = await sensor.new_sensor(fs_config)
      cg.add(var.set_sample_rate_sensor(sens))
