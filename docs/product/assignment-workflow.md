# TU Delft assignment workflow in EGTRAIN

EGTRAIN implements the railway authoring, simulation, capacity, and disruption
work behind the TU Delft OpenTrack assignment. The controls and result views are
EGTRAIN-native. Students should reproduce the method and explain their modelling
choices, not copy OpenTrack menus or target the model-answer timestamps.

The committed `Assignment_Gvc_Gdg_Ut` scene is an incomplete synthetic fixture.
It is useful for product smoke tests, but it is not the distributable assignment
case. The course archive named `OpenTrack data 2016.zip` is not in the repository,
and the model-answer PDF does not contain enough infrastructure, signalling, or
rolling-stock data to reconstruct it. [Issue #182](https://github.com/Ancientkingg/EGTRAIN/issues/182)
tracks the corridor and signalling source. [Issue #228](https://github.com/Ancientkingg/EGTRAIN/issues/228)
and [issue #181](https://github.com/Ancientkingg/EGTRAIN/issues/181) track the train
sources and compositions. Do not distribute the fixture as the completed course
case or fill those gaps from screenshots.

## Instructor preparation

Once the source records are available, an instructor can build the case through
the normal application workflow:

1. Choose **File > New Case Study...** and enter the case name, base time,
   duration, buffer, and recovery settings in **Case Settings**.
2. Use the **Infrastructure** table and network preview to add tracks, nodes,
   arcs, blocks, connections, stations, platforms, signals, routes, dependencies,
   restrictions, boundaries, and signalling areas.
3. Add sourced train units, their physical fields and traction curves, then build
   ordered compositions from those units.
4. Add the four services, route and platform stops, planned arrival and departure
   times, dwell, per-service performance, and repeat rules.
5. Add named scenarios for the signal failure and occurrence-specific train
   breakdown.
6. Resolve validation errors, run representative services, and save both the
   canonical folder and a `.egscene` bundle. Reopen the bundle before release.

The assignment defines these base services and compositions. Their physical
train-unit records still require the sources tracked in issues #228 and #181.

| Service | Assignment composition |
| --- | --- |
| IC 1723 | 2xICM3 + 2xICM4 |
| S 19825 | 1xSGM3 |
| IC 2025 | 2xIRM3 |
| S 9827 | 1xSGM3 |

The alternative-composition exercise uses 1xICM3 for IC 1723. The
rolling-stock comparison replaces SGM with sourced Plan V data without changing
the service pattern.

An incomplete scene remains editable and saveable. **Run** remains unavailable
when validation reports an error. Missing signalling coverage is never replaced
with an implicit conventional or ETCS setting.

## Exercise sequence

| # | Assignment operation | EGTRAIN action | Student work |
| --- | --- | --- | --- |
| 1 | Define train-unit compositions | Author sourced train units and ordered compositions. Inspect the static traction input separately from results. | Check the unit order and record the composition used. |
| 2 | Define four base services | Author services with an operating code, composition, route, and stops. | Check each stopping pattern. |
| 3 | Define routes, platforms, and stops | Use the Infrastructure table and preview, then select those records in the service editor. | Explain the chosen route and platform pattern. |
| 4 | Simulate trains individually | Select one service occurrence and run it without changing the canonical timetable. | Record and interpret minimum running times. |
| 5 | Tune per-service performance | Edit `performance_percent` and rerun the same occurrence. | Test values and explain the nonlinear running-time response. |
| 6 | Create the planned timetable | Edit planned arrival and departure independently at each stop. | Apply the course scheduling and rounding method. |
| 7 | Repeat half-hour services | Set a 1,800 second interval, an explicit count, and the operating-code step. Preview generated occurrences. | Check sequences such as 1723, 1725, and 1727. |
| 8 | Model overtaking at Gdg | Use ordinary route, platform, dwell, and timetable fields. | Choose and justify the overtaking pattern. |
| 9 | Inspect blocking times | Open **Blocking time**, select the route, block range, time range, and trains. | Compare planned timing, actual occupations, and conflicts. |
| 10 | Inspect speed versus distance | Open the existing speed-distance result and filter the trains. | Explain acceleration, braking, and line-speed effects. |
| 11 | Inspect actual tractive effort | Open the applied tractive-effort distance result or export its CSV. | Distinguish positive traction, coasting or balance, and braking from the static traction curve. |
| 12 | Compare an alternative composition | Duplicate or edit a composition assignment and rerun. | Explain the change in performance and feasibility. |
| 13 | Compare SGM and Plan V | Select the sourced alternatives and rerun the same service. | Explain the observed differences. This exercise remains blocked until #228 and #181 supply both records. |
| 14 | Compute minimum headways | Open **Capacity**, choose an ordered pair and explicit route section. | Interpret the minimum headway and governing block. |
| 15 | Compress the timetable | Use the same ordered occurrence sequence in **Capacity** and open the compressed diagram. | Check that order is preserved and occupations do not conflict. |
| 16 | Identify critical blocks | Inspect the reported touching constraints in the compressed result. | Explain why a critical touch is not an overlap conflict. |
| 17 | Obtain buffer times | Read scheduled headway, minimum headway, and their unclamped difference. | Interpret positive or negative buffer. |
| 18 | Assess capacity consumption | Select the analysis period and explicit cycle-closing occurrence. | Reproduce the displayed numerator, denominator, and percentage, then compare it with the course recommendation. |
| 19 | Generate a multi-hour timetable | Increase repeat count while retaining the interval and code step. | Check occurrence identities and planned offsets. |
| 20 | Simulate signal failure | Select the 08:30 to 09:00 signal-failure scenario and run the same occurrence set as the baseline. | Inspect the affected traffic and blocking-time change. The historical S451 mapping remains blocked by #182. |
| 21 | Simulate rolling-stock breakdown | Target IC 1727 at 08:25, set the 40 km/h cap, and request destination termination at Utrecht. | Check that sibling repeats are unchanged and the target continues under signalling. |
| 22 | Distinguish delay effects | Freeze an incident-free baseline, run the incident scenario, and open delay comparison. | Interpret direct primary rows, propagated secondary rows, and the reconciled total arrival delay. |
| 23 | Add the extra train | Create a normal through service with the 3xPlanV composition and a 100 km/h service cap. | Experiment with departure time to limit propagated delay. This exercise remains blocked until sourced Plan V data is available. |
| 24 | Export results | Use CSV from result tables and PNG from diagrams. | Keep the input settings and selected occurrences with submitted evidence. |
| 25 | Save and distribute | Save the canonical folder, use **Save As** for `.egscene`, reopen it, and rerun representative exercises. | Work from a copy of the distributed bundle. |

## Calculation boundaries

Capacity analysis uses complete native block occupations from the selected run.
If the scene has no sourced signalling coverage or the run produced no complete
occupations, EGTRAIN reports the analysis as unavailable. It does not create
blocking times from a default signalling system.

Minimum headway, compression, critical blocks, buffer, and capacity consumption
are section-scoped results. The Gdg overtaking exercise therefore uses separate
ordered sections before and after Gdg where the train order changes. Compression
is analysis output and does not rewrite the authored timetable.

Delay comparison requires equivalent run settings and the same selected
occurrences. A positive arrival difference is primary only when the runtime
recorded direct incident evidence for that occurrence. Other positive rows are
secondary. EGTRAIN leaves the causal interpretation and railway explanation to
the student.
