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
| Media player volume | **100%** | ESPHome scales the audio in software before the DSP sees it. At 50% every level is 6 dB lower and the answer moves 6 dB. |
| EQ | flat / off | EQ gain at 1 kHz changes the level directly. |
| `DRC` switch | **off** for run 1 | |
| `DRC Low Makeup` | 0 dB | |
| `DRC Low Threshold` | −20 dB | for run 2 |
| `DRC Low Ratio` | 2 | for run 2 |

Put the UMIK-1 on a stand a metre or so in front of the speaker, pointed at it,
and **do not move it or touch the volume again until both runs are done.**

### C. Open REW's SPL meter

REW → `Tools` menu → **SPL Meter**. Pick the UMIK-1 as the input and load its
calibration file if you have it. Set weighting to **Z** (not A). You now have one
big number on screen that tracks the level in the room. That number is all you
need — no sweeps, no logging, no graphs.

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

Subtract again to get the five gaps.

- Gaps of **6.0 dB** mean the compressor is doing nothing at those levels.
- Gaps of **3.0 dB** mean it is compressing at exactly 2:1 — which is what was
  asked for, and the result that closes this out.
- Some other number means the ratio is `6.0 / gap`. A gap of 2.0 is really 3:1,
  a gap of 1.5 is really 4:1.

With the threshold at −20 dB you should see the change happen between the
−18 dBFS and −24 dBFS plateaus: the loud end compressed, the quiet end not.

Two more things fall out of the same six numbers:

- **Where the change happens** tells you what the chip's detector measures. If
  the −18 plateau is compressed and −24 is not, that's about right for a
  threshold of −20. If the break sits noticeably lower than expected, the
  detector is reading RMS rather than peak, and every threshold in the UI is
  really 3 dB lower than it says.
- **The quiet plateaus must match run 1's numbers**, not just each other's
  spacing. If the −30 and −36 readings are (say) 10 dB below what run 1 gave for
  the same plateaus, the DRC is quietly cutting level where it should be doing
  nothing at all — which is the thing that was wrong with the piano track, and
  worth knowing.

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
chain. Spacing should be **3.00 dB between every pair of plateaus.**

Where it isn't, the fault is not the DRC:

- Spacing collapsing at the *bottom* is the room noise floor. Ignore plateaus
  below it.
- Spacing collapsing at the *top* is the amp or the speaker compressing. Reduce
  the volume and run again, or discard the top plateaus.

Do not proceed until you have a straight run of plateaus at 3.00 dB. Anything you
measure with the DRC on is only trustworthy across that range.

**Run 2 — DRC on.** Threshold −20 dB, ratio 2, makeup 0 dB. Same file, same
everything else.

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
