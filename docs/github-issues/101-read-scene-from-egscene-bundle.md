# #101 Read A Scene From An .egscene Bundle (Bundle Loader)

- GitHub: https://github.com/Ancientkingg/EGTRAIN/issues/101
- State: open
- Labels: `type: feature`, `area: scene model`, `area: bundle`
- Created: 2026-07-06
- Mirrored: 2026-07-06

Part of the single-file scene bundle epic (#99).

## Summary

Read a scene from a single `.egscene` file into a `SceneModel`, so the rest of the app can consume a bundle exactly as it consumes a scene directory today.

## Design

- Add `scene/SceneBundle.{h,cpp}` with:

  ```cpp
  SceneLoadResult loadSceneBundle(const std::string& bundlePath);
  ```

  returning the same `SceneLoadResult` (`scene/SceneModel.h`) as the existing `loadScene(sceneDir)`, so every current caller can accept a bundle by branching on the path.

- **First cut — extract then reuse.** Open the ZIP, extract the recognised JSON entries to a temp directory (`QTemporaryDir`), then call the existing `loadScene(tempDir)` unchanged and delete the temp dir. This keeps the container fully isolated from the model loader.
- **Follow-up (optional, note only):** feed JSON blobs to the loader in-memory to avoid touching disk. Out of scope here.

## Zip dependency

- Vendor **miniz** (single-file, MIT) under `EGTRAIN/QEGTRAIN/io/third_party/miniz/`, mirroring the existing `io/third_party/pugixml` pattern, and wire it into CMake.
- Alternative considered: Qt private `QZipReader`. Rejected for the first cut because it depends on private Qt Gui headers that are not API-stable across Qt versions.

## Scope

- Implement `loadSceneBundle` (open zip → validate manifest per the spec → extract → `loadScene` → cleanup).
- Reject a file whose `scene.json` is missing or whose `format` / `bundle_version` is unrecognised, with a `SceneDiagnostic` that names the file and the reason (reuse the existing diagnostic pattern from `SceneValidator`).
- Ignore a `legacy/` entry if present.
- Unit tests in `EGTRAIN/QEGTRAIN/tests/test_scenebundle.cpp`: load a good bundle; verify the resulting `SceneModel` equals the one from the equivalent directory; reject missing-manifest, wrong-`format`, and truncated-zip inputs.
- Add a fixture bundle under `EGTRAIN/QEGTRAIN/tests/fixtures/scenes/` (zip the existing `minimal/` scene).

## Acceptance criteria

- `loadSceneBundle("<scene>.egscene")` returns a `SceneModel` identical to `loadScene("<scene>/")` for the same scene.
- A malformed or non-bundle file returns diagnostics, never crashes.
- New ctest target passes on macOS with Qt 5; no regression in the existing scene tests.

## Notes

Depends on the format spec child. Hostile-archive hardening (zip bombs, path traversal, size caps) is deliberately split into its own child so this issue stays focused on the happy path plus basic validity.
