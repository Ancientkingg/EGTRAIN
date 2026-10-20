# Keyboard shortcuts

This page lists the keys that EGTRAIN sets, grouped as the menus are. On macOS, Ctrl in a shortcut is the Command key (⌘), and the menus show the shortcut with the macOS symbols of the third column: ⇧ is Shift and ⎋ is Esc. A key that the table shows once works the same on every system.

## File

| Entry | Windows and Linux | macOS |
| --- | --- | --- |
| **File > New Case Study...** | Ctrl+N | ⌘N |
| **File > Open Case Study...** | Ctrl+O | ⌘O |
| **File > Save Scene** | Ctrl+S | ⌘S |
| **File > Quit** | Ctrl+Q | ⌘Q, in the application menu |

## Edit

| Entry | Windows and Linux | macOS |
| --- | --- | --- |
| **Edit > Undo** | Ctrl+Z | ⌘Z |
| **Edit > Redo** | Windows: Ctrl+Y, Ctrl+Shift+Z or Alt+Shift+Backspace. Linux: Ctrl+Shift+Z | ⇧⌘Z |
| **Edit > Find...** | Ctrl+F | ⌘F |
| **Edit > Preferences...** | none | ⌘, as **EGTRAIN > Preferences...** |

## View

| Entry | Windows and Linux | macOS |
| --- | --- | --- |
| **View > Zoom In** | Ctrl++ | ⌘+ |
| **View > Zoom Out** | Ctrl+- | ⌘- |
| **View > Fit** | Ctrl+0 | ⌘0 |
| **View > Console Log** | Ctrl+Shift+L | ⇧⌘L |
| **View > Scene Validation** | Ctrl+Shift+V | ⇧⌘V |

**Follow** has no shortcut.

## Simulation

| Entry | Windows and Linux | macOS |
| --- | --- | --- |
| **Simulation > Run** | Ctrl+R | ⌘R |
| **Simulation > Pause** | Ctrl+. | ⌘. |
| **Simulation > Stop** | Ctrl+Esc | ⌘⎋ |

While a run is paused, **Pause** reads **Resume** and the same key resumes it. Windows takes Ctrl+Esc to open the Start menu, so on Windows use the **Stop** button of the toolbar or the menu.

## Help

| Entry | Windows and Linux | macOS |
| --- | --- | --- |
| **Help > EGTRAIN Manual** | F1 | F1, and ⌘? |

F1 opens the section of the manual about the window you are in, when the manual has one. It does not work while a dialog that blocks the main window is open, such as **Run simulation** or **Settings**. On macOS the system takes ⌘? to search the **Help** menu, so use F1 there.

## Find

**Edit > Find...** opens a box with a text field and a list of matches.

| Key | Effect |
| --- | --- |
| Up, Down | Choose a match in the list while you type |
| Return or Enter | Go to the chosen match |
| Esc | Close the box |

## Dialogs

Esc closes a dialog as its cancel button does. Return or Enter presses the default button, which these dialogs set. On macOS a message box shows no title, only its question. The table names such a box by the title that Windows and Linux show, except the update question.

| Dialog | Return or Enter | Esc |
| --- | --- | --- |
| **Run simulation** | **Run simulation** | **Cancel** |
| **Open a Case** | **Open** | **Continue**, or **Cancel** when no case is open |
| **Recover Unsaved Work** | **Recover** when the copy can be opened and its original has not changed; otherwise **Decide Later** | **Decide Later** |
| **Help improve EGTRAIN** | **Not now** | **Not now** |
| **Settings** | **OK** | **Cancel** |
| **Reference route** | **OK** | **Cancel** |
| **About EGTRAIN** | **Close** | **Close** |
| **Unsaved Scene** | **Save** | **Cancel** |
| **Save Scene As Folder** | **No** | **No** |
| **Import Legacy Case** | **No** | **No** |
| The delete questions of the editors, such as **Delete Service** and **Delete Scenario** | **No** | **No** |
| **Check for EGTRAIN updates automatically?** | **Check Automatically** | **Don't Check Automatically** |
| EGTRAIN *version* is Available | **Update and Restart** when EGTRAIN can update itself; otherwise **Open Release Page** | **Later** |
| **Linked source changed** | **Keep local** | **Keep local** |

## Result windows

The chart windows of **Diagrams**, such as **Speed / distance**, **Delays**, **Timetable graph...** and **Blocking time...**, and the chart of **Plot input traction characteristic** take these keys while the chart has the focus. [Diagram navigation](diagram-navigation.md) describes the mouse and the trackpad.

| Key | Effect |
| --- | --- |
| + or = | Zoom in |
| - | Zoom out |
| 0 or Home | Return to the whole chart |
| Left, Right, Up, Down | Move the view |

Return and Enter do not start an export in a result window.

## Editors

In the table of stops on the tab **Timetable** of **Editors > Services**, Return or Enter opens **Edit timetable stop** for the selected row, as a click does.
