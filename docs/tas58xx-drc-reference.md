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

**Still unmeasured:** the threshold scale, `10·log10 2`. It is derived from the
model rather than observed, because no run has yet produced a knee. The
corrected firmware makes that directly readable — the knee position is now the
one thing left to check, and `gain` can no longer go positive, so it is safe to
measure at normal listening levels.

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

## 6. Sources

| Doc | ID | Used for |
|---|---|---|
| TAS5825M Process Flows | **SLAA786A** | 5825M DSP memory maps (Table 9 = PF1), DRC block description |
| TAS5805M/5806M/5806MD Process Flows | **SLOA263A** | 5805M DSP memory maps, BQ normalisation |
| TAS57xx Dynamic Range Control | **SLOA148** | ratio↔k slope derivation, offset semantics |
| TAS5825M datasheet | SLASEQ8 | book 0 register map, startup/shutdown |
| General Tuning Guide for TAS58xx | SLAA894 | fixed-point formats, gain staging |

Local verification:

- `tools/drc_math_check.py` — reproduces every documented default in §2 and §3
  bit-exactly, and checks that an LR4 pair sums flat at its corner.
- `tools/cpp_check/run.py` — compiles the shipping
  `components/tas58xx/tas58xx_helpers.cpp` for the host against stub ESPHome
  headers and diffs its output against the Python reference. They agree
  bit-for-bit, so the tool above is a faithful oracle for the on-device code.

Both pass as committed.
