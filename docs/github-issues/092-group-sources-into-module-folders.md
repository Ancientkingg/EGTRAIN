# #92 Group The Flat QEGTRAIN Sources Into Module Folders

- GitHub: https://github.com/Ancientkingg/EGTRAIN/issues/92
- State: open
- Labels: `refactor`, `type: tech debt`
- Created: 2026-07-05
- Mirrored: 2026-07-05

Part of the source-tree reorganization epic (#91).

## Summary

`EGTRAIN/QEGTRAIN/` holds 119 tracked `.cpp`, `.h`, `.hpp`, `.ui`, and `.qrc` files in a single directory. Finding the network canvas, the scene importer, or the RNG means scanning one long alphabetical list. This issue moves the files into module folders without renaming any file or class, so the diff is purely relocation plus include and CMake path updates.

## Scope

Move every source into one of: `app/`, `simulation/`, `scene/`, `graphics/` (with `graphics/items/`), `widgets/`, `diagrams/`, `io/` (with `io/third_party/`), `util/`. Tests stay in `tests/`.

Then:

- Rewrite `#include` lines to be path-qualified, for example `#include "simulation/Signalling.h"`.
- Update the file paths in `CMakeLists.txt` (`EGTRAIN_SOURCES`, `EGTRAIN_HEADERS`, `EGTRAIN_UI`, `EGTRAIN_RESOURCES`, the `scene_tool` target, and all 11 test targets).
- `${SRC_DIR}` is already on the include path, so path-qualified includes resolve with no per-target include change.

## Out of scope

No file or class renames. Those are #93, #94, and #95.

## Acceptance criteria

- No `.cpp`/`.h`/`.hpp`/`.ui`/`.qrc` file remains directly under `EGTRAIN/QEGTRAIN/` except the folders and `tests/`.
- `cmake --build` succeeds; `ctest` reports 12/12.
- `git log --follow` still traces each moved file (moves done with `git mv`).

