# Scenarios and the student loop

This page describes the scenario library, the student loop of a case, and what the network view shows while a run plays and after it.

## Scenarios

Open the **Incidents** dock to choose the default or another named scenario.
The dock has the tabs **Scenario**, **Incidents** and **Entrance delays**, and a
line above them names the selected scenario. Each row of the library on the
**Scenario** tab shows the name of a scenario, marked "default" for the default
scenario and "modified" for one with unsaved edits. A second line gives the
number of incidents and entrance delays when it is not zero, and "has errors"
when the scenario has validation errors. Advanced mode also shows "has
warnings". The tooltip shows the ID and the description. When two scenarios
have the same name, the row and the line above the tabs add the ID.
**New** and **Duplicate** create isolated scenarios; editing incidents, names, or
descriptions changes only the selected scenario. **Import...** and
**Export...** use the standalone scenario object documented in the schema
reference. Imports retain entrance delays, validate references against the
open case, and report any adjusted ID rather than replacing an existing
scenario or incident.

## The student loop

The completed student loop is: open a case, optionally review **Loaded Data**
and **Validation**, choose or edit a scenario, save (or **Save Case Study As...**),
review the run summary, run the selected scenario, then use **Run Results** to
open the existing timetable, delay, speed, and blocking-time views. Results
are cleared when the case or scenario changes and are rebuilt only by a new
run, so a result window is never presented as belonging to a newly selected
scenario.

## Replay

The **Replay** row that appears under the network after a run is described in [Replay a completed run](how-to.md#replay-a-completed-run).

## Incidents and Follow

The incident editor exposes occurrence, reduced-speed cap, recovery end (or
until-destination), and destination termination directly; these are not hidden
JSON-only settings. During playback, choose a train in the list next to **Follow** in the toolbar and
activate **Follow** to center the network view on it immediately, including
while paused.

## Follow in detail

The list names the state of each train. A train that has not entered the network
shows its scheduled entry time, as in "Rail-1 (scheduled 08:25:50)"; the time is
the schedule, not the actual entry. After that a train is "(running)",
"(finished)", "(hidden)" while the Trains layer is off, or "(no position)" when
it has none to draw. The list keeps its width in the toolbar and asks for a popup as
wide as the longest entry. The label at the right of the status bar names the
state of the selected train in one short clause, such as "Rail-1 enters at
08:25:50" or "Following Rail-1", and ends a clause that does not fit with an
ellipsis. Its tooltip, and the tooltips of **Follow** and of the list, say in full
whether the train is followed and why not. A train that has not entered yet remains armed: the view does
not move, and Follow starts when the train enters. While the Trains layer is off
the view does not move either, and the nearest station is not emphasised; both
return when the layer is switched on. Before a case has run, and in a case
without services, **Follow** is disabled and its tooltip says why.

In a live run Follow is switched off when the selected train leaves the network.
The train stays in the list, but Follow cannot be switched on for it. In the
replay of a completed run Follow stays on after the selected train has left, and
continues when you go back in time. A train that is scheduled to enter after the
end of the completed run has no position in its replay; the label says so and the
view does not move. Follow is also cleared when the run ends, when
it is disabled, when playback exits, or when the selected train is no longer
available; it never silently switches to another train.

In a live run a train glides along its track from one update to the next. The glide
lasts about as long as the time between two updates at the current speed, so the train
moves all the time and reaches each position as the next one arrives. The followed train
stays in the middle of the view: the view moves with the train at every step of its glide.
A train that has just appeared, that runs backwards, or that would glide further than the
visible area moves to its new position at once. In the replay of a completed
run the trains and the view move at once at every seek and every step of the playback,
because the replay holds a frame every five seconds and a glide would show motion that
was not recorded. Switching Follow on and choosing another train also move the view at
once. Dragging the canvas, zooming with the wheel and resizing the window do not switch
Follow off: in a live run the view returns to the train at the next update, smoothly
over less than half a second. The zoom buttons of the toolbar centre the view on the
train at once, and **Fit** shows the whole network. Stop, switching Follow off and
switching the Trains layer off stop the view where it is.

## Selecting items

Click a track, connection, node, station, signal, train or passenger on the
canvas to select it, in the preview as well as during a run. The matching
inspector opens, or for a passenger a box of its details above it, and a light
blue ring marks the item. Its title reads
`Preview: <kind> <id> (not running)` in the preview and Node Info, Station Node
Info, Arc Info, Connection Info, Signalling Info or Train Info during a run. A
signal of the preview is named by the label of its section, as in
`Preview: Signal B0 0.230 to 0.310 km (not running)` in Paimpol. A signal that
`signalling.json` lists for the section comes first: `sig1 (B0 0.230 to 0.310 km)`.
During a run the inspector names tracks by their scene ids and sections by their
labels, and the tooltip of a field gives the runtime track number or the section
id ([ids and labels](glossary.md#ids-and-labels)). The ring keeps its size at
every zoom and neither covers nor recolours the item. It follows a moving train and
disappears with the layer of its item. Clicking another item moves the ring;
clicking an empty part of the canvas or closing the inspector removes it. The
inspector also closes when a run stops.
Dragging the canvas pans the view and keeps the selection.

## Signal heads

Signal heads on the canvas show what the simulation holds for the displayed
time: Stop (red, with a dark bar), Caution (yellow, with a dark dot), Proceed
(green), Unavailable (a gray ring with a short gray bar) or Failed (a red lamp
with a white cross). A head is unavailable when its section has no signalling area, so a scene
without one shows rings for the whole run. A head is failed on every signal of a
section for as long as a signal failure incident is active. The marks appear when
a head is at least 6 pixels wide; zoom in to see them. While a run is paused the
canvas shows the last snapshot the simulation delivered, and after the run the
replay slider shows the heads of the selected time. The tooltip and the signalling
inspector name the state, and the Map key lists all five. The tooltip also names
the track and the sections ahead of and behind the head, as in
`Track B0 at 0.23 km; ahead B0 0.230 to 0.310 km; behind B0 0.000 to 0.230 km`.

## Delay baseline

A completed run without incidents or entrance delays can be frozen with **Set
delay baseline**. The results panel confirms the completed run identity and
places the next action or disabled reason beside the baseline controls.
Selecting another scenario keeps the baseline, discards the results and closes
the dock **Run Results**. Opened again from the menu **View**, the dock reads
`No completed run | Scenario: <scenario>` and `Baseline: <case> / <scenario>. Run the selected scenario
to create completed results for comparison.` A canonical scene edit, **File > New Case Study...**, or
opening another case clears the baseline and explains that a new run without incidents or
entrance delays is needed. **Compare delays** requires a run with incidents or
entrance delays that matches the baseline, and opens the delay analysis window
on its tab Against baseline. The window reports the difference and the recorded
direct incident evidence, and never states a cause. What it shows, how to read
it and what it does not cover are in
[How delays are calculated](delay-analysis.md#the-delay-analysis-window).
