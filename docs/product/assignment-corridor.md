# Assignment Corridor

## Decision
Use Amsterdam (`Asd`) to Hilversum (`Hvs`) as the V1 assignment corridor.

## Reason
The Netherlands input contains Amsterdam and Hilversum infrastructure, Sprinter and Intercity timetables, and SLT and VIRM train data. This gives the assignment two train types and different stopping patterns.

The active train set currently contains eight Sprinter services around Utrecht, Hilversum, Baarn, and Den Dolder. Amsterdam services still need to be created and tested.

Candidate route IDs are 29 through 36, 39 through 42, 53, and 54. These IDs came from the original case author. Each route must be checked for direction, station sequence, and train type.

## Other options

- Utrecht to Hilversum, Baarn, and Den Dolder already runs with Sprinter services. It remains the baseline but lacks Intercity traffic.
- The full Netherlands case remains the import and compatibility reference. It is too large for the final teaching scene.

## Assignment scene

- Keep the full Netherlands legacy and V1 cases intact.
- Verify one Amsterdam to Hilversum service before building a timetable.
- Add one Sprinter and one Intercity in each direction.
- Set planned times from measured runs.
- Derive a smaller scene after the train set works.
- Remove only infrastructure that is outside the verified route dependency set.
