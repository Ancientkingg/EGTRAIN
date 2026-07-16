#!/usr/bin/env python3
import re
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE_ROOT = ROOT / "EGTRAIN/QEGTRAIN"
ASSETS = {
    "icons/station.png": "resources/icons/station.png",
    "icons/passenger.png": "resources/icons/passenger.png",
}
APP_ICON_SIZES = (16, 32, 48, 64, 128, 256, 512, 1024)
for _size in APP_ICON_SIZES:
    ASSETS[f"app/egtrain-{_size}.png"] = f"resources/app/egtrain-{_size}.png"


def main() -> None:
    qrc_path = SOURCE_ROOT / "app/resources.qrc"
    qrc_entries = {
        element.get("alias"): element.text
        for element in ET.parse(qrc_path).iterfind(".//file")
    }
    main_window = (SOURCE_ROOT / "app/MainWindow.cpp").read_text()
    cmake = (ROOT / "CMakeLists.txt").read_text()

    for alias, relative_path in ASSETS.items():
        if not (SOURCE_ROOT / relative_path).is_file():
            raise SystemExit(f"missing resource asset: {relative_path}")
        if qrc_entries.get(alias) != f"../{relative_path}":
            raise SystemExit(f"missing Qt resource entry: {alias}")
        if alias.startswith("app/"):
            if f'":/{alias}"' not in main_window:
                raise SystemExit(f"MainWindow does not load :/{alias}")
        elif f'QPixmap(":/{alias}")' not in main_window:
            raise SystemExit(f"MainWindow does not load :/{alias}")

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
