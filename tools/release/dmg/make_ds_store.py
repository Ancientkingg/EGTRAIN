#!/usr/bin/env python3
"""Write the Finder layout of the macOS disk image, the file dmg/DS_Store that make_dmg.py --layout puts on the volume.

usage: uv run --no-project --with ds_store python tools/release/dmg/make_ds_store.py tools/release/dmg/DS_Store

The layout is an icon view without toolbar, sidebar, path bar and status bar, in a window of WINDOW_SIZE points with
icons of ICON_SIZE points. The icons of QEGTRAIN.app and of the link Applications sit at the same height, the app on the
left and the link on the right, so the drag goes from left to right. There is no background picture and no volume icon.

The ds_store package writes the file without Finder. It is not a dependency of the repository or of CI: make_dmg.py only
copies the committed file.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from make_dmg import APP_NAME, LINK_NAME

WINDOW_ORIGIN = (200, 120)
WINDOW_SIZE = (600, 360)
ICON_SIZE = 128.0
TEXT_SIZE = 12.0
# The centre of each icon, in points from the top left corner of the window.
APP_POSITION = (160, 170)
LINK_POSITION = (440, 170)


def window_settings() -> dict:
    (x, y), (width, height) = WINDOW_ORIGIN, WINDOW_SIZE
    return {
        "WindowBounds": f"{{{{{x}, {y}}}, {{{width}, {height}}}}}",
        "ShowToolbar": False,
        "ShowSidebar": False,
        "ShowPathbar": False,
        "ShowStatusBar": False,
        "ShowTabView": False,
        "ContainerShowSidebar": False,
        "PreviewPaneVisibility": False,
        "SidebarWidth": 180,
    }


def icon_view_settings() -> dict:
    return {
        "viewOptionsVersion": 1,
        "backgroundType": 0,
        "backgroundColorRed": 1.0,
        "backgroundColorGreen": 1.0,
        "backgroundColorBlue": 1.0,
        "arrangeBy": "none",
        "gridOffsetX": 0.0,
        "gridOffsetY": 0.0,
        "gridSpacing": 100.0,
        "iconSize": ICON_SIZE,
        "textSize": TEXT_SIZE,
        "labelOnBottom": True,
        "showIconPreview": False,
        "showItemInfo": False,
        "scrollPositionX": 0.0,
        "scrollPositionY": 0.0,
    }


def write_layout(target) -> None:
    from ds_store import DSStore

    with DSStore.open(str(target), "w+") as store:
        store["."]["vSrn"] = ("long", 1)
        store["."]["bwsp"] = window_settings()
        store["."]["icvp"] = icon_view_settings()
        store["."]["icvl"] = ("type", b"icnv")
        store[APP_NAME]["Iloc"] = APP_POSITION
        store[LINK_NAME]["Iloc"] = LINK_POSITION


def main(argv=None) -> None:
    arguments = sys.argv[1:] if argv is None else argv
    if len(arguments) != 1:
        raise SystemExit(__doc__)
    try:
        import ds_store  # noqa: F401
    except ImportError:
        raise SystemExit("the ds_store package is needed: run this script with uv run --no-project --with ds_store python")
    write_layout(arguments[0])
    print(f"OK: {arguments[0]}")


if __name__ == "__main__":
    main()
