# V1 native cutover report

Milestone 4 converts every committed case to a self-contained canonical V1
directory. GUI Run, CLI/headless execution, and preview now load these files
directly. Each `scene.json` contains its row-level `import_report`; this page
records the cross-case parity decisions that do not fit in a count row.

## Converted cases

| Scene | Historical conversion source | Tracks | Nodes | Arcs | Blocks | Connections | Stations / platforms | Routes | Units / compositions | Services | Passengers |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Assignment Gvc-Gdg-Ut | prior canonical scene, exported and re-imported to populate complete infrastructure | 2 | 66 | 64 | 64 | 0 | 3 / 6 | 2 | 1 / 1 | 4 | 0 |
| Copenhagen | `Input/Input_EGTRAIN_Banedanmark` (historical symlink to Copenhagen) | 168 | 4,612 | 4,444 | 1,513 | 328 | 94 / 203 | 24 | 1 / 1 | 24 | 0 |
| Lebanon | prior canonical infrastructure shell plus the documented teaching baseline | 8 | 894 | 886 | 461 | 12 | 34 / 68 | 1 | 1 / 1 | 1 | 0 |
| Milano-Brescia | `Input/Input_EGTRAIN_Milano_Brescia` | 38 | 1,032 | 994 | 312 | 109 | 29 / 92 | 48 | 16 / 16 | 62 | 0 |
| Netherlands | `Input/Input_EGTRAIN_Netherlands` | 268 | 3,408 | 3,140 | 1,984 | 708 | 41 / 156 | 74 | 1 / 1 | 8 | 0 |
| Paimpol | `Input/Input_EGTRAIN_Paimpol` | 6 | 399 | 393 | 140 | 8 | 10 / 15 | 15 | 1 / 1 | 2 | 188 |

Paimpol contains 376 passenger journeys and 12 resolved legs. Netherlands
also contains two single-track restrictions and 15 station boundaries;
Paimpol contains four single-track restrictions. Zero rows in the other cases
reflect empty active source files, not dropped records.

## Deliberate source corrections

- Copenhagen `E-Holte-Koge_2` had Koge Nord arrival `4660` and departure
  `4460`. The prior canonical scene used `4660` for both; this conversion keeps
  that validated correction instead of reintroducing a backwards timetable.
- Paimpol `Draisy-traction.txt` contains two overlapping three-row alternatives.
  The prior validated scene selected the second group. The canonical unit keeps
  those three rows; its import report records three converted and three skipped.
- Paimpol B4 arc 108 named node 1 as its start after arcs 100–107 already form
  the consecutive 1→2→…→9 chain. V1 corrects that lone endpoint to 9→10,
  matching the ordered nodes, the adjacent B0/B1 files, and the linear
  TrackLine runtime model.
- Paimpol's exact source spelling `Tregonnau Squiffiec` maps to station
  `Tregonneau_Squiffiec`. Route-choice token `Guin-Paim_EXPRESS-1-1` maps to
  canonical service `Guin-Paim-EXPRESS-1`, occurrence 1.
- Assignment timing was restored from its prior canonical `scene.json` after
  legacy export/re-import: base time `07:00:00`, duration `10000`, zero buffer,
  and zero recovery.
- Netherlands block `0-B265` had the only non-positive block length in all six
  sources (`-1.597` km), contradicting its increasing node and arc geometry.
  The canonical scene preserves the source magnitude and corrects the sign;
  the remaining 20 metres are covered by the existing final-block extension.
- Lebanon had no rolling-stock or operating source. Its small B0 route and
  service are manual teaching data. The train parameters reuse the existing
  Assignment SLT parameters without claiming Lebanon provenance.
- Eleven Milano-Brescia timetable rows are outside their service's simulated
  route, or at a cut route boundary, and therefore have no unambiguous platform.
  They remain canonical planned stops, including negative times before the
  simulation base time, but no platform is invented for them.

## Reported unresolved source relationships

- Netherlands has six `virtual*Link` station markers whose positions do not
  match a track node. They remain named stations without platforms; no service
  stop refers to them.
- Two Paimpol route-choice tokens name reverse services that are absent from the
  active two-service manifest: `Paim-Guin-EXPRESS-2-1` and
  `Paim-Guin-ALL-STOPS-2-1`. Their journeys are retained with empty legs and the
  import report records two unresolved references. A third token,
  `Guin-Paim-EXPRESS-1-2`, names occurrence 2 while the 9,000-second case horizon
  expands only one occurrence. The canonical leg remains visible, is reported,
  and is skipped by runtime passenger mapping.
- No committed active case contains a `Rollout_<n>.txt`; the importer support is
  retained, but no entrance-delay rows were invented.

## Legacy material intentionally not migrated

- `Input_EGTRAIN_Paimpol - alternativeJourneys` was an experimental alternate
  case tree, not selected by normal GUI/CLI setup. Its trains and routes are not
  merged into the active Paimpol scene.
- GUI HTML templates, geocoding caches, hidden-track lists, virtual-link drawing
  files, and generated topology listings are display, derived, or output data.
  The native GUI derives its fallback layout from canonical coordinates.
- Disabled order-list, dwell-disturbance, ROMA/rescheduling, and unused case
  variants remain classified as dead/inert in the
  [active-input matrix](v1-scene-migration-matrix.md); no schema fields were
  created for them.
- Generated trajectory, TDS, rescheduling, timetable-graph, and exchange files
  are output/interoperability data and are written below the configured output
  directory when still applicable.

The old committed `Input/` tree and every `Scenes/*/legacy` directory were
removed after all six canonical scenes passed runnable validation and native
setup. The explicit importer and exporter remain the compatibility boundary
for external legacy cases.
