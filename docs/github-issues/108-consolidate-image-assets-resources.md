# #108 Consolidate Scattered Image Assets Into A Resources Folder And Load Them From Qt Resources

- GitHub: https://github.com/Ancientkingg/EGTRAIN/issues/108
- State: open
- Labels: `type: tech debt`, `area: ui`, `portability`
- Created: 2026-07-06
- Mirrored: 2026-07-06

## Summary

Consolidate the scattered GUI image files into a single `resources/` folder and load them through Qt's resource system (`:/` paths) instead of by working-directory-relative filename. Today the assets sit loose next to the source and only load if the app happens to run from the right directory.

## Current state

- Three image assets live at the top of the source tree:
  - `EGTRAIN/QEGTRAIN/train_station.png`
  - `EGTRAIN/QEGTRAIN/pax_icon.png`
  - `EGTRAIN/QEGTRAIN/window_icon.jpg`
- They are loaded by **relative path from the current working directory**:
  - `station_pixmap = QPixmap("train_station.png");` — `app/MainWindow.cpp:217`
  - `pax_pixmap = QPixmap("pax_icon.png");` — `app/MainWindow.cpp:220`
  - `QIcon window_icon = QIcon(QPixmap("window_icon.jpg"));` — `app/MainWindow.cpp:306`
- To make that work, `CMakeLists.txt:246-257` has a bespoke step that **copies** each PNG/JPG next to the binary (and into the `.app` bundle). If the app is launched from any other directory, the icons silently fail to load (blank station/passenger glyphs) — a latent portability bug.
- A resource bundle already exists but is **empty**: `app/resources.qrc` declares `<qresource prefix="MainWindow">` with no files, and it is already compiled in (`CMakeLists.txt:189`). The machinery is there; it just isn't used.

## Scope

- Create `EGTRAIN/QEGTRAIN/resources/` with a clear layout, e.g.:

  ```
  resources/
    icons/      station.png, passenger.png, ... (in-scene entity glyphs)
    app/        window/app icon source(s)
  ```

- Move the three existing assets there and register them in `resources.qrc` under a sensible prefix.
- Replace the CWD-relative loads in `app/MainWindow.cpp` with resource paths, e.g. `QPixmap(":/icons/station.png")`.
- Delete the bespoke image-copy commands in `CMakeLists.txt:246-257`; `qrc` embeds the assets in the binary, so nothing needs copying and the CWD dependency disappears. (Keep the Input-data copy step; that is separate.)
- Grep the tree for any other loose asset load and route it through the resource system too.

## Acceptance criteria

- No GUI image is loaded by a bare relative filename; all go through `:/` resource paths.
- Launching the built app from an arbitrary working directory shows the station and passenger icons and the window icon correctly.
- `resources.qrc` lists every bundled asset; the ad-hoc copy step for images is gone from CMake.
- Build and all ctest targets still pass on macOS with Qt 5.

## Notes

`type: tech debt` because it changes no user-facing behaviour when the app is launched normally, but it removes a fragile pattern and fixes the wrong-CWD failure. This is the natural home for the new artwork in the icon-revamp issues, so it should land first.
