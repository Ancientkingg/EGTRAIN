# Netherlands Input Audit

## Scope
Audited `EGTRAIN/QEGTRAIN/Input/Input_EGTRAIN_Netherlands` for the assignment case study path.

Baseline commands:

```bash
./build/scene_tool import EGTRAIN/QEGTRAIN/Input/Input_EGTRAIN_Netherlands /tmp/nl-scene Netherlands
./build/scene_tool validate /tmp/nl-scene
```

Before repair, import found 41 stations and 74 routes, but no services or rolling stock. Validation failed with:

- `scene.trains.none`
- `scene.services.none`

## Fixed
- `trainNames.txt` listed bucket directories (`All Trains`, `AM`, `PM`, etc.). Both the legacy loader and importer skip entries without `txt`, so no trains loaded.
- Added the selected Dutch Sprinter files as flat `Trains/Spr1.txt` through `Trains/Spr8.txt` and made `trainNames.txt` list those files.
- Repaired the selected train file references to use the Netherlands case folder:
  - `/TrainData/SLT06.txt`
  - `/TrainData/T_SLT06.txt`
  - `/TimeTable/Spr*.txt`
- Kept the old bucket directories in place as unused source data.

## Importer Gap Fixed
Netherlands route files contain switch-transition route entries such as:

```text
@1-B32@-1.577000/@0-B36@-1.582000
```

V1 route entries are opaque strings, so these are valid route tokens. The importer now preserves them without emitting `scene.import.parse`.

## Current Result
After repair:

- Import exits 0.
- Validate exits 0.
- Imported scene counts:
  - 41 stations
  - 74 routes
  - 5,429 route entries
  - 8 services
  - 1 train unit
  - 1 composition
- Import still emits 30 `scene.import.adjusted` warnings for `-1` departures at intermediate timetable stops. These are expected and documented adjustments.

Locked by `test_sceneimporter`, which now imports and validates the Netherlands source and checks that complex route tokens are preserved without parse warnings.

## Runtime Check
Case 1 now loads the Sprinter trains and runs to `End of Simulation` without the previous zero-train or train-file-open errors.

Remaining runtime limitations:

- The generated station statistics still show no served station rows for the repaired case.
- The run logs missing optional Network Areas and Passenger files.
- GUI timetable HTML template is absent for this case.

End-to-end Netherlands movement remains outside this slice.

## Wontfix For V1
- Unused Waterloo and other bucketed train sets under `Trains/` were left untouched.
- The repaired legacy Netherlands station set contains `Ut`, `Asd`, `Hvs`, `Brn`, `Dld`, and `Alm`, but not `Gvc` or `Gdg`. The assignment corridor should be built as a dedicated scene rather than treated as already present in this legacy folder.
