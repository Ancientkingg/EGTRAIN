# Roadmap

## Product Target

EGTRAIN V1 should support the railway traffic management assignment without requiring students to edit legacy input files. The application should load an assignment scene, edit assignment-level train and timetable data, run simulations, show the required diagrams, and export report data.

Full infrastructure editing is not part of V1. It stays on the roadmap after the assignment workflow works.

## Baseline In Place

- Qt application build through CMake.
- Start, pause, stop, and playback speed controls.
- Worker-thread simulation path.
- Smooth train drawing and current-speed labels.
- Timetable, delay, speed, and blocking-time diagrams.
- Read-only right-side train information.
- Console logging moved into an in-app log pane.
- Headless smoke tests for Copenhagen and Brescia.
- Visual smoke test for the main UI.
- GitHub Actions build and unit-test workflow.

## P0: Repository Setup

Goal: make the repository buildable, testable, and clean from a fresh clone.

Status: done.

## P0: Scene Model And Converter

Goal: replace the messy scene input workflow with a clean canonical scene format.

Missing work:

- Define the V1 scene schema.
- Write a scene validator with user-facing diagnostics.
- Import legacy case-study folders into the new format.
- Export canonical scenes back to the current simulator input files.
- Add conversion tests for Copenhagen and Brescia.
- Add clear errors for malformed input.

## P0: MVP Scene Editor

Goal: let students edit assignment-level data in the app.

Missing work:

- Scene open, save, and save-as flow.
- Validation panel.
- Train composition editor.
- Service and stopping-pattern editor.
- Timetable editor with half-hour service support.
- Repeated-service creation.
- Per-service performance parameter editor.
- Incident editor for signal failures and train breakdowns.
- Infrastructure context selection without full track drawing.

## P0: Assignment Case Study

Goal: build the first EGTRAIN version of the OpenTrack assignment.

Missing work:

- Audit and repair Netherlands input data.
- Verify the Amsterdam to Hilversum route candidates.
- Build a curated assignment scene.
- Add train material variants.
- Add base timetable and disruption scenarios.
- Add an assignment smoke test.
- Check the workflow from a student perspective.

## P1: CI/CD Pipeline

Goal: make pull requests and release candidates prove that the application builds, runs, and keeps its generated files out of git.

Missing work:

- Run unit tests on every pull request and push to `main`.
- Add headless smoke tests to CI.
- Add visual smoke test to CI where the runner supports it.
- Upload failure logs and screenshots as workflow artifacts.
- Add repository hygiene checks for generated files, backup files, and internal notes.
- Add a release matrix for Windows, macOS, and Linux.
- Build short-lived artifacts on `main`.
- Add tagged release workflow for `v*` tags.
- Package Windows releases as a portable zip first.
- Package macOS releases as a zipped `.app` first.
- Package Linux releases as an AppImage or portable archive first.
- Attach all three platform packages to GitHub Releases.
- Document a release testing checklist.

## P1: Student Workflow UI

Goal: make the GUI fit the assignment steps.

Missing work:

- Make load network the main entry point.
- Add guided setup for assignment runs.
- Improve simulation controls and speed control.
- Use proper `HH:MM:SS` time formatting with a configurable base time.
- Keep read-only panes clearly read-only.
- Add camera follow options.
- Add visual cues for acceleration and braking.
- Distinguish station, train, and track types.

## P1: Diagrams And Exports

Goal: support the figures and tables students need in their report.

Missing work:

- Train filtering in diagrams.
- Hover readouts for graph values.
- Planned timetable table and graph views.
- Simulated timetable table and graph views.
- Blocking-time overlay on the simulated timetable.
- Speed-distance, speed-time, and single-train time-distance diagrams.
- PNG export for diagrams.
- CSV export for tables.

## P1: Capacity And Disruptions

Goal: support the capacity and disruption sections of the assignment.

Missing work:

- Minimum headway workflow.
- Timetable compression.
- Critical-block highlighting.
- Buffer-time calculation.
- Capacity percentage calculation.
- Signal failure scenario.
- Train breakdown scenario.
- Primary, secondary, and total delay output.

## P2: Full Infrastructure Editor

Goal: edit infrastructure inside the application after V1 works.

Missing work:

- Node and arc editing.
- Track and switch editing.
- Gradient, curve, and speed-limit editing.
- Station and platform editing.
- Signal and block-section editing.
- Topology validation.
- Visual scene editor linked to the tabular model.

## P2: Memory And Performance

Goal: lower memory use without changing simulation behavior.

Missing work:

- Inventory ownership and raw pointer use.
- Replace oversized fixed arrays one storage type at a time.
- Start with `BlockSet B[268]`.
- Then handle `signalling_block_sections[6000]`.
- Then handle embedded fixed arrays inside `Route`, `BlockSet`, and `Section`.
- Add memory measurement scripts.
- Profile GUI playback and simulation step time.
