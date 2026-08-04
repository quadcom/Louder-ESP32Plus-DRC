# DRC status — where this stands, 2026-08-03

Runtime 3-band DRC control for the TAS5825M, exposed as Home Assistant entities
through ESPHome. This file is the handover: what works, what is measured, what is
suspect, and what to do next. `docs/tas58xx-drc-reference.md` §3 has the raw numbers
from every run; this is the summary you can act on.

**Repository is public** (`quadcom/Louder-ESP32Plus-DRC`). Real `secrets.yaml` and
`reference/` stay gitignored, as do the generated `*.wav` test signals.

---

## 1. The short version

The single-band full-range compressor works and is measured. Threshold, ratio and
the offset that anchors the knee are all confirmed on hardware, and the coefficient
math is verified bit-exact by register readback.

**What is not confirmed is the attack and release behaviour, and there is positive
evidence something is wrong with it.** See §4. Nothing about the static transfer
curve depends on that, but anything with real programme material does.

| area | state |
|---|---|
| write path, 1 band | verified by readback, 33/33 fields |
| write path, 3 band + crossover | verified by readback, 40/40 words |
| threshold | measured, RMS-referenced, lands where dialled |
| ratio / slope | measured four times, best is 1.494 for 1.500 requested |
| offset / knee anchoring | measured, `20·log10 2` per register unit |
| **attack / release** | **untested, and two runs suggest a fault** |
| makeup gain | untested |
| 3-band operation | write path only; never listened to or measured |

---

## 2. The coefficient model, as measured

The DSP's log domain is not dB, and the three quantities do not share a scaling:

    u        = log2(mean square) = level_dB / 3.0103      <- the threshold's domain
    g        = k * (u/2) + O                              <- log2 of an amplitude
    gain_dB  = 6.0206 * g  =  k * level_dB + 6.0206 * O

The part multiplies the slope by `log2` of the **RMS** — half the `log2` of the mean
square — and exponentiates the result as an amplitude gain. That internal factor of
two is the whole reason the three constants differ:

| quantity | scaling to the register | why |
|---|---|---|
| threshold `T1`, `T2` | ÷ `10·log10 2` = 3.0103 | compared against `u` |
| slope `k` | **none** | the part's ÷2 cancels `6.0206 / 3.0103` |
| offset `off1`, `off2` | ÷ `20·log10 2` = 6.0206 | added in the log-amplitude domain |

Region mapping, which the register numbering actively misleads about:

    region 1   x <  T1    gain = k0*x + O1,  k0 = 0, O1 = 0    -> transparent
    region 2   x >= T1    gain = k *x + O2,  O2 = -k*T1        -> 0 dB at T1
    region 3   x >= T2    same slope, so the line continues through T2

Two things worth keeping in mind:

- **`off1` governs region 1, below the knee, and must be zero.** With `−k·T1` there,
  quiet material took a flat 9.2 dB cut. That was the original bug, found by ear on a
  piano track.
- **The part does not subtract the threshold.** `gain = k·x + O`, with the thresholds
  only selecting a region, so an offset is a y-intercept and `O2 = −k·T1` is what
  puts the knee at `T1`. Measured directly: with a small offset the region above the
  knee *boosted*, where a threshold-subtracting part would have cut.

Both are load-bearing and both are counter-intuitive; see the comment block in
`components/tas58xx/tas58xx_drc.h` before changing either.

---

## 3. The measurement rig

Everything was measured with a UMIK-1 into REW.
Full procedure in `docs/drc-measurement.md`. The parts that matter:

- **RTA on the 1 kHz bin**, not the SPL Meter. Rejects broadband room noise, so the
  effective floor drops 20–30 dB, and reads to 0.1 dB instead of ~1 dB.
- **`tools/make_drc_staircase.py`** generates the test signal. Put the `.wav` in
  Home Assistant's `config/media/`, then play it from **Media → My media** to the
  device. No YAML, no automation.
- Volume **90%**, identical in both passes. ESPHome scales PCM in software *before*
  the DSP, so volume moves every level relative to a fixed knee. 100% has clipped
  the amp into protection on the test rig.

### Four error sources, all confirmed rather than suspected

| source | effect |
|---|---|
| EQ re-enables on every reboot | constant 1 kHz offset; invalidates cross-session absolute comparisons |
| speaker compresses above ~90 dB SPL | flattens the loud end of any run |
| room floor 49 dB (SPL meter) | anything within ~10 dB of it is unusable |
| the file's nominal step ≠ the room's | file steps 6.00 dB, room delivers 5.78 |

The EQ behaviour is **deliberately left alone** — turn the EQ off manually before each
session and check it stayed off.

### The one discipline that mattered

Every wrong constant this component has carried came from trusting an absolute level.
Three were written and removed: a factor of two on the slope (from reading a clipped
98 dB SPL as the chip's gain), a 1.152 on the slope (from dividing by the file's
nominal step instead of the room's), and a plain-dB offset (from a run whose only two
above-knee readings sat in the speaker's compression region).

What finally worked, in run 15, needed **no absolute reference at all**: hold the knee
below every plateau so the whole file is compressed, then

- the **difference between two passes at one plateau** isolates the offset, because
  `k·level` is identical in both and cancels;
- the **gaps within one pass** isolate the slope, because a constant offset shifts
  every plateau equally.

Prefer that shape of measurement over anything requiring a calibrated level.

---

## 4. The open problem: attack and release

**Untested, and two runs point at a fault.** Against the settled model, run 13 matches
every below-knee plateau to 0.8 dB and correctly buries its above-knee pair under the
noise floor. Two other runs read *louder* than predicted:

| run | plateau | predicted | measured | residual |
|---|---|---|---|---|
| 12 | −6 dBFS | 84.2 | 93.0 | **+8.8** |
| 12 | −12 dBFS | 81.4 | 90.4 | **+9.0** |
| 14 | −6 dBFS | 59.9 | 66.5 | **+6.6** |

### Why this looks like a time-constant problem

In both of those runs the DRC had to **attack into compression**. The test file's
reference tone sits below the knee at those thresholds, so the gain started from rest
at every loud plateau. In run 15 — the run that fits the model — the knee was at −60,
so the reference tone was already above it and the gain was engaged before the first
plateau began. That is the one structural difference between the run that agrees and
the two that do not, and the residuals all have the sign of *insufficient gain
reduction*.

### How slow it would have to be

Run 14's block 1 needed −29.1 dB and delivered −22.5 dB, so the gain had covered 78%
of its travel at the plateau's midpoint, about 4 s in. For a one-pole that is roughly
1.5 time constants, putting τ near **2.7 s against 10 ms dialled — a factor of ~270**.

That is a big enough error to be a format or interpretation bug rather than drift.
**2⁸ = 256** is suspiciously close, and 8 bits is exactly the difference between 1.31
and 9.23 — writing a 1.31 coefficient into a field the part reads as 9.23 would make
alpha 256× too small and τ 256× too long. Weigh that against two arguments for 1.31
being right, though: TI's documented reset value for these registers is `0x7FFFFFFF`,
which is ~1.0 in 1.31 and a nonsensical 256.0 in 9.23; and the AGL attack default of
`0x000369D0` reproduces exactly as 1.31 for 100 ms at 96 kHz. So if the DRC block
really is 9.23, it differs from the AGL block that shares the helper.

### Other candidates, cheapest first

1. **The DSP rate is not 96 kHz.** `DRC_DEFAULT_DSP_RATE = 96000`; at 48 kHz every τ
   is 2× off. Only a factor of two, so it cannot explain the residual alone, but it
   would be worth knowing and it is easy to check against a measured τ.
2. **Attack and decay swapped in the address map.** Would make the attack 200 ms —
   still ~13× too fast to explain the residual, but it would show up in the same test.
3. **The energy detector**, `DRC_ENERGY_DEFAULT_MS = 2.0`, feeding the level estimate
   rather than the gain smoother. A long window would delay engagement the same way.
4. **Run 12 is something else entirely.** +8.8 dB *above its own control* is a boost,
   which no incomplete attack can produce — the gain would have to overshoot past
   unity. It behaves as though `off2` never reached the register in that pass. It is
   also the run the retracted plain-dB conclusion was built on. Treat it as suspect
   data, not evidence, until something reproduces it.

### The test

`drc-attack.wav`, generated by the new `--toggle` mode, alternates 10 s at −30 dBFS
with 10 s at −6 dBFS, three times:

```
python tools/make_drc_staircase.py --toggle -30 -6 --cycles 3 --seconds 10 \
    --reference -30 --out drc-attack.wav
```

Settings: threshold **−20**, ratio **2**, makeup 0, `DRC Offset 2 Override` at **+1**
(derived), EQ off, volume 90. That puts the quiet blocks below the knee (−33 dBFS RMS)
and the loud blocks well above it (−9 dBFS RMS), so each transition is a real attack
or release edge.

**Read each loud block several times across its ten seconds** — at onset, 1 s, 2 s,
5 s, 9 s — rather than once in the middle. The steady state is already known; what is
being measured here is the trajectory.

| what you see | what it means |
|---|---|
| loud block starts high and settles down over seconds | slow attack confirmed; the time to cover 63% of the drop is τ |
| start and end agree | attack is faster than the block; shorten `--seconds` and repeat |
| loud block *rises* toward the end | release is involved, or the detector is the slow element |
| quiet block starts low and recovers slowly | that is the release, measured the same way |

Then vary one control at a time — attack 0.1 ms versus 500 ms at a fixed everything
else. If the trajectory does not change, the attack register is not reaching the part
or is not being interpreted as a time constant at all, and the next step is a register
dump at both settings to confirm what was actually written.

Expected steady state at those settings, for reference while reading: gain
`−0.5·level − 10`, so **−5.5 dB** on the loud blocks and **0 dB** on the quiet ones.

---

## 5. Also untested

- **Makeup gain.** Goes through a different encoder (`linear_db_to_f9_23`, a linear
  gain rather than a log quantity) and has never been measured. Cheap to fold into any
  future run: set it to 6 dB and check everything moves up 6 dB.
- **Three-band operation.** The write path and crossover coefficients are verified by
  readback, but nothing has ever been listened to or measured at `drc_bands: 3`. The
  trap in reference doc §4 is live: unmuting bands 2 and 3 without programming the
  crossover sums three copies of the full-range signal, about +9.5 dB.
- **Anything about programme material.** Every measurement so far is a 1 kHz sine.
  The piano track that started this whole exercise is the real acceptance test.

---

## 6. Current entities

Fifteen DRC controls (threshold, ratio, attack, release, makeup × 3 bands), a master
`DRC` switch, `DRC Reset Defaults`, `DRC Register Dump`, and:

**`DRC Offset 2 Override`** — diagnostic. Writes a raw value straight to every band's
region 2 offset register, bypassing the derived `off2 = −k·T1`. **`+1` means derived
and is the normal setting.** It exists because `off2 = −k·T1` couples the offset to
the threshold: a small offset forces a high knee with no material above it to measure,
a low knee forces a large offset, and no point in that two-dimensional space gives
both. Fighting that coupling is why three attempts at the offset scale produced three
different answers. `restore_value` is deliberately false, so an override lasts until
the next reboot and no longer.

A previous diagnostic — a `DRC Offset Convention` selector — was removed after it
tripped the amp's protection twice: two of its three options left an offset at zero,
which unanchors that region into a boost, and `restore_value: true` meant a stale
selection survived a reflash. **If you add another diagnostic entity, do not persist
it, and do not give it an option that can make the output louder.**

Note that the YAML in this repository is a *copy* of what runs on the ESPHome
instance. A component change reaches the device through `external_components`
(`refresh: 0s`, sourced from GitHub); an **entity** change has to be pasted into the
device's own YAML by hand.

---

## 7. Where to pick up

1. Reflash to pick up `ecc9613`, which restored the offset scale. The DRC should now
   be transparent below the knee and follow `k·(level − T)` above it.
2. Listen to the piano track at threshold −20, ratio 2. That is the acceptance test
   for everything measured so far.
3. Run the attack test in §4. It is the largest unknown and the one most likely to
   hide a real bug.
4. Makeup gain, then three-band.
