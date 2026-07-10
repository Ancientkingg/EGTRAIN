# #110 Replace The Application And Window Icon With A Proper Multi-Resolution App Icon

- GitHub: https://github.com/Ancientkingg/EGTRAIN/issues/110
- State: open
- Labels: `type: feature`, `area: ui`, `portability`
- Created: 2026-07-06
- Mirrored: 2026-07-06

## Summary

Replace the application / window icon with a proper, purpose-designed, multi-resolution app icon, and register it per platform so EGTRAIN has a recognisable identity in the title bar, taskbar/dock, Alt-Tab, and the installed app listing.

## Current state

- The window icon is a **JPEG**: `QIcon(QPixmap("window_icon.jpg"))` at `app/MainWindow.cpp:306-307`, loaded (like the other assets) by CWD-relative path from `EGTRAIN/QEGTRAIN/window_icon.jpg`.
- JPEG is the wrong format for an icon: no transparency, block artefacts at small sizes, single resolution.
- No platform icon is set for the packaged apps, so the macOS `.app`, the Windows `.exe`, and the Linux desktop entry fall back to a generic icon (relevant to the release-packaging work in #70–#72).

## Scope

- Design a distinctive EGTRAIN mark (railway/dispatch themed), consistent with the entity-icon set (#109) and the UI refresh (#79). Deliver **vector source** (SVG) plus rendered raster sizes (16–1024 px).
- Export the platform icon formats:
  - macOS: `.icns` and set it as the bundle icon (`MACOSX_BUNDLE_ICON_FILE` / `Info.plist`).
  - Windows: `.ico` and reference it from the app resource script so the `.exe` carries it.
  - Linux: PNG(s) plus a `.desktop` entry `Icon=`.
- Set the in-app window icon from the Qt resource system (`setWindowIcon(QIcon(":/app/icon.png"))`), replacing the JPEG relative-path load in `app/MainWindow.cpp:306`.
- Store all icon sources/exports under the new `resources/app/` folder.
- (If the `.egscene` file association from #103's packaging work lands) provide a companion document-type icon so scene files have their own glyph in the OS file browser.

## Acceptance criteria

- The window/taskbar/dock icon is a crisp, transparent, multi-resolution icon on macOS, Windows, and Linux — no JPEG.
- The packaged macOS `.app`, Windows `.exe`, and Linux desktop entry all show the new icon.
- Icon sources (SVG) and platform exports (`.icns`, `.ico`, PNG set) live under `resources/app/` and are loaded via Qt resources in-app.
- `window_icon.jpg` is removed.

## Notes

`type: feature` + `portability` because half the work is the per-platform icon toolchain in packaging (coordinate with #70–#72). Depends on the resources-folder consolidation (#108) for where the assets live; shares design direction with the entity-icon set (#109) and #79.
