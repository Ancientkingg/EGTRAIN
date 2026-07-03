# Assignment Corridor

## Decision
Use Den Haag Centraal (`Gvc`) - Gouda (`Gdg`) - Utrecht Centraal (`Ut`) as the V1 assignment corridor.

## Reason
This is the corridor named by the assignment workflow. The Netherlands audit found that the repaired legacy Netherlands data imports and validates, but its station set does not contain `Gvc` or `Gdg`. It covers a different network centered on Utrecht, Amsterdam, Hilversum, Breukelen, Duivendrecht, and Almere.

The consequence is that #32 should build a dedicated Gvc-Gdg-Ut scene. It should not assume that the current `Input_EGTRAIN_Netherlands` folder already contains the assignment infrastructure.

## Rejected Alternatives
- `Ut` - `Hvs` - `Brn`: present in the repaired legacy data, but not the assignment corridor.
- `Asd` - `Ut`: present in timetables, but broader than the assignment target and missing Gouda.
- Full repaired Netherlands case: useful as an import regression source, but too large and still not a running assignment scene.

## Consequences For #32
- Build the assignment scene around `Gvc`, `Gdg`, and `Ut`.
- Keep the first version narrow enough for one train and a small repeated timetable.
- Add only the infrastructure, routes, services, rolling stock, and timetable data needed for the assignment workflow.
- Use the repaired Netherlands import as regression coverage, not as the source of the final assignment scene.
