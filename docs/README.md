# Documentation

Choose the route that matches your task.

## Run existing scenes

- [EGTRAIN releases](https://github.com/Ancientkingg/EGTRAIN/releases)
- [Opening an `.egscene` case study](guides/opening-a-case-study.md)
- [Glossary of application terms](guides/glossary.md)
- [Command-line options and scene_tool](guides/command-line.md)
- [Run output files](guides/run-output.md)
- [Lebanon case study guide](guides/lebanon-case-study.md)
- [Amsterdam to Hilversum student case](guides/amsterdam-hilversum-student-case.md)
- [How delays are calculated](guides/delay-analysis.md)
- [How capacity is analysed](guides/capacity-analysis.md)
- [Inspecting diagrams](ui/diagram-navigation.md): how to hover, select, zoom and pan in a result diagram, and how the timetable graph, the train-path graph and the blocking-time charts are drawn.

## Create or edit scenes

- [Using EGTRAIN and authoring V1 scenes](guides/scenes-and-application.md)
- [V1 scene property reference](guides/v1-scene-properties.md)
- [Scene schema reference](architecture/scene-schema.md)
- [Scene bundle format](architecture/scene-bundle.md)
- [V1 active-input migration matrix](guides/v1-scene-migration-matrix.md): what each family of legacy input files becomes in a scene, and which of them a run does not use.
- [Signalling levels and signalling areas](architecture/signalling-levels.md)
- [Assignment corridor](product/assignment-corridor.md)
- [Assignment workflow](product/assignment-workflow.md)

Canonical V1 scene directories are the editable source of truth. Transparent
`.egscene` bundles package those files for transport. Legacy import and export
remain explicit interoperability actions; normal scene loading and simulation
use the canonical model.

## Develop or contribute

- [Build and test guide](development/build-and-test.md)
- [Coding guidelines](development/coding-guidelines.md)
- [QEGTRAIN source layout](architecture/source-layout.md): the folders of `EGTRAIN/QEGTRAIN` and what each holds, the libraries and the renames.
- [Scene model design](architecture/scene-model.md)
- [Scene editor flow](architecture/scene-editor-flow.md): how the editor creates, opens, validates, runs and saves a case, with the main window layout and the edit panes.
- [Signalling levels and signalling areas](architecture/signalling-levels.md)
- [External state sharing (legacy)](architecture/external-sharing.md)
- [Usage and diagnostics wire contract](telemetry/README.md)
- [Custom dialog presentation contract](ui/dialog-presentation.md): the rules for custom dialogs, how to build one with `DialogLayout`, which dialogs use it, and the checks that cover them.
- [Result-window presentation seam](ui/result-window-presentation.md): the contract for the heading, context and warning of the result windows, their size limits and the layout of the custom result dialogs.
- [Network renderer style map](ui/renderer-style-map.md): how the network canvas draws each element, with colours, sizes and contrast ratios.
- [Historical track presentation](ui/historical-track-comparison.md): the original renderer used as the visual reference, how its drawing distances convert to scene units and the visual acceptance work that it names, with two earlier Copenhagen comparison images kept as records.
- [Playback profiling](development/playback-profiling.md): the opt-in tool that measures GUI playback of the Copenhagen case, with its protocol, what it measures and its decision rule.
- [Peak-memory baselines](development/memory-baselines.md): how to record the peak memory of the Copenhagen and Milano-Brescia runs on macOS, and the baselines recorded so far.

## Release

- [Release testing checklist](development/release-testing-checklist.md)
- [Release rehearsal record template](development/release-rehearsal-template.md): the blank record to fill in for one candidate while following the checklist.
- [Code signing](development/code-signing.md)
- [CI and release branches](development/build-and-test.md#ci-and-release-branches): the pull request checks, the package check and the release workflow.

## History and records

Each of these pages records one conversion, comparison or rehearsal. They are not guides for the current release.

- [V1 native cutover report](guides/v1-native-cutover-report.md): the conversion of the cases that were committed then to canonical V1 scenes, with the source corrections and the legacy material that was not migrated.
- [Original-case native runtime parity](guides/original-case-runtime-parity.md): the comparison of the output of the Netherlands, Paimpol, Copenhagen and Milano-Brescia cases before and after the native runtime cutover, and the differences found.
- [Release rehearsal 2026-08-23](development/release-rehearsal-2026-08-23.md): the record of one release rehearsal, with the result for each platform.

Keep new documentation short, concrete, and current.
