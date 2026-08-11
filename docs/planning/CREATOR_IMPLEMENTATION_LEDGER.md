# Creator implementation ledger

## Master goal

Make EGTRAIN creator-complete for an independent seventh network. A user with authoritative railway data must be able to create, validate, save, bundle, reopen, run, inspect, and export the case through public application operations without C++, essential JSON edits, Cumpari runtime staging, hidden bundled-case data, generated runtime identifiers, or private model mutation.

## Current state

- Planning baseline: `59128cea7a50b2fbc83e147f2e6d9dd6f451c2cf`
- Current `origin/main`: `d90eb24de0f6f72e33b5206d1ffecec7ab64f7a2`
- Current milestone: PR 2, structured topology authoring and explicit block order
- Current worktree: `/Users/samuelbruin/Downloads/EGTRAIN/local/worktrees/structured-topology-authoring`
- Current branch: `feature/structured-topology-authoring`
- Current PR: #291, `Add structured topology authoring`
- Completed milestones and PRs: PR #290, `Bind signals to canonical track sections`, merged as
  `d90eb24de0f6f72e33b5206d1ffecec7ab64f7a2`
- Open milestone dependencies: PR 2 depends on merged PR #290. Issues #85 and #86 remain parity gates.

## Confirmed creator gaps

1. Switch routes and linked signalling structures expose generated section strings.
2. Block placement follows hidden vector order without insert, reorder, or placement inspection.
3. Save, Save As, close, and Run can omit focused editor values.
4. Service rename can migrate references to an empty or duplicate ID.
5. Referenced deletes can leave dangling references and dependent selectors can remain stale.
6. Entrance delays and non-default scenario deletion are not publicly authorable.
7. Passenger journeys and legs lack public import, inspection, and CRUD.
8. Passenger platform geometry uses hidden fixed values.
9. General result exports lack a saved-input snapshot and complete run provenance.
10. The current creator smoke bypasses public operations and does not prove a seventh case.

## Disproven or outdated findings

- Canonical arbitrary-case simulation does not depend on a bundled case name. The legacy numeric shortcut remains limited to six cases, while arbitrary scenes use `--scene`.
- A separate Loaded Data architecture is not required. Missing transparency belongs in the existing topology, scenario, passenger, and result workflows.
- Composition rename propagation and train-unit source editing were complete before the planning baseline.

## Architecture and schema decisions

- Use the current `SceneModel` and native runtime. Do not create a second topology model or validator.
- PR 1 added one small plain-C++ derived section inventory under `scene/`. It contains current runtime ID,
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
- `/` remains the reserved delimiter for connection-derived section identities. Canonical block IDs containing
  `/` still load and round-trip, but validation and direct native preflight reject them actionably before runtime
  mutation. Supporting them natively would require replacing the established section grammar and runtime parsers.
- PR 2 keeps `SceneModel::blocks` vector order as the persisted placement contract. The editor filters blocks by
  track, maps visible rows back to vector indices, and derives displayed placement from `SceneSectionInventory`.
- PR 2 uses the existing section inventory for route, dependency, restriction, and boundary choices. Route order
  is edited through one small ordered list; no schema field, second topology model, or generic editor framework is
  added. Invalid legacy references remain visible and unchanged until the creator replaces or removes them.

## Compatibility decisions

- Existing scene directories and `.egscene` bundles must remain readable.
- Do not infer a missing signal binding.
- Existing route strings must round-trip unchanged.
- Invalid or ambiguous legacy targets must receive actionable diagnostics before native allocation.
- The legacy exporter retains its exact block-map handling for slash-bearing stored references as a recovery and
  conversion path. That exporter behavior does not make `/` a valid canonical runtime block-ID character.

## GitHub issue state

- Closed by PR #290: #50 and #58.
- Reopened and reused for current work: #57.
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
- PR 2 changes only `MainWindow.{h,cpp}` and this ledger. It keeps block vector order as the canonical
  per-track placement, adds a per-track block view with Insert and Move Up/Down, and displays derived order,
  start, end, and coverage values from `SceneSectionInventory`.
- PR 2 replaces normal raw route, dependency, restriction, and boundary section entry with catalog-backed
  controls. Legacy and invalid references stay visible until the creator replaces or removes them. No model,
  schema, validator, native builder, or case-study data changed.
- The existing editor smoke now creates two track chains, six blocks, a connection, and a three-section switched
  route entirely through public widgets. It exercises block insertion and reordering, route add/remove/reorder,
  typed linked references, folder and bundle round trips, a reference-preserving block rename, and direct native
  construction retaining the exact authored route order. The smoke never types a generated section identity.

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
- After correctness-review fixes, the same focused gate passed 5 of 5 in 3.39 seconds.
- After the second corrected-diff review, `test_scenevalidator`, `test_scenebuilder`, and
  `test_sceneexporter` passed 3 of 3 in 1.36 seconds. The regressions cover a coordinate-reversed
  switch fork, a connection-derived U-turn rejected before native mutation, and legacy export of a
  signal failure through its protected-section binding.
- After the third corrected-diff review, `test_scenevalidator`, `test_scenebuilder`,
  `test_operationsbuilder`, `test_scenewriter`, `test_scenebundle`, and `test_sceneexporter`
  passed 6 of 6 in 2.90 seconds. The new regressions prove that declared topology, rather than
  regional coordinate values, controls native route direction and that legacy export unwraps an
  exact base-section identity while reporting an unrepresentable connection-derived target.
- After the fourth corrected-diff review, the same six-test gate passed 6 of 6 in 2.51 seconds.
  The new regressions prove that legacy regional compatibility cannot accept a wrong-branch fork
  through shared source blocks and that negative-coordinate connection targets are diagnosed and
  skipped by the legacy exporter.
- After the fifth corrected-diff review, the same six-test gate passed 6 of 6 in 3.45 seconds.
  The regressions now keep legacy switch-chain direction evidence except for the exact regional
  bridge shape required by committed compatibility data, and skip malformed as well as exact
  compound legacy incident targets.
- After the sixth corrected-diff review, the same six-test gate passed 6 of 6 in 3.46 seconds.
  The regression distinguishes a joined derived-section boundary from an unrelated regional
  outer boundary, rejects the resulting legacy derived U-turn, and preserves committed regional routes.
- After the seventh corrected-diff review, the same six-test gate passed 6 of 6 in 3.46 seconds.
  Focused validator and direct-builder regressions reject mixed ordinary and derived U-turn evidence
  before native mutation, while a separate regression preserves direction across a warned legacy
  regional segment boundary.
- After the eighth corrected-diff review, the same six-test gate passed 6 of 6 in 2.53 seconds.
  The exporter regression proves that an exact stored slash-bearing block reference can map to its
  representable legacy block ID instead of being dropped as a compound section.
- After the ninth corrected-diff review, `test_scenevalidator`, `test_scenebuilder`,
  `test_operationsbuilder`, `test_scenewriter`, `test_scenebundle`, and `test_sceneexporter`
  passed 6 of 6 in 2.54 seconds. The new regressions prove that `/` receives one
  actionable canonical validation error, direct native preflight rejects it before runtime mutation,
  and legacy export can still map an exact stored slash-bearing reference for recovery.
- After the tenth corrected-diff review, the same six-test gate passed 6 of 6 in 2.56 seconds.
  The exporter regression proves that a signal-failure target matching both a signal and a section
  is diagnosed and skipped instead of being silently retargeted through the signal binding.
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
  - the post-correctness CTest pass was 43 of 43 in 118.76 seconds;
  - editor smoke passed;
  - six-case native headless smoke passed;
  - six-case round-trip smoke passed;
  - Assignment smoke passed;
  - incident smoke passed with the named bound signal held at 24000 m during the incident window and passed after release;
  - visual-polish smoke passed at device-pixel ratios 1 and 2;
  - all six committed scenes saved and reopened as `.egscene` bundles successfully;
  - all six committed scene directories passed `scene_tool validate` after the final rebuild.
  - after the correctness fixes, six-case native headless smoke, six-case round-trip smoke, Assignment smoke,
    editor smoke, and incident smoke all passed again.
  - after the second corrected-diff fixes, the full build passed and CTest passed 43 of 43 in 120.46 seconds;
  - the editor smoke passed after its 7 scene tests;
  - the incident smoke passed both breakdown and bound-signal hold/release checks;
  - the six-case headless smoke passed every case, trajectory, and served-station assertion;
  - Assignment smoke passed its canonical timetable check;
  - six-case round-trip smoke ended with `ROUNDTRIP PASS`;
  - all six committed scenes validated with no errors and only their previously recorded warnings.
  - after the third corrected-diff fixes, the full build passed and CTest passed 43 of 43 in 119.95 seconds;
  - the editor smoke passed after its 7 scene tests;
  - the incident smoke passed both breakdown and bound-signal hold/release checks;
  - all six committed scenes validated with no errors and only their previously recorded warnings;
  - the six-case headless smoke passed every case, changing-trajectory, non-sentinel, and served-station assertion;
  - Assignment smoke passed its canonical timetable assertion;
  - six-case round-trip smoke ended with `ROUNDTRIP PASS`.
  - after the eighth corrected-diff fix, the full build passed and CTest passed 43 of 43 in 118.19 seconds;
  - six-case round-trip smoke ended with `ROUNDTRIP PASS`.
  - after the seventh corrected-diff fixes, the full configure and build passed and CTest passed 43 of 43
    in 119.44 seconds;
  - editor smoke passed after its 7 scene tests;
  - incident smoke passed both breakdown and protected-signal hold/release checks;
  - all six committed scenes validated with no errors and their recorded warning groups unchanged;
  - the six-case native headless smoke passed every case, changing-trajectory, non-sentinel, and
    served-station assertion;
  - Assignment smoke passed its canonical timetable assertion;
  - six-case round-trip smoke ended with `ROUNDTRIP PASS`.
  - after the fifth corrected-diff fixes, the full build passed and CTest passed 43 of 43 in 118.81 seconds;
  - the editor smoke passed after its 7 scene tests;
  - the incident smoke passed both breakdown and bound-signal hold/release checks;
  - all six committed scenes validated with no errors and their recorded warning groups unchanged;
  - the six-case headless smoke passed every case, changing-trajectory, non-sentinel, and served-station assertion;
  - Assignment smoke passed its canonical timetable assertion;
  - six-case round-trip smoke ended with `ROUNDTRIP PASS`.
  - after the fourth corrected-diff fixes, the full build passed and CTest passed 43 of 43 in 118.42 seconds;
  - the editor smoke passed after its 7 scene tests;
  - the incident smoke passed both breakdown and bound-signal hold/release checks;
  - all six committed scenes validated with no errors and their recorded warning groups unchanged;
  - the six-case headless smoke passed every case, changing-trajectory, non-sentinel, and served-station assertion;
  - Assignment smoke passed its canonical timetable assertion;
  - six-case round-trip smoke ended with `ROUNDTRIP PASS`.
  - after the tenth corrected-diff fix, the full build passed and CTest passed 43 of 43 in 118.23 seconds.
  - the eleventh independent reviewer passed CTest 43 of 43 in 117.44 seconds and passed the live
    breakdown and protected-signal incident smoke.
  - the final post-review six-case round-trip smoke ended with `ROUNDTRIP PASS`.
  - after the sixth corrected-diff fixes, the full configure and build passed and CTest passed 43 of 43
    in 118.89 seconds;
  - editor smoke passed after its 7 scene tests;
  - incident smoke passed both breakdown and protected-signal hold/release checks;
  - all six committed scenes validated with no errors and their recorded warning groups unchanged;
  - the six-case native headless smoke passed every case, changing-trajectory, non-sentinel, and
    served-station assertion;
  - Assignment smoke passed its canonical timetable assertion;
  - six-case round-trip smoke ended with `ROUNDTRIP PASS`.

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
- `graphify update .` completed after the second corrected-diff fixes with 4872 nodes, 10910 edges,
  and 239 communities.
- `graphify update .` completed after the third corrected-diff fixes with 4872 nodes, 10911 edges,
  and 236 communities.
- `graphify update .` completed after the fourth corrected-diff fixes with 4872 nodes, 10911 edges,
  and 237 communities.
- `graphify update .` completed after the fifth corrected-diff fixes with 4871 nodes, 10910 edges,
  and 231 communities.
- `graphify update .` completed after the sixth corrected-diff fixes with 4872 nodes, 10911 edges,
  and 234 communities.
- `graphify update .` completed after the seventh corrected-diff fixes with 4871 nodes, 10910 edges,
  and 233 communities.
- `graphify update .` completed after the eighth corrected-diff fix with 4871 nodes, 10910 edges,
  and 233 communities.
- The final PR 1 `graphify update .` completed with 4871 nodes, 10912 edges, and 236 communities.
- PR #290 current-head CI passed both jobs. The build job completed in 21 minutes 13 seconds and included
  CTest, headless, editor, round-trip, bundle, Assignment, incident, render, track-preview, and visual smokes.
  The sanitizer job completed in 15 minutes 46 seconds.
- `git diff --check` passed after the final rebuild and graph refresh.
- PR 1 merge commit: `d90eb24de0f6f72e33b5206d1ffecec7ab64f7a2`.
- PR 2 baseline configure and full build passed after adding the Homebrew Qt 5 prefix. Its focused
  `scenevalidator`, `scenewriter`, `scenebundle`, `scenebuilder`, and `operationsbuilder` gate passed 5 of 5 in
  3.43 seconds. The baseline editor smoke then passed after all 7 scene tests passed.
- PR 2 implementation configured and built successfully. The full CTest suite passed 43 of 43 in 124.93 seconds.
- PR 2 six-case native headless smoke passed every case, changing-trajectory, non-sentinel, and served-station
  assertion. Six-case round-trip smoke ended with `ROUNDTRIP PASS`. Assignment smoke passed its canonical
  timetable check. Visual-polish smoke passed DPR 1 and DPR 2 visual, station-overlay, scene-render, and
  legacy-import checks.
- After removing two redundant route-panel refreshes, the full build passed, the focused
  `scenevalidator`, `scenewriter`, `scenebundle`, `scenebuilder`, and `operationsbuilder` gate passed 5 of 5 in
  2.21 seconds, and `tools/e2e/editor_smoke.sh` passed after all 7 scene tests.
- The Assignment smoke file is not executable in this checkout. Direct invocation failed with permission denied;
  rerunning it as `python3 tools/e2e/assignment_smoke.py` passed. No file mode was changed.
- After correcting the two PR 2 review findings, the full build passed and the five-test topology/persistence
  gate passed 5 of 5 in 2.19 seconds. The first editor-smoke run reached every feature marker except the focused
  New Case Save marker because the newly exercised modal confirmation left the offscreen window inactive. After
  explicitly reactivating the window in that test path, the editor smoke passed after all 7 scene tests.
- The fresh corrected-diff reviewer independently passed the build, CTest 43 of 43 in 120.15 seconds, editor smoke,
  and visual-polish smoke before reporting the two remaining P2 boundary/test gaps.
- After the second PR 2 correction, the build passed, the five-test topology/persistence gate passed 5 of 5 in
  2.19 seconds, and the editor smoke passed after all 7 scene tests.
- The final fresh correctness reviewer passed the build, CTest 43 of 43, editor smoke, and visual-polish smoke
  before reporting the stale route-pane visibility defect. After that correction, the build passed, the same
  five-test gate passed 5 of 5 in 2.20 seconds, and editor smoke passed after all 7 scene tests.
- The fresh PR 2 closeout review found no P0, P1, or P2 issue at `49c0d74`. It independently passed the full
  build, CTest 43 of 43 in 120.38 seconds, editor smoke, and visual-polish smoke.
- After applying the PR 2 Ponytail simplifications, the full build passed, the five-test topology/persistence
  gate passed 5 of 5 in 2.16 seconds, and editor smoke passed after all 7 scene tests.
- The fresh post-simplification correctness review found no P0, P1, or P2 issue at `5f1599a`. The root full
  CTest suite passed 43 of 43 in 120.70 seconds; the reviewer independently passed 43 of 43 in 120.24 seconds.
- `git diff --check` passed after the final PR 2 source change.

## Review record

### Independent correctness findings

- The first fresh review of PR #290 found no P0 issue and four merge blockers:
  1. overlapping connection-derived sections could be accepted through a shared block without matching the
     first section's exit to the second section's entry;
  2. an incident target matching both a signal ID and a block ID silently selected the signal;
  3. the inventory and legacy connection builder disagreed for positive endpoint deltas at or below `1e-8`;
  4. direct native-builder callers did not enforce route adjacency before mutating runtime state.
- The review also noted that inventory and legacy section-ID formatting used different fixed buffer sizes.
- The second fresh corrected-diff review found no P0 issue and three remaining findings:
  1. a coordinate-reversed switch fork could reuse one section's internal connection as the external
     boundary to another branch;
  2. all connection-derived transitions were direction-neutral, allowing a new canonical route to
     traverse A/B, B/C, then A/B without a direction error or native preflight failure;
  3. legacy incident export wrote a signal ID instead of its protected section, so the exported failure
     had no legacy runtime effect.
- The third fresh corrected-diff review found no P0 issue and two remaining P1 findings:
  1. `Route::createRouteFromBlockIds` still derived native direction from regional X coordinates, so a
     route that validation accepted as forward could be reversed when its track coordinate systems had
     equal or descending values;
  2. the public signal editor stores exact section identities such as `@block@`, but legacy incident export
     passed that wrapper to a loader that adds another wrapper. Connection-derived exact identities are not
     representable in the legacy four-column incident grammar.
- The fourth fresh corrected-diff review found no P0 issue and two remaining merge blockers:
  1. a `legacy_root` marker allowed a disconnected, regional-coordinate wrong-branch fork to be
     downgraded to `scene.route.region_jump`, after which the native builder constructed the route;
  2. the positioned-endpoint parser treated the format separator as the coordinate sign, so a
     negative coordinate such as `@block@--1.000000` evaded the unrepresentable-target diagnostic.
- The fifth fresh corrected-diff review found no P0 issue and two remaining P1 merge blockers:
  1. legacy provenance suppressed all connection-derived direction evidence, allowing a derived
     U-turn to pass semantic and direct native preflight and mutate runtime state;
  2. malformed slash-containing signal bindings fell through the positioned-target check, were
     exported to `Incidents.txt`, and then reimported into an invalid unresolved target.
- The sixth fresh corrected-diff review found no P0 issue and one remaining P1 merge blocker:
  `regionJump` combined forward and reverse outer-boundary checks, so a normal joined Copenhagen
  derived-section pair was treated as direction-neutral merely because its unused reverse boundary
  crossed coordinate regions. A legacy route containing that pair in both directions passed validator
  and direct native preflight and then mutated runtime state.
- The seventh fresh corrected-diff review found no additional issue but confirmed one remaining P1
  route-direction blocker: once any ordinary transition established a direction, the validator and
  direct native preflight discarded all opposing deferred derived evidence. A monotonic legacy route
  containing a forward base-to-derived transition followed by a reverse derived transition passed and
  mutated runtime state. Existing regressions covered only routes whose evidence was entirely deferred.
- The eighth fresh corrected-diff review found no route, binding, runtime, persistence, or case-name
  blocker after direct probes, but found one P1 legacy-export data-loss defect. The signal-failure
  exporter rejected every slash-containing protected-section target as compound even though canonical
  base block IDs may contain slashes and already have direct legacy mappings. A valid `Depot/1` binding
  was skipped and produced an empty `Incidents.txt`.
- The ninth fresh corrected-diff review found one P1 identity-contract blocker. A slash-bearing source
  block could appear as an exact section in the inventory and legacy exporter, while route component
  checks split it and direct native staging interpreted `/` as the connection-section delimiter. The
  runtime also uses the delimiter in switch occupancy, GUI state, rendering, and signal-output paths, so
  accepting the ID would require a new escaped identity grammar rather than a local parser exception.
- The tenth fresh corrected-diff review found one P1 export-parity blocker. When a signal ID also matched
  an exact section ID, validator and native staging rejected the target as ambiguous, but legacy export
  resolved the signal binding and silently wrote a failure for a different section. No other P0, P1, or
  P2 blocker was found in that review.
- The eleventh fresh corrected-diff review found no P0, P1, or P2 issue at `64ce206`. It independently
  traced public signal binding, folder and bundle persistence, shared resolution, validation, native
  infrastructure and operations staging, runtime incident behavior, and legacy export.
- The fresh PR 2 review found no P0 or P1 issue at `a608b6e`. It traced public block and section controls
  through model mutation, invalidation, validation, folder and bundle persistence, reload, and exact native route
  construction. It found two P2 boundary defects: an incomplete dependency, restriction, or boundary row could
  not be deleted while its first required selector was empty, and Add silently used the first valid track when
  the block filter selected an orphan empty-track bucket.
- The fresh corrected PR 2 review found no P0 or P1 issue at `e686f5a` but blocked on two remaining P2s. A loaded
  `SceneTrack` with an empty ID still counted as a valid Add target even though validation rejects it. The negative
  Add regression checked only the disabled button, not the handler guard, and the incomplete-row regression
  exercised only dependencies rather than all three anonymous row types.
- The final fresh PR 2 review found no P0 or P1 issue at `42b883a` but found one P2 UI-state blocker. The route
  section detail pane was created visible and refreshed only while the Routes facet was active, so it remained
  visible with stale content after switching to Blocks, Signals, or another facet.
- The fresh PR 2 closeout review found no P0, P1, or P2 issue at `49c0d74` after tracing the complete public
  editor, model, persistence, validation, and native route path.
- The fresh post-simplification review found no P0, P1, or P2 issue at `5f1599a`. It verified that the filtered
  row map preserves block move semantics, the removed text commit paths were unreachable, and the shared smoke
  helper retains every add, confirmed delete, empty-model, and re-add assertion.

### Corrections made

- Corrected connection-derived adjacency to use actual section boundaries instead of internal switch endpoints.
- Kept switch-chain pairs direction-neutral so legacy double-switch routes do not conflict with route direction.
- Preserved the native final-block clipping warning and derived-section arc-capacity preflight after extraction.
- Restored lexical track ordering in the shared inventory to match the preexisting native contract.
- Classified switch chains by the actual exit block and next entry block. Wrong-branch overlaps now fail, while
  valid derived-to-derived transitions remain neutral to regional coordinate direction.
- Rejected signal-failure targets that identify both a signal and a section in validation and native operations.
- Matched legacy connection generation's exact positive-delta rule and made inventory/native ID formatting use
  one dynamically sized formatter.
- Applied the shared transition classifier in the native infrastructure builder before runtime reset/allocation.
- Added focused negative-switch, ambiguous-target, epsilon-endpoint, and direct-builder regressions.
- Excluded each derived section's own source connection from external boundary joins, including when
  regional coordinates reverse the generated section orientation.
- Counted connection-derived transition direction for new canonical scenes while retaining the recorded
  `legacy_root` compatibility behavior needed by existing imported routes.
- Resolved signal IDs through `protected_section` before writing legacy `Incidents.txt`, using the existing
  block-reference mapper and leaving direct block targets compatible.
- Added coordinate-reversed fork, connection-derived U-turn, no-native-mutation, and legacy-export regressions.
- Passed the topology direction established by shared native preflight into route construction. The old
  coordinate heuristic remains only as the compatibility fallback for callers without canonical direction data.
- Unwrapped exact base-section identities before legacy incident export. Connection-derived targets are skipped
  with `scene.export.compatibility` because the preserved legacy loader cannot encode them without changing the
  legacy file grammar.
- Added regional-coordinate route-direction and exact-section legacy-export regressions.
- Limited regional-jump compatibility to transitions whose descriptors do not reuse a source block.
  Shared-source transitions must join at an actual boundary or fail as disconnected in both validation
  and direct native preflight.
- Parsed the positioned-section separator separately from the signed coordinate, preserving positive
  targets and correctly recognizing negative-coordinate compound targets.
- Added legacy wrong-branch no-mutation and negative-coordinate export regressions.
- Retained the legacy direction exception only for a connection-derived join across a recorded
  regional discontinuity. Ordinary derived switch chains now contribute direction in legacy and
  canonical scenes, so a U-turn is rejected before native mutation.
- Skipped every slash-containing signal-failure target in legacy export because the preserved
  four-column incident loader cannot represent any compound identity.
- Removed the now-unused `SceneSectionTransition::switchChain` flag instead of preserving dead
  compatibility machinery.
- Added legacy derived-U-turn and malformed compound export regressions.
- Separated a real disconnected regional jump from a joined derived transition whose unused opposite
  boundary spans coordinate regions. Legacy direction evidence is deferred only for the latter and is
  used when no unambiguous transition establishes the route direction. This rejects the isolated
  Copenhagen-shaped derived U-turn while leaving existing long regional routes governed by their
  unambiguous transitions.
- Added validator and direct-builder regressions for the regional derived U-turn and verified rejection
  occurs before native runtime mutation.
- Removed the derived-direction ambiguity flag and its route-wide deferral. Direction is now checked
  independently within each connected route segment. A warned legacy regional discontinuity closes one
  segment before the next starts because their coordinate directions have no declared topological relation.
  The native builder retains the first connected segment's direction for its compatibility fallback.
- Added mixed-evidence U-turn and segmented-regional-route regressions in both semantic validation and
  direct native preflight. The mixed route is rejected before allocation; the segmented route retains
  the expected first-segment direction.
- Classified a signal-failure target as an exact base block through the existing `legacyBlockIds` map
  before applying the slash-based compound-section limit. Exact wrapped selector values are unwrapped
  only when their canonical base key exists; malformed and genuine compound targets remain skipped.
- Extended the incident-export regression with an exact `Depot/1` block reference and verified the
  representable mapped incident row is retained as a legacy recovery path.
- Reserved `/` in canonical block IDs through matching semantic and direct-native diagnostics. Existing
  files still load, write, bundle, and legacy-export; they cannot reach allocation with an identity that
  the established runtime section grammar cannot distinguish safely.
- Made exact block-ID precedence suppress misleading route-component errors, and qualified the legacy
  export regression so it is not cited as canonical runtime support.
- Made legacy incident export use the shared section inventory before resolving a matching signal. An
  ambiguous target now produces `scene.ref.ambiguous` and no incident row, matching validation and native staging.
- PR 2 now allows its three anonymous linked-topology row types to be deleted while incomplete, while retaining
  the existing confirmation. Block Add is disabled unless the selected filter value names a real track, and the
  mutation slot independently rejects an invalid selection rather than silently choosing another track.
- The public editor smoke now proves both corrections. Its first post-review run exposed an offscreen focus loss
  after the new confirmation dialog, so the existing focused-Save check now explicitly reactivates the main
  window before requesting focus. The rebuilt smoke then passed without weakening the focused-value assertion.
- The final correction requires a non-empty selected track ID in both button enablement and the mutation slot.
  The smoke exercises the disabled public action and then the slot's defensive check, and it creates, confirms,
  deletes, and recreates incomplete dependency, restriction, and boundary rows through the existing controls.
- Route-detail refresh now runs once at the common infrastructure-selection boundary. The existing helper hides
  the pane on every non-Route facet, and the smoke asserts both its initial hidden state and the Routes-to-Blocks
  transition so stale route controls cannot remain visible.

### Ponytail findings

- The fresh PR 1 review found no practical production-code simplification. `SectionInventory` replaces
  former duplicate derivations, while the validator and native transition loops retain distinct diagnostic
  and mutation-boundary responsibilities.
- The reviewer suggested deleting this tracked ledger because it does not affect product behavior. That
  suggestion was not applied because the execution brief explicitly requires this file to persist through
  all milestones and remain as a final deliverable.
- The PR 2 Ponytail review identified three practical reductions: use the existing filtered block-row map for
  neighbor moves, remove text-cell commit branches made unreachable by section combos, and share the repeated
  anonymous-row add/delete smoke sequence.

### Simplifications made

- The implementation uses one transient derived inventory rather than a second persisted topology model.
  No further production simplification was justified after the clean correctness review.
- PR 2 now uses one direction-parameterized block-move path, deletes the two unreachable linked-topology text
  commit paths, and uses one local smoke helper for dependency, restriction, and boundary row deletion checks.

## Blockers

### Known authoritative-data blockers

- #181, #182, and #228 remain outside application implementation. No railway values will be invented.

### Unresolved application blockers

- PR 1 is complete. PRs 2 through 6 remain open.

## Next action

Monitor PR #291 CI, then merge if both build and sanitizer checks pass.
