# Case file format

A case file is a case saved as one file, with the `.egscene` extension. The
payload is an ordinary ZIP archive, so external tools may report
`application/zip`; EGTRAIN does not rely on a registered MIME type. ZIP entries
use deflate or store compression. Encryption and passwords are not supported.
The archive is a transport container only: it loads into the existing V1
[`SceneModel`] and does not introduce a second scene representation.

The name V2 refers to the container. The container's own number is
`bundle_version` 1, and the data inside has `schema_version` 1. A higher
version needs a newer EGTRAIN.

A case folder is the editable source of truth. A case file packages
its files for transport. Legacy import and export remain explicit
interoperability actions; normal case loading and simulation use the
canonical model.

## Root layout

Entries are files at the archive root. A case file of `bundle_version` 1 contains exactly these
seven required files, plus optional passenger and display metadata:

```text
scene.json
infrastructure.json
stations.json
signalling.json
rolling_stock.json
services.json
scenarios.json
passengers.json       (optional)
views.json            (optional)
```

Entry names are case-sensitive and must match these names exactly. Unknown,
nested, duplicate (including case-insensitive duplicates), and directory
entries are rejected. `legacy/`, assets, results, and other generated or
passthrough files are not part of `bundle_version` 1.

## Manifest

The bundled `scene.json` keeps the V1 `schema_version: 1` and adds:

```json
{
  "format": "egscene",
  "bundle_version": 1
}
```

`format` identifies the container and `bundle_version` versions the container
independently from the data schema. Both fields are required in a case file;
case folders may omit them. A reader rejects another marker,
`bundle_version`, or schema version before extracting files.

Compatibility probing reads the bounded `scene.json` manifest after generic ZIP
safety checks. It can classify a newer layout without extracting unknown
entries; the v1 allowlist is enforced only when interpreting or extracting a
v1 case file.

## Safety limits

The reader checks archive metadata before allocating or extracting entry data:

- at most 16 entries;
- compressed case file at most 32 MiB;
- each entry at most 16 MiB uncompressed;
- total entries at most 64 MiB uncompressed;
- compression ratio at most 1000:1 (an entry with zero compressed bytes may
  only have zero uncompressed bytes).

Absolute, drive-qualified, backslash, dot, and dot-dot paths are rejected.
Encrypted or unsupported methods, malformed/truncated archives, invalid
central-directory metadata, symlinks, and special files identified by ZIP
attributes are rejected. Normal v1 loading extracts only allowlisted names.
An explicitly registered migration may inspect other generically safe entries
from an older layout, always inside a private temporary directory that is
removed on success and failure.

## Read, write, and unpack behavior

`loadSceneBundle` validates the complete archive, extracts the allowlisted JSON
files to a temporary directory, and calls the existing `loadScene`. The shared
`loadScenePath` helper dispatches directories to `loadScene` and files to the
case file reader. `unpackSceneBundle` performs the same validation and structural
load before publishing an extracted directory. The destination must not already
exist, so unpacking cannot replace unrelated files.

`saveSceneBundle` first writes the scene files into a temporary directory and
loads them back, as `saveScene` does in its staging folder. It adds only the
container marker and `bundle_version` to that temporary `scene.json`, then writes sorted
entries with fixed compression and zero ZIP timestamps. Source model values are
not mutated. The archive is written to a sibling staging file, flushed to disk
and atomically published. An existing case file at the path is
replaced by the same atomic operation, so a failed publication leaves the old
path intact. Generated outputs and legacy files are never included.

[`SceneModel`]: https://egtrain.samuelbruin.com/latest/api/struct_scene_model.html
