# Interface style

The visual specification of the EGTRAIN windows, docks, dialogs and tables. Most rules live in the style sheet `EGTRAIN/QEGTRAIN/resources/styles/egtrain.qss`, which `main` applies to the whole application before it shows the first window or message box. The rest are set in code and named below. The network canvas, railway notation, chart text and console follow their own rules ([exceptions](#exceptions)).

## Fonts

- **Interface:** the platform UI font at its default size. No family is set and no font is bundled, so each system shows its own font.
- **Fixed pitch:** the system fixed-pitch font (`QFontDatabase::systemFont(QFontDatabase::FixedFont)`) for the time in the progress bar (`widgets/TimeProgressBar.cpp`) and for the console (`widgets/ConsoleWidget.cpp`).

## Type scale

Sizes are relative to the font of the widget, so they follow a larger platform font, except the fixed pixel sizes in the sheet (14 px and 11 px).

| Role | Size | Weight | Where |
| --- | --- | --- | --- |
| Chart title | 1.25 times | bold | `DiagramWindow::applyChartStyle` |
| Section heading, table caption | 1.1 times | bold | `EditorLayout::sectionHeading` |
| Case name | 14 px | 600 | `QLabel#caseNameLabel` |
| Dock title, Open Case, run review values, group title in a dialog | body | 600 | style sheet |
| Table header | the platform header font, smaller than the body on macOS | 600 | style sheet |
| Body text, table cells, controls | body | normal | platform |
| Small text: case readiness, panel titles, map key | 11 px | normal, or 600 for the titles and the key toggle | style sheet |

There are two weights, normal and 600. Qt 6 keeps `font-weight: 600` as `QFont::DemiBold`, while `setBold(true)` in code sets `QFont::Bold` (700), so where the font has a semibold face the sheet and the code give different faces.

## Spacing and shape

- The padding of table headers, tabs, menu items and status labels, and the margins of toolbar separators, are multiples of 4 px.
- Push buttons, combo boxes, line edits, the spin boxes of a dialog and the train, occurrence and station buttons of the result windows are 28 px tall at a 13 px font: a button has 5 px above and below its text, a field 4 px. A push button in a dialog has 14 px beside its text, one in a dock or in a diagram or timetable window 7 px, so that the editor docks and the button bars of those windows keep their width; a field has 7 px. Outside a dialog a spin box has 2 px above and below its text and 4 px before it, so that it fits in a table row. It is a little larger than a Fusion spin box (6 px wider and 2 px taller at the font of the tests). The arrows of a spin box take 16 px at its right.
- Push buttons, toolbar buttons, the map key toggle, the train, occurrence and station buttons of the result windows, fields, the lists and tables of a dialog, status labels and the rows of the recovery prompt have a 6 px radius. Other tool buttons, such as the details toggles of a dialog and the scroll buttons of a tab bar, check box indicators, tabs, the progress bar and scroll handles keep 2 px. The slider handle is round.
- Scroll bars are 10 px wide, without arrow buttons.

## Colour tokens

Contrast ratios follow the WCAG formula for the pair named in the last column.

| Token | Value | Role | Contrast |
| --- | --- | --- | --- |
| Text | `#24313a` | body text, headings, header text | 11.8:1 on window, 13.3:1 on field, 11.0:1 on band |
| Secondary text | `#5b6871` | help, context lines, small titles | 5.1:1 on window, 4.7:1 on band |
| Status text | `#3f4d56` | neutral status label | 7.2:1 on band |
| Disabled text | `#64717a` | disabled controls and menu items | 4.3:1 on disabled fill, 4.5:1 on window |
| Accent | `#315a70` | focus of the Run Results table and of the tool buttons outside the toolbar, checked buttons, selected tab, progress fill, slider, links of About | white on accent 7.4:1 |
| Accent dark | `#26495c` | hover of the slider handle | white on it 9.6:1 |
| Action | `#2e7491` | default button fill, focus border of push buttons, toolbar buttons, the Run details and Calculation details toggles, fields, and the lists and tables of a dialog | white on it 5.2:1, 5.2:1 on field |
| Action hover | `#296882` | default button under the pointer | white on it 6.2:1 |
| Action pressed | `#23566c` | default button while pressed | white on it 8.0:1 |
| Action focus | `#0b2430` | focus border of the default button | 3.1:1 on action, 15:1 on surface |
| Window | `#eef2f5` | main window and panel background | |
| Surface | `#f8fafb` | toolbar, docks, menus, and a dialog with everything in it that is not a field | |
| Band | `#e3eaee` | dock titles, table headers, tabs, status bar, Open Case, hover of the tool buttons outside the toolbar, pressed push and toolbar buttons, scroll track, grid lines | |
| Field | `#ffffff` | inputs, buttons, tables | |
| Alternate row | `#f4f7f9` | every second table row | |
| Disabled fill | `#eceeec` | disabled and read-only fields, progress groove | |
| Selected | `#d8e1e5` | selected menu item, chosen case | text 10.0:1 |
| Border | `#c9d3db` | borders and rules of tables outside dialogs, docks, tabs, separators, status labels, disabled fields | |
| Control border | `#7b8b96` | border of push buttons, toolbar buttons and fields, and of the lists and tables of a dialog; slider groove | 3.5:1 on field, 3.4:1 on surface, 3.1:1 on window |
| Control hover | `#5b6871` | border of a push button, toolbar button or field under the pointer; the secondary text colour | 5.7:1 on field, 5.5:1 on surface |
| Warning | `#f5eadc`, border `#d8b98e`, text `#74451c` | warning status label | 6.8:1 |
| Error | `#f7e8e6`, border `#c98f88`, text `#81352d` | error status label | 7.1:1 |
| Blocking | `#965c22` | case readiness when the case cannot run | 4.8:1 on window |
| Delay late | `#81352d` above 60 s, `#74451c` above 0 s | timetable results | 8.0:1 or more on field, 7.5:1 or more on alternate row |
| Delay not late | `#006400` | timetable results, 0 s or early | 7.4:1 on field, 6.9:1 on alternate row |

The other values of the sheet are state shades of these roles: `#dce6eb` for Open Case under the pointer, `#dce3e7` for disabled default buttons, `#d8e1e5` for Open Case while pressed, `#d3e3ec` and `#bcd2df` for the checked Follow button, `#9aa5ab` for scroll handles and disabled check marks.

## States

| State | Look |
| --- | --- |
| Enabled | Field fill with the control border, also for toolbar buttons |
| Hover | Push buttons, toolbar buttons and fields: control hover border. Other tool buttons: accent border and band fill. Default button: action hover fill |
| Pressed | Push buttons and toolbar buttons: band fill. Default button: action pressed fill |
| Focused | Push buttons, toolbar buttons, fields, and the lists and tables of a dialog: a 2 px action border, with 1 px less padding so the text stays in place. Default button: a 2 px action focus border. Run details in the run review and Calculation details in the capacity analysis: band fill and a 2 px action border. Other tool buttons: accent border and band fill. Check box: band fill and an action border |
| Disabled | Disabled fill, border colour and disabled text |
| Selected | Item views use the platform selection colour; menus and the case chooser use the selected token; tabs and checked tool buttons use the accent with white text |
| Invalid | Fields are not recoloured. The message goes in a status label beside them (`DialogLayout::setStatus`, error or warning tokens), and the Scene Validation dock lists every issue |
| Empty | A help label in secondary text says what is missing (`EditorLayout::emptyStateLabel`); a table with no rows shows only its header; a value that does not exist shows as "-" |

## Tables

- The header is a band: band fill, text colour, weight 600, padding 4 by 8 px, and a border-colour rule to the right of and below each section. The weight is set on `QHeaderView`; the size is the platform header font.
- Cells are on the field colour with alternate rows and band-coloured grid lines.
- In a dialog a table has the control border, a 6 px radius and 2 px of padding, so that the header stays inside the rounded corners.
- Numeric columns are aligned to the right: the times, orders and delays of the timetable results, and the columns that `DelayAnalysisWindow` and `AreaCapacityWindow` name as numeric, and the headways, references, shifts and gaps of the capacity analysis dialog. Names, ids, codes and statuses are aligned to the left. The style sheet cannot turn on tabular figures, so digits line up only as far as the platform font has equal digit widths.
- Delays in the timetable results are signed and coloured with the delay tokens.
- The stops table of the Services dock shares the width of the dock among its columns and can cut a header when the dock is narrow; each header has its full text as a tooltip.

## Dialogs

- **One surface:** every `QDialog`, message boxes included, has the surface colour. The plain widgets, frames, labels, check boxes, radio buttons, scroll areas, tab bars and button boxes inside it take the same colour, so the body and its scroll viewport show no box of their own. The colour is opaque because a widget style such as Fusion draws the outline of a check box or a frame from the background colour of its widget.
- **Title:** the window title names the dialog. `DialogLayout::install` makes its heading the window title, and the body starts with the context line in secondary text.
- **Fields:** push buttons, combo boxes, line edits and spin boxes are white with the control border and a 6 px radius. A spin box draws its arrows from the icons `spin-up-arrow.svg` and `drop-down-arrow.svg`. Lists and tables in a dialog are white with the same border and radius.
- **Groups:** a group box in a dialog is a bold title over its fields, without a frame.
- **Status labels:** neutral, warning and error keep their colours, with a 6 px radius and 8 px padding.
- **Default button:** the default button of a dialog footer or a message box is filled with the action colour and has white text. Run simulation in the run review has the same look.
- **Footer buttons:** the buttons of a dialog footer and of a message box show only their text. The sheet turns off the icons that some desktop themes give OK, Cancel and Close, which would be hard to see on the filled default button.
- **Links:** the links of About EGTRAIN are underlined accent text without a button face or border, lined up with the text above them. Keyboard focus shows a 2 px border that does not move the text.

## Exceptions

- **Network canvas and railway notation:** drawn in code with their own colours and sizes, described in the [network renderer style map](renderer-style-map.md). Canvas text uses a default `QFont`, not the sheet.
- **Chart text:** Qt Charts draws axes and ticks with its own fonts; only the title, brushes and pens are set (`DiagramWindow::applyChartStyle`). The legend of line kinds below the plot uses the window font.
- **Console:** fixed-pitch text `#dde3e5` on `#12191f`, 13.7:1.

## The #409 decision

Issue #409 compared the current look with three prototypes in matched captures (macOS 26, 1280 by 800, the Netherlands and Paimpol cases, six states). The chosen direction is the refined system-native look: the platform font, one type scale, one bold weight, merged near-duplicate colours, spacing in 4 px steps, banded table headers and right-aligned numbers.

Rejected:

- **IBM Plex Sans:** it sets wider than the system font, so inspector values were cut, more labels wrapped, and table headers were cut the most.
- **Source Sans 3 with Source Serif 4 headings:** toolbar text became smaller, a second family for headings gained little, and the run review dialog with its details open reached the bottom of an 800 px screen.

Both would also have added font files, an OFL notice in three packages and an untested synthetic bold on Linux.

Remaining concerns:

- Windows and Linux rendering of this look has not been seen yet.
- Digits line up only through right alignment ([tables](#tables)).
