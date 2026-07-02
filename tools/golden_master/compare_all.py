#!/usr/bin/env python3
"""
Convenience wrapper: compare all 4 case studies against their baselines.
Runs compare.py for case{1,2,3,4} in sequence with auto-detected current dirs.
"""

import argparse
import os
import subprocess
import sys


CASES = [
    (1, "Output_EGTRAIN_Netherlands"),
    (2, "Output_EGTRAIN_Paimpol"),
    (3, "Output_EGTRAIN_Banedanmark"),
    (4, "Output_EGTRAIN_Milano_Brescia"),
]


def main():
    parser = argparse.ArgumentParser(
        description="Compare all 4 EGTRAIN case studies against baselines")
    parser.add_argument("--golden", "-g", default="golden",
                        help="Path to golden-master root (default: golden)")
    parser.add_argument("--current", "-c", default=None,
                        help="Base path for current outputs "
                             "(default: ../QEGTRAIN/Output)")
    parser.add_argument("--update", "-u", action="store_true",
                        help="Update baselines with current outputs")
    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    compare_script = os.path.join(script_dir, "compare.py")
    golden_root = os.path.join(script_dir, args.golden)

    current_base = args.current
    if current_base is None:
        current_base = os.path.normpath(
            os.path.join(script_dir, "../QEGTRAIN/Output"))

    if not os.path.isdir(golden_root):
        print(f"ERROR: golden dir not found: {golden_root}")
        print("Run capture.bat first to create baselines.")
        sys.exit(1)

    all_ok = True

    for case_num, case_dir in CASES:
        baseline = os.path.join(golden_root, f"case{case_num}")
        current = os.path.join(current_base, case_dir)

        if not os.path.isdir(baseline):
            print(f"[SKIP] case{case_num}: "
                  f"baseline not found at {baseline}")
            continue

        if not os.path.isdir(current):
            print(f"[SKIP] case{case_num}: "
                  f"current not found at {current}")
            continue

        print(f"\n{'='*60}")
        print(f" Case {case_num} ({case_dir})")
        print(f"{'='*60}")
        sys.stdout.flush()

        cmd = [sys.executable, compare_script, baseline,
               "--current", current]
        if args.update:
            cmd.append("--update")

        result = subprocess.run(cmd)
        if result.returncode != 0:
            all_ok = False

    if all_ok:
        print(f"\n{'='*60}")
        print(" ALL CASES PASSED")
        print(f"{'='*60}")
    else:
        print(f"\n{'='*60}")
        print(" SOME CASES HAVE MISMATCHES")
        print(f"{'='*60}")
        sys.exit(1)


if __name__ == "__main__":
    main()
