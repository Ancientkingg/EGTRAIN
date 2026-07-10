# Scene Schema Reference

The V1 scene schema defines a canonical JSON-based model. It consists of multiple JSON files within a single directory, each handling a different aspect of the simulation scene.

## Versioning

The schema is versioned using an integer `schema_version` inside `scene.json`. The current version is `1`.

## File Structure

| file | required | V1 content |
|---|---|---|
| scene.json | yes | `schema_version` (integer, must be 1), `name` (string, non-empty), optional `description`, optional `base_time` ("HH:MM:SS"), optional `units` object. The only supported units are the defaults: `distance: "m"`, `time: "s"`, `speed: "m/s"`. |
| infrastructure.json | yes | `nodes: []`, `arcs: []` (arrays may be empty in V1; full modelling comes with the importer) |
| stations.json | yes | `stations: [{id, name, platforms: [{id}]}]` |
| signalling.json | yes | `signals: [{id}]`, `routes: [{id, blocks: [string]}]` (block ids are opaque strings in V1; they are resolved against real block data once the legacy importer exists) |
| rolling_stock.json | yes | `train_units: [{id, physical?: {mass_of_traction_unit_kg, mass_of_a_wagon_kg, number_of_wagons, max_speed_ms, max_deceleration_ms2, frontal_area_m2, resistance_coefficient, jerk_ms3, length_m}, traction_curve?: [[v_lower, v_upper, c0, c1, c2]], source?: {data_file, traction_file}}]`, `compositions: [{id, units: [unit-id]}]` |
| services.json | yes | `services: [{id, composition, route, entry_time_seconds?, repeat?: {headway_seconds}, stops: [{station, platform?, arrival_seconds?, departure_seconds?, dwell_seconds}]}]`. `arrival_seconds` may be omitted on any stop (no scheduled arrival; typical for the first stop). `departure_seconds` may be omitted only on the last stop (terminus). Times are seconds from the scene base time and may be negative (an instant before the base time). A service with an empty `stops` array is a through service that stops nowhere. |
| incidents.json | no | `incidents: [{id, type: "signal_failure"\|"train_breakdown", target, start_seconds, end_seconds}]` |
| views.json | no | free-form display defaults; parse-checked only |

## Loaded Data Metadata

`scene.json.loaded_data` is derived provenance metadata written by the app. The loader recomputes it from the canonical files, so it is not the source of truth. Each row has `category`, `source_file`, `parsed_count`, `status`, and optional recursive `children`.

Current categories are `scene`, `infrastructure`, `stations`, `timetable`, `rolling_stock`, `signalling`, `incidents`, and `passenger_data`. File-backed categories include drill-down rows for `raw_file`, `parsed_objects`, and `derived_simulation`; the GUI adds validation status rows from current diagnostics. `rolling_stock` also exposes `train_units`, `compositions`, and `source_files`; `source_files` lists linked `data_file` / `traction_file` entries such as `LITRA` and `T_LITRA` files when present. `signalling` exposes `signals` and `routes`.

## Examples

**scene.json**
```json
{
    "schema_version": 1,
    "name": "Minimal Test Scene",
    "description": "A minimal scene for validation tests",
    "base_time": "08:00:00",
    "units": {
        "distance": "m",
        "time": "s",
        "speed": "m/s"
    }
}
```

**infrastructure.json**
```json
{
    "nodes": [],
    "arcs": []
}
```

**stations.json**
```json
{
    "stations": [
        {
            "id": "st.hillerod",
            "name": "Hillerød",
            "platforms": [{"id": "p1"}, {"id": "p2"}]
        }
    ]
}
```

**signalling.json**
```json
{
    "signals": [
        {"id": "sig.01"}
    ],
    "routes": [
        {
            "id": "rt.main",
            "blocks": ["blk.01", "blk.02", "blk.03"]
        }
    ]
}
```

**rolling_stock.json**
```json
{
    "train_units": [
        {"id": "tu.01"}
    ],
    "compositions": [
        {
            "id": "comp.01",
            "units": ["tu.01"]
        }
    ]
}
```

**services.json**
```json
{
    "services": [
        {
            "id": "svc.a1",
            "composition": "comp.01",
            "route": "rt.main",
            "stops": [
                {
                    "station": "st.hillerod",
                    "platform": "p1",
                    "departure_seconds": 120,
                    "dwell_seconds": 0
                }
            ]
        }
    ]
}
```

**incidents.json**
```json
{
    "incidents": [
        {
            "id": "inc.01",
            "type": "signal_failure",
            "target": "sig.01",
            "start_seconds": 300,
            "end_seconds": 900
        }
    ]
}
```

**views.json**
```json
{
    "diagram": {
        "x": 0,
        "y": 0,
        "zoom": 1.0
    }
}
```

## Diagnostics

The validator produces the following diagnostic codes. Note that semantic checks are only executed if structural loading produced no Errors.

**Structural Errors**
- `scene.dir.missing`: Directory absent.
- `scene.file.missing`: Missing required file.
- `scene.json.parse`: Unparseable JSON.
- `scene.section.missing`: Absent or mistyped required top-level key, or a file whose root is not an object.
- `scene.field.missing`: Missing or mistyped required field on an item.
- `scene.version.missing`: Missing `schema_version`.
- `scene.version.unsupported`: `schema_version` is not the integer `1`.
- `scene.units.unsupported`: A unit other than `m`, `s`, or `m/s`.
- `scene.import.missing`: Missing legacy file or directory, or a scene file that could not be written.
- `scene.import.parse`: Malformed token or row during legacy import.
- `scene.import.ref`: Unresolved reference during legacy import (e.g. missing referenced timetable or data file, route index out of range).

- `scene.export.write`: A file or directory could not be written during export.
- `scene.export.ref`: Missing data needed for export (e.g. unit physical data, a service referencing an unknown route).
- `scene.export.unsupported`: Scene cannot be represented in the legacy format (e.g. station id contains whitespace, or route ids mixing the `route<N>` pattern with other names).
- `scene.export.adjusted` (Warning): The exporter changed a derived value (e.g. a sanitized service file name collided and was suffixed).

**Semantic Errors**
- `scene.trains.none`: No compositions or train units.
- `scene.services.none`: No services.
- `scene.ref.unresolved`: Reference to missing entity (e.g. service→composition, service→route, stop→station, incident target).
- `scene.ref.platform`: Stop names a platform not on the stop's station.
- `scene.composition.empty`: Composition with empty units array.
- `scene.route.empty`: Route with empty blocks array.
- `scene.id.duplicate`: Duplicate id within a collection.
- `scene.time.invalid`: Departure before arrival at a stop.
- `scene.dwell.invalid`: Dwell < 0.
- `scene.incident.type`: Incident type other than `signal_failure` or `train_breakdown`.
- `scene.incident.window`: Incident end <= start, or negative times.
- `scene.basetime.invalid`: Base time present but not "HH:MM:SS".
- `scene.repeat.invalid`: Non-positive headway_seconds.

**Semantic Warnings**
- `scene.dwell.exceeds_window`: Dwell time is larger than the (departure - arrival) window.
- `scene.service.no_stops`: Service with an empty stops array (a through service; legitimate but worth flagging).
- `scene.time.order`: Departure times do not increase along a service's stops.

**Import Warnings and Infos**
- `scene.import.adjusted` (Warning): The importer changed or dropped a legacy value to fit the schema (e.g. a departure before its arrival raised to the arrival, or a duplicate service name suffixed). The message names the service, station, and values.
- `scene.import.info` (Info): Non-fatal import notes, such as falling back to directory enumeration when `trainNames.txt` is absent.

## Importing legacy case studies

The `SceneImporter` converts a legacy simulation folder into a canonical V1 scene.
The mapping is as follows:
- `trainNames.txt` (or all `Trains/*.txt` files) -> `services.json`
- `TrainData/*.txt` and `TrainData/T_*.txt` -> `rolling_stock.json`
- All `Routes/Route<N>.txt` files (numerically sorted) -> `signalling.json` (routes). Unreferenced routes become part of the scene because the simulator loads `Route0`..`Route<N-1>` unconditionally.
- `Tracklines/Stations.txt` -> `stations.json`
- Unconverted files (e.g. `TMS`, `TDS`) are copied verbatim to `<sceneDir>/legacy/`

Note on one-shot services: The legacy format uses a headway sentinel of `>= 99999999` to denote a service that runs only once. The importer omits the `repeat` field for such services. The exporter will write `99999999` back when exporting services without a `repeat` block.

## Exporting to legacy input

The `SceneExporter` converts a canonical V1 scene back into the legacy format so the simulator can run it.
The mapping back is:
- `services.json` -> `Trains/`, `TimeTable/`, and `trainNames.txt`
- `rolling_stock.json` -> `TrainData/`
- `signalling.json` (routes) -> `Routes/`
- `incidents.json` -> `Incidents.txt` when incidents are present
- `<sceneDir>/legacy/` is recursively walked and copied to the export directory. During this passthrough, top-level directories named `tracklines` or `timetable` (case-insensitive) are renamed to `TrackLines` and `TimeTable` to match the simulator's hard-coded case-sensitive paths.

What is not exported and why:
- `stations.json` and `infrastructure.json`: The simulator reads infrastructure from the Tracklines passthrough, so they are not written directly.
- `views.json`: UI-only preferences, not simulation input.

**Known lossiness during round-trip**:
- `legacy_source` records provenance when a scene is imported and does not round-trip.
- A hand-made scene without `entry_time_seconds` gains one on export+re-import (the legacy format requires the token; the exporter defaults it to the first stop's departure, else 0).
- `99999999` is the scene-layer convention for one-shot services. The legacy format does not have an explicit branch for this; it simply yields one train for any realistic horizon.
- Station ids cannot contain whitespace (constrained to whitespace-free identifiers), because the legacy timetable format is tokenized by whitespace.
