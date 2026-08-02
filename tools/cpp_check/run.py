#!/usr/bin/env python3
"""Compile the real C++ DRC encoders and check them against the Python reference.

tools/drc_math_check.py proves the *algorithm* matches TI's documented constants.
This proves the *shipping C++* matches that algorithm, by compiling
components/tas58xx/tas58xx_helpers.cpp for the host against stub esphome headers
and diffing its output against the same Python encoders.

Needs a host C++ compiler. Tries, in order: `zig c++` via the ziglang wheel
(pip install ziglang), then g++, then clang++.

Run: python tools/cpp_check/run.py
"""

import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
COMPONENT = os.path.join(ROOT, "components", "tas58xx")

sys.path.insert(0, os.path.join(ROOT, "tools"))
import drc_math_check as ref  # noqa: E402


def find_compiler():
    try:
        import ziglang  # noqa: F401
        return [sys.executable, "-m", "ziglang", "c++"]
    except ImportError:
        pass
    for exe in ("g++", "clang++"):
        if shutil.which(exe):
            return [exe]
    return None


def build_and_run(outdir):
    compiler = find_compiler()
    if compiler is None:
        print("No host C++ compiler found. Install one with: pip install ziglang")
        return None

    binary = os.path.join(outdir, "drccheck.exe")
    cmd = compiler + [
        "-std=c++17", "-Wall", "-Wextra", "-O2",
        "-Wno-nullability-completeness",   # noise from zig's bundled libcxx
        "-I", os.path.join(HERE, "stub"),
        "-I", COMPONENT,
        "-o", binary,
        os.path.join(HERE, "drc_cpp_check.cpp"),
        os.path.join(COMPONENT, "tas58xx_helpers.cpp"),
    ]
    print("Compiling with:", " ".join(compiler))
    build = subprocess.run(cmd, capture_output=True, text=True)

    # Surface only diagnostics about our own sources, not the toolchain's headers.
    ours = [ln for ln in build.stderr.splitlines()
            if "tas58xx" in ln or "drc_cpp_check" in ln]
    if ours:
        print("\n".join(ours))
    if build.returncode != 0:
        print("COMPILE FAILED")
        return None
    if not ours:
        print("  no warnings from component sources")

    run = subprocess.run([binary], capture_output=True, text=True)
    if run.returncode != 0:
        print("RUN FAILED")
        return None
    return run.stdout


def expected():
    """Same quantities the C++ harness prints, computed in Python."""
    lpf = ref.encode_biquad(ref.design_lowpass(300.0, 96000), mixed_format=False)
    hpf = ref.encode_biquad(ref.design_highpass(3000.0, 96000), mixed_format=False)
    pt_5827 = ref.encode_biquad((1.0, 0.0, 0.0, 0.0, 0.0), mixed_format=False)
    pt_131 = ref.encode_biquad((1.0, 0.0, 0.0, 0.0, 0.0), mixed_format=True)

    return {
        "T1_-50": [ref.db_to_f9_23(-50.0)],
        "T2_-3": [ref.db_to_f9_23(-3.0)],
        "off_0": [ref.db_to_f9_23(0.0)],
        "mixer_unity": [ref.linear_db_to_f9_23(0.0)],
        "slope_ratio1": [ref.slope_to_f9_23(ref.ratio_to_slope(1.0))],
        "slope_ratio2": [ref.slope_to_f9_23(ref.ratio_to_slope(2.0))],
        "slope_ratio4": [ref.slope_to_f9_23(ref.ratio_to_slope(4.0))],
        "slope_ratio10": [ref.slope_to_f9_23(ref.ratio_to_slope(10.0))],
        "agl_attack_100ms": [ref.time_constant_to_f1_31(100.0, 96000)],
        "agl_release_1s": [ref.time_constant_to_f1_31(1000.0, 96000)],
        "tc_instant": [ref.time_constant_to_f1_31(0.0, 96000)],
        "pass5827_B0": [pt_5827[0]],
        "pass5827_B1": [pt_5827[1]],
        "pass131_B0": [pt_131[0]],
        "pass131_A2": [pt_131[4]],
        "lpf300_5827": lpf,
        "hpf3000_5827": hpf,
    }


def main():
    with tempfile.TemporaryDirectory() as outdir:
        output = build_and_run(outdir)
    if output is None:
        return 1

    got = {}
    for line in output.splitlines():
        parts = line.split()
        if len(parts) >= 2:
            got[parts[0]] = [int(v, 16) for v in parts[1:]]

    want = expected()
    failures = []

    print()
    for label, want_values in want.items():
        got_values = got.get(label)
        if got_values is None:
            failures.append(f"{label} (missing from C++ output)")
            print(f"  [FAIL] {label:<18} not printed by the C++ harness")
            continue
        ok = got_values == want_values
        if not ok:
            failures.append(label)
        print(f"  [{'ok  ' if ok else 'FAIL'}] {label:<18} "
              + " ".join(f"0x{v:08X}" for v in got_values))
        if not ok:
            print(f"         {'':<18} " + " ".join(f"0x{v:08X}" for v in want_values)
                  + "   <- python reference")

    print()
    if failures:
        print(f"FAILED: C++ disagrees with the reference on {len(failures)}: "
              + ", ".join(failures))
        return 1
    print("All checks passed - shipping C++ encoders match the Python reference "
          "bit-for-bit.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
