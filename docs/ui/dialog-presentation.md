# Custom dialog presentation contract

Use `widgets/DialogLayout.h` for new or migrated custom `QDialog` forms. Supply the system-font heading and short context once, and place fields, units, help, and inline status inside the body widget. Use `dialogHelp=true` for help and `dialogStatus=neutral|warning|error` on a label with explicit message text. Neutral empty and successful states are not warnings. Keep labels associated with their fields and retain keyboard tab order.

`DialogLayout::install(dialog, heading, context, body, buttons)` transfers the body and button box to the dialog. It gives the context and body a resizable scroll viewport while keeping the heading and button box outside it. Keyboard focus entering the body scrolls the focused control into view. Heading and context are forced to plain text so authored names are never interpreted as markup. Its size cap uses the current screen's available geometry; the optional rectangle supports deterministic tests. Margins and title font follow the dialog's system font. Supply a `QDialogButtonBox` with task-appropriate roles and let Qt order its buttons for each platform. The helper does not connect buttons, select a default, validate, accept/reject, or mutate a model. The caller must explicitly wire acceptance and rejection, choose a safe default, and preserve its existing transaction/Cancel semantics. Never make a destructive action the Enter default. Use regular `QFileDialog` and `QMessageBox` unchanged rather than applying the custom layout to them.

The stylesheet retains transitional `runReview*` selectors for compatibility; the migrated review uses the shared font-scaled heading. New presentation selectors apply only to dialogs opting into the helper.

## Callers

Run review, the case chooser, timetable-stop editing, blocking-time scope and capacity options use this layout. Incident delay comparison and capacity analysis results also use it: long run identities and section context wrap as plain text in the scrollable content, tables and secondary CSV/diagram actions remain in the body, and Close stays in the fixed footer. Run review separates its summary from expandable configuration details. The case chooser groups bundled and recent cases and keeps creation/import actions in a secondary menu.

Choice combos whose entries can be long imported names use `widgets/ChoiceComboBox.h` instead of a plain `QComboBox`; the timetable stop editor does. With it, long station or platform names never force a horizontal scroll bar. The field can shrink below its text, while its size hint lets `DialogLayout::fitWidthToContent` widen the dialog to the content within the same width limit as `install`. A selected text that is still wider than its field is drawn with its middle elided. The tooltip is the full current text, and the popup list is as wide as its widest entry within 90 percent of the screen's available width, eliding the middle of entries that still do not fit. Callers set `Qt::ToolTipRole` on an item to explain it, as the timetable stop editor does for an invalid imported platform or a station that is not on the route.

The reference-route chooser for the timetable graph and train paths is a list dialog (`diagrams/RouteReferenceChoice.h`): all choices are visible at once, rows wrap, and a double click or Enter accepts the selected row.

Composition membership uses the standard single-choice `QInputDialog`. File selection, warnings and confirmations retain `QFileDialog` and `QMessageBox`.

`tools/e2e/dialog_presentation_contract.py` checks review cancellation and mode-dependent details against a built application and a creator-acceptance scene. Widget tests cover bounded dimensions, enlarged fonts, scrolling and keyboard access to the fixed footer.
