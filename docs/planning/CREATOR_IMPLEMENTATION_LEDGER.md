# Creator implementation ledger

## Master goal

Make EGTRAIN creator-complete for an independent seventh network. A user with authoritative railway data must be able to create, validate, save, bundle, reopen, run, inspect, and export the case through public application operations without C++, essential JSON edits, Cumpari runtime staging, hidden bundled-case data, generated runtime identifiers, or private model mutation.

## Current state

- Planning baseline: `59128cea7a50b2fbc83e147f2e6d9dd6f451c2cf`
- Current `origin/main`: `59128cea7a50b2fbc83e147f2e6d9dd6f451c2cf`
- Current milestone: PR 1, section resolution and signal binding
- Current worktree: `/Users/samuelbruin/Downloads/EGTRAIN/local/worktrees/creator-section-resolution`
- Current branch: `feature/creator-section-resolution`
- Current PR: not opened
- Completed milestones and PRs: none
- Open milestone dependencies: PR 1 has no implementation dependency. Issues #85 and #86 are parity gates.

## Confirmed creator gaps

1. Switch routes and linked signalling structures expose generated section strings.
2. Signals have no explicit protected-section binding.
3. Signal and compound-target validation is broader than native resolution.
4. Route adjacency is not validated and native construction can reorder sections.
5. Block placement follows hidden vector order without insert, reorder, or placement inspection.
6. Save, Save As, close, and Run can omit focused editor values.
7. Service rename can migrate references to an empty or duplicate ID.
8. Referenced deletes can leave dangling references and dependent selectors can remain stale.
9. Entrance delays and non-default scenario deletion are not publicly authorable.
10. Passenger journeys and legs lack public import, inspection, and CRUD.
11. Passenger platform geometry uses hidden fixed values.
12. General result exports lack a saved-input snapshot and complete run provenance.
13. The current creator smoke bypasses public operations and does not prove a seventh case.

## Disproven or outdated findings

- Canonical arbitrary-case simulation does not depend on a bundled case name. The legacy numeric shortcut remains limited to six cases, while arbitrary scenes use `--scene`.
- A separate Loaded Data architecture is not required. Missing transparency belongs in the existing topology, scenario, passenger, and result workflows.
- Composition rename propagation and train-unit source editing were complete before the planning baseline.

## Architecture and schema decisions

- Use the current `SceneModel` and native runtime. Do not create a second topology model or validator.
- PR 1 will add one small plain-C++ derived section inventory under `scene/`. It contains current runtime ID,
  source block/connection IDs, block placement, endpoints, node membership, and exact reference resolution.
  It is transient and derived from `SceneModel`; it is not persisted and is not a second topology model.
- Add one optional string protected-section reference to `SceneSignal`. The value is the exact current V1
  section identity. The signal editor presents catalog labels and never asks the creator to type that value.
- Do not add a nested signal selector or a second stable-ID scheme. The detailed implementation plan explicitly
  retains current section strings as the V1 persisted identity.
- Validate route tokens and each authored adjacent transition against the inventory. Same-track contiguous
  endpoints, declared connection endpoints, and the existing overlapping connection-section case are valid.
  A route must form one forward or one reverse chain.
- Native route creation preserves authored token order. Remove coordinate sorting; do not normalize a disconnected
  route into another route. Preserve the existing reverse-section transformation for a valid descending route.
- Validator and native builder both consume the inventory for routes, dependencies, restrictions, boundaries,
  signal bindings, signal incidents, section IDs, placement, and signalling-area/dependency capacity checks.
- The builder must compare catalog section IDs with the actual sections produced by legacy connection generation
  before operations or incidents are staged.
- Connected route segments must retain one authored direction. Consecutive connection-derived switch sections use
  the native shared-block/switch-chain contract and do not infer the route's global direction.
- The native runtime already supports discontinuities between regional coordinate systems. PR 1 preserves that
  generic contract with `scene.route.region_jump` warnings. It still rejects gaps on one track, undeclared
  same-coordinate track hops, and direction changes. No case name or bundled-case exception is used.

## Compatibility decisions

- Existing scene directories and `.egscene` bundles must remain readable.
- Do not infer a missing signal binding.
- Existing route strings must round-trip unchanged.
- Invalid or ambiguous legacy targets must receive actionable diagnostics before native allocation.

## GitHub issue state

- Reopened and reused: #50, #57, #58.
- Updated and reused: #126.
- Reused: #85, #86, #129, #164.
- Created during planning: #286, #287, #288, #289.
- Authoritative-data dependencies only: #181, #182, #228.
- No issue comments or open PRs newer than the planning review changed PR 1 scope.

## Files and areas changed

- Added a transient `scene/SectionInventory` shared by validation, native infrastructure staging, incident
  resolution, and the signal editor.
- Added optional `SceneSignal::protectedSection` parsing, folder writing, and bundle round-trip coverage.
- Updated native signalling and operations staging to resolve exact catalog sections and retain authored route order.
- Updated the existing infrastructure signal table with a creator-facing protected-section selector.
- Added focused validator, native builder, operations, folder, bundle, and public editor-smoke coverage.
- Updated the V1 schema/property documentation. No source data or committed case-study JSON was changed.

## Verification record

### Focused tests run

- Baseline before source edits:
  `ctest --test-dir build -R 'test_(scenevalidator|scenebuilder|operationsbuilder|scenewriter|scenebundle)' --output-on-failure`
  passed 5 of 5 (`test_scenevalidator`, `test_scenewriter`, `test_scenebundle`,
  `test_scenebuilder`, and `test_operationsbuilder`) in 3.29 seconds.
- Baseline `scene_tool` built successfully. `build/scene_tool validate` returned exit 0 for all six
  committed scene directories. Existing timetable/passenger warnings were emitted; no scene had an error.
- After implementation:
  `ctest --test-dir build -R 'test_(scenevalidator|scenebuilder|operationsbuilder|scenewriter|scenebundle)' --output-on-failure`
  passed 5 of 5 in 3.26 seconds. The clean pre-review rebuild passed the same 5 of 5 in 2.68 seconds.
- `tools/e2e/editor_smoke.sh` passed after all 7 scene tests passed. The smoke selected a protected base block
  through the visible combo, renamed the block, saved a folder and bundle, and reopened the binding.
- `build/scene_tool validate` returned exit 0 with zero errors for all six committed scenes. Copenhagen reports
  6 existing cross-region transitions and Netherlands reports 26 as `scene.route.region_jump` warnings.
- `git diff --check` passed.

### Full tests and smokes run

- Planning baseline: configure and build passed; CTest 43 of 43 passed; editor smoke, six-case headless smoke, six-case round-trip smoke, and Assignment smoke passed.
- Current milestone before independent review:
  - full configure and build passed;
  - the first complete CTest pass was 43 of 43 in 124.39 seconds;
  - the clean pre-review CTest pass was 43 of 43 in 117.81 seconds;
  - editor smoke passed;
  - six-case native headless smoke passed;
  - six-case round-trip smoke passed;
  - Assignment smoke passed;
  - incident smoke passed with the named bound signal held at 24000 m during the incident window and passed after release;
  - visual-polish smoke passed at device-pixel ratios 1 and 2;
  - all six committed scenes saved and reopened as `.egscene` bundles successfully;
  - all six committed scene directories passed `scene_tool validate` after the final rebuild.

### Exact results

- `origin/main` was fetched on 2026-08-11 and still resolves to the planning baseline.
- The primary checkout remains on `fix/canvas-startup-package` with unrelated modified and untracked files. It has not been changed by this execution.
- The local workspace connector cannot open EGTRAIN because its configured allowed root points to another project. Repository work uses the isolated filesystem worktree.
- The PR 1 focused targets configured and built successfully with Qt 5 before source edits.
- The six committed scenes are the compatibility baseline for strict route-adjacency work. Existing warnings
  are not PR 1 regressions and must not be converted into source-data edits.
- The first strict adjacency draft rejected Copenhagen, Milano-Brescia, and Netherlands. Source tracing proved
  two distinct compatibility contracts: consecutive switch-derived sections can share/cut a base section, and
  the native runtime intentionally joins different regional coordinate systems. The checker now models both;
  Milano-Brescia validates without route warnings, while the two region-jump cases remain visible warnings.
- A three-region focused native test uses authored order A, B, C while coordinate order is A, C, B. The native
  route retains A, B, C, proving coordinate sorting is gone.
- Baseline/current output comparison used clean builds from the same commit and identical random seed files.
  Netherlands, Paimpol, and Milano-Brescia matched semantically apart from append/timing files. Copenhagen
  differences are confined to the two routes whose authored descending section order was previously changed by
  coordinate sorting, plus aggregate files. Both versions served all 92 expected station rows. Assignment also
  differed when the unchanged baseline binary was repeated from the same seed, proving those byte differences are
  pre-existing nondeterminism. Lebanon route IDs, native section state, train state, and stops matched at 17-digit
  precision; its remaining trajectory-byte difference occurs below identical staged state. This PR therefore
  records structural, runtime, six-case, and authored-order parity rather than claiming byte-identical output.
- Temporary diagnostic instrumentation and random-seed source changes were removed. The source diff contains no
  output-parity tuning and no case-name exception.
- `graphify update .` completed after the final source changes with 4864 nodes, 10893 edges, and 232 communities.
- `git diff --check` passed after the final rebuild and graph refresh.

## Review record

### Independent correctness findings

- Not run for PR 1 yet.

### Corrections made

- Corrected connection-derived adjacency to use actual section boundaries instead of internal switch endpoints.
- Kept switch-chain pairs direction-neutral so legacy double-switch routes do not conflict with route direction.
- Preserved the native final-block clipping warning and derived-section arc-capacity preflight after extraction.
- Restored lexical track ordering in the shared inventory to match the preexisting native contract.

### Ponytail findings

- Not run for PR 1 yet.

### Simplifications made

- The planned design rejects a second topology model and retains existing section identities unless the source trace proves that impossible.

## Blockers

### Known authoritative-data blockers

- #181, #182, and #228 remain outside application implementation. No railway values will be invented.

### Unresolved application blockers

- PR 1 has no known test or parity blocker. A final clean rebuild, independent correctness review, and Ponytail
  review remain before merge. PRs 2 through 6 remain open.

## Next action

Rebuild the final source after diagnostic cleanup, rerun the focused and complete gates, update the ledger with
the exact final results, then commit, push, and open PR 1. Run fresh correctness and Ponytail reviews before merge.
