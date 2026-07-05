# GitHub Issue Mirror

Last updated: 2026-07-05

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

## Cross-Platform Build And Release Work

These GitHub issues already existed, so no duplicates were opened:

- [#64 Epic: CI/CD pipeline](064-epic-ci-cd-pipeline.md)
- [#69 Add cross-platform release matrix](069-add-cross-platform-release-matrix.md)
- [#70 Package Windows release artifact](070-package-windows-release-artifact.md)
- [#71 Package macOS release artifact](071-package-macos-release-artifact.md)
- [#72 Package Linux release artifact](072-package-linux-release-artifact.md)
- [#73 Attach release artifacts to tagged GitHub releases](073-attach-release-artifacts-to-tagged-releases.md)

Related CI hardening also exists on GitHub: #65 headless smoke tests, #66 visual smoke test in CI, #67 failure logs and screenshots, #68 repository hygiene workflow, and #74 release testing checklist.
