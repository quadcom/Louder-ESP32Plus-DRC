> **Superseded by [docs/tas58xx-drc-reference.md](docs/tas58xx-drc-reference.md).**
> This document is accurate for the TAS5805M, but the Louder ESP32 Plus carries a
> **TAS5825M**, whose DSP memory map is different. Two corrections also apply to
> the parts that are family-general: the time-constant formula (§6) and the
> three-band mixer trap (§8). Kept for provenance.

# TAS5805M DRC Control from ESPHome — Handoff Notes

Context for a coding session. Goal: expose the TAS5805M's 3-band DRC as
runtime-adjustable controls in ESPHome / Home Assistant. EQ and volume are
already working; DRC is the remaining gap.

---

## 1. Key premise

The TAS5805M has **no non-volatile memory**. Every register and DSP coefficient
lives in RAM and is lost on PDN or power cycle. The host MCU must write the full
configuration over I²C at every boot regardless.

**PurePath Console 3 (PPC3) is not required.** It is an authoring tool that emits
a byte list; it does not program the chip. Anything PPC3 writes at init can be
written by the ESP32 at any time afterward. Only authoring a *novel process flow*
(changing DSP topology) needs PPC3. Coefficient changes within an existing flow
do not.

---

## 2. I²C access model

- 7-bit address **0x2C / 0x2D** (0x58 / 0x5A in the 8-bit form TI uses in its
  example scripts), selected by the ADR pin.
- Register `0x00` = PAGE select, register `0x7F` = BOOK select.
- Writes auto-increment, so a 32-bit DSP value goes out as one 4-byte burst,
  **MSB first**.

### Mandatory navigation sequence

```
write 0x00, 0x00      ; page 0 FIRST — non-negotiable
write 0x7F, <book>
write 0x00, <page>
write <offset>, <b3>, <b2>, <b1>, <b0>
```

Skipping the "page 0 first" step is the most common cause of writes silently
doing nothing.

---

## 3. Book 0x00 / page 0x00 — hardware control registers

| Addr | Name | Notes |
|---|---|---|
| 0x01 | RESET_CTRL | 0x11 = reset DSP core + registers |
| 0x02 | DEVICE_CTRL_1 | switching freq, modulation (BD / 1SPW / hybrid), PBTL |
| 0x03 | DEVICE_CTRL_2 | 0x02 = Hi-Z, 0x03 = Play, bit 3 = soft mute |
| 0x28–0x2A | SAP_CTRL1..3 | I²S/TDM format, word length, slot select |
| 0x30 | FS_MON | read-only detected sample rate |
| 0x37 | CLKDET_STATUS | clock detect status |
| 0x4C | DIG_VOL_CTRL | 0.5 dB steps, 0xFF = mute |
| 0x53 | — | D[6:5] Class-D loop bandwidth (must match fsw in 0x02) |
| 0x54 | AGAIN | [4:0], 32 steps × 0.5 dB analog gain |
| 0x70–0x73 | fault status | CHAN / GLOBAL1 / GLOBAL2 / OT_WARNING |
| 0x78 | FAULT_CLEAR | — |

### Bring-up order

1. I²S clocks stable (SCLK/LRCLK valid) — required before any DSP write sticks
2. Release PDN
3. Hi-Z: `0x03` ← 0x02
4. Write the full init / coefficient blob
5. Play: `0x03` ← 0x03

---

## 4. DSP memory map — DRC (Book 0x8C)

Source: **SLOA263A**, *TAS5805M, TAS5806M and TAS5806MD Process Flows*.
Addresses below hold for Process Flow 1 (2.0, 96 kHz) and Process Flows 2/3
(48 kHz) — the DRC block is at identical locations in both maps.

Each of the three bands is a 10-parameter block:

| Parameter | DRC1 (low) | DRC2 (mid) | DRC3 (high) | Format |
|---|---|---|---|---|
| Energy    | p0x2B / 0x34 | p0x2D / 0x30 | p0x2D / 0x58 | 1.31 |
| Attack    | p0x2B / 0x38 | p0x2D / 0x34 | p0x2D / 0x5C | 1.31 |
| Decay     | p0x2B / 0x3C | p0x2D / 0x38 | p0x2D / 0x60 | 1.31 |
| K0 (region 1 slope) | p0x2B / 0x40 | p0x2D / 0x3C | p0x2D / 0x64 | 9.23 |
| K1 (region 2 slope) | p0x2B / 0x44 | p0x2D / 0x40 | p0x2D / 0x68 | 9.23 |
| K2 (region 3 slope) | p0x2B / 0x48 | p0x2D / 0x44 | p0x2D / 0x6C | 9.23 |
| T1 (threshold 1)    | p0x2B / 0x4C | p0x2D / 0x48 | p0x2D / 0x70 | 9.23 |
| T2 (threshold 2)    | p0x2B / 0x50 | p0x2D / 0x4C | p0x2D / 0x74 | 9.23 |
| Off1                | p0x2B / 0x54 | p0x2D / 0x50 | p0x2D / 0x78 | 9.23 |
| Off2                | p0x2B / 0x58 | p0x2D / 0x54 | p0x2D / 0x7C | 9.23 |

**Band mixer gains** (post-DRC, pre-sum), Book 0x8C page 0x2E, format 9.23:

| Band | Offset | Default |
|---|---|---|
| DRC1 mixer gain | 0x08 | 0x00800000 (= 1.0) |
| DRC2 mixer gain | 0x0C | 0x00000000 (= 0) |
| DRC3 mixer gain | 0x10 | 0x00000000 (= 0) |

**Documentation errata:** page 0x2D offset 0x6C is printed as `k1_3` in SLOA263A.
It is actually **k2_3** (region 3 slope for band 3) — `k1_3` is at 0x68.

### Defaults / reset state

- Energy / Attack / Decay = `0x7FFFFFFF` (α ≈ 1.0 → instantaneous, no smoothing)
- K0/K1/K2 = 0, Off1/Off2 = 0  → gain adjustment curve is flat → **DRC is a no-op**
- T1 = `0xE7000000` (−50 dB), T2 = `0xFE800000` (−3 dB)
- Mixer: band 1 unity, bands 2 and 3 muted

So the out-of-reset behaviour is: full signal through band 1, no compression.

---

## 5. Crossover biquads (Book 0xAA)

These are what actually split the three DRC bands. Only touch these to move the
crossover frequencies.

| Block | Location |
|---|---|
| DRC low BQ1, BQ2   | p0x2A / 0x34–0x58 |
| DRC mid BQ1, BQ2   | p0x2A / 0x5C–0x7C, continuing p0x2B / 0x08 |
| DRC high BQ1, BQ2  | p0x2B / 0x0C–0x30 |
| DRC mid BQ3, BQ4   | p0x2E / 0x40–0x64 |

**Mixed fixed-point formats within a single biquad:**
`B0, B2, A2 → 1.31` and `B1, A1 → 2.30` (the halved coefficients).
Defaults are pass-through: B0 = `0x7FFFFFFF`, all others 0.

All five coefficients of a biquad must be written completely and in sequence
from lowest to highest address.

### EQ biquads (for reference — already working)

Book 0xAA, 15 BQs per channel, format **5.27** throughout, unity default
`0x08000000`. Left channel starts at p0x24 / 0x18; right channel starts at
p0x26 / 0x54.

Normalisation applied by PPC3 (from SLOA263A Table 3):

```
B0_DSP =  b0 / a0
B1_DSP =  b1 / (a0 * 2)
B2_DSP =  b2 / a0
A1_DSP = -a1 / (a0 * 2)
A2_DSP = -a2 / a0
```

Note the sign inversion on A1/A2 and the factor-of-2 on B1/A1.

---

## 6. Fixed-point encoding

All DSP values are 32-bit two's complement, big-endian on the wire.

| Format | Scale factor | Used for |
|---|---|---|
| 1.31 | 2³¹ = 2147483648 | time constants, DRC/crossover BQ B0/B2/A2 |
| 2.30 | 2³⁰ = 1073741824 | crossover BQ B1/A1, fine volume |
| 5.27 | 2²⁷ = 134217728  | EQ biquad coefficients |
| 9.23 | 2²³ = 8388608    | gains, volume, mixer, DRC thresholds/slopes/offsets |

Unity in 9.23 = `0x00800000`. Unity in 5.27 = `0x08000000`.
1.31 saturates at `0x7FFFFFFF`.

### Thresholds and offsets are plain dB in 9.23

```
raw = round(dB * 8388608)      # two's complement for negative
```

Verified against the documented defaults: `0xE7000000` / 2²³ = −50.0 and
`0xFE800000` / 2²³ = −3.0. Both match the PPC3 default DRC curve.

### Time constants (Energy / Attack / Decay) are 1.31 alphas

```
alpha = 1 - exp(-1 / (fs * tau_seconds))
raw   = round(alpha * 2147483648)
```

For typical time constants this reduces to `alpha ≈ 1 / (fs * tau)`.

Verified against the AGL defaults in the same memory map: attack `0x000369D0`
= 1.0417e-4 → fs·τ = 9600 → **100 ms at 96 kHz**; release `0x00005762`
= 1.0417e-5 → fs·τ = 96000 → **1 s at 96 kHz**. Both are sane AGL values, which
confirms the formula.

**fs here is the DSP internal rate, not necessarily the I²S input rate.** With
SRC enabled in the 96 kHz flow, input is upconverted to 88.2/96 kHz. Read
`FS_MON` (book 0, 0x30) and pick 88.2k or 96k accordingly, or the time constants
will be wrong by up to 2x.

### K slopes

K0/K1/K2 define the slope of the **gain-adjustment** curve in each of the three
regions, not an input-vs-output curve. K = 0 with offset = 0 is the identity
(no gain change) — consistent with the documented defaults. For the exact
ratio↔slope derivation, see **SLOA148** (*TAS57xx Dynamic Range Control*), which
describes the same DRC structure. **This is the one item not yet verified
empirically — confirm on hardware before trusting a ratio mapping.**

---

## 7. Reference conversion helpers

```python
def q(value, frac_bits):
    """Encode a float into TI fixed point as a 32-bit unsigned int."""
    raw = int(round(value * (1 << frac_bits)))
    raw = max(-(1 << 31), min((1 << 31) - 1, raw))
    return raw & 0xFFFFFFFF

def db_9_23(db):
    """DRC thresholds and offsets."""
    return q(db, 23)

def gain_9_23(db):
    """Linear gains expressed in dB (mixer, volume)."""
    return q(10 ** (db / 20.0), 23)

def timeconst_1_31(tau_ms, fs=96000):
    """DRC/AGL energy, attack, decay."""
    import math
    alpha = 1.0 - math.exp(-1.0 / (fs * tau_ms / 1000.0))
    return q(alpha, 31)
```

### Write pattern for one 32-bit DSP value

```
0x00, 0x00              # page 0
0x7F, 0x8C              # book 0x8C
0x00, 0x2B              # page 0x2B
0x34, b3, b2, b1, b0    # 4-byte burst at offset, MSB first
```

Batch by page: set book/page once, then write all offsets on that page before
moving on. A full DRC band update is one page switch plus ten 4-byte bursts.

---

## 8. Traps

1. **Bands 2 and 3 are muted by default.** Enabling DRC on mid/high without
   also setting their mixer gain (p0x2E / 0x0C, 0x10) produces silence in those
   bands.
2. **Page 0 before book switch**, every time.
3. **Digital volume ramping blocks I²C reads and writes** until the ramp reaches
   its setpoint. Relevant if hammering registers from an automation.
4. The mainline Linux driver deliberately bypasses `0x4C` for volume and instead
   writes the DSP volume coefficients at **book 0x8C, page 0x2A, offsets 0x24
   (L) and 0x28 (R)** in 9.23, because the datasheet volume control misbehaves.
   Worth matching that approach.
5. All five biquad coefficients must be written; partial writes are ignored.

---

## 9. ESPHome landscape

| Project | DRC support |
|---|---|
| `mrtoy-me/esphome-tas58xx` (external component, ESP-IDF) | No — EQ, mixer, analog gain, digital volume, fault sensors only |
| `sonocotta/esp32-tas5805m-dac` (ESP-IDF library) | No — README states DRC and AGL are not yet implemented |
| `sonocotta/esp32-audio-dock` YAML packages | No — 15-band EQ, biamp, sub/satellite presets |
| `heytcass/louder-esphome-sendspin` | No, but has a working custom biquad-over-I²C header + profile manager worth copying as a pattern |

So DRC needs to be built. Options:

- **A —** Lambdas plus `number` template entities in YAML. Fastest to a working
  result, no C++ component to maintain, ugly but fine.
- **B —** Fork `esphome-tas58xx` and add DRC as first-class config + entities.
  More work, upstreamable.

Recommend A first to validate the coefficient math on hardware, then B.

---

## 10. Next steps

- [ ] Confirm which process flow the current init blob uses (96 kHz 2.0 vs
      48 kHz 2.x) — determines whether the sub-channel DRC block exists
- [ ] Read back the DRC region of book 0x8C at runtime to capture the current
      live values as a baseline
- [ ] Verify the DSP internal fs via `FS_MON` for the time-constant math
- [ ] Empirically pin down the K slope ↔ compression ratio mapping (sweep K,
      measure gain reduction vs input level)
- [ ] Build: threshold / ratio / attack / release / makeup `number` entities per
      band, plus a master DRC enable that saves and restores the mixer gains
- [ ] Decide persistence — re-apply DRC state on boot after the init blob, since
      nothing survives power cycle

---

## 11. References

| Doc | ID | Contents |
|---|---|---|
| TAS5805M datasheet | SLASEH5 | Book 0 register map, startup/shutdown, gain structure |
| TAS5805M/5806M/5806MD Process Flows | **SLOA263A** | DSP memory maps per flow — the DRC addresses above |
| General Tuning Guide for TAS58xx Family | SLAA894 | Fixed-point formats, headroom, gain staging |
| TAS57xx Dynamic Range Control | SLOA148 | DRC structure, coefficient computation, slope/offset derivation |
