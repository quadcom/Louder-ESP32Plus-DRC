# TAS58xx DRC control for the Louder ESP32 Plus

Adds runtime 3-band DRC control to the Sonocotta Louder ESP32 Plus, exposed as
Home Assistant entities through ESPHome. Built as a fork of
`mrtoy-me/esphome-tas58xx`, the component the Sonocotta ESPHome packages already
use for EQ, volume, mixer and fault reporting.

## The part is a TAS5825M, not a TAS5805M

The Louder ESP32 Plus (and Pro) carry a **TAS5825M**; only the plain Louder
ESP32 and the Mini use the TAS5805M. The two have incompatible DSP memory maps —
different I²C address, different pages for mixer, volume, EQ and DRC. See
[docs/tas58xx-drc-reference.md](docs/tas58xx-drc-reference.md) §1.

DRC is implemented for **both** parts behind the component's existing
`USE_TAS5805M_DAC` / `USE_TAS5825M_DAC` split.

## Layout

```
components/tas58xx/          fork of mrtoy-me/esphome-tas58xx, DRC added
  tas58xx_drc.h              per-variant DRC address maps, ranges, defaults
  tas58xx_drc.cpp            band coefficients, crossover, mixer, readback
  tas58xx_helpers.{h,cpp}    fixed-point encoders and biquad design
  number/drc_number.{h,cpp}  the 15 DRC number entities
  switch/drc_enable_switch.* master DRC enable
esphome/
  louder-esp32-plus-drc-sendspin.yaml  Sendspin player + DRC
  louder-esp32-plus-drc-full.yaml      HA media_player + DRC
  louder-esp32-plus-drc.yaml           amp only, for `esphome config` checks
  packages/dac-tas58xx-drc.yaml        DAC + DRC package
docs/tas58xx-drc-reference.md     verified register maps and coefficient math
tools/drc_math_check.py           checks the math against TI's constants
tools/cpp_check/run.py            checks the shipping C++ against that math
reference/                        upstream repos, for reference only
```

## Getting it into ESPHome

`packages/dac-tas58xx-drc.yaml` is a **drop-in replacement for the Sonocotta
`packages/dac-tas58xx.yaml`**. It exports the same ids the other Sonocotta
packages bind to (`external_dac`, `enable_dac`, `over_temperature_warning`,
`tas58xx_eq_mode_select`) and adds the DRC entities, so the rest of the stack —
audio, media player or Sendspin, light, IR, monitoring — is untouched and still
comes from the Sonocotta repo.

Two ready configs, both validated:

| Config | Player |
| --- | --- |
| `esphome/louder-esp32-plus-drc-sendspin.yaml` | Sendspin (multi-room) |
| `esphome/louder-esp32-plus-drc-full.yaml` | HA `media_player` / speaker |

`esphome/louder-esp32-plus-drc.yaml` is amp-control-only, with no player. It is
for `esphome config` checks. **Do not flash it and expect DRC to work** — the
DSP ignores coefficient writes until it has locked to the I²S clock, which is
why the player packages play a silent FLAC at boot.

### Local CLI

```
pip install esphome                     # 2026.7.3 or later
# put real credentials in esphome/secrets.yaml
esphome run esphome/louder-esp32-plus-drc-sendspin.yaml
```

The `external_components` path in the package is `../components`, resolved
relative to the directory of the **top-level** YAML — so run it from the project
root with the config still in `esphome/`.

Keep `name:` matching the device already in Home Assistant so it keeps its
entity history, and flash over OTA from the existing firmware.

### Home Assistant add-on / ESPHome Device Builder

Copy two things into the add-on's config share:

```
components/tas58xx/           ->  /config/esphome/components/tas58xx/
esphome/packages/dac-tas58xx-drc.yaml -> /config/esphome/packages/
```

Then copy one of the board YAMLs to `/config/esphome/` and change the component
path, since the YAML now sits beside `components/` rather than above it:

```yaml
external_components:
  - source:
      type: local
      path: components      # was ../components
    components: [tas58xx]
```

That edit goes in `packages/dac-tas58xx-drc.yaml`.

Alternative that avoids copying: push this fork to a git remote and point the
package at it instead, which is the tidier option if you run more than one
board.

```yaml
external_components:
  - source: github://<you>/esphome-tas58xx-drc@main
    components: [tas58xx]
    refresh: 48h
```

## Verify before flashing

```
python tools/drc_math_check.py     # algorithm vs TI documented defaults
python tools/cpp_check/run.py      # shipping C++ vs the algorithm
esphome config esphome/louder-esp32-plus-drc-sendspin.yaml
```

All three pass as committed. The C++ check needs a host compiler —
`pip install ziglang` provides one.

Not yet done: a full firmware compile. The DRC math layer is compiled and
checked for the host, but `tas58xx_drc.cpp` and the entity classes have only
been reviewed, not built. `esphome compile` is the next gate and will pull the
ESP-IDF toolchain (a few GB on first run).

## Controls

Per band (low / mid / high): threshold dB, ratio n:1, attack ms, release ms,
makeup dB. Plus a master `DRC` switch. Settings persist in ESP32 flash and are
re-applied after boot, which they must be — the amp has no non-volatile memory,
so every coefficient is lost on power cycle.

Ratio 1:1 makes a band inert, and that is the default, so a fresh install
changes nothing until you ask it to.

### One band vs three

`drc_bands: 1` (default) is a single full-range compressor. The crossover
biquads stay at their pass-through defaults and DRC bands 2/3 stay muted,
exactly as out of reset. Only the `drc_low_*` controls do anything.

`drc_bands: 3` programs the crossover from `drc_crossover_low` /
`drc_crossover_high` (4th-order Linkwitz-Riley) and sums all three bands.

Start with one band. It is the safe configuration, it validates the coefficient
math on real hardware, and the crossover addresses are the one part of the
memory map that no shipping code has previously exercised.

## Status

Verified statically: every DRC address is from the TI process-flow document for
that part, and the encoders reproduce every documented default bit-exactly.
The 5825M DRC coefficient addresses come from the same table that already
supplies the component's working EQ, volume and mixer addresses.

Not yet verified on hardware:

1. **The K-slope ↔ compression-ratio mapping.** `k = 1/ratio − 1` comes from
   SLOA148 §5.2 and is consistent with the documented defaults, but no
   measurement confirms it on this part. Sweep input level, measure gain
   reduction, compare against the requested ratio.
2. **The offset continuity convention.** `off1 = 0`, `off2 = k·(T2 − T1)` is
   derived from SLOA148's description of offsets as the curve value at each
   threshold. If region 2 and 3 turn out to be discontinuous, this is why.
3. **The 5825M crossover addresses.** Reconstructed from the offset sequence
   because SLAA786A's page column is mis-transcribed in that section. Read them
   back before trusting them — hit the `DRC Register Dump` button, which logs
   the live block.

Suggested first hardware session: flash with `drc_bands: 1`, press **DRC
Register Dump** to capture a baseline, enable the DRC switch with ratio 4:1 and
threshold −20 dB, dump again, and confirm the written values match what
`tools/drc_math_check.py` predicts.
