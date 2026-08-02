#!/usr/bin/env python3
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def _has_direct_qlist_equality(text: str) -> bool:
    normalized = re.sub(r"[\s()]", "", text)
    return any(
        f"{left}.{field}=={right}.{field}" in normalized
        for field in ("items", "itemBounds")
        for left, right in (("left", "right"), ("right", "left"))
    )


def _has_direct_entry_label_equality(text: str) -> bool:
    normalized = re.sub(r"[\s()]", "", text)
    entry_labels = r"(?:[A-Za-z_]\w*(?:\.|->))*entryLabels"
    return any(
        re.search(rf"(?:{entry_labels}{operator}{other}|{other}{operator}{entry_labels})", normalized)
        for other in ("mapKeyEntries", "labelsBeforeCollapse")
        for operator in ("==", "!=")
    )


def test_preview_snapshot_comparator() -> None:
    for sample in (
        "( ( left . items ) ) == ( right.items )",
        "right . itemBounds\n== ((left.itemBounds))",
    ):
        if not _has_direct_qlist_equality(sample):
            raise SystemExit("QList equality detector misses reversed or parenthesized operands")

    for sample in (
        "entryLabels() != (mapKeyEntries)",
        "labelsBeforeCollapse == ((legend.entryLabels()))",
    ):
        if not _has_direct_entry_label_equality(sample):
            raise SystemExit("entryLabels comparison detector misses reversed or parenthesized operands")

    source = (ROOT / "EGTRAIN/QEGTRAIN/app/MainWindow.cpp").read_text(encoding="utf-8")
    test_source = (ROOT / "EGTRAIN/QEGTRAIN/tests/test_networklegend.cpp").read_text(encoding="utf-8")
    for path, text in (
        ("MainWindow.cpp", source),
        ("test_networklegend.cpp", test_source),
    ):
        if _has_direct_entry_label_equality(text):
            raise SystemExit(f"{path} still compares entryLabels directly")
    if not re.search(
        r"if\s*\(\s*actual\.size\(\)\s*!=\s*expected\.size\(\)\s*"
        r"\|\|\s*!std::equal\(actual\.begin\(\),\s*actual\.end\(\),\s*expected\.begin\(\)\)\s*\)",
        source,
    ):
        raise SystemExit("Tab traversal comparison must size-check before std::equal")
    match = re.search(
        r"auto samePreviewContent = \[\]\(const PreviewContentSnapshot& left, "
        r"const PreviewContentSnapshot& right\) \{(?P<body>.*?)\n\t\};",
        source,
        re.DOTALL,
    )
    if not match:
        raise SystemExit("samePreviewContent lambda is missing")
    comparator = match.group("body")

    if _has_direct_qlist_equality(comparator):
        raise SystemExit("samePreviewContent still compares QList fields directly")
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
