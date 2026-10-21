# Simulation characterization tests

This page describes the characterization tests that pin what the simulation core does, how to read
their golden files and how to change an expectation on purpose.

## Characterization tests

The characterization tests pin what the simulation core does today, so that
refactors of movement, signalling and global state show up as a reviewable
diff. They drive the real `DispatchController` in the test process on the small
scene `EGTRAIN/QEGTRAIN/tests/fixtures/scenes/line` ("Characterization Line"):
one track of 16 km in eight blocks of 2 km, three stations, seven services and
twelve scenarios (`baseline`, `signal-failure-forward`, `signal-failure-reverse`,
`signal-failure-adjacent`, `signal-failure-staggered`, `signal-failure-last`,
`signal-failure-first`, `signal-failure-entered`, `signal-failure-late`,
`signal-failure-retarget`, `entrance-delay-f2`, `breakdown-f1`).
The stub cases add the routes `routeStubIn` and `routeStubOut` and the services
`U1` and `D1` to the scene in the test, because a route with `4-B0` or `5-B0` in
the fixture would add a movement authority to the cases with a signal failure on
those sections.

The passenger cases add station `P`, services `Q1` to `Q3` and passengers in the
test. Transfer cases also add `Platform_4` on B: its numeric label reaches the
walking time calculation, while `B.platform.1` reads as 0. Only transfer cases
have this platform, because its node adds a second timetable point named B.
Their B stops have no planned arrival. The cases set `PAX_GUI` directly with
the window off; the [command line](../guides/command-line.md) reads `-pax` only
with `-g 1`.

All railway and rolling-stock values are copied from the committed Assignment
scene. The signalling level is not part of the scene. The test sets it with one
network-wide signalling area, so one scene covers levels 0 to 5 and "none". The
border cases use two areas that meet at 8 km, the edge between `3-B0` and `4-B0`
at station B. In `border-0-2-near-fwd` and `border-0-1-near-fwd` they meet at
4 km, the edge between `1-B0` and `2-B0`.
[Signalling levels](../guides/signalling-levels.md) says what each level
does and cites these cases.

A case is a scenario, a set of services and a level. The case table is in
`tests/characterization/test_characterization.cpp`:

| Cases | Run |
| --- | --- |
| `single-train` | train `T1` alone, no signalling area |
| `follow-level-none`, `follow-level-0` to `-5` | trains `F1` and `F2` following each other |
| `sf-forward-level-none`, `-0` to `-5` | the same trains with a signal failure from 400 s to 1000 s |
| `sf-reverse-level-none`, `-0` to `-5` | trains `R1` and `R2` in the opposite direction, same failure window |
| `sf-adjacent-level-none`, `-0` to `-2` | trains `F1` and `F2` with failures on the adjacent sections `4-B0` and `5-B0` from 400 s to 1000 s |
| `sf-staggered-level-none`, `-0` to `-2` | the same trains and sections, with the failure on `4-B0` ending at 700 s and the one on `5-B0` at 1000 s |
| `sf-last-level-none`, `-0` to `-2` | the same trains with a failure on `7-B0`, the last section of the route, from 400 s to 1000 s |
| `sf-first-level-none`, `-0` to `-2` | the same trains with a failure on `0-B0`, the first section of the route, from 0 s to 400 s; both trains wait to enter until the failure has ended, at every level; without a signalling level they enter together at 402 s, as nothing separates trains there |
| `sf-entered-level-none` | the same trains with a failure on `0-B0` from 90 s to 400 s, without a signalling level; `F1` has entered by then and runs on, `F2` waits to enter until the failure has ended |
| `same-entry-level-none`, `-0` to `-5` | trains `T1` and `F1`, both due at 60 s at the entry of the same route, without an incident; without a signalling level both enter at 60 s, as nothing separates trains there; at levels 0 to 5 `F1` enters after `T1` has moved on |
| `entry-order-level-0` to `-2` | trains `F2` and `L1` on the `sf-first` scenario; `F2` is listed before `L1` in the scene but `L1` is due first, so `L1` enters first when the failure ends |
| `sf-late-level-none`, `-0`, `-3` | train `F1` alone with a failure on `2-B0` from 188 s to 600 s; at 188 s `F1` is 316 m before the failed section at top speed, closer than its braking distance of 767 m, so it brakes with full force and its position never goes back; it passes the authority and runs through the failed section |
| `sf-retarget-level-0` | train `F1` alone with a failure on `6-B0` from 600 s and one on `5-B0` from 752 s to 1200 s; `F1` brakes to 11.1 m/s at the end of `4-B0` for the first failure, and 23 m before that point the second failure makes the end of `4-B0` a stop target, which `F1` cannot reach; it brakes with full force, its position never goes back, it passes the authority and stops at the end of `5-B0` for the failure on `6-B0` |
| `late-leader-level-3`, `-4` | trains `L1` and `F2`; `L1` is `F1` with a dwell of 100 s at C, so `F2` is held behind it there |
| `single-track-level-none`, `-0` to `-5` | train `S1` from A to B and `R1` from C to A, with a single-track restriction from `1-B0` to `4-B0`, protected by `0-B0` and `5-B0`; with a signalling level `R1` waits in front of the section while `S1` is in it |
| `single-track-follow-level-3`, `-4` | trains `F1` and `F2` in the same direction through the restricted section; the output equals `follow-level-3` and `-4`, because the restriction does not delay a train that follows the holder |
| `stub-departure-first-level-0` | trains `D1` (due at 60 s) and `U1` (due at 160 s) on a stub track, the closed end of the line from station B to station C, with a single-track restriction over `5-B0` to `7-B0`, protected by `4-B0` and `7-B0`; `D1` runs out of the stub and `U1` into it, and `U1` waits at the entry of its route until `D1` has left the stub |
| `stub-arrival-first-level-0` | the same stub with `U1` due at 60 s and `D1` due at 160 s; `D1` waits at the entry of its route until `U1` has left the stub |
| `stub-tie-level-0` to `-5` | the same stub with both trains due at 60 s; the forward route of `U1` reserves the free stub at 59 s, and `D1` waits outside until `U1` has left; both reach their last stop |
| `stub-zero-level-0` to `-5` | the same stub with both trains due at step zero; step zero initializes trajectories and reserves before the first movement; both finish without overlap |
| `running-tie-level-0` to `-5` | S1 runs from A to B and a copy of R1 runs from C to B on existing blocks 7 to 4; both are moving and requesting when the free 4000 to 12000 m stretch is first reserved, and both finish without opposing overlap |
| `running-forward-first-level-0` to `-5`, `running-reverse-first-level-0` to `-5` | the same approaches with one entry 3 s later and capped at the existing 40 km/h approach speed; both are moving at different speeds, giving each direction a turn at priority |
| `running-follower-level-0` to `-5` | F2 follows S1 to B while R1 waits; all finish without opposing zone overlap |
| `stub-breakdown-level-0`, `-4` | the forward owner is held at its origin by a breakdown from 60 to 120 s; its reservation survives, the opposing train waits, and both finish |
| `running-through-level-0` to `-5` | the original full-route F1 and R1 on the line without a passing loop; no opposing zone overlap, but both cannot finish because the waiting loser occupies the winner's exit track |
| `border-0-2-fwd`, `border-0-2-rev` | level 0 from A to 8 km and level 2 from 8 km to C; `F1` and `F2` run from A to C, `R1` and `R2` from C to A, so a `rev` case enters on the C side and `border-0-2-rev` mirrors `border-2-0-fwd` |
| `border-2-0-fwd`, `border-2-0-rev` | the same trains with level 2 from A to 8 km and level 0 from 8 km to C |
| `border-0-3-fwd`, `border-0-3-rev` | the same trains with level 0 from A to 8 km and level 3 from 8 km to C |
| `border-0-1-fwd` | trains `F1` and `F2` with level 0 from A to 8 km and level 1 from 8 km to C |
| `border-0-2-near-fwd`, `border-0-1-near-fwd` | trains `F1` and `F2` with level 0 from A to 4 km and level 2 or level 1 from 4 km to C, so that the head of a train in `2-B0` has the level 0 sections `1-B0` and `0-B0` behind it |
| `pax-board-gui0`, `pax-board-gui1`, `pax-full-gui0`, `pax-full-gui1` | `Q1` boards passengers at P, including ties and an entry during the stop; a later entry stays at P. Full cases cut ties with three seats at P and B; alighting at B frees seats before boarding |
| `pax-transfer-gui0`, `pax-transfer-gui1` | `Q1` connects to `Q2` on the same platform and to `Q3` on another label; a passenger also starts a second journey on `Q3` |
| `entrance-delay-f2-level-0` | trains `F1` and `F2` with an entrance delay of 120 s for `F2` at A, its first stop; a train is not held at the stop where it begins its route, so every simulated time equals `follow-level-0` and only the planned departure of `F2` at A moves, from 180 s to 300 s |
| `breakdown-f1-level-0` | the same trains with a breakdown of `F1` from 200 s that caps its speed at 40 km/h until the end of its route; `F1` reaches C late and `F2`, held behind it, has not reached C when the run ends at 1500 s |

Every case runs in CTest. They are listed in
`tests/characterization/CMakeLists.txt`, in the order of the table, and each
has a golden file. Any case can also be run by hand with `--case`. Each case is
its own CTest entry and process, labelled `characterization` and `unit`:

```bash
cmake --build build --target test_characterization test_crossover_chain test_signalling_aspects
ctest --test-dir build -L characterization --output-on-failure
```

`characterization_repeatable` runs several cases in one process and then the
first one again. The two runs of that case must be identical, with no
tolerance. It fails when state from an earlier run leaks into a later one.
The same executable can run any scene directories back to back:

```bash
build/EGTRAIN/QEGTRAIN/tests/characterization/test_characterization \
    --repeat EGTRAIN/QEGTRAIN/Scenes/Paimpol EGTRAIN/QEGTRAIN/Scenes/Assignment_Gvc_Gdg_Ut \
    EGTRAIN/QEGTRAIN/Scenes/Paimpol
```

A step is a scene directory, optionally followed by `#` and a case name. Without
a case name the scene runs as committed.

Three more entries run committed scenes this way: `characterization_repeat_paimpol`
(Paimpol twice, then Assignment, then Paimpol again),
`characterization_repeat_milano_paimpol` (Milano-Brescia and Paimpol, alternating)
and `characterization_repeat_netherlands` (Netherlands, Paimpol, Netherlands, labelled
`slow`). They fail when a run leaves state behind that changes a later run of the
same scene, which is what a change to the arrays of the run can cause.

These entries compare the observation of each run: what the test reads from
memory, and the two station statistics files. The other files of a run are not
read. `--repeat-files` runs the steps in the same way, but every run writes its
files into a folder of its own (`run-01`, `run-02` and so on, in a temporary
folder), and after the last run every distinct step runs once more in a fresh
process of the same executable, with the same working directory and
environment. Every file of every in-process run must then be equal, byte for
byte, to the file of the fresh run of its step, and both must hold the same set
of files. The files are the ones a headless run of the application writes with
`--detailed-trajectories`, including `TrainServicePathDiagram.txt`, which the
test writes after the run as the application does.
`TrainTrajectories/Computing_Times.txt` is the only file
whose content is not compared. It has to exist, but it holds wall-clock timings
and the computation time accumulated over the process, so it differs from run to
run. As with `--repeat`, a step has to appear twice. A difference is reported
with the run, the step, the file and the first differing line, and the folders
of a failed test are kept; the message names them. The fresh run is
`--single STEP --output-dir DIR`, which can also be used by hand. Use a new or
empty folder, because a run replaces only the files it writes.

```bash
build/EGTRAIN/QEGTRAIN/tests/characterization/test_characterization \
    --repeat-files EGTRAIN/QEGTRAIN/Scenes/Paimpol EGTRAIN/QEGTRAIN/Scenes/Paimpol
```

Three entries run this way. `characterization_repeat_files_paimpol` (labelled
`slow`) and `characterization_repeat_files_milano_paimpol` run the sequences of
`characterization_repeat_paimpol` and `characterization_repeat_milano_paimpol`.
`characterization_repeat_files_fixture` runs `sf-forward-level-3` twice,
`single-track-level-4`, and `sf-forward-level-3` again on the line fixture,
which gives an incident and a single-track restriction that no committed scene
has. They fail when a run in a process writes a file that differs from the file
of a fresh process, which is what state left behind by an earlier run can cause
in a file that the entries above never read.

`characterization_aspects_dutch` (labelled `slow`) runs Netherlands and
Assignment once each with `--aspects` and fails unless at least one route
section leaves the clear code 270 and returns to it during the run.

`test_crossover_chain` is a separate executable, because the harness above is
tied to the line fixture (its length, its block size and the station at 8 km).
It runs the scene `tests/fixtures/scenes/crossovers`: a chain of five tracks
with two crossovers that follow each other directly, so that the second half of
the first crossover and the first half of the second one share one long block.
The rolling stock and the timetable are copied from the line fixture. Platform
B lies inside the second crossover, and `F1` reaches it long before its planned
departure, so `F1` stands in the crossover while `F2` arrives. The geometry is
synthetic. For each of the levels 0, 1, 2 and 5, one CTest entry
(`characterization_crossover_chain_level_0`, `-1`, `-2`, `-5`) sets a
network-wide area of that level and runs train `F1` alone, train `R1` alone in
the opposite direction, and `F1` with `F2` behind it. A train alone has to reach
its last platform and stand only at platforms. The follower has to reach it too,
stay behind the rear of `F1`, and not enter the second crossover while `F1` is
in it. At levels 0, 1 and 2 it also has to wait in the second half of the first
crossover meanwhile; at level 5 it waits further back, in front of the first
crossover, and that is not checked. The levels 3 and 4 are not run, because a
train alone does not stop in front of the second crossover there. The test reads
no golden file. Each level also runs `R1` on a route marked `reversed` with
its sections in reverse order, with the same expectations, and refuses `F1` on
a route marked `reversed` with its sections in forward order with
`scene.route.direction`.

`test_signalling_aspects` is a third executable, because it needs no scene and
no Qt. It builds routes of up to seven sections by hand in the route list of
the run and calls the release and the activation of the mixed signalling
(`releaseMixedSignallingSystem` and `activateMixedSignallingSystem`, in the
order of a step) and the release of the last section
(`relLastSectionMixedSignalling`). After a step it writes the code, state, signal
speed limit, speed in braking and exit speed of every section: one line per
case, with the group, the parameters, a `|` and one token
`code:state:signal:braking:exit` per section. The header of the golden file
`tests/characterization/expected/signalling-aspects.txt` explains the fields.
The groups are uniform levels, two levels with a border, three or more levels,
sweeps of a moving train that carry the state from step to step, the release
functions, other signal speeds and a held single-track zone. The cases are in
the file, and the comparison is exact text, without a tolerance. The speed
limits of the sections are numbers of the test, not railway data. The test also
checks, without the golden file, the aspects of one step for every level, the
release of one connected section on two routes of different lengths, the level
mask of a route, and that a step leaves the lists of occupied and connected
sections as they were. A change of the aspect routines that is not
meant to change behaviour has to leave the file as it is. The file has no
`# case:` line and no `# known-wrong:` line, because it is not a case of the
table. It is recorded with `EGTRAIN_UPDATE_EXPECTATIONS=1`, like the others. The
command in "Changing an expectation deliberately" that runs every
characterization test records it too; this command records it alone:

```bash
EGTRAIN_UPDATE_EXPECTATIONS=1 build/EGTRAIN/QEGTRAIN/tests/characterization/test_signalling_aspects \
    --expect EGTRAIN/QEGTRAIN/tests/characterization/expected/signalling-aspects.txt
```

The test does not call diverging switches, double switches, station boundaries,
signal failures or the authorities of levels 3 and 4. Its CTest entry is
`characterization_signalling_aspects`, labelled `characterization` and `unit`.

### Reading a golden file

Golden files are in `tests/characterization/expected/<case>.txt`. Lines starting
with `#` are comments. The header names the case and the run. Every other line is
one fact: some key words, then `name=value` fields.

- `train <id> sample`: time `t` (s), route position `x` (m) and speed `v` (m/s),
  every 60 s from the first to the last step the train is in the network.
- `train <id> stop`: a stretch of at least 5 s below 0.001 m/s, with its first and
  last step, position and `dwell` (last minus first step).
- `train <id> braking`: the last step at the speed before a stop that is
  followed by a continuous speed decrease down to that stop.
- `separation <lead>><follower>`: the smallest distance between the rear of the
  leading train and the head of the one behind it, and when. Negative means
  overlap.
- `aspect <route> <section>`: the signal aspect code of the section as `step:code`
  at every change, starting at step 0 (270 clear, 180, 75, 0 stop, 751 BACC).
- `result`, `run_result`, `network`: timetable and run results as the application
  reports them. `n/a` means the value is not available.
- `blocktime`: the blocking-time records of a train.
- `capacity_pair <leader> <follower>` and `capacity_resource <section>`: what
  `analyzeCapacity` and `analyzeAreaCapacity` report for the two trains of a
  case, with all sections of their route as the area and a period of 3600 s,
  the default of the capacity dialog. Only the cases `follow`, `sf-forward`,
  `sf-reverse`, `sf-adjacent`, `sf-staggered`, `sf-last` and `sf-first` at the
  levels 0 and 2 have them, and only when both trains have complete blocking
  times; `F2` is still in `7-B0` when `sf-adjacent-level-0` ends, so that case
  has none. The pair line gives `min_headway` (s) with the sections that govern
  it, `cycle` (s) and `cycle_pct` (percent of the period) from the first train
  to the second after compression, whether the compressed trains overlap
  (`conflict_free`), and the sections where they touch (`critical`). Each
  resource line gives the number of trains and occupations of a section, the sum
  of the occupation times (`occupation`, s), their union (`union`, s),
  `occupation_pct`, and `governing=1` for the section with the largest sum. The
  values cover these two trains only, not the capacity of the line.
- `incident`, `boundary`, `authority`: the incident definition, and for the steps
  just before and after its start and end, how many signal-failure authorities
  exist and which sections are blocked.
- `stats`: the rows of `TrainTrajectories/Stats_Stations.txt`, which holds six
  significant digits. An early arrival counts there as a delay of 0 in the
  total and in `N_StopTrains`. The average and deviation cover the delayed
  trains only.
- `signed_stats`: the same rows of `TrainTrajectories/Pos&Neg_Stats_Stations.txt`,
  where early arrivals are negative delays.
- `delay_summary <arrival|departure> <station|network>` and `delay_input <train>`:
  what `summarizeDelays` reports for the timetable results of the run and the
  entrance delay of each train, with the thresholds 0, 60, 180 and 300 s (a call
  is later only when it is strictly later). A row counts its calls by status
  and by early, on time and late, and gives sum, mean, minimum and maximum over
  the calls that have a delay; `n/a` means no call has one, never 0. Only the
  cases `entrance-delay-f2-level-0` and `breakdown-f1-level-0` have them. A
  `delay-pair-<incident case>.txt` file comes from `test_characterization
  --pair <baseline case> <incident case>`, which runs both cases in one process.
  It holds the same lines for both runs, with `baseline` or `incident` after the
  key, and the lines of `compareAgainstBaseline`: `delay_compare status`, one
  `delay_compare diagnostic` for every reason a baseline does not fit, a
  `delay_compare occurrence` line per train (entrance delay, direct evidence,
  final call, additional delay and `secondary`), a `delay_compare call` line per
  call and a `delay_compare total`. Its CTest entry is
  `characterization_delay_pair_<incident case>`.
- `pax <id> state`: changes in network membership, status, active journey and trip,
  waiting station and platform, awaited and boarded trains, and arrival station.
- `gui_platform <station> <platform>`: changes in the ordered passenger IDs sent
  to the window; `-` means empty.
- `gui_passenger <id>`: changes in status, waiting platform, next train and next
  destination sent to the window.
- `onboard <train>`: changes in onboard count and capacity sent to the window.
- `journey <passenger> <journey>` and `trip <passenger> <trip>`: final start and
  completion flags, actual times and arrival delay, plus walking and waiting
  times for journeys. Passenger times are absolute seconds of the day; trace
  `t` values are steps of the run.
- `file PassengerStatus.txt` and `file JourneyDelays.txt`: every line of the two
  passenger files, as whitespace separated tokens.

Passenger traces store the first snapshot and print only later changes, in step
order. Passenger windows have equal ends, so no golden depends on the seed.

Transfers to a platform with another label never complete: the passenger stays
on the arrival platform. Waiting time is accumulated for the first journey
only. With `PAX_GUI` off the persistent platform lists stay empty. The snapshot
part for passengers who just left never matches when the base time is at or
above the horizon. A station with platforms on two nodes of one route has one
timetable point per node; result rows and statistics for a stop at that station
read the first point. See [Delay analysis](../guides/delay-analysis.md#limits).

Both station statistics files take the arrival of a stop from its timetable
point, the arrival that `TimetablePoints.txt`, the timetable results, the
diagrams and the CSV export report, so a `result` line and a station row show
the same delay. A train that did not reach a stop, and a stop without a planned
arrival, have no delay and are counted in neither file. A stop with no timetable
point keeps the arrival recorded during the run. A station row without such a
train holds `-1` in the average, deviation, maximum, cumulative and percentage
columns and 0 in `Total_Delay` and the train counts. The `Final_Station` row is
the same statistic over the last stop of each train whose last stop is counted;
when no last stop is counted it holds the values of a row without a counted train. The first station of the network has no row and
is not in `TOTALS`; its calls can still be the last stop of a train and so enter
`Final_Station`. See [Delay analysis](../guides/delay-analysis.md#the-station-statistics-files).

Integers and strings must match exactly. Floats match within an absolute
tolerance of 1e-4 and a relative tolerance of 1e-9, and the statistics rows
within 1e-5 relative. A failure prints the first 20 differing lines with the
expected and the actual line, and writes the actual output to
`<case>.actual.txt` in the test's build directory.

Each run also checks facts that do not come from the golden file, so a
re-recorded golden cannot hide them: speed never above the 36.11 m/s limit, no
backward movement, acceleration and braking within what the rolling stock can do,
no faster run to station B than the top speed allows (in a stub case only for a
train that starts before B), planned dwell and departure times kept, stops only at a platform, a
block boundary or behind another train, no overlap of two trains, and in every
station row of `stats` and `signed_stats` a `Total_Delay` and an `N_StopTrains`
that equal the sum and the number of the arrival delays of its `result` lines
(only the late ones in the sum for `stats`). In a `single-track-*` case `S1` and
`R1` are never inside the restricted section at the same time. In a stub case
both trains reach their last stop and are never inside the stub together. The
terminating running approaches also must all reach their last stop, and every
running case checks opposing zone overlap at every shared step. A case
without a signalling area is not checked for overlap or for the single-track
section: a scene without a signalling level does not separate trains, and
validation warns about it (`scene.signalling.level.missing`,
`scene.single_track.no_effect`).
The `-level-none` goldens therefore show trains at one position.

The signal states that the simulation hands to the window are checked at every
step of every case against the section codes of all routes. There must be one
entry per section and direction. Its code is the most restrictive code of the
route copies of the section (0, 751, 75, 180, 270 in that order). Its level is
the signalling level of the section, or none. Its failed flag is set exactly
from the start to the end of a signal failure.

### Changing an expectation deliberately

Run the case with the update switch, then review the diff before committing:

```bash
EGTRAIN_UPDATE_EXPECTATIONS=1 ctest --test-dir build -L characterization
git diff EGTRAIN/QEGTRAIN/tests/characterization/expected
```

To run one case directly, call the executable:

```bash
EGTRAIN_UPDATE_EXPECTATIONS=1 build/EGTRAIN/QEGTRAIN/tests/characterization/test_characterization \
    --fixture EGTRAIN/QEGTRAIN/tests/fixtures/scenes/line \
    --expect EGTRAIN/QEGTRAIN/tests/characterization/expected --case follow-level-3
```

The switch rewrites the golden file and passes. It is refused when the `CI`
environment variable is set, and it writes nothing when a check above fails.
Read the whole diff and check that every changed number is a consequence of the
change you made and still physically plausible. Commit the new golden file in
the same commit as the behaviour change and give the reason in the commit
message. Do not edit the scene fixture to make a case pass. Changing it changes
every golden file, so add a service or scenario instead.

### Known-wrong behaviour

Behaviour that is known to be wrong can be pinned and marked. The golden file
still pins it, so a refactor cannot change it unnoticed. The golden header
carries `# known-wrong: #<issue> <reason>` and the case table carries the same
text. The test fails when the two differ and prints `KNOWN-WRONG #<issue>` on
every run. A marked case is exempt from the checks on stops, separation and
timetable. The change that fixes the bug updates the golden file and removes the
marker in one commit. The markers are in the `kKnownWrong` table of
`test_characterization.cpp`. List the marked cases with:

```bash
grep -rn "known-wrong: #" EGTRAIN/QEGTRAIN/tests/characterization/expected
```

No case carries a marker. The table is empty, every golden header says
`# known-wrong: none`, the grep lists nothing and the checks apply to every case.
