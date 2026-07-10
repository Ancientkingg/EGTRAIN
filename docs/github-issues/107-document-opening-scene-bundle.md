# #107 Document How Students Download And Open A Scene Bundle

- GitHub: https://github.com/Ancientkingg/EGTRAIN/issues/107
- State: open
- Labels: `type: docs`, `documentation`, `area: bundle`
- Created: 2026-07-06
- Mirrored: 2026-07-06

Part of the single-file scene bundle epic (#99).

## Summary

Write the student-facing guide for the single-file scene workflow: download a `.egscene` file, open it in EGTRAIN, run it. No mention of folders, subdirectories, or loose JSON files.

## Scope

- Add `docs/product/opening-a-scene.md` (or a section in the existing user/getting-started docs) covering:
  - where to download case-study / assignment bundles (release page link from the distribution child),
  - File → Open (or drag-and-drop) a `.egscene` file,
  - what to do if a bundle fails to load (the reader's diagnostics, re-download),
  - optional: double-click to open once file association lands.
- Include one annotated screenshot of the open flow and the loaded network view.
- Add a short "Authoring a bundle" note for instructors: edit the scene, Save As `.egscene`, or `scene_tool bundle <dir> <out.egscene>`.
- Cross-link the format spec (`docs/architecture/scene-bundle.md`) for readers who want the internals.
- Update the top-level README's quick-start to point at the bundle flow.

## Acceptance criteria

- A new student can follow the guide end to end: download a bundle, open it, run the simulation.
- The guide references only single-file steps for the student path; folder handling is confined to the instructor/authoring note.
- Screenshots match the shipped GUI.

## Notes

Depends on the GUI child (for the real open flow and screenshots) and the distribution child (for the download location). Keep the prose plain and task-oriented.
