# Scene Model Design

The current scene input is split across many legacy folders and text files. It is hard to validate, hard to edit, and easy to break. V1 should introduce a canonical scene model that sits between the UI and the current simulator input files.

```text
legacy case folder -> importer -> canonical scene -> validator -> editor UI
canonical scene -> exporter -> current simulator input files
```

## Goals

- Use one scene model for validation, UI editing, and export.
- Keep the current simulator running while the input layer changes.
- Convert old case-study folders into the new format.
- Keep scene files readable for instructors and researchers.
- Make manual text editing optional instead of required.

## Format

Use one directory per scene. Prefer JSON for V1 because Qt can read and write it without a new parser dependency.

```text
scene.json              metadata, schema version, units, base time
infrastructure.json     nodes, arcs, tracks, switches, speed limits, gradients
stations.json           stations, station types, platforms
signalling.json         signals, block sections, routes, corridors
rolling_stock.json      train units and train compositions
services.json           services, stops, dwell times, platforms, timetable values
incidents.json          signal failures, train breakdowns, disruption windows
views.json              default diagram and viewport settings
```

The schema can be split later if a single-file scene is easier for exchange.

The V1 schema is defined in scene-schema.md.

## Validator

The validator should catch:

- missing files
- unresolved station, route, platform, signal, or train references
- zero trains
- empty routes
- invalid timetable values
- invalid dwell times
- incident targets that do not exist
- output path problems

Each error should name the scene item and suggest the smallest fix.

## Converter

The converter has two directions:

- Legacy importer: reads existing case-study folders and writes canonical scenes. Implemented; see scene-schema.md for the mapping.
- Legacy exporter: writes the current simulator input files from a canonical scene. Implemented. The editor->export->simulate path now exists end to end.

The editor should save the canonical scene. The simulation can keep reading exported legacy files until the simulator input layer is replaced.

## Migration Path

1. Define the scene schema.
2. Import Copenhagen and Brescia into canonical scenes.
3. Validate converted scenes.
4. Export scenes back to current input files.
5. Add the MVP editor on top of the scene model.
6. Build the Den Haag Centraal to Utrecht assignment case through the scene model.
