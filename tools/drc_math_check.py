#!/usr/bin/env python3
"""Check the DRC coefficient encoders against TI's documented defaults.

This mirrors components/tas58xx/tas58xx_helpers.cpp exactly. Every assertion
below is a value printed in SLAA786A (TAS5825M) or SLOA263A (TAS5805M), so if
this passes, the on-device encoders reproduce TI's own constants bit-for-bit.

Run: python tools/drc_math_check.py
"""

import math
import sys

FAILURES = []


def check(label, got, want):
    ok = got == want
    if not ok:
        FAILURES.append(label)
    status = "ok  " if ok else "FAIL"
    print(f"  [{status}] {label:<44} got 0x{got:08X} want 0x{want:08X}")


# ---------------------------------------------------------------- encoders

def to_fixed(value, frac_bits):
    """Round-to-nearest with saturation, as in to_fixed()."""
    scaled = round(value * (1 << frac_bits))
    if scaled >= 2**31:
        return 2**31 - 1
    if scaled <= -(2**31):
        return -(2**31)
    return scaled


def u32(v):
    return v & 0xFFFFFFFF


def db_to_f9_23(db):
    """DRC thresholds and offsets: plain dB in 9.23."""
    return u32(to_fixed(db, 23))


def slope_to_f9_23(slope):
    return u32(to_fixed(slope, 23))


def linear_db_to_f9_23(db):
    """Band mixer gains: a linear gain expressed in dB."""
    return u32(to_fixed(10 ** (db / 20.0), 23))


def time_constant_to_f1_31(tau_ms, sample_rate):
    """alpha = 1/(fs*tau) - the reciprocal form, matching TI's AGL defaults."""
    if tau_ms <= 0 or sample_rate == 0:
        return 0x7FFFFFFF
    samples = sample_rate * tau_ms / 1000.0
    if samples <= 1.0:
        return 0x7FFFFFFF
    return u32(max(1, min(0x7FFFFFFF, to_fixed(1.0 / samples, 31))))


def ratio_to_slope(ratio):
    """SLOA148 Â§5.2: n:1 compression -> k = 1/n - 1."""
    if ratio <= 1.0:
        return 0.0
    return max(-0.999, (1.0 / ratio) - 1.0)


# ---------------------------------------------------- crossover biquads

BUTTERWORTH_Q = 1.0 / math.sqrt(2.0)


def _terms(corner_hz, fs):
    w0 = 2.0 * math.pi * corner_hz / fs
    cos_w0 = math.cos(w0)
    alpha = math.sin(w0) / (2.0 * BUTTERWORTH_Q)
    return cos_w0, 1.0 + alpha, -2.0 * cos_w0, 1.0 - alpha


def _normalise(b0, b1, b2, a0, a1, a2):
    """PPC3 convention: sign flip on A1/A2, factor of two on B1/A1."""
    return (b0 / a0, b1 / (2 * a0), b2 / a0, -a1 / (2 * a0), -a2 / a0)


def design_lowpass(corner_hz, fs):
    cos_w0, a0, a1, a2 = _terms(corner_hz, fs)
    k = 1.0 - cos_w0
    return _normalise(k / 2, k, k / 2, a0, a1, a2)


def design_highpass(corner_hz, fs):
    cos_w0, a0, a1, a2 = _terms(corner_hz, fs)
    k = 1.0 + cos_w0
    return _normalise(k / 2, -k, k / 2, a0, a1, a2)


def encode_biquad(bq, mixed_format):
    full = 31 if mixed_format else 27
    half = 30 if mixed_format else 27
    b0, b1, b2, a1, a2 = bq
    return [u32(to_fixed(b0, full)), u32(to_fixed(b1, half)), u32(to_fixed(b2, full)),
            u32(to_fixed(a1, half)), u32(to_fixed(a2, full))]


# ------------------------------------------------------------------ checks

def main():
    print("Thresholds and offsets - 9.23 plain dB")
    print("  documented T1/T2 defaults, identical on TAS5805M and TAS5825M")
    check("T1 default -50.0 dB", db_to_f9_23(-50.0), 0xE7000000)
    check("T2 default  -3.0 dB", db_to_f9_23(-3.0), 0xFE800000)
    check("offset default 0 dB", db_to_f9_23(0.0), 0x00000000)

    print("\nBand mixer gains - 9.23 linear")
    check("mixer unity (0 dB)", linear_db_to_f9_23(0.0), 0x00800000)
    check("K/slope default (no compression)", slope_to_f9_23(ratio_to_slope(1.0)), 0x00000000)

    print("\nTime constants - 1.31, reciprocal form")
    print("  AGL attack/release defaults, byte-identical in SLAA786A and SLOA263A")
    check("AGL attack  100 ms @ 96 kHz", time_constant_to_f1_31(100.0, 96000), 0x000369D0)
    check("AGL release   1 s  @ 96 kHz", time_constant_to_f1_31(1000.0, 96000), 0x00005762)
    check("DRC E/A/D default (instantaneous)", time_constant_to_f1_31(0.0, 96000), 0x7FFFFFFF)

    print("\n  the exponential form the original handoff specified, for contrast:")
    alpha_exp = 1.0 - math.exp(-1.0 / 9600.0)
    exp_raw = u32(to_fixed(alpha_exp, 31))
    print(f"  1 - exp(-1/(fs*tau)) at 100 ms/96 kHz -> 0x{exp_raw:08X}, "
          f"off by {exp_raw - 0x000369D0:+d} from TI's 0x000369D0")

    print("\nRatio to K slope - SLOA148 Â§5.2, k = 1/n - 1")
    for ratio, want_k in [(1.0, 0.0), (2.0, -0.5), (4.0, -0.75), (10.0, -0.9)]:
        got_k = ratio_to_slope(ratio)
        ok = abs(got_k - want_k) < 1e-9
        if not ok:
            FAILURES.append(f"ratio {ratio}")
        print(f"  [{'ok  ' if ok else 'FAIL'}] {ratio:5.1f}:1 -> k = {got_k:+.6f} "
              f"(want {want_k:+.4f})  raw 0x{slope_to_f9_23(got_k):08X}")
    k_limit = ratio_to_slope(1e9)
    ok = -1.0 < k_limit < 0.0
    if not ok:
        FAILURES.append("slope floor")
    print(f"  [{'ok  ' if ok else 'FAIL'}] limiter ratio -> k = {k_limit:+.6f} "
          f"(must stay > -1 per SLOA148)")

    print("\nCrossover biquads - pass-through defaults")
    print("  TAS5825M: 5.27 throughout, documented B0 default 0x08000000")
    pt_5825 = encode_biquad((1.0, 0.0, 0.0, 0.0, 0.0), mixed_format=False)
    check("5825M pass-through B0", pt_5825[0], 0x08000000)
    check("5825M pass-through B1", pt_5825[1], 0x00000000)
    print("  TAS5805M: mixed 1.31 / 2.30, documented B0 default 0x7FFFFFFF")
    pt_5805 = encode_biquad((1.0, 0.0, 0.0, 0.0, 0.0), mixed_format=True)
    check("5805M pass-through B0 (1.31 saturated)", pt_5805[0], 0x7FFFFFFF)
    check("5805M pass-through A2", pt_5805[4], 0x00000000)

    print("\nCrossover sanity - a real 300 Hz / 3 kHz split at 96 kHz")
    for name, bq in [("low  LPF 300 Hz", design_lowpass(300.0, 96000)),
                     ("high HPF 3 kHz ", design_highpass(3000.0, 96000))]:
        coeffs = encode_biquad(bq, mixed_format=False)
        print(f"  {name}: " + " ".join(f"0x{c:08X}" for c in coeffs))
        # A stable normalised biquad must keep every coefficient inside 5.27 range
        assert all(abs(v) < 16.0 for v in bq), f"{name} overflows 5.27"

    # A 4th-order Linkwitz-Riley pair should sum to unity gain at the corner.
    # Two cascaded Butterworth sections are -6 dB each at the corner, so the
    # squared magnitude of the cascade is 0.5 -> the LP and HP cascades sum flat.
    def magnitude(bq, freq, fs):
        """|H(e^jw)| for a PPC3-normalised biquad (undo the halving and sign flip)."""
        b0, b1h, b2, a1h, a2 = bq
        b1, a1, a2 = b1h * 2, -a1h * 2, -a2
        w = 2 * math.pi * freq / fs
        z1 = complex(math.cos(-w), math.sin(-w))
        z2 = z1 * z1
        return abs((b0 + b1 * z1 + b2 * z2) / (1 + a1 * z1 + a2 * z2))

    fc, fs = 1000.0, 96000
    lp = design_lowpass(fc, fs)
    hp = design_highpass(fc, fs)
    lp_at_fc = magnitude(lp, fc, fs) ** 2   # squared = two cascaded sections
    hp_at_fc = magnitude(hp, fc, fs) ** 2
    print(f"\n  LR4 at the {fc:.0f} Hz corner: LP {20*math.log10(lp_at_fc):+.2f} dB, "
          f"HP {20*math.log10(hp_at_fc):+.2f} dB, sum {lp_at_fc + hp_at_fc:.4f}")
    if abs(lp_at_fc + hp_at_fc - 1.0) > 0.02:
        FAILURES.append("LR4 crossover does not sum flat at the corner")

    print()
    if FAILURES:
        print(f"FAILED: {len(FAILURES)} check(s): {', '.join(FAILURES)}")
        sys.exit(1)
    print("All checks passed - encoders reproduce every TI documented default exactly.")


if __name__ == "__main__":
    sys.exit(main())
