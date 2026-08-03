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

## 0. The short version

**Six numbers per run, written down on paper.** That is the entire measurement.
Sections 1–7 are the detailed version; if this section is enough, stop here.

### A. Put the file where the speaker can fetch it

Use `drc-simple.wav` — 64 seconds, a 1 kHz tone that drops in six 6 dB steps,
eight seconds each.

Copy it into Home Assistant's **`config/media/`** folder (create that folder if it
doesn't exist). That is the one Home Assistant lists in its own media browser, so
playing it takes no YAML at all — see step D.

To regenerate it:

```
python tools/make_drc_staircase.py --start -6 --stop -36 --step 6 --seconds 8 --reference -18 --out drc-simple.wav
```

### B. Set the amp up — this part is not optional

In Home Assistant, on the Speaker device page:

| Control | Set to | Because |
|---|---|---|
| Media player volume | **90%**, and identical in both runs | ESPHome scales the audio in software *before* the DSP, so volume moves every level relative to a fixed knee. 100% has clipped the amp into protection on this rig; 90% has been the working setting throughout. |
| EQ | flat / off | EQ gain at 1 kHz changes the level directly. |
| `DRC` switch | **off** for run 1 | |
| `DRC Low Makeup` | 0 dB | |
| `DRC Low Threshold` | **−16 dB** | for run 2 |
| `DRC Low Ratio` | **1.5** | for run 2 |

Threshold −16 with ratio 1.5 is chosen deliberately, not arbitrarily. It puts two
plateaus above the knee and four below, and it keeps every predicted reading away
from both hazards that have wrecked earlier runs — nothing near the ~90 dB SPL where
this speaker starts compressing, nothing near the room's noise floor. Threshold −20
with ratio 2 does not: at the current offset scale it buries everything above the
knee. **If you are listening rather than measuring, leave the ratio at 1.**

Put the UMIK-1 on a stand a metre or so in front of the speaker, pointed at it,
and **do not move it or touch the volume again until both runs are done.**

### C. Open REW's RTA and watch the 1 kHz bin

REW → `Tools` menu → **RTA**. Pick the UMIK-1 as the input and load its
calibration file if you have it. Set a long averaging window (a high average
count, or "Forever"). Read the height of the **1 kHz** bar at the middle of each
plateau. That one number per plateau is the whole measurement — no sweeps, no
logging, no exporting.

**Use the RTA rather than the SPL Meter.** The RTA counts only energy in the 1 kHz
bin, so broadband room noise stops mattering and the effective noise floor drops
20–30 dB. That matters more than it sounds: with the SPL meter a 49 dB room floor
put a hard limit on how quiet the plateaus could go, which distorted the bottom of
every early run and forced an awkwardly quiet test file. The RTA also reads to
0.1 dB where the SPL meter was being read to about 1 dB — and at 3 dB steps the
whole difference between transparent and 2:1 is 1.44 dB, so that resolution was
the difference between a result and a shrug.

If you do fall back to the **SPL Meter**, set weighting to **Z** (not A), and treat
any plateau within about 10 dB of the room floor as unusable.

Play the file once with nothing running to check the number lands somewhere
sensible: comfortably above the quiet-room reading, and not so loud the amp is
straining.

### D. Run 1 — DRC switch OFF

Start the file playing: **Media** in the Home Assistant sidebar → **My media** →
`drc-simple.wav` → choose **Speaker** → play. Two clicks to repeat it for
run 2.

> If it isn't listed, the file isn't in `config/media/`. Nothing else is needed —
> that folder is browsable out of the box.

The first eight seconds are a reference tone — ignore them. Then six plateaus
follow, eight seconds each. **Write down the SPL number in the middle of each
plateau**, once it has settled. Six numbers.

| plateau | starts at | read at | Run 1 (DRC off) | Run 2 (DRC on) |
|---|---|---|---|---|
| −6 dBFS | 0:08 | 0:12 | | |
| −12 dBFS | 0:16 | 0:20 | | |
| −18 dBFS | 0:24 | 0:28 | | |
| −24 dBFS | 0:32 | 0:36 | | |
| −30 dBFS | 0:40 | 0:44 | | |
| −36 dBFS | 0:48 | 0:52 | | |

### E. Check run 1 before going on

Subtract each number from the one above it. **All five gaps should be 6.0 dB**
(±0.3 is fine — it's a room).

If the top gaps are smaller than 6, the amp or speaker is compressing: turn the
volume down and redo run 1. If the bottom gaps are smaller than 6, those
plateaus are down in the room noise — ignore them and only use the ones above.

Do not do run 2 until run 1 gives you a straight run of 6.0 dB gaps. A bad run 1
means the measurement chain is lying and nothing from run 2 can be trusted.

### F. Run 2 — DRC switch ON

Turn the `DRC` switch on. Change nothing else. Play the same file, write down the
same six numbers in the second column.

### G. What the numbers mean

The ratio and the threshold are already confirmed. What this run measures is the
**offset scale**, and the number that carries it is not a gap — it is how far the
top two plateaus sit below the same plateaus in run 1. Three candidate scales
predict wildly different answers:

| plateau | if the scale is 1 | if it is `10·log10 2` | if it is `20·log10 2` |
|---|---|---|---|
| −6 dBFS | run 1 − 2.3 | run 1 − 13.1 | run 1 − 29.1 |
| −12 dBFS | run 1 − 0.3 | run 1 − 11.1 | run 1 − 27.1 |

They are ~11 dB apart, so this does not need a careful measurement to settle — only
an honest one. If the answer lands nowhere near any of the three columns, that is
worth knowing too: it would mean the offset is not a scaled dB quantity at all.

Two checks on the same six numbers:

- **The four quiet plateaus must match run 1's numbers**, not just each other's
  spacing. Threshold −16 means the −18 plateau and below are beneath the knee (the
  detector is RMS-referenced, so a −18 dBFS sine reads −21 dBFS to it) and the DRC
  should be leaving them completely alone. If they are down by a constant amount,
  the region-1 offset has come back.
- **The gap between the top two** should be run 1's own gap × `1/1.5`, so about
  3.9 dB if run 1 delivers its usual 5.8 — not 4.0 dB from the file's nominal 6.0.
  That is a free re-check of the ratio, and it is independent of the offset scale.

Send me the two columns and I'll do the arithmetic and write it up.

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

Simplest route, and no YAML: copy the WAV into `config/media/` and play it from
**Media → My media** in the Home Assistant sidebar, choosing the speaker as the
target.

The alternative is `config/www/`, which is served at `/local/` without
authentication, driven by a `media_player.play_media` action. That is worth using
if you want the run scripted or repeatable from an automation — the URL is stable
where a media-browser one is signed and temporary.

Note this is an **action**, not a dashboard card: it goes in *Developer tools →
Actions* (use `Go to YAML mode` to paste it), or in the `action:` block of a
script or automation. Pasting it into a manual card will not work.

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
chain. It is not optional and it is not reusable from an earlier session: the
board re-enables its EQ on every reboot, which puts a constant offset on 1 kHz and
silently invalidates any cross-session comparison of absolute level.

The spacing should be **the same between every pair of plateaus.** It will not be
the file's nominal 6.00 — this rig delivers 5.78 to 5.80 — and that measured figure,
not the nominal, is what every later division uses. Getting this backwards inflated
a slope constant by 4% and cost two runs.

Where the spacing isn't straight, the fault is not the DRC:

- Collapsing at the *bottom* is the room noise floor. Ignore plateaus within about
  10 dB of a quiet-room reading; on the SPL meter that has meant everything under
  ~59 dB.
- Collapsing at the *top* is the amp or the speaker compressing — on this rig,
  anything above about 90 dB SPL. Reduce the volume and run again, or discard the
  top plateaus. **Do not fit anything to a reading in that region**; two conclusions
  have already been retracted for resting on one.

**Run 2 — DRC on.** Threshold −16 dB, ratio 1.5, makeup 0 dB. Same file, same
volume, same mic position, same session.

## 5. Read it

Expected spacing above the knee, as a fraction of run 1's own measured spacing:

| ratio | above the knee | × run 1's 5.80 |
|---|---|---|
| 1:1 | run 1 × 1.00 | 5.80 dB |
| 1.5:1 | run 1 × 0.667 | **3.87 dB** |
| 2:1 | run 1 × 0.50 | 2.90 dB |
| 4:1 | run 1 × 0.25 | 1.45 dB |
| 8:1 | run 1 × 0.125 | 0.73 dB |
| 20:1 | run 1 × 0.05 | 0.29 dB |

The ratio and the threshold are both confirmed on this part already — three runs
each. What is left is the **offset scale**, and spacing cannot see it: an offset is a
constant, so it shifts the whole above-knee line without changing its slope. The
number that carries it is how far the above-knee plateaus sit below run 1's, in
absolute terms. §0's table has the predictions.

Two supporting checks, both from the same six readings:

**Flatness below the knee.** Plateaus below the knee must sit at the *same absolute
level* as run 1, not merely share its spacing. A uniform shortfall of about `−k·T1`
means region 1 has picked up an offset it should never have — the bug that started
this whole exercise, where a quiet piano track lost most of its level. Comparing
absolute levels between runs is the one place absolute numbers matter, and it only
works because nothing in the chain moved between them.

**The break position** confirms what the detector measures, and it has: the knee
lands 3.01 dB above the dialled threshold in sine peak terms, because a sine's RMS is
3.01 dB below its peak. With the threshold at −16, expect the −18 dBFS plateau
(−21 dBFS RMS) to be *below* the knee and −12 dBFS (−15 RMS) above it.

## 6. While it's set up

Cheap additions, since the rig is already together:

- **Ratio 4 at the same threshold.** Spacing should go 1.50 → 0.75 dB. Two ratios
  agreeing is much stronger evidence than one.
- **Threshold −10 with ratio 2.** The knee should move up by exactly 10 dB and
  the spacing should not change.
> **The `DRC Offset Convention` select no longer exists.** Two of its three
> options left an offset at zero, which leaves `gain = k·x` — a boost that grows
> as the signal gets quieter and reaches full scale at every input level. `Zero`
> tripped the amp's protection twice on 2026-08-03, once at a measured +18.6 dB.
> It earned its keep on the way out: that boost is what revealed the doubled
> slope. The offsets are settled now and computed unconditionally, so if the
> entity still appears in Home Assistant after a flash, delete it under
> *Settings → Devices & services → Entities*.
- **Attack and release.** The step transitions are level steps of known size, so
  the settling time at each edge is the real time constant. That checks
  `α = 1/(fs·τ)` and, with it, whether `fs` is really 96 kHz — an 88.2 kHz DSP
  rate would make every time constant 8.8% out.

## 7. Recording the result

Put the numbers in `docs/tas58xx-drc-reference.md` §3 and update the verification
list in `README.md`. Export the REW measurements too — plateau levels for both
runs is a small table, and it is the only hard evidence that exists for how this
part's DRC actually behaves.
