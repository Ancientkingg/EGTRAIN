# Playback profiling

Playback profiling is an opt-in measurement tool for Copenhagen GUI playback. It does not run unless `QEGTRAIN_PLAYBACK_PROFILE=1` is set. Startup timing and playback profiling cannot run together.

## Recorded protocol

Use a Release build on native macOS Cocoa:

```bash
tools/performance/playback_profile.py \
  --app <release-app> \
  --scene <copenhagen-scene> \
  --scenario default \
  --views fit dense \
  --duration 30 \
  --step-delay 250 \
  --trials 3 \
  --output <new-output-dir>
```

The output directory must be new, non-symlinked, outside the repository, application, and scene input. Each process uses a separate temporary `QEGTRAIN_OUTPUT_DIR` which is removed after the trial.

The recorded settings are Copenhagen, its default baseline scenario, passenger GUI on, traffic-state monitoring and route choice off, an 8000-second horizon, a 250 ms step delay, 30 measured seconds, and three independent processes per view. Fit calls `fitToTopology()`, disables follow, retains the existing speed-label setting, and uses zoom ratio 1. Dense first fits, selects the lowest-index visible train, centers it, applies `zoomBy(3.0)`, and leaves follow off.

## Measurement boundary

Scene loading, native preparation, `setupGUI()`, `startSimulation()`, and waiting for the first visible train are outside the window. The requested view is applied before measurement. The first completed viewport paint after that view resets all aggregates and starts the monotonic window. At expiry, aggregates freeze before the worker is asked to stop. Records are emitted only after `simulationFinished` reaches the GUI thread.

The profiler records these paths; animation is conditional, so a trial with no movement reports `gui/train_animation/value_changed` as an unobserved optional path while every other listed path remains mandatory.

- Worker: `worker/playback_step/compute`; its five direct children `passenger_entry_platform_refresh`, `train_movement`, `train_passenger_state_payload`, `passenger_status_output`, and `infrastructure_signalling_cleanup`; `worker/playback_step/snapshot_build_publish`; and its `build_gui_snapshot` and `mailbox_publish` children.
- GUI: `gui/snapshot_delivery`, `timeline`, throttled `render_frame`, and its `signalling`, `train_position`, `platforms`, `passenger_icons`, and `train_passenger_info` children.
- Animation: `gui/train_animation/value_changed`.
- Rendering: `render/viewport_paint` and `render/viewport_paint/background`.

The worker compute scope starts after pause, stop, and step-delay handling and ends before snapshot publication. Its five children are unconditional sequential scopes called once per compute step. `passenger_entry_platform_refresh` combines journey starts with the optional all-platform waiting-list refresh. `train_movement` covers trajectory calculation and per-train movement records. `train_passenger_state_payload` combines train-passenger interaction with construction of the per-train traffic-state payload. `passenger_status_output` covers writing the current passenger status. `infrastructure_signalling_cleanup` combines occupancy, failures, movement authorities, station protection, signalling activation and release, track unlocking, occupied-list cleanup, and operating-order detection.

Compute self time is the residual after subtracting those five direct children. It intentionally retains progress output, traffic-state and route-choice external sharing, CPU-time bookkeeping, gaps between the compound phases, and profiler overhead. Snapshot deliveries, rendered updates, completed paints, and start/end timesteps are counted separately. Calls, inclusive totals, and extrema cover every observation. Median and p95 use a bounded deterministic streaming reservoir across the full window rather than retaining only early samples.

## Evidence and structural checks

`--structural-test` permits an offscreen, short run to check automation, begin/freeze/clean-stop behavior, and record structure. Structural output is always labelled `structural` and is not performance evidence. Recorded evidence is rejected unless every trial reports Release, native macOS Cocoa, the exact recorded settings, a clean stop, advancing timesteps, and at least 100 snapshot deliveries, 100 rendered updates, and 100 completed paints.

The harness persists only:

- `records.jsonl`: validated run, aggregate, and completion records.
- `summary.json`: schema 3, the exact `structural` or `recorded` mode and evidence status, separate Fit and dense rankings, each trial's direct-child-subtracted self total, each compute child's inclusive share of its compute parent, median trial self totals, median per-call median/p95, decision results, and valid dense-to-Fit comparisons.
- `report.txt`: the same decision inputs and compute-parent shares in readable form, settings, and a path-safe reproducible command.

It does not retain process output, environments, paths, binary details, identifiers, timestamps, or unrelated logs.

## Decision rule

A follow-up is material only when the same path dominates at least two of three trials and accounts for at least 20 percent of its parent lane's self-accounted time and 5 percent of the 30-second window. Run the protocol before drawing any optimization conclusion.
