# Stabilization execution ledger

- Initial `origin/main`: `935d94227fa3ebc61de371a52d663e711a3e4805`
- Release status: P0 blockers cleared; blocked by remaining P1 findings G-04 and G-08
- Active milestone: 4, result integrity

| Milestone | Findings | Status | Issue | Pull request | Merge |
|---|---|---|---|---|---|
| 1. Runtime memory safety | G-01, G-05, G-06; P-03 | Complete | [#306](https://github.com/Ancientkingg/EGTRAIN/issues/306) | [#307](https://github.com/Ancientkingg/EGTRAIN/pull/307) | `743fe85798aef38a6862b989434650dc87106b2b` |
| 2. Deterministic simulation state | G-02; P-04 | Complete | [#308](https://github.com/Ancientkingg/EGTRAIN/issues/308) | [#309](https://github.com/Ancientkingg/EGTRAIN/pull/309) | `d029582fcb230c8419e9332bfdb608c792974384` |
| 3. External communication lifecycle | G-03, G-07; P-05 | Complete | [#310](https://github.com/Ancientkingg/EGTRAIN/issues/310) | [#311](https://github.com/Ancientkingg/EGTRAIN/pull/311) | `d2c8dd761808c45deaa3f44a2348e10c5ba3c37a` |
| 4. Result integrity | G-04; P-02 | In progress | [#312](https://github.com/Ancientkingg/EGTRAIN/issues/312) | Pending | Pending |
| 5. Persistence and validation hardening | G-08, G-09, G-10 | Blocked on milestone 4 | Pending | Pending | Pending |
| 6. Station platform-booking verification | G-NV-01 | Blocked on release fixes | Pending | Pending | Pending |
| 7. Dead-code cleanup | P-01, P-06 | Blocked on correctness work | Pending | Pending | Pending |

## Milestone 1: runtime memory safety

- Finding IDs: G-01, G-05, G-06, P-03
- Branch: `fix/runtime-memory-safety`
- Worktree: `/Users/samuelbruin/Downloads/EGTRAIN/local/worktrees/runtime-memory-safety`
- Base SHA: `935d94227fa3ebc61de371a52d663e711a3e4805`
- Scope: zero-stop through-service runtime handling, bounded station-area lookahead, time-zero initialization, and focused regressions
- Files changed: `CMakeLists.txt`; `EGTRAIN/QEGTRAIN/app/DispatchController.cpp`; `EGTRAIN/QEGTRAIN/simulation/RollingStock.cpp`; `EGTRAIN/QEGTRAIN/simulation/RollingStock.h`; `EGTRAIN/QEGTRAIN/simulation/Simulation.cpp`; `EGTRAIN/QEGTRAIN/tests/test_operationsbuilder.cpp`; `tools/e2e/runtime_memory_safety_smoke.py`; this ledger
- Test results: focused normal CTest 2/2 passed; focused ASan/UBSan CTest 2/2 passed; final full CTest 46/46 passed; six-case native headless passed; six-case roundtrip passed
- Adversarial-review findings: the first review found one P2 empty-sample non-finite result; the focused re-review found one medium stale-total/max error on a reused aggregate; final re-review found no remaining issues; no P0/P1 findings
- Fixes made from review: empty final-station samples now reset the complete exported metric set to the existing unavailable/zero semantics, including stale totals and maxima; focused tests cover reused-state values and passed under normal and ASan/UBSan builds
- Ponytail findings: accepted the `std::find` replacement for a manual membership loop and the single guarded station-delay log expression; rejected replacing the required bounded +1/+2 loop with duplicated comparisons because P-03 explicitly requires the loop
- Simplifications made: consolidated duplicated +1/+2 station-area lookahead into one bounded loop, removed the manual occupancy flag/scan, and emitted the guarded station-delay log through one expression
- Commit SHA: `f1a7e581f9ae618d98818210756276c4a423a155`
- PR number and URL: [#307](https://github.com/Ancientkingg/EGTRAIN/pull/307)
- CI status: passed in 9m55s
- Merge SHA: `743fe85798aef38a6862b989434650dc87106b2b`
- Remaining blockers: G-02

### Execution log

- 2026-08-19: Fetched `origin`; the reviewed SHA remains the latest `origin/main`.
- 2026-08-19: Confirmed no existing issue owns G-01, G-05, and G-06; opened issue [#306](https://github.com/Ancientkingg/EGTRAIN/issues/306).
- 2026-08-19: Created the milestone branch and clean isolated worktree from the recorded base SHA.
- 2026-08-19: Traced the live dispatch path and all compiled callers. The shared fix points are `Train::trajectoryComputationIncludingMovingBlock()`, `Train::checkTrainArrDep()`, `protectStationAreas()`, final-station aggregation, and the dispatch traffic-state lookup.
- 2026-08-19: Mechanical audit confirmed no upstream partial fix and identified the existing `test_operationsbuilder` runtime fixture plus CTest sanitizer wiring as the smallest regression seams.
- 2026-08-19: Implemented explicit zero-stop handling, time-zero initialization, delayed-index guards, and route-length-bounded station-area lookahead. Added focused unit coverage and a canonical Milano plus modified Paimpol live-runtime smoke.
- 2026-08-19: Focused normal and ASan/UBSan CTest each passed 2/2. Full normal CTest passed 46/46, and all six scene roundtrips passed.
- 2026-08-19: Independent correctness review found one P2 empty-sample statistics gap. The zero-stop final-station path now preserves the existing unavailable sentinel, with a regression assertion; focused normal and sanitizer tests passed again.
- 2026-08-19: Simplicity review produced three suggestions. Two local reductions were accepted; the proposed duplicated lookahead comparisons were rejected because they would undo P-03.
- 2026-08-19: Focused re-review identified stale totals and maxima on a reused empty aggregate. Reset the complete exported metric set, added stale-value assertions for both calculation paths, and passed focused normal and sanitizer CTest again.
- 2026-08-19: Final independent re-review found no remaining issues. Final full CTest passed 46/46; six-case native headless and six-case roundtrip both passed.
- 2026-08-19: Refreshed the local code knowledge graph after the code and ledger changes.
- 2026-08-19: Committed the verified implementation as `f1a7e581f9ae618d98818210756276c4a423a155`.
- 2026-08-19: Pushed `fix/runtime-memory-safety` and opened pull request [#307](https://github.com/Ancientkingg/EGTRAIN/pull/307); required CI is pending.
- 2026-08-19: Pull request #307 passed required CI and was squash-merged as `743fe85798aef38a6862b989434650dc87106b2b`. Deleted the remote and local feature branches and removed the clean milestone worktree.

## Milestone 2: deterministic simulation state

- Finding IDs: G-02, P-04
- Branch: `fix/serialize-train-runtime`
- Worktree: `/Users/samuelbruin/Downloads/EGTRAIN/local/worktrees/serialize-train-runtime`
- Base SHA: `743fe85798aef38a6862b989434650dc87106b2b`
- Scope: serialize the per-train runtime step, add repeated-run determinism coverage, and assess a local virtual-coupling message-record simplification
- Files changed: `CMakeLists.txt`; `EGTRAIN/QEGTRAIN/app/DispatchController.cpp`; `tools/e2e/simulation_determinism_smoke.py`; this ledger
- Test results: focused normal CTest 3/3 passed; full normal CTest 47/47 passed; focused ASan/UBSan CTest 3/3 passed; six-case native headless passed; six-case roundtrip passed
- Adversarial-review findings: final independent correctness review found no undefined behavior, ordering regression, test weakness, or other actionable issue; no P0/P1/P2 findings
- Fixes made from review: none required
- Ponytail findings: accepted removal of redundant CMake environment setup, an unnecessary nested working directory, and vestigial commented OpenMP scaffolding; declined P-04 because the only existing matching record belongs to the GUI layer and a new shared type would add an abstraction after serialization removed the practical need
- Simplifications made: removed the live OpenMP directive, its redundant critical section, stale multithreading comments, duplicate offscreen environment setup, and one unnecessary test-directory level
- Commit SHA: `61b0fc3313fbd72cddba0a9d71c2610751840672`
- PR number and URL: [#309](https://github.com/Ancientkingg/EGTRAIN/pull/309)
- CI status: passed in 10m59s
- Merge SHA: `d029582fcb230c8419e9332bfdb608c792974384`
- Remaining blockers: P0 blockers are cleared; remaining P1 findings G-03, G-04, G-07, and G-08 still block stabilization

### Execution log

- 2026-08-19: Confirmed no existing issue owns G-02; opened issue [#308](https://github.com/Ancientkingg/EGTRAIN/issues/308).
- 2026-08-19: Fetched `origin`, created the clean isolated milestone worktree from the latest merged `origin/main`, and recorded the exact base SHA.
- 2026-08-19: Traced the live GUI and headless call paths to the single active per-train OpenMP loop in `DispatchController`. Iterations reach shared routes, signalling sections, order lists, infrastructure event lists, and virtual-coupling vectors; serial ascending train order is the bounded root-cause fix.
- 2026-08-19: Audited P-04. The only existing matching record is the GUI snapshot type, and using it from `RollingStock.h` would create a simulation-to-application dependency. Deferred the optional consolidation rather than add a new shared type or header to this correctness fix.
- 2026-08-19: Fixed the determinism regression seam as two shortened canonical Milano-Brescia runs with identical seed and inputs, comparing trajectories, timetable events, station statistics, and energy results. Milano directly exercises the shared-route fixture while the bounded horizon keeps normal and sanitizer CTest practical.
- 2026-08-19: Removed the live per-train parallel directive and its now-redundant arrival/departure critical wrapper. Registered the process-level determinism test; its two Milano-Brescia runs produced byte-identical results in about four seconds.
- 2026-08-19: Focused normal CTest passed 3/3 and full normal CTest passed 47/47, including the new process-level determinism regression.
- 2026-08-19: Independent correctness review found no actionable correctness, regression, or test-coverage issue. The reviewer also confirmed that each test run produced non-empty trajectory and energy observations.
- 2026-08-19: Separate Ponytail review identified three safe local reductions. Removed duplicate test environment setup, an unnecessary nested working directory, and vestigial commented OpenMP scaffolding; no correctness check was weakened.
- 2026-08-19: Focused ASan/UBSan CTest passed 3/3 and all six committed scene roundtrips passed.
- 2026-08-19: Six-case native headless smoke passed, including the canonical Copenhagen and Milano-Brescia runtime baselines.
- 2026-08-19: Refreshed the local code knowledge graph after the code and ledger changes.
- 2026-08-19: Committed the verified implementation as `61b0fc3313fbd72cddba0a9d71c2610751840672`.
- 2026-08-19: Pushed `fix/serialize-train-runtime` and opened pull request [#309](https://github.com/Ancientkingg/EGTRAIN/pull/309); required CI is pending.
- 2026-08-19: Pull request #309 passed required CI in 10m59s and was squash-merged as `d029582fcb230c8419e9332bfdb608c792974384`. Deleted the remote feature branch and removed the clean milestone worktree. Both P0 findings are now resolved.

## Milestone 3: external communication lifecycle

- Finding IDs: G-03, G-07, P-05
- Branch: `fix/external-sharing-lifecycle`
- Worktree: `/Users/samuelbruin/Downloads/EGTRAIN/local/worktrees/external-sharing-lifecycle`
- Base SHA: `d029582fcb230c8419e9332bfdb608c792974384`
- Scope: bounded ZeroMQ sending and shutdown, a shared sender path for both channels, and explicit preservation of supported active legless route-choice demand
- Files changed: `CMakeLists.txt`; `EGTRAIN/QEGTRAIN/app/DispatchController.cpp`; `EGTRAIN/QEGTRAIN/io/RailMLParser.cpp`; `EGTRAIN/QEGTRAIN/io/RailMLParser.h`; `EGTRAIN/QEGTRAIN/simulation/Passengers.cpp`; `EGTRAIN/QEGTRAIN/tests/test_operationsbuilder.cpp`; `EGTRAIN/QEGTRAIN/tests/test_railmlparser.cpp`; this ledger
- Test results: focused normal CTest 4/4 passed, covering bounded no-listener return, a real loopback request/reply, fixed-seed mixed routed and legless active payloads, empty-demand setup rejection, and a one-step live Paimpol run with both sharing modes enabled; full normal CTest passed 48/48 before and after the CI correction; six-case ordinary-mode headless passed; the corrected loopback lifecycle test passed 100 consecutive runs after reproducing the CI-only failure locally
- Adversarial-review findings: independent correctness review found no actionable finding; it noted only that absent and present peers are covered separately rather than an absent-to-late-peer transition; final post-simplification re-review also found no issue; focused review of the CI correction found no deadlock, data race, weakened production timeout, or other finding
- Fixes made from review: none required; the retained per-endpoint socket already preserves connection progress for a peer that appears later
- Ponytail findings: accepted consolidation of identical exception handlers, replacement of an unnecessary post-join atomic flag with `bool`, and removal of unnecessary CTest `RUN_SERIAL`
- Simplifications made: replaced the two detached port-specific senders with one shared sender, deleted the duplicated hard-coded XML example bodies, computed each XML document once, and applied the three accepted Ponytail reductions for a further five-line reduction
- Commit SHA: implementation `1d94c3c62126535d4826d0f2674aa57fbdd9fe44`; CI correction `b60af2da27f8587291d1667f498f775ea0db96e4`
- PR number and URL: [#311](https://github.com/Ancientkingg/EGTRAIN/pull/311)
- CI status: replacement run 32353585680 passed in 10m53s after the initial flaky test-harness failure was corrected
- Merge SHA: `d2c8dd761808c45deaa3f44a2348e10c5ba3c37a`
- Remaining blockers: G-04 and G-08

### Execution log

- 2026-08-19: Confirmed no existing issue owns G-03 and G-07; opened issue [#310](https://github.com/Ancientkingg/EGTRAIN/issues/310).
- 2026-08-19: Fetched `origin`, created the clean isolated milestone worktree from the latest merged `origin/main`, and recorded the exact base SHA.
- 2026-08-20: Traced all sender and payload call sites. The only live sends are two detached calls in the per-timestep dispatch loop; both duplicate stack REQ-socket logic on one process context, have no timeout, and wait indefinitely for replies. The route-choice payload already carries only origin, destination, and departure time, so the external contract does not require route legs.
- 2026-08-20: Fixed the implementation direction as one shared synchronous sender with nonblocking enqueue, finite reply wait, zero linger, and caught transport errors. This gives natural backpressure without threads or a queue. Active legless journeys will use their existing canonical journey fields, and empty passenger demand will remain a setup error.
- 2026-08-20: Implemented one retained REQ socket per endpoint behind the shared synchronous sender. Removed both detached-thread calls and duplicated port-specific senders; each channel now has zero linger, nonblocking enqueue, a finite reply timeout, exception containment, and automatic socket reset after a missing reply or transport error.
- 2026-08-20: Included active legless journeys from their canonical origin, destination, and planned departure fields, and aligned route-choice setup validation so legless-only prepared demand remains supported.
- 2026-08-20: Focused normal CTest passed 4/4. The tests cover a prompt absent-peer return, a successful loopback request/reply with the expected JSON/XML envelope, deterministic mixed routed/legless payload contents, retained empty-demand rejection, and a live Paimpol `-TSM 1 -RC 1` no-listener process that exited in under one second.
- 2026-08-20: Independent correctness review found no actionable issue. It recorded absent-to-late-peer recovery as an untested transition, while confirming that the retained socket structure supports it.
- 2026-08-20: Full normal CTest passed 48/48 in 168.67 seconds after completing the full build.
- 2026-08-20: Separate Ponytail review produced three safe reductions. Collapsed identical exception handlers, used the thread `join()` happens-before relation instead of an atomic test flag, and removed unnecessary CTest serialization; focused CTest passed 4/4 again.
- 2026-08-20: Six-case native headless smoke passed in ordinary `-TSM 0 -RC 0` mode, preserving the existing runtime structure and observables.
- 2026-08-20: Final independent correctness re-review confirmed that thread join synchronizes the test flag, the generic exception handler covers ZeroMQ errors, test serialization is unnecessary, and the legless payload matches the current external contract; no findings remain.
- 2026-08-20: Refreshed the local code knowledge graph after the code and ledger changes.
- 2026-08-20: Committed the verified implementation as `1d94c3c62126535d4826d0f2674aa57fbdd9fe44`.
- 2026-08-20: Pushed `fix/external-sharing-lifecycle` and opened pull request [#311](https://github.com/Ancientkingg/EGTRAIN/pull/311); required CI is pending.
- 2026-08-20: Initial CI run 32351657448 passed 47/48 tests but failed `test_railmlparser`. Added failure-state diagnostics and reproduced the same state locally: the mock REP server received the envelope while the client missed its queued reply.
- 2026-08-20: Removed the mock server's zero-linger shutdown and added an explicit client-completion handshake so the server remains alive until the request/reply call returns. Retained the 100 ms production transport bound. The corrected test passed 100 consecutive runs, and the focused milestone set passed 4/4.
- 2026-08-20: Focused independent review of the CI correction found no issue and confirmed all failure paths remain bounded. Full normal CTest then passed 48/48 in 149.02 seconds. The existing Ponytail reductions remain intact; the completion handshake is the minimum synchronization needed for a reliable request/reply test.
- 2026-08-20: Committed the verified CI correction as `b60af2da27f8587291d1667f498f775ea0db96e4`; replacement CI is pending.
- 2026-08-20: Replacement CI run 32353585680 passed in 10m53s. Squash-merged pull request #311 as `d2c8dd761808c45deaa3f44a2348e10c5ba3c37a`, deleted the feature branch, and removed the clean milestone worktree.

## Milestone 4: result integrity

- Finding IDs: G-04, P-02
- Branch: `fix/finite-delay-statistics`
- Worktree: `/Users/samuelbruin/Downloads/EGTRAIN/local/worktrees/finite-delay-statistics`
- Base SHA: `d2c8dd761808c45deaa3f44a2348e10c5ba3c37a`
- Scope: finite station-delay statistics for zero, one, and multiple samples, with shared aggregation where scientifically equivalent
- Files changed: `EGTRAIN/QEGTRAIN/simulation/Infrastructure.cpp`; `EGTRAIN/QEGTRAIN/simulation/Simulation.cpp`; `EGTRAIN/QEGTRAIN/tests/test_operationsbuilder.cpp`; `tools/e2e/headless_smoke.py`; `tools/memory/test_raii_contract.py`; this ledger
- Test results: focused operations and RAII contract tests passed 2/2; Python export-scan syntax check passed; full build and post-simplification application rebuild passed; focused Paimpol and Milano native headless runs passed their existing runtime baselines and both finite-statistics export scans; full CTest passed 48/48; six-case native headless smoke passed with finite station-statistics exports; six-case roundtrip passed
- Adversarial-review findings: one medium sentinel collision in signed statistics: a valid one-second early arrival had `StationDelay == -1` and was mistaken for the positive-mode unavailable marker
- Fixes made from review: signed-mode collection now derives availability from the actual-arrival array while positive-only mode retains its delay sentinel; an exact `-1` signed-delay regression passes
- Ponytail findings: accepted inlining the one-caller station sample collector and reusing one input vector sequentially instead of allocating three
- Simplifications made: collapsed roughly 270 duplicated calculation lines into one private path, then removed the one-use sample record/helper and two input-vector allocations for a further ten-line reduction
- Commit SHA: `cabb787588c2bf28308931c62cb4749f7716be4e`
- PR number and URL: [#313](https://github.com/Ancientkingg/EGTRAIN/pull/313)
- CI status: required checks running
- Merge SHA: pending
- Remaining blockers: G-04 remains unresolved until the verified change is merged

### Execution log

- 2026-08-20: Confirmed no existing issue owns G-04; opened issue [#312](https://github.com/Ancientkingg/EGTRAIN/issues/312).
- 2026-08-20: Fetched `origin`, created the clean isolated milestone worktree from the latest merged `origin/main`, and recorded the exact base SHA.
- 2026-08-20: Traced the complete result path. Both station modes duplicate collection and calculation, while `Compute_Input_Delays()` repeats the same unsafe positive-delay mean and sample-deviation logic for three input populations; all six values flow directly into `Print_Station_Delay_Stats()` and its totals.
- 2026-08-20: Fixed the semantics before implementation: no recorded arrivals retain the existing unavailable marker; a non-empty all-punctual positive-only population reports zero mean and deviation; singleton statistical populations report zero deviation; larger populations retain the sample denominator. Positive-only means remain based on positive delays, while positive/negative means include every recorded arrival.
- 2026-08-20: Replaced the duplicated station bodies and three input calculations with one private collection/calculation path. Added direct zero, singleton, multi-sample, positive, negative, and input-population assertions plus a canonical output scan that rejects non-finite numeric tokens in both station-statistics files.
- 2026-08-20: Configured a clean Qt 5 test build. The focused `test_operationsbuilder` regression passed 1/1 in 2.86 seconds, and the Python export-scan syntax check passed.
- 2026-08-20: Completed the full application build. Native Paimpol and Milano-Brescia headless checks passed their structure, movement, served-arrival, and pre-cutover runtime baselines; both generated station-statistics files contained only finite numeric values.
- 2026-08-20: Root review found the exported `TOTALS` row could still divide by `numStations - 1` when no reportable station followed the excluded first station. Totals now aggregate only rows with recorded samples and use the established unavailable marker when none exist. A one-station export regression passed with no non-finite numeric token; focused CTest passed again 1/1.
- 2026-08-20: Independent correctness review found that signed delay `-1` is a valid one-second early arrival but collided with the positive-mode unavailable marker. Signed collection now checks the actual-arrival sentinel instead; positive-only collection retains the legacy delay sentinel. Added an exact `-1` regression, and focused CTest passed 1/1 in 2.71 seconds.
- 2026-08-20: Fresh correctness re-review found no remaining issue. Separate Ponytail review identified two local reductions: inlined the one-caller station collector and reused one vector across the three input populations, removing ten lines and two allocations. Focused CTest passed 1/1 after simplification in 2.63 seconds.
- 2026-08-20: Rebuilt the complete `QEGTRAIN` target after the accepted simplifications; compilation and linking passed.
- 2026-08-20: Re-ran the canonical Paimpol and Milano-Brescia native headless cases against the final executable. Both passed clean exit, structure, movement, served-station, runtime-observable, and finite-statistics checks.
- 2026-08-20: Final independent correctness review of the simplified diff found no issue. The reviewer traced current callers and delay producers; residual risk is limited to legacy paths that bypass native validation and supply non-finite inputs.
- 2026-08-20: The first full CTest run passed 47/48. The only failure was the source-level RAII contract, whose literal checks still expected the four duplicated `TrainDelay`/`TrainConsDelay` append sites removed by consolidation. Updated that contract to verify the shared `arrivals` and `consecutive` vectors grow together and that the one reusable input vector remains RAII-managed. The contract and focused operations regression then passed 2/2.
- 2026-08-20: Re-ran full CTest after correcting the stale source contract; all 48 tests passed in 147.67 seconds.
- 2026-08-20: Six-case native headless smoke passed. Every case exited cleanly and produced finite station-statistics exports; all applicable structure, movement, runtime-observable, and served-station baselines also passed.
- 2026-08-20: Six-case folder export, compatibility reimport, and validation roundtrip passed, including the reimported Assignment scene runtime check. Existing scene diagnostics remained warnings or informational messages.
- 2026-08-20: Refreshed the repository knowledge graph after the code and guidance changes. Graphify reported its existing parser warnings for four files and zero-node warnings for data files; the graph rebuild completed with 4,923 nodes and 11,600 edges.
- 2026-08-20: Final mechanical audit confirmed both public station-statistics entry points and input aggregation retain their production callers, the duplicated formulas are gone, every sample-variance division is guarded by a population larger than one, both station export variants are scanned, and the zero/singleton/multiple plus exact signed `-1` cases remain covered. Final diff checks passed.
- 2026-08-20: Committed the verified milestone implementation as `cabb787588c2bf28308931c62cb4749f7716be4e`.
- 2026-08-20: Pushed `fix/finite-delay-statistics` and opened PR [#313](https://github.com/Ancientkingg/EGTRAIN/pull/313); required CI is running.
