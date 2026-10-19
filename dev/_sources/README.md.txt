# Documentation

Choose the route that matches your task.

## Run existing scenes

- [EGTRAIN releases](https://github.com/Ancientkingg/EGTRAIN/releases)
- [Getting started](guides/getting-started.md): from a download to a first run of the Paimpol case study, its results and two exported files.
- [How-to](guides/how-to.md): recipes for opening and saving a case, fixing validation errors, editing a timetable or rolling stock, comparing a scenario with a baseline, moving around the network view, replaying a run, exporting results and converting a legacy case, and why a command is disabled.
- [Opening an `.egscene` case study](guides/opening-a-case-study.md)
- [Updating EGTRAIN](guides/updates.md): the automatic and manual checks, consent, what an update does to an installed or portable copy, and the earlier builds that need a manual upgrade.
- [Glossary of application terms](guides/glossary.md)
- [Command-line options and scene_tool](guides/command-line.md)
- [Run output files](guides/run-output.md)
- [Sharing a live run](guides/live-sharing.md): how to start a run with the live stream, a small client that reads positions and sends one command, the commands, and what to check when it does not work.
- [Amsterdam to Hilversum student case](guides/amsterdam-hilversum-student-case.md)
- [How delays are calculated](guides/delay-analysis.md): the definitions of delay, how to read the Delay analysis window and its comparison against a baseline, the statistics files, the supported scope and the limits.
- [How capacity is analysed](guides/capacity-analysis.md): the train chain window and the capacity by area window, with the choice of scope and period, the completeness rule, the governing resource and a figure reproduced by hand.
- [Inspecting diagrams](ui/diagram-navigation.md): how to hover, select, zoom and pan in a result diagram, and how the timetable graph, the train-path graph and the blocking-time charts are drawn.

## Tutorials

- [Tutorial: modify a supplied case](guides/tutorial-modify-a-case.md): save a working copy of Paimpol, change a dwell and a performance, validate, save, reopen, run, and compare delays in an incident scenario.
- [Tutorial: the smallest useful scene](guides/tutorial-minimal-scene.md): build a one-track case with two stations and one train from New Case Study..., run it, save it as a folder and a bundle, and open it again.

## Create or edit scenes

- [Using EGTRAIN and authoring scenes](guides/scenes-and-application.md): what the docks and windows show, the rules for editing and validating, and the scene files in a Reference part.
- [V1 scene property reference](guides/v1-scene-properties.md)
- [Scene schema reference](architecture/scene-schema.md)
- [Scene bundle format](architecture/scene-bundle.md)
- [V1 active-input migration matrix](guides/v1-scene-migration-matrix.md): what each family of legacy input files becomes in a scene, and which of them a run does not use.
- [Signalling levels and signalling areas](architecture/signalling-levels.md)
- [Assignment corridor](product/assignment-corridor.md)
- [Assignment workflow](product/assignment-workflow.md)
- [Feature status 2026-10-17](product/feature-status.md): a dated snapshot of what works, what is only partly done and what is untested, with the evidence for each row and the gaps found.

A scene folder is the editable source of truth. An `.egscene` bundle packages
its files for transport. Legacy import and export remain explicit
interoperability actions; normal scene loading and simulation use the
canonical model.

## Develop or contribute

- [Getting started as a developer](development/getting-started.md): the tools you need, a first build and run, a map of the sources, the path from opening a case to saving it, which checks to run, and a first change to a test.
- [Build and test guide](development/build-and-test.md): configure recipes for each system, common setup failures, test labels and smokes, where output goes, and CI.
- [Coding guidelines](development/coding-guidelines.md)
- [Error handling](development/error-handling.md): where exceptions are caught or escape, the fatal paths, what the user sees and what is logged for each failure, the safety nets, and the crashes seen in October 2026.
- [Documentation decision](development/documentation-decision.md): how the docs are structured, built, checked and shipped in the app, the options compared, the screenshot process and the map of every page.

- [Code documentation investigation](development/code-documentation-investigation.md): the comment coverage per source folder, the legacy simulation core, the unstated units and conventions, dead or misleading comments, open questions and the order of the follow-up work.
- [QEGTRAIN source layout](architecture/source-layout.md): the folders of `EGTRAIN/QEGTRAIN` and what each holds, the libraries and the renames.
- [Scene model design](architecture/scene-model.md)
- [Scene editor flow](architecture/scene-editor-flow.md): how the editor creates, opens, validates, runs and saves a case, with the main window layout and the edit panes.
- [Signalling levels and signalling areas](architecture/signalling-levels.md)
- [External state sharing](architecture/external-sharing.md): the legacy `-TSM` and `-RC` channels, and the live stream with its contract version 1 (frames, units, ordering, sampling, compatibility, threads, commands and errors).
- [Usage and diagnostics wire contract](telemetry/README.md)
- [Custom dialog presentation contract](ui/dialog-presentation.md): the rules for custom dialogs, how to build one with `DialogLayout`, which dialogs use it, and the checks that cover them.
- [Dialog sheet 2026-10-17](ui/dialog-sheet-2026-10-17.md): the custom dialogs before and after the shared presentation, side by side, as a dated record.
- [Result-window presentation seam](ui/result-window-presentation.md): the contract for the heading, context and warning of the result windows, their size limits and the layout of the custom result dialogs.
- [Main window](ui/main-window.md): the toolbar speed slider and its readout, the run duration in Run simulation, the lines at the top of Run Results, the first sizes of the Run Results and Case and Layers docks, and the look of tables, the progress bar and the Run Results buttons.
- [Network renderer style map](ui/renderer-style-map.md): how the network canvas draws each element, with colours, sizes and contrast ratios.
- [Historical track presentation](ui/historical-track-comparison.md): the original renderer used as the visual reference, the only intentional differences from it, the captures that compare the two renderers, and how its drawing distances convert to scene units.
- [Playback profiling](development/playback-profiling.md): the opt-in tool that measures GUI playback of the Copenhagen case, with its protocol, what it measures and its decision rule.
- [Peak-memory baselines](development/memory-baselines.md): how to record the peak memory of the Copenhagen and Milano-Brescia runs on macOS, and the baselines recorded so far.
- [Qt 6 migration study](development/qt6-migration-study.md): what moving from Qt 5.15 to Qt 6 takes, with the incompatible APIs, packaging, CI, platforms, licences, estimates and an order of work.

## Release

- [Release testing checklist](development/release-testing-checklist.md)
- [Release rehearsal record template](development/release-rehearsal-template.md): the blank record to fill in for one candidate while following the checklist.
- [Code signing](development/code-signing.md)
- [Windows installer script](../installer/README.md): the parameters of the Inno Setup script, what the installer installs and removes, and how an installed copy is updated.
- [CI and release branches](development/build-and-test.md#ci-and-release-branches): the pull request checks, the package check and the release workflow.

## History and records

Each of these pages records one conversion, comparison or rehearsal. They are not guides for the current release.

- [V1 native cutover report](guides/v1-native-cutover-report.md): the conversion of the cases that were committed then to canonical V1 scenes, with the source corrections and the legacy material that was not migrated.
- [Original-case native runtime parity](guides/original-case-runtime-parity.md): the comparison of the output of the Netherlands, Paimpol, Copenhagen and Milano-Brescia cases before and after the native runtime cutover, and the differences found.
- [Release rehearsal 2026-08-23](development/release-rehearsal-2026-08-23.md): the record of one release rehearsal, with the result for each platform.
- [Release rehearsal 2026-10-17](development/release-rehearsal-2026-10-17.md): the record of the packaged rehearsal on macOS, Windows and Linux, with the result for each checklist row, the defects and the rows left to the owner.

Keep new documentation short, concrete, and current.
