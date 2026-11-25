# Single-track locking

A single-track restriction of a scene keeps trains of opposite directions out of
a stretch of track at the same time. The scene format is described in
[Single-track restrictions](../guides/scene-schema.md#single-track-restrictions).
This page describes how a run decides, step by step, which train may enter.

The code is in `simulation/Signalling.cpp` (`singleTrackZone`,
`occupySingleTrackForRoute`, `Apply_Single_Track_Authorities_Mixed_Signalling`)
and `simulation/RollingStock.cpp` (`updateSingleTrackLocks`). The state is in
`RunInfrastructure`: `singleTrackHeld`, `singleTrackReservations` and
`singleTrackClosures`.

## Zones

`singleTrackZone` derives, once per run, where the zone of each restriction lies
on each route: its sections, the index of its first and last section, and its
intervals of route position. It also derives the way out of the zone between
every pair of routes of opposite directions, described [below](#ways-out).

## The lock in a step

`Occupy_Block_Sections_Of_Route` calls `updateSingleTrackLocks` after the trains
have moved. For each zone it decides the direction that holds it:

1. A train with a part inside the zone holds it for its direction. When trains of
   both directions are inside, the previous holder keeps priority.
2. Otherwise a reservation that a train already has decides.
3. Otherwise a new request decides. Of simultaneous requests on a free zone,
   the forward direction wins.

A train asks for the zone when its next movement could take away its last
chance to brake for the zone entry (`Train::needsSingleTrackReservation`). A
train that is due at its entry asks too. A granted request is kept until the
train has passed the zone, has ended its run, or has a changed route, destination
or entry.

Every route of the other direction then treats the sections of the zone as
occupied (`occupySingleTrackForRoute`) and, at levels 0 to 5, gets an end of
authority in front of the zone that only trains of its direction obey.

## Service stops

A train that stands at a service stop does not need the zone until it is ready
to leave. While its dwell is not over or its planned departure has not come
(`Train::dwellsAtServiceStop`), it does not ask for the zone and does not keep
a reservation. A train of the other direction can cross the zone during the
dwell. When the train is ready to leave it asks again. If the zone is held the
other way by then, it waits at the platform.

A train that brakes for a platform just before the zone can still reserve the
zone on the way in. It loses the reservation once it stands at the platform.

Other stops keep the reservation, for example a stop at a red signal right
before or inside the zone, or a breakdown. Such a train leaves as soon as the
cause is gone, so it still needs the zone. If it gave up its reservation, trains
that asked later could take its turn.

## Ways out

A train that enters a zone has to leave it on the other side. The track right
after the zone is often the track where a train of the other direction waits for
the same zone, for example the main track of a station with a loop. If that
train stands there, the two trains meet head to head and neither can move.

### The way out of a zone

Take a zone, a route r and a route q of the other direction. The way out of the
zone for r toward q is the part of q just before the zone whose sections r uses
after it leaves the zone, or which belong to a further zone that r passes after
this one. `SingleTrackZone::waitBefore` gives, on q, the index of the first
section of that stretch. When r and q use different tracks right before the zone
on q, as at a station where they take different loop tracks, the stretch is
empty and `waitBefore` is the zone entry. `SingleTrackZone::exitUntil` gives,
on r, the last section after the zone that lies in the stretch.

### The rule

A train gets a new reservation of a zone only when no train of the other
direction blocks its way out. A train of route q blocks the way out of route r
when:

- it stands on the way out, while it moves or waits at a stop;
- it runs toward the way out and can no longer stop in front of it; or
- it holds a reservation of a further zone that overlaps the way out.

The first two apply only to a train in the network. A train that waits to enter
the network does not block by its position: `Train::checkEntrance` keeps it out
while the way of its route is closed in front of its entry (see below). A train
that dwells at a service stop does not count as running toward the way
out. Neither does a train that will be stopped in front of the way out anyway,
because the way is closed in front of it (see below). Trains that stand on a way
out are taken first, then forward trains that run toward one, then reversed
trains. Of two trains that run toward each other's way out, the forward one
therefore gets the zone and the reversed one stops in front of the way out.

The rule applies to the first train into a free zone as well as to a follower
of the holder. Forward wins simultaneous requests only among the requests that
are not refused. If a forward train would enter toward the track where a
reversed train waits, the reversed train gets the zone.

### Where the trains wait

The way of a route is closed at the first section of a way out
(`RunInfrastructure::singleTrackClosures`):

- every train that holds a zone, inside it or by reservation, closes the way of
  each route of the other direction in front of its own way out;
- a train that has left a zone keeps that closure until its tail has passed its
  way out (`exitUntil`);
- a train that blocks the way out of a route closes the way of that route in
  front of its own way out, and that route also cannot enter the zone.

`occupySingleTrackForRoute` treats the first section of the closed stretch and
the first section of the zone as occupied for that route only, and
`Apply_Single_Track_Authorities_Mixed_Signalling` gives that route an end of
authority in front of both (`MovementAuthority::routeIndex`). Trains of other
routes of the same direction, such as the holder's own route, ignore it. When a
closure moves or lifts, `updateSingleTrackLocks` releases both sections, because
levels 0 to 2 rewrite only the aspects of released sections; otherwise the zone
entry would keep a stop aspect after the way is open. A train
that waits to enter the network does not start on a closed stretch
(`Train::checkEntrance`). At a fixed-block level the section in front of a closed
stretch shows a stop aspect, so a train due to enter there waits until the way
is open.

So a train held back waits in front of the stretch that the other train needs,
not on it. When that stretch reaches back to the start of its route, as on a
terminal platform that both directions use, the train does not enter the network
until the way is open. If a train already stands on such a stretch, it blocks
the other train, which then does not enter the zone.

### Cases

- Two zones with a short station between them: the way out of the first zone
  includes the second zone when the route passes it. A train does not enter the
  first zone while a train of the other direction that will need the track
  between them is in the second zone or has reserved it.
- A train of the other direction that still approaches its waiting place blocks
  only once it can no longer stop in front of the way out. Before that, the
  requesting train gets the zone, and its closure stops the other train in front
  of its way out.
- A through line without a loop: two trains of opposite directions in front of
  the zone each stand on the other's way out, so neither enters
  (`running-through-level-*` in the
  [characterization tests](../development/characterization-tests.md)).

### Limits

The rule looks only at trains of the other direction. A train can still enter a
zone toward a track that a train of its own direction occupies and cannot leave,
for example when that train waits for the next zone. A way out lasts as long as
the two routes share track, so where both directions use the same track through
a station, trains cross only at a station where they use different tracks.
