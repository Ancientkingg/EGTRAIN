# Release rehearsal — 2026-08-23

Candidate: GitHub Actions [Release run 32633492769](https://github.com/Ancientkingg/EGTRAIN/actions/runs/32633492769), commit `947fbc5`.

The package jobs are automated build/package evidence. A GUI pass below means
the listed interaction was performed from the downloaded package; it is not
inferred from CI.

| Platform | Package and environment | Package/build verification | GUI clean-install rehearsal | Result |
| --- | --- | --- | --- | --- |
| macOS 26.6.2, Apple Silicon | `QEGTRAIN-macos-arm64.zip`, downloaded from the candidate run into a fresh `/tmp` directory outside either checkout | Package macOS job passed; local bundle signature verified and executable references only bundled frameworks plus system libraries | Launched `QEGTRAIN.app`; opened downloaded `Paimpol.egscene`; Loaded Data showed the bundle source and zero validation errors; saved `Paimpol-working.egscene` outside the package; reviewed baseline scenario and ran two selected occurrences; inspected Run Results and Speed vs Distance; exported `run_summary.csv` and `run_summary.png`; quit, relaunched, and reopened the working copy | **Core #74 path passed; broader checklist remains partial** |
| Windows | `QEGTRAIN-windows-x64.zip` from the candidate run | Package Windows job passed | No Windows GUI environment was available | **Not genuinely verified** — manual GUI rehearsal required |
| Linux | `QEGTRAIN-linux-x86_64.AppImage` from the candidate run | Package Linux job passed | No Linux desktop GUI environment was available | **Not genuinely verified** — manual GUI rehearsal required |

## macOS integrity evidence

- Source bundle: `Paimpol.egscene`, SHA-256
  `4e9c743a4626e594529d7f3f8fa298a5397591cebc6629d09c20e734f08566ce`
  both before and after the rehearsal.
- Saved working copy reopened successfully after relaunch. Its SHA-256 matched
  the unedited source bundle.
- `run_summary.csv` contained a header, two train rows, and a network-total
  row. `run_summary.png` was a non-empty 2192×116 PNG.
- The separately downloaded `Assignment_Gvc_Gdg_Ut.egscene` also opened in the
  same app with zero validation errors and a rendered two-track preview.
- The host contains development tools, but the package was run outside either
  checkout and its executable resolved only bundled frameworks and macOS system
  libraries. This is packaged-path evidence, not a claim that the host was a
  sterile clean OS image.

The supplementary checklist result views (timetable, delay, and blocking-time)
were not exercised in this run. They are not needed for the core #74 path
recorded above and must not be inferred from CI.

## Remaining manual verification

On both Windows and Linux, download the listed candidate package and a
distributed `Paimpol.egscene`, then perform the same GUI sequence recorded for
macOS: launch, open bundle, Save Case Study As outside the package, review the
baseline scenario, run, inspect Run Results and a diagram, export CSV and PNG,
relaunch/reopen the copy, and verify the source bundle hash is unchanged.

Issue [#74](https://github.com/Ancientkingg/EGTRAIN/issues/74) remains open
until those two genuine GUI rehearsals are recorded.
