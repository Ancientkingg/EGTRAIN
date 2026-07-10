# #106 Package The Case Studies As Downloadable .egscene Bundles On Releases

- GitHub: https://github.com/Ancientkingg/EGTRAIN/issues/106
- State: open
- Labels: `type: feature`, `area: bundle`, `area: ci`
- Created: 2026-07-06
- Mirrored: 2026-07-06

Part of the single-file scene bundle epic (#99).

## Summary

Package the four case studies (and the assignment scenario) as downloadable `.egscene` bundles and attach them to GitHub releases, so a student's whole starting point is one link → one file → open.

## Scope

- Add a build/CI step that packs each committed scene under `EGTRAIN/QEGTRAIN/Scenes/` — `Copenhagen`, `Netherlands` (NL), `Milano_Brescia`, `Paimpol`, and `Assignment_Gvc_Gdg_Ut` — into `<Name>.egscene` using `scene_tool bundle` (from the writer child).
- Attach the generated `.egscene` files to tagged GitHub releases, alongside the platform binaries (extends #73, "attach release artifacts to tagged releases").
- Verify each shipped bundle in CI: pack it, then `scene_tool validate <Name>.egscene` and load it headless, so a broken bundle can't be released.
- Keep the source scene **directories** in the repo as the editable source of truth; bundles are generated artifacts, not committed blobs.

## Acceptance criteria

- Each tagged release carries an `.egscene` for every case study and the assignment scenario.
- CI validates every bundle it produces before upload; a validation failure fails the release job.
- Source scene directories remain the version-controlled source; bundles are build outputs.

## Notes

Depends on the writer child (produces the bundles) and the CLI child (validates them headless). Coordinates with the release-artifact issues (#69–#73). The student-facing "how to download and open" guide is a separate docs child.
