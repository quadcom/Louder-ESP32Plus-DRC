# TAS5805M / TAS5825M DRC — Verified Reference

Supersedes `tas5805m-drc-handoff.md`. That document is correct **for the TAS5805M**
but was written against the wrong part for the Louder ESP32 Plus. See §1.

Everything below is either quoted from a TI document (cited inline) or verified
numerically in `tools/drc_math_check.py`, which reproduces every documented
default bit-exactly. Items still needing hardware confirmation are marked
**[UNVERIFIED]**.

---

## 1. Which part is on which board

| Board | Amp | I²C addr | DSP map source |
|---|---|---|---|
| Louder-ESP32, Louder-ESP32-Mini | TAS5805M | 0x2D | SLOA263A |
| **Louder-ESP32-Plus, Louder-ESP32-Pro** | **TAS5825M** | **0x4C** | **SLAA786A** |

Confirmed from `hardware/7-louder-esp32-plus/**/*-bom.csv` (`TAS5825MRHBR`, U9),
all three Louder-Plus schematics, and `tas58xx_variant: TAS5825M` in every
`7-louder-esp32-plus/*.yaml`.

The two parts' DSP maps are **not** compatible. Same feature, different address:

| | TAS5805M | TAS5825M |
|---|---|---|
| Mixer gain | bk 0x8C / pg 0x29 / 0x18 | bk 0x8C / pg 0x0B / 0x14 |
| Channel volume | pg 0x2A / 0x24, 0x28 | pg 0x0B / 0x0C, 0x10 |
| EQ biquads | bk 0xAA / pg 0x24–0x28 | bk 0xAA / pg 0x01–0x06 |
| EQ enable | reg 0x66 bitfield | coeff write, bk 0xAA pg 0x0B / 0x28, 0x2C |

What *does* carry over from the handoff unchanged: the no-NVM premise, the
"PPC3 is not required" conclusion, the bring-up order, the page-0-before-book
rule, and the fixed-point scale factors.

### Which process flow

The Louder Plus runs **Process Flow 1 (Base/Pro, 96 kHz, 2.0)**, which includes a
3-band DRC with a 4th-order crossover (SLAA786A Table 2). Two independent
confirmations:

- `tas58xx_minimal.h` writes no DSP coefficients at all — only register tuning
  plus `{0x46, 0x01}` ("undocumented SR 96KHz"), so the DSP runs its default
  ROM flow.
- Every TAS5825M address already working in the component (mixer, volume, gang/
  bypass EQ, all 30 EQ biquads) matches SLAA786A Table 9 exactly. The DRC
  addresses below come from that same table, so they carry the same confidence
  as the shipping EQ code.

---

## 2. DRC memory map

Ten parameters per band, plus three band mixer gains. All in **book 0x8C**.
Reach any of them with the standard navigation:

```
write 0x00, 0x00      ; page 0 first — non-negotiable
write 0x7F, 0x8C      ; book
write 0x00, <page>    ; page
write <offset>, b3, b2, b1, b0   ; 4-byte burst, MSB first
```

### TAS5825M — Process Flow 1 (SLAA786A Table 9, pp. 65–66)

| Parameter | Format | DRC1 (low) | DRC2 (mid) | DRC3 (high) |
|---|---|---|---|---|
| Mixer gain | 9.23 | p06 / 0x58 | p06 / 0x5C | p06 / 0x60 |
| Energy | 1.31 | p06 / 0x64 | p07 / 0x14 | p07 / 0x3C |
| Attack | 1.31 | p06 / 0x68 | p07 / 0x18 | p07 / 0x40 |
| Decay | 1.31 | p06 / 0x6C | p07 / 0x1C | p07 / 0x44 |
| K0 (region 1 slope) | 9.23 | p06 / 0x70 | p07 / 0x20 | p07 / 0x48 |
| K1 (region 2 slope) | 9.23 | p06 / 0x74 | p07 / 0x24 | p07 / 0x4C |
| K2 (region 3 slope) | 9.23 | p06 / 0x78 | p07 / 0x28 | p07 / 0x50 |
| T1 (threshold 1) | 9.23 | p06 / 0x7C | p07 / 0x2C | p07 / 0x54 |
| T2 (threshold 2) | 9.23 | p07 / 0x08 | p07 / 0x30 | p07 / 0x58 |
| Off1 | 9.23 | p07 / 0x0C | p07 / 0x34 | p07 / 0x5C |
| Off2 | 9.23 | p07 / 0x10 | p07 / 0x38 | p07 / 0x60 |

Note DRC1 straddles the p06→p07 boundary, so a single-band update is two page
switches, not one. Bands 2 and 3 each sit entirely on page 0x07.

### TAS5805M (SLOA263A Table 5, pp. 20–21)

Verified against the source; matches the original handoff §4 exactly.

| Parameter | Format | DRC1 (low) | DRC2 (mid) | DRC3 (high) |
|---|---|---|---|---|
| Mixer gain | 9.23 | p2E / 0x08 | p2E / 0x0C | p2E / 0x10 |
| Energy | 1.31 | p2B / 0x34 | p2D / 0x30 | p2D / 0x58 |
| Attack | 1.31 | p2B / 0x38 | p2D / 0x34 | p2D / 0x5C |
| Decay | 1.31 | p2B / 0x3C | p2D / 0x38 | p2D / 0x60 |
| K0 | 9.23 | p2B / 0x40 | p2D / 0x3C | p2D / 0x64 |
| K1 | 9.23 | p2B / 0x44 | p2D / 0x40 | p2D / 0x68 |
| K2 | 9.23 | p2B / 0x48 | p2D / 0x44 | p2D / 0x6C |
| T1 | 9.23 | p2B / 0x4C | p2D / 0x48 | p2D / 0x70 |
| T2 | 9.23 | p2B / 0x50 | p2D / 0x4C | p2D / 0x74 |
| Off1 | 9.23 | p2B / 0x54 | p2D / 0x50 | p2D / 0x78 |
| Off2 | 9.23 | p2B / 0x58 | p2D / 0x54 | p2D / 0x7C |

**Documentation errata — present in both documents.** The region-3 slope of band 3
is printed as `k1_3`, duplicating the region-2 name. The DESCRIPTION column
disambiguates it ("DRC3 Region 3 Slope"), so it is **K2_3**:

- SLAA786A p.66: page 0x07 offset **0x50** labelled `k1_3`, described as region 3
- SLOA263A p.21: page 0x2D offset **0x6C** labelled `k1_3`, described as region 3

The original handoff flagged this for the 5805M; it recurs identically in the
5825M doc, which is good evidence it is a copy-paste error in TI's table
generator rather than a real address collision.

**Sub-channel DRC.** Only the TAS5805M 2.1/48 kHz flow has an extra `CH-Sub DRC1`
block (SLOA263A Table 8: pg 0x2B / 0x5C–0x7C, continuing pg 0x2C / 0x08). It does
not exist in the 2.0 flows or on the 5825M PF1. This answers the handoff's open
question about the sub-channel block.

### Reset state

Identical on both parts:

- Energy / Attack / Decay = `0x7FFFFFFF` → α = 1.0 → instantaneous, no smoothing
- K0/K1/K2 = 0 and Off1/Off2 = 0 → flat gain curve → **DRC is a no-op**
- T1 = `0xE7000000` = −50.0 dB, T2 = `0xFE800000` = −3.0 dB
- Mixer: band 1 unity (`0x00800000`), bands 2 and 3 muted (`0x00000000`)

So out of reset the full-range signal passes through band 1 with no compression.

---

## 3. Fixed-point encoding

32-bit two's complement, big-endian on the wire.

| Format | Scale | Used for |
|---|---|---|
| 1.31 | 2³¹ | time constants (energy/attack/decay), 5805M crossover B0/B2/A2 |
| 2.30 | 2³⁰ | 5805M crossover B1/A1 |
| 5.27 | 2²⁷ | EQ biquads (both parts), 5825M crossover biquads |
| 9.23 | 2²³ | K slopes, thresholds, offsets, mixer/band gains, volume |

### Thresholds and offsets — plain dB in 9.23

```
raw = round(dB * 8388608)
```

Verified: `0xE7000000` / 2²³ = −50.000000 and `0xFE800000` / 2²³ = −3.000000,
matching the documented T1/T2 defaults exactly on both parts.

> **Do not apply SLOA148 §5.1** (`Tdb = 6.0206·T_entry + 24 dB`). That scaling is
> for the older TAS57xx generation. If it applied here, the −50 dB default would
> not encode as `0xE7000000`. Same for SLOA148 §5.3's offset formula
> (`10^(gd/20)/15.5`) — offsets on these parts are plain dB, default 0 = no change.

### Time constants — 1.31 alphas

**Correction to the handoff §6.** The handoff specifies the exponential form
`alpha = 1 - exp(-1/(fs*tau))`. TI's own defaults are generated with the simple
reciprocal:

```
alpha = 1 / (fs * tau_seconds)      clamped to [1, 0x7FFFFFFF]
raw   = round(alpha * 2147483648)
```

Evidence — the AGL defaults, which are byte-identical on both parts
(`0x000369D0` attack, `0x00005762` release):

| | reciprocal form | exponential form |
|---|---|---|
| 100 ms @ 96 kHz | `0x000369D0` ✓ exact | `0x000369C5` ✗ off by 11 |
| 1 s @ 96 kHz | `0x00005762` ✓ exact | `0x00005762` ✓ |

Two further checks: AGL energy `0x051EB852` = α 0.040000000 exactly (25 samples),
and its companion AGL omega `0x7AE147AE` = 0.96 = 1 − 0.04. Clean decimals, which
is what the reciprocal form produces and the exponential form does not.

The two agree to <0.01% for τ ≫ 1/fs, so this is not audible — but the reciprocal
form round-trips against readback bit-exactly, which matters for verifying writes.

**fs is the DSP internal rate, not the I²S input rate.** PF1 upconverts via SRC to
88.2/96 kHz. Read `FS_MON` (book 0, 0x37) and pick accordingly or attack/release
land up to 2× off.

### K slopes — ratio mapping

From SLOA148 §5.2, which describes this same DRC structure:

> For a 1:n expansion, the slope k can be found by : k = n – 1
> For an n:1 compression, the slope k can be found by: k = 1/n − 1
> In compression (n:1), n is implied to be greater than 1. For compression,
> Equation 5 means –1 < k < 0 for n > 1. Thus k must always lie in the range k > –1.

So for an **n:1 compressor**:

```
k = 1/ratio - 1          ratio 1 -> k = 0     (1:1, no compression)
                         ratio 2 -> k = -0.5
                         ratio 4 -> k = -0.75
                         ratio -> inf: k -> -1 (limiter)
```

This is self-consistent with the documented default K = 0 meaning "no gain
change". Keep `k > -1`; values ≤ −1 invert the transfer curve (SLOA148's k = −4
illustration, where output *falls* 3 dB per dB of input rise).

K is the slope of the **gain-adjustment** curve, not an input-vs-output curve.

### Offsets — **the part does not subtract the threshold**

SLOA148: "Offsets O1 and O2 define, in dB, the attenuation (cut) or gain (boost)
applied by the DRC-derived gain coefficient at the threshold points T1 and T2."

That reads as "an offset is the value of the gain curve *at* its threshold",
which for a single-knee compressor unity below the knee gives `off1 = 0` and, so
that region 3 continues the region 2 line, `off2 = k1 * (T2 - T1)`. It is
self-consistent, it matches the all-zero defaults being flat, and **it is
wrong.**

**Disproved on hardware, TAS5825M, 2026-08-02.** With `drc_bands: 1`,
threshold −20 dB, ratio 2:1, makeup 0 dB, a quiet solo piano track — RMS far
below the knee, where a compressor is required to do nothing whatsoever — lost
roughly 10 dB. Raising the threshold to −7 dB brought most of it back. So the
loss tracked the *threshold*, not the *signal level*, and that is the signature
of a constant offset, not of a gain curve. The arithmetic closes:

```
off2 = k * (T2 - T1) = -0.5 * (-1 - -20) = -9.5 dB
```

**Zero is wrong too, and that identified the real convention.** Writing zero to
both offsets and going back to threshold −20 dB / ratio 2:1 / makeup 0 dB made
the output **louder**, not quieter — confirmed against a fresh reset, with the
ratio as the only variable.

That is decisive, because with zero offsets the only remaining term is the
region slope, and `k · (x − T1)` cannot be positive above the threshold: k is
negative for compression and `(x − T1)` is positive. A boost is only possible if
the part never subtracts the threshold at all. So the region equation is

```
gain = K · x + O
```

with T1 and T2 doing nothing but selecting which region's K and O to use. **The
offsets are y-intercepts**, and they are what places the knee. That also
explains why the registers exist: under the "value at the threshold" reading a
single-knee compressor would leave both at zero permanently, which is not a
register TI would provide.

For a compressor that is unity below T1 and slope k above it, with regions 2 and
3 sharing that slope:

```
O1 = O2 = -k · T1        threshold -20, ratio 2  ->  -10 dB
```

| region | condition | gain |
|---|---|---|
| 1 | x < T1 | `K0 · x` = 0 |
| 2 | T1 ≤ x < T2 | `k · x - k · T1` → 0 dB at T1 |
| 3 | x ≥ T2 | same line, so continuous through T2 |

For a while all three candidate conventions were runtime-selectable via a
`DRC Offset Convention` select entity, because settling this by ear one rebuild at
a time was costing a flash cycle per hypothesis. **That selector is gone as of
2026-08-03.** Two of its three options left an offset at zero, and under
`gain = k·x + O` a zero offset unanchors its region into a boost of `−k·x` — which
tripped the amp's protection twice on the bench. Intercept is now the only
behaviour, computed unconditionally.

One loose end. The intercept model predicts region 1 is flat regardless of the
offsets, so it does **not** explain the original quiet-piano loss — under
`O1 = 0, O2 = −9.5` a signal below the knee should have been untouched. Either
that track's RMS was higher than it sounded and it was sitting in region 2, or
region 1 picks up an offset as well. The distinguishing test is below; if
Intercept leaves quiet material transparent, the question is moot.

Still open: the **K-slope ↔ ratio mapping**. `k = 1/ratio − 1` is from SLOA148
and matches the documented defaults, but no measurement confirms the *amount* of
compression on this part. That needs a level sweep against measured gain
reduction.

#### Test procedure

`drc_bands: 1`, threshold −20 dB, ratio 2:1, makeup 0 dB, and material whose RMS
sits below −20 dBFS — quiet solo piano is ideal. A compressor is required to do
nothing at all there, so:

| convention | quiet material | predicted |
|---|---|---|
| Intercept | unchanged | correct |
| Zero | louder | boost of `−k · x`, about +7 dB at −15 dBFS |
| Continuity | quieter | constant cut of `k · (T2 − T1)`, −9.5 dB |

Then confirm on loud material: peaks should pull down a few dB and nothing else.

#### Measured 2026-08-03 — Intercept mutes the output

First real measurement, per `docs/drc-measurement.md`: UMIK-1 on a stand, REW SPL
meter, Z weighting, room noise floor 49 dB. 1 kHz stepped tone, six plateaus of
6 dB. `drc_bands: 1`, threshold −20 dB, ratio 2, makeup 0 dB, Intercept.

| plateau | Run 1, DRC off | gap | Run 2, DRC on | implied loss |
|---|---|---|---|---|
| −6 dBFS | 90.8 | | noise floor | ≥ 42.8 |
| −12 dBFS | 85.3 | 5.5 | noise floor | ≥ 37.3 |
| −18 dBFS | 79.4 | 5.9 | noise floor | ≥ 31.4 |
| −24 dBFS | 73.6 | 5.8 | noise floor | ≥ 25.6 |
| −30 dBFS | 67.5 | 6.1 | noise floor | ≥ 19.5 |
| −36 dBFS | 62.0 | 5.5 | noise floor | ≥ 14.0 |

Run 1 validates the chain: the middle three gaps average **5.93 dB** against a
target of 6.00. The two end gaps are the low ones — speaker compression at the
top (90.8 dB SPL), room noise at the bottom — so the trustworthy window is −12 to
−30 dBFS, which brackets the threshold.

Run 2 put **every plateau into the noise floor**, reading 49–51 dB against a 49 dB
room. This is the same failure as the original report that the sound cut out, now
with a number on it.

Two things follow, and both contradict the model above:

**No region is flat.** The −24, −30 and −36 plateaus are below the −20 dB
threshold. Every convention predicts gain = 0 there, so they should have matched
Run 1 exactly. They lost at least 14–26 dB. Whatever region the signal is in, it
is the same region at −36 dBFS as at −6 dBFS — so the threshold comparison is not
selecting regions the way the table above assumes.

**The offset is being scaled up by at least 4.6×.** Take a single global
`gain = k·x − A`. The loss is then `0.5x + A`, which is 15 dB greater at the −6
plateau than at the −36 plateau; the measured lower bounds differ by 28.8 dB, so
that shape is consistent. Burying every plateau needs `A ≥ 46 dB`. Intercept wrote
`−k·T1 = −10`. So the offset registers are not in dB the way the thresholds are:

```
A_actual / A_intended ≥ 4.6
```

`db_to_f9_23` writes the dB figure straight into 9.23, which is right for
thresholds — TI's own reset values decode exactly that way, `0xE7000000` = −50.0
and `0xFE800000` = −3.0, both clean round dB. The offsets take the same encoding
and evidently should not. **6.0206 dB per unit (log2) is the obvious suspect**,
since a DSP tracking log2 of the level gets the exponent for free: it would make
the −10 into −60.2 dB, and `k·x` would be unaffected because k is dimensionless.
That last point is why every ratio-only change behaved sensibly while every
attempt to add an offset overshot into silence. The measurement only bounds the
scale from below, though — 10 and 20 are not yet excluded.

#### Measured 2026-08-03, run 3 — the slope is doubled

The `Zero` run never got past its lead-in tone: the amp clipped and tripped
protection. The reference tone measured **98 dB** on the way there, against
**79.4 dB** for the same −18 dBFS level with the DRC off. A **+18.6 dB boost**
where `gain = k·x` at `k = −0.5` predicts +9.0 dB.

Exactly double, to within 0.6 dB. And `gain = −1.0 × level` means
`output = level − level = 0 dBFS` for *every* input, which is why it clipped
immediately and why a quieter test file would not have helped.

#### The log domain, and the correction

One model accounts for all three anomalies. The detector tracks **log2 of the
mean square**, and the gain comes back as a power-of-two multiplier on amplitude:

```
u       = log2(P)      = level_dB / 3.0103      (10·log10 2)
gain_dB = 6.0206 · gain_u                       (20·log10 2)
gain_u  = k·u + O
     ⇒  gain_dB = 2·k·level_dB + 6.0206·O
```

| symptom | explanation |
|---|---|
| slope acts twice as hard | the power-to-amplitude conversion, `2·k` |
| −10 offset cost ≥46 dB | `6.0206 × 10` = 60.2 dB |
| threshold never engaged | −20 written raw puts the knee at −60 dB |

`k` is the only dimensionless one of the three, which is why every ratio-only
experiment behaved plausibly while every offset overshot into silence.

So each dB quantity is divided by the dB-per-unit of whichever side of that
equation it lands on — `DRC_THRESHOLD_DB_PER_UNIT`, `DRC_OFFSET_DB_PER_UNIT` and
`DRC_SLOPE_POWER_FACTOR` in `tas58xx_drc.h`. `db_to_f9_23` was renamed
`log_units_to_f9_23`; the arithmetic is unchanged, the name is no longer a lie.

At threshold −20, ratio 2, Intercept the part now gets `k = −0.25`
(`0xFFE00000`), `T1 = −6.6439` (`0xFCAD9620`), `off = −1.6610` (`0xFF2B6588`),
which is `gain = −0.5·level − 10.0` — zero at the −20 threshold and −7 dB at
−6 dBFS, matching the ideal `k·(level − T)` at every level.

**A correction to what is written above.** Earlier in this section TI's reset
values are used to argue thresholds are plain dB, on the grounds that
`0xE7000000` decodes to −50.0 and `0xFE800000` to −3.0. That inference is void:
those are round numbers in the *raw* domain whatever the raw domain means, and TI
ships them with every slope at zero, so they never had to be correct in dB. In
the log2 reading they are a threshold at −150 dB (never reached) and one at
−9.03 dB. The measurement outranks the inference.

#### Measured 2026-08-03, run 4 — a knee, in the right place

Same rig, corrected firmware, threshold −20 dB, ratio 2, makeup 0. The debug log
confirms the writes landed exactly as intended:

```
Set Low band DRC: -20.0dB 2.0:1 attack 10.0ms release 200ms
  (k=-0.5000 off1=-10.00dB off2=-10.00dB)
  raw: k=-0.2500 t1=-6.6439 t2=-0.3322 off1=-1.6610 off2=-1.6610
```

so everything below is the part's interpretation, not an encoding slip.

| plateau (peak) | RMS | Run 1 off | run 4 | gap | gain, noise-corrected |
|---|---|---|---|---|---|
| −6 dBFS | −9.01 | 90.8 | 90.6 | | −0.20 |
| −12 dBFS | −15.01 | 85.3 | 86.2 | 4.40 | +0.90 |
| −18 dBFS | −21.01 | 79.4 | 72.2 | **14.00** | −7.22 |
| −24 dBFS | −27.01 | 73.6 | 66.7 | 5.50 | −6.96 |
| −30 dBFS | −33.01 | 67.5 | 61.0 | 5.70 | −6.72 |
| −36 dBFS | −39.01 | 62.0 | 56.0 | 5.00 | −6.74 |

**The threshold works, and it is RMS-referenced.** A 14 dB step between the −12
and −18 plateaus, against a 6 dB input step, is a knee — the first one this part
has ever shown. It sits between −15.01 and −21.01 dBFS RMS, which brackets the
−20 dB that was dialled in. So `DRC_THRESHOLD_DB_PER_UNIT = 3.0103` is right, and
the detector measures RMS: for a sine the knee appears 3.01 dB above the
threshold in peak terms, at −17 dBFS.

**Region 1's slope is right, its offset is not.** Below the knee the gain must be
zero and instead it is a flat cut. Corrected for the 49 dB room floor the four
plateaus give −7.22, −6.96, −6.72, −6.74 — constant within 0.5 dB over 18 dB of
input, which confirms `k0 = 0` is landing. A constant is an offset, so **off1 is
being applied to the region below the knee**, where the code assumes no offset
applies at all. That also accounts for the 14 dB knee: about 7 dB of it is this
cut, and the rest is the genuine step.

**The offset scale is not `20·log10 2` either.** `off1 = −1.6610` raw was meant to
be −10.00 dB and measures −6.91, giving **≈4.16 dB per unit**, not 6.0206. That is
also consistent with run 2 burying the staircase, but is not a clean constant, and
6 plateaus of SPL cannot resolve it further.

Above the knee there are only two plateaus, and the −6 dBFS one is where the
speaker itself compresses, so the slope there is not yet measured. With
`S = 4.16` the −12 plateau predicts +0.6 dB against +0.9 observed, which is
encouraging and nothing more.

#### Measured 2026-08-03, run 5 — off1 belongs to region 1

Threshold −2 dB, ratio 2, so the knee sits above full scale and all six plateaus
fall in region 1. `off1 = off2 = −0.1661` raw, intended −1.00 dB.

**A control run matters more than it looked.** An accidental ratio-1 run turned
out to be the most useful measurement of the day. At ratio 1 the slope
short-circuits to zero and the offsets follow, so the DRC writes coefficients
*identical to the bypass path* — same zero slopes, same zero offsets, and the same
unity mixer gain, since `DRC_MIXER_UNITY` is `0x00800000` and 0 dB of makeup
encodes to the same 1.0. It therefore cannot differ from DRC-off, yet it measured
**2.55 dB louder** than run 1. That is drift between sessions, not gain, and it
means run 1 is not a safe baseline for anything measured later. Referenced against
the ratio-1 control instead:

| plateau | run 4, T = −20 | run 5, T = −2 |
|---|---|---|
| −6 dBFS | −2.90 | −1.60 |
| −12 dBFS | −1.40 | −1.60 |
| −18 dBFS | −9.60 | −1.70 |
| −24 dBFS | −9.40 | −1.70 |
| −30 dBFS | −9.30 | −1.60 |
| −36 dBFS | −8.60 | −1.70 |

Run 5 is flat to **0.10 dB across all six plateaus** and shows no knee, which is
correct for a signal entirely inside region 1 — and confirms again that `k0 = 0`
lands. But region 1 is not transparent: it is a constant cut, and the cut tracks
the offset.

**The offset scale of `20·log10 2` was right; the register mapping was not.**
Three independent estimates of the scale:

| measurement | offset written | effect | implied dB/unit |
|---|---|---|---|
| run 4, below knee | −1.6610 | −9.22 dB | 5.55 |
| run 4, knee discontinuity | −1.6610 | 10.50 dB | 6.32 |
| run 5, all plateaus | −0.1661 | −1.65 dB | 9.93 |

The first two bracket 6.0206 and need no change. The third cannot resolve
anything: the run-to-run scatter is around 0.7 dB, which swamps a 1 dB signal, so
that 9.93 carries no weight. The knee figure is the strongest of the three because
it is baseline-free — with the knee at −20 dBFS RMS, the −12 dBFS plateau sits
4.99 dB above it and the −18 dBFS plateau 1.01 dB below, so a correct 2:1 curve
gives `4.99/2 + 1.01 = 3.50 dB` between them. The measured 14.00 dB leaves a
**10.50 dB discontinuity** against an offset of −10.00 dB.

So −10 dB is landing, at the right size, in the wrong place. **`off1` governs
region 1, below the knee — not region 2.** The fix is `off1 = 0`, leaving
`off2 = −k·T1` to anchor the knee. That is both symptoms at once: the flat 9.2 dB
cut on quiet material and the 10.5 dB step at the knee.

It also, finally, explains the original report that started all of this — a quiet
piano track losing most of its level at threshold −20 with ratio 2. Region 1 was
being attenuated by the full `−k·T1`, and quiet material lives entirely in
region 1.

**Still unmeasured: the slope above the knee.** Every run so far has had at most
two plateaus in region 2, one of them the −6 dBFS plateau where the speaker itself
compresses. Run 4's single usable gap there was 4.4 dB against 2.9 dB predicted,
which hints the compression is weaker than asked for, but one gap is not a
measurement. A threshold of −40 dB puts eight plateaus above the knee and reads
the ratio off their spacing directly.

**And capture a DRC-off control in the same session as every future run**, given
2.55 dB of drift turned up between two that were assumed comparable.

#### Measured 2026-08-03, runs 6 and 7 — region 1 is fixed, the slope constant is not 2

`off1 = 0`, `off2 = −k·T1`, ratio 2, makeup 0.

**Run 6, threshold −20 dB — region 1 is transparent.** Against the ratio-1
control:

| plateau | run 6 | control | Δ | |
|---|---|---|---|---|
| −6 dBFS | 90.7 | 93.5 | −2.80 | above knee |
| −12 dBFS | 86.2 | 87.6 | −1.40 | above knee |
| −18 dBFS | 81.5 | 81.8 | **−0.30** | below knee |
| −24 dBFS | 75.8 | 76.1 | **−0.30** | below knee |
| −30 dBFS | 70.0 | 70.3 | **−0.30** | below knee |
| −36 dBFS | 64.5 | 64.6 | **−0.10** | below knee |

Below the knee the DRC is now transparent to 0.3 dB, against 9.2 dB of cut before.
The 14 dB discontinuity is gone too — the gap across the knee is 4.7 dB, in line
with the neighbouring 4.5 and 5.7. `off1` is settled.

**Run 7, threshold −40 dB — the slope, at last.** With the knee at −40 dBFS RMS
every plateau sits in region 2, so all five gaps measure the slope and nothing
else. The whole run sat between 60 and 81 dB SPL, clear of the room floor and well
below where the speaker compresses, which makes it the cleanest data in this file:

| step | gap | implied ratio |
|---|---|---|
| −6 → −12 dBFS | 4.40 | 1.362 |
| −12 → −18 dBFS | 4.11 | 1.459 |
| −18 → −24 dBFS | 4.23 | 1.418 |
| −24 → −30 dBFS | 4.29 | 1.400 |
| −30 → −36 dBFS | 4.32 | 1.388 |
| **mean** | **4.271 ± 0.098** | **1.405** |

Asked for 2.000. So the slope acts, linearly and consistently, but at about
70% of the requested strength:

```
output slope  0.7119   (want 0.5000)
gain slope   -0.2881   (want -0.5000)   with k written as -0.2500
⇒ 1.152 ± 0.065 dB of gain per dB of level, per unit of slope register
```

**The log2 model predicted 2.0 for that number.** It got the threshold and offset
scales right and this one wrong, so the slope and offset domains are not related
the way it claims. `DRC_SLOPE_GAIN_PER_UNIT = 1.152f` is therefore empirical and
provisional — the least certain constant in the component, and the only one not
derived from anything.

Its own prediction is easy to check. At the corrected `k = −0.4340` the same run
should give **3.00 dB** steps; the model's 2.0 would give 3.40, and no scaling at
all 4.50. Those are far enough apart to separate at ±0.1 dB.

Also worth noting what run 6 says about **makeup**: nothing yet. Every measurement
so far has been at 0 dB. Once the ratio confirms, a run at some non-zero makeup
would check that the mixer's `linear_db_to_f9_23` path is as clean as the rest.

#### Measured 2026-08-03, run 8 — the slope needs no scaling

Threshold −40 dB, ratio 2, `k = −0.4340` written. Mean gap **3.30 dB** against
run 7's 4.20, so the ratio moved from 1.38 to **1.78 ± 0.10**.

**Read gaps against the control, never against the file's nominal step.** The
staircase steps 6.00 dB and this room delivers **5.78**. Dividing by 6.00 inflates
every slope estimate by 4%, and that — not the part — is where the previous
entry's `1.152` came from. Corrected:

| run | k written | mean gap | ratio vs control | dB gain per dB level, per unit |
|---|---|---|---|---|
| 7 | −0.2500 | 4.20 | 1.377 ± 0.011 | 1.093 |
| 8 | −0.4340 | 3.30 | 1.778 ± 0.102 | 0.989 |

Two independent runs, an octave apart in `k`, both give **1.0**. So
`k = 1/ratio − 1` goes to the register **unscaled**, exactly as SLOA148 says, and
neither the factor of 2 nor the 1.152 was ever real. At `k = −0.5` the predicted
gap is 2.89 dB, i.e. ratio 2.00.

**A new problem, visible only in the absolute levels.** Run 8's gain against the
control, with the threshold at −40 dBFS RMS:

| plateau | RMS | gain |
|---|---|---|
| −6 dBFS | −9.01 | −7.40 |
| −12 dBFS | −15.01 | −4.80 |
| −18 dBFS | −21.01 | −2.10 |
| −24 dBFS | −27.01 | −0.10 |
| −30 dBFS | −33.01 | **+3.00** |
| −36 dBFS | −39.01 | **+5.00** |

The −36 dBFS plateau sits 0.99 dB above the threshold, where the gain must be
essentially zero. It is **+5.0 dB**. The offset is under-anchoring badly: −20.00 dB
was asked for and roughly −11.6 dB arrived.

Worse, the offset scale is not constant. `off = −1.6610` produced 9.2–10.5 dB in
runs 4 and 6; `off = −3.3219` produces about 11.6 dB here. Doubling the register
bought 26% more effect, so **no single dB-per-unit constant can anchor the knee at
every threshold**, and `DRC_OFFSET_DB_PER_UNIT` is downgraded to provisional.

**The measurement that isolates it:** threshold −20 and threshold −40 at the same
ratio on the same firmware. The slope term is then identical between the two runs
and only the offset differs — by −10.00 dB as written. Whatever the measured
difference is at a fixed input level above both knees is the offset's true
authority, with the slope, the room and the speaker all cancelling.

**Confirming the live config:** the DRC debug line only prints when a setting
actually changes, so after a reboot that restores the same values there is nothing
in the log. Press **DRC Register Dump** instead — it is read-only and reports what
is actually in the part.

#### Measured 2026-08-03, runs 9 and 10 — ratio 2.09 delivered

Unscaled slope, `k = −0.5000`, ratio 2, makeup 0.

| run | threshold | gaps | mean |
|---|---|---|---|
| 9 | −40 dB | 2.4, 3.5, 2.3, 3.5, 2.2 | **2.780** |
| 10 | −20 dB | 3.1, **12.9**, 5.8, 5.8, 5.8 | — |

**Run 10 supplies its own control.** Its three plateaus below the knee step
**5.80 dB**, matching the 5.78 measured in a separate session. Spacing is stable
across sessions and immune to both EQ state and level, which makes it the only
quantity here worth trusting without a same-session baseline.

**The ratio is confirmed.** Run 9 held every plateau above the knee, so all five
gaps measure the slope: `5.80 / 2.780 = ` **2.086** against 2.000 requested. Run 10's
single above-knee gap gives 1.87. The slope, at last, is right.

**But the offset does not anchor the knee at this slope.** Extrapolating run 10's
own transparent region upwards, baseline-free:

| plateau | transparent | correct curve | measured | error |
|---|---|---|---|---|
| −12 dBFS | 85.60 | 83.10 | 92.70 | **+9.60** |
| −6 dBFS | 91.40 | 85.90 | 95.80 | **+9.90** |

A boost of nearly 10 dB where a cut of 2.5–5.5 dB is due, consistent across both
plateaus. Yet **run 6 anchored the same knee to within 0.25 dB** with the identical
`off2 = −1.6610` register value, at half the slope. So the offset's authority is
not simply proportional to what is written, and there is a term still unaccounted
for.

**Why this is not yet conclusive:** run 10's two above-knee plateaus sat at 92.7
and 95.8 dB SPL, above the ~90 dB where run 1 showed the speaker compressing. That
can only make a reading *low*, so the electrical excess is at least this large —
but the run cannot be used to fit a constant. **Repeat threshold −20 about 10 dB
quieter, with a ratio-1 control at the same volume in the same session.** Until
then treat threshold −20 at high volume as capable of boosting, which is how the
amp's protection tripped twice before.

Two other things the session established, both about method rather than the part:

- **EQ re-enables itself on reboot** on this board. It is a constant offset at
  1 kHz, so it corrupts every cross-run absolute comparison while leaving spacing
  untouched. It is the most likely source of the unexplained 2.55 dB between run 1
  and the later runs. Turn it off before each run and re-check after any reboot.
- **Clearing the build directory does not reset the device's stored settings.**
  They survive a clean build and reflash; only the entity set changing can
  invalidate them.

#### Verified 2026-08-03 — the write path, by readback

A register dump at threshold −35, ratio 2 returns all seven Low band
coefficients bit-exact against what the code intends:

| reg | read | decoded | |
|---|---|---|---|
| k0 | `0x00000000` | 0.00000 | |
| k1 | `0xFFC00000` | −0.50000 | ratio 2, unscaled |
| k2 | `0xFFC00000` | −0.50000 | |
| t1 | `0xFA2FC6B8` | −11.62675 | −35 dB ÷ 10·log10 2, one LSB of rounding |
| t2 | `0xFFD57AB5` | −0.33219 | |
| off1 | `0x00000000` | 0.00000 | region 1 transparent |
| off2 | `0xFE8BF1AE` | −2.90669 | −17.5 dB ÷ 20·log10 2 |

So the write path is verified end to end, and everything still unexplained is the
part's interpretation. It also kills the theory that `off2` was never arriving,
which run 10's numbers had fitted to within 0.4 dB.

#### Measured 2026-08-03, run 11 — compression 10 dB below the threshold

Same settings as the dump above, on the 3 dB-step file. It compresses **uniformly
across its whole range**: 1.53 dB per step above the nominal knee, 1.67 below,
where transparent is 2.89. Compression continues a full 10 dB below the −35 dB
threshold. The overall 1.571 dB/step gives ratio **1.84**, a third confirmation of
the slope alongside 2.09 and 1.87.

**This contradicts run 10**, which put the knee where a −20 dB threshold predicts
and was transparent below it to 0.02 dB per step. No single threshold scale fits
both — run 10 bounds it to 2.26–3.16 dB per unit, this run needs more than 3.87.

**And the measurement is now the limiting factor, not the part.** Five error
sources, all confirmed rather than suspected:

| source | evidence |
|---|---|
| EQ re-enables on reboot | observed on the device; a constant 1 kHz offset |
| speaker compresses above ~90 dB | run 1's end gaps, run 10's 95.8 dB |
| reading resolution ~1 dB | 81, 79, 78, 75, 73, 72, 70 |
| room floor 49 dB | caps how quiet the below-knee plateaus can go |
| cross-session drift | 2.55 dB between the control and run 1 |

At 3 dB steps the transparent-versus-2:1 difference is only 1.44 dB, inside that
budget, so **3 dB steps were the wrong choice** — 6 dB steps are matched to what
the test rig resolves. Before fitting any more constants, the protocol needs
tightening: see the RTA note in `docs/drc-measurement.md`.

#### Measured 2026-08-03, run 12 — the first properly controlled run

Threshold −20, ratio 2, makeup 0, 6 dB-step file, **with a ratio-1 control taken in
the same session at the same volume** — the first pair in the series to rule out
both cross-session drift and the file-versus-room step error.

| block | RMS | control | DRC on | gain |
|---|---|---|---|---|
| −6 | −9.01 | 89.7 | 93.0 | **+3.3** |
| −12 | −15.01 | 83.9 | 90.4 | **+6.5** |
| −18 | −21.01 | 78.1 | 77.4 | −0.7 |
| −24 | −27.01 | 72.2 | 71.6 | −0.6 |
| −30 | −33.01 | 66.5 | 65.5 | −1.0 |
| −36 | −39.01 | 60.7 | 60.0 | −0.7 |

The control's gaps are 5.8/5.8/5.9/5.7/5.8, mean **5.800** — and the below-knee
plateaus step 5.800 too, within 0.7 dB of the control absolutely. Region 1 is
transparent, confirming the `off1` fix.

Two things follow from the above-knee pair. The region **boosts**, which proves the
part does not subtract the threshold itself: a threshold-subtracting part would cut
5.5 dB here. And the fit `gain = −0.533·level − 1.51` against a register holding
−1.6610 gives **0.91 dB per unit**, which was read as plain dB.

**That reading was wrong, and run 13 shows why.** Both above-knee points sit at 93.0
and 90.4 dB SPL, inside the speaker's compression region, with the control's top
point at 89.7 on the same edge — the three least trustworthy readings in the table
carrying the entire conclusion.

#### Measured 2026-08-03, run 13 — plain dB over-attenuates by ~30 dB

Same settings, after the offsets were switched to plain dB (`off2` register −10.00
instead of −1.6610).

| block | RMS | control | DRC on | gain | implied dB/unit |
|---|---|---|---|---|---|
| −6 | −9.01 | 89.0 | **52** | ≤ −37.0 | **≥ 4.15** |
| −12 | −15.01 | 83.0 | **50** | ≤ −33.0 | **≥ 4.05** |
| −18 | −21.01 | 77.0 | 77.8 | +0.8 | — |
| −24 | −27.01 | 71.2 | 71.5 | +0.3 | — |
| −30 | −33.01 | 65.5 | 65.9 | +0.4 | — |
| −36 | −39.01 | 60.0 | 60.4 | +0.4 | — |

Region 1 is transparent for the third time. Region 2 is **1–3 dB above a 49 dB room
floor** — not a measurement of the output, a measurement of the room, so both
dB-per-unit figures are lower bounds. They agree with each other, and `6.0206`
predicts −60 dB, which reads exactly like this.

**A 6× change in the register cannot produce a ≥28× change in delivered
attenuation.** So at most one of runs 12 and 13 is measuring a scale factor, and no
single constant fits both. The offset's units are **open**; the code writes plain dB
as a placeholder, flagged as such in `tas58xx_drc.h`.

The discriminating run avoids both failure modes — nothing near the speaker's
compression region, nothing near the floor. Ratio 1.5, threshold −16 puts `off2` at
−5.333, and the candidates separate by ~11 dB per step:

| block | RMS | `c = 1` | `c = 3.0103` | `c = 6.0206` |
|---|---|---|---|---|
| −6 | −9.01 | 86.7 | 76.0 | 59.9 |
| −12 | −15.01 | 82.7 | 72.0 | 55.9 |

No reflash is needed: `off2 = −k·T1` is derived from the ratio and threshold
entities, so dialling them moves the register directly.

#### Measured 2026-08-03, run 14 — the curve above the knee is inverted

Threshold −16, ratio 1.5, makeup 0, `off2` register −5.3333, same-session control.

| block | RMS | control | DRC on | gain |
|---|---|---|---|---|
| −6 | −9.01 | 89.0 | **66.5** | **−22.50** |
| −12 | −15.01 | 83.0 | 82.5 | −0.50 |
| −18 | −21.01 | 77.4 | 76.7 | −0.70 |
| −24 | −27.01 | 71.6 | 71.2 | −0.40 |
| −30 | −33.01 | 65.7 | 65.2 | −0.50 |
| −36 | −39.01 | 60.1 | 59.9 | −0.20 |

Region 1 transparent for the fourth time. Everything else about this run says the
model is wrong in shape, not merely in its constants.

**Louder input produced quieter output.** Block 1 is 6 dB hotter than block 2 and
came out 16 dB below it. Across those two points the transfer slope is **−3.67
dB/dB**, and any slope past −1.0 inverts the curve — the condition `ratio_to_slope`
clamps against. The clamp is doing nothing useful here, because the steepness is not
coming from `k`: `k` was −0.3333.

Two readings, two irreconcilable ways to take them:

- **If both blocks are above the knee**, they lie on one line with one slope and one
  offset, and that line has slope −3.67 where −0.3333 was written. An 11× error in
  the slope, in a run whose only change from the confirmed-slope runs was the size of
  the offset.
- **If only block 1 is above the knee** — the knee having landed higher than the −16
  dialled — then `k` is fine, and the delivered offset is −25.50 dB from a register
  of −5.3333: **4.78 dB per unit**. But that requires the threshold to deliver a knee
  1–7 dB high, i.e. 1.70–2.82 dB per unit, when the slope measurements pin that same
  scaling to 3.0103.

Neither survives. The first contradicts four slope measurements; the second
contradicts the threshold scale that those same measurements depend on. **The offset
is not behaving as a constant added to the gain**, and that is the finding — runs
9–11 measured `k` correctly with a small offset register (−1.6610), and run 14 sees
an apparent 11× slope error with a larger one (−5.3333). Slope and offset are
interacting.

#### Why the next run needs a register override

The offset cannot be varied independently through the normal controls, because
`off2 = −k·T1` couples it to the threshold:

| ratio | T1 | off2 | plateaus above the knee |
|---|---|---|---|
| 1.5 | −6 | −2.000 | **0** |
| 1.5 | −10 | −3.333 | 1 |
| 1.5 | −16 | −5.333 | 2 |
| 1.5 | −40 | −13.333 | 6 |
| 2.0 | −20 | −10.000 | 2 |

A small offset forces a high knee, which leaves no material above the knee to
measure; a low knee forces a large offset. There is no setting anywhere in the
two-dimensional control space that gives a small offset *and* several plateaus above
the knee. Every run so far has been fighting that, and it is why three attempts to
fit a scale factor have produced three different answers.

`DRC Offset 2 Override` (diagnostic, added 2026-08-03) writes a raw value straight to
the register and breaks the coupling. With the threshold at −60 the knee is below
everything — for any threshold scale in 1.70–3.01 it lands between −33.9 and −60, so
blocks 1–5 are above it regardless, and block 6 becomes a free probe of where it
actually is.

Sweeping the override at ratio 1.5 then measures both unknowns at once, five plateaus
per point: the **gaps** give the slope, independently of the offset, and should read
`5.78 × 2/3` = **3.85 dB** whatever the offset turns out to be. The **absolute level**
gives the offset scale. If the gaps move as the offset changes, the interaction is
confirmed directly.

| override | c = 1 | c = 3.0103 | c = 6.0206 |
|---|---|---|---|
| −2.0 | 90.0 … 71.1 | 86.0 … 67.1 | 80.0 … 61.1 |
| −4.0 | 88.0 … 69.1 | 80.0 … 61.1 | 67.9 … 49.0 |
| −6.0 | 86.0 … 67.1 | 73.9 … 55.0 | 55.9 … 37.0 |

#### Measured 2026-08-03, run 15 — the offset is 20·log10 2, and nothing interacts

Threshold −60, ratio 1.5, makeup 0, the `off2` override swept. Every plateau above
the knee in all three passes.

| plateau | override −2 | −4 | −6 |
|---|---|---|---|
| −6 | 89.4 | 77.2 | 65.6 |
| −12 | 85.3 | 73.0 | 61.5 |
| −18 | 81.6 | 69.4 | 58.1 |
| −24 | 77.6 | 65.7 | 55.0 |
| −30 | 73.5 | 61.6 | 52.1 |
| −36 | 69.7 | 58.2 | 50.2 |

**Neither of the two numbers this run produces needs a control**, which is what makes
it the only trustworthy measurement in the series. An absolute reference was exactly
what every earlier run got wrong, whether through the EQ returning on reboot, the
speaker compressing, or the room floor.

**The offset, from the difference between passes at one plateau.** `k·level` is
identical in both, so it cancels and only the offset term survives:

| plateau | −2 vs −4, per unit | −2 vs −6, per unit |
|---|---|---|
| −6 | 6.100 | 5.950 |
| −12 | 6.150 | 5.950 |
| −18 | 6.100 | 5.875 |
| −24 | 5.950 | 5.650 |

6.050 over five clean plateaus and 5.950 over the four-unit span. That is
**20·log10 2 = 6.0206** — the constant removed in `21f9769` on the strength of run
12, now restored.

**The slope, from the gaps within one pass.** A constant offset shifts every plateau
by the same amount, so the gaps are immune to it:

| pass | gaps | mean |
|---|---|---|
| −2 | 4.1 3.7 4.0 4.1 3.8 | 3.94 |
| −4 | 4.2 3.6 3.7 4.1 3.4 | 3.90 |

Pooled, 3.870 dB per step against 3.853 predicted, a delivered ratio of **1.494
against 1.500 requested** — the best slope measurement of the series. And it does not
move with the offset: 3.94 versus 3.80 across a 2-unit change. **There is no
slope/offset interaction**, so run 14's apparent 11× slope error was an artifact of
reading a single point in a run where the knee had not landed where it was dialled.

##### The model this settles

    u        = log2(mean square) = level_dB / 3.0103      <- the threshold's domain
    g        = k * (u/2) + O                              <- log2 of an amplitude
    gain_dB  = 6.0206 * g  =  k * level_dB + 6.0206 * O

The part multiplies the slope by log2 of the RMS — half the log2 of the mean square —
and exponentiates as an amplitude gain. That internal factor of two is why the three
scalings differ: the threshold is divided by `10·log10 2`, the offset by `20·log10 2`,
and the slope by nothing at all, its two conversions cancelling.

##### Two residuals it does not explain

Against this model, run 13 matches on every below-knee plateau to 0.8 dB and its
above-knee pair correctly lands under the noise floor. Runs 12 and 14 do not:

| run | plateau | predicted | measured | residual |
|---|---|---|---|---|
| 12 | −6 | 84.2 | 93.0 | **+8.8** |
| 12 | −12 | 81.4 | 90.4 | **+9.0** |
| 14 | −6 | 59.9 | 66.5 | **+6.6** |

Both read *louder* than predicted, and in both the DRC had to attack into compression
— the file's reference tone sits below the knee at those thresholds, so the gain
started from rest at every loud plateau. In run 15 the knee was at −60, so the
reference tone was already above it and the gain was engaged before the first plateau
began. That is the one structural difference between the run that fits and the two
that do not.

Run 12 is worse than partial engagement, though: +8.8 dB puts it *above* its own
control, a boost, which no incomplete attack can produce. It behaves as if `off2`
were zero — as though the offset never reached the register in that pass. It remains
unexplained, and it is the run the plain-dB conclusion was built on.

The leading hypothesis for run 14 is that the attack is far slower than the 10 ms
dialled. That would also be the first evidence about the time constants, which are
still untested, and it has a cheap test: hold one setting and compare the first loud
plateau at attack 0.1 ms against 500 ms.

---

## 4. The three-band trap

The handoff's trap #1 says enabling mid/high DRC without setting their mixer
gains produces silence. The **inverse** trap is worse and is not in the handoff:

Out of reset, the crossover biquads are **pass-through** (B0 = unity, rest zero)
*and* bands 2/3 are muted. Unmuting bands 2 and 3 without programming the
crossover therefore sums three copies of the full-range signal — about +9.5 dB
of gain and every band compressing the same content.

A functional 3-band DRC requires the crossover biquads to be programmed. Band 1
alone, with bands 2/3 left muted, is a perfectly valid full-range single-band
compressor and is the safe first milestone.

### Crossover biquads

**TAS5825M PF1** (SLAA786A pp. 70–72), book 0xAA, all coefficients **5.27**,
default pass-through `B0 = 0x08000000`:

| Block | B0 | B1 | B2 | A1 | A2 |
|---|---|---|---|---|---|
| DRC low BQ1 | p07/0x78 | p07/0x7C | p08/0x08 | p08/0x0C | p08/0x10 |
| DRC low BQ2 | p08/0x14 | 0x18 | 0x1C | 0x20 | 0x24 |
| DRC high BQ1 | p08/0x28 | 0x2C | 0x30 | 0x34 | 0x38 |
| DRC high BQ2 | p08/0x3C | 0x40 | 0x44 | 0x48 | 0x4C |
| DRC mid BQ1 | p08/0x50 | 0x54 | 0x58 | 0x5C | 0x60 |
| DRC mid BQ2 | p08/0x64 | 0x68 | 0x6C | 0x70 | 0x74 |
| DRC mid BQ3 | p08/0x78 | p08/0x7C | p09/0x08 | p09/0x0C | p09/0x10 |
| DRC mid BQ4 | p09/0x14 | 0x18 | 0x1C | 0x20 | 0x24 |

**TI's PAGE column is mis-transcribed in this section** — it repeats the
pre-wrap page for rows immediately after an offset wrap, and slips early on
`high BQ1 A1`. The offsets are trustworthy: they form a strictly monotonic
sequence in clean groups of five, wrapping 0x7C → 0x08 exactly twice. The
reconstruction above follows the offsets and is corroborated independently:
page 0x09 offsets 0x08–0x24 are the only free slots on that page (DPEQ control
starts at 0x28), and that is exactly **eight** slots — precisely what mid BQ3's
remaining three plus mid BQ4's five require. Still marked **[UNVERIFIED]**
pending readback, since no shipping code exercises these addresses.

Note the band order in memory is low, **high**, mid — not low, mid, high.

**TAS5805M** (SLOA263A p.25), book 0xAA, **mixed formats within one biquad** —
`B0, B2, A2 → 1.31` and `B1, A1 → 2.30`:

| Block | B0 | B1 | B2 | A1 | A2 |
|---|---|---|---|---|---|
| DRC low BQ1 | p2A/0x34 | 0x38 | 0x3C | 0x40 | 0x44 |
| DRC low BQ2 | p2A/0x48 | 0x4C | 0x50 | 0x54 | 0x58 |
| DRC mid BQ1 | p2A/0x5C | 0x60 | 0x64 | 0x68 | 0x6C |
| DRC mid BQ2 | p2A/0x70 | 0x74 | 0x78 | 0x7C | p2B/0x08 |
| DRC high BQ1 | p2B/0x0C | 0x10 | 0x14 | 0x18 | 0x1C |
| DRC high BQ2 | p2B/0x20 | 0x24 | 0x28 | 0x2C | 0x30 |
| DRC mid BQ3 | p2E/0x40 | 0x44 | 0x48 | 0x4C | 0x50 |
| DRC mid BQ4 | p2E/0x54 | 0x58 | 0x5C | 0x60 | 0x64 |

Here the memory order is low, mid, high, with mid BQ3/BQ4 on a separate page.
SLOA263A's page column is self-consistent in this section (one wrap, at mid BQ2
A2), so unlike the 5825M table it needs no reconstruction.

Note the format difference between parts is real: 5825M crossover BQs are 5.27
like its EQ, 5805M crossover BQs are the mixed 1.31/2.30 the handoff describes.

All five coefficients of a biquad must be written, in ascending address order.

### PPC3 biquad normalisation

Unchanged from the handoff (SLOA263A Table 3 / SLAA786A Table 6):

```
B0_DSP =  b0 / a0
B1_DSP =  b1 / (a0 * 2)
B2_DSP =  b2 / a0
A1_DSP = -a1 / (a0 * 2)
A2_DSP = -a2 / a0
```

Sign inversion on A1/A2, factor of 2 on B1/A1.

---

## 5. Other traps

1. **Page 0 before every book switch.** Most common cause of silent no-ops.
2. **Digital volume ramping blocks I²C** until the ramp settles. Relevant when
   an automation hammers registers.
3. **All five biquad coefficients** must be written or the write is ignored;
   `GLOBAL_FAULT1` bit 6 reports "recent BQ written failed".
4. The mainline Linux 5805M driver bypasses register `0x4C` for volume and uses
   the DSP volume coefficients instead. The component already does the
   equivalent via its channel-volume path.
5. **Rounding, not truncation.** Sonocotta's `tas5805m_float_to_q9_23` casts
   rather than rounds, so e.g. −3 dB encodes as `0x005A9DF7` where TI's own
   value is `0x005A9DF8`. One LSB is inaudible, but it breaks readback
   comparison and round-trips. This fork rounds.

---

## 5b. What the part will tell you about itself

Settled question, since it bears directly on §3: **the DRC cannot be observed from
the outside.** There is no register anywhere in the TAS5825M control port map that
reports signal level or the attenuation the DRC, AGL or thermal foldback is
applying. Checked against the full §9.6.1 register map in SLASEH7H, not inferred.
So the only way to see what a compressor curve is doing remains what §3 already
does: play material and measure the acoustic result.

Nor is there any current, power or load-impedance measurement. Those live in other
families - TAS2770/TAS2780 have I/V sense, TAS6424 has load diagnostics. The DSP
memory map in SLAA786A is write-only coefficient space, not telemetry.

What does exist, all of it, and all now surfaced by the component:

| Reg | Name | Content | Notes |
|---|---|---|---|
| `0x5E` | `PVDD_ADC` | `PVDD = raw / 8.428` V | **TAS5825M only.** Reads `0x00` outside play, so the component publishes NAN unless `POWER_STATE == 3` |
| `0x73` | `WARNING` | bits 3-0 = OTW 1-4 at 112 / 122 / 134 / 146 °C; bit 5 = left CBC, bit 4 = right | Thermal foldback is attenuating from OTW1 up. The CBC bits are the only hint the load is drawing more than the amp will give |
| `0x69` | `AUTOMUTE_STATE` | bit 1 right / bit 0 left, 1 = output zero | Crude signal-present flag, gated by the auto mute threshold and timer |
| `0x68` | `POWER_STATE` | 0 deep sleep, 1 sleep, 2 Hi-Z, 3 play | Same encoding as `ControlState` |
| `0x37` | `FS_MON` | bits 3-0: `0` FS error, `4`=16k, `6`=32k, `9`=48k, `B`=96k, `D`=192k | Detected, not configured. 44.1/88.2 share the 48/96k codes |

Derived from `PVDD_ADC` plus the `AGAIN` setting, since together they give the real
output ceiling the DRC is working against. `AGAIN` code 0 is "0 dB (29.5 V peak)",
so 0 dBFS maps to `29.5 · 10^(gain/20)` volts:

```
V_peak_eff       = min(PVDD, 29.5 · 10^(again_dB/20))
max_output_power = V_peak_eff² / (2 · load_impedance)     # W, one channel, BTL
clip_headroom    = 20 · log10(PVDD / (29.5 · 10^(again_dB/20)))
```

`clip_headroom` negative means `analog_gain` is set above what the supply can
deliver and full-scale material clips on the rail. The datasheet's tabulated
per-step voltages sit up to ~2% off that curve - those are measured typicals, the
formula is the register definition.

**Level, as far as it can be had.** `components/audio_level/` is a pass-through
speaker that meters the stream on its way to the DAC - windowed RMS and peak dBFS
per channel. It measures the amplifier's digital *input*: digital volume, EQ, the
DRC and thermal foldback all happen after it, inside the chip, and none of them
are observable. Comparing a reading against a DRC threshold needs the digital
volume added back, and whether that is the right correction depends on where the
volume stage sits relative to the DRC in Process Flow 1 - not established, so the
component deliberately does not apply it.

---

## 6. Sources

| Doc | ID | Used for |
|---|---|---|
| TAS5825M Process Flows | **SLAA786A** | 5825M DSP memory maps (Table 9 = PF1), DRC block description |
| TAS5805M/5806M/5806MD Process Flows | **SLOA263A** | 5805M DSP memory maps, BQ normalisation |
| TAS57xx Dynamic Range Control | **SLOA148** | ratio↔k slope derivation, offset semantics |
| TAS5825M datasheet | SLASEQ8 | book 0 register map, startup/shutdown |
| TAS5825M datasheet, rev H | **SLASEH7H** | §9.6.1 register map - the telemetry survey in §5b, `PVDD_ADC` scaling, OTW thresholds, `AGAIN` peak voltage |
| TAS5825M Advanced Features | **SLAA846** | thermal foldback, PVDD sensing, hybrid modulation |
| General Tuning Guide for TAS58xx | SLAA894 | fixed-point formats, gain staging |

Local verification:

- `tools/drc_math_check.py` — reproduces every documented default in §2 and §3
  bit-exactly, and checks that an LR4 pair sums flat at its corner.
- `tools/cpp_check/run.py` — compiles the shipping
  `components/tas58xx/tas58xx_helpers.cpp` for the host against stub ESPHome
  headers and diffs its output against the Python reference. They agree
  bit-for-bit, so the tool above is a faithful oracle for the on-device code.

Both pass as committed.
