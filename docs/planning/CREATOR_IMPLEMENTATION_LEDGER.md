# Creator implementation ledger

## Master goal

Make EGTRAIN creator-complete for an independent seventh network. A user with authoritative railway data must be able to create, validate, save, bundle, reopen, run, inspect, and export the case through public application operations without C++, essential JSON edits, Cumpari runtime staging, hidden bundled-case data, generated runtime identifiers, or private model mutation.

## Current state

- Planning baseline: `59128cea7a50b2fbc83e147f2e6d9dd6f451c2cf`
- Current `origin/main`: `59128cea7a50b2fbc83e147f2e6d9dd6f451c2cf`
- Current milestone: PR 1, section resolution and signal binding
- Current worktree: `/Users/samuelbruin/Downloads/EGTRAIN/local/worktrees/creator-section-resolution`
- Current branch: `feature/creator-section-resolution`
- Current PR: #290, `Bind signals to canonical track sections`
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
- `git diff --check` passed after the final rebuild and graph refresh.
- Milestone implementation commit: `987488f`, `Add canonical section resolution and signal binding`.

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
- An eighth fresh corrected-diff review remains required before Ponytail review.

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

### Ponytail findings

- Not run for PR 1 yet.

### Simplifications made

- The planned design rejects a second topology model and retains existing section identities unless the source trace proves that impossible.

## Blockers

### Known authoritative-data blockers

- #181, #182, and #228 remain outside application implementation. No railway values will be invented.

### Unresolved application blockers

- PR 1 has no known test or parity blocker. An eighth independent correctness review and Ponytail review remain
  before merge. PRs 2 through 6 remain open.

## Next action

Commit and push the seventh corrected-diff fixes, then run an eighth fresh review of PR #290. When no merge blocker
remains, run the fresh Ponytail simplicity review before final verification and merge.
