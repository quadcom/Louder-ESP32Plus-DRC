#!/usr/bin/env python3
"""Generate a stepped-level sine WAV for measuring a compressor's transfer curve.

The output is one continuous tone whose level drops in equal dB steps. Play it
through the amp and log SPL: each step becomes a plateau, and the *spacing*
between plateaus is what identifies the compressor.

Why relative spacing rather than absolute level: below the knee the compressor is
inactive, so plateaus are spaced exactly one step apart. Above the knee they
compress to step/ratio. Speaker sensitivity, mic calibration, gain settings and
room response are all fixed offsets that apply equally to every plateau, so they
cancel out of the spacing entirely. Nothing needs calibrating.

    ratio    = step_dB / measured_spacing_dB
    knee     = the level where the spacing changes

Levels are stated as sine PEAK amplitude relative to full scale, which is what
"dBFS" means for a generator. A sine's RMS is 3.01 dB below its peak, so if the
DSP's detector is RMS-referenced the knee lands 3.01 dB above the threshold you
set. That offset is itself a useful result - see docs/drc-measurement.md.

Each step is a whole number of cycles, so level changes land on a zero crossing
and there are no clicks to upset the detector.

Usage:
    python tools/make_drc_staircase.py
    python tools/make_drc_staircase.py --start -30 --stop -6 --step 1 --seconds 3
"""

import argparse
import math
import struct
import wave


def build_levels(start_db: float, stop_db: float, step_db: float) -> list:
    """Descending list of peak levels in dBFS, inclusive of both ends."""
    if step_db <= 0:
        raise ValueError("--step must be positive")
    if start_db <= stop_db:
        raise ValueError("--start must be above --stop; the sweep descends")

    count = int(round((start_db - stop_db) / step_db)) + 1
    return [start_db - i * step_db for i in range(count)]


def sine_block(level_db, freq, rate, seconds, phase):
    """One constant-level block. Returns (samples, next_phase).

    Phase carries across blocks so the waveform stays continuous.
    """
    amplitude = (10.0 ** (level_db / 20.0)) * 32767.0
    total = int(round(rate * seconds))
    radians_per_sample = 2.0 * math.pi * freq / rate

    samples = []
    for n in range(total):
        value = int(round(amplitude * math.sin(phase + n * radians_per_sample)))
        # Clamp rather than wrap. Only reachable at 0 dBFS through rounding.
        samples.append(max(-32768, min(32767, value)))

    return samples, (phase + total * radians_per_sample) % (2.0 * math.pi)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", default="drc-staircase.wav")
    parser.add_argument("--freq", type=float, default=1000.0,
                        help="tone frequency in Hz (default 1000)")
    parser.add_argument("--rate", type=int, default=48000)
    parser.add_argument("--start", type=float, default=0.0,
                        help="loudest step, dBFS peak (default 0)")
    parser.add_argument("--stop", type=float, default=-54.0,
                        help="quietest step, dBFS peak (default -54)")
    parser.add_argument("--step", type=float, default=3.0,
                        help="dB per step (default 3)")
    parser.add_argument("--seconds", type=float, default=4.0,
                        help="seconds per step (default 4)")
    parser.add_argument("--reference", type=float, default=-24.0,
                        help="level of the lead-in and lead-out reference tone, "
                             "dBFS peak. Both should measure the same; if they "
                             "do not, something drifted mid-run. (default -24)")
    parser.add_argument("--toggle", nargs=2, type=float, metavar=("QUIET", "LOUD"),
                        help="switch to step mode: alternate between these two "
                             "peak levels in dBFS instead of walking a staircase. "
                             "For measuring attack and release, where what matters "
                             "is the level TRAJECTORY after an abrupt change, not "
                             "the steady state. Use a long --seconds so a slow "
                             "time constant has room to show itself.")
    parser.add_argument("--cycles", type=int, default=3,
                        help="quiet/loud pairs in --toggle mode (default 3)")
    args = parser.parse_args()

    if args.toggle and args.cycles < 1:
        parser.error("--cycles must be at least 1")

    # A whole number of cycles per step keeps every level change on a zero
    # crossing. Warn rather than fail - a fractional cycle is a click, not a
    # wrong answer.
    cycles = args.freq * args.seconds
    if abs(cycles - round(cycles)) > 1e-9:
        print("WARNING: {:.0f} Hz x {:g}s = {:.3f} cycles, not a whole number. "
              "Level changes will click.".format(args.freq, args.seconds, cycles))

    if args.toggle:
        quiet, loud = args.toggle
        if quiet >= loud:
            parser.error("--toggle takes QUIET then LOUD, and QUIET must be lower")
        levels = [quiet, loud] * args.cycles
    else:
        levels = build_levels(args.start, args.stop, args.step)

    phase = 0.0
    samples = []

    block, phase = sine_block(args.reference, args.freq, args.rate, args.seconds, phase)
    samples += block

    for level in levels:
        block, phase = sine_block(level, args.freq, args.rate, args.seconds, phase)
        samples += block

    block, phase = sine_block(args.reference, args.freq, args.rate, args.seconds, phase)
    samples += block

    # Stereo, both channels identical: the DRC acts on the band, not the channel.
    frames = b"".join(struct.pack("<hh", s, s) for s in samples)

    with wave.open(args.out, "wb") as wav:
        wav.setnchannels(2)
        wav.setsampwidth(2)
        wav.setframerate(args.rate)
        wav.writeframes(frames)

    duration = len(samples) / float(args.rate)
    print("Wrote {}".format(args.out))
    print("  {:.0f} Hz sine, {} Hz, 16-bit stereo, {:.0f}s total".format(
        args.freq, args.rate, duration))
    if args.toggle:
        print("  {} quiet/loud pairs at {:g} / {:g} dBFS, {:g}s each, plus a {:g} dBFS "
              "reference at each end".format(args.cycles, args.toggle[0], args.toggle[1],
                                             args.seconds, args.reference))
    else:
        print("  {} steps of {:g} dB, {:g}s each, plus a {:g} dBFS reference at each end".format(
            len(levels), args.step, args.seconds, args.reference))
    print()

    if args.toggle:
        print("Block schedule - for attack and release, read each LOUD block several")
        print("times across its length rather than once in the middle:")
        print("  {:>7}  {:>7}  {:>10}  {:>10}  {}".format(
            "start", "end", "peak dBFS", "RMS dBFS", "edge"))
        t = args.seconds
        prev = args.reference
        for level in levels:
            edge = "attack" if level > prev else "release"
            print("  {:>7.1f}  {:>7.1f}  {:>10.1f}  {:>10.2f}  {}".format(
                t, t + args.seconds, level, level - 3.01, edge))
            t += args.seconds
            prev = level
        print()
        print("A gain that is still moving reads differently at the start of a block")
        print("than at the end. If a loud block is loudest at its onset and settles")
        print("downward, that decay IS the attack, and its shape gives the time")
        print("constant directly - roughly the time to cover 63% of the total drop.")
        print("If start and end agree, the attack is faster than the block and this")
        print("file cannot resolve it: shorten --seconds and try again.")
        return 0

    print("Step schedule - t is the mid-point of each plateau, measure around there:")
    print("  {:>7}  {:>10}  {:>10}".format("t (s)", "peak dBFS", "RMS dBFS"))
    t = args.seconds  # the reference tone occupies the first block
    for level in levels:
        print("  {:>7.1f}  {:>10.1f}  {:>10.2f}".format(t + args.seconds / 2.0,
                                                        level, level - 3.01))
        t += args.seconds
    print()

    print("Expected plateau spacing, for a step of {:g} dB:".format(args.step))
    print("  {:>7}  {:>10}".format("ratio", "spacing"))
    for ratio in (1.0, 2.0, 3.0, 4.0, 8.0, 20.0):
        print("  {:>6.0f}:1  {:>7.2f} dB".format(ratio, args.step / ratio))
    print()
    print("Below the knee every ratio gives the full {:g} dB. Above it, spacing".format(args.step))
    print("collapses to step/ratio. Read the ratio off the spacing, and the knee")
    print("off the level where the spacing changes.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
