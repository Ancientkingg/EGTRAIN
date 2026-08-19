# Stabilization execution ledger

- Initial `origin/main`: `935d94227fa3ebc61de371a52d663e711a3e4805`
- Release status: blocked by G-02
- Active milestone: 2, deterministic simulation state

| Milestone | Findings | Status | Issue | Pull request | Merge |
|---|---|---|---|---|---|
| 1. Runtime memory safety | G-01, G-05, G-06; P-03 | Complete | [#306](https://github.com/Ancientkingg/EGTRAIN/issues/306) | [#307](https://github.com/Ancientkingg/EGTRAIN/pull/307) | `743fe85798aef38a6862b989434650dc87106b2b` |
| 2. Deterministic simulation state | G-02; P-04 | In progress | [#308](https://github.com/Ancientkingg/EGTRAIN/issues/308) | Pending | Pending |
| 3. External communication lifecycle | G-03, G-07; P-05 | Blocked on milestone 2 | Pending | Pending | Pending |
| 4. Result integrity | G-04; P-02 | Blocked on milestone 3 | Pending | Pending | Pending |
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
- PR number and URL: pending
- CI status: pending
- Merge SHA: pending
- Remaining blockers: G-02 remains unresolved until this verified change is merged

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
