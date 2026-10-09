# How delays are calculated

This guide says what the delay figures of EGTRAIN mean: the formula, the unit, the reference timetable, which calls are counted and what is shown when a value is missing. It describes the program as it is, including its limits. The scene fields it mentions are in the [scene schema](../architecture/scene-schema.md#servicesjson).

## What this guide covers

A run produces delay figures in five places. The application has no separate delay analysis window. The figures of the five places are not the same quantity: they start from different arrivals, count different populations and use different thresholds.

| Place | Where to find it |
| --- | --- |
| Timetable results | Diagrams > Timetable table (planned vs simulated); Diagrams > Train delays and Run Results > Open result view > Delays (a chart); the file `timetable.csv` from the **Export CSV...** button of both windows |
| Station statistics | `Stats_Stations.txt` and `Pos&Neg_Stats_Stations.txt` in the `TrainTrajectories` folder of the run output |
| Baseline comparison | The buttons **Set delay baseline** and **Compare delays** in the Run Results dock, and the file `delay_comparison.csv` |
| Passenger journey delay | `JourneyDelays.txt` in the `PassengerStatus` folder of the run output |
| Log line | One "Station delay:" line per train in the application log |

The Timetable graph (Run Results > Open result view > Timetable graph (planned vs simulated stops)) draws the planned and the simulated times whose difference the timetable results report. It shows no delay figure and is not a sixth place.

## Units and the reference timetable

All values are seconds. One simulation step is one second: the global timestep is 1 and no code assigns another value.

Planned times are the `planned_arrival_seconds` and `planned_departure_seconds` of the stops of a service, in seconds from the base-time origin of the scene. Each is optional. Occurrence n of a repeated service is shifted by (n - 1) times the headway. A planned time that is absent, or below 0 after the shift, counts as no planned time. Simulated times are seconds on the same axis.

A scenario entrance delay adds its seconds to the planned departure of the named stop of that occurrence, records the seconds as the entrance delay of the train, and leaves every planned arrival as authored. So the departure delay at that stop is measured against the shifted planned departure, and arrival delays are not shifted (see [Where delays are calculated](#where-delays-are-calculated) and [Early, missing and repeated cases](#early-missing-and-repeated-cases)). The entrance delay is a field of a scenario in [`scenarios.json`](../architecture/scene-schema.md#scenariosjson).

A train that stands at a stop it has reached does not leave before the planned departure of that stop, when the stop has one, and not before its dwell time has passed (`trajectoryComputationIncludingMovingBlock`). In the line fixture of the tests a train with a planned departure of 660 s at its middle stop leaves at 660 s. A train that begins its route at a stop is not held there: it starts at the entry time of its service. In the same fixture a train starts at 60 s although its planned departure at the first stop is 120 s. An entrance delay at such a stop changes the planned departure the results show but adds no waiting time.

## Arrival and departure of a call

Both come from the timetable points that the run computes for every station node on the route of a train (`ComputeTimetablingPoints`). For each point, `computeArrivalAndDepartureAtLocation` scans the position samples from the entry of the train:

- The arrival is (t - 1) times the timestep for the first sample t whose position is no longer short of the point while sample t - 1 is short of it.
- The departure is (t - 1) times the timestep for the first sample t whose position is past the hold-back position of the point while sample t - 1 is not.

The hold-back position is 0.0001 m before the point (`kStopHoldbackM`). A position within 0.000001 m of it (`kStopHoldbackToleranceM`) is neither short of the point nor past the hold-back position. The standing-train branch of `trajectoryComputationIncludingMovingBlock` holds a train that serves a stop at the hold-back position with speed 0, and starts it again at 0.0001 m past the point with speed 0.0001 m/s. So for a stopping train the arrival is the last sample before the train is held at the hold-back position, and the departure is the last sample at which it is held there.

The line fixture shows the pairs. The test harness treats every sample below 0.001 m/s as standing, and the release sample (0.0001 m/s) is one of them. In each of the four stops of `tests/characterization/expected/follow-level-0.txt` the arrival equals the first standing sample and the departure equals the last standing sample minus 1:

| Stop | First standing sample | Last standing sample | Arrival | Departure |
| --- | ---: | ---: | ---: | ---: |
| Train 1, middle station | 331 | 661 | 331 | 660 |
| Train 1, last station | 933 | 981 | 933 | 980 |
| Train 2, middle station | 818 | 879 | 818 | 878 |
| Train 2, last station | 1151 | 1199 | 1151 | 1198 |

A train that passes a station node without stopping has an arrival and a departure there as well; it does not have to come to rest. A train that never reaches a point has no arrival there: the event keeps its default time of -10000, which every delay figure treats as unavailable. The same holds for a point the train begins its route on, because its position is never short of it (the first stop of both trains in the fixture has no simulated time). A train that is still at its last point when the run ends has no departure.

The n-th stop of a train at a station name uses the n-th timetable point of that name along its route. `timetablePointOfStop` counts the earlier stops of the name and `buildTimetableResults` counts the earlier rows of the name; both pair by name and order. The same events feed the timetable results and both statistics files. Only a stop that has no timetable point at all uses an arrival recorded while the train moved: the first second in which the head is within 5 m of the station position, or the second of standing at it.

## Where delays are calculated

| Place and how to open it | Quantity and formula | Reference | Calls counted and denominator | Unavailable values |
| --- | --- | --- | --- | --- |
| Timetable results: Diagrams > Timetable table; Diagrams > Train delays; `timetable.csv` | Arrival delay and departure delay, each simulated minus planned, in seconds, signed (early is negative), not rounded. The table shows text such as "+158 s" without decimals, red above 60 s, dark yellow above 0, dark green otherwise. The chart plots the arrival delay in minutes over journey order. The CSV holds all ten columns | Planned time of the same stop ([units](#units-and-the-reference-timetable)); at an entrance-delayed stop the planned departure is the shifted one | One row per stop of every train; a repeated station gives one row per call. No sum, mean or share is formed. **Export CSV...** of both windows writes the rows of the trains selected in the window (suggested name `timetable.csv`), with a provenance file beside the CSV | Unavailable when either side is. The table shows "-", the CSV an empty field, the chart skips the point |
| `Stats_Stations.txt` | Arrival delay per call; early arrivals set to 0 ([rules](#the-station-statistics-files)) | Planned arrival | Every call with a simulated and a planned arrival; the denominator of a station row is its `N_StopTrains` | The call is not counted. A row without a counted call holds -1 |
| `Pos&Neg_Stats_Stations.txt` | The same with the signed delay, early arrivals negative | Planned arrival | As above | As above, but -1 can also be a real value |
| Rows `Ent_Del+DwT_Dist`, `Ent_Delays`, `DwT_Dist` of both files | Entrance delay, and entrance delay plus dwell disturbance, of each train in seconds | None (scenario input) | Every train of the run; `N_StopTrains` is the number of trains | -1 when the run has no train |
| Baseline comparison: **Compare delays** ([rules](#delay-comparison-against-a-baseline)) | Scenario minus baseline of the simulated arrival at the final call of each occurrence | The baseline run, not a timetable | Every occurrence of the run; only positive differences become rows; the total is their sum | The whole comparison is rejected |
| Passenger journey delay: `JourneyDelays.txt` | Actual arrival time of the last trip of the journey minus its planned arrival, in seconds, signed. The planned arrival is drawn at random within the planned arrival window of the journey, and both times are seconds from midnight | Planned arrival window of the journey | One line per journey of every passenger. This is a different quantity from train delay | A journey that did not complete is written as Journey_not_yet_completed |
| Log line "Station delay:" | `StationDelay` of the last stop of each train after the first pass, so early arrivals show as 0. Written with six decimals | Planned arrival of the last stop | One line per train | -1 when that stop has no delay value; the text "unavailable (no stops)" for a train without stops |

An unavailable value in a CSV is an empty field.

## The station statistics files

A run writes both files at the end of the simulation, in the `TrainTrajectories` folder. `Stats_Stations.txt` is written first (`Print_Station_Delay_Stats`, "pos"), then the arrival delays are recomputed with the sign kept and `Pos&Neg_Stats_Stations.txt` is written. The first line names 12 columns; values are printed with six significant digits.

| Column | Unit |
| --- | --- |
| `StName` | station name |
| `Av_Delay`, `Std_Delay`, `Total_Delay`, `Max_Tot_Delay` | seconds |
| `Cum_Cons_Delay`, `Max_Cons_Delay` | seconds |
| `Perc_Delayed`, `Perc_Delayed_3min`, `Perc_Delayed_5min` | percent |
| `N_StopTrains`, `N_DelTrains` | counts (calls; trains in the three input rows) |

The rows are the three input rows, then one row per station of the network except the first station of the network, then `TOTALS` and `Final_Station`. The rules, as `calculateDelayStatistics`, `calculateStationDelayStatistics`, `computeArrivalDelaysAtStations` and `Compute_Pos_And_Neg_Arrival_Delays_At_Stations` apply them:

1. A call is counted when its stop has a simulated arrival and a planned arrival; otherwise it is in neither file. Every call of a repeated station is one sample. `Final_Station` takes the last stop of each train, when that stop is counted.
2. In `Stats_Stations.txt` the delay is the arrival minus the planned arrival, with early arrivals set to 0 and counted as samples. The average and the standard deviation are over the delayed samples only. `Total_Delay` sums all samples and `Max_Tot_Delay` is the largest. The shares count the samples with a delay greater than 0, 180 and 300 seconds (strictly greater), over all samples, in percent. `N_DelTrains` counts the samples greater than 0. In `Pos&Neg_Stats_Stations.txt` the same is computed with the signed delay, and the average and deviation are over all samples. The standard deviation is the sample standard deviation (n - 1) and is 0 for one sample.
3. `Cum_Cons_Delay` and `Max_Cons_Delay` use the delay minus the entrance delay of the train, and minus the dwell disturbances of earlier stops, which are always 0 because nothing sets them. The value of each call is set to 0 when negative in the first file only. `Cum_Cons_Delay` is the sum of these values over the calls of the station and `Max_Cons_Delay` the largest.
4. A row without any sample holds -1 in the average, deviation, maximum, cumulative and percentage columns and 0 in `Total_Delay` and the counts. In the signed file -1 can also be a real value, so `N_StopTrains` = 0 is the test for no sample. A 0 average in the first file can mean that no train was delayed.
5. `TOTALS` is the unweighted mean of the station values (average, deviation, shares) over the stations that have samples, the sum of their `Total_Delay` and the largest of their maxima. It is not a mean over all calls, and it has no count columns. With no sample anywhere it holds -1 in every column except `Total_Delay`, which is 0.
6. The three input rows summarise the entrance delay, and the dwell disturbance, of each train over all trains of the run; `N_StopTrains` is the number of trains and the average is over the positive values only. Their `Cum_Cons_Delay` and `Max_Cons_Delay` columns hold N/D.
7. The thresholds are divided by the timestep, which is 1.

Worked example, the arithmetic of the test case `follow-level-0` (entrance delays 0). Two trains call at two stations, S1 and S2. Train 1 arrives 269 s early at S1 and 167 s early at S2; train 2 arrives 158 s late at S1 and 9 s early at S2. The last stop of both trains is S2.

```
Stats_Stations.txt
S1 158 0 158 158 158 158 50 0 0 2 1
S2 0 0 0 0 0 0 0 0 0 2 0
TOTALS 79 0 158 158 158 158 25 0 0
Final_Station 0 0 0 0 0 0 0 0 0 2 0

Pos&Neg_Stats_Stations.txt
S1 -55.5 301.935 -111 158 -111 158 50 0 0 2 1
S2 -88 111.723 -176 -9 -176 -9 0 0 0 2 0
TOTALS -71.75 206.829 -287 158 -287 158 25 0 0
Final_Station -88 111.723 -176 -9 -176 -9 0 0 0 2 0
```

At S1 the first file has the samples 0 and 158: the average is over the one delayed sample, the share is 1 of 2. The signed file has -269 and 158: the mean is -55.5 and the standard deviation 213.5 times the square root of 2.

The characterization tests check that `Total_Delay` and `N_StopTrains` of every station row equal the sum and the number of the arrival delays in the timetable results (only the late ones in the sum for the first file).

## Delay comparison against a baseline

**Set delay baseline** freezes the completed run as the baseline. The run must have no incidents and no entrance delays in its scenario. The baseline is cleared when the case changes: creating a new case study, opening another one, or editing the case in any way. Selecting another scenario keeps it.

**Compare delays** needs a baseline and a completed run whose scenario has at least one incident and no entrance delays. The line above the buttons says why a button is disabled. The comparison (`compareDelayRuns`) requires different scenario ids, the same scene revision, base time, duration and timestep, and the same set of (service, occurrence) pairs in both runs.

For each occurrence the metric is the simulated arrival at its final call in the scenario run minus the same arrival in the baseline run. The final call is the last authored call. The comparison uses no planned times. Only positive differences become rows; zero and negative differences are not listed. The total is the sum of the positive rows. A comparison with no positive row is a success ("zero positive additional final-arrival delay"). The whole comparison is rejected, with the first failing reason only, when any check fails or when an occurrence has no simulated arrival at its final call in either run.

The dialog has 13 columns: Service, Occurrence, Operating code, Baseline final arrival, Scenario final arrival, Positive delay, Attribution, Incident IDs, First direct time, First direct location, Termination requested, Terminated, Baseline / scenario. `delay_comparison.csv` has 14: Baseline scenario, Scenario, Case revision, Service, Occurrence, Operating code, Baseline final arrival[s], Scenario final arrival[s], Positive contribution[s], Attribution, Incident IDs, First direct time[s], First direct location[m], Termination outcome.

Attribution is "primary" when the scenario run recorded direct incident evidence for that occurrence: a speed cap, a breakdown hold, or a stop at the end of authority of a signal failure. It is "secondary" when none was recorded. That is not evidence that the delay was propagated, and the comparison makes no causal claim. The line "Direct incident evidence: N" of the Run Results dock counts the trains with at least one recorded incident id. It shows only when View > Advanced / Developer details is checked.

## Early, missing and repeated cases

| Case | Timetable results | Statistics files | Baseline comparison |
| --- | --- | --- | --- |
| Early arrival | Signed negative value, for example "-269 s", dark green | 0 in `Stats_Stations.txt` (a counted sample, not delayed); negative in `Pos&Neg_Stats_Stations.txt` | Planned times are not used; an occurrence earlier than in the baseline gives a negative difference, not listed |
| Exactly on time | "+0 s", dark green | A sample of 0, counted, not delayed | A difference of 0 is not listed |
| No planned time | The delay of that event is unavailable; planned arrival and planned departure are independent | A call without a planned arrival is not counted; a missing planned departure has no effect | Not needed |
| Train did not reach the stop | Simulated time and delay unavailable | Not counted: it adds no sample | Rejected when it is the final call in either run; otherwise no effect |
| No simulated departure (train still at its last point) | Departure delay unavailable; arrival delay shown | No effect; only arrivals are used | No effect |
| Repeated station | One row per call; the n-th call takes the n-th timetable point of the name | Every call is a sample of the station row; `Final_Station` takes the last stop only | Only the last call of the occurrence counts |
| Entrance delay | Departure delay at the named stop is against the shifted planned departure; arrival delays are not shifted | `Cum_Cons_Delay` and `Max_Cons_Delay` subtract the entrance delay from each call of the train; the row `Ent_Delays` lists it | Rejected: the scenario may not have entrance delays |
| Scene with no planned arrival at all | Arrival delay unavailable at every stop; departure delay exists where a planned departure exists | No station has a sample: rows hold -1, `TOTALS` -1 and 0 | Works; it needs no planned time |

The Netherlands scene has no `planned_arrival_seconds` at any stop.

## Calculations the program no longer contains

The only delay statistics code left in the program is the code this guide describes.

| Calculation | What it did | Names absent from the program |
| --- | --- | --- |
| Rescheduling, optimisation and dispatching | A rescheduling interface to an external dispatching tool (messages, decisions, platform and route changes) and functions that ordered services, built an optimised timetable and moved train times to it. The area capacity code removed with them computed no delay | `handleDispMessage`, `loadDispDecisions`, `checkArrivalPlatform`, `ComputeOptimisedTimetable`, `Generate_Optimised_Train_Service_Sequence`, `ResetTrainsAccordingToTimetable` |
| Older delay entry point | A second way to run the arrival delay calculation, with the older arrival detector | `calculateArrivalDelayAllTrainsOldVersion` |
| Stochastic dwell and entrance delays | Drew dwell times at stations from a Gaussian distribution around the planned dwell time and wrote them to files; read such files back and set the stop time and the dwell disturbance (stop time minus planned dwell time) of each stop; read per-train entrance delays from files and added them to the planned departure at a named station; added a Gaussian error to such entrance delays. Nothing sets the dwell disturbance, so the `DwT_Dist` row and the dwell part of the consecutive delay are always 0 | `drawGaussianTrainDwellTimes`, `Load_And_Set_Stoc_Dwell_Times`, `Load_Departure_Delay_At_Station`, `Load_Entrance_Delay_Disturbed_Scenario`, `Generate_Entrance_Delays_Affected_By_Error` |
| Older arrival detectors | Took the arrival of a stop from the position samples: the first sample at rest exactly 2 mm short of the station, or the first sample at rest less than 5 mm short of it or beyond it after a sample more than 3 m short of it. The `Actual_Arrivals` functions applied this to the stops of a train | `Arrival_At_Station`, `Arrival_At_Station_NewVersion`, `Actual_Arrivals`, `Actual_Arrivals_NewVersion` |

## Limits

1. The figures cover arrival and departure events of authored stops. Passing a station without a stop is not a call.
2. Three threshold sets are in use: the statistics files count calls later than 0, 180 and 300 s; the Timetable table colours at 0 and 60 s; the chart is in minutes with no threshold. The code has no 1 minute threshold.
3. Early arrivals are 0 in `Stats_Stations.txt` and negative in `Pos&Neg_Stats_Stations.txt` (the `stats` and `signed_stats` lines of `follow-level-0.txt`, as in the example above).
4. A scene or a stop without a planned arrival gives no arrival delay in any place. The baseline comparison does not need planned times.
5. The comparison refuses a scenario that has entrance delays and lists no zero or negative difference.
6. An entrance delay moves the planned departure of its stop only; the departure delay at that stop is against the moved planned departure.
7. Direct incident evidence is recorded for a speed cap (`effectiveIncidentSpeedLimit`), a breakdown hold, and a train that stops at the end of authority of a signal failure (`recordDirectSignalFailure`, called once). A train held by the red aspect of a failed signal at the fixed-block levels (0, 1, 2 and 5, see [signalling levels](../architecture/signalling-levels.md)) is not recorded. In the signal failure goldens (the files in `tests/characterization/expected` whose names start with sf-) `direct_incident count=0` holds for every train in the 20 cases at levels 0, 1 and 2 and in the two level 5 cases. At levels 3 and 4 only the first train of the direction has it (`sf-forward` and `sf-reverse`). With no level both trains have it in `sf-adjacent`, `sf-forward`, `sf-last`, `sf-reverse` and `sf-staggered`, and neither has it in `sf-first-level-none` and `sf-entered-level-none`. For such a run with advanced details on, the Run Results line says "Direct incident evidence: 0" and **Compare delays** is refused with "Incident run has no direct incident evidence for attribution". This describes the current behaviour of the program and is not a statement about the railway.
8. The **Export CSV...** button of the Delays chart writes the timetable file for the selected trains, not a delay file.
9. The passenger journey delay is another quantity: it is measured against a planned arrival drawn from a window, not against the timetable of the train.
10. None of the committed scenes has an incident or an entrance delay; each has only the baseline scenario, so the baseline comparison cannot be tried on a committed scene.
11. Nothing in the application attributes a delay to a cause beyond the recorded direct evidence.
12. The pairing of stops and timetable points is by name and order only. It is exact when the n-th stop of a name takes place at the n-th station node of that name on the route; the run does not check this.
