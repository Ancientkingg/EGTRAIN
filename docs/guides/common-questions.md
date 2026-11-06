# Common questions

Short answers to the questions that come up most, each with a link to the full steps. [How-to](how-to.md) has the recipes, and the [glossary](glossary.md) defines the words.

## Why is Run disabled?

**Run** is disabled while no case is open, while a run is active and while validation reports an error. A warning does not disable it. When an error blocks the run, the line under the case name in the dock **Case and Layers** shows the first error, and the status bar counts the errors, as in `Cannot run: 3 errors`. Choose **View > Case Validation** (Ctrl+Shift+V) for the whole list and fix the errors from the top ([Fix validation errors](how-to.md#fix-validation-errors)).

When **Run** is enabled but a message refuses the run, the message names the problem. `Select at least one occurrence on the Run occurrences tab of Services.` means that no occurrence is ticked on that tab. [Why a command is disabled](how-to.md#why-a-command-is-disabled) lists the other commands.

## Why did my results disappear after an edit?

Results are not part of the case. They hold the run of the case as it was, so any edit of the case discards them, with the replay and the delay baseline. If the status bar still said that the run was complete, it now says `Run results discarded: the case changed.` Selecting another scenario discards the results too, but keeps the baseline. A stopped run leaves no results at all. Run the case again for new results.

What a run has already written stays. The result windows that are open keep the run they were opened for, the exports are files of their own, and the text files of the run stay in its output folder until a later run there replaces them. Export what you want to keep before you edit ([Results and exports](how-to.md#results-and-exports)).

## Where does a run write its files?

A run writes its text files, such as the energy files and the delay statistics, to the output folder of the case: `Output/<case name>` in the application data folder.

| System | Output folder of the case Paimpol |
| --- | --- |
| macOS | `~/Library/Application Support/EGTRAIN/EGTRAIN/Output/Paimpol` |
| Windows | `%APPDATA%\EGTRAIN\EGTRAIN\Output\Paimpol` |
| Linux | `~/.local/share/EGTRAIN/EGTRAIN/Output/Paimpol` |

**Simulation > Set Output Folder...** chooses another folder for the next runs of the session. The exports of **Run Results** and of the result windows do not go there: each asks for its own path. [Run output](run-output.md) lists the files, and [Command-line options](command-line.md#output-folder) gives the rule and the variable `QEGTRAIN_OUTPUT_DIR`.

## How do I keep my changes to a bundled case?

Save them in a copy. EGTRAIN never writes into its installation, so **File > Save** on a bundled case asks for a new `.egscene` file, as **File > Save As...** with **Case File...** does. Make the copy before you edit, then save your edits with **File > Save** (Ctrl+S). The copy is listed in **File > Recent Cases**, and the bundled case stays as it was ([Copy and save a case](how-to.md#copy-and-save-a-case)).

If EGTRAIN closes before you save, the next start offers your edits in **Recover Unsaved Work**.

## Why can I not edit while a run is active?

While a run uses the case, the contents of the editors **Services**, **Passengers**, **Infrastructure** and **Scenarios** are disabled, and so are **Edit > Undo** and **Edit > Redo**. The menu **Editors** still opens the docks. Click **Stop**, or wait until the run ends, and they are enabled again. **Case Settings**, **Rolling Stock Units** and **Compositions** stay usable during a run, but an edit there discards the results of that run ([Why a command is disabled](how-to.md#why-a-command-is-disabled)).

## Can I draw tracks on the network view?

No. The network view has no drawing tools. You add and change tracks, nodes, arcs, stations and platforms in the tables of the dock **Infrastructure** (**Editors > Infrastructure**). When the view shows no run, a click on an item of the view selects its row in that dock, and **Edit in Infrastructure** in the context menu of the item also brings the dock to the front.

[Change the infrastructure of a case](how-to.md#change-the-infrastructure-of-a-case) gives two examples, and the [tutorial on a minimal case](tutorial-minimal-scene.md) builds a line from empty tables. For many changes at once, save the case as a folder with **Case Folder...** of **File > Save As...** and edit its JSON files ([editing examples](scene-schema.md#editing-examples)).

## How do I see a time-distance graph of the trains?

Run the case to its end and choose **Diagrams > Train paths...**. In the dialog **Reference route**, choose the route to measure the distance along and click **OK**. The window draws every train on that route, with the distance across and the time downward. **Diagrams > Time / distance** draws, for each train, the distance it has covered against the time, without a reference route.

[View a graph of a run](how-to.md#view-a-graph-of-a-run) lists every view, and [Inspecting diagrams](diagram-navigation.md#timetable-graphs-and-train-paths) explains how to read and move the chart.

## Why is Compare delays disabled?

**Compare delays** compares a run with a delay baseline, so it needs both:

1. A baseline. After a completed run of a scenario without incidents and entrance delays, such as `baseline`, click **Set delay baseline** in the dock **Run Results**.
2. A completed run of a scenario that has at least one incident or entrance delay.

Any edit of the case clears the baseline, so build the scenario before you run the baseline. The tooltip of **Compare delays** and the line above the buttons say what is missing ([Compare a scenario with a baseline](how-to.md#compare-a-scenario-with-a-baseline)).

## What should I attach to a bug report?

Report the problem as the [support page](https://egtrain.samuelbruin.com/support/) of the EGTRAIN website says. **Report an issue** in **Help > About EGTRAIN** opens that page. Include:

- The version of EGTRAIN. **Help > About EGTRAIN** shows it under its title, after `Version`. On macOS the entry is in the menu **EGTRAIN**.
- Your system and its version, such as macOS 26 or Windows 11.
- The case, and the steps that lead to the problem.
- What you expected and what happened instead, with the exact text of any message.
- The lines around the problem in **View > Log** (Ctrl+Shift+L).
- After a crash, the crash note ([If EGTRAIN closes unexpectedly](how-to.md#if-egtrain-closes-unexpectedly)).

A case file, a log or a picture can hold data that you do not want to share. Attach a case only when the problem needs it, and remove what you do not want to share.
