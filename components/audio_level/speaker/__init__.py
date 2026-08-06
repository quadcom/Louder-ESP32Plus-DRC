import esphome.codegen as cg
from esphome.components import audio, sensor, speaker
import esphome.config_validation as cv
from esphome.const import (
    CONF_BITS_PER_SAMPLE,
    CONF_ID,
    CONF_NUM_CHANNELS,
    CONF_OUTPUT_SPEAKER,
    CONF_SAMPLE_RATE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    PLATFORM_ESP32,
    STATE_CLASS_MEASUREMENT,
    UNIT_DECIBEL,
)
from esphome.core.entity_helpers import inherit_property_from

from .. import audio_level_ns

CODEOWNERS = ["@quadcom"]

LevelMeterSpeaker = audio_level_ns.class_(
    "LevelMeterSpeaker", cg.PollingComponent, speaker.Speaker
)

CONF_LEFT_RMS = "left_rms"
CONF_RIGHT_RMS = "right_rms"
CONF_LEFT_PEAK = "left_peak"
CONF_RIGHT_PEAK = "right_peak"
CONF_SILENCE_FLOOR = "silence_floor"

# dBFS relative to digital full scale, so every reading is negative.
LEVEL_SENSOR_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_DECIBEL,
    accuracy_decimals=1,
    state_class=STATE_CLASS_MEASUREMENT,
    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
)


def _set_stream_limits(config):
    # 16 and 32 bit are what the meter knows how to read. Anything else passes
    # through unmeasured with a warning rather than failing the build, but there is
    # no reason to accept it here.
    audio.set_stream_limits(
        min_bits_per_sample=16,
        max_bits_per_sample=32,
    )(config)
    return config


def _validate_audio_compatibility(config):
    # A meter changes nothing about the stream, so its format is whatever the
    # speaker below it takes. Nothing to configure and nothing to get wrong.
    inherit_property_from(CONF_NUM_CHANNELS, CONF_OUTPUT_SPEAKER)(config)
    inherit_property_from(CONF_SAMPLE_RATE, CONF_OUTPUT_SPEAKER)(config)
    inherit_property_from(CONF_BITS_PER_SAMPLE, CONF_OUTPUT_SPEAKER)(config)

    audio.final_validate_audio_schema(
        "source_speaker",
        audio_device=CONF_OUTPUT_SPEAKER,
        bits_per_sample=config.get(CONF_BITS_PER_SAMPLE),
        channels=config.get(CONF_NUM_CHANNELS),
        sample_rate=config.get(CONF_SAMPLE_RATE),
    )(config)


CONFIG_SCHEMA = cv.All(
    speaker.SPEAKER_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(LevelMeterSpeaker),
            cv.Required(CONF_OUTPUT_SPEAKER): cv.use_id(speaker.Speaker),

            cv.Optional(CONF_LEFT_RMS): LEVEL_SENSOR_SCHEMA,
            cv.Optional(CONF_RIGHT_RMS): LEVEL_SENSOR_SCHEMA,
            cv.Optional(CONF_LEFT_PEAK): LEVEL_SENSOR_SCHEMA,
            cv.Optional(CONF_RIGHT_PEAK): LEVEL_SENSOR_SCHEMA,

            # Reported instead of -infinity for a window of digital silence, so the
            # sensors stay graphable.
            cv.Optional(CONF_SILENCE_FLOOR, default=-100.0): cv.float_range(
                min=-200.0, max=-20.0
            ),
        }
    ).extend(cv.polling_component_schema("500ms")),
    cv.only_on([PLATFORM_ESP32]),
    _set_stream_limits,
)

FINAL_VALIDATE_SCHEMA = _validate_audio_compatibility


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await speaker.register_speaker(var, config)

    output_spkr = await cg.get_variable(config[CONF_OUTPUT_SPEAKER])
    cg.add(var.set_output_speaker(output_spkr))

    cg.add(var.set_silence_floor(config[CONF_SILENCE_FLOOR]))

    for key, setter in (
        (CONF_LEFT_RMS, var.set_left_rms_sensor),
        (CONF_RIGHT_RMS, var.set_right_rms_sensor),
        (CONF_LEFT_PEAK, var.set_left_peak_sensor),
        (CONF_RIGHT_PEAK, var.set_right_peak_sensor),
    ):
        if sensor_config := config.get(key):
            sens = await sensor.new_sensor(sensor_config)
            cg.add(setter(sens))
