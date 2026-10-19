# Documentation page map

Where each file under `docs/` and the README goes in the structure of the [documentation decision](documentation-decision.md). Actions: keep, move, merge, split, delete. #916 applies the map and judges the content of each page; the action here is about structure only. Line counts are from 2026-10-19.

## Target structure

```text
README.md                      repository landing page
docs/README.md                 short landing page for readers on GitHub
docs/index.md                  table of contents of the site
docs/guides/                   manual, also built as the help in the app
docs/development/              developer: onboarding, build, test, release
docs/development/records/      developer: dated records
docs/architecture/             developer: design and contracts
docs/telemetry/, docs/live/    developer: wire contracts and JSON schemas
api/                           generated C++ reference, in the build folder only
```

The manual links only to the manual. So the four developer pages that the guides link to most move into `docs/guides/`: `signalling-levels.md` (27 links from the guides), `scene-schema.md` (24), `scene-bundle.md` (4) and `diagram-navigation.md` (3).

## README files

| Current path | Lines | Destination | Action | Note |
| --- | ---: | --- | --- | --- |
| `README.md` | 108 | same | Keep, trim | Keeps the description, one screenshot, what you can do, credits, citation, support, licence and links. "macOS disk image" and "Windows installer and portable archive" move to `guides/install.md`, "Case studies" to `guides/case-studies.md`. |
| `docs/README.md` | 80 | same, and `docs/index.md` | Split | The route lists become the captions of the table of contents in `index.md`. The paragraph on scene folders and bundles moves to `guides/scene-bundle.md`. |
| `installer/README.md` | 85 | `development/windows-installer.md` | Move | Sphinx cannot resolve a link out of `docs/`. |

## docs/guides

| Current path | Lines | Destination | Action | Note |
| --- | ---: | --- | --- | --- |
| `getting-started.md` | 130 | same | Keep | Start of the manual. |
| `opening-a-case-study.md` | 169 | same | Keep, trim | "Open and run" repeats `getting-started.md`. |
| `tutorial-modify-a-case.md` | 171 | same | Keep | |
| `tutorial-minimal-scene.md` | 139 | same | Keep | |
| `how-to.md` | 209 | same | Keep | |
| `scenes-and-application.md` | 468 | `main-window.md`, `editing-a-case.md`, `scenarios.md` | Split | "Open and run" and "Application updates" are covered by `opening-a-case-study.md` and `updates.md`. "Review what loaded" becomes `main-window.md`, "Edit and validate" `editing-a-case.md`, "Scenario library and student loop" `scenarios.md`. "Timetable graphs and train paths" joins `diagram-navigation.md`; "Reference" merges into `scene-schema.md`. |
| `glossary.md` | 195 | same | Keep | Source of terms for the writing guide. |
| `command-line.md` | 128 | same | Keep | |
| `run-output.md` | 42 | same | Keep | |
| `updates.md` | 72 | same | Keep | |
| `delay-analysis.md` | 291 | same | Keep | |
| `capacity-analysis.md` | 358 | same | Keep | |
| `amsterdam-hilversum-student-case.md` | 124 | same | Keep | |
| `v1-scene-migration-matrix.md` | 84 | same | Keep | |
| `v1-scene-properties.md` | 317 | `scene-schema.md` | Merge | Same files and keys as the schema. |
| `live-sharing.md` | 164 | `development/live-sharing.md` | Move | Written for someone who writes a client. |
| `v1-native-cutover-report.md` | 88 | `development/records/` | Move | Conversion record. |
| `original-case-runtime-parity.md` | 108 | `development/records/` | Move | Comparison record. |

## docs/architecture

| Current path | Lines | Destination | Action | Note |
| --- | ---: | --- | --- | --- |
| `signalling-levels.md` | 751 | `guides/signalling-levels.md` | Move | Most linked page of the manual. |
| `scene-schema.md` | 533 | `guides/scene-schema.md` | Move | Receives `v1-scene-properties.md` and the Reference part of `scenes-and-application.md`. |
| `scene-bundle.md` | 92 | `guides/scene-bundle.md` | Move | |
| `source-layout.md` | 183 | same | Keep | Links to the C++ reference once #918 builds it. |
| `scene-model.md` | 385 | same | Keep, trim | "Bundle format (v2)" repeats `scene-bundle.md`; a link replaces it. |
| `scene-editor-flow.md` | 216 | same | Keep | |
| `external-sharing.md` | 414 | same | Keep | The live contract; `live-sharing.md` is its guide. |

## docs/ui

The folder goes away.

| Current path | Lines | Destination | Action | Note |
| --- | ---: | --- | --- | --- |
| `diagram-navigation.md` | 19 | `guides/diagram-navigation.md` | Move | |
| `dialog-presentation.md` | 133 | `architecture/dialog-presentation.md` | Move | |
| `result-window-presentation.md` | 9 | `architecture/dialog-presentation.md` | Merge | Becomes a section. |
| `renderer-style-map.md` | 77 | `architecture/renderer-style-map.md` | Move | |
| `historical-track-comparison.md` | 274 | `architecture/historical-track-comparison.md` | Move | Its images are dated comparisons and are not regenerated. |
| `dialog-sheet-2026-10-17.md` | 65 | `development/records/` | Move | Dated record. |

## docs/development

| Current path | Lines | Destination | Action | Note |
| --- | ---: | --- | --- | --- |
| `getting-started.md` | 255 | same | Keep | Start of the onboarding path. |
| `build-and-test.md` | 2382 | five pages | Split | `build.md`: Requirements to Common setup failures. `testing.md`: Unit Tests and Verification Gates. `characterization-tests.md`: Characterization tests. `smoke-tests.md`: Simulation Smoke Test to Where test and run output goes. `ci-and-release.md`: CI and release branches. About 40 links point to it; `check_docs.py` lists them. |
| `coding-guidelines.md` | 434 | same | Keep | |
| `code-signing.md` | 447 | same, and `guides/install.md` | Split | "Checking a download", the three "First start" sections and "Checking a macOS download" go to the manual. |
| `release-testing-checklist.md` | 455 | same | Keep | Gets a row that runs `capture_screenshots.sh --check`. |
| `release-rehearsal-template.md` | 112 | same | Keep | |
| `release-rehearsal-2026-10-17.md` | 233 | `development/records/` | Move | Latest rehearsal. |
| `release-rehearsal-2026-08-23.md` | 44 | none | Delete | Superseded by the later rehearsal. |
| `memory-baselines.md` | 111 | same | Keep | |
| `playback-profiling.md` | 54 | same | Keep | |
| `documentation-decision.md` | new | same | Keep | |
| `documentation-page-map.md` | new | none | Delete | Once #916 has applied it. |

## docs/product

The folder goes away.

| Current path | Lines | Destination | Action | Note |
| --- | ---: | --- | --- | --- |
| `assignment-workflow.md` | 114 | `guides/assignment-workflow.md` | Move | Instructor guide. |
| `assignment-corridor.md` | 46 | none | Delete | Status and blockers belong in the issue tracker; open items become issues first. |
| `feature-status.md` | 269 | `development/records/feature-status-2026-10-17.md` | Move | Dated snapshot. The manual gets a short "Known limits" section from it. |

## docs/telemetry and docs/live

Paths stay: CI, tests and `CMakeLists.txt` read them.

| Current path | Action | Note |
| --- | --- | --- |
| `telemetry/README.md` | Keep | Developer contract. What is sent and how to switch it off goes to a new `guides/privacy-and-diagnostics.md`. |
| `telemetry/batch-v1.schema.json` | Keep | |
| `telemetry/examples/`: `invalid-category-mixing.json`, `invalid-missing-identifier.json`, `invalid-private-string.json`, `invalid-unknown-field.json`, `valid-diagnostics.json`, `valid-usage.json` | Keep | Left out of the Sphinx sources. |
| `live/frame-v1.schema.json` | Keep | |

## Images

All are taken again by the capture program and renamed to `<page>-<subject>.png`, except the dated comparisons.

| Current path | Used by | Destination | Action |
| --- | --- | --- | --- |
| `images/guide-open-a-case.png` | getting started, opening a case | `images/getting-started-open-a-case.png` | Retake |
| `images/guide-main-window.png` | getting started, opening a case | `images/getting-started-main-window.png` | Retake |
| `images/guide-run-simulation.png` | getting started | `images/getting-started-run-simulation.png` | Retake |
| `images/guide-run-in-progress.png` | getting started | `images/getting-started-run-in-progress.png` | Retake |
| `images/guide-run-results.png` | getting started | `images/getting-started-run-results.png` | Retake |
| `images/guide-scene-validation.png` | how-to, modify a case | `images/how-to-scene-validation.png` | Retake |
| `images/guide-replay-menu.png` | how-to | `images/how-to-replay-menu.png` | Retake |
| `images/guide-edit-timetable-stop.png` | modify a case | `images/tutorial-modify-edit-timetable-stop.png` | Retake |
| `images/guide-incident-baseline.png` | modify a case | `images/tutorial-modify-incident-baseline.png` | Retake |
| `images/guide-minimal-nodes.png` | minimal scene | `images/tutorial-minimal-nodes.png` | Retake |
| `images/guide-minimal-platforms.png` | minimal scene | `images/tutorial-minimal-platforms.png` | Retake |
| `images/guide-minimal-service.png` | minimal scene | `images/tutorial-minimal-service.png` | Retake |
| `images/main-window-milano-brescia.png` | README | `images/readme-main-window.png` | Retake |
| `images/speed-distance-milano-brescia.png` | README | `images/readme-speed-distance.png` | Retake |
| `ui/images/dialog-sheet-2026-10-17/item01.png` to `item06.png` | dialog sheet | `development/records/images/` | Move |
| `ui/images/`: `assignment-corridor-current.png`, `assignment-detail-current.png`, `copenhagen-junction-current.png`, `copenhagen-junction-historical.png`, `netherlands-stretch-current.png`, `netherlands-stretch-historical.png`, `paimpol-detail-current.png`, `paimpol-detail-historical.png`, `paimpol-fit-current.png`, `paimpol-fit-historical.png` | historical track comparison | `architecture/images/` | Move |

## Overlaps

Each topic gets one owner page; the others link to it.

- Opening and running a case: `README.md`, `getting-started.md`, `opening-a-case-study.md` and `scenes-and-application.md`. Owner: `guides/getting-started.md`.
- Install and first start: `README.md` and `code-signing.md`. Owner: `guides/install.md`.
- Updates: `updates.md` and `scenes-and-application.md`. Owner: `guides/updates.md`.
- Scene files: `v1-scene-properties.md`, `scene-schema.md`, `scenes-and-application.md` and `scene-model.md`. Owner: `guides/scene-schema.md`; `scene-model.md` keeps the design.
- Bundles: `scene-bundle.md` and `scene-model.md`. Owner: `guides/scene-bundle.md`.

## Gaps

- Manual: `install.md`, `case-studies.md`, `main-window.md`, `privacy-and-diagnostics.md` and `shortcuts.md`.
- Developer: a page on the simulation core, and the C++ reference (#918).
- No script makes the current pictures in `docs/images/`.

## Files that name docs paths

A move needs a matching edit in the same change:

- `.github/workflows/cmake.yml` skips the build for changes under `docs/`, and `.github/workflows/release.yml` filters on `docs/**`.
- `.github/workflows/telemetry-contract.yml`, `tools/e2e/test_telemetry_contract.py`, `tools/e2e/telemetry_smoke.py`, `CMakeLists.txt`, `EGTRAIN/QEGTRAIN/io/LiveFrame.h` and `EGTRAIN/QEGTRAIN/tests/test_liveframe.cpp` read the schemas.
- `.clang-tidy` names `coding-guidelines.md`; `EGTRAIN/QEGTRAIN/tests/live_client.cpp`, `EGTRAIN/QEGTRAIN/tests/test_sceneversions.cpp`, `tools/e2e/lsan_suppressions.txt` and `installer/README.md` name pages in comments or links.
