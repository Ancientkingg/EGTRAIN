# Assignment corridor

## Target case

The TU Delft assignment runs from Den Haag Centraal (`Gvc`) to Utrecht (`Ut`)
via Gouda (`Gd` and `Gdg`). EGTRAIN uses this as its acceptance case for
authoring, timetable, capacity, rolling-stock, and disruption work.

## Repository status

`Assignment_Gvc_Gdg_Ut` currently contains a synthetic two-track fixture with
three stations (`Gvc`, `Gdg`, and `Ut`), no signals, one placeholder
`SLT_Sprinter` composition, and four services that all use that placeholder.
The historical generator built uniform 2 km blocks with fixed geometry and did
not convert the OpenTrack case. The fixture can validate, run, round trip, and be
packed as `.egscene`, but those checks do not make its railway data authoritative.

Do not use the fixture for model answers or publish it as the completed
assignment. The expected course archive, `OpenTrack data 2016.zip`, is not in
the workspace or repository. The model-answer PDF gives the educational tasks,
named services, timetable exercises, and selected composition information. It
does not supply the complete corridor geometry, signalling records, S451
mapping, or train physics.

## Source blockers

- [Issue #182](https://github.com/Ancientkingg/EGTRAIN/issues/182) owns the
  corridor, intermediate stations, stopping patterns, detailed signalling, and
  historical S451 mapping.
- [Issue #228](https://github.com/Ancientkingg/EGTRAIN/issues/228) owns sourced
  ICM3, ICM4, IRM3, SGM3, and Plan V physical and traction records.
- [Issue #181](https://github.com/Ancientkingg/EGTRAIN/issues/181) builds the
  assignment compositions and service assignments after those records exist.

Screenshots and OpenTrack result plots are not substitutes for missing input
records. Current ProRail data also cannot establish the historical teaching
model.

## Completion rule

The case is complete only after the source records have been converted into
canonical scene data, inspected through the normal editor, rehearsed through
all 25 operations in the [assignment workflow](assignment-workflow.md), saved
as `.egscene`, reopened, and rerun. Normal runtime must remain canonical
`SceneModel` data through native builders, with legacy import and export used
only for explicit compatibility work.
