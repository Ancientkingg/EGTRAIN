# Peak-memory baselines

`tools/memory/measure_peak_rss.py` records reproducible peak resident-set-size
(RSS) observations for the canonical Copenhagen and Milano-Brescia native runs.
It is a macOS-only, dependency-free measurement tool, not a benchmark or a
memory ceiling.

## Protocol

Configure and build one Release executable, then run the collector while no
other heavy workload is active:

```bash
cmake -S . -B build -DEGTRAIN_BUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt@5
cmake --build build
python3 tools/memory/measure_peak_rss.py \
  --app build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN \
  --output-dir build/memory-measurements/issue-62
```

The tool runs Copenhagen first and Milano-Brescia second. Both use the same
executable and the native full headless scenario:

```text
/usr/bin/time -l <app> --scene <absolute-scene-directory> -g 0 -TSM 0 -RC 0
```

The source metric is macOS `/usr/bin/time -l`'s `maximum resident set size`,
reported in bytes. It is distinct from the tool's `peak memory footprint`
statistic. Elapsed time is wall time observed around that command and is
context only, not a pass/fail threshold.

The generated `peak-rss.json` records the platform and macOS version,
measurement tool, exact command argv, case, scenario, run order, peak RSS in
bytes and derived MiB, elapsed seconds, output paths, executable, and build
type. Raw stdout and stderr are retained beside the record. Runtime output is
routed below the selected output directory. A path inside the checkout is
accepted only below ignored `build/`; source-tree output and symlinked
collector-owned paths are rejected. A usable CMake cache containing the
executable is authoritative for build type, and a conflicting `--build-type`
is rejected. The option supplies fallback metadata only when no containing
cache provides a build type.

A timeout terminates and waits for the native measurement process group. A
timeout, non-zero native run, or malformed `/usr/bin/time` output leaves the raw
logs but no successful JSON record. Review both raw logs and confirm both runs
exited with zero before citing a baseline.

## Baseline recorded 2026-08-29

The protocol above was run alone on macOS 26.6.2 (Darwin 25.6.0, arm64) using
a locally built Release executable. The collector invocation was:

```bash
python3 tools/memory/measure_peak_rss.py \
  --app build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN \
  --output-dir build/memory-measurements/issue-62
```

| Order | Case | Peak RSS (bytes) | Peak RSS (MiB) | Elapsed (s) | Exit |
| ---: | --- | ---: | ---: | ---: | ---: |
| 1 | Copenhagen | 2,157,969,408 | 2,058.0 | 278.430165 | 0 |
| 2 | Milano-Brescia (`Milano_Brescia`) | 2,238,185,472 | 2,134.5 | 7.760147 | 0 |

The generated record and raw logs are under
`build/memory-measurements/issue-62`; runtime results are below its `runtime/`
directory rather than a source directory. `peak-rss.json` contains the exact
absolute native command argv and output path for each row.

Both cases peaked at almost the same value although Milano-Brescia is the much
smaller network: fixed-size arrays that were allocated for every run, whatever
the scene, dominated both. The next baseline was recorded after they were
replaced.

## Baseline recorded 2026-10-09

The runtime arrays are sized from the scene since #463 and #486. The protocol
was run twice on macOS 27.0.1 (Darwin 27.0.0, arm64) with a Release executable
built from `main` at `10cf22c`. Other builds were running on the machine at the
time, so the elapsed times are not those of an idle machine. The peak RSS of
the two runs differs by 1.6 percent for Copenhagen and 0.9 percent for
Milano-Brescia.

| Run | Case | Peak RSS (bytes) | Peak RSS (MiB) | Elapsed (s) | Exit |
| ---: | --- | ---: | ---: | ---: | ---: |
| 1 | Copenhagen | 334,839,808 | 319.3 | 42.293266 | 0 |
| 1 | Milano-Brescia (`Milano_Brescia`) | 95,256,576 | 90.8 | 2.939590 | 0 |
| 2 | Copenhagen | 329,695,232 | 314.4 | 48.098261 | 0 |
| 2 | Milano-Brescia (`Milano_Brescia`) | 96,124,928 | 91.7 | 2.122596 | 0 |

Against the baseline of 2026-08-29 the peak RSS fell from 2,058.0 MiB to about
319 MiB for Copenhagen and from 2,134.5 MiB to about 91 MiB for Milano-Brescia.

### Idle memory after startup

The idle memory is what the process holds before it has loaded a network. It is
measured by starting the executable with a scene path that does not exist: the
process initialises, reports the missing scene and exits.

```text
/usr/bin/time -l <app> --scene <path-that-does-not-exist> -g 0 -TSM 0 -RC 0
```

Three runs of the same executable gave a `maximum resident set size` of
16,662,528, 16,662,528 and 16,695,296 bytes (15.9 MiB) and a
`peak memory footprint` of 5,341,712, 5,341,712 and 5,374,480 bytes (5.1 MiB).
Release v1.0.2 reached about 1.7 GB of resident memory in the same start,
because static constructors touched the fixed arrays before any scene was
loaded (#463).
