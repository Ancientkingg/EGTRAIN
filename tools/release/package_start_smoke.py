#!/usr/bin/env python3
"""Start QEGTRAIN from an assembled package directory with nothing else to load from.

usage: package_start_smoke.py PACKAGE_DIR [--platform NAME]

The package must bring every library and plugin that QEGTRAIN needs. Both
launches get a search path with only the operating system directories and no
Qt or vcpkg variable, so a library that is missing from the package makes the
launch fail here as it would on a machine without a developer setup.

1. Headless (-g 0) on the packaged Paimpol scene for a short horizon, to the
   end of the run.
2. With a window, in startup timing mode: the application opens the scene,
   prepares a run, paints it and exits.

Before the launches it checks that no file of the package is an OpenMP runtime
library or names one. QEGTRAIN does not use OpenMP, and the package ships no
such library, so an executable that asks for one would not start everywhere.

--platform sets QT_QPA_PLATFORM for the second launch. Without it the platform
plugin of the package is used, which is what a user gets.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

RUN_TIMEOUT_SECONDS = 120
HORIZON_SECONDS = "120"
# Variables that point a process at a developer installation of Qt or vcpkg.
DEVELOPER_PREFIXES = ("QT_", "QML", "QT5", "VCPKG", "CMAKE_")
# File names of the OpenMP runtime of MSVC, LLVM, Intel and GCC.
OPENMP_RUNTIME = re.compile(rb"vcomp\d+d?\.dll|lib(?:i?omp|gomp)[\w.]*?\.(?:dll|dylib|so)", re.IGNORECASE)


def find_package_layout(package: Path):
    """Returns the executable and the scene directory of a Windows, Linux or macOS package."""
    candidates = (
        (package / "QEGTRAIN.exe", package / "Scenes"),
        (package / "QEGTRAIN.app/Contents/MacOS/QEGTRAIN", package / "QEGTRAIN.app/Contents/Resources/Scenes"),
        (package / "QEGTRAIN", package / "Scenes"),
    )
    for executable, scenes in candidates:
        if executable.is_file():
            return executable, scenes / "Paimpol"
    raise SystemExit(f"no QEGTRAIN executable in {package}")


def check_no_openmp_runtime(package: Path) -> None:
    for path in sorted(package.rglob("*")):
        if not path.is_file():
            continue
        if OPENMP_RUNTIME.match(path.name.encode()):
            raise SystemExit(f"the package contains an OpenMP runtime library: {path}")
        named = OPENMP_RUNTIME.search(path.read_bytes())
        if named:
            raise SystemExit(f"{path} names the OpenMP runtime library {named.group().decode()}")
    print(f"PASS no OpenMP runtime library in {package}")


def clean_environment() -> dict:
    env = {name: value for name, value in os.environ.items()
           if not name.upper().startswith(DEVELOPER_PREFIXES)}
    if os.name == "nt":
        root = os.environ.get("SystemRoot", r"C:\Windows")
        env["PATH"] = os.pathsep.join((str(Path(root) / "System32"), root))
    else:
        env["PATH"] = "/usr/bin:/bin"
    return env


def launch(command, cwd: Path, env: dict, what: str) -> str:
    try:
        proc = subprocess.run(command, cwd=cwd, env=env, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, timeout=RUN_TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired as exc:
        tail = (exc.stdout or b"").decode("utf-8", errors="replace")[-4000:]
        raise SystemExit(f"{what} timed out after {RUN_TIMEOUT_SECONDS}s\n{tail}") from exc
    text = proc.stdout.decode("utf-8", errors="replace")
    if proc.returncode != 0:
        raise SystemExit(f"{what} exited with {proc.returncode}\n{text[-4000:]}")
    return text


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("package", type=Path)
    parser.add_argument("--platform")
    args = parser.parse_args()

    package = args.package.resolve()
    executable, scene = find_package_layout(package)
    if not (scene / "scene.json").is_file():
        raise SystemExit(f"the package has no Paimpol scene at {scene}")
    check_no_openmp_runtime(package)

    with tempfile.TemporaryDirectory(prefix="qegtrain-package-") as temp:
        env = clean_environment()
        env["QEGTRAIN_OUTPUT_DIR"] = str(Path(temp) / "headless")
        text = launch([str(executable), "--scene", str(scene), "-h", HORIZON_SECONDS, "-g", "0",
                       "-pax", "0", "-TSM", "0", "-RC", "0"], executable.parent, env, "headless run")
        if "End of Simulation" not in text:
            raise SystemExit(f"headless run printed no 'End of Simulation'\n{text[-4000:]}")
        energy = list((Path(temp) / "headless" / "Output").glob("*/EnergyConsumptionPerTrain.txt"))
        if len(energy) != 1:
            raise SystemExit(f"expected one EnergyConsumptionPerTrain.txt, found {len(energy)}\n{text[-4000:]}")
        print(f"PASS headless run of {scene.name} from {package}")

        env = clean_environment()
        env["QEGTRAIN_OUTPUT_DIR"] = str(Path(temp) / "window")
        env["QEGTRAIN_STARTUP_TIMING"] = "1"
        env["QEGTRAIN_STARTUP_WARM_TRIALS"] = "0"
        if args.platform:
            env["QT_QPA_PLATFORM"] = args.platform
        text = launch([str(executable), "--scene", str(scene)], executable.parent, env, "window start")
        if "first_runtime_paint" not in text:
            raise SystemExit(f"window start did not paint the prepared run\n{text[-4000:]}")
        print(f"PASS window start of {scene.name} from {package}")


if __name__ == "__main__":
    main()
