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
| rolling_stock.json | yes | `train_units: [{id}]`, `compositions: [{id, units: [unit-id]}]` |
| services.json | yes | `services: [{id, composition, route, stops: [{station, platform?, arrival_seconds?, departure_seconds, dwell_seconds}]}]`. `arrival_seconds` may be omitted only on the first stop. Times are seconds from the scene base time. |
| incidents.json | no | `incidents: [{id, type: "signal_failure"\|"train_breakdown", target, start_seconds, end_seconds}]` |
| views.json | no | free-form display defaults; parse-checked only |

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
- `scene.field.missing`: Missing or mistyped required field on an item, including `arrival_seconds` on any stop after the first.
- `scene.version.missing`: Missing `schema_version`.
- `scene.version.unsupported`: `schema_version` is not the integer `1`.
- `scene.units.unsupported`: A unit other than `m`, `s`, or `m/s`.

**Semantic Errors**
- `scene.trains.none`: No compositions or train units.
- `scene.services.none`: No services.
- `scene.ref.unresolved`: Reference to missing entity (e.g. service→composition, service→route, stop→station, incident target).
- `scene.ref.platform`: Stop names a platform not on the stop's station.
- `scene.service.no_stops`: Service with empty stops array.
- `scene.composition.empty`: Composition with empty units array.
- `scene.route.empty`: Route with empty blocks array.
- `scene.id.duplicate`: Duplicate id within a collection.
- `scene.time.invalid`: Negative times, departure < arrival, non-increasing departures along a route.
- `scene.dwell.invalid`: Dwell < 0.
- `scene.incident.type`: Incident type other than `signal_failure` or `train_breakdown`.
- `scene.incident.window`: Incident end <= start, or negative times.
- `scene.basetime.invalid`: Base time present but not "HH:MM:SS".

**Semantic Warnings**
- `scene.dwell.exceeds_window`: Dwell time is larger than the (departure - arrival) window.
