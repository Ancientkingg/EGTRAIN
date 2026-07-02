# Assignment Workflow

EGTRAIN should replace the current OpenTrack workflow used in the railway traffic management assignment. The target V1 scenario is Den Haag Centraal (`Gvc`) to Gouda (`Gdg`) to Utrecht Centraal (`Ut`).

## Users

- Students configure trains, build timetables, run simulations, inspect diagrams, and export results.
- Instructors need repeatable scenes and clear validation errors.
- Researchers need readable input data and reproducible runs.

## V1 Student Stories

- As a student, I can load the assignment scene from the app start screen.
- As a student, I can edit train material, service stops, dwell times, platforms, and per-service performance.
- As a student, I can create a basic half-hour timetable without editing text files.
- As a student, I can run one train to get minimum running time.
- As a student, I can add running-time supplements and timetable rounding.
- As a student, I can run the full timetable and compare planned and simulated output.
- As a student, I can inspect speed-distance, speed-time, time-distance, delay, and blocking-time diagrams.
- As a student, I can toggle trains in diagrams and export tables and figures for a report.
- As a student, I can add assignment incidents such as a signal failure or train breakdown.

## V1 Editor Scope

The first editor should cover assignment-level data:

- train compositions
- train services
- stopping patterns
- dwell times
- platform choices
- scheduled times
- repeated services
- per-service performance values
- signal and train incidents

The editor should show infrastructure context for selection. It should not support full track drawing in V1.

## Later Editor Scope

Full infrastructure editing is a later milestone:

- nodes and arcs
- tracks and switches
- gradients and curves
- speed limits
- stations and platforms
- signals and block sections
- routes and corridors

## Data Policy

Students should work through the application. Hand editing should remain possible for review, debugging, and research, but it should not be the normal assignment workflow.
