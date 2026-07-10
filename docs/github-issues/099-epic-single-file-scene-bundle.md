# #99 Epic: Portable Single-File Scene Bundle (.egscene) As V2 Scene Input

- GitHub: https://github.com/Ancientkingg/EGTRAIN/issues/99
- State: open
- Labels: `type: epic`, `area: scene model`, `area: bundle`, `enhancement`
- Created: 2026-07-06
- Mirrored: 2026-07-06

## Goal

Introduce a **v2 scene input format**: a single, portable file a student can download and open, instead of a folder of loose JSON files they have to keep together on disk.

Today a scene is a **directory** (`EGTRAIN/QEGTRAIN/Scenes/<Name>/`) containing `scene.json` plus `stations.json`, `signalling.json`, `services.json`, `rolling_stock.json`, `infrastructure.json`, and optionally `incidents.json` / `views.json`. To hand a scenario to a student we have to hand them a whole folder; if one file goes missing or the folder is unzipped into the wrong place, the scene fails to load. `MainWindow::openSceneDialog()` even asks for a directory (`QFileDialog::getExistingDirectory`, `EGTRAIN/QEGTRAIN/app/MainWindow.cpp:722`), which is an unusual and error-prone thing to ask a student to do.

The scene-model design already anticipates this. `docs/architecture/scene-model.md` says: *"The schema can be split later if a single-file scene is easier for exchange."* This epic does exactly that.

## Why

- **One artifact.** A student downloads one `.egscene` file, double-clicks or picks it in the app, and the scenario loads. No folder to keep intact, no "select the right subdirectory" step.
- **Integrity.** A single archive can't lose one of its seven JSON files in transit. The reader validates the whole bundle up front.
- **Distribution.** Case studies and assignments become downloadable attachments on GitHub releases and in the course materials, rather than a directory tree to clone.
- **No format churn for authors.** The bundle is just the existing scene directory zipped, so the editor, validator, importer, and exporter keep working on the same JSON. We are adding a container, not a new data model.

## Design

- **Container: ZIP.** Extension **`.egscene`**, MIME `application/vnd.egscene+zip`. Same well-understood pattern as `.docx` / `.epub` / Scratch `.sb3`: a zip with a known internal layout.
- **Internal layout:** the current scene directory at the archive root — `scene.json` (manifest) plus the other JSON files. The derived `legacy/` passthrough is **not** included; bundles carry only canonical data.
- **Manifest:** `scene.json` gains a `format: "egscene"` marker and a `bundle_version` alongside the existing `schema_version` (currently `1`) so a reader can sniff and version-check the container independently of the data schema.
- **Code shape:** a new `scene/SceneBundle.{h,cpp}` exposing `loadSceneBundle(path)` and `saveSceneBundle(scene, path)`. The first cut can extract to a temp directory and reuse the existing `loadScene(sceneDir)` / `saveScene(scene, sceneDir)` unchanged, so the container is isolated from the model code.
- **Zip dependency:** vendor a small MIT single-file zip lib (e.g. miniz) under `io/third_party/`, mirroring the existing pugixml pattern. Alternative considered: Qt's private `QZipReader`/`QZipWriter` (no new dep but private API, not guaranteed stable across Qt versions).

## Decisions

- **Extension: `.egscene`** (decided).
- **Container: ZIP** (decided). Keeps the existing multi-file layout and compresses the large `signalling.json` (96–170 KB) well; a single merged-JSON container was considered and rejected because it loses per-section editing on disk.

## Open questions

- **Zip library** — vendored miniz (single-file, MIT; proposed) vs Qt private `QZipReader`/`QZipWriter` (no new dependency but private API, not API-stable across Qt versions).

## Children

- #100 Define the `.egscene` bundle format and manifest (v2 scene container spec)
- #101 Read a scene from an `.egscene` bundle (bundle loader)
- #102 Write a scene to an `.egscene` bundle (bundle writer and `scene_tool` pack/unpack)
- #103 Open and save single-file scene bundles from the GUI
- #104 Accept an `.egscene` bundle path from the CLI and headless tools
- #105 Harden bundle loading against malformed and hostile archives
- #106 Package the case studies as downloadable `.egscene` bundles on releases
- #107 Document how students download and open a scene bundle

## Boundary / relationships

- This epic is about **packaging** scene input for exchange. It sits on top of the canonical scene model and is independent of whether the engine runs a scene natively (#83) or via the legacy export path — a bundle loads to a `SceneModel` either way. Where the two meet (running a bundle straight from CLI/GUI) is called out and tracked against #88.
- Directory scenes keep working. The bundle is an additional open/save path, not a replacement, so existing `Scenes/<Name>/` folders and tests are untouched.

## Acceptance criteria

- A scene directory can be saved to a single `.egscene` file and re-opened into an identical `SceneModel` (round-trip).
- The GUI can open and save `.egscene` bundles via a normal file picker, and the CLI accepts a bundle path anywhere a scene directory is accepted.
- The four case studies ship as downloadable `.egscene` bundles.
- Student-facing documentation explains "download a scene file, open it in EGTRAIN" with no mention of folders.
- Malformed or hostile archives are rejected with a clear diagnostic, not a crash.
