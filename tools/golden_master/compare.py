#!/usr/bin/env python3
"""
EGTRAIN Golden-Master Comparator

Token-wise diff of simulation output files with numeric tolerance.
Compares all files in a current run directory against a baseline directory,
reporting the first divergence per file.

Usage:
    # Compare current run against baseline
    python compare.py golden/case1 --current Output/Output_EGTRAIN_Netherlands

    # Compare all 4 cases (current is parent of all output dirs)
    python compare.py golden --current ../QEGTRAIN/Output

    # Re-baseline after verifying a change is correct
    python compare.py golden --current Output_EGTRAIN_Paimpol --update
"""

import argparse
import os
import re
import sys


# Numeric comparison tolerance
REL_TOL = 1e-9
ABS_TOL = 1e-9

# Files to skip in comparison (auto-generated, non-reproducible, or metadata)
SKIP_PATTERNS = [
    r'.*\.html?$',          # HTML timetable graphs (contain timestamps)
    r'.*[Ll]ogging?.*',     # Log files
    r'.*\.log$',            # Log files
    r'stdout\.txt',          # stdout (contains run-specific timestamps/paths)
    r'rand1\.seed',          # Seed file (overwritten by each run)
]


def should_skip(relpath: str) -> bool:
    return any(re.match(p, relpath) for p in SKIP_PATTERNS)


def is_numeric(token: str) -> bool:
    """Check if a token is a number (int, float, scientific notation)."""
    try:
        float(token)
        return True
    except ValueError:
        return False


def is_integer_token(token: str) -> bool:
    """Check if a token represents an integer value (not a label)."""
    try:
        int(token)
        return True
    except ValueError:
        return False


def tokens_close(a: str, b: str, rel_tol: float, abs_tol: float) -> bool:
    """Compare two tokens with numeric tolerance if they're numbers."""
    if a == b:
        return True
    if is_numeric(a) and is_numeric(b):
        fa, fb = float(a), float(b)
        if fa == fb:
            return True
        diff = abs(fa - fb)
        magnitude = max(abs(fa), abs(fb), 1.0)
        return diff <= max(rel_tol * magnitude, abs_tol)
    return False


def compare_file(baseline_path: str, current_path: str,
                 rel_tol: float = REL_TOL, abs_tol: float = ABS_TOL):
    """
    Compare two text files and yield (line_no, col_no, expected, actual, delta)
    for each mismatch. Stops at the first mismatch per file.
    """
    with open(baseline_path, 'r') as f:
        baseline_lines = f.readlines()
    with open(current_path, 'r') as f:
        current_lines = f.readlines()

    max_lines = max(len(baseline_lines), len(current_lines))

    for line_no in range(max_lines):
        # Line numbers are 1-based
        lineno = line_no + 1

        if line_no >= len(baseline_lines):
            yield (lineno, 0, '<EOF>', current_lines[line_no].rstrip(), None)
            return
        if line_no >= len(current_lines):
            yield (lineno, 0, baseline_lines[line_no].rstrip(), '<EOF>', None)
            return

        bline = baseline_lines[line_no].rstrip('\n').rstrip('\r')
        cline = current_lines[line_no].rstrip('\n').rstrip('\r')

        # Split by whitespace
        btokens = bline.split() if bline else []
        ctokens = cline.split() if cline else []

        max_tokens = max(len(btokens), len(ctokens))

        for col_no in range(max_tokens):
            # Column numbers are 1-based
            col = col_no + 1

            if col_no >= len(btokens):
                yield (lineno, col, '<EMPTY>', ctokens[col_no], None)
                return
            if col_no >= len(ctokens):
                yield (lineno, col, btokens[col_no], '<EMPTY>', None)
                return

            btok = btokens[col_no]
            ctok = ctokens[col_no]

            if not tokens_close(btok, ctok, rel_tol, abs_tol):
                delta = None
                if is_numeric(btok) and is_numeric(ctok):
                    delta = abs(float(btok) - float(ctok))
                yield (lineno, col, btok, ctok, delta)
                return


def collect_files(root_dir: str) -> list[tuple[str, str]]:
    """Recursively collect (relative_path, absolute_path) pairs."""
    files = []
    for dirpath, dirnames, filenames in os.walk(root_dir):
        for fn in filenames:
            full = os.path.join(dirpath, fn)
            rel = os.path.relpath(full, root_dir)
            files.append((rel, full))
    return sorted(files)


def run_comparison(baseline_root: str, current_root: str,
                   update: bool = False):
    """Compare all files under current_root against baseline_root."""
    baseline_files = collect_files(baseline_root)
    current_files = collect_files(current_root)

    baseline_map = {rel: path for rel, path in baseline_files}
    current_map = {rel: path for rel, path in current_files}

    all_rels = sorted(set(list(baseline_map.keys()) + list(current_map.keys())))

    total_mismatches = 0
    total_errors = 0
    skipped_count = 0

    for rel in all_rels:
        if should_skip(rel):
            skipped_count += 1
            continue

        in_baseline = rel in baseline_map
        in_current = rel in current_map

        if not in_current:
            print(f"[MISSING] {rel}  (in baseline, not in current)")
            total_mismatches += 1
            continue

        if not in_baseline:
            print(f"[NEW]     {rel}  (in current, not in baseline)")
            total_mismatches += 1
            continue

        if update:
            import shutil
            os.makedirs(os.path.dirname(os.path.join(baseline_root, rel)),
                        exist_ok=True)
            shutil.copy2(current_map[rel], baseline_map[rel])
            continue

        try:
            mismatches = list(compare_file(baseline_map[rel], current_map[rel]))
            if mismatches:
                lineno, col, expected, actual, delta = mismatches[0]
                delta_str = ""
                if delta is not None:
                    delta_str = f" (delta={delta:.6g})"
                print(f"[DIFF]    {rel}  "
                      f"line {lineno} col {col}: "
                      f"expected '{expected}' got '{actual}'{delta_str}")
                total_mismatches += 1
        except Exception as e:
            print(f"[ERROR]   {rel}: {e}")
            total_errors += 1

    return total_mismatches, total_errors, skipped_count


def main():
    parser = argparse.ArgumentParser(
        description="EGTRAIN golden-master regression comparator")
    parser.add_argument("baseline",
                        help="Path to baseline directory (e.g. golden/case1)")
    parser.add_argument("--current", "-c", default=None,
                        help="Path to current run output directory. "
                             "If baseline ends with case{N}, "
                             "defaults to Output/Output_EGTRAIN_<case>")
    parser.add_argument("--update", "-u", action="store_true",
                        help="Update baseline with current output "
                             "(copy current into baseline)")
    args = parser.parse_args()

    # Auto-detect current directory if not provided
    current_root = args.current
    if current_root is None:
        case_match = re.match(r'.*[Cc]ase(\d)$', args.baseline)
        if case_match:
            case_num = int(case_match.group(1))
            case_dirs = {
                1: "../QEGTRAIN/Output/Output_EGTRAIN_Netherlands",
                2: "../QEGTRAIN/Output/Output_EGTRAIN_Paimpol",
                3: "../QEGTRAIN/Output/Output_EGTRAIN_Banedanmark",
                4: "../QEGTRAIN/Output/Output_EGTRAIN_Milano_Brescia",
            }
            if case_num in case_dirs:
                # Resolve relative to baseline dir
                current_root = os.path.normpath(
                    os.path.join(os.path.dirname(args.baseline),
                                 case_dirs[case_num]))
                if not os.path.isdir(current_root):
                    print(f"Auto-detected current dir: {current_root}")
                    print("(not found, specify --current explicitly)")
                    sys.exit(1)
                print(f"Auto-detected current dir: {current_root}")

    if not os.path.isdir(args.baseline):
        print(f"ERROR: baseline directory not found: {args.baseline}")
        sys.exit(1)

    if current_root and not os.path.isdir(current_root):
        print(f"ERROR: current directory not found: {current_root}")
        sys.exit(1)

    baseline_root = os.path.abspath(args.baseline)
    if current_root:
        current_root = os.path.abspath(current_root)

    print(f"Baseline: {baseline_root}")
    print(f"Current:  {current_root or '<not specified>'}")
    print()

    mismatches, errors, skipped = run_comparison(
        baseline_root, current_root, update=args.update)

    print()
    print(f"Summary: {mismatches} file(s) with mismatches, "
          f"{errors} file(s) with errors, {skipped} file(s) skipped")

    if args.update:
        print("Baseline updated.")

    return 1 if (mismatches > 0 or errors > 0) else 0


if __name__ == "__main__":
    sys.exit(main())
