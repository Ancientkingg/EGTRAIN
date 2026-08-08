# V2 Transparent Scene Bundle

EGTRAIN identifies portable case studies by the `.egscene` extension. The
payload is an ordinary ZIP archive, so external tools may report
`application/zip`; EGTRAIN does not rely on a registered MIME type. ZIP entries
use deflate or store compression. Encryption and passwords are not supported.
The archive is a transport container only: it loads into the existing V1
`SceneModel` and does not introduce a second scene representation.

## Root layout

Entries are files at the archive root. Bundle version 1 contains exactly these
seven required files, plus the optional passenger file:

```text
scene.json
infrastructure.json
stations.json
signalling.json
rolling_stock.json
services.json
scenarios.json
passengers.json       (optional)
```

Entry names are case-sensitive and must match these names exactly. Unknown,
nested, duplicate (including case-insensitive duplicates), and directory
entries are rejected. `views.json`, `legacy/`, assets, results, and other
generated or passthrough files are not part of bundle version 1.

## Manifest

The bundled `scene.json` keeps the V1 `schema_version: 1` and adds:

```json
{
  "format": "egscene",
  "bundle_version": 1
}
```

`format` identifies the container and `bundle_version` versions the container
independently from the data schema. Both fields are required in a bundle;
directory scenes may omit them. A reader rejects another marker, bundle
version, or schema version before extracting files.

## Safety limits

The reader checks archive metadata before allocating or extracting entry data:

- at most 16 entries;
- compressed bundle file at most 32 MiB;
- each entry at most 16 MiB uncompressed;
- total entries at most 64 MiB uncompressed;
- compression ratio at most 1000:1 (an entry with zero compressed bytes may
  only have zero uncompressed bytes).

Absolute, drive-qualified, backslash, dot, and dot-dot paths are rejected.
Encrypted or unsupported methods, malformed/truncated archives, invalid
central-directory metadata, symlinks, and special files identified by ZIP
attributes are rejected. Extraction uses only predetermined allowlisted names
inside a private RAII temporary directory, which is removed on success and
failure.

## Read, write, and unpack behavior

`loadSceneBundle` validates the complete archive, extracts the allowlisted JSON
files to a temporary directory, and calls the existing `loadScene`. The shared
`loadScenePath` helper dispatches directories to `loadScene` and files to the
bundle reader. `unpackSceneBundle` performs the same validation and structural
load before publishing an extracted directory. The destination must not already
exist, so unpacking cannot replace unrelated files.

`saveSceneBundle` first calls the existing `saveScene` in a temporary
directory. It adds only the bundle marker and version to that temporary
`scene.json`, then writes sorted entries with fixed compression and zero ZIP
timestamps. Source model values are not mutated. The archive is written to a
sibling staging file and atomically published. An existing regular bundle is
replaced by the same atomic operation, so a failed publication leaves the old
path intact. Generated outputs and legacy files are never included.
