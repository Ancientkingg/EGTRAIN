# #104 Accept An .egscene Bundle Path From The CLI And Headless Tools

- GitHub: https://github.com/Ancientkingg/EGTRAIN/issues/104
- State: open
- Labels: `type: feature`, `area: bundle`
- Created: 2026-07-06
- Mirrored: 2026-07-06

Part of the single-file scene bundle epic (#99).

## Summary

Accept an `.egscene` bundle path anywhere the CLI and headless tools accept a scene directory, so scripts, CI, and the native-run path can consume the single-file format.

## Scope

- Wherever a tool takes a scene directory and calls `loadScene(dir)` — the CLI/headless entry point, `scene/SceneValidator.cpp`, `scene/SceneExporter.cpp`, `scene/SceneTool.cpp` — accept a path that may be a directory **or** a `.egscene` file, dispatching to `loadScene` or `loadSceneBundle` based on the path (a file with the `.egscene` extension / ZIP magic → bundle; a directory → directory).
- Centralise the dispatch in one helper (e.g. `loadSceneAny(path)` in `scene/SceneBundle.h`) so every caller shares one rule.
- `scene_tool validate <path>` and `scene_tool export <path>` work on both a directory and a bundle.
- Extend the native scene-run entry point (the CLI scene argument in #88) to accept a bundle path, so a student scenario can be run headless as `... --scene copenhagen.egscene`.

## Acceptance criteria

- Every command that previously took a scene directory also takes an `.egscene` bundle and behaves identically.
- Validation and export produce the same results from a bundle as from the equivalent directory.
- Headless smoke covers running a scene from a bundle path.

## Notes

Depends on the reader child. Overlaps with #88 (run a scene natively from CLI/GUI) — coordinate so the scene-path argument added there accepts both forms rather than adding two arguments.
