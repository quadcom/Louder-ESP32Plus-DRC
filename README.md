# TAS58xx DRC control for the Louder ESP32 Plus

Adds runtime 3-band DRC control to the Sonocotta Louder ESP32 Plus, exposed as
Home Assistant entities through ESPHome. Built as a fork of
`mrtoy-me/esphome-tas58xx`, the component the Sonocotta ESPHome packages already
use for EQ, volume, mixer and fault reporting.

**Primary target: Louder ESP32-S3 Plus, Sendspin, Ethernet** —
[`esphome/louder-esp32-s3-plus-drc-sendspin.yaml`](esphome/louder-esp32-s3-plus-drc-sendspin.yaml).
Fully self-contained, so it can be pasted straight into the ESPHome device code
window with nothing copied alongside it.

The plain-ESP32 configs are kept and still build. Nothing in the component is
MCU-specific — the DSP register maps depend on the *amplifier* variant, not on
which Espressif part drives it — so ESP32 and ESP32-S3 need no separate code
paths.

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
  louder-esp32-s3-plus-drc-sendspin.yaml        PRIMARY - S3, Sendspin, Ethernet,
                                       self-contained, paste-ready
  louder-esp32-plus-drc-sendspin.yaml  ESP32, Sendspin, WiFi
  louder-esp32-plus-drc-full.yaml      ESP32, HA media_player, WiFi
  louder-esp32-plus-drc.yaml           ESP32, amp only - compile check target
  packages/dac-tas58xx-drc.yaml        DAC + DRC package, for the ESP32 configs
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

Ready configs, all validated and compiled:

| Config | MCU | Player | Net |
| --- | --- | --- | --- |
| `louder-esp32-s3-plus-drc-sendspin.yaml` | S3 | Sendspin | Ethernet |
| `louder-esp32-plus-drc-sendspin.yaml` | ESP32 | Sendspin | WiFi |
| `louder-esp32-plus-drc-full.yaml` | ESP32 | HA `media_player` | WiFi |

The S3 config inlines the DAC + DRC block instead of including the package,
because a config pasted into the ESPHome device code window cannot `!include` a
local file. That also means its DAC substitutions are uncommented with real
values — those defaults used to live inside the remote `dac-tas58xx.yaml` it
replaces, and would otherwise be undefined.

`esphome/louder-esp32-plus-drc.yaml` is amp-control-only, with no player. It is
the fast compile-check target. **Do not flash it and expect DRC to work** — the
DSP ignores coefficient writes until it has locked to the I²S clock, which is
why the player packages play a silent FLAC at boot.

The component is pulled from this repo, so nothing needs copying:

```yaml
external_components:
  - source: github://quadcom/Louder-ESP32Plus-DRC@${tas58xx_drc_branch}
    components: [tas58xx]
    refresh: 48h
```

Override `tas58xx_drc_branch` in your board YAML to pin a tag or track a
feature branch. It defaults to `main`.

### Home Assistant add-on / ESPHome Device Builder

Copy one file into the add-on's config share and add a board YAML beside it:

```
esphome/packages/dac-tas58xx-drc.yaml  ->  /config/esphome/packages/
esphome/louder-esp32-plus-drc-*.yaml   ->  /config/esphome/
```

No `components/` copy and no path edit — the component comes from GitHub.

### Local CLI

```
pip install esphome                     # 2026.7.3 or later
# put real credentials in esphome/secrets.yaml
esphome run esphome/louder-esp32-plus-drc-sendspin.yaml
```

Keep `name:` matching the device already in Home Assistant so it keeps its
entity history, and flash over OTA from the existing firmware.

### Working on the component itself

Building from GitHub means a push before every test flash. To build against the
working copy instead, swap the `external_components` block in
`packages/dac-tas58xx-drc.yaml` for the commented-out local one kept directly
below it:

```yaml
external_components:
  - source:
      type: local
      path: ../components
    components: [tas58xx]
```

That path is relative to the directory of the **top-level** YAML, not the
package — so it assumes the board config stays in `esphome/`. Run `esphome` from
the project root.

## Verify before flashing

```
python tools/drc_math_check.py     # algorithm vs TI documented defaults
python tools/cpp_check/run.py      # shipping C++ vs the algorithm
esphome compile esphome/louder-esp32-plus-drc-sendspin.yaml
```

All three pass as committed. The C++ check needs a host compiler —
`pip install ziglang` provides one. The first `esphome compile` pulls the
ESP-IDF toolchain, a few GB.

Firmware builds clean, warning-free, for all of:

| Target | Result |
| --- | --- |
| TAS5825M, 1 band, amp only | 779 KB flash / 26.9% RAM |
| TAS5805M, 3 band | 779 KB flash / 26.9% RAM |
| TAS5825M, Sendspin (full stack) | 1307 KB flash / 32.6% RAM |

The 5805M target is built purely to exercise the other side of the per-variant
`#ifdef` — its address tables and the three-band crossover path are otherwise
never seen by a compiler when building for a Louder Plus. Keep it in the loop
after touching `tas58xx_drc.h`.

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
that part, the encoders reproduce every documented default bit-exactly, and the
firmware compiles warning-free for both variants and both band modes.
The 5825M DRC coefficient addresses come from the same table that already
supplies the component's working EQ, volume and mixer addresses.

Nothing here has been run on a board. What compiles and what the amp does with
the coefficients are different questions, and only the first is settled.

Not yet verified on hardware:

1. **The K-slope ↔ compression-ratio mapping.** `k = 1/ratio − 1` comes from
   SLOA148 §5.2 and is consistent with the documented defaults, but no
   measurement confirms it on this part. Sweep input level, measure gain
   reduction, compare against the requested ratio.
2. **The offset continuity convention.** `off1 = 0`, `off2 = k·(T2 − T1)` is
   derived from SLOA148's description of offsets as the curve value at each
   threshold. If region 2 and 3 turn out to be discontinuous, this is why.
3. **The 5825M crossover addresses — believed wrong, not merely unconfirmed.**
   Reconstructed from the offset sequence because SLAA786A's page column is
   mis-transcribed in that section. They fail the strongest available
   cross-check: every proven TAS5825M biquad address sits on the grid
   `0x08 + n*0x14` within a page (six per page, the last ending exactly at
   `0x7F`, never crossing a page), and none of the reconstructed offsets do.
   Two of them would straddle a page boundary, which the TAS5825M's own layout
   never does. See the comment on `DRC_XOVER_*` in `tas58xx_drc.h`.

   Only `drc_bands: 3` writes them. Leave it at `1` until they are read back and
   re-derived — the **DRC Register Dump** button does the reading, see below.

### Locating the real crossover addresses

The **DRC Register Dump** button is read-only and writes nothing. Press it with
`drc_bands: 1`, before enabling the DRC, and it will find the crossover blocks
by observation rather than by trusting the reconstructed table. Needs the logger
at `DEBUG` or lower.

It sweeps book `0xAA` across the crossover pages and prints:

1. **A raw dump** of every 4-byte slot, six per line. Unreadable slots show as
   `--------` rather than zeros, because a failed read that looked like zeros
   would fake the signature below.
2. **Pass-through candidates.** Out of reset every crossover biquad is a
   pass-through: one non-zero word then four zeros. The pages are concatenated
   into one flat buffer first — that space is contiguous across pages, as the
   5805M's own EQ proves with a biquad starting at `0x7C` and continuing at the
   next page's `0x08` — so a straddling biquad is still found. Straddling is
   precisely what is in dispute here. B0's value also names the format:
   `0x08000000` is unity in 5.27, `0x40000000` in 2.30, `0x7FFFFFFF` in 1.31.
3. **A verdict per configured address**, marking each of the eight
   `DRC_XOVER_*` entries `address plausible` or `NOT pass-through - suspect`.

Expect **eight** candidates while the crossover is at reset. Then:

| What you see | What it means |
| --- | --- |
| Candidates land on the configured addresses | the alignment concern was unfounded; three-band is safe to try |
| Candidates land on `0x08 + n*0x14` instead | those are the real addresses — correct the table |
| No candidates | the crossover is not in the swept pages; the reference doc is wrong |

### What the risk actually is

Not chip damage and not bricking — these are volatile DSP coefficient RAM
writes, reloaded from the ESP32 on every boot, so a power cycle is a guaranteed
clean reset. They also cannot reach analog gain, modulation or PDN, which live in
book `0x00`; the crossover writes are confined to book `0xAA` and the write
helper always restores book 0 / page 0. (Register `0x7F` is book-select, and a
20-byte biquad write does reach it — that is safe, because book only changes from
page 0, which is why the helper sets page←0 before book. Upstream's working EQ
writes `0x7F` routinely.)

The exposure is your **speakers**, two ways:

- A straddling or misaligned write can splice the tail of one biquad onto the
  head of another. That is not a coherent filter and its poles may sit outside
  the unit circle; an unstable IIR biquad self-oscillates to full scale.
- If the crossover does not land while bands 2 and 3 are unmuted, three copies
  of the full-range signal sum — about **+9.5 dB** — into hard clipping.

The amp's DC-offset, overcurrent and thermal protections guard the amp, not a
loudspeaker fed a full-scale tone. First three-band test: low volume, speakers
you can afford to lose.

Suggested first hardware session: flash with `drc_bands: 1`, press **DRC
Register Dump** to capture a baseline, enable the DRC switch with ratio 4:1 and
threshold −20 dB, dump again, and confirm the written values match what
`tools/drc_math_check.py` predicts.
