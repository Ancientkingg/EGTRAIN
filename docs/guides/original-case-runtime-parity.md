# Original-case native runtime parity

This report closes the historical evidence gap for the Netherlands, Paimpol,
Copenhagen, and Milano-Brescia native loaders. It compares generated output,
not merely legacy conversion output.

## Baseline and method

The trusted pre-cutover baseline is
`d3f5c7005c7030ba3745c8a41b0572e61974bd15` (PR #262). It is the direct parent
of the native runtime cutover in PR #263. At that commit canonical scenes were
already present, but normal GUI and CLI runs still built the simulation from
the committed `Input/` trees. This makes it the last revision where the old
and new representations coexist without making the native loader the normal
runtime path.

Both that revision and the current native revision were built with tests
enabled and run headlessly for cases 1–4 with `-g 0 -TSM 0 -RC 0`. The retained
observables are the canonical structure, `EnergyConsumptionPerTrain.txt`,
`TrainServicePathDiagram.txt`, `TimetablePoints.txt`, and
`Stats_Stations.txt`. PR #302 separately established conversion parity; its
counts support the structure comparison below but are not used as a substitute
for the runtime output comparison.

## Infrastructure and signalling

Counts are baseline / native:

| Case | Blocks | Connections | Stations / platforms | Routes | Stable identifiers |
| --- | ---: | ---: | ---: | ---: | --- |
| Netherlands | 1,984 / 1,984 | 708 / 708 | (41 / 156) / (41 / 156) | 74 / 74 | station `Ut`…`virtualDvdLink`; route `route0`…`route73` |
| Paimpol | 140 / 140 | 8 / 8 | (10 / 15) / (10 / 15) | 15 / 15 | station `Guingamp`…`Paimpol`; route `route0`…`route14` |
| Copenhagen | 1,513 / 1,513 | 328 / 328 | (94 / 203) / (94 / 203) | 24 / 24 | station `Frederikssund`…`Hillerod`; route `route0`…`route23` |
| Milano-Brescia | 312 / 312 | 109 / 109 | (29 / 92) / (29 / 92) | 48 / 48 | station `Segrate`…`BivioRoncadelle`; route `route0`…`route47` |

The complete station-ID and route-ID sets at the baseline and native revisions
are identical. The focused smoke stores SHA-256 digests of those sets. Legacy
runtime block and connection identifiers used a different derived syntax, so
their identities cannot be compared honestly one-for-one; their counts match,
and PR #302 verifies that importing each retained legacy source produces the
same canonical block and connection data as the committed scene.

## Trains, trajectories, and arrivals

Each representative has the same trajectory origin and the same ordered stop
set at both revisions. `End` is `(time seconds, position metres)` and `arrivals`
is the first/last simulated station arrival. The tolerances are explicit in the
focused smoke.

| Case | Runtime trains baseline / native | Representative | End baseline → native | Arrivals baseline → native | Allowed end tolerance |
| --- | ---: | --- | --- | --- | --- |
| Netherlands | 40 / 40 | `SPR_2-1` | `(7999, 1252.38)` → `(7999, 1335.0)` | `(132, 847)` → `(135, 1190)` | 0 s, 100 m |
| Paimpol | 2 / 2 | `Guin-Paim-EXPRESS-1-1` | `(2571, 37081.3)` → `(2789, 37076.9)` | `(43, 2558)` → `(36, 2705)` | 250 s, 10 m |
| Copenhagen | 196 / 196 | `F-Hellerup-NyEllebjerg-2-1` | `(934, 37943.0)` → `(1877, 37951.9)` | `(367, 927)` → `(364, 1819)` | 950 s, 10 m |
| Milano-Brescia | 65 / 65 | `201-1` | `(3673, 78768.2)` → `(3673, 78768.2)` | `(1855, 3666)` → `(1855, 3666)` | 0 s, 0.1 m |

The longer Paimpol and Copenhagen times are not tuned values. Their retained
legacy output recorded arrival and departure at the same instant through
intermediate stops. The native path honors the canonical planned times and
dwell values, so it reaches the same ordered stations and route endpoint later.
The tolerances cover the observed scheduling correction while keeping the
path endpoint tight. Netherlands remains active at the 8,000-second horizon;
its final position and five-stop sequence are compared instead of inventing a
completion time.

The expanded train total is identical in all four cases. Milano's legacy
output used `9707-1` and `9709-1` twice. The native output retains 65 trains but
uses the distinct canonical service IDs `9707_2-1` and `9709_2-1` for the
second pair.

## Aggregate served-station output

| Case | Served rows baseline / native | Comparison |
| --- | ---: | --- |
| Netherlands | 10 / 10 | same station set; some occurrence totals are lower after planned waits are honored |
| Paimpol | 9 / 9 | same station set |
| Copenhagen | 93 / 92 | native run has no served `BuddingeReverse` row within the horizon; the station, platform, and service stop remain canonical |
| Milano-Brescia | 13 / 13 | sets differ: legacy output includes `MelzoScalo3-5`, while native output includes `MorengoBariano`; the representative 17-stop sequence is identical |

These differences are recorded rather than converted into fabricated equality.
The focused regression asserts exact network counts, stable station/route ID
sets, expanded train counts, representative path endpoints, ordered stops, and
arrival tolerances. The existing smoke additionally requires real movement and
served arrivals for all four original cases, without legacy runtime staging.
