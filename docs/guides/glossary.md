# Glossary of application terms

This page says what the words of the program and of its documents mean. Every entry says what a word means in EGTRAIN and where a user meets it: in a menu, a window, a field or a file. Railway words are explained only as far as the documents of this repository define them; [What is not established](../architecture/signalling-levels.md#what-is-not-established) says what they do not claim. The schema and the guides stay the authority, so an entry links to them instead of repeating them.

## Cases and files

### Case study

A case study is what the program opens, edits, runs and saves: everything a run needs, that is infrastructure, signalling, rolling stock, services with timetables, scenarios and, optionally, passengers. It is stored as a scene folder or as a bundle, and "case" alone means the same. You meet it in **File > New Case Study...**, **File > Open Case Study...** (which opens a bundle), **File > Save Case Study As...**, the dock **Case and Layers** (the line "Current case study") and the command-line option `-n` ([case study numbers](command-line.md#case-study-numbers)). The toolbar button **Open Case** shows the chooser **Open a Case**, which lists "Bundled cases" and "Recent cases" and has the button **Other ways to open...**.

### Scene

A scene is the data of a case study as the program reads it: a folder of JSON files. The required files are `scene.json`, `infrastructure.json`, `stations.json`, `signalling.json`, `rolling_stock.json` and `services.json`, and `scenarios.json`, `passengers.json` and `views.json` are optional on load ([scene directory](scenes-and-application.md#scene-directory), [contents of the files](../architecture/scene-schema.md#version-and-files)). The menus say "scene" for this data in whichever form it is stored: **File > Open Scene Folder...**, **File > Save Scene** (it writes back to the folder or bundle that is open, and for a new case that has no location yet it asks for a bundle name, like **File > Save Case Study As...**), **File > Save Scene As Folder...** and the dock **Scene Validation**. "Canonical" means this form: the scene folder is the editable source of truth, loading, validating and running use only it, and legacy files enter or leave only through the explicit import and export ([scene model](../architecture/scene-model.md)).

### Bundle

A bundle is one file for moving a case study: a ZIP archive that holds the JSON files of a scene and has the extension `.egscene`. It holds no results and no legacy input. **File > Save Case Study As...** and `scene_tool pack` make it, and **File > Open Case Study...**, the chooser and the option `--scene` open it. The folder stays the editable source format and the bundle is its transport form ([opening a case study](opening-a-case-study.md), [bundle format](../architecture/scene-bundle.md)).

### V1 and V2

V1 and V2 are names of formats that appear in page titles and file names. V1 is the scene data, a folder of JSON files numbered by `schema_version` in `scene.json`, and V2 is the bundle container, numbered by `bundle_version`. The two numbers are independent. A reader of the program needs only the words scene and bundle; see [Bundle format (v2)](../architecture/scene-model.md#bundle-format-v2) and [the bundle page](../architecture/scene-bundle.md).

### Scene version

These version numbers are independent: the version of the application, the `schema_version` of the scene data and the `bundle_version` of a bundle. The application version that a scene was saved with is provenance and does not by itself lead to an upgrade offer. When you open a case the program checks the versions and never changes the file, and a scene that it can open opens directly. Otherwise it shows one of these dialogs: **Older Scene Not Supported**, **Newer Scene** (with the button **Check for Updates...**), **Cannot Open Scene** (the message names the first problem) or **Older Scene**. **Older Scene** offers **Upgrade a Copy...**, which writes an upgraded copy and leaves the original unchanged, and it appears only for a version that has a registered migration. The [compatibility boundary](../architecture/scene-model.md#compatibility-boundary) lists which scenes open, and [Open and run](opening-a-case-study.md#open-and-run) describes the dialogs.

### Legacy case

A legacy case is the input format of earlier versions: a folder of text files, for example `Routes/Route<N>.txt`, with a train definition as one line of whitespace-separated tokens. The program does not run it. **File > Load Legacy Case...** (also **Other ways to open... > Import Legacy Case...** in the chooser) and `scene_tool import` convert it into a new scene folder at a destination you choose and leave the source unchanged; what could not be converted is listed in `import_report` in `scene.json`, and the dock **Loaded Data** shows its counts. `scene_tool export` writes legacy files from a scene for other tools, and edits to them do not flow back. See [convert a legacy case](scenes-and-application.md#convert-a-legacy-case), [scene_tool](command-line.md#scene_tool) and the [migration matrix](v1-scene-migration-matrix.md), which says what each legacy file becomes.

### Provenance

Provenance names two things. Each export of results (**Export CSV...**, **Export PNG...**) writes a file with the same name plus `.provenance.json` beside it; it records the run that produced the export, and [Random seed](command-line.md#random-seed) names the seed that it holds. Rolling stock units carry descriptive source references, shown as **Parameter source reference** and **Tractive-effort source reference** in the dock **Rolling stock units** (the keys `data_file` and `traction_file` under `source`). The program does not reopen those files for a run ([review what loaded](scenes-and-application.md#review-what-loaded)).

### Views file

`views.json` is optional display data: the drawing level, region and visibility of tracks and the coordinates of stations in the network view. It changes nothing in topology, signalling, services or results, and folders and bundles keep it. The "level" in it is a drawing level and has nothing to do with a signalling level. See [views.json](../architecture/scene-schema.md#viewsjson-and-runtime-metadata).

### Output folder

The output folder is the folder where a run writes its text output. **File > Set Output Folder...** chooses it for the next run. [Output folder](command-line.md#output-folder) gives the default and the environment variable that replaces it. The exports (**Export CSV...**, **Export PNG...**) ask for their own path and are not written there.

## Checking, running and results

### Validation

Validation is the program's check of a case; each finding is a diagnostic with a severity (error, warning or info), a code such as `scene.ref.unresolved`, a message, a file and path and a suggested fix. Its layers are structural (files, JSON shape, required sections), semantic (references, identifiers, values, timetables, incidents) and runnable (the minimum a run needs). It runs when a case opens, after each edit, after a save and just before a run: an error disables **Run**, warnings and info do not, and **Save Scene** stays allowed with semantic errors. You meet it in the dock **Scene Validation** (**View > Scene Validation**; columns **Severity**, **Code**, **Message**, **File**, **Path** and **Suggested Fix**) and in `scene_tool validate`, where `--runnable` applies the check that Run applies ([validation layers](../architecture/scene-model.md#validation-layers), [validation lifecycle](../architecture/scene-editor-flow.md#validation-lifecycle)).

### Run

A run is a simulation of the open case with the selected scenario and the ticked occurrences. You start it with the toolbar button **Run**, **Simulation > Run** (Ctrl+R) or **File > Run Scene**. They are disabled while no case is loaded, while a run is active and while validation reports an error; with no occurrence ticked, starting a run opens the message **Cannot Run Scene**. Otherwise the dialog **Run simulation** shows **Selected in period**, **Start clock**, **Duration** and **Active incidents**, and its button **Run simulation** starts the run. A run uses the case as it is in the window, including unsaved edits ([simulation handoff mechanics](../architecture/scene-editor-flow.md#simulation-handoff-mechanics)).

### Runtime

The runtime is the working data that a run builds in memory from the case: the infrastructure and signalling, the services and their trains, the selected scenario and the passengers. It is not part of the case. The dock **Loaded Data** (**View > Loaded Data**; columns **Category**, **Source**, **Count** and **Status**) shows its state in the row "Runtime and results": "Not built" (after a case is opened or edited), "Ready", "Running", "Completed", "Stopped" or "Failed". In the same dock "Missing optional" marks an optional part that the case does not have, for example an optional file that the case can do without ([review what loaded](scenes-and-application.md#review-what-loaded), [native runtime path](../architecture/scene-model.md#native-runtime-path)).

### Playback

Playback is what you watch during a run: the trains move on the canvas as the simulation produces steps. **Pause** (the button then reads **Resume**), **Stop** and the speed slider between **Slower** and **Faster** control it, and the tooltip of the slider reads "Simulation speed: fastest" or names a factor. A run that you stop before its end leaves no results. Playback is not the replay and not the results; [Completed-run replay](../development/build-and-test.md#completed-run-replay) says what a stopped run leaves.

### Replay

The replay is the row under the network that appears after a successful run. It has the buttons **Start**, **Play** (which then reads **Pause**) and **End**, a slider and a label that begins "Replay" and states the interval between frames. It shows the recorded frames of the latest completed run: a position shows the last recorded frame at or before the chosen time. A stopped run has none, and a new run or any edit of the case clears it ([Completed-run replay](../development/build-and-test.md#completed-run-replay)).

### Results

Results are what a completed run leaves in memory: the dock **Run Results** (one row per train and a last row **Network total**, with the buttons **Open result view...**, **Export CSV...**, **Export PNG...**, **Set delay baseline** and **Compare delays**) and the result windows, which open from the **Diagrams** menu or from **Open result view...**. The entries that open them stay disabled until a run has completed ("Run a simulation to enable this diagram"). Results are not part of the case, so saving the case does not save them; any edit of the case, creating or opening a case and selecting another scenario discard them, and only a new run rebuilds them. A run also writes text files to the output folder ([scenario library and student loop](scenes-and-application.md#scenario-library-and-student-loop), [what this guide covers](delay-analysis.md#what-this-guide-covers)).

### Normal mode and advanced mode

**View > Advanced / Developer details** (a check mark that the program remembers) switches between normal mode and advanced mode. Advanced mode adds technical inventories, validation details, non-blocking warnings and, after a case opens, the dock **Loaded Data**. It changes the presentation only: not what **Run** requires, what **Save Scene** writes or what a run produces ([validation lifecycle](../architecture/scene-editor-flow.md#validation-lifecycle)).

## Scenarios and incidents

### Scenario

A scenario is a named set of incidents and entrance delays; a case study can hold several of them, and a scenario is not a bundle. They are stored in `scenarios.json`, where `default_scenario_id` names the default one, and infrastructure, rolling stock and services are shared by all of them. A run applies one scenario: the selected one, otherwise the default one ([model ownership](../architecture/scene-model.md#model-ownership)). You meet scenarios in the dock **Incidents** (**Editors > Incidents**): the list "Scenarios" (accessible name "Scenario library") with **Blank**, **Duplicate**, **Delete**, **Import JSON...**, **Export JSON...** and the fields **Scenario ID**, **Name** and **Description**, and in the dialog **Run simulation**, which shows the case name and the scenario ID under its title ([scenarios.json](../architecture/scene-schema.md#scenariosjson), [scenario library](scenes-and-application.md#scenario-library-and-student-loop)).

### Baseline

The word baseline has two meanings. The baseline scenario has the ID `baseline` and the name "Baseline"; a new case study starts with it, with no incidents and no entrance delays, and loading a scene without `scenarios.json` creates a scenario with that ID and name. The delay baseline is a completed run without incidents and entrance delays that you freeze with **Set delay baseline** in the dock **Run Results**, so that **Compare delays** can compare a later run with incidents against it. An edit of the case clears the delay baseline and selecting another scenario does not ([delay comparison against a baseline](delay-analysis.md#delay-comparison-against-a-baseline)).

### Incident

An incident is a disturbance in a scenario. In the dock **Incidents** the tab **Incidents** has the fields **Type** (`signal_failure` or `train_breakdown`), **Target**, **Start (s)**, **Recovery end (s)** with the box **Use recovery end**, **Reduced speed cap** and **Terminate at destination**. A signal failure needs an end; from its start to its end the sections of its target count as occupied and a stop target lies before them ([common behaviour](../architecture/signalling-levels.md#common-behaviour)). A train breakdown acts on a service and may name one occurrence: it holds the train in place from its start to its end, or, with a **Reduced speed cap**, limits its speed, and a cap without an end lasts until the train reaches the end of its route. **Terminate at destination** marks the occurrence when it reaches its authored route end ([scenarios.json](../architecture/scene-schema.md#scenariosjson)).

### Entrance delay

An entrance delay is a number of seconds that a scenario adds to the planned departure of the named stop of one occurrence of a service. Planned arrivals stay as authored. It is an input of a scenario, not a result, and **Compare delays** refuses a scenario that has one. You meet it on the tab **Entrance Delays** of the dock **Incidents**, with the fields **Service**, **Occurrence**, **Station** and **Delay (s)** ([units and the reference timetable](delay-analysis.md#units-and-the-reference-timetable)).

## Services, timetables and rolling stock

### Service

A service is a train service as the case defines it: an ID, a composition, a route, stops, and optionally an operating code, an entry time, a repetition, a performance, a speed cap, a category and a colour. You meet it in the dock **Services** (**Editors > Services**), tab **Service and timetable**, where **Service Id** is the unique key that other files use. A service can run more than once, and each run is an [occurrence](#occurrence) ([services.json](../architecture/scene-schema.md#servicesjson)).

### Service code

The editor label **Service code (number)** is the key `operating_code`; the results and the delay comparison call the same value **Operating code**. It defaults to the service ID and need not be unique. For a repeated service each occurrence has its own code: the base code followed by the occurrence number, or, when the box **Service code (number) step** is ticked and the base code is digits only, a stepped number, for example 1723, 1725, 1727 ([services.json](../architecture/scene-schema.md#servicesjson), [model ownership](../architecture/scene-model.md#model-ownership)).

### Occurrence

An occurrence is one generated run of a service. Its identity is the pair of the service ID and an occurrence number counted from 1, written `<service ID>-<number>` where a single string is needed, and in the results one occurrence is one **Train**. A service without **Repeat Headway (s)** has one occurrence; with it, the number is the value of the box **Configured total** if that box is ticked, otherwise the duration divided by the headway, rounded up ([services.json](../architecture/scene-schema.md#servicesjson)). You meet occurrences on the tab **Run occurrences** of the dock **Services**: one row per occurrence with the columns **Include**, **Service code (number)**, **Generated service**, **Scheduled entry**, **Running performance (parameter) %** and **Maximum speed restriction (km/h)**, the buttons **Select all** and **Select none**, and a line "Configured total: ...; Number of services in sim.: ...; Selected: ...; Selected in period: ...". In the order of that line the counts are all occurrences of all services; those whose scheduled entry lies from zero up to, but not including, the duration; the ticked ones; and the ticked ones in that period, and the line itself says that they are configured identities, not observed trains. **Run** needs at least one ticked occurrence, the ticks are not saved with the case and are reset when a case is created or opened, and **Configured total** is also the label of the box that sets the number of occurrences of one service.

### Entry time

The entry time is the second at which an occurrence of a service enters its route, counted as elapsed time. It is the key `entry_time_seconds` and the box **Entry Time (s)** (the field works only while the box is ticked). When it is not set, the planned departure of the first stop is used, and zero when that is absent too; a repeated service adds (n - 1) times the headway, where n is the occurrence number. **Scheduled entry** on the tab **Run occurrences** shows the result ([units and the reference timetable](delay-analysis.md#units-and-the-reference-timetable)).

### Headway

In the editor the headway is the time between the entries of consecutive occurrences of one service: the key `repeat.headway_seconds` and the box **Repeat Headway (s)**. Occurrence n is shifted by (n - 1) times the headway, for its entry and for its stop times ([services.json](../architecture/scene-schema.md#servicesjson)). The capacity analysis window uses the word for results of an ordered pair of trains, not for this setting; see [Capacity analysis](#capacity-analysis).

### Timetable stop

A timetable stop is one scheduled call of a service at a station: the station, an optional platform, **Planned arrival**, **Planned departure** and **Minimum dwell (s)** (the keys `station`, `platform`, `planned_arrival_seconds`, `planned_departure_seconds` and `dwell_seconds`). Arrival and departure are each optional, where blank means absent and 0 is the simulation zero, and both are planned input counted from the base time of the case, never results ([edit and validate](scenes-and-application.md#edit-and-validate)). You meet stops in the table **Timetable stops** on the tab **Service and timetable** (columns **Timetable stops (station)**, **Stop platform**, **Minimum dwell (s)**, **Planned arrival** and **Planned departure**). The dialog **Edit timetable stop** (**Add timetable stop** for a new stop) opens from **Add Stop**, **Insert After Selected** or a click on a row.

### Dwell

The dwell is the time that has to pass before a train that has reached a stop can leave it: the key `dwell_seconds` and the field **Minimum dwell (s)**, which is required and not negative. A train that has reached a stop leaves it no earlier than the dwell has passed and no earlier than the planned departure of the stop, when it has one. A train that begins its route at a stop is not held there ([units and the reference timetable](delay-analysis.md#units-and-the-reference-timetable)).

### Elapsed time and clock time

Planned times and entry times count elapsed seconds from the base time of the case (the key `base_time`, the field **Base time**, written `HH:MM:SS`; the rules are in [edit and validate](scenes-and-application.md#edit-and-validate)). Clock time is the base time plus the elapsed seconds: with the base time 08:00:00, 90 seconds is 08:01:30. **Planned time display** in the dock **Services** and in the stop dialog chooses **Elapsed offsets (s)** or **Clock time**, and switching changes the display only, not the stored seconds. A later day is written with the prefix `+1d`, and passenger windows count differently (see [Passenger](#passenger)).

### Case settings

The dock **Case Settings** (**Editors > Case Settings**) holds **Name**, **Description**, **Base time**, **Duration / horizon (s)**, **Buffer (s)** and **Recovery (%)**; in `scene.json` they are `name`, `description`, `base_time` and, under `simulation_settings`, `duration_seconds`, `buffer_time_seconds` and `recovery_time_percent`. The duration is the length of the simulated period (the option `-h` calls it the simulation horizon), and the occurrences counted "in period" are those whose scheduled entry lies within it. Buffer and recovery are numbers that the blocking-time calculation reads, and `-b` and `-c` replace them for a command-line start. **Recovery (%)** has nothing to do with **Recovery end (s)** of an incident ([scene.json](../architecture/scene-schema.md#scenejson), [options](command-line.md#options)).

### Performance and speed cap

Performance and speed cap are two settings of a service. **Running performance (parameter) %** (`performance_percent`; the default is in [services.json](../architecture/scene-schema.md#servicesjson)) scales the available tractive effort and the commanded maximum speed, not braking, mass or composition data. **Maximum speed restriction km/h** (`maximum_speed_kmh`, a box) caps the speed: the commanded maximum is the composition maximum limited by the cap, then multiplied by the performance. **Performance (%)** and **Maximum speed (km/h)** in **Run Results** show the applied values per train, and the **Reduced speed cap** of a train breakdown is a different, temporary limit.

### Rolling stock unit and composition

A rolling stock unit (`train_units[]`; the files and the schema call it a train unit) has physical values, a traction curve of rows `v_lower`, `v_upper`, `c0`, `c1`, `c2` and descriptive source references. A composition is an ordered list of units, and a service names one composition. You meet them in the docks **Rolling stock units** and **Compositions** (**Editors > Rolling stock units**, **Editors > Compositions**). The traction curve is input: the button **Plot input traction characteristic** draws it, and that plot shows rolling stock input, not simulated effort, while the result view **Tractive effort / distance** shows what the run applied ([rolling_stock.json](../architecture/scene-schema.md#rolling_stockjson), which lists the keys).

### Passenger

A passenger record is optional input in `passengers.json`: passengers with journeys, where a journey has an origin station and a destination station, a planned departure window and a planned arrival window, and legs. A leg rides one occurrence of a service from one station to a later one. The windows are absolute seconds from midnight, not from the base time. You meet them in the dock **Passengers** (**Editors > Passengers**; lists **Passengers**, **Journeys** and **Legs**) and in the option `-pax` ([passengers.json](../architecture/scene-schema.md#passengersjson), [options](command-line.md#options)).

## Network and signalling

### Infrastructure entities

The dock **Infrastructure** (**Editors > Infrastructure**) has a chooser **Entity** with one entry for each row of this table, in the order of the chooser.

| Entity | What the documents say | Stored in |
| --- | --- | --- |
| Tracks | `id`. Nodes, arcs and blocks name their track; positions along a track are in km. | `infrastructure.json` |
| Nodes | A point of a track, with `x_km` and `y_km`. | `infrastructure.json` |
| Arcs | Join two nodes of a track (`from`, `to`) and carry the curvature radius, the gradient (percent, used unchanged as rise per length) and the speed limit (m/s). | `infrastructure.json` |
| Blocks | Follow each other along a track, each with `length_km`. A block is the part of a track that a train occupies and the unit in which aspects are written ([block sections](../architecture/signalling-levels.md#signalling-areas-block-sections-and-capacity-areas)). A block ID cannot contain "/". | `infrastructure.json` |
| Connections | Join two nodes (`from`, `to`), with an optional speed limit; each makes a switch section ([how an area gives sections their level](../architecture/signalling-levels.md#how-an-area-gives-sections-their-level)). | `infrastructure.json` |
| Stations | `id`, `name`, an optional `position_km` and platforms. | `stations.json` |
| Platforms | `id`, node IDs and an optional `length_m` and `width_m`, which set the passenger capacity ([stations.json](../architecture/scene-schema.md#stationsjson) gives the defaults). | `stations.json` |
| Signals | `id` and an optional `protected_section`, used to resolve the targets of signal failures; the heads on the canvas show the aspect of a section (see [Signal head](#signal-head)). | `signalling.json` |
| Signalling area | See [Signalling area](#signalling-area). | `signalling.json` |
| Routes | `id` and `blocks` in authored order, an optional `corridor` and `reversed`; a service names a route by its ID. | `signalling.json` |
| Block dependencies | A pair `block` and `depends_on`. | `signalling.json` |
| Single-track restrictions | `start_block`, `end_block`, `protected_start_block` and `protected_end_block`; they keep trains of opposite directions out of a stretch ([single-track restrictions](../architecture/scene-schema.md#single-track-restrictions)). | `signalling.json` |
| Station boundaries | `entrance_block`, an optional `exit_block` and an optional `direction`; the simulation can mark the entrance section as occupied ([what happens in a step](../architecture/signalling-levels.md#what-happens-in-a-step)). | `signalling.json` |

### Section

A section is the unit of signalling at run time: a block of one track, or a switch section that a connection builds from two blocks. It is not stored in the case, because the program derives it from tracks, blocks, arcs and connections. A section has a signalling level or none, a code and a state, and the validation warning `scene.signalling.level.missing` names route sections that have no level ([what a signalling level is](../architecture/signalling-levels.md#what-a-signalling-level-is), [block sections](../architecture/signalling-levels.md#signalling-areas-block-sections-and-capacity-areas)).

### Signalling area

A signalling area is a range along a track that gives the sections inside it their signalling level: `signalling_areas[]` in `signalling.json` with `id`, `start_km`, `end_km`, `level` and an optional `track`. A section belongs to an area only when it lies wholly inside; an area without a track applies to every track, an area with a track applies to that track and takes precedence, and a section that no area contains has no level. The positions are chainage, the `x_km` values along the track of the section, and are not converted between tracks. You meet it in the dock **Infrastructure**, entity **Signalling area** ([how an area gives sections their level](../architecture/signalling-levels.md#how-an-area-gives-sections-their-level)).

### Signalling level

A signalling level is the whole number from 0 to 5 that a section has, or none ("No signalling"). The scene stores no level on a block; the program takes it from the signalling area. The levels differ in how trains are kept apart: with a fixed block (the levels that the table calls fixed block) a train stops at the end of a block in front of an occupied block, with a moving block (the levels that the table calls moving block) the stop point lies behind the tail of the train ahead or the train takes the speed of the train ahead, and without a level trains are not separated; [the table of levels](../architecture/signalling-levels.md#levels) says which block and which levels. The editor shows the number, and the labels in [what a signalling level is](../architecture/signalling-levels.md#what-a-signalling-level-is) name what the model does and are not claims about real systems ([what is not established](../architecture/signalling-levels.md#what-is-not-established)).

### End of authority

An end of authority is, in the model, a point on a route that a train takes as a target: a stop or, in the following mode that [the table of levels](../architecture/signalling-levels.md#levels) names, the speed of the train ahead. The program makes one behind the tail of a train at the moving block levels and, for a signal failure, at the end of the section before the failed one; a train obeys only the ends of authority of its own direction ([common behaviour](../architecture/signalling-levels.md#common-behaviour)).

### Signal head

A signal head is the mark that the canvas draws for a signal. It shows what the simulation holds for the displayed time: the code that the program writes on the section ahead (a section has a code and a state, and each of its arcs has a signal speed limit). The **Map key** (**View > Map key**, in the dock **Case and Layers**) lists, when the case has signals, "Stop signal", "Caution signal", "Proceed signal", "Unavailable signal" and "Failed signal". A head is unavailable when its section has no level, and failed while a signal failure is active ([codes and what they do to a following train](../architecture/signalling-levels.md#codes-and-what-they-do-to-a-following-train)).

## Delay, blocking time and capacity

### Blocking time

Blocking time is the time that the program calculates for a train on a block: an envelope from the approach minus the setup and sight reaction time to the clearance plus the release and run margin. It is calculated, not observed occupation ([timetable graphs and train paths](scenes-and-application.md#timetable-graphs-and-train-paths)). The table of [levels](../architecture/signalling-levels.md#levels) says which levels have blocking times. You meet it in **Diagrams > Blocking-time overlay...** and **Run Results > Open result view... > Blocking time**, which need a completed run; the case settings **Buffer (s)** and **Recovery (%)** feed the calculation.

### Delay

A delay is, in the timetable results, the simulated time minus the planned time, in seconds and signed, so that early is negative. The program has no delay analysis window: delay figures appear in the timetable results (**Diagrams > Timetable table (planned vs simulated)...**, **Diagrams > Train delays...**), in the station statistics files, in the comparison against a delay baseline and in the passenger journey delay, and they are different quantities. An entrance delay is an input, not a delay result. See [what this guide covers](delay-analysis.md#what-this-guide-covers) and [where delays are calculated](delay-analysis.md#where-delays-are-calculated).

### Capacity analysis

Capacity analysis is the window opened by **Diagrams > Capacity analysis...** or **Run Results > Open result view... > Capacity** after a run. It works on one route and a range of blocks that you choose and on the blocking times of the trains you select, not on signalling areas, so it has data only for sections that have blocking times ([block sections and capacity areas](../architecture/signalling-levels.md#signalling-areas-block-sections-and-capacity-areas)). The table in [exercise sequence](../product/assignment-workflow.md#exercise-sequence) names its results: the minimum headway and the scheduled headway of an ordered pair of trains, the compressed timetable, critical blocks, buffer times and capacity consumption. In this window "headway" names a result of an ordered pair of trains, not the setting **Repeat Headway (s)** of a service, and the buffer there is a result as well, not the case setting **Buffer (s)**.
