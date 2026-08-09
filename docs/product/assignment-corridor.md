# Assignment Corridor

## Decision

Use Den Haag Centraal (`Gvc`) to Utrecht (`Ut`) via Gouda (`Gd` and `Gdg`) as
the assignment corridor. `Assignment_Gvc_Gdg_Ut` is the current runnable
baseline.

## Current gaps

The committed scene has only `Gvc`, `Gdg`, and `Ut`. Issue #182 adds the missing
intermediate stations, keeps `Gd` and `Gdg` distinct, and applies the source
stopping patterns.

The scene defines only `SLT_Sprinter`, and all four services use that placeholder
composition. Issue #228 obtains traceable ICM3, ICM4, IRM3, and SGM3 parameters.
Issue #181 uses those records to build the source compositions. Do not guess
train physics while #228 is open.

## Data policy

- Keep the full canonical Netherlands scene intact as a reference.
- Use source-backed station, timetable, and train data in the assignment scene.
- Update the assignment generator, committed scene, and focused checks together.
- Use legacy import and export only for explicit compatibility work.
