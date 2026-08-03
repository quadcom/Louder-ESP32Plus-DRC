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
docs/drc-measurement.md           measuring the real curve with REW + a UMIK-1
docs/ha-dashboard-drc.yaml        dashboard card with the bands separated
tools/drc_math_check.py           checks the math against TI's constants
tools/make_drc_staircase.py       stepped-level test tone for the measurement
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
    refresh: 0s
```

Override `tas58xx_drc_branch` in your board YAML to pin a tag, a commit SHA, or
track a feature branch. It defaults to `main`.

`refresh: 0s` re-fetches on every build. That is deliberate: with a cache window
a push can take hours to reach the device, and you end up debugging code you are
not running. This bit during development — a build compiled a stale clone and
reported four format warnings that had already been fixed, and the only reason
it was caught was that the warnings named lines whose fix was visible in the
working copy. The cost is one `git fetch` per build. If you want reproducible
builds instead, pin `tas58xx_drc_branch` to a commit SHA, which is cached
separately per ref.

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

Two buttons: **DRC Register Dump** (diagnostic, read-only) and **DRC Reset
Defaults**, which puts all fifteen controls back to their compiled-in values.
The reset routes through each entity's `control()`, so the Home Assistant state,
the saved preference and the DSP all end up agreeing — and since ratio 1:1 is a
unity gain curve, the result is inert whatever the master switch is doing. The
switch itself is deliberately left alone.

### Separating the bands in the UI

A Home Assistant **device page** cannot show a divider. It is generated from the
entity list and has no rule or heading primitive, so any "spacer" has to be a
real entity — which renders as a greyed-out read-only row and reads worse than
no separator at all. That was tried and reverted.

Dividers are a dashboard feature. [`docs/ha-dashboard-drc.yaml`](docs/ha-dashboard-drc.yaml)
is a paste-ready `entities` card using `type: section` rows, each drawing an
actual labelled horizontal rule above its band. Add it with **Edit dashboard →
Add card → Manual**. It also drops the repeated band word from each row, since
the heading already carries it, so the controls read `Threshold` / `Ratio` /
`Attack` rather than `DRC Low Threshold`.

### Do not add `on_boot` to a board config

`sendspin-addon-tas58xx.yaml` uses `esphome.on_boot` to play the silent startup
FLAC, and that FLAC is the only thing that gives the DSP its I²S clock lock.
ESPHome's package merge **replaces** a dict with a list rather than appending,
so an `on_boot` in your own file deletes the package's. It validates cleanly,
boots fine, and every EQ and DRC coefficient is then silently discarded with
nothing in the log to explain it.

This was hit while adding the band headers above — which is why they are polled
template sensors rather than published once at boot. If you need a boot action,
put it behind a trigger the packages do not use.

### One band vs three

`drc_bands: 1` (default) is a single full-range compressor. The crossover
biquads stay at their pass-through defaults and DRC bands 2/3 stay muted,
exactly as out of reset. Only the `drc_low_*` controls do anything.

`drc_bands: 3` programs the crossover from `drc_crossover_low` /
`drc_crossover_high` (4th-order Linkwitz-Riley) and sums all three bands.

Start with one band. It is the safe configuration and it validates the
coefficient math on real hardware. The crossover addresses are now confirmed
(see below), so three-band is defensible — but the crossover write path itself
still has no hardware run behind it.

## Status

Verified statically: every DRC address is from the TI process-flow document for
that part, the encoders reproduce every documented default bit-exactly, and the
firmware compiles warning-free for both variants and both band modes.
The 5825M DRC coefficient addresses come from the same table that already
supplies the component's working EQ, volume and mixer addresses.

Verified on hardware — Louder ESP32-S3 Plus, TAS5825M, 2026-08-02:

- **One-band write path.** Every coefficient reads back at the address and in
  the format the code intends: thresholds and slopes in 9.23, time constants in
  1.31, offsets in 9.23, band 1 mixer carrying makeup gain with bands 2
  and 3 held muted. Checked with a distinct fingerprint per band — thresholds −6 /
  −30 / −48 dB, ratios 2 / 4 / 8, attacks 1 / 20 / 200 ms, releases 100 / 500 /
  2000 ms — so all three address maps are confirmed independent and correct.
  33 of 33 fields matched, two of them ±1 LSB of float rounding.
- **Three-band crossover write path.** At `drc_bands: 3` with 300 Hz / 3000 Hz
  corners at 96 kHz, all **forty** coefficient words match
  `tools/drc_math_check.py` exactly. That includes both page-straddling writes
  (`p07/78` and `p08/78`), whose tails landed correctly on the following page.
  The metadata block at `p09/28` and the DRC parameter block on page 0x07 were
  both left intact, so nothing overran.
- **The part does not subtract the threshold.** Found by listening, not by
  readback. Writing zero to both offsets — their documented reset value — and
  going back to threshold −20 dB / ratio 2:1 / makeup 0 dB made the output
  **louder**. With zero offsets the only remaining term is the region slope, and
  `k·(x − T1)` cannot be positive above the threshold, so the part is computing
  `gain = K·x + O` with the thresholds doing nothing but selecting a region.
  **The offsets are y-intercepts**, and they are what places the knee:
  `O1 = O2 = −k·T1`. That also explains why the registers exist at all — under
  the "value at the threshold" reading, a single-knee compressor would leave
  both at zero forever.

  The first attempt wrote `off2 = k·(T2 − T1)`, the natural reading of SLOA148,
  and behaved as a constant −9.5 dB cut. See §3 of the reference doc for both
  results and the test that separates the three candidate conventions.
- **The DSP's log domain is not dB.** Measured 2026-08-03 with a UMIK-1 and REW.
  A −20 dB threshold written raw never engaged anywhere between −6 and −36 dBFS,
  because the detector tracks `log2` of the mean square rather than dB:

      u       = log2(mean square) = level_dB / 3.0103    (10·log10 2)
      gain_dB = 3.0103·k·u + O    = k·level_dB + O

  So a dB quantity needs converting only if it is compared against `u`. The
  threshold is, and is divided by `10·log10 2`. The slope is not, and goes in
  unscaled — the `3.0103` cancels. **What happens to the offsets is still open**;
  see item 1 below.
- **Region 1's offset must be zero.** `off1` governs the region *below* the knee,
  not above it, whatever SLOA148's numbering suggests. With `−k·T1` written there,
  the four below-knee plateaus came back a flat 9.2 dB down and the knee showed a
  10.5 dB discontinuity where 3.5 dB was due. Zeroing it made region 1 transparent
  to 0.3 dB, confirmed in three separate runs. This was the original bug — quiet
  material losing most of its level.

That establishes what the amp *stores*, and most of the shape of what it *does*
with it. The rest needed audio measurements rather than register dumps.

- **The K-slope ↔ ratio mapping, and the threshold.** Measured 2026-08-03 with a
  UMIK-1 and REW. `k = 1/ratio − 1` goes to the register **unscaled**, as SLOA148
  says. Holding every plateau above the knee (threshold −40 dB, ratio 2) gave
  2.780 dB steps against a transparent control's 5.80, i.e. a delivered ratio of
  **2.09 against 2.00 requested**; two later runs give 1.87 and 1.84 for the same
  request. Read the spacing against a control run, never against the test file's
  nominal step: the file steps 6.00 dB and the room delivered 5.78, and dividing by
  the nominal inflates the slope by 4%.

  The threshold is right too, and **RMS-referenced** — the knee lands 3.01 dB above
  the dialled value in sine peak terms. Below it the DRC is transparent, its
  plateaus stepping 5.80 dB against the control's 5.78.

  One measurement discipline earned the rest: **every reliable result here came from
  a control run in the same session, at the same volume.** Each of the three wrong
  constants that got written and then removed came from comparing across sessions,
  or against the file's nominal step, or from readings sitting in the speaker's
  compression region or the room's noise floor.

Not yet verified on hardware:

1. **What the offset register actually does.** Not merely its scale — three runs
   give three answers, and the third one says the shape of the equation is wrong:

   | run | `off2` register | delivered offset | dB per unit |
   |---|---|---|---|
   | 12 | −1.6610 | −1.51 | 0.91 |
   | 13 | −10.00 | ≤ −41.5 | ≥ 4.15 |
   | 14 | −5.3333 | −25.50 | 4.78 |

   A 6× change in the register cannot produce a ≥28× change in delivered
   attenuation, so at most one of those is measuring a scale factor. Each has a
   known weakness: run 12's above-knee points sat at 93.0 and 90.4 dB SPL, inside
   this speaker's compression region; run 13's landed 1–3 dB above a 49 dB room
   floor, making its figures lower bounds.

   Run 14 is the one that reframes the problem. **Louder input produced quieter
   output** — block 1 is 6 dB hotter than block 2 and came out 16 dB below it, a
   transfer slope of −3.67 dB/dB where −0.3333 was written. Any slope past −1.0
   inverts the curve, which is the condition `ratio_to_slope` clamps against; the
   clamp is irrelevant here because the steepness is not coming from `k`. Reading it
   the other way — knee higher than dialled, so only one point is above it — rescues
   `k` but then needs the threshold to deliver 1.70–2.82 dB per unit when the slope
   measurements pin that same scaling to 3.0103.

   So the offset is **not a constant added to the gain**. Runs 9–11 measured `k`
   correctly with a small offset register; run 14 sees an apparent 11× slope error
   with a larger one. Slope and offset are interacting.

   The code writes plain dB meanwhile, as a placeholder rather than a finding. **At
   that scale a low knee over-attenuates loud material severely** — run 13 buried
   region 2 in the noise floor — so treat any offset-dependent behaviour as
   unverified, and leave the ratio at 1 for listening.

   Measuring this needs the `DRC Offset 2 Override` diagnostic, because the two
   normal controls cannot vary the offset independently: `off2 = −k·T1`, so a small
   offset forces a high knee and leaves nothing above it to measure, while a low knee
   forces a large offset. No point in that two-dimensional space gives both. Fighting
   that coupling is why three attempts at a scale factor produced three answers.

   What *is* settled about the offsets: the part computes `gain = k·x + O` and does
   not subtract the threshold itself, measured directly — at register −1.6610 the
   above-knee region **boosted** +3.3 and +6.5 dB where a threshold-subtracting part
   would have cut 5.5 dB.
2. ~~**The offset continuity convention.**~~ **Superseded 2026-08-02 — offsets
   are y-intercepts, not curve values at the thresholds.** The runtime selector
   that compared the three candidates was removed on 2026-08-03: two of them left
   an offset at zero, which unanchors that region into a boost and tripped the
   amp's protection twice.
3. ~~**The 5825M crossover addresses.**~~ **Resolved 2026-08-02 — confirmed
   correct by readback on hardware.** See "What the readback found" below.

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

Expect **eight contiguous** candidates while the crossover is at reset. Others
may appear from unrelated filter banks in the same pages; contiguity and the
bounds either side are what identify the crossover, not the raw count.

### What the readback found

Run on a Louder ESP32-S3 Plus, 2026-08-02. **The configured addresses are
correct.** All eight read back as unity 5.27 pass-throughs forming one
contiguous bank at `p07/78 + n*0x14`, bounded before by non-biquad data at
`p07/64`–`0x74` and after by a metadata block at `p09/28`
(`02DEAD00 74013901 0020C49B`). Eight is exactly a 3-band LR4 split: 2 low,
4 mid, 2 high. Three further candidates at `p09/34`, `p09/48`, `p09/5C` sit on
the far side of that metadata block and belong to a different bank.

The objection that condemned these addresses was right on the fact and wrong on
the conclusion. They are *not* on the EQ grid, and two of them *do* straddle a
page. But the crossover bank simply uses a different alignment from the EQ bank
on this part: the EQ-grid slots `p08/08`, `p08/1C` and `p08/30` all read
`0x00000000`, a null biquad rather than a pass-through. Straddling is handled —
`book_and_page_write_` splits at the boundary and resumes at the next page's
`0x08`, the path it already takes for the TAS5805M.

One incidental finding: **books `0x8C` and `0xAA` alias** on this part (or book
select is a no-op for these DSP pages). The sweep reads book `0xAA` `p07/08`
and gets back exactly what the DRC block wrote via book `0x8C`. Harmless, since
reads and writes are paired consistently, but it means the DRC parameters and
the crossover bank share one address space. They do not overlap.

### What the risk actually is

Not chip damage and not bricking — these are volatile DSP coefficient RAM
writes, reloaded from the ESP32 on every boot, so a power cycle is a guaranteed
clean reset. They also cannot reach analog gain, modulation or PDN, which live in
book `0x00`; the crossover writes are confined to book `0xAA` and the write
helper always restores book 0 / page 0. (Register `0x7F` is book-select, and a
20-byte biquad write does reach it — that is safe, because book only changes from
page 0, which is why the helper sets page←0 before book. Upstream's working EQ
writes `0x7F` routinely.)

The exposure is your **speakers**. The larger of the two original concerns is
now closed: the misaligned-write scenario — splicing the tail of one biquad onto
the head of another, producing poles outside the unit circle and an unstable
biquad that self-oscillates to full scale — depended on the addresses being
wrong, and the readback shows they are not.

What remains is ordinary misuse: if a crossover corner is set nonsensically
while bands 2 and 3 are unmuted, the three bands still sum, and overlapping
bands mean a level rise — up to about **+9.5 dB** if all three pass the same
full-range content.

The amp's DC-offset, overcurrent and thermal protections guard the amp, not a
loudspeaker fed a full-scale tone. First three-band test: low volume, speakers
you can afford to lose.

Suggested first hardware session: flash with `drc_bands: 1`, press **DRC
Register Dump** to capture a baseline, enable the DRC switch with ratio 4:1 and
threshold −20 dB, dump again, and confirm the written values match what
`tools/drc_math_check.py` predicts.
