# Track Preview Fallback Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show legacy track geometry as a non-interactive preview whenever a V1 scene opens, and retain it when validation blocks simulation.

**Architecture:** A pure C++17 helper reads track nodes and optional connections from `legacy/TrackLines` or `legacy/Tracklines` without initializing simulator globals. `MainWindow` converts the returned geometry into light-gray `QGraphicsPathItem` objects, fits the view, and relies on the existing successful-run teardown and `setupGUI()` path to replace the preview.

**Tech Stack:** C++17 standard library, Qt 5 Widgets/Gui, CMake, CTest, Bash GUI smoke tests.

## Global Constraints

- Preserve legacy V1 scene compatibility and the existing Qt, CMake, simulation, and scene-model architecture.
- Add no dependency: parsing uses `std::filesystem`, `std::ifstream`, and `std::istringstream`.
- Preview failures are warnings only and never become scene validation errors.
- Draw tracks and connections only; do not invent trains, routes, signalling, blocks, station labels, or timetable data.
- Keep the preview visible when `Run Scene` is blocked; a successful run replaces it through the existing teardown and `setupGUI()` path.
- Preserve raw X/Y geometry and use only a small stable vertical offset based on numeric track order to separate coincident lines.
- Run application commands from `EGTRAIN/QEGTRAIN` so legacy relative paths resolve.
- Prefix every shell command with `rtk`.

---

### Task 1: Parse legacy track-preview geometry

**Files:**
- Create: `EGTRAIN/QEGTRAIN/scene/TrackPreview.h`
- Create: `EGTRAIN/QEGTRAIN/scene/TrackPreview.cpp`
- Create: `EGTRAIN/QEGTRAIN/tests/test_trackpreview.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: a V1 scene directory containing either `legacy/TrackLines` or `legacy/Tracklines`.
- Produces: `TrackPreviewResult loadTrackPreview(const std::string& sceneDir)` with numerically ordered `TrackPreviewLine` records, parsed `TrackPreviewConnection` records, and non-blocking warning strings.

- [ ] **Step 1: Add the parser regression test and CMake target before production code**

Create `test_trackpreview.cpp` with a temporary-directory helper and real files. The test must cover both directory spellings, `B2` before `B10`, valid nodes and connections, malformed rows, one-point tracks, nonnumeric `B` directories, and a missing preview directory:

```cpp
#include "scene/TrackPreview.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

static bool expect(bool condition, const char* message) {
    if (!condition)
        std::cerr << "failed: " << message << "\n";
    return condition;
}

struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() /
               ("track_preview_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

static void writeFile(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path) << text;
}

int main() {
    bool ok = true;
    {
        TempDir scene;
        const fs::path tracks = scene.path / "legacy" / "TrackLines";
        writeFile(tracks / "B10" / "NodiCumPari.txt", "1 10 0\n2 20 1\n");
        writeFile(tracks / "B2" / "NodiCumPari.txt", "bad row\n1 0 0\n2 5 0\n");
        writeFile(tracks / "B3" / "NodiCumPari.txt", "1 3 0\n");
        writeFile(tracks / "Bbad" / "NodiCumPari.txt", "1 0 0\n2 1 0\n");
        writeFile(tracks / "Connections.txt", "2 4.5 10 10.5\ninvalid\n");

        const auto result = loadTrackPreview(scene.path.string());
        ok &= expect(result.lines.size() == 2, "keeps only tracks with at least two valid points");
        ok &= expect(result.lines[0].id == 2 && result.lines[1].id == 10, "sorts B directories numerically");
        ok &= expect(result.lines[0].points.size() == 2, "skips malformed node rows");
        ok &= expect(result.connections.size() == 1, "parses valid connections and skips malformed rows");
        ok &= expect(result.connections[0].firstTrackId == 2 && result.connections[0].secondTrackId == 10,
                     "preserves connection track ids");
        ok &= expect(!result.warnings.empty(), "reports non-blocking diagnostics for malformed input");
    }
    {
        TempDir scene;
        const fs::path tracks = scene.path / "legacy" / "Tracklines";
        writeFile(tracks / "B0" / "NodiCumPari.txt", "1 0 0\n2 1 0\n");
        ok &= expect(loadTrackPreview(scene.path.string()).lines.size() == 1,
                     "accepts legacy Tracklines spelling");
    }
    {
        TempDir scene;
        const auto result = loadTrackPreview(scene.path.string());
        ok &= expect(result.lines.empty(), "missing preview directory returns no lines");
        ok &= expect(!result.warnings.empty(), "missing preview directory reports a warning");
    }
    return ok ? 0 : 1;
}
```

Add `${SRC_DIR}/scene/TrackPreview.cpp` and `.h` to the application source/header lists. Add this test target and compatibility label:

```cmake
add_executable(test_trackpreview
    ${SRC_DIR}/tests/test_trackpreview.cpp
    ${SRC_DIR}/scene/TrackPreview.cpp)
add_test(NAME test_trackpreview COMMAND test_trackpreview)

set_tests_properties(test_trackpreview PROPERTIES LABELS "legacy-compat")
```

- [ ] **Step 2: Configure and run the new target to verify the red state**

Run:

```bash
rtk cmake -S . -B build -DEGTRAIN_BUILD_TESTS=ON -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt@5
rtk cmake --build build --target test_trackpreview
```

Expected: build fails because `scene/TrackPreview.h` and the parser implementation do not exist yet.

- [ ] **Step 3: Add the minimal parser interface**

Create `TrackPreview.h`:

```cpp
#ifndef TRACKPREVIEW_H
#define TRACKPREVIEW_H

#include <string>
#include <vector>

struct TrackPreviewPoint {
    double x = 0.0;
    double y = 0.0;
};

struct TrackPreviewLine {
    int id = -1;
    std::vector<TrackPreviewPoint> points;
};

struct TrackPreviewConnection {
    int firstTrackId = -1;
    double firstX = 0.0;
    int secondTrackId = -1;
    double secondX = 0.0;
};

struct TrackPreviewResult {
    std::vector<TrackPreviewLine> lines;
    std::vector<TrackPreviewConnection> connections;
    std::vector<std::string> warnings;
};

TrackPreviewResult loadTrackPreview(const std::string& sceneDir);

#endif
```

- [ ] **Step 4: Implement only the file discovery and parsing required by the test**

Create `TrackPreview.cpp`:

```cpp
#include "scene/TrackPreview.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

bool parseTrackId(const std::string& name, int& id) {
    if (name.size() < 2 || name.front() != 'B')
        return false;
    const char* begin = name.data() + 1;
    const char* end = name.data() + name.size();
    const auto parsed = std::from_chars(begin, end, id);
    return parsed.ec == std::errc() && parsed.ptr == end;
}

} // namespace

TrackPreviewResult loadTrackPreview(const std::string& sceneDir) {
    TrackPreviewResult result;
    try {
        fs::path root = fs::path(sceneDir) / "legacy" / "TrackLines";
        if (!fs::is_directory(root))
            root = fs::path(sceneDir) / "legacy" / "Tracklines";
        if (!fs::is_directory(root)) {
            result.warnings.push_back("legacy track preview directory is missing");
            return result;
        }

        std::vector<std::pair<int, fs::path>> trackDirs;
        for (const auto& entry : fs::directory_iterator(root)) {
            if (!entry.is_directory())
                continue;
            int id = -1;
            if (!parseTrackId(entry.path().filename().string(), id)) {
                result.warnings.push_back("ignored nonnumeric track directory " + entry.path().filename().string());
                continue;
            }
            trackDirs.emplace_back(id, entry.path());
        }
        std::sort(trackDirs.begin(), trackDirs.end(), [](const auto& left, const auto& right) {
            return left.first < right.first;
        });

        for (const auto& trackDir : trackDirs) {
            TrackPreviewLine line;
            line.id = trackDir.first;
            const fs::path nodesPath = trackDir.second / "NodiCumPari.txt";
            std::ifstream nodes(nodesPath);
            if (!nodes) {
                result.warnings.push_back("could not read " + nodesPath.string());
                continue;
            }
            std::string row;
            while (std::getline(nodes, row)) {
                std::istringstream values(row);
                int nodeId = -1;
                TrackPreviewPoint point;
                if (!(values >> nodeId >> point.x >> point.y) || !std::isfinite(point.x) || !std::isfinite(point.y)) {
                    result.warnings.push_back("ignored malformed node row in B" + std::to_string(line.id));
                    continue;
                }
                line.points.push_back(point);
            }
            if (line.points.size() < 2) {
                result.warnings.push_back("ignored track B" + std::to_string(line.id) + " with fewer than two points");
                continue;
            }
            result.lines.push_back(std::move(line));
        }

        const fs::path connectionsPath = root / "Connections.txt";
        std::ifstream connections(connectionsPath);
        std::string row;
        while (connections && std::getline(connections, row)) {
            std::istringstream values(row);
            TrackPreviewConnection connection;
            if (!(values >> connection.firstTrackId >> connection.firstX
                         >> connection.secondTrackId >> connection.secondX)
                || !std::isfinite(connection.firstX) || !std::isfinite(connection.secondX)) {
                result.warnings.push_back("ignored malformed connection row");
                continue;
            }
            result.connections.push_back(connection);
        }
    } catch (const fs::filesystem_error& error) {
        result.warnings.push_back(error.what());
    }
    return result;
}
```

The implementation must keep parsing local to this helper and must not include simulation headers or touch `BlockSet`, `Node`, `Arc`, or global input paths.

- [ ] **Step 5: Build and run the parser test to verify green**

Run:

```bash
rtk cmake --build build --target test_trackpreview
rtk ctest --test-dir build -R '^test_trackpreview$' --output-on-failure
```

Expected: `test_trackpreview` passes with zero failures.

- [ ] **Step 6: Commit the parser slice**

```bash
rtk git add CMakeLists.txt EGTRAIN/QEGTRAIN/scene/TrackPreview.h EGTRAIN/QEGTRAIN/scene/TrackPreview.cpp EGTRAIN/QEGTRAIN/tests/test_trackpreview.cpp
rtk git commit -m "feat: parse legacy track previews"
```

---

### Task 2: Render the automatic preview and retain it on blocked runs

**Files:**
- Modify: `EGTRAIN/QEGTRAIN/app/MainWindow.h`
- Modify: `EGTRAIN/QEGTRAIN/app/MainWindow.cpp`
- Create: `tools/e2e/track_preview_smoke.sh`

**Interfaces:**
- Consumes: `TrackPreviewResult loadTrackPreview(const std::string&)` from Task 1.
- Produces: private `void renderTrackPreview(const QString& sceneDir)` and `void runTrackPreviewE2E()` methods.

- [ ] **Step 1: Add the GUI smoke hook before rendering production code**

Add `runTrackPreviewE2E()` to the existing environment-gated E2E methods. Add `QEGTRAIN_E2E_TRACK_PREVIEW` to `suppressBlockingDialogs()` and schedule the hook from `showEvent()`.

The hook opens `QEGTRAIN_E2E_SCENE`, requires validation errors, records the graphics item count and bounds, calls `runScene()`, and fails unless the blocked run leaves the same non-empty, horizontally spread preview in the viewport:

```cpp
void MainWindow::runTrackPreviewE2E() {
    if (m_e2eFinished)
        return;
    m_e2eFinished = true;

    bool ok = true;
    QStringList failures;
    const QString scenePath = qEnvironmentVariable("QEGTRAIN_E2E_SCENE");
    if (scenePath.isEmpty() || !openSceneDirectory(scenePath)) {
        ok = false;
        failures << "scene did not open";
    }

    const int itemCount = scene->items().size();
    const QRectF bounds = scene->itemsBoundingRect();
    if (!hasErrors(m_sceneDiagnostics)) {
        ok = false;
        failures << "scene unexpectedly passed validation";
    }
    if (itemCount < 2) {
        ok = false;
        failures << "too few preview items";
    }
    if (bounds.width() < 10.0) {
        ok = false;
        failures << "preview geometry did not spread horizontally";
    }
    const QRectF visible = networkView->mapToScene(networkView->viewport()->rect()).boundingRect();
    if (!visible.intersects(bounds)) {
        ok = false;
        failures << "preview is outside the viewport";
    }

    runScene();
    QApplication::processEvents();
    if (m_worker) {
        ok = false;
        failures << "invalid scene started simulation";
    }
    if (scene->items().size() != itemCount || scene->itemsBoundingRect() != bounds) {
        ok = false;
        failures << "blocked run removed the preview";
    }

    if (ok) {
        std::fprintf(stdout, "E2E_TRACK_PREVIEW_OK\n");
        std::fflush(stdout);
        QCoreApplication::exit(0);
        return;
    }
    std::fprintf(stderr, "E2E_TRACK_PREVIEW_FAIL: %s\n", failures.join(", ").toStdString().c_str());
    std::fflush(stderr);
    QCoreApplication::exit(2);
}
```

Create `tools/e2e/track_preview_smoke.sh`. It accepts an optional scene path. Without one, it copies the committed `Assignment_Gvc_Gdg_Ut` scene and empties its services so the default smoke input is incomplete but still has valid legacy tracks:

```bash
#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP="$ROOT/build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN"
LOG="${TMPDIR:-/tmp}/qegtrain-track-preview-e2e.log"
TMP_ROOT=""

if [[ ! -x "$APP" ]]; then
    echo "QEGTRAIN app not found or not executable: $APP" >&2
    exit 1
fi

if [[ $# -gt 0 ]]; then
    SCENE="$1"
else
    TMP_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/qegtrain-track-preview.XXXXXX")"
    trap 'rm -rf "$TMP_ROOT"' EXIT
    SCENE="$TMP_ROOT/scene"
    cp -R "$ROOT/EGTRAIN/QEGTRAIN/Scenes/Assignment_Gvc_Gdg_Ut" "$SCENE"
    python3 -c 'from pathlib import Path; import json, sys; Path(sys.argv[1]).write_text(json.dumps({"services": []}))' "$SCENE/services.json"
fi

set +e
cd "$ROOT/EGTRAIN/QEGTRAIN"
QEGTRAIN_E2E_TRACK_PREVIEW=1 \
QEGTRAIN_E2E_SCENE="$SCENE" \
"$APP" -n 3 -h 100 -g 1 -pax 0 -TSM 0 -RC 0 >"$LOG" 2>&1
APP_EXIT=$?
set -e

if [[ "$APP_EXIT" -eq 0 ]] && grep -q "E2E_TRACK_PREVIEW_OK" "$LOG"; then
    echo "track preview smoke passed"
else
    echo "track preview smoke failed (app exit $APP_EXIT)" >&2
    tail -20 "$LOG" >&2
    exit 1
fi
```

- [ ] **Step 2: Build and run the smoke test to verify the red state**

Run:

```bash
rtk cmake --build build --target QEGTRAIN
rtk tools/e2e/track_preview_smoke.sh
```

Expected: the smoke test fails with too few graphics items or collapsed preview bounds because scene opening still clears the network without rendering tracks.

- [ ] **Step 3: Add the minimal preview renderer**

Declare `void renderTrackPreview(const QString& sceneDir);` near the scene-opening helpers. In `MainWindow.cpp`, include `scene/TrackPreview.h` and add this file-local interpolation helper:

```cpp
static bool previewPointAtX(const TrackPreviewLine& line, double x, qreal offset, QPointF& point) {
    if (line.points.empty())
        return false;
    for (std::size_t index = 1; index < line.points.size(); ++index) {
        const auto& first = line.points[index - 1];
        const auto& second = line.points[index];
        if (x < std::min(first.x, second.x) || x > std::max(first.x, second.x))
            continue;
        const double span = second.x - first.x;
        const double ratio = span == 0.0 ? 0.0 : (x - first.x) / span;
        point = QPointF(x, first.y + ratio * (second.y - first.y) + offset);
        return true;
    }
    const auto closest = std::min_element(line.points.begin(), line.points.end(), [x](const auto& left, const auto& right) {
        return std::abs(left.x - x) < std::abs(right.x - x);
    });
    point = QPointF(closest->x, closest->y + offset);
    return true;
}
```

Implement the renderer:

```cpp
void MainWindow::renderTrackPreview(const QString& sceneDir) {
    const TrackPreviewResult preview = loadTrackPreview(sceneDir.toStdString());
    if (preview.lines.empty()) {
        statusBar()->showMessage("Scene loaded - no track preview available; simulation not running");
        return;
    }

    QPen pen(QColor(185, 190, 198));
    pen.setWidthF(2.0);
    pen.setCosmetic(true);

    std::map<int, std::pair<const TrackPreviewLine*, qreal>> tracks;
    for (std::size_t index = 0; index < preview.lines.size(); ++index) {
        const auto& line = preview.lines[index];
        const qreal offset = static_cast<qreal>(index) * 3.0;
        tracks[line.id] = {&line, offset};
        QPainterPath path(QPointF(line.points.front().x, line.points.front().y + offset));
        for (std::size_t point = 1; point < line.points.size(); ++point)
            path.lineTo(line.points[point].x, line.points[point].y + offset);
        auto* item = scene->addPath(path, pen);
        item->setAcceptedMouseButtons(Qt::NoButton);
    }

    for (const auto& connection : preview.connections) {
        const auto first = tracks.find(connection.firstTrackId);
        const auto second = tracks.find(connection.secondTrackId);
        if (first == tracks.end() || second == tracks.end())
            continue;
        QPointF start;
        QPointF end;
        if (!previewPointAtX(*first->second.first, connection.firstX, first->second.second, start)
            || !previewPointAtX(*second->second.first, connection.secondX, second->second.second, end))
            continue;
        QPainterPath path(start);
        path.lineTo(end);
        auto* item = scene->addPath(path, pen);
        item->setAcceptedMouseButtons(Qt::NoButton);
    }

    fitView();
    statusBar()->showMessage(QString("Previewing %1 trackline(s); simulation not running")
                                 .arg(static_cast<int>(preview.lines.size())));
}
```

Call `renderTrackPreview(scenePath);` at the end of `openSceneDirectory()` after the editor panels refresh. Do not change the validation-first branch in `runScene()` or the existing teardown/setup sequence after validation.

- [ ] **Step 4: Build and rerun the smoke test to verify green**

Run:

```bash
rtk cmake --build build --target QEGTRAIN
rtk tools/e2e/track_preview_smoke.sh
```

Expected: `E2E_TRACK_PREVIEW_OK`, zero exit, multiple graphics items, nonzero horizontal bounds, and identical items/bounds after the blocked run.

- [ ] **Step 5: Run existing GUI regression smoke tests**

Run:

```bash
rtk tools/e2e/editor_smoke.sh
rtk tools/e2e/scene_render_smoke.sh
rtk tools/e2e/visual_polish_smoke.sh
```

Expected: all three scripts exit zero and print their pass markers.

- [ ] **Step 6: Commit the rendering slice**

```bash
rtk git add EGTRAIN/QEGTRAIN/app/MainWindow.h EGTRAIN/QEGTRAIN/app/MainWindow.cpp tools/e2e/track_preview_smoke.sh
rtk git commit -m "feat: preview legacy tracks on scene open"
```

---

### Task 3: Record the canonical-infrastructure migration and verify the feature

**Files:**
- Modify: `docs/guides/v1-scene-properties.md`
- Modify: `docs/planning/2026-07-13-track-preview-fallback-implementation-plan.md`

**Interfaces:**
- Consumes: the compatibility parser and rendering path from Tasks 1 and 2.
- Produces: three issue-ready future work items that define how the compatibility path will be deleted.

- [ ] **Step 1: Add the three future issue descriptions**

Under `Work that removes the legacy dependency`, add:

```markdown
The compatibility preview should be removed through three ordered follow-up issues:

1. **Define and persist canonical infrastructure.** Specify V1 fields and validation for tracks, nodes, arcs, switches, connections, gradients, speed limits, blocks, stations, and signalling references. Update scene loading and writing so those values round-trip instead of leaving `nodes` and `arcs` empty.
2. **Import legacy infrastructure into the canonical model.** Convert `TrackLines`, `Connections.txt`, station anchors, block data, and signalling topology into those fields. Cover all committed case studies with import, validation, and round-trip tests.
3. **Render and simulate canonical infrastructure.** Build the network view and simulator topology from `SceneModel`, then delete `TrackPreview`, the compatibility rendering hook, and the runtime dependency on `legacy/TrackLines` once canonical coverage is complete.

These issues form a dependency chain: schema and persistence first, import second, native rendering and simulation third.
```

- [ ] **Step 2: Run the full verification gate**

Run:

```bash
rtk cmake -S . -B build -DEGTRAIN_BUILD_TESTS=ON -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt@5
rtk cmake --build build
rtk ctest --test-dir build --output-on-failure
rtk tools/e2e/track_preview_smoke.sh
rtk tools/e2e/editor_smoke.sh
rtk tools/e2e/scene_render_smoke.sh
rtk tools/e2e/visual_polish_smoke.sh
rtk tools/e2e/headless_smoke.py
rtk python3 tools/e2e/roundtrip_smoke.py
```

Expected: build exit zero, CTest reports zero failures, and every smoke script reports its pass marker.

- [ ] **Step 3: Verify the real converted Lebanon scene**

Run:

```bash
rtk tools/e2e/track_preview_smoke.sh /Users/samuelbruin/Downloads/Lebanon-v1
```

Expected: the eight Lebanon tracklines render, validation remains blocking, and `E2E_TRACK_PREVIEW_OK` is printed.

- [ ] **Step 4: Inspect scope and update the knowledge graph**

Run:

```bash
rtk git diff --check
rtk git status --short
rtk git diff --stat HEAD~2
rtk graphify update .
```

Expected: no whitespace errors, only planned source/test/documentation changes plus the user's existing untracked `.codex/`, and a successful incremental graph update.

- [ ] **Step 5: Commit documentation and graph metadata if tracked**

```bash
rtk git add docs/guides/v1-scene-properties.md docs/planning/2026-07-13-track-preview-fallback-implementation-plan.md
rtk git commit -m "docs: track canonical infrastructure migration"
```
