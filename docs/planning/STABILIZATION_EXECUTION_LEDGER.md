# Stabilization execution ledger

- Initial `origin/main`: `935d94227fa3ebc61de371a52d663e711a3e4805`
- Release status: blocked by G-01 and G-02
- Active milestone: 1, runtime memory safety

| Milestone | Findings | Status | Issue | Pull request | Merge |
|---|---|---|---|---|---|
| 1. Runtime memory safety | G-01, G-05, G-06; P-03 | In progress | [#306](https://github.com/Ancientkingg/EGTRAIN/issues/306) | Pending | Pending |
| 2. Deterministic simulation state | G-02; P-04 | Blocked on milestone 1 | Pending | Pending | Pending |
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
- Commit SHA: pending
- PR number and URL: pending
- CI status: pending
- Merge SHA: pending
- Remaining blockers: milestone 1 remains unmerged; G-02 continues to block release after this milestone

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
