# How capacity is analysed

## What this guide covers

This guide states what the Capacity analysis window computes: its inputs, its formulas and their units. It also states what the window does not compute, and what the area calculation of older versions did. It describes the program as it is, including its limits.

| Place | Where to find it |
| --- | --- |
| The window | Diagrams > Capacity analysis... and Run Results > Open result view > Capacity. Both need a completed run. A chooser titled "Capacity analysis" asks for Route / corridor, First block, Last block, Analysis period, Cycle-closing occurrence and the checked occurrences. The result window then opens with the header "Cycle: X s  \|  Y% of period" (one decimal) and the tabs Pairs, Compression and Critical blocks. Each tab title ends with its row count |
| The CSV | The button **Export capacity CSV...** asks for a path and suggests the name `capacity_analysis.csv`. It writes a provenance file beside the CSV, named like the CSV with `.provenance.json` added |
| The compressed diagram | The button **Open compressed blocking-time diagram**. The diagram window has its own **Export CSV...** (suggested name `capacity_compressed_blocking_time.csv`) and **Export PNG...** |
| The input | The occupations: Diagrams > Blocking-time overlay... and Run Results > Open result view > Blocking time, and `BlockingTimes.txt` in the `TrainTrajectories` folder of the run output |

The program computes no capacity of an area, a line or a network. The percentage in the header belongs to the chain of occurrences that the user selected. The area calculation of older versions is not in the program ([The area calculation the program no longer contains](#the-area-calculation-the-program-no-longer-contains)).

The [Assignment workflow](../product/assignment-workflow.md) uses this window in steps 14 to 18. The [signalling levels page](../architecture/signalling-levels.md#signalling-areas-block-sections-and-capacity-areas) has a section that separates signalling areas, block sections and the scope of this window.

## Terms and units

All times are seconds on the time axis of the run. One simulation step is one second, as the [delay guide](delay-analysis.md#units-and-the-reference-timetable) shows.

| Term | Meaning in the program |
| --- | --- |
| Occupation | The blocking time of one train on one block, from `StartOccTime` to `EndOccTime`. It starts at the start of the approach minus the setup time and the sight reaction time. It ends at the end of clearing plus the release time and the run-time margin. The blocking-time paragraph of [Using EGTRAIN and authoring V1 scenes](scenes-and-application.md#timetable-graphs-and-train-paths) says the same. [Inputs](#inputs) gives the values |
| Resource | A block. Two occupations share a resource when a component of their block ids is equal. An id is split at "@" and its first and third non-empty parts are compared. An id without "@" is compared whole. An id has no direction of travel (`shareBlockingTimeResource`) |
| Scope | One route of the run, a first and a last block chosen from the blocks of that route, in either order, and every block between them. The first selected block is the one that comes first in the route, whichever of the two lists it was chosen in. The corridor name of a route is a label only. There is no area, corridor or network scope |
| Occurrence | A train of the run, named by its runtime id. It is listed when it has a valid occupation on the first selected block with a usable `StartRunTime`, on any route ([step 1](#the-calculation)). An entry of the list reads "runtime id \| operating code \| reference source" |
| Order | The order of the checked rows of the list. The user can drag rows. The first order is the order in which the run holds its trains, not the order of time. The program never sorts |
| Period | A number of seconds entered in the chooser: default 3600, minimum 0.001, step 60. It is not read from the scene or the run, and it is not checked against the length of the run. There is no selection of a timetable window: every valid occupation of a selected train on the selected blocks is used |
| Profile reference | The `StartRunTime` of the train on the first selected block, in seconds. The result window calls it "Original reference" |
| Scheduled reference | A time that stands for the timetable, in seconds. [Step 2](#the-calculation) gives the three ways to get it |

## Inputs

1. Occupations exist only for sections of level 0, 2, 3 and 4 (`ComputeBlockingTimesInMixedSignallingAreas`). A section of level 1 or 5, or without a level, gives none. A scene gives input only for the sections that a signalling area of level 0, 2, 3 or 4 contains. [The committed scenes](../architecture/signalling-levels.md#the-committed-scenes) lists the areas of each committed scene. At levels 3 and 4 the run builds occupations per infrastructure element, and the line fixture of the characterization tests lists none at those levels; [What is not established](../architecture/signalling-levels.md#what-is-not-established) says so.
2. An occupation is used only when it is complete, has a block id, has finite times and positions, starts at 0 or later, ends after it starts and has positions of 0 or more (`validBlockingTimeDiagramInput`). The window leaves other records out without a message.
3. The setup time, the sight reaction time and the release time are not inputs of the window. The run passes fixed values and two settings of the case to the occupation routines (the call of `ComputeBlockingTimesInMixedSignallingForAllTrains` in `app/DispatchController.cpp`): a setup time of 5 s, a release time of 3 s plus the buffer time, a sight reaction time of 0.5 s, a safety margin of 50 that the routines of levels 0 and 2 do not take, an absolute running-time supplement of 0 and the recovery percentage. For a section of level 0 or 2, `ComputeBlockingTimeForSingleLocation` sets:
   - the start of the occupation to the start of the approach minus the setup time and the sight reaction time. For the first block of the route the approach starts at the start run time and both times are 0;
   - the end of the occupation to the end of clearing plus the release time. For the last block of the route the clearing ends when the head of the train leaves the block;
   - when the recovery percentage is not 0, a run-time margin that moves the end later: the run time of the block x the percentage / 100. With a percentage of 0 there is no margin and the routine writes a warning to the log.

   The case setting **Buffer (s)** (`buffer_time_seconds`, rounded to a whole number of seconds) is the buffer time of the release time. It is not the Buffer [s] column of the result. The case setting **Recovery (%)** (`recovery_time_percent`, rounded to a whole number) is the recovery percentage. The command-line options `-b` and `-c` replace the two settings ([command-line options](command-line.md)). The routines of levels 3 and 4 are separate functions, and this guide gives no formula for them.
4. `BlockingTimes.txt` holds, for each train, its name and then five lines: the block ids, `GeoPosStart`, `GeoPosEnd`, `StartOccTime` and `EndOccTime` of its first N records. N is the number of blocking times that the run counted as complete (`PrintTrainBlockingTimes`). The window tests the complete flag of each record instead, so the two differ only when an incomplete record comes before a complete one. The file has no `StartRunTime`, so the analysis cannot be redone from the file.
5. Not inputs: a stored analysis area, a timetable window and a description of the other traffic.

## The calculation

The steps follow `analyzeCapacity` and the helpers of `app/MainWindow.cpp`. Times are in seconds.

1. **Candidates.** `capacityTrainsForScope` offers every train of the run whose first valid occupation that shares a resource with the first selected block has a finite `StartRunTime` of 0 or more. That record gives the profile reference of step 2. A train without such a record is not offered. The occupations of an offered train are all its valid occupations that share a resource with a selected block. The direction of the train is not examined.
2. **References.** The profile reference is the `StartRunTime` of that record. The scheduled reference is, in this order:
   1. the authored time of the first stop, in stop order, that has one and whose route position is within 5 m of the start of that record: its departure, or its arrival when it has no departure (reference source "authored ScheduledDeparture at boundary station" or "authored ScheduledArrival at boundary station (departure absent)");
   2. the scheduled entry time of the train plus (`StartRunTime` minus `RunStartTime`) (source "canonical scheduled entry + elapsed from RunStartTime"). `RunStartTime` is the second before the train first moves;
   3. the profile reference (source "profile reference fallback (scheduled entry unavailable)").

   The result window and the CSV name the label and the source that applied ("Reference label/source").
3. **Minimum headway of an ordered pair.** The leader is earlier in the order than the follower. Every pair of valid occupations, one of each train, that share a resource gives a candidate: (leader `EndOccTime` - leader profile reference) - (follower `StartOccTime` - follower profile reference). The minimum headway is the largest candidate, but not below 0. The resources whose candidate is within 1e-7 s of the largest are the governing resources. A pair with no shared resource has no minimum headway.
4. **Scheduled headway and buffer.** Scheduled headway = follower scheduled reference - leader scheduled reference. Buffer = scheduled headway - minimum headway. It is not clamped: a negative value means that the scheduled references are closer together than the minimum headway. The Pairs tab lists the adjacent pairs of the order. The CSV lists every earlier/later pair.
5. **Compression.** The first train keeps its scheduled reference as its compressed reference. Each later train gets the largest of two values: the compressed reference of the train just before it in the order, and, for each earlier train that shares a resource with it, that train's compressed reference plus the minimum headway of the pair. The scheduled reference of a later train does not enter. It only gives the Shift column (compressed reference minus scheduled reference). The occupations of a train are moved by (compressed reference - profile reference). Only the occupations move, not the trajectories. The earlier trains whose compressed reference plus minimum headway equals the compressed reference of the follower (within 1e-7 s) are its governing predecessors.
6. **Critical blocks.** After compression, an occupation of a follower and an occupation of one of its governing predecessors that share a resource and touch are marked as critical (legend key "Capacity critical block (touching)"). They touch when the start of the follower minus the end of the predecessor is within 1e-7 s of 0. A touch is not an overlap. An overlap is an intersection of occupations of two trains on a shared resource longer than 1e-7 s. It is the conflict that the blocking-time chart draws in red (legend key "Key: Conflict"; `buildBlockingTimeDiagramSegments`).
7. **Cycle.** Cycle time = compressed reference of the cycle-closing occurrence - compressed reference of the first train. Cycle percentage = cycle time / period x 100. There is no upper bound. The user picks the closing occurrence in the chooser; it must be a checked row after the first. The program does not infer it.
8. **When a result exists.** A result is shown only when it is analyzable: at least two trains, a valid profile reference for each (finite and 0 or more), a shared resource for every adjacent pair of the order, and no overlap among the compressed occupations of any two trains on a shared resource.

## A worked example

The figures come from hand arithmetic on the `blocktime` lines of `tests/characterization/expected/follow-level-0.txt` and the planned departures of the first stop in `tests/fixtures/scenes/line/services.json`. Two trains, train 1 and train 2, run over the eight blocks of the line fixture at level 0. The scope is all eight blocks, the order is train 1 then train 2, the period is 3600 s and the closing occurrence is train 2. The characterization test does not run this analysis, so the figures are arithmetic by hand and not an output of the program.

The first stop of each train is at the start of the first block (the station position and the first node of the track are both at 0 km), so the scheduled references follow rule 1 of step 2: the planned departures 120 s and 180 s. The profile references are the `StartRunTime` on `@0-B0@`: 59 s and 199 s. Each occupation in the lines ends 3 s after its clearing ends. Train 1 occupies the second block from 53.5 s, which is the start run time of the first block, 59 s, minus the setup time 5 s and the sight reaction time 0.5 s.

| Block | Train 1 `EndOccTime` | Train 1 end - 59 | Train 2 `StartOccTime` | Train 2 start - 199 | Candidate headway |
| --- | ---: | ---: | ---: | ---: | ---: |
| `@0-B0@` | 146 | 87 | 199 | 0 | 87 |
| `@1-B0@` | 201 | 142 | 193.5 | -5.5 | 147.5 |
| `@2-B0@` | 257 | 198 | 275.5 | 76.5 | 121.5 |
| `@3-B0@` | 674 | 615 | 342.5 | 143.5 | 471.5 |
| `@4-B0@` | 748 | 689 | 666.5 | 467.5 | 221.5 |
| `@5-B0@` | 803 | 744 | 872.5 | 673.5 | 70.5 |
| `@6-B0@` | 859 | 800 | 955.5 | 756.5 | 43.5 |
| `@7-B0@` | 983 | 924 | 1010.5 | 811.5 | 112.5 |

- Minimum headway: 471.5 s, governed by `@3-B0@`, the fourth block.
- Scheduled headway: 180 - 120 = 60 s. Buffer: 60 - 471.5 = -411.5 s.
- Compressed references: train 1 keeps 120 s. Train 2 gets the larger of 120 and 120 + 471.5, which is 591.5 s. The Shift column reads 0 and 411.5 s.
- Occupation shifts: train 1 by 120 - 59 = 61 s, train 2 by 591.5 - 199 = 392.5 s.
- Gap at `@3-B0@` after compression: train 1 ends at 674 + 61 = 735 s and train 2 starts at 342.5 + 392.5 = 735 s. The gap is 0, so this is the one critical touch. The gaps on the other seven blocks are 384.5, 324, 350, 250, 401, 428 and 359 s, so nothing overlaps.
- Cycle time: 591.5 - 120 = 471.5 s. Cycle percentage: 471.5 / 3600 x 100 = 13.097 %. The header would show 13.1.

The unit test `tests/test_capacityanalysis.cpp` has a second example with four trains A to D. By hand, the minimum headways of the four pairs that share a block (A with B, A with C, B with C, C with D) are 20, 100, 5 and 10 s. The test asserts the compressed references 20, 100 and 110 s of B, C and D, and that A, not B, governs C. With C as the closing occurrence it asserts a cycle of 100 s, which is 50 % of a period of 200 s.

## Special cases

| Case | What the program does |
| --- | --- |
| No completed run | The Diagrams entry is disabled, with the hint "Run a simulation to enable this diagram", and so is the button Open result view. If the function is reached anyway, the status bar shows "Run a simulation to open capacity analysis" |
| No block of any route with two trains that have a usable occupation on it | An information box: "No retained native blocking-time section has two occurrences with a common entry block. Run a scene with explicit signalling data; EGTRAIN does not infer a signalling system or blocking-time parameters." |
| Fewer than two occurrences for the chosen section, an empty list or fewer than two checked rows | The list is empty when no train occupies the block range or the route has no blocks. **OK** of the chooser shows "Select at least two ordered occurrences with a common entry block." and the chooser stays open. The code after the chooser repeats the check with two boxes, "No route section has two occurrences with a common entry block. ..." and "Fewer than two occurrences share the selected section entry. ...". The chooser accepts only two or more checked rows that it listed for the same scope, so neither box follows an accepted chooser |
| A closing occurrence that is not a checked row after the first | **OK** of the chooser shows "Choose a checked cycle-closing occurrence after the first row, normally the first train in the next period." and the chooser stays open. The first entry of the combo box, "Select the first train in the next period...", is not an occurrence. The code after the chooser repeats the check with the box "The cycle-closing occurrence must be a selected row after the first occurrence." |
| Incomplete occupations | Left out without a message. A train without a valid occupation on the first selected block is not listed. A train that lacks occupations on other selected blocks is listed. Its pairs use the resources it has, and nothing reports the missing ones |
| A repeated block | A train with two occupations that share a resource: both are used, every pair of occupations is a candidate, the largest governs, and both move by the same shift |
| Opposing trains | There is no check of direction. A train in the opposite direction whose records carry the same block ids is listed and, when checked, enters the pair and compression calculation like a following train. In `tests/characterization/expected/sf-reverse-level-0.txt` the trains run the line in the opposite direction and their records carry the ids `@7-B0@` to `@0-B0@`. These are the ids that the trains of `follow-level-0.txt` carry, in reverse order |
| A period of zero, negative or not finite | The chooser accepts 0.001 s and more, so the window cannot start with such a period. `analyzeCapacity` reports a period that is negative or not finite as unavailable (-1) and keeps a period of 0. In both cases it gives no percentage |
| No shared resource for an adjacent pair, or an overlap after compression | The box "The selected occurrences do not form a valid shared-resource chain. ..." appears, followed by a hint to choose a common entry or to split the order into separate sections. There is no result window and no CSV |
| A percentage above 100 | Shown as computed |

The CSV is written only for an analyzable result. The window keeps the result it was opened with.

## Signalling areas, scope and the legacy area

The [section of the signalling levels page](../architecture/signalling-levels.md#signalling-areas-block-sections-and-capacity-areas) on areas and block sections already separates signalling areas, block sections and the scope of this window. Its table is not repeated here. The table below adds the area of the removed program.

|  | Signalling area (scene) | Scope of the Capacity analysis window | Area of the removed program |
| --- | --- | --- | --- |
| What it is | `id`, start km, end km, level 0 to 5 and an optional track | One route and a block range, chosen for one analysis | One row of `TrackLines/AreasCaseStudy.txt`: name, start, end, signalling level and an optional track line number |
| Where it is stored | `signalling.json` | Not stored | The case input folder |
| When a block belongs to it | The section lies wholly inside the range, with a coordinate tolerance of 1e-8 km (`sectionSpanInsideArea`) | An occupation belongs when its block id shares a resource with a block of the range | A block or blocking time belongs when at least one end lies inside the closed range from start x 1000 to end x 1000 metres. A block that contains the whole range is not in. A block that touches an edge is in, so two areas that meet at a block edge both hold that block |
| Whether it gives a level | Yes | No | Yes: a network-wide row only where none was set, a row for a track line overrides |
| Whether it gives a result | No | Yes: cycle time and cycle percentage for the chain | Yes: occupation envelope, ratio and trains per hour for the area (never called in the history of the repository) |
| Period | None | Entered per analysis | The length of the simulation |
| Traffic | None | The checked occurrences | Every train with a blocking time in the range |

An analysis area is not a stored object. The scope can cross the edges of signalling areas and does not follow them. A block of level 1 or 5, or without a level, has no occupation and drops out without a message.

No simulation code reads `TrackLines/AreasCaseStudy.txt` today. The legacy export writes the signalling areas of the scene to it. The legacy import reports it and converts none of its rows (see the [migration matrix](v1-scene-migration-matrix.md)).

## The area calculation the program no longer contains

### Where to read it

The sources are in the Git history. Two commands show them:

```
git log --diff-filter=D --format=%H -- EGTRAIN/QEGTRAIN/simulation/Capacity.h
git show <that commit>~1:EGTRAIN/QEGTRAIN/simulation/Capacity.h
```

The callers were in `simulation/Optimisation.cpp` and `app/DispatchController.cpp`.

### How it was meant to run

1. The area file `TrackLines/AreasCaseStudy.txt` of the case input folder held one row per area: name, start, end, signalling level and an optional track line number. Start and end are in km. The track line number is -9999 when absent, which means all track lines. A row with a track line number names its area `<name>_<number>`.
2. `prepareSimulation` called `InitializeAllNetworkAreas` for this file (in the last revision that had the code, only when the file existed). That set the level of the blocks and collected the block ids of each area.
3. `computeCapacityAndTphForAllNetworkAreas` was to do the rest. For each area it selected the trains, computed the timetable variant and wrote `TT_Statistics_Area_<name>.txt` and `TT_BlockTrainsInArea_<name>.txt`. Then it compressed the trains and computed again. Last it added an area named EntireNetwork and wrote `Statistics_Area_<name>.txt` and `BlockTrainsInArea_<name>.txt`. The files went to `AreaResults/<area>_Results` under the folder that `ComputeNetworkCapacityForTimetable` received as its folder name.

In the history of the repository only the initialisation had a caller. `computeCapacityAndTphForAllNetworkAreas` was called only by `ComputeNetworkCapacityForTimetable`, as its last step: before it, that function reset the trains to the timetable and ran the train-level compression of the last row of [Status of each piece](#status-of-each-piece) on the trains of the run. No revision in the Git history of the repository called `ComputeNetworkCapacityForTimetable` (the history starts with an initial import). So no run of any revision in that history produced these files. The history also holds area files in the input folders of some older cases, and no output file of the calculation.

### Formulas

| Quantity | Rule | Unit |
| --- | --- | --- |
| Membership | See [Signalling areas, scope and the legacy area](#signalling-areas-scope-and-the-legacy-area). Only the first M blocking times of a train were taken, where M is the number of its blocking times that the run counted as complete | metres |
| Trains in the area | Every train with at least one such blocking time in the area, counted once by train description, ordered by entry time | count |
| Entry and exit time of a train | The smallest `StartOccTime` and the largest `EndOccTime` of its blocking times in the area | s |
| `TotalOccupationTime` | The latest exit minus the earliest entry over the trains. This is an envelope that includes the idle time between trains. It is -1 when there is no train | s |
| `PercentageTTCapOccupation` | `TotalOccupationTime` / `times`, a ratio and not multiplied by 100. `times` is the length of the simulation, a double. The header text of the file says `%TTCapacityConsumption` | ratio of two times |
| `TrainsPerHour` | N / ratio / (`times` / 3600), which equals N x 3600 / `TotalOccupationTime`. The header text of the file says `PotentialTPH`. Nothing guards against a zero or negative envelope or a zero `times` | trains per hour |
| Compression | The first train in entry order is not moved. Each later train is moved by the largest, over every earlier train and every pair of blocks that match, of (end of the earlier occupation - start of the later occupation). The earlier trains count at the times they have after their own move: the list handed to `ComputeMaxShiftAndShiftTrainsInArea` is a copy taken when the later train is handled. A train that matches no earlier train is moved to the earliest entry of the area. After the move the same envelope, ratio and trains-per-hour formulas apply | s |
| Critical couple | Per train: the absolute difference between the `StartRunTime` of the block that set its shift, moved by that shift, and the `StartRunTime` of the matching block of the earlier train. The couple of the area is the train with the smallest such value, written "train/earlier train". It is None when no train was shifted by a matching block | s |
| EntireNetwork | N is the number of all trains of the run, also those without blocking times. The envelope runs over the blocking time records of the trains themselves, not over the shifted copies that the area compression moves. The divisions are not guarded | as above |

The compression rule follows from `computeEntryTimesToSolveConflicts`. Call a the block of the later train and b the block of the earlier train. If a.start > b.end and a.end > b.end, the function returns the entry time of the later train - (a.start - b.end), which is that entry time + (b.end - a.start). Otherwise it returns that entry time + |b.end - a.start|. In that branch a.start <= b.end, because a.start > b.end together with a.end <= b.end would give a.end < a.start, and a blocking time that the run completed has `EndOccTime` not below `StartOccTime`. So |b.end - a.start| = b.end - a.start, and both branches give the entry time + (b.end - a.start). The shift of the later train is the largest of these differences. The removed code took the first M records of a train without testing whether each was complete (see the Membership row), so the single expression holds only for the records for which it was.

Two blocks matched when `AreOnTheSameBlock` returned true. It compared the same parts of the block ids as `shareBlockingTimeResource` does and had one more condition: when the first record of the pair carried a station name other than None, the second had to carry the same station name. As far as the code shows, only the routines of levels 3 and 4 copy a station name onto a blocking time, so the condition did not apply to records of levels 0 and 2.

### Status of each piece

A piece is valid today when it is still true in the current code. It is superseded when it ran and other code does the job now. It is dead when no revision in the history of the repository called it, or when nothing of the program uses it now. It is unsupported when the formula can be evaluated but the guide gives no reading of its result.

| Piece | What it did | Status | Names absent from the program |
| --- | --- | --- | --- |
| Reading the area file | Read one row per area from the area file | Superseded: no code reads the file; the signalling areas of the scene replace it | `NetworkArea`, `InitializeAllNetworkAreas` |
| Level assignment by area rows | Set the level of the blocks of an area | Superseded: the rule differs, see [Signalling areas, scope and the legacy area](#signalling-areas-scope-and-the-legacy-area) | `SetNetworkAreaBlocksAndSignalLevel` |
| Membership by start or end coordinate | Took a blocking time into an area when its start or its end lay in the range | Unsupported: ambiguous at the edges | `TrainInArea`, `InitializeTrainInArea`, `SetTrainsInNetworkArea` |
| The per-area calculation and the files it wrote | Selected the trains of each area, computed the variants and wrote the area files | Dead | `computeCapacityAndTphForAllNetworkAreas`, `ComputeNetworkCapacityForTimetable`, `PrintAreaResults`, `PrintTimetableAreaResults` |
| The occupation envelope | Latest exit minus earliest entry | Unsupported: it includes idle time and is neither the sum nor the union of occupation lengths | `ComputeTotalOccupationOfNetworkArea` |
| The ratio to the length of the simulation | Envelope divided by `times` | Unsupported: the denominator is not a chosen period and is not guarded | `ComputeAreaCapacityOccupationAndTPH_For_Timetable` |
| Trains per hour | N divided by the ratio, per hour of simulation | Unsupported: an extrapolation of the envelope, unguarded | `ComputeAreaCapacityOccupationAndTPH` |
| The area compression | Moved each later train until its blocks touch the blocks of the earlier trains | Dead. The idea of moving a train until its occupation touches the previous one is valid today in `analyzeCapacity`, with different rules (see step 5 of [The calculation](#the-calculation)) | `compressAllTrainsInArea`, `ComputeMaxShiftAndShiftTrainsInArea`, `computeEntryTimesToSolveConflicts` |
| The block match test | Compared the block ids of two blocking times | Valid today in `shareBlockingTimeResource`, without the station-name condition | `AreOnTheSameBlock` |
| EntireNetwork | Added an area for all trains | Unsupported | `Compute_Capacity_And_TPH_For_Entire_Network` |
| The critical couple | Named the pair of trains with the smallest difference | Dead | `IdentifyMostCriticalTrainCouple` |
| The macroscopic events and processes export | Computed headways and wrote `Events.csv` and `Processes.csv` for a macroscopic model, as its comments say. It is not part of the analysis of this guide | Dead: no revision in the history of the repository called the three functions | `MacroscopicEvent`, `initialiseAllMacroEvents`, `Compute_Headways_For_AllNetworkAreas_For_Macro_Model`, `Print_Macroscopic_Events_And_Processes` |
| The train-level headway matrix and compression that comments call the "UIC code 406 approach" | The first function took trains in a given order. For the first train it applied a shift. For each later train it computed a departure matrix, shifted the train and detected conflicts, and repeated while overlaps remained. `ComputeNetworkCapacityForTimetable` was its only caller, and the other two functions were called only by it. The formulas are not described here | Dead | `ComputeHwMatrixWithGivenOrderSolvingAllConflicts`, `ShiftTrainToCompressTT`, `DepartureMatrixToSolveConflictsForGivenOrderImproved3` |

### A hand check of the envelope

Apply the envelope rule to the two trains of the worked example, with an area that spans the whole line, so that all eight blocks are in. The earliest `StartOccTime` is 53.5 s (train 1, `@1-B0@`) and the latest `EndOccTime` is 1201 s (train 2, `@7-B0@`). The envelope is 1201 - 53.5 = 1147.5 s. For N = 2 the trains per hour are 2 x 3600 / 1147.5 = 6.27. The ratio would be 1147.5 / `times` for whatever the simulation length was, and N / ratio / (`times` / 3600) gives the same 6.27 for every length.

This is the formula applied by hand to current records. It is not an output of the removed program. It answers a different question than the cycle of the worked example, which covers one chain after compression.

### The label

The removed code carries the label "UIC code 406" for the compression, in comments in `Optimisation.cpp`, `Optimisation.h` and `Simulation.cpp` and in one console message in `Optimisation.cpp`. A comment in `Capacity.cpp` writes it "UIC Code 406". The repository holds no description of that method. This guide therefore says neither that either calculation follows it nor that it does not.

## Limits

1. The percentage covers the chain only. Trains that are not checked, or that have no valid occupation on the first selected block, are not part of the figure, and the window does not say whether they use the same blocks.
2. The compression depends on the order the user gives. The first order is the order of the run, not the order of time, and the program never sorts.
3. No buffer or supplement is added after the compression. The Buffer column is the scheduled headway minus the minimum headway, and the scheduled reference of a later train does not enter the compression.
4. The program compares the percentage with no threshold and makes no recommendation. It can exceed 100.
5. Occupations exist only at levels 0, 2, 3 and 4 ([Inputs](#inputs)).
6. The setup, sight reaction and release times and the margin are fixed by the run, not by the window. The figures depend on them.
7. The direction of a train is not examined ([Special cases](#special-cases)).
8. A missing occupation on a selected block is not reported ([Special cases](#special-cases)).
9. One analysis covers one route section. The [Assignment workflow](../product/assignment-workflow.md#calculation-boundaries) handles a change of train order by separate sections.
10. The period is independent of the length of the run and of the timetable.
11. No agreement with the removed calculation is established. No output of it exists in the repository, and the two answer different questions ([hand check](#a-hand-check-of-the-envelope)). Network-wide validity is not claimed: the program has no rule for which traffic a result must include.
12. The program cites no norm or recommendation for the percentage. Step 18 of the Assignment workflow refers to a course recommendation and does not state it.
