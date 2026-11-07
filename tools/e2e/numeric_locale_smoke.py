#!/usr/bin/env python3
"""Start QEGTRAIN on a scene with LANG set to a locale that writes numbers with a decimal comma.

usage: numeric_locale_smoke.py APP SCENE

On macOS and Linux, Qt sets the locale of the C library from LANG and the LC_ variables when the application object is
built. Scene files, legacy files and run output write numbers with a full stop in every locale, so QEGTRAIN must read
and write them in the C locale. This script sets LANG to the first locale of COMMA_LOCALES that is installed and runs
SCENE headless with -h 300 twice: once with LC_NUMERIC=C and once without it. Both runs must finish and write the same
files, apart from the wall-clock times in Computing_Times.txt. It then opens SCENE in the window in startup timing mode,
which starts the run and ends the program when the window first shows it, and requires exit code 0 there as well. When
none of the locales is installed, the script exits with 77, which CTest reports as skipped.
"""
import locale
import os
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Optional

COMMA_LOCALES = ("de_DE.UTF-8", "nl_NL.UTF-8", "fr_FR.UTF-8")
SKIP_RETURN_CODE = 77
RUN_TIMEOUT_SECONDS = 120
# Wall-clock timings: the only file that two runs of one build write differently.
TIMING_FILES = {"Computing_Times.txt"}


def comma_locale() -> Optional[str]:
    """The first locale of COMMA_LOCALES that is installed and writes a decimal comma, or None."""
    for name in COMMA_LOCALES:
        try:
            locale.setlocale(locale.LC_NUMERIC, name)
        except locale.Error:
            continue
        try:
            if locale.localeconv()["decimal_point"] == ",":
                return name
        finally:
            locale.setlocale(locale.LC_NUMERIC, "C")
    return None


def launch(app: Path, arguments: list[str], variables: dict[str, str], label: str) -> str:
    """Start APP with the given variables, which replace LANG and the LC_ variables, and return its output once it has
    exited with 0."""
    env = {key: value for key, value in os.environ.items() if key != "LANG" and not key.startswith("LC_")}
    env.update(variables)
    env["QT_QPA_PLATFORM"] = "offscreen"
    try:
        proc = subprocess.run([str(app), *arguments], cwd=app.parent, env=env, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, timeout=RUN_TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired as exc:
        tail = (exc.stdout or b"").decode("utf-8", errors="replace")[-4000:]
        raise SystemExit(f"{label} timed out after {RUN_TIMEOUT_SECONDS}s\n{tail}") from exc
    text = proc.stdout.decode("utf-8", errors="replace")
    if proc.returncode != 0:
        # Scene errors come before the warnings, which can fill the end of the output.
        errors = [line for line in text.splitlines() if line.lower().startswith("error")]
        shown = (f"{len(errors)} error lines, the first ones:\n" + "\n".join(errors[:20])) if errors else text[-4000:]
        raise SystemExit(f"{label} exited with {proc.returncode}\n{shown}")
    return text


def run_headless(app: Path, scene: str, output: Path, locale_variables: dict[str, str]) -> dict[str, bytes]:
    """Run the scene headless into the output folder and return the files that it wrote, by path."""
    label = "the headless run with " + " ".join(f"{key}={value}" for key, value in locale_variables.items())
    text = launch(app, ["--scene", scene, "-h", "300", "-g", "0"],
                  {**locale_variables, "QEGTRAIN_OUTPUT_DIR": str(output)}, label)
    if "End of Simulation" not in text:
        raise SystemExit(f"{label} printed no 'End of Simulation'\n{text[-4000:]}")
    files = {path.relative_to(output).as_posix(): path.read_bytes() for path in output.rglob("*")
             if path.is_file() and path.name not in TIMING_FILES}
    if not files:
        raise SystemExit(f"{label} wrote no output files")
    return files


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    app = Path(sys.argv[1]).resolve()
    scene = sys.argv[2]
    if not app.is_file():
        raise SystemExit(f"QEGTRAIN executable not found: {app}")
    name = comma_locale()
    if name is None:
        print(f"SKIP: no installed locale writes a decimal comma (tried {', '.join(COMMA_LOCALES)})")
        sys.exit(SKIP_RETURN_CODE)

    with tempfile.TemporaryDirectory(prefix="qegtrain-locale-") as temp:
        expected = run_headless(app, scene, Path(temp) / "numbers-c", {"LANG": name, "LC_NUMERIC": "C"})
        actual = run_headless(app, scene, Path(temp) / "numbers-comma", {"LANG": name})
        differing = sorted(path for path in expected.keys() | actual.keys() if expected.get(path) != actual.get(path))
        if differing:
            raise SystemExit(f"LANG={name} changed the output files: {', '.join(differing)}")
        print(f"PASS LANG={name}: the headless run wrote the {len(actual)} output files of the run with LC_NUMERIC=C")

        # Settings of its own, so that the window neither reads nor changes the settings of the user.
        launch(app, ["--scene", scene],
               {"LANG": name, "QEGTRAIN_STARTUP_TIMING": "1", "QEGTRAIN_E2E_SETTINGS_DIR": str(Path(temp) / "settings"),
                "QEGTRAIN_OUTPUT_DIR": str(Path(temp) / "window")},
               f"the window with LANG={name}")
        print(f"PASS LANG={name}: the window opened the scene and showed its run")


if __name__ == "__main__":
    main()
