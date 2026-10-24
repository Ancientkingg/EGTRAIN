# Glossary of application terms

This page says what the words of the program and of its documents mean. Every entry says what a word means in EGTRAIN and where a user meets it: in a menu, a window, a field or a file. Railway words are explained only as far as the documents of this repository define them; [What is not established](signalling-levels.md#what-is-not-established) says what they do not claim. The schema and the guides stay the authority, so an entry links to them instead of repeating them.

## Cases and files

### Case

A case is what the program opens, edits, runs and saves: everything a run needs, that is infrastructure, signalling, rolling stock, services with timetables, scenarios and, optionally, passengers. It is stored as a case folder or as a bundle. You meet it in **File > New Case...**, **File > Open Case...** (which opens a bundle), **File > Save As...**, the dock **Case and Layers**, which names the open case, and the command-line option `-n` ([bundled case numbers](command-line.md#bundled-case-numbers)). The toolbar button **Open Case** shows the chooser **Open a Case**, which lists "Bundled cases" and "Recent cases" and has the button **Other ways to open...**.

### Case folder

A case folder is a case as the program reads it: a folder of JSON files. The required files are `scene.json`, `infrastructure.json`, `stations.json`, `signalling.json`, `rolling_stock.json` and `services.json`, and `scenarios.json`, `passengers.json` and `views.json` are optional on load ([case folder](scene-schema.md#case-folder), [contents of the files](scene-schema.md#version-and-files)). You meet it in **File > Open Case Folder...**, **File > Save** (it writes back to an ordinary open folder or bundle; for a bundled case or a new case without a location it asks for a new bundle name, like **File > Save As...**) and **File > Save As Folder...**. The word "scene" names only the format of these files and the tools that read them, as in `scene.json`, the [scene schema](scene-schema.md) and `scene_tool`. "Canonical" means this form: the case folder is the editable source of truth, loading, validating and running use only it, and legacy files enter or leave only through the explicit import and export.

### Bundle

A bundle is one file for moving a case: a ZIP archive that holds the JSON files of a case folder and has the extension `.egscene`. It holds no results and no legacy input. **File > Save As...** and `scene_tool pack` make it, and **File > Open Case...**, the chooser, the option `--scene`, a file name on the command line and a double-click in Explorer or Finder open it. The folder stays the editable source format and the bundle is its transport form ([opening a case](opening-a-case-study.md), [bundle format](scene-bundle.md)).

### V1 and V2

V1 and V2 are names of formats that appear in page titles and file names. V1 is the scene data, a folder of JSON files numbered by `schema_version` in `scene.json`, and V2 is the bundle container, numbered by `bundle_version`. The two numbers are independent. A reader of the program needs only the words case folder and bundle; see [the bundle page](scene-bundle.md).

### Scene version

These version numbers are independent: the version of the application, the `schema_version` of the scene data and the `bundle_version` of a bundle. The application version that a case was saved with is provenance and does not by itself lead to an upgrade offer. When you open a case the program checks the versions and never changes the file, and a case that it can open opens directly. Otherwise it shows one of these dialogs: **Older Case Not Supported**, **Newer Case** (with the button **Check for Updates...**), **Cannot Open Case** (the message names the first problem) or **Older Case**. **Older Case** offers **Upgrade a Copy...**, which writes an upgraded copy and leaves the original unchanged, and it appears only for a version that has a registered migration. [Cases that do not open](opening-a-case-study.md#cases-that-do-not-open) describes the dialogs.

### Legacy case

A legacy case is the input format of earlier versions: a folder of text files, for example `Routes/Route<N>.txt`, with a train definition as one line of whitespace-separated tokens. The program does not run it. **File > Import Legacy Case...** (also **Other ways to open... > Import Legacy Case...** in the chooser) and `scene_tool import` convert it into a new case folder at a destination you choose and leave the source unchanged; what could not be converted is listed in `import_report` in `scene.json`, and the dock **Loaded Data** shows its counts. `scene_tool export` writes legacy files from a case for other tools, and edits to them do not flow back. See [convert a legacy case](scene-schema.md#convert-a-legacy-case), [scene_tool](command-line.md#scene_tool) and the [migration matrix](v1-scene-migration-matrix.md), which says what each legacy file becomes.

### Provenance

Provenance names two things. Each export of results (**Export CSV...**, **Export PNG...**) writes a file with the same name plus `.provenance.json` beside it; it records the run that produced the export, and [Random seed](command-line.md#random-seed) names the seed that it holds. Rolling stock units carry descriptive source references, shown as **Parameter file** and **Traction file** in the dock **Rolling Stock Units** (the keys `data_file` and `traction_file` under `source`). The program does not reopen those files for a run ([review what loaded](main-window.md#review-what-loaded)).

### Views file

`views.json` is optional display data: the drawing level, region and visibility of tracks and the coordinates of stations in the network view. It changes nothing in topology, signalling, services or results, and folders and bundles keep it. The "level" in it is a drawing level and has nothing to do with a signalling level. See [views.json](scene-schema.md#viewsjson-and-runtime-metadata).

### Output folder

The output folder is the folder where a run writes its text output. **File > Set Output Folder...** chooses it for the next run. [Output folder](command-line.md#output-folder) gives the default and the environment variable that replaces it. [Run output files](run-output.md) lists the files of a run. The exports (**Export CSV...**, **Export PNG...**) ask for their own path and are not written there.

## Checking, running and results

### Validation

Validation is the program's check of a case; each finding is a diagnostic with a severity (error, warning or info), a code such as `scene.ref.unresolved`, a message, a file and path and a suggested fix. Its layers are structural (files, JSON shape, required sections), semantic (references, identifiers, values, timetables, incidents) and runnable (the minimum a run needs). It runs when a case opens, after each edit, after a save and just before a run: an error disables **Run**, warnings and info do not, and **Save** stays allowed with semantic errors. You meet it in the dock **Case Validation** (**View > Case Validation**; columns **Severity**, **Code**, **Message**, **File**, **Path** and **Fix**) and in `scene_tool validate`, where `--runnable` applies the check that Run applies.

### Run

A run is a simulation of the open case with the selected scenario and the ticked occurrences. You start it with the toolbar button **Run**, **Simulation > Run** (Ctrl+R) or **File > Run**. They are disabled while no case is loaded, while a run is active and while validation reports an error; with no occurrence ticked, starting a run shows the message `Select at least one occurrence on the Run occurrences tab of Services.`, titled **Cannot Run Case** on Windows and Linux. Otherwise the dialog **Run simulation** shows **Trains**, **Start time**, **Duration** and, when the scenario has any, **Incidents**, and its button **Run simulation** starts the run. A run uses the case as it is in the window, including unsaved edits.

### Runtime

The runtime is the working data that a run builds in memory from the case: the infrastructure and signalling, the services and their trains, the selected scenario and the passengers. It is not part of the case. The dock **Loaded Data** (**View > Loaded Data**; columns **Category**, **Source**, **Count** and **Status**) shows its state in the row "Run": "Not run" (after a case is opened or edited), "Ready", "Running", "Completed", "Stopped" or "Failed". In the same dock "Not present" marks an optional part that the case does not have, for example an optional file that the case can do without ([review what loaded](main-window.md#review-what-loaded)).

### Playback

Playback is what you watch during a run: the trains move on the canvas as the simulation produces steps. **Pause** (the button then reads **Resume**), **Stop** and the speed slider, with the speed shown next to it, control it, and the tooltip of the slider reads "Simulation speed", and the readout beside it shows **Fastest** or a factor such as `4.0x`. A run that you stop before its end leaves no results. Playback is not the replay and not the results; [Replay a completed run](how-to.md#replay-a-completed-run) says what a stopped run leaves.

### Replay

The replay is the row under the network that appears after a successful run. It has the buttons **Start**, **Play** (which then turns into **Pause**) and **End**, a slider, the clock time of the shown frame and a line that states the interval between frames. It shows the recorded frames of the latest completed run: a position shows the last recorded frame at or before the chosen time. A stopped run has none, and a new run or any edit of the case clears it ([Replay a completed run](how-to.md#replay-a-completed-run)).

### Results

Results are what a completed run leaves in memory: the dock **Run Results** (one row per train and a last row **Network total**, with the buttons **Open result view...**, **Export CSV...**, **Export PNG...**, **Set delay baseline** and **Compare delays**) and the result windows, which open from the **Diagrams** menu or from **Open result view...**. The line `Occurrences` at the top of the dock counts the ticked occurrences against all configured ones, as in `Occurrences: 196/196 selected`. When some ticked occurrences enter outside the run period, it adds the number that are inside, as in `Occurrences: 196/196 selected, 185 in the run period`; that number is the **Trains** of the dialog **Run simulation**. The entries that open them stay disabled until a run has completed. Results are not part of the case, so saving the case does not save them; any edit of the case, creating or opening a case and selecting another scenario discard them, and only a new run rebuilds them. A run also writes text files to the output folder ([scenario library and student loop](scenarios.md), [what this guide covers](delay-analysis.md#what-this-guide-covers)).

### Normal mode and advanced mode

**View > Advanced Details** (a check mark that the program remembers) switches between normal mode and advanced mode. Advanced mode adds technical inventories, validation details, non-blocking warnings and, after a case opens, the dock **Loaded Data**. It changes the presentation only: not what **Run** requires, what **Save** writes or what a run produces.

## Scenarios and incidents

### Scenario

A scenario is a named set of incidents and entrance delays; a case can hold several of them, and a scenario is not a bundle. They are stored in `scenarios.json`, where `default_scenario_id` names the default one, and infrastructure, rolling stock and services are shared by all of them. A run applies one scenario: the selected one, otherwise the default one. You meet scenarios in the tab **Scenario** of the dock **Scenarios** (**Editors > Scenarios**), under a line that names the selected scenario: the list "Scenarios" (accessible name "Scenario library") with **New**, **Duplicate**, **Delete**, **Import...**, **Export...** and the fields **Scenario ID**, **Name** and **Description**, and in the dialog **Run simulation**, which shows the case name and the scenario ID under its title ([scenarios.json](scene-schema.md#scenariosjson), [scenario library](scenarios.md)).

### Baseline

The word baseline has two meanings. The baseline scenario has the ID `baseline` and the name "Baseline"; a new case starts with it, with no incidents and no entrance delays, and loading a case without `scenarios.json` creates a scenario with that ID and name. The delay baseline is a completed run without incidents and entrance delays that you freeze with **Set delay baseline** in the dock **Run Results**, so that **Compare delays** can compare a later run with incidents or entrance delays against it. An edit of the case, creating a case and opening another one clear the delay baseline, and selecting another scenario does not ([delay comparison against a baseline](delay-analysis.md#delay-comparison-against-a-baseline)).

### Incident

An incident is a disturbance in a scenario. In the dock **Scenarios** the tab **Incidents** has the fields **Type** (**Signal failure** or **Train breakdown**, stored as `signal_failure` and `train_breakdown`), **Target**, **Start (s)**, the box **End (s)** with its field, **Reduced speed** and **Terminate at destination**; a box stands in front of the field it switches on. The icon button **Pick** beside **Target** takes the target from the next signal head or train that you click on the network view, and **Add Incident** takes it from the one that is selected there. **Add signal failure** in the context menu of a signal head, **Add train breakdown** in that of a train after a run, and the same button in the information panel of either add an incident with that target and open it on this tab ([compare a scenario](how-to.md#compare-a-scenario-with-a-baseline)). A signal failure needs an end; from its start to its end the sections of its target count as occupied and a stop target lies before them ([common behaviour](signalling-levels.md#common-behaviour)). A train breakdown acts on a service and may name one occurrence: it holds the train in place from its start to its end, or, with a **Reduced speed**, limits its speed, and a cap without an end lasts until the train reaches the end of its route. **Terminate at destination** marks the occurrence when it reaches its authored route end ([scenarios.json](scene-schema.md#scenariosjson)).

### Entrance delay

An entrance delay is a number of seconds that a scenario adds to the planned departure of the named stop of one occurrence of a service. Planned arrivals stay as authored. It is an input of a scenario, not a result, and **Compare delays** shows it as the input delay of the occurrence. You meet it on the tab **Entrance delays** of the dock **Scenarios**, with the fields **Service**, **Occurrence**, **Station** and **Delay (s)** ([units and the reference timetable](delay-analysis.md#units-and-the-reference-timetable)).

## Services, timetables and rolling stock

### Service

A service is a train service as the case defines it: an ID, a composition, a route, stops, and optionally an operating code, an entry time, a repetition, a performance, a speed cap, a category and a colour. You meet it in the dock **Services** (**Editors > Services**), tab **Service**, where **Service ID** is the unique key that other files use. A service can run more than once, and each run is an [occurrence](#occurrence) ([services.json](scene-schema.md#servicesjson)).

### Service code

The editor label **Operating code** is the key `operating_code`; the results and the delay comparison use the same name. It defaults to the service ID and need not be unique. For a repeated service each occurrence has its own code: the base code followed by the occurrence number, or, when the box **Code step** is ticked and the base code is digits only, a stepped number, for example 1723, 1725, 1727 ([services.json](scene-schema.md#servicesjson)).

### Occurrence

An occurrence is one generated run of a service. Its identity is the pair of the service ID and an occurrence number counted from 1, written `<service ID>-<number>` where a single string is needed, and in the results one occurrence is one **Train**. A service without **Repeat every (s)** has one occurrence; with it, the number is the value of the box **Repeat count** if that box is ticked, otherwise the duration divided by the headway, rounded up ([services.json](scene-schema.md#servicesjson)). You meet occurrences on the tab **Run occurrences** of the dock **Services**: one row per occurrence with the columns **Run**, **Operating code**, **Route**, **Entry**, **Performance (%)** and **Maximum speed (km/h)**, the buttons **Select All** and **Select None**, and a line such as "3 of 4 selected, 2 in the run period". It counts the ticked occurrences of all occurrences of all services, and, when they differ, the ticked ones whose scheduled entry lies from zero up to, but not including, the duration. **Run** needs at least one ticked occurrence, and the ticks are not saved with the case and are reset when a case is created or opened.

### Entry time

The entry time is the second at which an occurrence of a service enters its route, counted as elapsed time. It is the key `entry_time_seconds` and the box **Entry time (s)** (the field works only while the box is ticked). When it is not set, the planned departure of the first stop is used, and zero when that is absent too; a repeated service adds (n - 1) times the headway, where n is the occurrence number. **Entry** on the tab **Run occurrences** shows the result ([units and the reference timetable](delay-analysis.md#units-and-the-reference-timetable)).

### Headway

In the editor the headway is the time between the entries of consecutive occurrences of one service: the key `repeat.headway_seconds` and the box **Repeat every (s)**. Occurrence n is shifted by (n - 1) times the headway, for its entry and for its stop times ([services.json](scene-schema.md#servicesjson)). The capacity analysis window uses the word for results of an ordered pair of trains, not for this setting; see [Capacity analysis](#capacity-analysis).

### Timetable stop

A timetable stop is one scheduled call of a service at a station: the station, an optional platform, **Planned arrival**, **Planned departure** and **Minimum dwell (s)** (the keys `station`, `platform`, `planned_arrival_seconds`, `planned_departure_seconds` and `dwell_seconds`). Arrival and departure are each optional, where blank means absent and 0 is the simulation zero, and both are planned input counted from the base time of the case, never results ([edit and validate](editing-a-case.md)). You meet stops in the table on the tab **Timetable** of the dock **Services** (columns **Station**, **Stop platform**, **Minimum dwell (s)**, **Planned arrival** and **Planned departure**). The dialog **Edit Timetable Stop** (**Add Timetable Stop** for a new stop) opens from **Add Stop**, **Insert After Selected** or a click on a row.

### Dwell

The dwell is the time that has to pass before a train that has reached a stop can leave it: the key `dwell_seconds` and the field **Minimum dwell (s)**, which is required and not negative. A train that has reached a stop leaves it no earlier than the dwell has passed and no earlier than the planned departure of the stop, when it has one. Passengers who board or alight can make the stop longer than the dwell, never shorter, and a stop where nobody boards or alights lasts exactly the dwell ([the rule](scene-schema.md#servicesjson)). A train that begins its route at a stop is not held there ([units and the reference timetable](delay-analysis.md#units-and-the-reference-timetable)).

### Elapsed time and clock time

Planned times and entry times count elapsed seconds from the base time of the case (the key `base_time`, the field **Base time**, written `HH:MM:SS`; the rules are in [edit and validate](editing-a-case.md)). Clock time is the base time plus the elapsed seconds: with the base time 08:00:00, 90 seconds is 08:01:30. **Planned time display** in the dock **Services** and in the stop dialog chooses **Elapsed offsets (s)** or **Clock time**, and switching changes the display only, not the stored seconds. A later day is written with the prefix `+1d`, and passenger windows count differently (see [Passenger](#passenger)).

### Case settings

The dock **Case Settings** (**Editors > Case Settings**) holds **Name**, **Description**, **Base time**, **Duration (s)**, **Buffer (s)** and **Recovery (%)**; in `scene.json` they are `name`, `description`, `base_time` and, under `simulation_settings`, `duration_seconds`, `buffer_time_seconds` and `recovery_time_percent`. The duration is the length of the simulated period (the option `-h` calls it the simulation horizon), and the occurrences counted "in period" are those whose scheduled entry lies within it. Buffer and recovery are numbers that the blocking-time calculation reads, and `-b` and `-c` replace them for a command-line start. **Recovery (%)** has nothing to do with **End (s)** of an incident ([scene.json](scene-schema.md#scenejson), [options](command-line.md#options)).

### Performance and speed cap

Performance and speed cap are two settings of a service. **Performance (%)** (`performance_percent`; the default is in [services.json](scene-schema.md#servicesjson)) scales the available tractive effort and the commanded maximum speed, not braking, mass or composition data. **Maximum speed (km/h)** (`maximum_speed_kmh`, a box) caps the speed: the commanded maximum is the composition maximum limited by the cap, then multiplied by the performance. **Performance (%)** and **Maximum speed (km/h)** in **Run Results** show the applied values per train, and the **Reduced speed** of a train breakdown is a different, temporary limit.

### Rolling stock unit and composition

A rolling stock unit (`train_units[]`; the files and the schema call it a train unit) has physical values, a traction curve of rows `v_lower`, `v_upper`, `c0`, `c1`, `c2` and descriptive source references. A composition is an ordered list of units, and a service names one composition. You meet them in the docks **Rolling Stock Units** and **Compositions** (**Editors > Rolling Stock Units**, **Editors > Compositions**). The traction curve is input: the button **Plot Traction Curve** draws it, and that plot shows rolling stock input, not simulated effort, while the result view **Tractive effort / distance** shows what the run applied ([rolling_stock.json](scene-schema.md#rolling_stockjson), which lists the keys).

### Passenger

A passenger record is optional input in `passengers.json`: passengers with journeys, where a journey has an origin station and a destination station, a planned departure window and a planned arrival window, and legs. A leg rides one occurrence of a service from one station to a later one. The windows are absolute seconds from midnight, not from the base time. You meet them in the dock **Passengers** (**Editors > Passengers**; lists **Passengers**, **Journeys** and **Legs**) and in the option `-pax` ([passengers.json](scene-schema.md#passengersjson), [options](command-line.md#options)).

## Network and signalling

### Infrastructure entities

The dock **Infrastructure** (**Editors > Infrastructure**) has a chooser above the table with one entry for each row of this table, in the order of the chooser.

| Entity | What the documents say | Stored in |
| --- | --- | --- |
| Tracks | `id`. Nodes, arcs and blocks name their track; positions along a track are in km. | `infrastructure.json` |
| Nodes | A point of a track, with `x_km` and `y_km`. | `infrastructure.json` |
| Arcs | Join two nodes of a track (`from`, `to`) and carry the curvature radius, the gradient (percent, used unchanged as rise per length) and the speed limit (m/s). | `infrastructure.json` |
| Blocks | Follow each other along a track, each with `length_km`. A block is the part of a track that a train occupies and the unit in which aspects are written ([block sections](signalling-levels.md#signalling-areas-block-sections-and-capacity-areas)). A block ID cannot contain "/". | `infrastructure.json` |
| Connections | Join two nodes (`from`, `to`), with an optional speed limit; a connection makes a switch section for each pair of blocks that hold its two nodes ([how an area gives sections their level](signalling-levels.md#how-an-area-gives-sections-their-level)). | `infrastructure.json` |
| Stations | `id`, `name`, an optional `position_km` and platforms. | `stations.json` |
| Platforms | `id`, node IDs and an optional `length_m` and `width_m`, which set the passenger capacity ([stations.json](scene-schema.md#stationsjson) gives the defaults). | `stations.json` |
| Signals | `id` and an optional `protected_section`, used to resolve the targets of signal failures; the heads on the canvas show the aspect of a section (see [Signal head](#signal-head)). | `signalling.json` |
| Signalling area | See [Signalling area](#signalling-area). | `signalling.json` |
| Routes | `id` and `blocks` in authored order, an optional `corridor` and `reversed`; a service names a route by its ID. | `signalling.json` |
| Block dependencies | A pair `block` and `depends_on`. | `signalling.json` |
| Single-track restrictions | `start_block`, `end_block`, `protected_start_block` and `protected_end_block`; they keep trains of opposite directions out of a stretch ([single-track restrictions](scene-schema.md#single-track-restrictions)). | `signalling.json` |
| Station boundaries | `entrance_block`, an optional `exit_block` and an optional `direction`; the simulation can mark the entrance section as occupied ([what happens in a step](signalling-levels.md#what-happens-in-a-step)). | `signalling.json` |

### Section

A section is the unit of signalling at run time: a block of one track, or a switch section that a connection builds from two blocks. It is not stored in the case, because the program derives it from tracks, blocks, arcs and connections. A section has a signalling level or none, a code and a state, and the validation warning `scene.signalling.level.missing` names route sections that have no level ([what a signalling level is](signalling-levels.md#what-a-signalling-level-is), [block sections](signalling-levels.md#signalling-areas-block-sections-and-capacity-areas)).

### Ids and labels

The case names tracks, blocks and sections by ids. Where the program lists or names a section, it shows a label that it derives from the layout instead.

| Name | Example | What it is |
| --- | --- | --- |
| Track id | `B0` | The `id` of a track in `infrastructure.json`. An imported case calls its tracks `B` and the number of the legacy folder. |
| Block id | `0-B0` | The `id` of a block. An imported block is named by its row in the legacy file, counted from 0, and its track. A block that the editor adds is called `block`, `block_2` and so on. |
| Block section id | `@0-B0@` | The block id between `@` signs. The run and the exports name a block section by this id; routes and incident targets may name it by its block id. |
| Switch section id | `@71-B0@-21.235000/@0-B1@-21.252000` | The two blocks that hold the nodes of a connection, each followed by `-` and the kilometre of its node. The `-` is a separator, not a minus sign: this is block 71 of track B0 up to the node at 21.235 km, then block 0 of track B1 from the node at 21.252 km. |
| Label | `B0 0.000 to 0.230 km`, `B0 21.010 to 21.235, B1 21.252 to 21.450 km` | The track and the kilometres of the two ends of a block section, or the piece on each track of a switch section, with three decimals. |
| Short block id | `71-B0`, `71-B0/0-B1` | The block ids of a block section or a switch section, joined with `/`. The section strip of the blocking-time charts and the tables of the capacity windows show it, and the block lists of the result windows give it as a tooltip. |
| Track number | `0` | The number that a run gives a track: its place in the list of track ids sorted as text. It matches the digits of the track id only in cases with at most ten tracks: in Netherlands, B10 is track 2. The information panels of a run show the track id, with this number in the tooltip. |

The section choices of the dock **Infrastructure** and the **Target** list of a signal failure show labels, with the id as the tooltip of an entry. The block lists of the result windows show labels too, with the short block id as the tooltip of an entry. The section strip of the blocking-time charts shows the short block id instead, with the label in its hover. The information panels name a track by its id and a section by its label, and the tooltip of the field gives the track number or the section id. The tooltip of a signal head names its track and position, and in the preview the label of its section; **Edit > Find...** lists a signal by its track and position and finds it by the labels and the ids of its sections too; **Copy section ID** in the context menu of a signal head during a run copies the id. A validation message gives the id with the label in parentheses. The case files and the exports keep the ids, and the tables of the capacity windows their short block ids; a table cell that holds an id has the label as its tooltip. A label is not a reference: it changes when an earlier block of the same track changes length, so notes and scripts use the id. When two sections would get the same label, both labels end with the id in parentheses, and a reference that names no section is shown as it is.

### Position

A position is the X of the case in kilometres, the geo X, at a point of a route. Within a section it follows the distance along the route linearly between the X of the end nodes of the section. It counts down where the route runs against the case coordinate, as Paimpol route1 does, and it jumps where the route continues in another display region: on Netherlands route0, 18.298 km continues at 30.569 km. The blocking-time charts plot the distance along the reference route and label their bottom axis with positions, titled `Position (km)`, with a break mark at each jump ([inspecting diagrams](diagram-navigation.md)). The blocking-time exports give positions in their geo X columns. The route charts, **Timetable graph** and **Train paths**, plot the distance along the route, titled `Distance (km)`.

### Signalling area

A signalling area is a range along a track that gives the sections inside it their signalling level: `signalling_areas[]` in `signalling.json` with `id`, `start_km`, `end_km`, `level` and an optional `track`. A section belongs to an area only when it lies wholly inside; an area without a track applies to every track, an area with a track applies to that track and takes precedence, and a section that no area contains has no level. The positions are chainage, the `x_km` values along the track of the section, and are not converted between tracks. You meet it in the dock **Infrastructure**, entity **Signalling area** ([how an area gives sections their level](signalling-levels.md#how-an-area-gives-sections-their-level)).

### Signalling level

A signalling level is the whole number from 0 to 5 that a section has, or none ("No signalling"). The case stores no level on a block; the program takes it from the signalling area. The levels differ in how trains are kept apart: with a fixed block (the levels that the table calls fixed block) a train stops at the end of a block in front of an occupied block, with a moving block (the levels that the table calls moving block) the stop point lies behind the tail of the train ahead or the train takes the speed of the train ahead, and without a level trains are not separated; [the table of levels](signalling-levels.md#levels) says which block and which levels. The editor offers the labels in [what a signalling level is](signalling-levels.md#what-a-signalling-level-is) in a list; they name what the model does and are not claims about real systems ([what is not established](signalling-levels.md#what-is-not-established)).

### End of authority

An end of authority is, in the model, a point on a route that a train takes as a target: a stop or, in the following mode that [the table of levels](signalling-levels.md#levels) names, the speed of the train ahead. The program makes one behind the tail of a train at the moving block levels and, for a signal failure, at the end of the section before the failed one; a train obeys only the ends of authority of its own direction ([common behaviour](signalling-levels.md#common-behaviour)).

### Signal head

A signal head is the mark that the canvas draws for a signal. It shows what the simulation holds for the displayed time: the code that the program writes on the section ahead (a section has a code and a state, and each of its arcs has a signal speed limit). The **Map Key** (**View > Map Key**, in the dock **Case and Layers**) lists, when the case has signals, "Stop signal", "Caution signal", "Proceed signal", "Unavailable signal" and "Failed signal". A head is unavailable when its section has no level, and failed while a signal failure is active ([codes and what they do to a following train](signalling-levels.md#codes-and-what-they-do-to-a-following-train)).

## Delay, blocking time and capacity

### Blocking time

Blocking time is the time that the program calculates for a train on a block: an envelope from the approach minus the setup and sight reaction time to the clearance plus the release and run margin. It is calculated, not observed occupation ([timetable graphs and train paths](diagram-navigation.md#timetable-graphs-and-train-paths)). The table of [levels](signalling-levels.md#levels) says which levels have blocking times. You meet it in **Diagrams > Blocking time...** and **Run Results > Open result view... > Blocking time**, which need a completed run; the case settings **Buffer (s)** and **Recovery (%)** feed the calculation.

### Delay

A delay is, in the timetable results, the simulated time minus the planned time, in seconds and signed, so that early is negative. The Delay analysis window (**Diagrams > Delay analysis**) shows the delay of each call, a summary, a histogram of the delays and the comparison against a delay baseline. Delay figures also appear in the timetable results (**Diagrams > Timetable**, **Diagrams > Delays...**), in the station statistics files, in the comparison against a delay baseline and in the passenger journey delay, and they are different quantities. An entrance delay is an input, not a delay result. See [what this guide covers](delay-analysis.md#what-this-guide-covers) and [where delays are calculated](delay-analysis.md#where-delays-are-calculated).

### Capacity analysis

Capacity analysis is the window opened by **Diagrams > Capacity...** or **Run Results > Open result view... > Capacity** after a run. It works on one route, on the blocks of it that the trains you select all occupy, and on the blocking times of those trains, not on signalling areas, so it has data only for sections that have blocking times ([block sections and capacity areas](signalling-levels.md#signalling-areas-block-sections-and-capacity-areas)). The table in [exercise sequence](assignment-workflow.md#exercise-sequence) names its results: the minimum headway and the scheduled headway of an ordered pair of trains, the compressed timetable, critical blocks, buffer times and capacity consumption. In this window "headway" names a result of an ordered pair of trains, not the setting **Repeat every (s)** of a service, and the buffer there is a result as well, not the case setting **Buffer (s)**. [How capacity is analysed](capacity-analysis.md) states what the window computes.
