# Release Testing Checklist

Use this checklist before a student release. Record the tag, commit SHA,
platform, OS version, package name, case-study bundle, result, and linked defect
for every rehearsal.

## Automated release evidence

Confirm the `Release` workflow finishes with the macOS, Windows, and Linux jobs
green. The macOS job must also complete its scene-bundle generation and
validation step.

Expected application assets:

- `QEGTRAIN-macos-arm64.zip`
- `QEGTRAIN-windows-x64.zip`
- `QEGTRAIN-linux-x86_64.AppImage`

Expected case-study assets:

- `Netherlands.egscene`
- `Paimpol.egscene`
- `Copenhagen.egscene`
- `Milano_Brescia.egscene`
- `Assignment_Gvc_Gdg_Ut.egscene`
- `Lebanon.egscene`

Confirm the workflow validates all six bundles and publishes exactly the three
application assets plus the six case-study assets. The macOS job also runs a
packaged headless case. These checks prove artifact construction, not the GUI
student workflow.

## Clean-install GUI rehearsal

Run this section on macOS, Windows, and Linux from downloaded release assets.
Use a clean machine, VM, or user account without a source checkout, Qt, CMake,
or other development dependencies in the test path.

1. Launch the downloaded application through the platform's normal GUI path.
2. Open a downloaded `.egscene` through **File > Open Case Study...**.
3. Confirm the network renders and Loaded Data shows identity, versions,
   category counts, scenarios, validation, runtime, and result readiness.
4. Select or edit a scenario and confirm the run review names that scenario.
5. Use **Save Case Study As...** to write a working copy outside the package.
   Confirm the downloaded source bundle is unchanged.
6. Run a short simulation from the working copy.
7. Open timetable, delay, speed, and blocking-time results where the case
   supports them. Confirm timetable output separates planned and simulated
   arrival and departure values.
8. Export one CSV and one PNG to a user-writable directory.
9. Quit, relaunch, and reopen the working copy. Confirm saved canonical and
   scenario edits remain.

Also open `Assignment_Gvc_Gdg_Ut.egscene` and confirm its current data loads.
Known station and rolling-stock fidelity gaps belong to #182, #228, and #181;
they are not packaging failures.

## Result record

| Check | macOS | Windows | Linux | Evidence or defect |
| --- | --- | --- | --- | --- |
| Clean GUI launch |  |  |  |  |
| Open downloaded `.egscene` |  |  |  |  |
| Loaded Data and validation |  |  |  |  |
| Scenario select or edit |  |  |  |  |
| Save As outside package |  |  |  |  |
| Source bundle unchanged |  |  |  |  |
| GUI simulation completes |  |  |  |  |
| Result views open |  |  |  |  |
| CSV export |  |  |  |  |
| PNG export |  |  |  |  |
| Saved copy reopens |  |  |  |  |
| No source or development dependency |  |  |  |  |

The release gate passes only after every required row passes on all three
platforms or is marked not applicable with a case-specific reason. Open one
focused defect for each failed root cause instead of adding implementation work
to issue #74.
