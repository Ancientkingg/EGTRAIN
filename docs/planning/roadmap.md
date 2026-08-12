# Roadmap

## Current product state

EGTRAIN uses canonical V1 scene data for normal GUI, CLI, preview, and
simulation input. The native builders construct infrastructure, signalling,
rolling stock, services, timetables, the selected scenario, entrance delays,
and passengers directly from `SceneModel`. Legacy import and export remain
explicit interoperability tools.

Six case studies are committed as canonical scene directories. V2 `.egscene`
files provide the portable form of the same V1 data. The application can open
and save both forms. Loaded Data, validation, train and service editors, named
scenario editing, native simulation, planned and simulated timetable results,
CSV export, and PNG export are implemented.

Release automation builds macOS, Windows, and Linux packages and publishes six
validated `.egscene` bundles. The remaining release gate is a recorded
clean-install GUI rehearsal, not package generation.

## P0: Packaged student release rehearsal

Goal: prove that a student can use a downloaded package and case study without
a source checkout or development dependencies.

- Complete and record the Windows, macOS, and Linux rehearsal in issue #74.
- Open a downloaded `.egscene`, inspect Loaded Data, select or edit a scenario,
  save a working copy, run it, inspect results, export CSV and PNG files, and
  reopen the saved copy.
- Treat CI package and bundle checks as automated evidence. They do not replace
  the clean-install GUI run.
- Fold the remaining macOS GUI acceptance from #233 into the same rehearsal.
  Track any failure as a focused platform issue.

See [Release Testing Checklist](../development/release-testing-checklist.md).

## P0: Assignment case-study fidelity

Goal: make `Assignment_Gvc_Gdg_Ut` match the source corridor, stopping patterns,
and train material without guessed parameters.

1. Complete #182 for intermediate stations, distinct `Gd` and `Gdg` records,
   route reachability, and source stopping patterns.
2. Complete #228 for traceable ICM3, ICM4, IRM3, and SGM3 physical and traction
   source data. This can proceed alongside #182.
3. Complete #181 after #228, using that data for train-unit definitions and the
   four assignment service compositions.

The assignment epic remains #4. Do not substitute similar train types or infer
missing physics.

## P1: Assignment-level editing gaps

Goal: support the remaining assignment changes through canonical data and the
GUI.

- #35: define, persist, and apply per-service performance controls.
- #36: add repeated-service count and train-number step. Repeat headway editing
  already exists.
- #37: add the course-defined timetable rounding workflow.
- #51: add reduced-speed and recovery behavior for train breakdowns. Basic
  target and incident-window editing already exists.

## P1: Assignment analysis and disruption outputs

Goal: provide the unfinished assignment analysis workflows without relisting
basic scenario editing as missing.

- #38: support the Gouda overtaking workflow.
- #45 through #49: add minimum headway, timetable compression, critical-block,
  buffer-time, and capacity-percentage workflows.
- #52: report primary, secondary, per-train, and total arrival delay.

Signal-failure editing and selected-scenario execution are complete.

## P1: Focused UI, release, and test reliability

Keep defects and small workflow gaps as focused issues. Current examples are
repository hygiene (#68), native migration parity evidence (#85 and #86),
bundle drag and drop (#103), hostile-bundle regression coverage (#105),
bundle-guide screenshots (#107), scenario change visibility (#126), export
provenance (#129), consent-based updates (#155), startup state (#240),
deterministic train reselection (#244), station-name acronyms (#246), overview
station markers (#252), and measured startup or case-loading latency (#257).

## P2: Full infrastructure editing

The full infrastructure editor remains under epic #8 and issues #53 through
#58. It covers canonical node, arc, track, switch, station, platform, signal,
block, and topology editing. Normal verification must validate and save the
canonical scene. Legacy export is an additional compatibility check only where
the feature promises it.

## P2: Measurement-led performance and refactoring

Measure before changing storage or ownership. Use #62, #63, and #257 to identify
the dominant memory, playback, startup, or loading cost. Schedule #60, #61, or
other storage changes only when measurements support them.

Issues #117 through #120 remain valid but deferred. Extract editor or session
ownership from `MainWindow` only when a product change is blocked, duplicated
state causes defects, or focused tests cannot be added safely.

## Completed scene migration

| PR | Completed work |
| --- | --- |
| #260 | Canonical V1 model coverage |
| #261 | Native infrastructure and signalling construction |
| #262 | Native rolling stock, services, scenarios, entrance delays, and passengers |
| #263 | Normal GUI, CLI, preview, and simulation cutover to canonical scenes |
| #264 | Portable `.egscene` bundles and release distribution |
| #265 | Loaded Data inspection and lineage |
| #266 | Named scenario and transparent results workflow |
| #267 | Removal of obsolete normal-runtime legacy readers |

The migration is complete. Explicit legacy import and export remain supported
for compatibility and research workflows.
