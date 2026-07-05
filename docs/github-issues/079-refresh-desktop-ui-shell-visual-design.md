# #79 Refresh Desktop UI Shell Visual Design

- GitHub: https://github.com/Ancientkingg/EGTRAIN/issues/79
- State: open
- Labels: `type: feature`, `area: ui`, `priority: p1`
- Created: 2026-07-04
- Mirrored: 2026-07-04

## Summary

Refresh the Qt desktop shell so EGTRAIN reads as a compact railway operations workbench rather than a default Qt sample or a web dashboard.

The issue references:

- `EGTRAIN/QEGTRAIN/mainwindow.ui`
- `EGTRAIN/QEGTRAIN/mainwindow.cpp`
- `EGTRAIN/QEGTRAIN/myQGraphicsView.cpp`
- `docs/ui/2026-07-04-ui-refresh-proposal.md`
- `docs/ui/egtrain-ui-refresh-mock.svg`
- `docs/ui/egtrain-ui-refresh-mock.png`

## Design Constraints

- Avoid gradients, glass effects, blur, glow, decorative cards, oversized rounded corners, and marketing copy.
- Keep the first implementation within Qt Widgets: `QMainWindow`, `QToolBar`, `QDockWidget`, `QStatusBar`, `QSplitter`, `QTabWidget`, `QSlider`, `QComboBox`, and checkable actions.
- Use QSS only where Qt supports it consistently.
- Do not rely on styling the macOS native menu bar or undocked dock widgets.
- Keep the log pane secondary so the network view stays primary.

## Acceptance Criteria

- Main window, toolbar, menus, dock panels, status bar, tabs, buttons, sliders, and combo boxes share one restrained visual system.
- Run controls use recognizable actions with tooltips and keyboard focus states.
- Case study state and layer toggles are visible without opening dialogs.
- Inspector and log panes remain readable at default window size.
- The app remains usable on macOS, Windows, and Linux Qt themes.
- Visual smoke coverage includes the styled shell.

## Notes

The GitHub issue comment embeds the rendered PNG mockup and links the SVG source from the `_gh-attach-assets` release.

`npx impeccable detect docs/ui` returned `ok` on 2026-07-04.
