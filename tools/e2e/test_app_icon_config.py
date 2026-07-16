#!/usr/bin/env python3
import struct
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE_ROOT = ROOT / "EGTRAIN/QEGTRAIN"
APP_ROOT = SOURCE_ROOT / "resources/app"
ICON_SIZES = (16, 32, 48, 64, 128, 256, 512, 1024)
PALETTE = ("#101A22", "#0F7C73", "#F8FAFB", "#D94A5A")


def png_size(path: Path) -> tuple[int, int]:
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        raise SystemExit(f"invalid PNG header: {path.relative_to(ROOT)}")
    return struct.unpack(">II", data[16:24])


def ico_sizes(path: Path) -> set[tuple[int, int]]:
    data = path.read_bytes()
    if data[:4] != b"\x00\x00\x01\x00":
        raise SystemExit("invalid ICO header")
    count = struct.unpack_from("<H", data, 4)[0]
    sizes = set()
    for index in range(count):
        width, height = struct.unpack_from("<BB", data, 6 + index * 16)
        sizes.add((width or 256, height or 256))
    return sizes


def require(text: str, needle: str, message: str) -> None:
    if needle not in text:
        raise SystemExit(message)


def main() -> None:
    svg_path = APP_ROOT / "egtrain.svg"
    if not svg_path.is_file():
        raise SystemExit("missing resources/app/egtrain.svg")
    svg_text = svg_path.read_text(encoding="utf-8")
    svg = ET.fromstring(svg_text)
    if svg.get("viewBox") != "0 0 1024 1024":
        raise SystemExit("SVG viewBox must be 0 0 1024 1024")
    for color in PALETTE:
        if color not in svg_text:
            raise SystemExit(f"SVG is missing palette color {color}")

    for size in ICON_SIZES:
        path = APP_ROOT / f"egtrain-{size}.png"
        if not path.is_file():
            raise SystemExit(f"missing {path.relative_to(ROOT)}")
        if png_size(path) != (size, size):
            raise SystemExit(f"{path.name} has the wrong dimensions")

    icns = APP_ROOT / "egtrain.icns"
    if not icns.read_bytes().startswith(b"icns"):
        raise SystemExit("egtrain.icns is missing the icns header")
    ico = APP_ROOT / "egtrain.ico"
    if not ico_sizes(ico).issuperset({(16, 16), (32, 32), (48, 48), (256, 256)}):
        raise SystemExit("egtrain.ico is missing a required resolution")

    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    require(cmake, "set(EGTRAIN_PLATFORM_RESOURCES", "CMake does not define platform resources")
    require(cmake, 'MACOSX_PACKAGE_LOCATION "Resources"', "CMake does not package the macOS icon")
    require(cmake, 'MACOSX_BUNDLE_ICON_FILE "egtrain.icns"', "CMake does not set the macOS icon name")
    require(cmake, "EGTRAIN_WINDOWS_ICON", "CMake does not configure the Windows icon")
    require(cmake, "egtrain.rc.in", "CMake does not configure the Windows resource template")
    require(cmake, "${EGTRAIN_PLATFORM_RESOURCES}", "CMake does not add platform resources to QEGTRAIN")

    plist = (ROOT / "Info.plist.in").read_text(encoding="utf-8")
    require(plist, "${MACOSX_BUNDLE_ICON_FILE}", "Info.plist does not use the configured icon name")
    rc = (APP_ROOT / "egtrain.rc.in").read_text(encoding="utf-8")
    require(rc, 'IDI_ICON1 ICON "@EGTRAIN_WINDOWS_ICON@"', "Windows resource template has the wrong icon entry")

    workflow = (ROOT / ".github/workflows/release.yml").read_text(encoding="utf-8")
    require(workflow, "resources/app/egtrain-256.png", "Linux release does not copy egtrain-256.png")
    if "resources/icons/station.png" in workflow or "convert EGTRAIN/QEGTRAIN/resources/icons/station.png" in workflow:
        raise SystemExit("Linux release still renders station.png as the app icon")
    require(workflow, "squashfs-root/usr/share/icons/hicolor/256x256/apps/qegtrain.png", "Linux release does not verify the extracted icon")


if __name__ == "__main__":
    main()
