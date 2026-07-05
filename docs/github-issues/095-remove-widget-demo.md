# #95 Remove Or Repurpose The Leftover Widget Demo

- GitHub: https://github.com/Ancientkingg/EGTRAIN/issues/95
- State: open
- Labels: `type: tech debt`
- Created: 2026-07-05
- Mirrored: 2026-07-05

Part of the source-tree reorganization epic (#91).

## Summary

`Widget.cpp`, `Widget.h`, and `Widget.ui` look like the leftover of a `new Qt Widgets` template. `Widget.ui` is a bare `QWidget` named `Widget` with a single `QPushButton`. `Widget.h` is a header with a long list of `#include` lines and no declared members. All three are still compiled into the `QEGTRAIN` target.

## Task

Check whether anything constructs or references the `Widget` class or `ui_Widget.h` outside the three `Widget.*` files. If nothing does, delete the three files and remove them from `CMakeLists.txt` (`EGTRAIN_SOURCES`, `EGTRAIN_HEADERS`, `EGTRAIN_UI`).

If something does depend on it, rename it to describe its actual use instead of deleting it, and record what that use is in this issue.

## Acceptance criteria

- Either the `Widget.*` files are gone and dropped from CMake, or they are renamed with a note explaining the dependency.
- `cmake --build` succeeds; `ctest` reports 12/12.

