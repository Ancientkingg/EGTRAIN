# #100 Define The .egscene Bundle Format And Manifest (V2 Scene Container Spec)

- GitHub: https://github.com/Ancientkingg/EGTRAIN/issues/100
- State: open
- Labels: `type: feature`, `area: scene model`, `area: bundle`, `documentation`
- Created: 2026-07-06
- Mirrored: 2026-07-06

Part of the single-file scene bundle epic (#99).

## Summary

Define and document the **`.egscene` bundle format**: the container, the internal layout, and the manifest fields that let a reader recognise and version-check a single-file scene. This is the spec every other child in the epic builds against, so it lands first.

## Format

- **Container:** ZIP (deflate). No encryption, no password.
- **Extension:** `.egscene`. **MIME:** `application/vnd.egscene+zip`.
- **Internal layout:** the current scene directory, at the archive root:

  ```
  scene.json            manifest: name, schema_version, format, bundle_version
  stations.json
  signalling.json
  services.json
  rolling_stock.json
  infrastructure.json
  incidents.json        optional
  views.json            optional
  ```

- The derived `legacy/` passthrough directory is **excluded** from bundles. A bundle carries only canonical scene data; the legacy export is regenerated on demand.
- Unknown extra files at the root are ignored by the reader (forward compatibility) but a writer only emits the files above.

## Manifest additions (`scene.json`)

Today `scene.json` holds `{ name, schema_version, legacy_source }` with `schema_version: 1`. Add:

- `format: "egscene"` — a fixed marker so a reader can sniff a valid bundle before trusting it.
- `bundle_version: 1` — versions the **container** independently of `schema_version`, which continues to version the **data**.

A reader accepts a bundle when `scene.json` exists at the root, `format == "egscene"`, and `bundle_version`/`schema_version` are within the supported range.

## Scope

- Write `docs/architecture/scene-bundle.md` specifying the container, layout, manifest, MIME, magic (ZIP `PK\x03\x04`), and the exclusion of `legacy/`.
- Add a `## Bundle format (v2)` section to `docs/architecture/scene-model.md`, replacing the "The schema can be split later..." sentence with a link to the new spec.
- Extend the scene-schema doc / JSON schema for `scene.json` with the `format` and `bundle_version` fields, marked optional for directory scenes and required inside a bundle.
- Bump the writer to emit the new manifest fields (behaviour change is documented, no reader breakage — the fields are additive).

## Acceptance criteria

- `docs/architecture/scene-bundle.md` exists and fully specifies the container, layout, and manifest.
- The manifest fields (`format`, `bundle_version`) are documented in the schema doc.
- The spec explicitly states which files are included and that `legacy/` is excluded.
- No code that reads directory scenes is broken by the additive manifest fields.

## Notes

Depends on nothing. Reader (bundle loader) and writer children implement against this spec. The extension (`.egscene`) and container (ZIP) are settled in the epic's Decisions; the merged-JSON alternative was considered and rejected there.
