# Netherlands scene assignment

## Objective

Prepare an Amsterdam to Hilversum teaching scene with Sprinter and Intercity services. Start from the full Netherlands case. Do not reduce the network until the selected trains run correctly.

The repository keeps two products:

- `Scenes/Netherlands` is the full V1 reference case.
- A separate Amsterdam to Hilversum scene will contain the smaller assignment network.

Read [Using EGTRAIN and authoring V1 scenes](scenes-and-application.md) before changing data.

## Supplied files

The professor supplied `Trains.zip` and `trainNames.txt`.

The active manifest contains:

```text
Spr1.txt
Spr2.txt
Spr3.txt
Spr4.txt
Spr5.txt
Spr6.txt
Spr7.txt
Spr8.txt
```

These eight services cover Utrecht, Hilversum, Baarn, and Den Dolder. They do not add Amsterdam services.

The repository already contains the archive contents. The eight active train files use `T_SLT06.txt` instead of the attachment's `T455_04.txt`. Keep the repository value. It matches the SLT train data used by the V1 scene.

The legacy case also contains Amsterdam and Hilversum timetable files, `VIRM06.txt`, `T_VIRM06.txt`, `SLT06.txt`, and `T_SLT06.txt`. The candidate routes named by the professor are:

```text
29 30 31 32 33 34 35 36
39 40 41 42
53 54
```

The route numbers are leads, not final assignments. Test each route before using it.

## Working rules

- Keep the full Netherlands legacy case intact.
- Keep `Scenes/Netherlands` as the full V1 reference.
- Work on copies until a train completes its route.
- Test one service at a time.
- Change one input at a time when diagnosing a failure.
- Record the route, direction, stopping pattern, rolling stock, and result.
- Remove unrelated infrastructure only after the timetable works.

## Work sequence

### 1. Reproduce the current baseline

Run the legacy Netherlands case with `Spr1.txt` through `Spr8.txt`. Record which services load, move, and reach their final station.

Import the legacy case and validate the result:

```bash
./build/scene_tool import \
  EGTRAIN/QEGTRAIN/Input/Input_EGTRAIN_Netherlands \
  /tmp/netherlands-v1 \
  Netherlands
./build/scene_tool validate /tmp/netherlands-v1
```

Compare the imported JSON files with `EGTRAIN/QEGTRAIN/Scenes/Netherlands`. Explain every difference before changing the committed scene.

### 2. Map the candidate routes

Inspect routes 29 through 36, 39 through 42, 53, and 54. Run a single test train on each plausible route.

Fill in this table as evidence:

| Route | Origin | Destination | Direction | Stations passed | SPR or IC | Test result | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 29 |  |  |  |  |  |  |  |
| 30 |  |  |  |  |  |  |  |
| 31 |  |  |  |  |  |  |  |
| 32 |  |  |  |  |  |  |  |
| 33 |  |  |  |  |  |  |  |
| 34 |  |  |  |  |  |  |  |
| 35 |  |  |  |  |  |  |  |
| 36 |  |  |  |  |  |  |  |
| 39 |  |  |  |  |  |  |  |
| 40 |  |  |  |  |  |  |  |
| 41 |  |  |  |  |  |  |  |
| 42 |  |  |  |  |  |  |  |
| 53 |  |  |  |  |  |  |  |
| 54 |  |  |  |  |  |  |  |

### 3. Build one working service

Choose one direction. Create one service with:

- a verified route
- the correct SLT or VIRM composition
- the matching stopping pattern
- an entry time inside the simulation window
- planned arrival and departure times
- dwell times at scheduled stops

Run it alone. Check movement, station order, final position, and output diagrams.

### 4. Add both train types and directions

After the first service works, add:

- one Sprinter in each direction
- one Intercity in each direction

Use the service worksheet for every train:

| Service | Type | Route | Direction | Timetable | Composition | Loads | Moves | Completes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
|  |  |  |  |  |  |  |  |  |

### 5. Tune the timetable

Set times from observed running times rather than guesses. For each service:

1. Run the train without timetable pressure.
2. Record the minimum running time between stops.
3. Add the agreed running time supplement.
4. Add dwell time.
5. Round the timetable as required by the assignment.
6. Run the complete timetable and check conflicts.

Keep the raw measurements with the timetable notes.

### 6. Create the smaller assignment scene

Copy the verified full V1 scene. Remove data in this order:

1. Unused services
2. Unused compositions and train units
3. Unused routes
4. Infrastructure outside the selected route dependency set

Validate and run after each step. Stop at the first failure and restore the last removed dependency.

Record memory use and startup time before and after the reduction.

## Responsibility split

Student 1 owns route and infrastructure analysis. This includes the candidate route table, direction checks, station sequence, and the later network reduction.

Student 2 owns rolling stock and timetable analysis. This includes the SLT and VIRM data, service definitions, stopping patterns, and planned times.

Both students integrate the first service, review each other's mappings, and run the final checks.

## Milestones

### Baseline

- The eight supplied Sprinter services import into V1.
- `scene_tool validate` exits successfully.
- The current legacy case reaches the end of the simulation.
- Known import adjustments are recorded.

### Route map

- Every candidate route has a direction and station sequence, or a recorded reason for rejection.
- At least one Amsterdam to Hilversum route works in each direction.

### Train set

- One Sprinter and one Intercity work in each direction.
- Each service uses the correct route, rolling stock, and stopping pattern.
- Planned times are based on measured runs.

### Assignment scene

- The full Netherlands V1 scene remains available as a reference.
- The smaller scene validates, exports, and runs.
- The scene contains no unresolved references.
- A smoke check proves train movement and station arrival output.
- The GitHub guide matches the delivered workflow.

## Meeting agenda

Use this 60 minute agenda for the first meeting:

| Time | Topic |
| --- | --- |
| 0 to 10 minutes | Goal, source files, and the difference between the full case and assignment scene |
| 10 to 20 minutes | Open the Netherlands V1 scene and trace one service through the JSON files |
| 20 to 30 minutes | Import, validate, export, and run workflow |
| 30 to 40 minutes | Candidate route IDs and the route worksheet |
| 40 to 50 minutes | Student responsibilities and the first single-train test |
| 50 to 60 minutes | Evidence, first milestone, and next review date |

End the meeting with one assigned route test per student and a shared date for integrating the first working service.

