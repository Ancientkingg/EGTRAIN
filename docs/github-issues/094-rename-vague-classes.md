# #94 Rename The Remaining Vague Classes

- GitHub: https://github.com/Ancientkingg/EGTRAIN/issues/94
- State: open
- Labels: `refactor`, `type: tech debt`
- Created: 2026-07-05
- Mirrored: 2026-07-05

Part of the source-tree reorganization epic (#91).

## Summary

Five names outside the graphics layer describe their origin or nothing at all rather than their role.

| Current | New | Reason |
| --- | --- | --- |
| `EGTRAIN` (class in `EGTRAIN.cpp/.h`) | `DispatchController` | A `QObject` labelled "dispatching tool" in its header. Sharing the class name with the project, the binary (`QEGTRAIN`), and the source folder makes every mention ambiguous. |
| `IOClass` | `RailMLParser` | Holds the `RailML` XML reader and a `ZMQ_channel`. The file name says nothing about either. |
| `Owl` | `Logger` | The project logger. `Owl.hpp` defines `Owl`, `OwlSettings`, the global `logger`, and the `eglogger` macro. |
| `dispDecision` | `DispatchDecision` | A dispatch decision record, abbreviated. |
| `initialParameters` (`initial_parameters`) | `InitialParameters` | Simulation start parameters, in mixed snake and camel case. |

## Notes

- The `EGTRAIN` rename touches only the class named `EGTRAIN`, not the project name, the `QEGTRAIN` target, the `EGTRAIN/QEGTRAIN` path, or the `EGTRAIN_*` CMake options and macros. Rename the identifier, not the string.
- `Owl` renames the class, `OwlSettings` to `LoggerSettings`, and the `OwlShutdown` macro, while keeping the global instance name `logger`.
- Also normalize `mainwindow.{cpp,h,ui}` to `MainWindow` so the file matches its class; update the `ui_mainwindow.h` include to `ui_MainWindow.h`.

## Acceptance criteria

- No class named `EGTRAIN`, `IOClass`, `Owl`, `dispDecision`, or `initial_parameters` remains.
- The `QEGTRAIN` target name, bundle identifier, and `EGTRAIN_*` CMake options are unchanged.
- `cmake --build` succeeds; `ctest` reports 12/12.

