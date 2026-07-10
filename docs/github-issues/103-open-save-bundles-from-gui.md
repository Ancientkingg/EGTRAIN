# #103 Open And Save Single-File Scene Bundles From The GUI

- GitHub: https://github.com/Ancientkingg/EGTRAIN/issues/103
- State: open
- Labels: `type: feature`, `area: ui`, `area: bundle`
- Created: 2026-07-06
- Mirrored: 2026-07-06

Part of the single-file scene bundle epic (#99).

## Summary

Let a student open and save a scene as a single `.egscene` file from the GUI, using an ordinary file picker instead of the current "pick a directory" flow.

## Current state

- `MainWindow::openSceneDialog()` calls `QFileDialog::getExistingDirectory(this, "Open Scene", ...)` (`EGTRAIN/QEGTRAIN/app/MainWindow.cpp:722`) and then `openSceneDirectory(dir)` → `loadScene(...)`.
- "Save Scene As" also uses `getExistingDirectory` (`app/MainWindow.cpp:799`).
- Asking a student to select a folder is unusual and easy to get wrong (they select the parent, or a subfolder).

## Scope

- **Open:** replace the open flow with `QFileDialog::getOpenFileName` filtered to `EGTRAIN scene (*.egscene)`, routing through the new `loadSceneBundle(path)`. Keep opening a directory available as a secondary path (a "Open Scene Folder…" action, or auto-detect: if the chosen path is a directory use `loadScene`, else `loadSceneBundle`) so existing `Scenes/<Name>/` folders still work.
- **Save:** change "Save Scene As" to `getSaveFileName` with the `.egscene` filter and call `saveSceneBundle`, appending `.egscene` if the user omits it. Keep a "Save as Folder…" option for authors who want the exploded layout.
- **Drag and drop:** accept a dropped `.egscene` onto the main window (`dragEnterEvent` / `dropEvent`) and open it.
- **Recent files:** add opened bundles to a "Recent Scenes" menu (`QSettings`), so returning students reopen with one click.
- Update the window title / status to show the loaded bundle name; keep `m_sceneLoaded` / `m_runSceneAction` gating (`app/MainWindow.cpp:910`) working.

## Acceptance criteria

- File → Open shows a file picker for `*.egscene` and loads the selected bundle.
- File → Save Scene As writes a single `.egscene` file.
- Dropping a `.egscene` file onto the window opens it.
- Recent Scenes lists and reopens previously opened bundles.
- Opening an existing scene **directory** still works (back-compat path preserved).
- Visual smoke coverage: open a bundle, confirm the network view renders (ties into the existing scene-render smokes referenced by #77).

## Notes

Depends on the reader and writer children. OS-level file association (double-click a `.egscene` in Finder/Explorer to launch EGTRAIN) is handled separately in the packaging child so this issue stays inside the app.
