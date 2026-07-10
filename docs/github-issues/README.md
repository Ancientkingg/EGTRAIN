# GitHub Issue Mirror

Last updated: 2026-07-06

This directory mirrors GitHub issues that track UI design and release automation work for EGTRAIN. It exists because no local issue mirror was present in the repository.

## Source Tree Reorganization

- [#91 Epic: Reorganize the QEGTRAIN source tree and retire boilerplate names](091-epic-reorganize-source-tree.md)
- [#92 Group the flat QEGTRAIN sources into module folders](092-group-sources-into-module-folders.md)
- [#93 Rename the Qt graphics classes to describe what they draw](093-rename-graphics-classes.md)
- [#94 Rename the remaining vague classes](094-rename-vague-classes.md)
- [#95 Remove the leftover Widget demo](095-remove-widget-demo.md)

The new layout and rename table are documented in `docs/architecture/source-layout.md`.

## UI Work

- [#79 Refresh desktop UI shell visual design](079-refresh-desktop-ui-shell-visual-design.md)
- [#80 Redesign train and track rendering for readable network playback](080-redesign-train-and-track-rendering.md)

Mockup files:

- `docs/ui/egtrain-ui-refresh-mock.svg`
- `docs/ui/egtrain-ui-refresh-mock.png`

## Scene Bundle (v2 Scene Input)

A portable single-file `.egscene` (ZIP) scene container so a student downloads and opens one file instead of a folder of loose JSON. The format spec is in `docs/architecture/scene-bundle.md` (added by #100).

- [#99 Epic: Portable single-file scene bundle (.egscene) as v2 scene input](099-epic-single-file-scene-bundle.md)
- [#100 Define the .egscene bundle format and manifest](100-define-egscene-bundle-format.md)
- [#101 Read a scene from an .egscene bundle (bundle loader)](101-read-scene-from-egscene-bundle.md)
- [#102 Write a scene to an .egscene bundle (bundle writer and scene_tool pack/unpack)](102-write-scene-to-egscene-bundle.md)
- [#103 Open and save single-file scene bundles from the GUI](103-open-save-bundles-from-gui.md)
- [#104 Accept an .egscene bundle path from the CLI and headless tools](104-accept-egscene-bundle-cli.md)
- [#105 Harden bundle loading against malformed and hostile archives](105-harden-bundle-loading.md)
- [#106 Package the case studies as downloadable .egscene bundles on releases](106-package-case-studies-bundles.md)
- [#107 Document how students download and open a scene bundle](107-document-opening-scene-bundle.md)

## Icons And Visual Assets

- [#108 Consolidate scattered image assets into a resources folder and load them from Qt resources](108-consolidate-image-assets-resources.md)
- [#109 Revamp the in-scene entity icons (stations, passengers, trains, signals)](109-revamp-in-scene-entity-icons.md)
- [#110 Replace the application and window icon with a proper multi-resolution app icon](110-replace-application-window-icon.md)

#108 is the prerequisite that gives the icon work (#109, #110) a home under `EGTRAIN/QEGTRAIN/resources/` and fixes the current CWD-relative asset loading.

## Cross-Platform Build And Release Work

These GitHub issues already existed, so no duplicates were opened:

- [#64 Epic: CI/CD pipeline](064-epic-ci-cd-pipeline.md)
- [#69 Add cross-platform release matrix](069-add-cross-platform-release-matrix.md)
- [#70 Package Windows release artifact](070-package-windows-release-artifact.md)
- [#71 Package macOS release artifact](071-package-macos-release-artifact.md)
- [#72 Package Linux release artifact](072-package-linux-release-artifact.md)
- [#73 Attach release artifacts to tagged GitHub releases](073-attach-release-artifacts-to-tagged-releases.md)

Related CI hardening also exists on GitHub: #65 headless smoke tests, #66 visual smoke test in CI, #67 failure logs and screenshots, #68 repository hygiene workflow, and #74 release testing checklist.
