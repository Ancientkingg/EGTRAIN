# Signalling levels

This page says what the simulation does at each signalling level, how a
signalling area gives a section its level, and how signalling areas differ from
block sections and from the areas of the capacity analysis. It describes the
code, not a real railway. The names of the levels are labels for what the model
does. Nothing here is checked against a source about a real system; see
[What is not established](#what-is-not-established).

Function names are given so that a maintainer can find them. Test cases are named
in code font. They are the characterization cases in
`EGTRAIN/QEGTRAIN/tests/characterization`; see
[Characterization tests](../development/build-and-test.md#characterization-tests).

## What a signalling level is

- A signalling level is a whole number from 0 to 5 that belongs to a runtime
  section (`Section::SignallingLevel`). A section is a block of one track, or a
  section that a connection builds from two blocks. See
  [Signalling areas, block sections and capacity areas](#signalling-areas-block-sections-and-capacity-areas).
- A section can also have no level. Its value is -99999999
  (`kSignallingLevelUnset`). This page calls it "no level".
- The scene stores no level on a block. It stores signalling areas in
  `signalling.json`. When the scene is prepared for a run, the builder gives
  each section the level of the area that covers it; see
  [How an area gives sections their level](#how-an-area-gives-sections-their-level).
- A route holds copies of its sections. The copies carry the level of the
  section. A route that runs in the opposite direction has its own copies with
  the same level.
- The word "level" also appears in `views.json`, for the display level of a
  track in the network view. That has nothing to do with the signalling level.

The labels are defined in `scene/SignallingLevelNames.h`. The validation message
about areas that give one section different levels shows them. The editor shows
the number.

| Value | Label | One line from the program |
| --- | --- | --- |
| none | `No signalling` | Trains in this area are not separated. |
| 0 | `0 ATB fixed block` | Fixed block; the block before an occupied one is limited to 40 km/h. |
| 1 | `1 ETCS Level 1 fixed block` | Fixed block; moves trains like level 2; no blocking times. |
| 2 | `2 ETCS Level 2 fixed block` | Fixed block; moves trains like level 1; blocking times computed. |
| 3 | `3 ETCS Level 3 moving block` | Movement authority ends 50 m behind the train ahead. |
| 4 | `4 Virtual coupling` | Moving block; a train can follow the train ahead at its speed. |
| 5 | `5 BACC track circuits` | Fixed block; one extra empty block (double red) before an occupied one. |

### What happens in a step

The order is fixed in `DispatchController`.

1. Every train moves. It reads the codes and speed limits that the previous
   step wrote.
2. `Occupy_Block_Sections_Of_Route` lists the sections that each train covers
   from its tail to its head (`BlocksOccupied`). It also decides which
   direction holds each single-track section.
3. `Apply_Signal_Failures_Mixed_Signalling` adds the failed sections to the
   occupied sections and makes their ends of authority.
4. `ReportAllTrainPositionsToRBC` makes the ends of authority of levels 3 and 4,
   with a margin of 50 m (see level 3 for the start of a section).
5. `protectStationAreas` can mark the entrance section of a configured station
   boundary as occupied.
6. `releaseMixedSignallingSystem` resets the sections that trains have left.
7. `activateMixedSignallingSystem` goes through every route copy. It adds the
   single-track sections that the copy has to treat as occupied, and then runs
   the routine of each level. A routine writes only on sections of its own
   level, with the exceptions in
   [Sections of different levels on one route](#sections-of-different-levels-on-one-route).
8. The simulation hands the codes to the window.

The signalling delay `S_delay` is 0 and nothing else sets it, so the signalling
uses the train positions of the current step.

## Codes and what they do to a following train

A section has a code, a state and, on each of its arcs, a signal speed limit.
The routines write these values. The numbers are the values the program uses
internally.

| Code | State | Written | Signal head in the window |
| --- | --- | --- | --- |
| 270 | green | at the start, when a section is released, and as the last code of a chain | green |
| 180 | yellow | two sections behind an occupied section (levels 0, 1, 2); three behind at level 5 | green |
| 75 | red | one section behind an occupied section (levels 0, 1, 2); two behind at level 5 | yellow with a dark dot |
| 751 | red_red | one section behind an occupied section, level 5 only | red with a dark bar |
| 0 | keeps its state | on an occupied or failed section | red with a dark bar |

A head shows the code of the section ahead of it. If route copies of one
section disagree, it shows the most restrictive code, in the order 0, 751, 75,
180, 270. A copy without a level never replaces one with a level. The snapshot
also carries the level and a failed flag for each signal
(`app/GuiSimulationSnapshot.h`). A head whose section has no level, and a head
that no route passes in its direction, is an empty grey ring. The head of a
failed signal is a red lamp with a white cross for as long as the failure
lasts. The marks are drawn when a head is at least 6 pixels wide.

A code changes what a following train does in four ways.

1. **Speed limit.** The limit in force is the lowest of the maximum speed of the
   train, the speed limit of its arc and the signal speed limit of its arc.
   A limit of 0 holds a train that stands.
2. **Speed at the end of a section.** After the aspects are written, a speed
   pass of the level sets the speed at the end of each section of that level.
   It is 0 when the next section has code 0. Otherwise it is the lower of the
   speed limit and the signal speed limit of the first arc of the next section.
   A train looks at its own section and the eight after it
   (`europeanVitalComputerWithListsImproved`) and brakes along a braking curve
   so that it has the end speed at the end of each of them. A train that is
   closer to a target than its braking distance cannot follow the curve. It
   brakes with full force from where it is, and its position never goes back.
3. **Hold at a red state.** A train that stands within 4 m of the end of an arc
   waits there while the state of its section is `red`. A train that has
   finished its dwell at a platform at the end of a block waits in the same way.
4. **Entry.** A train enters the route when the code of the first section is 270
   or 180. This rule applies at the levels other than 3 and 4.

The signal speed limit is 999, which means none, unless a routine writes one.
Levels 0 and 5 write 11.111 m/s, about 40 km/h, on the section with code 75.
Level 5 writes 0 on the section with code 751.

`releaseBlocksMixedSignalling` clears the codes. It runs for the sections that
trains have left, and in each step for the section before each occupied section.
For each of them it sets the code of the section and of the four sections before
it to 270 and removes their signal speed limits. It sets the state of the four
sections before it to green. Fewer sections are cleared at the start of a route.
It does not depend on the level.
When a train leaves the last section of its route, `relLastSectionMixedSignalling`
hands that section to the release function. It also sets the code of that
section and of the two before it to 270 (three before it at level 5), according
to the level of the last section. Levels 3 and 4 and sections without a level get
nothing more. Level 5 also sets all of its sections to clear at the start of
every step and writes the aspects again (`baccMixedSignalling`).

## Common behaviour

**Platforms.** At every level, including none, a train stops at the platform of a
station it serves. It leaves after the dwell time. A platform at the end of a
block adds a condition: the train waits while the state of that block is `red`.
A train that is stopped for an end of authority also waits.

**Route entry.** A train enters when its departure time has come and the entry
rule of its level holds (code 270 or 180 at levels none, 0, 1, 2 and 5; no
foreign end of authority at the start of the first section at levels 3 and 4).
Where the first section has a level, only one train enters a route per step: the
train that enters puts the first section in `BlocksOccupied` at once, and the
trains that wait are visited in the order in which they are due. A train waits
while the first section is failed, at every level (`checkEntrance`). Cases:
`same-entry-level-*`, `entry-order-level-0`, `-1` and `-2`, and
`sf-first-level-0`, `-1`, `-2` and `-none`.

**Signal failure.** A `signal_failure` incident fails one or more sections from
its start second to its end second. A scene has to give the end second:
validation rejects a signal failure without one (`scene.incident.window`). In
each step:

- The failed section is added to `BlocksOccupied`. The routines of levels 0, 1,
  2 and 5 then write the same aspects behind it as behind a train, and levels 3
  and 4 give it code 0.
- For every route that contains the section, an end of authority is made
  at the end of the section before it. If the failed section is the first of
  the route, it is made at its start. The authority has the direction of its
  route, and a train obeys only the authorities of its own direction.
- The authority is a stop target at every level, including none.
- A train that waits to enter does not enter while the first section of its route
  is failed.
- A train that is already inside the section when the failure starts is not
  stopped: the authority lies behind it (`sf-entered-level-none`).
- A train that is closer to the authority than its braking distance when the
  failure starts brakes with full force until it passes the authority. The
  authority then lies behind it as for a train inside the section, so the train
  accelerates again and runs through the failed section (`sf-late-level-*`).
  The same holds when a second failure turns a speed target into a stop at the
  point the train is about to reach (`sf-retarget-level-0`).
- On the first step after the incident has ended, the failed sections are handed
  to the release function.
- The heads of a failed section show the failure, and its track is drawn as
  blocked.

Cases: `sf-forward-level-*` and `sf-reverse-level-*` for the levels 0 to 5 and
none; `sf-adjacent-level-*`, `sf-staggered-level-*`, `sf-last-level-*` and
`sf-first-level-*` for the levels 0, 1, 2 and none; `sf-entered-level-none`;
`sf-late-level-none`, `-0` and `-3`; `sf-retarget-level-0`.

**Single-track restrictions.** While a train of one direction is inside a
single-track stretch, the sections of the stretch count as occupied for the route
copies of the other direction (`updateSingleTrackLocks`,
`occupySingleTrackForRoute`). The stretch is described in
[Single-track restrictions](scene-schema.md#single-track-restrictions). What this
does to a train depends on the level, as the sections below say. Cases:
`single-track-level-*`.

## Levels

| Level | Train separation | Stops before an occupied section | Blocking times | Single-track restriction |
| --- | --- | --- | --- | --- |
| none | none | no | none | no effect |
| 0 | fixed block | at the end of the block before it | yes | yes |
| 1 | fixed block | at the end of the block before it | none | yes |
| 2 | fixed block | at the end of the block before it | yes | yes |
| 3 | moving block | 50 m behind its tail | yes, per infrastructure element | yes |
| 4 | moving block, following mode | 50 m behind its tail, or at the speed of the train ahead | yes, per infrastructure element | yes |
| 5 | fixed block, one block more | at the end of the second block before it | none | yes |

The cases below use the characterization line. It is one track of 16 km in eight
blocks of 2 km, with a station at 8 km and a train length of 70 m. `F1` runs
ahead of `F2` and stands at the station at 8 km while `F2` arrives
(`follow-level-*`).

| Level | Where `F2` stops behind `F1` | Smallest gap |
| --- | --- | --- |
| none | 8000 m, at the position of `F1` | -70 m |
| 0, 1 and 2 | 6000 m, the end of the block before the one that holds `F1` | 1930 m |
| 3 and 4 | 7880 m, 50 m behind the tail of `F1` | 50 m |
| 5 | 4000 m, the end of the second block before the one that holds `F1` | 3930 m |

### No level (`No signalling`)

- **Separation.** None. The routines of levels 0, 3, 4 and 5 write nothing on a
  section without a level. A code of 270 stays on the section, so a train
  enters and runs without regard to other trains. `follow-level-none` and
  `same-entry-level-none` show two trains at one position (gap -70 m).
- **Codes.** Only a train on a section of level 1 or 2 ahead of the section can
  write a code onto it; see
  [Sections of different levels](#sections-of-different-levels-on-one-route).
- **Platform and route entry.** As in
  [Common behaviour](#common-behaviour). The rule of one train per step does not
  apply where the first section has no level.
- **Signal failure.** The authority at the end of the section before the failed
  one is the only effect. The train brakes to a stop there; no code or signal
  speed limit changes (`sf-forward-level-none`: `F1` and `F2` stop at 10000 m).
- **Blocking times.** None (`blocktime n=0`).
- **Single-track restriction.** No effect. Validation warns with
  `scene.single_track.no_effect`.
- **Window.** The head shows green (code 270). A green head on a section
  without a level does not mean that the line is clear; nothing separates trains.
- Validation warns about route sections without a level with
  `scene.signalling.level.missing`.

### Level 0 (`0 ATB fixed block`)

- **Separation.** Fixed blocks. A train stops at the end of the block before the
  block that is occupied (`atbMixedSignalling`, `setBlockSpeedAtbMixedSignalling`).
- **Codes behind an occupied section.** The occupied section gets code 0. The
  section behind it gets code 75, state red and a signal speed limit of 11.111 m/s.
  The next gets 180, state yellow. The next gets 270, state green. A train
  therefore slows to 11.111 m/s at the entry of the block behind the occupied
  one, and stops at its end. The yellow section has no limit of its own; its end
  speed is 11.111 m/s because the next section has that limit.
  `follow-level-0`: `F2` runs at 11.111 m/s before it stops at 6000 m.
- **Platform and route entry.** As in
  [Common behaviour](#common-behaviour).
- **Signal failure.** The chain of aspects is written behind the failed section.
  In `sf-forward-level-0`, `F1` slows to 11.111 m/s and stops at 10000 m from
  854 s to 1002 s.
- **Blocking times.** Computed ("Conventional"). The approach of a section starts
  when the head of the train enters the section before it
  (`ComputeBlockingTimeForSingleLocation`).
- **Single-track restriction.** Has an effect. In `single-track-level-0`, `R1`
  waits 41 s in front of the stretch.
- **Window.** The heads show red, yellow, green, green behind an occupied
  section.

### Levels 1 and 2 (`1 ETCS Level 1 fixed block`, `2 ETCS Level 2 fixed block`)

The routines of levels 1 and 2 are the same code with the level number changed:
`etcsLev1MixedSignalling` and `etcsLev2MixedSignalling`, their speed passes and
their release functions. Trains move the same
way at both levels. The levels differ in the blocking times only.

- **Separation.** Fixed blocks. A train stops at the end of the block before the
  occupied one, as at level 0, with no speed limit.
- **Codes behind an occupied section.** Code 0 on the occupied section, 75 and
  state red on the section behind it, 180 and state yellow on the next, then 270
  and state green. No signal speed limit is written, so the codes 180 and 270
  change nothing for the train; the speed at the end of the section behind the
  occupied one is 0.
- **Platform and route entry.** As in
  [Common behaviour](#common-behaviour).
- **Signal failure.** As at level 0, without the speed limit. `F1` stops at
  10000 m in `sf-forward-level-1` and `sf-forward-level-2`.
- **Blocking times.** Level 1: none. Level 2: computed ("ETCS2"). The approach
  of a section starts when the position of the train plus `v^2 / (2 a)` reaches
  the start of the section, where `a` is the maximum deceleration.
- **Single-track restriction.** Has an effect. In `single-track-level-1` and
  `single-track-level-2`, `R1` waits 161 s.
- **Window.** Red, yellow, green, green, as at level 0.
- **Difference from level 0.** Levels 1 and 2 write no 11.111 m/s limit. They
  also test the level of the occupied section instead of the level of the
  section they write (see
  [Sections of different levels](#sections-of-different-levels-on-one-route)).
  In both routines, the loop that follows the write of code 270 on the third
  section behind an occupied section clears the signal speed limit of the second
  section behind it, not of the third. With one level on the line this changes
  nothing, because every limit is 999. No issue tracks it.

### Level 3 (`3 ETCS Level 3 moving block`)

- **Separation.** Moving block. For every train, the program makes an end of
  authority 50 m behind its tail and one 50 m ahead of its head, each on the
  section of level 3 or 4 that holds that point (`ReportPositionToRBC`). A train
  looks at the authorities of other trains on its own section and the eight after
  it, where those have level 3 or 4. While the tail has been inside its section
  for less than 50 m, the authority is at the start of that section instead. The
  authority is a stop target (speed 0).
  The train brakes along its braking curve and stops before it. `follow-level-3`:
  `F2` stops at 7880 m, 50 m behind the tail of `F1`, which stands at 8000 m.
- **Codes.** `rbcSendsMasToRouteMixedSignalling` gives code 0 to an occupied
  section of level 3 or 4 and writes nothing else. The section shows only 0 and
  270, so the window shows red or green. The end speed of a section is 0 only if
  the next section has code 0 and is not of level 3 or 4.
- **Platform and route entry.** A train enters when no other train has an
  authority at the start node of the first section. The routines of this level
  never set the state `red`, so at a platform at a block end only an authority
  holds a train.
- **Signal failure.** The failed section gets code 0 and the authority is the
  stop target. `F1` stops at 10000 m in `sf-forward-level-3`; `F2` stops at
  9880 m, 50 m behind the tail of `F1`.
- **Blocking times.** Computed, but as records for the infrastructure elements
  of the route, not for the sections
  (`ComputeBlockTime_ETCSLevel_3_ForSection`). `follow-level-3` lists none
  (`blocktime n=0`).
- **Single-track restriction.** Has an effect. A train of level 3 or 4 does not
  follow aspects, so every route against the holder gets an end of authority at
  the end of the section before the stretch, for as long as the stretch is held
  (`Apply_Single_Track_Authorities_Mixed_Signalling`). In `single-track-level-3`,
  `R1` waits 161 s. A train that follows the holder is not delayed
  (`single-track-follow-level-3`).
- **Window.** Red on an occupied section, otherwise green.

### Level 4 (`4 Virtual coupling`)

Level 4 uses the same routines as level 3 for codes, authorities, entry and
failures. It adds a following mode.

- **Separation.** The end of authority of a train that goes the same way and
  has the same next section, or is at the end of its route, has the speed of
  that train as its target, not 0. For a train end authority that does not meet
  these conditions the target is 0, as at level 3. The authority of an occupied
  diverging switch is not described here. A train that is faster than the train
  ahead moves its braking point and can couple. It couples when its speed differs by less than
  0.278 m/s from that of the train ahead and it is within 30 m of the authority.
  `follow-level-4`: `F2` stops 50 m behind the tail of `F1`, which stands at
  8000 m.
- **Following mode.** A coupled train takes the speed and acceleration of the
  leader. It stops following the leader (unintentional decoupling) when it falls
  more than 30 m behind the authority, and it can couple again. It leaves the
  mode when the routes diverge, or when the leader reports no authority any more.
  The characterization cases do not record whether a train enters the following
  mode.
- **Blocking times.** Computed per infrastructure element, as at level 3
  (`ComputeBlockTime_ETCSLevel4_ForSection_MaxCapacity_Improved`).
  `follow-level-4` lists none.
- **Single-track restriction.** Has an effect, as at level 3
  (`single-track-level-4`, `single-track-follow-level-4`).
- **A leader that slows down.** A train that is not coupled yet treats a leader
  that is slowing down as a leader that stops. The end of authority moves to
  where the leader comes to a stand, estimated with the braking curve of the
  follower from the speed of the leader, and its target speed is 0. In
  `same-entry-level-4` the second train stops 50 m behind the rear of the first
  at the platform and moves up when the first has left.

### Level 5 (`5 BACC track circuits`)

- **Separation.** Fixed blocks, like level 0, with one more block held free
  (`baccMixedSignalling`, `setBlockSpeed1MixedSignalling`).
- **Codes behind an occupied section.** Code 0 on the occupied section. The
  section behind gets 751, state red_red and a signal speed limit of 0, so a
  train cannot move inside it. The next gets 75, state red and 11.111 m/s. The
  next gets 180 and the next 270. A train stops at the end of the block that is
  second behind the occupied one: in `follow-level-5`, `F2` stops at 4000 m,
  one block before the stop of level 0. Only the train order code reads the state
  red_red, and native scenes do not use it; the hold comes from the limit of 0.
- **Differences from level 0.** One block more is held free; the chain is one
  code longer; and all level 5 sections are set to clear at the start of every
  step, where levels 0, 1 and 2 clear a section when a train leaves it.
- **Platform and route entry.** As in
  [Common behaviour](#common-behaviour).
- **Signal failure.** The chain is written behind the failed section. In
  `sf-forward-level-5`, `F1` stands at the platform at 8 km from 331 s to 1002 s.
  After its dwell (661 s in `follow-level-5`) it is held by the red state of that
  block, not by its timetable.
- **Blocking times.** None.
- **Single-track restriction.** Has an effect. In `single-track-level-5`, `R1`
  waits 129 s.
- **Window.** The head of the section behind the occupied one is grey (code
  751), then yellow, green, green.

## Sections of different levels on one route

A route can cross sections of several levels. This is what the code does at the
border. The cases `border-*` pin it on the characterization line for a border at
8 km between levels 0 and 2, 2 and 0, 0 and 3, and 0 and 1. From level 0 into
level 1 or 2 the block before the border is not protected while a train is in it
(#602).

- Which sections a routine writes on:
  - Levels 0 and 5 test the level of the section they write. They write the
    chain behind an occupied section when the sections behind have their level,
    whatever the level of the occupied section. They set code 0 on the occupied
    section only when it has their level.
  - Levels 1 and 2 test the level of the occupied section. The exception is
    the code 75 on the first section of a route when the second is occupied; it
    tests the level of the first section. An occupied section of level 1 or 2
    writes the chain on the sections behind it whatever their level, including
    no level.
  - Levels 3 and 4 write code 0 on an occupied section of their level and
    nothing else.
- A speed pass sets the end speed of the sections of its own level. It reads
  the code and signal speed limit of the next section whatever the level of the
  next section. The pass of levels 3 and 4 sets 0 only when the next section has
  code 0 and is not of level 3 or 4. This is the one place that compares
  neighbouring levels (`manageEtcs3TransitionsToOtherSignalling`).
- An authority of level 3 or 4 is made only where its point lies in a section of
  level 3 or 4. The exception is a train whose tail is still before the start of
  its route, on a route that has at least one section of level 3 or 4: it gets
  an authority on the first section of the route whatever the level of that
  section (`ReportPositionToRBC`). A train of any level reads authorities on its own section and the
  eight after it where the section has level 3 or 4, or where a signal failure
  has made one.
- The entry rule is the rule of the level of the first section of the route.
- A release does not depend on the level.
- The blocking times of a section follow its own level.

## How an area gives sections their level

A signalling area is `id`, `start_km`, `end_km`, `level` and an optional `track`
(see the [scene schema](scene-schema.md)). The rule is in
`analyzeSignallingAreas` (`scene/SectionInventory.cpp`), which the validator
uses. The builder repeats it in `buildInfrastructureAndSignallingFromScene`
(`simulation/Signalling.cpp`); unit tests compare the two on small scenes.

- **Which sections.** The unit is a runtime section from the section inventory
  (`buildSceneSectionInventory`). The blocks of a track follow each other from the
  first node of the track. The last block is cut at the last node, or extended
  to it when the blocks are too short. A switch section is made for each
  connection and each pair of blocks that hold its two nodes. It runs from the
  start of the block that holds the node with the lower `x_km` to the end of
  the block that holds the other node, and it has two tracks.
- **Containment.** A section belongs to an area only when the whole section lies
  inside the area: `section.start >= area.start - 1e-8` and
  `section.end <= area.end + 1e-8`, in kilometres. The edges are inclusive. A
  tolerance of 1e-8 km is 0.01 mm. The numbers are compared with the
  `x_km` chainage of the track of the section. For a switch section, the start
  is on its first track and the end on its second.
- **Network-wide and track-scoped areas.** An area without `track` is
  network-wide. It is compared with every section, whatever its track. An area
  with a `track` applies to the sections that have that track as their first or
  second track. A track-scoped area wins over network-wide areas: if any
  track-scoped area contains the section, its level is used and the network-wide
  level is dropped.
- **Chainage.** The numbers are not converted between tracks. In the
  `Assignment_Gvc_Gdg_Ut` scene, track `B0` runs from 0 to 64 km and track `B1`
  from 100 to 164 km, so one network-wide area has to span 0 to 164 km or more
  to cover both, and it says nothing about the part from 64 to 100 km.
- **Conflict.** Two areas of the same kind (both network-wide, or both
  track-scoped for a track of the section) that contain the same section and
  have different levels conflict. Equal levels do not. A switch section has two
  tracks, so track-scoped areas of its two tracks with different levels conflict
  on it. A conflict is the error
  `scene.signalling_area.conflict`, reported once for each pair of areas, with
  the number of sections, the first of them and the labels of both levels. The
  builder reports `scene.native.signalling_area.conflict` and does not build.
  Two areas that overlap but contain no common section do not conflict.
- **A section that an edge cuts.** A section that crosses the edge of an area
  is not inside that area. If no other area contains the whole section, it has
  no level. This holds for the edge between two adjacent areas too: with areas
  0 to 2.5 km and 2.5 to 4 km, a section from 2 to 3 km has no level unless a
  third area contains it. Switch sections span two blocks, so an edge between
  two blocks can leave the switch section without a level while both blocks are
  covered. Validation warns with `scene.signalling_area.splits_section` when the
  cut section is on a route.
- **Areas with no section.** An area that contains no complete section changes
  nothing. Validation warns with `scene.signalling_area.empty`.
- **Routes used in reverse.** The level belongs to the section ID. A route in
  decreasing chainage is built from copies of the sections
  (`Route::createRouteFromBlockIds`, `Section::reverseBlockSection`), which copy
  the level. The area range is not mirrored; the same area covers a section
  whichever way a route runs.
- **Sections on no route.** They get a level, but no train uses them. The
  missing-level warning does not name them.

Checks on the areas themselves, in `scene/SceneValidator.cpp`:

| Code | Severity | When |
| --- | --- | --- |
| `scene.signalling_area.range` | error | `start_km` or `end_km` is not finite, or `start_km` is not below `end_km` |
| `scene.signalling_area.level` | error | the level is not from 0 to 5 |
| `scene.ref.unresolved` | error | the `track` does not exist |
| `scene.signalling_area.conflict` | error | see above |
| `scene.signalling_area.splits_section` | warning | an edge of the area lies inside a route section |
| `scene.signalling_area.empty` | warning | the area contains no complete section |
| `scene.signalling.level.missing` | warning | route sections have no level; the message names up to five sections and the tracks, and the suggested fix lists the stretches without a level when the scene has areas |
| `scene.single_track.no_effect` | warning | a block of a single-track restriction has no level |

Tests: `tests/test_scenebuilder.cpp` (`runAreaMappingChecks`),
`tests/test_scenevalidator.cpp`, `tests/test_scenebundle.cpp`.

## Signalling areas, block sections and capacity areas

Three things that are easy to confuse are all about stretches of track. They are
independent. Changing one does not change the others.

| | Signalling area | Physical block section | Capacity-analysis area |
| --- | --- | --- | --- |
| What it is | A range of chainage that gives sections a signalling level | A part of one track that a train occupies; the unit of the aspects | A stretch for which a capacity result is computed |
| Where it is stored | `signalling.json`, `signalling_areas[]` | `infrastructure.json`, `blocks[]`: `id`, `track` and `length_km`; blocks follow each other along the track. Connections derive switch sections | Not stored in the scene. There is no such object |
| Where it is computed | Builder, `buildInfrastructureAndSignallingFromScene` | `buildSceneSectionInventory`; the runtime sections are `signalling_block_sections` and the route copies | The capacity analysis dialog: route, first and last block, trains and period, from the blocking times of the run (`diagrams/CapacityAnalysis`) |
| What it decides | The level of every section that it contains | What a train occupies, and where an aspect is written | Which blocking times enter the capacity result |
| Depends on | The sections | The tracks and blocks | The blocking times, which are computed only at levels 0, 2, 3 and 4 |

Areas are measured against block sections, never the other way round. A
block section has the level of the area that contains it, or none. The capacity
analysis uses blocks that the user selects. Its scope does not follow the
signalling areas, and a signalling area does not follow the scope. A section
without a level or with level 1 or 5 has no blocking times, so the capacity
analysis has no data for it.

Older versions used one row of `TrackLines/AreasCaseStudy.txt` for both the
signalling level and the statistics of an area (the removed `NetworkArea`
class). No current code reads that file. The legacy export writes it
(see [Known limits](#known-limits-and-open-issues)).

What a capacity-analysis area is, and how it relates to the signalling areas, is
the subject of #444.

Signals and track detection sections are a fourth thing. `signals[]` bind to a
section only to resolve signal failures. See the
[scene schema](scene-schema.md) and the
[migration matrix](../guides/v1-scene-migration-matrix.md).

## The committed scenes

| Scene | Signalling areas | Runs with |
| --- | --- | --- |
| `Netherlands` | none | no level |
| `Assignment_Gvc_Gdg_Ut` | one network-wide area, 0 to 165 km, level 0 | `0 ATB fixed block` |
| `Copenhagen` | none | no level |
| `Milano_Brescia` | none | no level |
| `Paimpol` | one network-wide area, 0 to 38 km, level 0 | `0 ATB fixed block` |
| `Lebanon` | one network-wide area, 0 to 121 km, level 0 | `0 ATB fixed block` |

The scene descriptions of `Paimpol` and `Lebanon` call their area a placeholder.
The description of `Assignment_Gvc_Gdg_Ut` says that its level is a project
decision. Assigning levels to the other three scenes is #459.

## Known limits and open issues

Open on this version:

- #602: at a border from level 0 to level 1 or 2 the block before the border
  shows 75 while a train is in it (`border-0-1-fwd`, `border-0-2-fwd`,
  `border-2-0-rev`).
- #459: three committed scenes have no signalling area.
- #439 lists the preservation of areas through legacy export and import as an
  acceptance criterion. The legacy export writes `TrackLines/AreasCaseStudy.txt`
  with one row that covers the network at level 3 when the file is absent and
  the export has track line node data, whatever the signalling areas of the scene say. The legacy import does not
  read area files.

Limits that no issue tracks:

- The characterization cases do not record whether a train of level 4 enters the
  following mode.
- At level 4 a train that is not coupled treats every leader that slows down as
  a leader that stops, also one that only brakes for a speed restriction. A
  train that lost its coupling unintentionally still takes the speed of a
  braking leader as its target. No case covers either.
- The routines of levels 1 and 2 clear the wrong section after writing code 270
  (see levels 1 and 2). It has no effect with one level.
- A train that cannot stop before a stop target passes it. The target then lies
  behind the train, so nothing holds it in front of the failed or occupied
  section that the target protected, and the train can end inside a section
  that another train occupies. The model does not hold the train that takes the
  section.
- When several authorities compete at levels 3 and 4 or at a signal failure, the
  closest one is chosen with a braking distance whose formula subtracts the
  squared target speed only after dividing it (`V^2 - Vt^2 / (2 a)`). The effect
  is not measured.

## What is not established

The labels name what the model does. They are not claims that the model matches
the real system of the same name. Each of the following is not checked against a
source about a real system:

- whether level 0 matches any national system, and what the codes 270, 180, 75,
  751 and 0 and the limit of 11.111 m/s mean outside this program;
- whether the stop at the end of the block before an occupied block, with no
  overlap, matches a real rule;
- whether level 1 matches the system named in the comment of its routine, and
  whether level 2 can be modelled as fixed block with the aspects of level 1;
- whether the margin of 50 m, the release without delay, and the braking curve
  at every block end match a real system;
- whether level 3 matches a real moving block, and whether level 4 matches any
  real concept called virtual coupling; the distances of 30 m and 0.278 m/s are
  constants of the model;
- whether level 5 matches a real system, and its double red block;
- what a real traffic controller allows at a failed signal. The model lets no
  train enter a failed section until the failure ends.

Where the code does not settle a point, this page says so. It does not describe
how diverging switches are handled at levels 3 and 4
(`activateBlocksWithSwitchesDiv` and the fixed block routine next to it), and it
does not say what creates the infrastructure elements that levels 3 and 4 make
blocking times for; the characterization line has none.
