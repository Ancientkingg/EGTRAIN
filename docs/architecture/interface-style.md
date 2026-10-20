# Interface style

The visual specification of the EGTRAIN windows, docks, dialogs and tables. Most rules live in the style sheet `EGTRAIN/QEGTRAIN/resources/styles/egtrain.qss`, which the main window applies to the whole application. The rest are set in code and named below. The network canvas, railway notation, chart text and console follow their own rules ([exceptions](#exceptions)).

## Fonts

- **Interface:** the platform UI font at its default size. No family is set and no font is bundled, so each system shows its own font.
- **Fixed pitch:** the system fixed-pitch font (`QFontDatabase::systemFont(QFontDatabase::FixedFont)`) for the time in the progress bar (`widgets/TimeProgressBar.cpp`) and for the console (`widgets/ConsoleWidget.cpp`).

## Type scale

Sizes are relative to the font of the widget, so they follow a larger platform font, except the fixed pixel sizes in the sheet (14 px and 11 px).

| Role | Size | Weight | Where |
| --- | --- | --- | --- |
| Dialog and result window heading | 1.25 times | 600 | `DialogLayout` heading (`QLabel#dialogHeading`) |
| Chart title | 1.25 times | bold | `DiagramWindow::applyChartStyle` |
| Section heading, table caption | 1.1 times | bold | `EditorLayout::sectionHeading` |
| Case name | 14 px | 600 | `QLabel#caseNameLabel` |
| Dock title, table header, Open Case, run review values | body | 600 | style sheet |
| Body text, table cells, controls | body | normal | platform |
| Small text: case readiness, panel titles, map key | 11 px | normal, or 600 for the titles and the key toggle | style sheet |

There are two weights, normal and 600. Qt 5 turns `font-weight: 600` into `QFont::Bold`, the weight that `setBold(true)` sets in code, so the sheet and the code give the same face.

## Spacing and shape

- The padding of table headers, tabs, menu items and status labels, and the margins of toolbar separators, are multiples of 4 px.
- Controls keep the padding that gives them the usual platform control height; the exact values are in the sheet.
- Corners have a 2 px radius. The slider handle is round.
- Scroll bars are 10 px wide, without arrow buttons.

## Colour tokens

Contrast ratios follow the WCAG formula for the pair named in the last column.

| Token | Value | Role | Contrast |
| --- | --- | --- | --- |
| Text | `#24313a` | body text, headings, header text | 11.8:1 on window, 13.3:1 on field, 11.0:1 on band |
| Secondary text | `#5b6871` | help, context lines, small titles | 5.1:1 on window, 4.7:1 on band |
| Status text | `#3f4d56` | neutral status label | 7.2:1 on band |
| Disabled text | `#64717a` | disabled controls and menu items | 4.3:1 on disabled fill, 4.5:1 on window |
| Accent | `#315a70` | focus border, checked buttons, selected tab, progress fill, slider | white on accent 7.4:1 |
| Accent dark | `#26495c` | hover of the default button | white on it 9.6:1 |
| Window | `#eef2f5` | main window and panel background | |
| Surface | `#f8fafb` | toolbar, docks, dialogs, menus | |
| Band | `#e3eaee` | dock titles, table headers, tabs, status bar, hover, scroll track, grid lines | |
| Field | `#ffffff` | inputs, buttons, tables | |
| Alternate row | `#f4f7f9` | every second table row | |
| Disabled fill | `#eceeec` | disabled and read-only fields, progress groove | |
| Selected | `#d8e1e5` | selected menu item, pressed button, chosen case | text 10.0:1 |
| Border | `#c9d3db` | borders and rules of fields, tables, docks, tabs, separators | |
| Outline | `#7b8b96` | toolbar button outline, slider groove | 3.5:1 on field |
| Warning | `#f5eadc`, border `#d8b98e`, text `#74451c` | warning status label | 6.8:1 |
| Error | `#f7e8e6`, border `#c98f88`, text `#81352d` | error status label | 7.1:1 |
| Blocking | `#965c22` | case readiness when the case cannot run | 4.8:1 on window |
| Delay late | `#81352d` above 60 s, `#74451c` above 0 s | timetable results | 8.0:1 or more on field, 7.5:1 or more on alternate row |
| Delay not late | `#006400` | timetable results, 0 s or early | 7.4:1 on field, 6.9:1 on alternate row |

The other values of the sheet are state shades of these roles: `#dce6eb` and `#dce3e7` for hover and disabled default buttons, `#d3e3ec` and `#bcd2df` for the checked Follow button, `#9aa5ab` for scroll handles and disabled check marks.

## States

| State | Look |
| --- | --- |
| Enabled | Field fill with the border colour; toolbar buttons with the outline colour |
| Hover | Accent border; tool buttons also take the band fill |
| Focused | Accent border and a 1 px accent outline 1 px outside; the default dialog button has a 2 px outline; a focused check box takes the band fill and an accent border |
| Disabled | Disabled fill, border colour and disabled text |
| Selected | Item views use the platform selection colour; menus and the case chooser use the selected token; tabs and checked tool buttons use the accent with white text |
| Invalid | Fields are not recoloured. The message goes in a status label beside them (`DialogLayout::setStatus`, error or warning tokens), and the Scene Validation dock lists every issue |
| Empty | A help label in secondary text says what is missing (`EditorLayout::emptyStateLabel`); a table with no rows shows only its header; a value that does not exist shows as "-" |

## Tables

- The header is a band: band fill, text colour, weight 600, padding 4 by 8 px, and a border-colour rule to the right of and below each section. The weight is set on `QHeaderView`. Qt sizes header sections with the header font but paints them with the body font, so `MainWindow` gives `QHeaderView` the body font in place of the smaller platform header font of macOS, which would cut the titles.
- Cells are on the field colour with alternate rows and band-coloured grid lines.
- Numeric columns are aligned to the right: the times, orders and delays of the timetable results, and the columns that `DelayAnalysisWindow` and `AreaCapacityWindow` name as numeric, and the headways, references, shifts and gaps of the capacity analysis dialog. Names, ids, codes and statuses are aligned to the left. Qt 5 cannot turn on tabular figures, so digits line up only as far as the platform font has equal digit widths.
- Delays in the timetable results are signed and coloured with the delay tokens.
- The stops table of the Services dock shares the width of the dock among its columns and can cut a header when the dock is narrow; each header has its full text as a tooltip.

## Exceptions

- **Network canvas and railway notation:** drawn in code with their own colours and sizes, described in the [network renderer style map](../ui/renderer-style-map.md). Canvas text uses a default `QFont`, not the sheet.
- **Chart text:** Qt Charts draws axes, ticks and legends with its own fonts; only the title, brushes and pens are set (`DiagramWindow::applyChartStyle`).
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
