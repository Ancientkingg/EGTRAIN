# #105 Harden Bundle Loading Against Malformed And Hostile Archives

- GitHub: https://github.com/Ancientkingg/EGTRAIN/issues/105
- State: open
- Labels: `type: feature`, `area: bundle`, `testing`
- Created: 2026-07-06
- Mirrored: 2026-07-06

Part of the single-file scene bundle epic (#99).

## Summary

Harden `.egscene` loading against malformed and hostile archives. A bundle is an untrusted file a student downloads from the internet, so the reader must fail safely on anything abnormal rather than crash, hang, or write outside its temp directory.

## Threats to defend against

- **Zip-slip / path traversal:** an entry named `../../etc/...` or an absolute path escaping the extraction directory. Reject any entry whose normalised path leaves the temp root.
- **Zip bomb:** a tiny archive that expands to gigabytes. Cap total uncompressed size (e.g. 256 MB), per-entry size, entry count, and compression ratio; abort when a cap is exceeded.
- **Unexpected members:** only extract the recognised canonical filenames (`scene.json` and the known JSON files); ignore everything else, and never execute or follow symlinks.
- **Malformed data:** truncated ZIP, corrupt central directory, non-UTF-8 or non-JSON contents → a clear diagnostic naming the offending entry.
- **Resource cleanup:** always delete the temp extraction directory, including on the error path.

## Scope

- Add the caps and traversal checks to `loadSceneBundle` (and the shared extraction routine).
- Emit `SceneDiagnostic`s that name the entry and the limit/rule violated (reuse the `SceneValidator` diagnostic style).
- Malicious-fixture tests in `test_scenebundle.cpp`: zip-slip entry, oversize/ratio bomb (synthetic, not a real 4 GB file), truncated archive, absolute-path entry, symlink entry, non-JSON member. Each must return diagnostics and leave no files outside the temp dir.

## Acceptance criteria

- No archive can cause a write outside the extraction temp directory.
- Configured size/count/ratio caps abort loading with a diagnostic.
- Every malicious fixture is rejected cleanly; ASan build stays clean.
- Temp directories are removed on both success and failure.

## Notes

Depends on the reader child. Kept separate so the reader can land on the happy path first and the security work is reviewable on its own — appropriate weight for a file format students obtain from the internet.
