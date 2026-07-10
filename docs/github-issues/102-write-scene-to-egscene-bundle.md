# #102 Write A Scene To An .egscene Bundle (Bundle Writer And scene_tool Pack/Unpack)

- GitHub: https://github.com/Ancientkingg/EGTRAIN/issues/102
- State: open
- Labels: `type: feature`, `area: scene model`, `area: bundle`
- Created: 2026-07-06
- Mirrored: 2026-07-06

Part of the single-file scene bundle epic (#99).

## Summary

Write a `SceneModel` (or an existing scene directory) to a single `.egscene` file, so authors can produce the one-file artifact students download.

## Design

- Extend `scene/SceneBundle.{h,cpp}` with:

  ```cpp
  SceneSaveResult saveSceneBundle(const SceneModel& scene, const std::string& bundlePath);
  ```

  returning the same `SceneSaveResult` as `saveScene` (`scene/SceneWriter.h`).

- **First cut — write then zip.** Call the existing `saveScene(scene, tempDir)`, then add each emitted JSON file to a ZIP at `bundlePath` with miniz, then delete the temp dir. Reuses all serialization logic in `SceneWriter`.
- Emit the manifest fields defined in the spec (`format: "egscene"`, `bundle_version`) into `scene.json`.
- **Exclude** the `legacy/` passthrough — bundles carry canonical data only.
- Deterministic output: sort entries by name and use fixed timestamps so re-saving an unchanged scene yields a byte-stable archive (helps diffs and release reproducibility).

## Scope

- Implement `saveSceneBundle`.
- Add a `scene_tool` sub-command (see `scene/SceneTool.cpp`) to pack a directory into a bundle and unpack a bundle back to a directory, e.g. `scene_tool bundle <sceneDir> <out.egscene>` / `scene_tool unbundle <in.egscene> <sceneDir>` — useful for scripting and CI.
- Round-trip test in `test_scenebundle.cpp`: `saveSceneBundle` then `loadSceneBundle` yields an equal `SceneModel`; and directory → bundle → directory is content-equal for the canonical files.
- Confirm no `legacy/` entry appears in a produced bundle.

## Acceptance criteria

- `saveSceneBundle` produces a valid `.egscene` that `loadSceneBundle` reads back to an equal `SceneModel`.
- Re-saving an unchanged scene produces a byte-identical archive.
- `legacy/` is never written into a bundle.
- New tests pass; existing `test_scenewriter` unaffected.

## Notes

Depends on the format spec and shares `SceneBundle.{h,cpp}` with the reader child; can land in the same PR series. Pairs with the GUI child, which calls `saveSceneBundle` from "Save Scene As".
