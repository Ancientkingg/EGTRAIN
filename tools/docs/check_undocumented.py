#!/usr/bin/env python3
"""Fails when a change adds undocumented C++ classes or members to the files it changes.

  python3 tools/docs/check_undocumented.py BASE    compare the working tree with the Git revision BASE

Doxygen runs with docs/Doxyfile and EXTRACT_ALL set to NO, once on the working tree and once on a copy of
BASE, and reports its "is not documented" warnings. Private members are not reported. A warning in a file
that changed since BASE is new when the file has more of it than in BASE. Line numbers are ignored, so
moved code is not new, and a renamed file is compared with its old name.

Each new warning is printed as path: text, followed by a summary line. The exit code is 0 when no warning
is new, 1 when there are new warnings and 2 when the check cannot run.
"""
import io
import os
import re
import subprocess
import sys
import tarfile
import tempfile
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WARNING = re.compile(r"^(.+?):\d+: warning: (.+ is not documented\.)$")
CHECK_SETTINGS = "\nEXTRACT_ALL = NO\nGENERATE_HTML = NO\nHAVE_DOT = NO\nOUTPUT_DIRECTORY = {output}\nWARN_LOGFILE = {log}\n"


def parse_warnings(text: str, tree: str) -> Counter:
    """Counts the warnings of a Doxygen log by file, relative to tree, and text."""
    found = Counter()
    for line in text.splitlines():
        match = WARNING.match(line)
        if match:
            path = Path(os.path.relpath(match.group(1), tree)).as_posix()
            found[(path, match.group(2))] += 1
    return found


def new_warnings(base: Counter, head: Counter, changed: dict) -> list:
    """Lists the warnings of head that base does not have, in the files of changed, which maps each changed
    path to its path in base."""
    new_names = {old: new for new, old in changed.items()}
    renamed = Counter()
    for (path, text), count in base.items():
        renamed[(new_names.get(path, path), text)] += count
    extra = head - renamed
    return sorted(f"{path}: {text}" for (path, text), count in extra.items() if path in changed for _ in range(count))


def git(*arguments, **options):
    return subprocess.run(["git", "-C", str(ROOT), *arguments], check=True, capture_output=True, **options).stdout


def changed_files(base: str) -> dict:
    changed = {}
    for line in git("diff", "--name-status", "-M", base, text=True).splitlines():
        status, *paths = line.split("\t")
        changed[paths[-1]] = paths[0] if status.startswith("R") else paths[-1]
    return changed


def doxygen_warnings(tree: Path, config: str, output: Path) -> Counter:
    output.mkdir()
    log = output / "warnings.log"
    settings = CHECK_SETTINGS.format(output=output.as_posix(), log=log.as_posix())
    subprocess.run(["doxygen", "-"], input=config + settings, cwd=tree, check=True, capture_output=True, text=True)
    return parse_warnings(log.read_text(encoding="utf-8"), str(tree))


def main(argv=None) -> int:
    arguments = sys.argv[1:] if argv is None else argv
    if len(arguments) != 1:
        print(__doc__.strip().splitlines()[2].strip(), file=sys.stderr)
        return 2
    base = arguments[0]
    config = (ROOT / "docs/Doxyfile").read_text(encoding="utf-8")
    inputs = re.search(r"^INPUT\s*=(.*)$", config, re.MULTILINE).group(1).split()
    try:
        changed = changed_files(base)
        with tempfile.TemporaryDirectory() as directory:
            scratch = Path(directory).resolve()
            base_tree = scratch / "base"
            with tarfile.open(fileobj=io.BytesIO(git("archive", base, "--", *inputs))) as archive:
                archive.extractall(base_tree, filter="data")
            base_warnings = doxygen_warnings(base_tree, config, scratch / "base-output")
            head_warnings = doxygen_warnings(ROOT, config, scratch / "head-output")
    except subprocess.CalledProcessError as error:
        print(f"check_undocumented: {error}\n{error.stderr}", file=sys.stderr)
        return 2
    except OSError as error:
        print(f"check_undocumented: {error}", file=sys.stderr)
        return 2
    found = new_warnings(base_warnings, head_warnings, changed)
    for warning in found:
        print(warning)
    print(f"{len(found)} new undocumented " + ("entity" if len(found) == 1 else "entities") + f" since {base}")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
