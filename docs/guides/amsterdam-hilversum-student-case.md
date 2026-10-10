# Amsterdam to Hilversum student case

You plan a half-hourly service from Amsterdam Centraal to Hilversum. You test a supplied timetable, try a train with lower running performance, move one planned time, and recommend a configuration. You hand in the case you recommend, the exported results behind it, and a short written recommendation. Every step uses the application window; you edit no files by hand.

## What the case is and is not

The infrastructure, stations, rolling stock and scenarios are those of the Netherlands case that ships with EGTRAIN. `Asd` and `Hvs` are the station ids of Amsterdam Centraal and Hilversum. The background services of the Netherlands case are left out, so the only trains are the four runs of one service.

The timetable is fictional and made for teaching. `SLT06` is the only composition of the case and a technical stand-in, not validated intercity rolling stock. The signalling is one network-wide area of level 0, assigned by project decision. Use the results only to compare the configurations of this exercise. They do not describe real operation, and the exercise is not a capacity study.

## Supplied inputs

The service `IC_ASD_HVS_DEMO` runs with composition `SLT06` on `route32` from `Asd` to `Hvs`, with no other scheduled stop. The editor counts times in seconds from the case base time, 06:28:20. The case duration is 8000 s.

| Input | Seconds from base time | Clock time, run 1 |
|---|---|---|
| Entry time | 100 s | 06:30:00 |
| `Asd` planned arrival | 120 s | 06:30:20 |
| `Asd` planned departure | 180 s | 06:31:20 |
| `Hvs` planned arrival | 1500 s | 06:53:20 |
| `Hvs` planned departure | 1560 s | 06:54:20 |

The minimum dwell is 60 s at both stops and the running performance is 100 %. The service repeats every 1800 s for 4 runs. Entry time and planned times of each later run are 1800 s (30 minutes) later than those of the run before.

## Open the case and keep your own copy

1. Click **Open Case** in the toolbar. In the dialog **Open a Case**, select **Amsterdam_Hilversum_Student** under **Bundled cases** (the first entry) and click **Open**. The network appears and the window title names the case. If the list does not show the case, open the downloaded `Amsterdam_Hilversum_Student.egscene` with **File > Open Case Study...** instead; see [Opening an `.egscene` case study](opening-a-case-study.md).
2. Before you change anything, choose **File > Save Case Study As...** and save a `.egscene` file in a folder of your own. The dialog starts in the folder of the open case (for the bundled case, a folder of the application), so pick a folder of your own. The status bar reads **Scene saved** and your file is the open case.
3. From then on **File > Save Scene** saves to your file. Do not use it before step 2: on the supplied case it writes back into the place the case was loaded from, with no question asked.

The saved file holds the inputs of the case. It holds neither results nor the selection of runs.

## Tasks

### 1. Inspect the inputs

1. Choose **Editors > Services**. The panel **Services** opens with the tabs **Service and timetable** and **Run occurrences**. Choosing the entry while the panel is showing hides it. You can use the panel only while no run is active.
2. On **Service and timetable**, read **Composition**, **Route** (the entry that ends in `[route32]`), **Entry Time (s)**, **Repeat Headway (s)**, **Configured total**, **Running performance (parameter) %** and **Maximum speed restriction km/h**. A field beside a check box works only while its box is checked.
3. Read the table **Timetable stops**: **Timetable stops (station)**, **Stop platform**, **Minimum dwell (s)**, **Planned arrival** and **Planned departure**. Switch **Planned time display** between **Elapsed offsets (s)** and **Clock time**. This changes how the times are shown, not the case. Leave it on **Elapsed offsets (s)**. Neither stop names a platform.
4. A click on a row of the table opens the dialog **Edit timetable stop**. Choose **Cancel** unless you mean to change the stop.
5. Open the tab **Run occurrences**. The text above the table starts with "Configured total: 4; Number of services in sim.: 4; Selected: 4; Selected in period: 4." The table has the columns **Include**, **Service code (number)**, **Generated service**, **Scheduled entry**, **Running performance (parameter) %** and **Maximum speed restriction (km/h)**.
6. Choose **Editors > Case Settings** and read **Base time** and **Duration / horizon (s)**. Do not change them.

Explain the difference between the one service definition and the four generated runs. Name the inputs that are assumptions of the exercise, not sourced railway data.

### 2. Establish a baseline

Run 1 alone at 100 % performance.

1. On **Run occurrences**, click **Select none**, then tick **Include** in the first row, service code `IC_ASD_HVS_DEMO-1`. The text above the table contains "Selected: 1" and "Selected in period: 1". Highlighting a row is not enough; the box must be ticked. **Run** refuses to start while no run is selected.
2. Click **Run** in the toolbar. The dialog **Run simulation** shows **Selected in period**, **Start clock**, **Duration** and **Active incidents**; **Run details** shows more. Check that **Selected in period** is 1, then click **Run simulation**.
3. Move the speed slider, which sits between **Slower** and **Faster**, towards **Faster** to shorten the wait. Do not click **Stop**: a run you stop before it ends leaves no results. When the run has ended, the dock **Run Results** appears and the line at its top contains "Status: Completed".
4. In the dock, click **Open result view...** and choose **Timetable**. The window lists, for each stop of the run, the planned and simulated arrival and departure and the arrival and departure delay. Click **Export CSV...** in that window and save `baseline-timetable.csv` in your own folder. Close the window.
5. Choose **Open result view...** > **Speed / distance**, click **Export PNG...** in that window and save `baseline-speed-distance.png`. Close the window.
6. Click **Export CSV...** in the dock and save `baseline-run-summary.csv`. The two export buttons of the dock write the summary table only; every result window has its own **Export CSV...** and **Export PNG...**.

Every edit of the case removes the current results and hides **Run Results**, so finish all exports of a run before you change an input.

The arrival delay is the simulated arrival minus the planned arrival, so a negative value is an early arrival. The table of **Run Results** has one row per run and a last row **Network total**. **Start time (hh:mm:ss)** and **End time (hh:mm:ss)** show clock times; **Travel time (h:mm:ss)** shows a duration. **Export PNG...** uses these displayed formats. **Export CSV...** writes these three columns as numeric seconds, with start and end relative to the simulation start.

Record the arrival and departure delays at `Asd` and `Hvs`. Separate the time from `Asd` departure to `Hvs` arrival, read from **Timetable**, from the whole modelled **Travel time (h:mm:ss)** of the run. Use **Speed / distance** to explain the movement.

### 3. Compare lower performance

1. In the panel **Services** (**Editors > Services**, if it is closed), open **Service and timetable** and set **Running performance (parameter) %** to 60. Press Tab to finish the edit. Change nothing else.
2. On **Run occurrences**, check that the text still contains "Selected: 1" and that the column **Running performance (parameter) %** shows 60. If the selection differs, repeat step 1 of task 2.
3. Run and export as in task 2, with file names that start with `slower`.
4. Compare the `Hvs` arrival delay, the whole **Travel time (h:mm:ss)**, **Maximum speed (km/h)** and **Energy consumed (kWh)** with the baseline. Use the same energy column in every comparison.

The percentage multiplies the tractive effort at every speed and the maximum speed of the train. It does not change the mass or the braking effort. Read the journey time from the results; do not derive it from the percentage.

Optional: choose **Open result view...** > **Tractive effort / distance** and compare it with **Speed / distance**. It shows the tractive effort the run applied, in kN, not the traction curve of the rolling stock unit. Export a PNG if you want it in your hand-in.

**Set delay baseline** and **Compare delays** compare a run with incidents against a baseline run. This exercise does not use them.

### 4. Revise the timetable

Keep the performance at 60 and move the `Hvs` planned times two minutes later, keeping the 60 s planned dwell.

1. In the panel **Services**, open **Service and timetable** and click the `Hvs` row in **Timetable stops**.
2. In **Edit timetable stop**, the time fields hold seconds from the base time while **Planned time display** is **Elapsed offsets (s)**, and HH:MM:SS clock times when it is **Clock time**. Set **Planned arrival** to 1620 and **Planned departure** to 1680, which are 06:55:20 and 06:56:20 for run 1. Leave both boxes ticked and **Minimum dwell (s)** at 60. Click **OK**.
3. Check the table: `Hvs` shows 1620 and 1680, `Asd` still 120 and 180. Entry time, headway, total, composition and route stay as supplied. Check the selection of run 1 as in step 2 of task 3.
4. Run and export as in task 2, with file names that start with `slower-revised`.

Explain whether the changed delay comes from faster movement, a changed target or departure holding. A train leaves a stop only after its minimum dwell has passed and its planned departure time is reached, so read the arrival and the departure columns separately. A negative arrival delay is an early arrival, not a failure of the simulation.

### 5. Check the repetition

1. On **Run occurrences**, click **Select all**. The text contains "Selected: 4" and "Selected in period: 4". Keep the performance at 60 and the `Hvs` times at 1620 and 1680.
2. Run as in steps 2 and 3 of task 2. **Selected in period** in **Run simulation** is 4.
3. Open **Timetable**. Its train filter button reads **Trains (4/4)** while all four runs are shown. Check that the planned times shift by 30 minutes from run to run and that both stops of every run have simulated times. Note whether the **End time (hh:mm:ss)** of every run in **Run Results** is before 08:41:40, the base time plus the duration of 8000 s. The row **Duration** in **Run simulation** shows the same end.
4. Click **Export CSV...** in the timetable window and save `revised-four-runs-timetable.csv`.

Report results per run. The **Network total** row spans from the earliest start to the latest end of the selected runs and sums the energy columns, so its **Travel time (h:mm:ss)** is not the travel time of one train. This is a check of the repeat pattern, not a capacity study.

### 6. Recommend and hand in

1. Choose a configuration: the performance, the `Hvs` planned times and the number of runs.
2. Set the open case to that configuration and choose **File > Save Case Study As...** with a new file name. The file keeps **Configured total** from **Service and timetable**, not the ticks on **Run occurrences**, so state in your recommendation which runs you ran. Your first copy keeps what you last saved in it.
3. To check your file, choose **File > Open Case Study...** and select it. Check the performance and the `Hvs` times on **Service and timetable**. Opening a case resets the selection of runs, so select the runs again on **Run occurrences**, run, and compare with your exports.

Hand in:

- The `.egscene` file of the recommended case.
- The timetable CSV and the run summary CSV of the baseline, the slower original plan and the slower revised plan, each for run 1 only.
- The speed and distance PNG of the baseline and of the slower train. The tractive effort PNGs are optional.
- The timetable CSV of the four-run check.
- Your recommendation.

Each export also writes a file with the same name plus `.provenance.json` next to it. Keep it with its export. Keep the exports separate from the `.egscene` file.

## Recommendation template

1. **Chosen configuration:** performance, `Hvs` planned times and number of runs.
2. **Evidence:** arrival delay, whole run time and energy for each comparison, with units.
3. **Interpretation:** what changed in the movement, what changed only as a planning target, and any departure holding.
4. **Limitations:** fictional timetable, placeholder stock, left-out background traffic, and capacity and blocking-time conclusions that are not validated.
5. **Next investigation:** one source-backed input or further test you would require before an operational recommendation.

## Limits of the exercise

The case has no validation errors. With **View > Advanced / Developer details** checked, the status bar shows "Validation: 0 error(s), 26 warning(s)". All 26 warnings have the code `scene.route.region_jump` (a route crosses an undeclared regional coordinate discontinuity). They concern routes other than `route32` and are inherited from the Netherlands case. By default the status bar shows nothing for a case without errors.

If a train meets planned times that you set yourself, that does not prove the plan is feasible in real operation.
