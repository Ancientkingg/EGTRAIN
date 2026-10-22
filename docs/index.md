# EGTRAIN documentation

EGTRAIN is a railway micro-simulation desktop application. The manual is for people who run and edit case studies.

```{toctree}
:caption: Run existing cases
:maxdepth: 1

README
guides/install
guides/getting-started
guides/case-studies
guides/how-to
guides/opening-a-case-study
guides/main-window
guides/updates
guides/privacy-and-diagnostics
guides/glossary
guides/keyboard-shortcuts
guides/command-line
guides/run-output
guides/amsterdam-hilversum-student-case
guides/delay-analysis
guides/capacity-analysis
guides/diagram-navigation
```

```{toctree}
:caption: Tutorials
:maxdepth: 1

guides/tutorial-modify-a-case
guides/tutorial-minimal-scene
```

```{toctree}
:caption: Create or edit cases
:maxdepth: 1

guides/editing-a-case
guides/scenarios
guides/scene-schema
guides/scene-bundle
guides/v1-scene-migration-matrix
guides/signalling-levels
guides/assignment-workflow
```

The developer pages explain how the application is built, tested and structured, next to the [C++ reference](https://ancientkingg.github.io/EGTRAIN/latest/api/index.html).

```{toctree}
:caption: Develop or contribute
:maxdepth: 1

development/getting-started
development/build
development/testing
development/smoke-tests
development/characterization-tests
development/ci-and-release
development/coding-guidelines
development/error-handling
development/documentation-decision
development/code-documentation-investigation
development/readable-ids-decision
architecture/source-layout
architecture/scene-model
architecture/scene-editor-flow
architecture/external-sharing
development/live-sharing
telemetry/README
architecture/dialog-presentation
architecture/main-window-style
architecture/interface-style
architecture/renderer-style-map
architecture/historical-track-comparison
development/playback-profiling
development/memory-baselines
development/qt6-migration-study
```

```{toctree}
:caption: Release
:maxdepth: 1

development/release-testing-checklist
development/release-rehearsal-template
development/code-signing
development/windows-installer
```

Each of these pages records one conversion, comparison, rehearsal or check. They are not guides for the current release.

```{toctree}
:caption: Records
:maxdepth: 1

development/records/v1-native-cutover-report
development/records/original-case-runtime-parity
development/records/release-rehearsal-2026-10-17
development/records/dialog-sheet-2026-10-17
development/records/feature-status-2026-10-17
```
