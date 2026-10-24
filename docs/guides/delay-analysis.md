# How delays are calculated

This guide says what the delay figures of EGTRAIN mean: the formula, the unit, the reference timetable, which calls are counted and what is shown when a value is missing. It describes the program as it is, including its limits. [Definitions](#definitions) fixes the words, [Supported scope](#supported-scope) says what the figures cover and [Limits](#limits) what they do not. The case fields it mentions are in the [scene schema](scene-schema.md#servicesjson).

## What this guide covers

A run produces delay figures in four places. The Delay analysis window (Diagrams > Delay analysis and Run Results > Open result view > Delay analysis) shows the first and the third of them. The figures of the four places are not the same quantity: they start from different arrivals, count different populations and use different thresholds.

| Place | Where to find it |
| --- | --- |
| Timetable results | Diagrams > Timetable and Run Results > Open result view > Timetable; Diagrams > Delays... and Run Results > Open result view > Delays (a chart); the file `timetable.csv` from the **CSV...** button of both windows; the tabs Calls, Summary and Punctuality of the Delay analysis window and the files `delay_analysis.csv` and `delay_histogram.csv` from its **CSV...** button |
| Station statistics | `Stats_Stations.txt` and `Pos&Neg_Stats_Stations.txt` in the `TrainTrajectories` folder of the run output |
| Baseline comparison | The buttons **Set delay baseline** and **Compare delays** in the Run Results dock, the tab Against baseline of the Delay analysis window and the file `delay_comparison.csv` from its **CSV...** button |
| Passenger journey delay | `JourneyDelays.txt` in the `PassengerStatus` folder of the run output |

The Timetable graph (Run Results > Open result view > Timetable graph) draws the planned and the simulated times whose difference the timetable results report. It shows no delay figure and is not a fifth place.

## Definitions

These words mean the same in the Delay analysis window, in its CSV files and in this guide. Every value is in seconds.

- **Event.** The arrival or the departure of one call of an occurrence. A call is an authored stop of a service, and an occurrence is one run of a service, so a service that repeats has several. Passing a station without a stop is not an event.
- **Planned lateness.** The window shows it as Delay. The simulated time of an event minus its planned time, signed: negative is early, 0 is on time, positive is late. The stored value has no tolerance. The only tolerances are the thresholds of the summary, which also bound the classes of the tab Punctuality. The planned time is the authored one ([units](#units-and-the-reference-timetable)): for the departure of a stop that an entrance delay moved, the window takes the shift back.
- **Not available.** An event has no delay when its plan is missing (No plan), when the train did not reach it in the run (Not reached), or both. The delay is then "-" in the window and an empty field in the files. These events are counted next to the figures and are never taken as 0.
- **Entrance delay.** It is also called the input delay and the primary delay. The seconds that a scenario adds to the planned departure of one stop of one occurrence. The author sets it, so it is an input and not a measurement. No other delay is called primary: an incident does not make a delay primary.
- **Additional delay.** The simulated time of an event in the scenario run minus the simulated time of the same call of the same occurrence in the baseline run. It needs a compatible baseline ([rules](#delay-comparison-against-a-baseline)). It can be negative (Earlier) or 0 (Same).
- **Secondary delay.** The additional delay at the final arrival of an occurrence minus its entrance delay, when the stop that the entrance delay moved comes before the final call. Without an entrance delay, or when the moved stop is the final call, it equals the additional delay. It is a difference of two numbers. It says nothing about why the train was late and it can be negative.
- **Direct incident evidence.** The scenario run recorded that an incident acted directly on the occurrence ([which cases](#limits), item 7). "None recorded" means that no direct effect was recorded. It does not show that another train caused the delay, and the window does not call such a delay propagated.
- **Class.** Early, On time or Late for a delay below 0, exactly 0 and above 0.
- **Later than a threshold.** Strictly later: a delay equal to the threshold is not later. The summary and the classes of the tab Punctuality use the thresholds 0, 60, 180 and 300 s, so a delay equal to a threshold is in the class below it.

## Units and the reference timetable

All values are seconds. One simulation step is one second: `timestep` is the constant 1.

Planned times are the `planned_arrival_seconds` and `planned_departure_seconds` of the stops of a service, in seconds from the base-time origin of the case. Each is optional. Occurrence n of a repeated service is shifted by (n - 1) times the headway. A planned time that is absent, or below 0 after the shift, counts as no planned time. Simulated times are seconds on the same axis.

A scenario entrance delay adds its seconds to the planned departure of the named stop of that occurrence, records the seconds as the entrance delay of the train, and leaves every planned arrival as authored. The stop needs a planned departure: without one, validation reports the error `scene.entrance.timetable` and the case does not run. When the service stops more than once at the named station, the first of those stops is the one that is shifted. So the departure delay at that stop in the timetable results is measured against the shifted planned departure, and arrival delays are not shifted (see [Where delays are calculated](#where-delays-are-calculated) and [Early, missing and repeated cases](#early-missing-and-repeated-cases)). The Delay analysis window takes the shift back, so that the departure shows the entrance delay ([the window](#the-delay-analysis-window)). The entrance delay is a field of a scenario in [`scenarios.json`](scene-schema.md#scenariosjson).

A train that stands at a stop it has reached does not leave before the planned departure of that stop, when the stop has one, and not before its dwell time has passed (the `dwell_seconds` of the stop, or longer when boarding and alighting passengers need longer; [the rule](scene-schema.md#servicesjson)). It also stays while a red signal at the platform or an end of authority applies, and at a node with a train order until the order allows it to leave (`trajectoryComputationIncludingMovingBlock`). For a station that a train serves more than once, the planned departure of the last stop of that name applies at every call of the station. In the line fixture of the tests a train with a planned departure of 660 s at its middle stop leaves at 660 s.

The last stop of a service ends the service when it has no planned departure: the train leaves after its dwell, drives on and is removed from the run when its head passes the end node of its route. A planned departure at the last stop holds the train there until that time, so a planned departure after the end of the run keeps it at the stop to the end of the run. The Netherlands case has no planned time at any stop, so its trains leave the network after their dwell at the last stop unless the run ends first.

A train that begins its route at a stop is not held there: it enters at the entry time of its service (`entry_time_seconds`, or the planned departure of the first stop when the service has no entry time, or 0 when that stop has none), shifted like the other times of a repeated service. In the same fixture a train enters at 60 s although its planned departure at the first stop is 120 s. An entrance delay does not change the entry time; at the first stop it shifts the planned departure that the results show.

## Arrival and departure of a call

Both come from the timetable points that the run computes for every station node on the route of a train (`ComputeTimetablingPoints`). For each point, `computeArrivalAndDepartureAtLocation` scans the position samples from the entry of the train:

- The arrival is (t - 1) times the timestep for the first sample t whose position is no longer short of the point while sample t - 1 is short of it.
- The departure is (t - 1) times the timestep for the first sample t whose position is past the hold-back position of the point while sample t - 1 is not.

The hold-back position is 0.0001 m before the point (`kStopHoldbackM`). A position within 0.000001 m of it (`kStopHoldbackToleranceM`) is neither short of the point nor past the hold-back position. The standing-train branch of `trajectoryComputationIncludingMovingBlock` holds a train that serves a stop at the hold-back position with speed 0, and starts it again at 0.0001 m past the point with speed 0.0001 m/s. So for a stopping train the arrival is the last sample before the train is held at the hold-back position, and the departure is the last sample at which it is held there.

The line fixture shows the pairs. The test harness treats every sample below 0.001 m/s as standing, and the release sample (0.0001 m/s) is one of them. It lists a stop of at least five standing samples. In each of the two stops at a station in `tests/characterization/expected/follow-level-0.txt` the arrival equals the first standing sample and the departure equals the last standing sample minus 1:

| Stop | First standing sample | Last standing sample | Arrival | Departure |
| --- | ---: | ---: | ---: | ---: |
| Train 1, middle station | 331 | 661 | 331 | 660 |
| Train 2, middle station | 817 | 878 | 817 | 877 |

The file lists one more stop, of train 2 at 5999.998 m, in front of the block that train 1 occupies. The last stations have a dwell of 0 s, so the trains stand for fewer than five samples there and the file lists no stop for them. Their results are arrival 933 and departure 934 for train 1, and arrival 1150 and departure 1151 for train 2.

A train that passes a station node without stopping has an arrival and a departure there as well; it does not have to come to rest. A train that never reaches a point has no arrival there: the event keeps its default time of -10000, which every delay figure treats as unavailable. The same can hold for a stop where the train begins its route: its position is never short of the point, or the route has no timetable point on that node (the first stop of both trains in the fixture has no simulated time). A train that is still at its last point when the run ends has no departure.

The n-th stop of a train at a station name uses the n-th timetable point of that name along its route. `timetablePointOfStop` counts the earlier stops of the name and `buildTimetableResults` counts the earlier rows of the name; both pair by name and order. The same events feed the timetable results and both statistics files. Only the statistics files have a fallback: a stop that has no timetable point at all uses an arrival recorded while the train moved, the first second in which the head is within 5 m of the station position or the second of standing at it, whichever the run records first. The timetable results show such a stop as unavailable.

## Where delays are calculated

| Place and how to open it | Quantity and formula | Reference | Calls counted and denominator | Unavailable values |
| --- | --- | --- | --- | --- |
| Timetable results: Diagrams > Timetable; Diagrams > Delays...; `timetable.csv` | Arrival delay and departure delay, each simulated minus planned, in seconds, signed (early is negative), not rounded. The table shows text such as "+157 s" without decimals, dark red above 60 s, dark brown above 0, dark green otherwise. The chart plots the arrival delay in minutes at the stations of a reference route, against the distance along it. The CSV holds all ten columns | Planned time of the same stop ([units](#units-and-the-reference-timetable)); at an entrance-delayed stop the planned departure is the shifted one | One row per stop of every train; a repeated station gives one row per call. No sum, mean or share is formed (the tabs Summary and Punctuality of the Delay analysis window form them from the same rows, [the window](#the-delay-analysis-window)). **CSV...** of both windows writes the rows of the trains selected in the window (suggested name `timetable.csv`), with a provenance file beside the CSV | Unavailable when either side is. The table shows "-", the CSV an empty field, the chart skips the point |
| `Stats_Stations.txt` | Arrival delay per call; early arrivals set to 0 ([rules](#the-station-statistics-files)) | Planned arrival | Every call with a simulated and a planned arrival; the denominator of a station row is its `N_StopTrains` | The call is not counted. A row without a counted call holds -1 |
| `Pos&Neg_Stats_Stations.txt` | The same with the signed delay, early arrivals negative | Planned arrival | As above | As above, but -1 can also be a real value |
| Rows `Ent_Del+DwT_Dist`, `Ent_Delays`, `DwT_Dist` of both files | Entrance delay, and entrance delay plus dwell disturbance, of each train in seconds | None (scenario input) | Every train of the run; `N_StopTrains` is the number of trains | -1 when the run has no train |
| Baseline comparison: **Compare delays** ([rules](#delay-comparison-against-a-baseline)) | Scenario minus baseline of the simulated arrival and of the simulated departure of each call; the totals add the arrival difference at the final call of each occurrence | The baseline run, not a timetable | Every call of every occurrence of the run; the totals count the occurrences | The call is Not available and is left out of the totals; an incompatible baseline gives no difference |
| Passenger journey delay: `JourneyDelays.txt` | Actual arrival time of the last trip of the journey minus its planned arrival, in seconds, signed. The planned arrival is drawn at random within the planned arrival window of the journey, and both times are seconds from midnight | Planned arrival window of the journey | One line per journey of every passenger. This is a different quantity from train delay | A journey that did not complete is written as Journey_not_yet_completed |

An unavailable value in a CSV is an empty field.

## The Delay analysis window

**Diagrams > Delay analysis** and **Run Results > Open result view... > Delay analysis** open the window for the completed run on the tab Calls. **Compare delays** opens it on the tab Against baseline. The window does not block the main window and keeps the run it was opened for, so a later run does not change it. Its title is Delay analysis, and its header names the run and the delay baseline, such as "Paimpol / incident · delay baseline Paimpol / baseline", or "no delay baseline set". The window explains no column in a paragraph: the headers of the delay columns have the definitions of [Definitions](#definitions) as their tooltips.

The tabs lie at the left and a panel at the right. The panel starts with three figures of the selected events, the same as the row "All selected stations" of the tab Summary: the sum of the delays ("Sum of delays"), their mean ("Mean"), and the number of early and late events ("Early / late", such as "3 / 1"). The tooltips of the sum and the mean give the number of Available events, and the tooltip of Early / late gives the early, on-time and late counts. A sum or mean without an Available event reads "-". The three choices follow. **Trains** and **Stations** list what the run has, each with its count, such as "Trains (2/2)"; uncheck an entry, or use All and None, to leave it out. **Events** switches Calls, Summary, Punctuality and the figures between arrivals and departures, as in "Events: Arrivals". The choice of trains applies to all four tabs and the figures, the choice of stations to Calls, Summary, Punctuality, the figures and the call rows of Against baseline, and the choice of events to Calls, Summary, Punctuality and the figures only.

### Calls

One row per event. Read a row from the left: the service, occurrence, train and operating code; the station; the journey order (the position of the stop in the journey of the train) and the call (the n-th stop at that station name); the event; the planned and the simulated time; the delay; the class; and the status. The planned and the simulated time are those of the timetable results ([arrival and departure](#arrival-and-departure-of-a-call)), except that the planned departure of a stop that an entrance delay moved is the authored one, so that departure shows the entrance delay.

The delay is simulated minus planned, signed; the tooltip of the header Delay (s) says so. The class is Early, On time (exactly 0) or Late. The status is Available, No plan, Not reached, or No plan, not reached. For any other status the delay is "-" and the class is Not available. A train that begins its route at a stop often has no simulated time there: in the line fixture the first stop of both trains is Not reached for the departure.

### Summary

One row for each station, in the order in which the stations first appear, and one row for all selected stations. The row for all stations is over all selected events, not a mean of the station means. A row counts:

- the selected events (Calls), which are the Available events, the events with No plan, those Not reached and those with both missing;
- the Available events by class, Early, On time and Late, which add up to Available;
- for the Available events only, the sum, mean, minimum and maximum of the delays;
- for each of the thresholds 0, 60, 180 and 300 s, the share of the Available events that are strictly later, as a percentage with the count, such as "50.0% (1 of 2)". The tooltip of each header Later than N s reads "Strictly later" and "Counts available events only".

The Available events are the denominator of every mean and share. A figure without an Available event is "-", not 0. The line fixture without incidents (`follow-level-0`) gives for arrivals:

| Station | Calls | Available | Early | On time | Late | Sum | Mean | Minimum | Maximum | Later than 0 s | Later than 60 s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |
| A | 2 | 0 | 0 | 0 | 0 | - | - | - | - | - | - |
| B | 2 | 2 | 1 | 0 | 1 | -112 | -56 | -269 | 157 | 50.0% (1 of 2) | 50.0% (1 of 2) |
| C | 2 | 2 | 2 | 0 | 0 | -177 | -88.5 | -167 | -10 | 0.0% (0 of 2) | 0.0% (0 of 2) |
| All selected stations | 6 | 4 | 3 | 0 | 1 | -289 | -72.25 | -269 | 157 | 25.0% (1 of 4) | 25.0% (1 of 4) |

Station A has no planned arrival, and the trains begin their route there, so both of its arrivals are No plan, not reached. When a train does not finish, its later calls are Not reached and drop out of the figures in the same way: in `breakdown-f1-level-0` the second train does not reach C within the run, so C has two calls, one Available, and its mean is the delay of that one.

### Punctuality

A bar chart of the selected events in six classes: Early (below 0), On time (exactly 0), Up to 1 min late, 1 to 3 min late, 3 to 5 min late and More than 5 min late. A delay equal to a threshold is in the class below it, so the classes agree with the shares of the tab Summary. Each bar has its count above it, and its tooltip gives the count and the share of the Available events, such as "1 of 4 arrivals (25.0%)".

The line above the chart counts the Available events and their trains, the events that are not reached or have no plan, which are in no class, and the shares no more than 3 and no more than 5 min late, such as "4 arrivals of 2 trains; 2 not reached or without a plan. No more than 3 min late: 100.0% (4 of 4); no more than 5 min late: 100.0% (4 of 4)." Without an Available event the tab says "The selected trains and stations have no arrival with a delay." and shows no chart.

### Against baseline

The tab shows the comparison of the run with the baseline ([rules](#delay-comparison-against-a-baseline)). Without a baseline it says "No baseline is set. In Run Results, click Set delay baseline after a run without incidents or entrance delays." When the baseline does not fit the run, it says "This run cannot be compared with the baseline:", lists each reason as a sentence in the column Reason, such as "The two runs have different durations.", and shows no difference. Otherwise it shows a line of totals and two tables.

The Trains table has one row for each occurrence: the input delay (the entrance delay) and the station that it moved; whether direct incident evidence was recorded (Direct incident evidence), and the incident ids; the final station; the baseline and the scenario arrival there; the additional delay and the change (Later, Same, Earlier or Not available); and the secondary delay. The Calls table has one row for each call with the baseline time, the scenario time, the additional delay and the change, for the arrival and for the departure. The headers Input delay (s), Additional delay (s) and Secondary delay (s) have their definitions as tooltips.

Two cases of the line fixture show how to read it. In both, the baseline is `follow-level-0` and the occurrences are the services F1 and F2.

A signal failure (`sf-forward-level-0`, the incident `inc.fwd` from 400 s to 1000 s):

| Service | Input delay | Direct incident evidence | Final station | Baseline final arrival | Scenario final arrival | Additional delay | Change | Secondary delay |
| --- | ---: | --- | --- | ---: | ---: | ---: | --- | ---: |
| F1 | 0 | Yes: inc.fwd | C | 933 | 1218 | +285 | Later | +285 |
| F2 | 0 | None recorded | C | 1150 | 1373 | +223 | Later | +223 |

The line of totals reads "2 trains. At the final arrival: 2 later. Added delay +508 s, net +508 s." F1 arrived 285 s later and the run recorded that the failure held it. F2 arrived 223 s later, at B its arrival was 49 s later and its departure 136 s later, and the run recorded no direct effect on it. The window shows both numbers and does not call the 223 s propagated: the number is a difference and the evidence column says only what the run recorded.

An entrance delay (`entrance-delay-f2-level-0`, 120 s for F2 at A):

| Service | Input delay | Input delay station | Final station | Baseline final arrival | Scenario final arrival | Additional delay | Change | Secondary delay |
| --- | ---: | --- | --- | ---: | ---: | ---: | --- | ---: |
| F1 | 0 | | C | 933 | 933 | 0 | Same | 0 |
| F2 | 120 | A | C | 1150 | 1150 | 0 | Same | -120 |

In the line fixture a train is not held at the stop where it begins its route, so the 120 s do not change any simulated time. The additional delay is 0 and the secondary delay is 0 minus 120. That shows the arithmetic and not an effect on the train.

When an occurrence has no arrival at its final call in the scenario run, its additional delay, change and secondary delay are "-" and the line of totals ends with "Sums leave out trains without a final arrival." In `breakdown-f1-level-0` the second train does not reach C, and the totals count one occurrence later and one not available, with a positive and net sum of 461 s over the first: "2 trains. At the final arrival: 1 later, 1 not available. Added delay +461 s, net +461 s. Sums leave out trains without a final arrival."

### Export

**CSV...** under Export in the panel writes the visible tab, with a provenance file beside it that names the run (for Against baseline, the baseline too). The file is `delay_analysis.csv` for Calls and Summary, `delay_histogram.csv` for Punctuality and `delay_comparison.csv` for Against baseline. All times and differences are seconds. A value that does not exist is an empty field. A tab without a row cannot be exported.

- Calls holds one record for each row of the table, with the columns Service, Occurrence, Train, Operating code, Station, Journey order, Call, Event, Planned[s], Simulated[s], Delay[s], Class and Status.
- Summary holds a `station` record for each station and a `network` record, with the counts, the four figures and, for each threshold, the number of later events and its percentage.
- Punctuality holds a record for each class, with the columns Event, Class, From[s], To[s], Events and Share[%], and then one record each for No plan, Not reached and No plan, not reached, with the count and no bounds or share. A class holds the delays above From up to and including To; Early has no From and More than 5 min late has no To. The tab has no file when no event is Available.
- Against baseline holds an `occurrence` record for each occurrence (with the input delay, the direct incident evidence, its incident ids and the secondary delay), a `call` record for each call and one `total` record, with a column `Complete` that is `no` when the totals leave an occurrence out.

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

1. A call is counted when its stop has a simulated arrival and a planned arrival; otherwise it is in neither file. Every call of a repeated station is one sample. Calls at the first station of the network are computed, but no station row and not `TOTALS` shows them; they can still enter `Final_Station`. `Final_Station` takes the last stop of each train, when that stop is counted.
2. In `Stats_Stations.txt` the delay is the arrival minus the planned arrival, with early arrivals set to 0 and counted as samples. The average and the standard deviation are over the delayed samples only. `Total_Delay` sums all samples and `Max_Tot_Delay` is the largest. The shares count the samples with a delay greater than 0, 180 and 300 seconds (strictly greater), over all samples, in percent. `N_DelTrains` counts the samples greater than 0. In `Pos&Neg_Stats_Stations.txt` the same is computed with the signed delay, and the average and deviation are over all samples. The standard deviation is the sample standard deviation (n - 1) and is 0 for one sample.
3. `Cum_Cons_Delay` and `Max_Cons_Delay` use the delay minus the entrance delay of the train, and minus the dwell disturbances of earlier stops, which are always 0 because nothing sets them. The value of each call is set to 0 when negative in the first file only. `Cum_Cons_Delay` is the sum of these values over the calls of the station and `Max_Cons_Delay` the largest.
4. A row without any sample holds -1 in the average, deviation, maximum, cumulative and percentage columns and 0 in `Total_Delay` and the counts. In the signed file -1 can also be a real value, so `N_StopTrains` = 0 is the test for no sample. A 0 average in the first file can mean that no train was delayed.
5. `TOTALS` is the unweighted mean of the station values (average, deviation, shares) over the stations that have samples, the sum of their `Total_Delay` and the largest of their maxima. It is not a mean over all calls, and it has no count columns. With no sample anywhere it holds -1 in every column except `Total_Delay`, which is 0.
6. The three input rows summarise the entrance delay, and the dwell disturbance, of each train over all trains of the run; `N_StopTrains` is the number of trains and the average is over the positive values only. Their `Cum_Cons_Delay` and `Max_Cons_Delay` columns hold N/D.
7. The thresholds are divided by the timestep, which is 1.

Worked example, the arithmetic of the test case `follow-level-0` (entrance delays 0). Two trains call at two stations, S1 and S2, which are stations B and C of the line fixture; station A, where the trains enter, has no row. Train 1 arrives 269 s early at S1 and 167 s early at S2; train 2 arrives 157 s late at S1 and 10 s early at S2. The last stop of both trains is S2.

```
Stats_Stations.txt
S1 157 0 157 157 157 157 50 0 0 2 1
S2 0 0 0 0 0 0 0 0 0 2 0
TOTALS 78.5 0 157 157 157 157 25 0 0
Final_Station 0 0 0 0 0 0 0 0 0 2 0

Pos&Neg_Stats_Stations.txt
S1 -56 301.227 -112 157 -112 157 50 0 0 2 1
S2 -88.5 111.016 -177 -10 -177 -10 0 0 0 2 0
TOTALS -72.25 206.122 -289 157 -289 157 25 0 0
Final_Station -88.5 111.016 -177 -10 -177 -10 0 0 0 2 0
```

At S1 the first file has the samples 0 and 157: the average is over the one delayed sample, the share is 1 of 2. The signed file has -269 and 157: the mean is -56 and the standard deviation is 426 divided by the square root of 2, which is 301.227.

The characterization tests check that `Total_Delay` and `N_StopTrains` of every station row equal the sum and the number of the arrival delays in the timetable results (only the late ones in the sum for the first file).

## Delay comparison against a baseline

The comparison reports how much later or earlier each call of a run with incidents or entrance delays is than in a run without them. It reports the difference and the recorded direct evidence, and it never states a cause. [Reading the tab](#against-baseline) shows two examples.

**Set delay baseline** freezes the completed run as the baseline. The run must have no incidents and no entrance delays in its scenario. The baseline is kept in memory only, and it is cleared when the case changes: creating a new case, opening another one, or editing the case in any way, including adding an incident. So the incident scenario has to exist before the baseline is set. Selecting another scenario keeps it.

**Compare delays** needs a baseline and a completed run whose scenario has at least one incident or at least one entrance delay. The line above the buttons says why a button is disabled. The button opens the Delay analysis window on the tab Against baseline. The window compares the run with the baseline (`compareAgainstBaseline`) when all of these hold:

- the two runs have different scenario ids;
- they have the same case revision, base time, duration and timestep;
- they have the same set of (service, occurrence) pairs;
- each occurrence has the same calls in both runs: the same stations, journey order and call numbers;
- the baseline has no incident and no entrance delay.

When a check fails, the tab lists every reason that applies and shows no difference.

For each call the comparison takes the simulated arrival and the simulated departure in both runs and reports the [additional delay](#definitions), scenario minus baseline, with the change Later, Same, Earlier or Not available. The final call of an occurrence is its last authored call. For each occurrence it also reports the entrance delay as the input delay, the direct incident evidence and the [secondary delay](#definitions). The comparison uses simulated times only, no planned times, so it works for a case without planned arrivals.

The totals count the occurrences that are later, the same, earlier or not available at their final arrival, and give the sum of the positive additional delays and the net sum. An occurrence whose final arrival is missing in either run has no difference. It is counted as not available, the sums leave it out, and the tab says so. A difference that cannot be computed is never 0. In the line of totals, "Added delay" is the sum of the positive additional delays and "net" the sum of all of them; a count of 0 is left out.

An occurrence has direct evidence when the scenario run recorded a speed cap, a breakdown hold, a stop at the end of authority of a signal failure, or a stop at the red aspect before a failed section ([which cases](#limits), item 7). The tab shows "None recorded" or "Yes" with the incident ids. An occurrence without recorded evidence is not shown to be delayed by propagation, and no part of a difference is attributed to a cause. The line "Incidents acted on k of n trains" of the Run Results dock counts the trains with at least one recorded incident id. It shows only when View > Advanced Details is checked.

The tab exports `delay_comparison.csv` ([export](#export)).

## Early, missing and repeated cases

The tabs Calls, Summary and Punctuality of the Delay analysis window follow the column of the timetable results. They count an early arrival as Early, an exact one as On time, and a missing value as No plan or Not reached. The one difference is the entrance delay.

| Case | Timetable results | Statistics files | Baseline comparison |
| --- | --- | --- | --- |
| Early arrival | Signed negative value, for example "-269 s", dark green | 0 in `Stats_Stations.txt` (a counted sample, not delayed); negative in `Pos&Neg_Stats_Stations.txt` | Planned times are not used; a call earlier than in the baseline gives a negative difference and the change Earlier |
| Exactly on time | "+0 s", dark green | A sample of 0, counted, not delayed | A difference of 0 is the change Same |
| No planned time | The delay of that event is unavailable; planned arrival and planned departure are independent | A call without a planned arrival is not counted; a missing planned departure has no effect | Not needed |
| Train did not reach the stop | Simulated time and delay unavailable | Not counted: it adds no sample | The call and its difference are Not available; at the final call the totals leave the occurrence out and say so |
| No simulated departure (train still at its last point) | Departure delay unavailable; arrival delay shown | No effect; only arrivals are used | The departure of that call is Not available |
| Repeated station | One row per call; the n-th call takes the n-th timetable point of the name | Every call is a sample of the station row; `Final_Station` takes the last stop only | Every call has its own row; the totals use the last call |
| Entrance delay | Departure delay at the named stop is against the shifted planned departure; arrival delays are not shifted. The Delay analysis window uses the authored planned departure, so the departure shows the entrance delay | `Cum_Cons_Delay` and `Max_Cons_Delay` subtract the entrance delay from each call of the train; the row `Ent_Delays` lists it | Accepted: the entrance delay is the input delay of the occurrence. The baseline may not have one |
| Incompatible baseline | Not involved | Not involved | The tab lists every reason that applies, shows no difference and offers no export |
| Case with no planned arrival at all | Arrival delay unavailable at every stop; departure delay exists where a planned departure exists | No station has a sample: rows hold -1, `TOTALS` -1 and 0 | Works; it needs no planned time |

The Netherlands case has no `planned_arrival_seconds` and no `planned_departure_seconds` at any stop, so no event of that case has a delay in the timetable results or the Delay analysis window.

## Calculations the program no longer contains

The only delay statistics code left in the program is the code this guide describes. These calculations were removed and none has been restored. A file as it was before a commit is shown by `git show <commit>^:<path>`.

| Calculation | What it did | Names absent from the program | Removed in |
| --- | --- | --- | --- |
| Rescheduling, optimisation and dispatching | A rescheduling interface to an external dispatching tool (messages, decisions, platform and route changes) and functions that ordered services, built an optimised timetable and moved train times to it. The area capacity code removed with them computed no delay | `handleDispMessage`, `loadDispDecisions`, `checkArrivalPlatform`, `ComputeOptimisedTimetable`, `Generate_Optimised_Train_Service_Sequence`, `ResetTrainsAccordingToTimetable` | `08105b7` (#254) |
| Older delay entry point | A second way to run the arrival delay calculation, with the older arrival detector | `calculateArrivalDelayAllTrainsOldVersion` | `e41b3c3` (#319) |
| Stochastic dwell and entrance delays | Drew dwell times at stations from a Gaussian distribution around the planned dwell time and wrote them to files; read such files back and set the stop time and the dwell disturbance (stop time minus planned dwell time) of each stop; read per-train entrance delays from files and added them to the planned departure at a named station; added a Gaussian error to such entrance delays. Nothing sets the dwell disturbance, so the `DwT_Dist` row and the dwell part of the consecutive delay are always 0 | `drawGaussianTrainDwellTimes`, `Load_And_Set_Stoc_Dwell_Times`, `Load_Departure_Delay_At_Station`, `Load_Entrance_Delay_Disturbed_Scenario`, `Generate_Entrance_Delays_Affected_By_Error` | `e7fad81` (#510) |
| Older arrival detectors | Took the arrival of a stop from the position samples: the first sample at rest exactly 2 mm short of the station, or the first sample at rest less than 5 mm short of it or beyond it after a sample more than 3 m short of it. The `Actual_Arrivals` functions applied this to the stops of a train | `Arrival_At_Station`, `Arrival_At_Station_NewVersion`, `Actual_Arrivals`, `Actual_Arrivals_NewVersion` | `e7fad81` (#510: `Arrival_At_Station`, `Actual_Arrivals_NewVersion`), `fc3eeb1` (#585: `Arrival_At_Station_NewVersion`, `Actual_Arrivals`) |

Of these calculations only the older arrival detector reached a figure: the signed statistics file used it before #585, and it matched few stops, so that file held -1 in many rows (#585). The other calculations were not reached by a run. Both statistics files now take the arrival from the timetable point, as the timetable results do ([arrival and departure](#arrival-and-departure-of-a-call)).

## Supported scope

The Delay analysis window supports:

- one completed run at a time, for the selected occurrences and stations, with the delay of each arrival and each departure of an authored stop against the authored planned time;
- a summary for each station and for all selected stations, for arrivals or for departures, with the thresholds 0, 60, 180 and 300 s;
- a histogram of the delays of the selected events in six classes at the same thresholds, for arrivals or for departures;
- the comparison of that run with one baseline run, at every call and at the final arrival, when the baseline has no incident and no entrance delay and the run has incidents, entrance delays or both;
- the export of the visible tab as a CSV file with a provenance file.

For every figure the window says how many events it covers and how many it leaves out. An event without a planned time or without a simulated time has no delay and is never counted as 0.

The window does not support:

- events at stations where a train passes without a stop;
- a delay for a case or a stop without planned times: the Netherlands case has none, and an arrival delay needs a planned arrival and a departure delay a planned departure;
- another set of thresholds, more than one baseline, or a baseline that is saved with the case;
- a baseline with incidents or entrance delays, or two runs that differ in case revision, base time, duration, timestep, occurrences or calls;
- the delay of passenger journeys, which is another quantity ([limit 9](#limits));
- a cause for a delay: the window states the entrance delay, which is an input, and the direct incident evidence that the run recorded, and nothing more.

## Limits

1. The figures cover arrival and departure events of authored stops. Passing a station without a stop is not a call.
2. Four sets of thresholds are in use. The statistics files count calls later than 0, 180 and 300 s. The Timetable view colours at 0 and 60 s. The chart is in minutes with no threshold. The Delay analysis window counts events later than 0, 60, 180 and 300 s, sorts them into classes at the same thresholds, and its thresholds are fixed. All of them mean strictly later. The statistics files have no 1 minute threshold; only the Timetable view and the Delay analysis window have a threshold at 60 s.
3. Early arrivals are 0 in `Stats_Stations.txt` and negative in `Pos&Neg_Stats_Stations.txt` (the `stats` and `signed_stats` lines of `follow-level-0.txt`, as in the example above).
4. A case or a stop without a planned arrival gives no arrival delay in the timetable results, the Delay analysis window or the statistics files. A stop without a planned departure gives no departure delay in the timetable results or the window; the statistics files use arrivals only. The baseline comparison does not need planned times, and the passenger journey delay uses the planned arrival of the journey.
5. The comparison refuses a baseline that has incidents or entrance delays. A scenario may have either. One baseline exists at a time and it is not saved with the case: any edit of the case, a new case or opening another one clears it.
6. An entrance delay moves the planned departure of its stop only. The departure delay at that stop in the timetable results is against the moved planned departure. The Delay analysis window measures it against the authored one.
7. Direct incident evidence is recorded for a speed cap (`effectiveIncidentSpeedLimit`), a breakdown hold, and a train held by a signal failure (`recordDirectSignalFailure`). A train is held by a signal failure when it stops at the end of authority of the failure, or when it stands at a red aspect within 4 m of the failed section that is the next section on its own route; a train that stands there for a planned stop is recorded too. The second rule covers the fixed-block levels 0, 1 and 2 (see [signalling levels](signalling-levels.md)). It does not cover level 5, whose restrictive aspect is red-red. A train that is held only by the train ahead is not recorded, and neither is a train that waits to enter while the first section of its route is failed. So the evidence depends on the signalling level. The signal failure cases of the line fixture show it (`direct_incident count` in the files of `tests/characterization/expected` whose names start with sf-):

   | Signalling level | Trains with evidence |
   | --- | --- |
   | None | Both trains in `sf-adjacent`, `sf-forward`, `sf-last`, `sf-reverse` and `sf-staggered` |
   | 0, 1 and 2 | The first train of the direction in the same five cases and in `sf-retarget-level-0`; the second train in none of them |
   | 3 and 4 | The first train of the direction in `sf-forward` and `sf-reverse` |
   | 5 | No train |
   | Any | No train in `sf-first` at any level, in `sf-late` at the levels none, 0 and 3, or in `sf-entered-level-none` |

   For a run in which no train has evidence, with advanced details on, the Run Results line says "Incidents acted on 0 of n trains" and the tab Against baseline shows "None recorded" for every occurrence. A train that is delayed because the train ahead was held shows its difference and no evidence. This describes the current behaviour of the program and is not a statement about the railway.
8. The **CSV...** button of the Delays chart writes the timetable file for the selected trains, not a delay file.
9. The passenger journey delay is another quantity: it is measured against a planned arrival drawn from a window, not against the timetable of the train.
10. None of the bundled cases has an incident or an entrance delay; each has only the baseline scenario, so the baseline comparison cannot be tried on a bundled case.
11. Nothing in the application attributes a delay to a cause beyond the recorded direct evidence.
12. The pairing of stops and timetable points is by name and order only. It is exact when the n-th stop of a name takes place at the n-th station node of that name on the route; the run does not check this.
