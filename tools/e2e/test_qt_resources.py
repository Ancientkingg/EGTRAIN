#!/usr/bin/env python3
import re
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE_ROOT = ROOT / "EGTRAIN/QEGTRAIN"
ENTITY_ASSETS = {
    "icons/station-stop.svg": "resources/icons/station-stop.svg",
    "icons/station-platform.svg": "resources/icons/station-platform.svg",
    "icons/station-interchange.svg": "resources/icons/station-interchange.svg",
    "icons/passenger.svg": "resources/icons/passenger.svg",
    "icons/train-passenger.svg": "resources/icons/train-passenger.svg",
    "icons/train-sprinter.svg": "resources/icons/train-sprinter.svg",
    "icons/train-intercity.svg": "resources/icons/train-intercity.svg",
    "icons/train-high-speed.svg": "resources/icons/train-high-speed.svg",
    "icons/train-freight.svg": "resources/icons/train-freight.svg",
    "icons/signal-neutral.svg": "resources/icons/signal-neutral.svg",
    "icons/signal-stop.svg": "resources/icons/signal-stop.svg",
    "icons/signal-caution.svg": "resources/icons/signal-caution.svg",
    "icons/signal-proceed.svg": "resources/icons/signal-proceed.svg",
}
ASSETS = dict(ENTITY_ASSETS)
APP_ICON_SIZES = (16, 32, 48, 64, 128, 256, 512, 1024)
for _size in APP_ICON_SIZES:
    ASSETS[f"app/egtrain-{_size}.png"] = f"resources/app/egtrain-{_size}.png"


def main() -> None:
    qrc_path = SOURCE_ROOT / "app/resources.qrc"
    qrc_entries = {
        element.get("alias"): element.text
        for element in ET.parse(qrc_path).iterfind(".//file")
    }
    source_text = "\n".join(
        path.read_text(errors="replace")
        for source_root in (SOURCE_ROOT / "app", SOURCE_ROOT / "graphics")
        for path in source_root.rglob("*")
        if path.suffix in {".cpp", ".h"}
    )
    cmake = (ROOT / "CMakeLists.txt").read_text()

    for alias, relative_path in ASSETS.items():
        if not (SOURCE_ROOT / relative_path).is_file():
            raise SystemExit(f"missing resource asset: {relative_path}")
        if qrc_entries.get(alias) != f"../{relative_path}":
            raise SystemExit(f"missing Qt resource entry: {alias}")
        if f'":/{alias}"' not in source_text:
            raise SystemExit(f"application and graphics source do not reference :/{alias}")

    if not any(f'":/{alias}"' in source_text for alias in ENTITY_ASSETS):
        raise SystemExit("application and graphics source do not reference an SVG resource")

    if "find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Charts Svg)" not in cmake:
        raise SystemExit("CMake does not require Qt5 Svg")
    if "Qt5::Svg" not in cmake:
        raise SystemExit("CMake does not link Qt5::Svg")

    release = (ROOT / ".github/workflows/release.yml").read_text()
    if re.search(r"^\s*modules:.*\bqtsvg\b", release, re.MULTILINE):
        raise SystemExit("Windows release job lists qtsvg as an optional module")
    for requirement in ("Qt5Svg.dll", '$dist/imageformats/qsvg.dll'):
        if requirement not in release:
            raise SystemExit(f"Windows release job does not verify {requirement}")
    if "libqt5svg5-dev" not in release:
        raise SystemExit("Linux release job does not install libqt5svg5-dev")

    for relative_path in ("resources/icons/station.png", "resources/icons/passenger.png"):
        if (SOURCE_ROOT / relative_path).exists():
            raise SystemExit(f"legacy entity icon still exists: {relative_path}")

    old_names = ("train_station.png", "pax_icon.png", "window_icon.jpg", "app/window.jpg")
    for name in old_names:
        if (SOURCE_ROOT / name).exists():
            raise SystemExit(f"loose image asset still exists: {name}")
        if name in cmake:
            raise SystemExit(f"CMake still copies image asset: {name}")

    if '"${SRC_DIR}/Input"' not in cmake:
        raise SystemExit("CMake no longer copies Input data")
    cmake_requirements = (
        "set(CMAKE_AUTORCC ON)",
        "set(EGTRAIN_RESOURCES",
        "${SRC_DIR}/app/resources.qrc",
        "${EGTRAIN_RESOURCES}",
    )
    for requirement in cmake_requirements:
        if requirement not in cmake:
            raise SystemExit(f"CMake does not wire Qt resources: {requirement}")

    bare_load_patterns = (
        re.compile(r'Q(?:Pixmap|Icon|Image)(?:\s+\w+)?\s*[\(\{]\s*"(?!:/)[^"]+\.(?:png|jpe?g|svg|ico)"'),
        re.compile(r'\.(?:load|addFile)\s*\(\s*"(?!:/)[^"]+\.(?:png|jpe?g|svg|ico)"'),
        re.compile(r'url\s*\(\s*["\']?(?!:/)[^)"\']+\.(?:png|jpe?g|svg|ico)', re.IGNORECASE),
        re.compile(r'<(?:pixmap|\w+(?:off|on))>(?!:/)[^<]+\.(?:png|jpe?g|svg|ico)<', re.IGNORECASE),
    )
    for path in SOURCE_ROOT.rglob("*"):
        if path.suffix in {".cpp", ".h", ".ui", ".qss"}:
            source = path.read_text(errors="replace")
            for pattern in bare_load_patterns:
                match = pattern.search(source)
                if match:
                    raise SystemExit(f"bare GUI image load in {path.relative_to(ROOT)}: {match.group(0)}")


if __name__ == "__main__":
    main()
