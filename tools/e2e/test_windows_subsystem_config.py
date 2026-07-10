#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    vcxproj = (ROOT / "EGTRAIN/QEGTRAIN/QEGTRAIN.vcxproj").read_text(encoding="utf-8")

    if "WIN32_EXECUTABLE TRUE" not in cmake and "add_executable(QEGTRAIN WIN32" not in cmake:
        raise SystemExit("CMake QEGTRAIN target is not configured as a Windows GUI executable")

    if "<SubSystem>Console</SubSystem>" in vcxproj:
        raise SystemExit("Visual Studio project still links QEGTRAIN with the console subsystem")

    if vcxproj.count("<SubSystem>Windows</SubSystem>") < 2:
        raise SystemExit("Visual Studio Debug and Release configurations must use Windows subsystem")


if __name__ == "__main__":
    main()
