# Measuring the DRC with REW and a UMIK-1

Everything about the DRC that a register dump cannot answer — the real
compression ratio, where the knee actually sits, whether the curve is flat below
it — falls out of one measurement: play a tone whose level steps down in known
dB increments, and log the output level.

**Nothing needs calibrating.** Below the knee the compressor is inactive, so the
measured plateaus are spaced exactly one input step apart. Above the knee they
compress to `step / ratio`. Speaker sensitivity, mic calibration, amp gain, room
response and mic position are all fixed offsets that apply equally to every
plateau, so they cancel out of the *spacing*:

```
ratio = step_dB / measured_spacing_dB
knee  = the input level at which the spacing changes
```

Absolute SPL is never used. The room cannot bias the answer.

---

## 1. Make the signal

```
python tools/make_drc_staircase.py
```

Default output is `drc-staircase.wav`: a 1 kHz sine at 48 kHz, 16-bit stereo,
84 seconds — a −24 dBFS reference tone, then nineteen 4-second plateaus stepping
from 0 down to −54 dBFS in 3 dB steps, then the reference again. The script
prints the timestamp of every plateau; keep that output.

Each plateau is a whole number of cycles, so level changes land on a zero
crossing and there is no click for the detector to chase.

The two reference tones must measure the same. If they don't, something drifted
during the run and the whole capture is suspect.

For a finer look at the knee once you know roughly where it is:

```
python tools/make_drc_staircase.py --start -30 --stop -6 --step 1 --seconds 3 --out drc-knee.wav
```

**Levels are sine peak amplitude relative to full scale.** A sine's RMS is
3.01 dB below its peak, and the script prints both columns. Which one the knee
lines up with is itself a result — see §5.

## 2. Get it to the amp

Copy the WAV into Home Assistant's `config/www/` directory, which is served at
`/local/`:

```yaml
action: media_player.play_media
target:
  entity_id: media_player.speaker
data:
  media_content_id: http://homeassistant.local:8123/local/drc-staircase.wav
  media_content_type: music
```

Use the real hostname or IP if `homeassistant.local` doesn't resolve from the
ESP32.

## 3. Set up, and do not touch anything mid-run

These matter, because each one shifts the level reaching the DSP and would move
the apparent knee:

- **Media player volume at 100%.** ESPHome scales the PCM in software before it
  reaches I²S, so anything less makes the file's stated dBFS a lie. At 50% every
  level is 6 dB lower and the knee appears 6 dB off.
- **EQ flat or off.** EQ gain at 1 kHz shifts the level directly.
- **Channel volumes fixed**, and left alone between runs.
- **Makeup 0 dB.** Makeup is a constant offset that cancels out of the spacing,
  but leaving it at zero keeps the plots directly comparable.
- **`drc_bands: 1`** for the first measurement, so band 1 is full range and one
  set of controls owns the whole result. In three-band mode a 1 kHz tone lands in
  the *mid* band, and the `drc_mid_*` controls are the ones that matter.
- **The mic does not move** between runs. Put it on a stand, not in your hand.

In REW: UMIK-1 selected as input with its calibration file loaded, **Z
weighting** (not A), and levels set so the loudest plateau is well clear of both
the room noise floor and any clipping.

Capture with REW's **SPL meter with logging enabled**, which gives you the whole
84-second staircase as a time series to read plateau levels straight off. Failing
that the **RTA** works fine — watch the 1 kHz bin and note each of the nineteen
plateaus by hand against the script's timestamps.

## 4. Run it twice

**Run 1 — DRC off.** This is the control, and it validates the whole measurement
chain. Spacing should be **3.00 dB between every pair of plateaus.**

Where it isn't, the fault is not the DRC:

- Spacing collapsing at the *bottom* is the room noise floor. Ignore plateaus
  below it.
- Spacing collapsing at the *top* is the amp or the speaker compressing. Reduce
  the volume and run again, or discard the top plateaus.

Do not proceed until you have a straight run of plateaus at 3.00 dB. Anything you
measure with the DRC on is only trustworthy across that range.

**Run 2 — DRC on.** Threshold −20 dB, ratio 2, makeup 0 dB, offset convention
**Intercept**. Same file, same everything else.

## 5. Read it

Expected spacing for a 3 dB input step:

| ratio | spacing above the knee |
|---|---|
| 1:1 | 3.00 dB |
| 2:1 | **1.50 dB** |
| 3:1 | 1.00 dB |
| 4:1 | 0.75 dB |
| 8:1 | 0.38 dB |
| 20:1 | 0.15 dB |

At threshold −20 and ratio 2, a correct compressor gives:

- **3.00 dB spacing** for every plateau below the knee, identical to run 1.
- **1.50 dB spacing** for every plateau above it.
- A clean break between the two.

Three things to read off the result:

**The ratio** is `3.00 / spacing`. If it comes out at 2.0 ± 0.1, `k = 1/ratio − 1`
is confirmed on this part and the last open item in the README is closed.

**The knee position** tells you what the detector measures. The threshold was set
to −20 dB. If the break appears at a plateau of −20 dBFS *peak*, the detector is
peak- or full-scale-referenced. If it appears at −17 dBFS peak — which is
−20 dBFS RMS — the detector is RMS-referenced, and every threshold number in the
UI is really 3 dB lower than it looks for sine input. Either answer is fine, but
it needs writing down.

**Flatness below the knee** settles the region 1 question. Plateaus below the
knee must sit at the *same absolute level* as run 1, not merely have the same
spacing. If they are all uniformly low by about 10 dB — which is `−k·T1`, the
value Intercept writes to the offset registers — then region 1 is picking up an
offset it shouldn't, and the offsets need rethinking again. Comparing absolute
levels between runs is the one place absolute numbers matter, and it works
because nothing in the chain moved between them.

## 6. While it's set up

Cheap additions, since the rig is already together:

- **Ratio 4 at the same threshold.** Spacing should go 1.50 → 0.75 dB. Two ratios
  agreeing is much stronger evidence than one.
- **Threshold −10 with ratio 2.** The knee should move up by exactly 10 dB and
  the spacing should not change.
- **The other two offset conventions.** Switch the `DRC Offset Convention` entity
  to `Zero` and to `Continuity` and repeat. Predictions: `Zero` should show
  correct 1.50 dB spacing above the knee but the whole curve lifted, and no flat
  region below the knee. `Continuity` should show a constant offset. This
  converts the by-ear comparison into numbers, and would explain the original
  quiet-piano result that the intercept model still doesn't account for.
- **Attack and release.** The step transitions are level steps of known size, so
  the settling time at each edge is the real time constant. That checks
  `α = 1/(fs·τ)` and, with it, whether `fs` is really 96 kHz — an 88.2 kHz DSP
  rate would make every time constant 8.8% out.

## 7. Recording the result

Put the numbers in `docs/tas58xx-drc-reference.md` §3 and update the verification
list in `README.md`. Export the REW measurements too — plateau levels for both
runs is a small table, and it is the only hard evidence that exists for how this
part's DRC actually behaves.
