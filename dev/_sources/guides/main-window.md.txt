# Main window

This page describes how the main window shows the case that loaded: the **Loaded Data** dock and the source files of rolling stock. [Getting started](getting-started.md#3-look-at-the-case) introduces the window.

## Review what loaded

The **Loaded Data** dock stays separate from running. Its case-study tree shows
the source path, canonical schema version, bundle format version, source files,
category counts, default and available scenarios, validation state, and
runtime/result readiness. Expand a category to follow source data to parsed
canonical objects. Imported scenes also show conversion, skipped, and
unresolved-reference counts from `import_report`. `Missing optional` means the
scene is usable without that file; `Unsupported` means the file was found but
EGTRAIN did not consume it. `Not built` means no runtime has been prepared from
the current input. Advanced mode raises this panel automatically after open;
normal mode leaves it available through View when you need it.

Double-click infrastructure or signalling rows to focus the existing network
view, validation rows to open the diagnostics table, or a rolling stock unit,
composition, service, or scenario incident to open its existing editor.
Each rolling-stock-unit row owns its parameter, curve, plot, and provenance details.
Rolling-stock-unit provenance is descriptive:
an original parameter or tractive-effort filename is not reopened by the native
runtime. The tractive-effort plot evaluates the same piecewise polynomial as the
runtime and displays speed in km/h and effort in kN.

The rolling-stock editor also has explicit **Link parameter file...** and
**Link traction file...** actions. These links are session-only absolute local
paths, separate from the descriptive provenance fields, and are cleared when a
scene is reopened or replaced; Save As does not relocate or persist them.
Parameter files contain exactly nine whitespace-separated finite values:
traction-unit mass (kg), wagon mass (kg), wagon count, maximum speed (m/s),
maximum deceleration (m/s²), frontal area (m²), resistance coefficient, jerk
(m/s³), and length (m). Traction files contain up to 20 nonblank rows, each
with five finite values: starting speed (m/s), ending speed (m/s), and the
coefficients `a b c` for force in N, `a + b*v + c*v*v`. Speed intervals must
have positive width, be ordered, and not overlap.

The label under **Link parameter file...** and **Link traction file...** shows
the state of the link. **Unlinked** means no file is linked. A linked file that
exists shows **Linked**, **Up to date** or **Reloaded**, **Rejected** with the
reason when it cannot be read, or **Kept local** after Keep local. A reload or a retry that has not finished shows **Linked; waiting for reload** or **Retrying**, and during a run **Linked; pending reload after run** or **Retry pending after run**. **Missing**
replaces all of these when the linked file does not exist, also after Keep local.
It shows the path of the file, and **Retry** stays available. A conflict between
local edits and a changed file is a question, not a label: it asks for Reload
or Keep local.

Each linked update is applied only after the complete candidate validates.
Malformed or missing content leaves accepted values unchanged and keeps
**Retry** available. A valid change marks the scene dirty only when values
differ and refreshes open input traction plots. Conflicting local edits require
a Keep local/Reload decision, grouped for all units sharing the file. Updates
observed during a run are offered after the run completes. Completed result
charts do not change.
