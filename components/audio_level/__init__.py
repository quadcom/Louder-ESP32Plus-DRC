"""A pass-through speaker that measures the level of the audio going past it.

Exists because the TAS58xx amplifiers cannot report level: there is no peak, RMS
or dBFS register anywhere in their control port map, and no readback of the gain
reduction their DRC applies. The only place a number can be had is in the stream
on its way to the DAC.

Independent of the tas58xx component - it wraps any ESPHome speaker.
"""

import esphome.codegen as cg

AUTO_LOAD = ["audio", "sensor"]

audio_level_ns = cg.esphome_ns.namespace("audio_level")
