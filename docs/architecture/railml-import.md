# railML import

`scene/RailmlImporter` converts a railML 2.x file into a V1 scene folder. The file may hold infrastructure, rolling
stock and a timetable, as exports of OpenTrack and FBS/iPLAN do. The import writes the scene with
`replaceSceneDirectory`, in a private folder that is then renamed into place, as the legacy import does.

```text
railML 2.x file -> importRailmlScene -> canonical SceneModel -> replaceSceneDirectory -> scene folder
```

Entry points:

- `scene_tool import-railml <file> <sceneDir> [sceneName] [--operating-period id]`
- File > Import railML... in the application. It asks for the file and a destination folder and handles the
  destination as Import Legacy Case does.

The scene name is the one given, else the file name without its extension. The description names the railML
version, the file and the imported operating period.

## Accepted files

The root element must be `railml` (any case) with a `version` that starts with `2.`, or without a version and with a
namespace `http(s)://www.railml.org/schemas/20xx`. A railML 3 file (version 3.x or a `schemas/3` namespace) is refused
with `scene.import.railml.version`, any other XML file with `scene.import.railml.format`, and a file that cannot be read
or parsed with `scene.import.railml.read`. Each refusal is one error and writes nothing. A file without an importable
track is refused with `scene.import.railml.empty`.

## Mapping

| railML element | Scene | Notes |
| --- | --- | --- |
| `track` | `infrastructure.json` tracks, nodes, arcs, blocks | Length from `trackBegin`/`trackEnd` `pos`. A track is split into pieces `<track>.s<k>` at each switch or crossing inside it, and a macroscopic track at each OCP where another track ends or a train changes track. Node, arc and block ids are `<piece>.n<k>`, `.a<k>`, `.b<k>` in chainage order. |
| `connection` (track end, `switch`, `crossing`) | `infrastructure.json` connections | Pairs of connections that refer to each other through `ref`. The id is the id of one of the two connection elements, `<ocp>.<n>` for a join at a macroscopic node, and `<piece>.join` between two pieces of one track. `orientation` incoming and outgoing decides the piece end inside a track; `rightAngled` crossing connections are not converted. The switch `course` is not used. |
| `macroscopicNode ocpRef` | connections | Track ends at one OCP are joined where a train part moves from one to the other, else where the layout allows it. |
| `bufferStop`, `openEnd` | track end | A track end without a connection. |
| `crossSection ocpRef` | station platform node | A platform with the piece id on that track, when the OCP has no platform edge on it. |
| `platformEdge` | `stations.json` platform | Id and `length` of the edge; the stop node is at the middle of the edge. The OCP is `ocpRef`, else the nearest cross-section. |
| `ocp` | `stations.json` station | Id and `name`. An OCP on no track is skipped. |
| `geoCoord` of an OCP | `views.json` station latitude and longitude | `coord` read as latitude then longitude in WGS 84 (EPSG:4326, also when `epsgCode` is missing). Other systems and 0 0 are skipped. |
| `signal` type `main` or `combined` | `signalling.json` signal | Starts a block at its position. It protects the block on its side of travel (`dir`). Other types are skipped. |
| `speedChange` | arc `speed_limit_ms` | `vMax` per direction, `vMax="end"` restores the previous limit. An arc takes the lower of its up and down limits. Changes of other speed profiles than the one a track uses are skipped (below). |
| `gradientChange` | arc `gradient_percent` | `slope` in per mille, stored as rise per length (5 per mille is 0.005), signed for the chainage direction of the piece. |
| `radiusChange` | arc `curvature_radius_m` | 0 is straight. |
| `vehicle` | train unit data | Through the formations; a vehicle in no formation becomes a unit of its own. |
| `formation` | `rolling_stock.json` train unit and composition | One unit and one composition per formation, both with the formation id. |
| `operatingPeriod` | which train parts are imported | One period per import (below). |
| `category` | service `category` | The `code`, else the `name`. |
| `trainPart` | `services.json` service | Through its train (below). Stops from `ocpTT`, route from `sectionTT/trackRef`. |
| `train` | service | One service per operational `trainPartSequence`; commercial trains are skipped when the file has operational ones. |

Every other element kind is counted and reported as unsupported, for example `trainDetector`, `line`,
`electrificationChange`, `gaugeChange`, `ownerChange`, `axleWeightChange`, `mileageChange` and `rostering`.

## Network and chainage

The scene has one chainage axis. The import orients the pieces so that every connection runs from the higher end of
one piece to the lower end of the next, and lays them out by the longest path from the pieces without predecessors.
A connection gets 1 m of chainage where the layout leaves no slack. A link that closes a loop, or that would give a
track end more than 6 connections, cannot be held and is left out (`scene.import.railml.topology`); trains that need it
are not imported. Where the network has two paths of different lengths between the same points, the shorter path gets
a longer connection: a link that no imported train uses is then left out, and the remaining long connections are
reported with their total length (`scene.import.railml.connection_length`), since trains over them run that much
further.

## Blocks and signals

With main signals in the file, the blocks of a track run between its main and combined signals and its piece ends. A
macroscopic file has no main signals: the blocks then run between the OCPs of each track, and the import generates two
signals per block, `<block>.up` and `<block>.down` (`scene.import.railml.signals_generated` with the count). railML 2
has no signalling system per track, so the whole network gets one signalling area `network` at level 0, fixed block
with lineside signals, as the bundled cases have.

## Arcs

A piece is cut at its block boundaries, OCPs and platforms, speed, gradient and radius changes. A section holds at most
20 arcs, and a block at a track end at most 9, because it shares a switch section with the block across the
connection. Where a block would hold more, the import merges neighbouring arcs, radius and gradient changes first and
speed changes last. A merged arc has the lower speed limit and the length-weighted mean gradient and curvature
(`scene.import.railml.profile_merged`). A stretch without a speed change gets 100 km/h. Each track uses its speed
changes without a profile, else those of the speed profile that the most train sequences refer to, else the profile
with the most changes on that track.

## Layout

The layout is geographic when every piece has two stations with coordinates: each piece gets a region, and its
stations are anchored at their latitude and longitude. Otherwise the layout is schematic: one region, the tracks along
their chainage, parallel pieces on separate levels. `scene.import.railml.layout` says which one was used.

## Rolling stock

| Quantity | Source | Default |
| --- | --- | --- |
| Length | formation `length`, else the sum of its vehicles | 70 m |
| Maximum speed | formation `speed`, else the lowest vehicle `speed` | 100 km/h |
| Masses | `bruttoWeight`, else `tareWeight`, else `nettoWeight` of the vehicles: powered vehicles (driven axles or an engine) are the traction unit, the others wagons; else the formation `weight` | 151 t, the mass of the reference unit |
| Passenger capacity | sum of `places` counts, without bicycle and `other:` places | none |
| Deceleration, frontal area, resistance, jerk | railML 2 has none | 0.75 m/s2, 1.45 m2, 0.004, 0.75 m/s3 |
| Traction curve | railML 2 has none | the curve of the reference unit, forces scaled by unit mass / 151 t, cut at the maximum speed and constant above 130 km/h |

The reference unit is `SLT_Sprinter` of the rolling stock library (`resources/rolling_stock_library.json`). A new train
unit in the editor has no physical data or traction, so the import takes a library unit instead. Scaling the forces by
mass gives every unit the acceleration of the reference unit. Each defaulted quantity is one
`scene.import.railml.default` diagnostic that lists the units. A train part without a formation runs with the unit
`railml_default`, which has the reference values.

## Timetable

- Operating period: `--operating-period <id>` (`RailmlImportOptions::operatingPeriod`), else the period that the most
  train parts refer to. Train parts without a period are always imported. The diagnostic names the period and the
  number of train parts left out; an unknown id is an error.
- Services: one per operational train sequence, with the id of the train, and one per train part in no sequence, with
  the id of the train part. Coupled parts of one sequence run as the first part. Operating code from `trainNumber`,
  maximum speed from `formationTT speed`.
- Stops: `ocpTT` of type stop, begin or end, with the scheduled times (`times scope="scheduled"`, else the first
  `times`), `arrivalDay`/`departureDay`, and `stopTimes minimalTime` as the dwell (0 s when missing). A time that runs
  back by more than 12 hours is on the next day (`scene.import.railml.midnight`). A first OCP that is passed gives the
  entry time. The platform of a stop is the first platform of its OCP on the route after the previous stop.
- Route: the tracks of `sectionTT/trackRef` with `dir` (else from the OCP positions), through the connections that join
  them, from the first to the last OCP. Services with the same route share it, with the id `route.<service>`. A route
  of one block that runs down the chainage is extended by one block, as the simulation reads a one-section route as
  forward.
- A service with more than 40 stops runs as consecutive services `<id>_1`, `<id>_2`, ... that meet at a shared stop.
- Base time: the earliest time, down to the minute. Duration: to the latest time plus 10 minutes.

## Report

The import report of `scene.json` has one row per railML element kind, `railml:<element>`, with the railML file as
source and the counts of source elements, converted, skipped and unresolved references. Kinds that the file does not
have get no row.

| Code | Severity | Meaning |
| --- | --- | --- |
| `scene.import.railml.format`, `.version`, `.read`, `.empty` | error | the file is refused |
| `scene.import.railml.operating_period` | info, error | the imported period and the parts left out; an unknown period |
| `scene.import.railml.default` | info | a value the file lacks, with the default and the count |
| `scene.import.railml.unsupported` | info | an element kind without a scene counterpart, with the count |
| `scene.import.railml.signals_generated` | info | the generated signals, with the count |
| `scene.import.railml.layout` | info | geographic or schematic layout |
| `scene.import.railml.profile_merged` | info | changes merged into neighbouring arcs |
| `scene.import.railml.midnight` | info | train parts past midnight without a day offset |
| `scene.import.railml.skipped` | info, warning | elements that cannot be converted, such as an OCP on no track |
| `scene.import.railml.unresolved` | warning | references to elements that the file does not hold |
| `scene.import.railml.topology` | info, warning | links left out of the chainage |
| `scene.import.railml.connection_length` | warning | connections longer than 1 m |
| `scene.import.railml.service_skipped`, `.stop_skipped`, `.service_split`, `.coupled` | warning | trains and stops that are not imported as in the file |
| `scene.import.railml.opposing_traffic` | warning | macroscopic tracks with trains in both directions |
| `scene.import.railml.capacity` | warning | more nodes in a block or more services than the simulation holds |

## Limits

- A macroscopic network has no station tracks. Trains of opposite directions on one of its single-track lines cannot
  pass each other: when they meet they stop head to head until the end of the run. The import warns about these
  tracks (`scene.import.railml.opposing_traffic`); passing loops and single-track restrictions have to be added in the
  editor.
- Speed profiles per train category, the switch course and branch speeds, train detectors, axle counters, balises and
  electrification are not converted.
- railML 3 is not read.
