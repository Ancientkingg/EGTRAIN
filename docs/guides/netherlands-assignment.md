# Netherlands assignment development brief

## Purpose

This is a working brief for the assignment lead and two teaching assistants. The three of us are co-authors of a new EGTRAIN assignment based on the existing OpenTrack assignment. The teaching assistants are not students completing the assignment.

Our first task is to agree on the learning goals, build a working Amsterdam to Hilversum reference case, and decide which parts of the OpenTrack assignment transfer well to EGTRAIN. The final student handout and model answers come after the reference case works.

Read [Using EGTRAIN and authoring V1 scenes](scenes-and-application.md) and the [V1 scene property reference](v1-scene-properties.md) before changing scene data.

## Material we are adapting

The OpenTrack model answers use the Den Haag Centraal to Utrecht corridor. They assess a sequence of modelling and analysis tasks:

1. Define train compositions and services.
2. Measure minimum running times with unhindered trains.
3. Add running time supplements and produce a conflict-free timetable.
4. Inspect speed, traction, and blocking time diagrams.
5. Compare rolling stock assignments.
6. Calculate headways, identify critical blocks, and assess capacity.
7. Simulate signal and rolling stock failures.
8. Insert an extra train with as little delay as possible.

The model answers state that the method matters more than reproducing their exact numerical results. Our assignment should follow the same rule. We must document the assumptions, rounding method, train performance settings, and service choices used for our own reference answers.

The Netherlands material supplied by the professor includes `Trains.zip` and `trainNames.txt`. The active manifest contains `Spr1.txt` through `Spr8.txt`. These services cover Utrecht, Hilversum, Baarn, and Den Dolder, but not Amsterdam.

The committed files use `T_SLT06.txt` instead of the attachment's `T455_04.txt`. Keep `T_SLT06.txt`; it matches the SLT data used by the current Netherlands scene. The case also contains Amsterdam and Hilversum timetable files plus SLT and VIRM rolling stock data.

The professor identified these route numbers as candidates:

```text
29 30 31 32 33 34 35 36
39 40 41 42
53 54
```

They are leads, not confirmed assignments. We must establish direction, station order, stopping suitability, and runtime behaviour before using any of them in the assignment.

## Current EGTRAIN baseline

`EGTRAIN/QEGTRAIN/Scenes/Netherlands` is the full V1 reference scene. Keep it intact. Create the smaller Amsterdam to Hilversum scene only after the selected services work on the full network.

The repaired legacy case currently imports and validates with:

- 41 stations
- 74 routes with 5,429 route entries
- 8 Sprinter services
- 1 train unit and 1 composition

The eight services run to the end of the simulation, but they do not yet establish the Amsterdam to Hilversum assignment. Station output, optional passenger data, the GUI timetable template, the candidate route directions, and the Intercity services still need verification.

Reproduce the baseline with:

```bash
./build/scene_tool import \
  EGTRAIN/QEGTRAIN/Input/Input_EGTRAIN_Netherlands \
  /tmp/netherlands-v1 \
  Netherlands
./build/scene_tool validate /tmp/netherlands-v1
```

Compare the result with `EGTRAIN/QEGTRAIN/Scenes/Netherlands`. Record every importer adjustment that affects the proposed assignment.

## Decisions for the co-author team

Before writing student questions, agree on:

- the learning outcomes and assumed prior knowledge
- the exact corridor boundaries and stations
- the Sprinter and Intercity service pattern in both directions
- the train compositions and performance settings
- the timetable period, dwell times, supplements, and rounding rules
- which OpenTrack analysis tasks EGTRAIN can support with evidence students can inspect
- the evidence students must submit for each question
- the grading criteria and acceptable numerical variation

Use this table to decide what transfers from OpenTrack:

| OpenTrack task | Keep, adapt, or omit | EGTRAIN evidence | Reference answer owner | Open question |
| --- | --- | --- | --- | --- |
| Train compositions and services |  |  |  |  |
| Minimum running times |  |  |  |  |
| Running time supplements |  |  |  |  |
| Conflict-free timetable and overtaking |  |  |  |  |
| Speed and traction analysis |  |  |  |  |
| Blocking time analysis |  |  |  |  |
| Rolling stock comparison |  |  |  |  |
| Headway and capacity assessment |  |  |  |  |
| Signal and train failures |  |  |  |  |
| Extra train insertion |  |  |  |  |

## Development sequence

### 1. Verify the route and service data

Test each candidate route with one train at a time. Record the result instead of inferring direction or suitability from the route number.

| Route | Origin | Destination | Direction | Stations passed | Suitable for | Result and evidence |
| --- | --- | --- | --- | --- | --- | --- |
| 29 |  |  |  |  |  |  |
| 30 |  |  |  |  |  |  |
| 31 |  |  |  |  |  |  |
| 32 |  |  |  |  |  |  |
| 33 |  |  |  |  |  |  |
| 34 |  |  |  |  |  |  |
| 35 |  |  |  |  |  |  |
| 36 |  |  |  |  |  |  |
| 39 |  |  |  |  |  |  |
| 40 |  |  |  |  |  |  |
| 41 |  |  |  |  |  |  |
| 42 |  |  |  |  |  |  |
| 53 |  |  |  |  |  |  |
| 54 |  |  |  |  |  |  |

### 2. Build the reference service set

Start with one verified service. Check its route, direction, rolling stock, stopping pattern, entry time, dwell times, station order, and final position. Then add one Sprinter and one Intercity in each direction.

| Service | Type | Route | Direction | Composition | Loads | Moves | Completes | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
|  |  |  |  |  |  |  |  |  |

Run trains separately before measuring minimum running times. Add timetable constraints only after the unhindered runs are repeatable.

### 3. Prototype the assignment questions

For each retained OpenTrack task, write one EGTRAIN question and solve it ourselves. Keep the evidence that produced the answer, including input values, output files, screenshots where they clarify a diagram, and any calculation outside EGTRAIN.

A question stays in the assignment only when:

- the required EGTRAIN function works on the reference scene
- the result can be checked from saved evidence
- the wording permits documented modelling choices without making the answer arbitrary
- the model answer explains the method and the expected range of results

### 4. Build the smaller assignment scene

Copy the verified full scene. Remove unused services, rolling stock, routes, and infrastructure in that order. Validate and run after each reduction. Keep a dependency when removing it changes the route or simulation result.

The current V1 format still depends on `legacy/` for track geometry and other runtime inputs. Do not distribute a scene without that directory until the native scene-input work listed in the [V1 reference](v1-scene-properties.md#work-that-removes-the-legacy-dependency) is complete.

### 5. Test the student workflow

Each co-author completes the draft from a clean copy without using the model answers. Record ambiguous wording, missing files, hidden assumptions, runtime, and any step that requires repository knowledge not taught in the handout.

Revise the handout and model answers from those test runs. A second review should reproduce the same method and a defensible range of numerical results.

## Work ownership

Choose owners during the first meeting. Ownership is for development and review, not a division into student roles.

| Workstream | Owner | Reviewer | First deliverable |
| --- | --- | --- | --- |
| Learning outcomes and assignment structure |  |  | Question map |
| Route and infrastructure verification |  |  | Completed route table |
| Rolling stock and service definitions |  |  | Four-service reference set |
| Timetable and analysis questions |  |  | Solved question prototype |
| Scene packaging and clean-run test |  |  | Tested assignment scene |
| Student handout and model answers |  |  | Reviewable draft |

## First meeting agenda

Use the first 60 minutes to leave with decisions and named work, not to perform the assignment.

| Time | Discussion | Output |
| --- | --- | --- |
| 0 to 10 minutes | Intended course role and learning outcomes | Agreed audience and outcomes |
| 10 to 20 minutes | OpenTrack question sequence and professor's comments | Initial keep, adapt, or omit decisions |
| 20 to 30 minutes | Current Netherlands scene, supplied trains, and route candidates | Confirmed starting material and unknowns |
| 30 to 40 minutes | Proposed Amsterdam to Hilversum service pattern | Reference service target |
| 40 to 50 minutes | Workstreams, evidence, and review method | Owners and reviewers |
| 50 to 60 minutes | First development cycle | Deliverables and next review date |

The meeting is complete when the team has an agreed assignment aim, a first route test for each technical workstream, one owner for the question map, and a date to review the first working service.

## Release criteria for the assignment draft

- One Sprinter and one Intercity run in each direction on verified routes.
- The planned times come from recorded unhindered runs and stated supplements.
- Every assignment question has been solved from a clean copy by a co-author.
- The model answers explain the method, assumptions, and accepted result range.
- The smaller scene validates, exports, and runs without unresolved references.
- The full Netherlands scene remains available as the reference case.
- The handout distinguishes student instructions from notes for instructors and maintainers.
