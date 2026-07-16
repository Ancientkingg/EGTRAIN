#!/usr/bin/env python3
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def test_preview_snapshot_comparator() -> None:
    source = (ROOT / "EGTRAIN/QEGTRAIN/app/MainWindow.cpp").read_text(encoding="utf-8")
    match = re.search(
        r"auto samePreviewContent = \[\]\(const PreviewContentSnapshot& left, "
        r"const PreviewContentSnapshot& right\) \{(?P<body>.*?)\n\t\};",
        source,
        re.DOTALL,
    )
    if not match:
        raise SystemExit("samePreviewContent lambda is missing")
    comparator = match.group("body")

    if "left.items == right.items" in comparator:
        raise SystemExit("samePreviewContent still compares QList items directly")
    if "left.itemBounds == right.itemBounds" in comparator:
        raise SystemExit("samePreviewContent still compares QList item bounds directly")
    for requirement in (
        "left.items.size() != right.items.size()",
        "left.itemBounds.size() != right.itemBounds.size()",
        "left.items.at(index) != right.items.at(index)",
        "left.itemBounds.at(index) != right.itemBounds.at(index)",
        "left.bounds != right.bounds",
        "return false;",
        "return true;",
    ):
        if requirement not in comparator:
            raise SystemExit(f"samePreviewContent is missing: {requirement}")


def main() -> None:
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    vcxproj = (ROOT / "EGTRAIN/QEGTRAIN/QEGTRAIN.vcxproj").read_text(encoding="utf-8")

    if "WIN32_EXECUTABLE TRUE" not in cmake and "add_executable(QEGTRAIN WIN32" not in cmake:
        raise SystemExit("CMake QEGTRAIN target is not configured as a Windows GUI executable")

    if "<SubSystem>Console</SubSystem>" in vcxproj:
        raise SystemExit("Visual Studio project still links QEGTRAIN with the console subsystem")

    if vcxproj.count("<SubSystem>Windows</SubSystem>") < 2:
        raise SystemExit("Visual Studio Debug and Release configurations must use Windows subsystem")

    test_preview_snapshot_comparator()


if __name__ == "__main__":
    main()
