# Single-track locking

A single-track restriction of a scene keeps trains of opposite directions out of
a stretch of track at the same time. The scene format is described in
[Single-track restrictions](../guides/scene-schema.md#single-track-restrictions).
This page describes how a run decides, step by step, which train may enter.

The code is in `simulation/Signalling.cpp` (`singleTrackZone`,
`occupySingleTrackForRoute`, `Apply_Single_Track_Authorities_Mixed_Signalling`)
and `simulation/RollingStock.cpp` (`updateSingleTrackLocks`). The state is in
`RunInfrastructure`: `singleTrackHeld`, `singleTrackReservations`,
`singleTrackClosures` and `singleTrackKeptOut`.

## Zones

`singleTrackZone` derives, once per run, where the zone of each restriction lies
on each route: its sections, the index of its first and last section, and its
intervals of route position. It also derives the way out of the zone between
every pair of routes of opposite directions, described [below](#ways-out), and
for each route the zone that the route passes next (`SingleTrackZone::next`).

Sections that share track count as one. A switch section shares track with the
plain block under it, which `Section::IDConnectedBS` records in one direction
only: the switch `@32-B0@-9.539000/@0-B4@-9.549000` lies over `@32-B0@`. The
zone of a restriction holds every such section in either direction, so a
restriction that ends on `31-B0`, next to the switch over `32-B0`, still takes
in `32-B0` and the switch, and a route that runs only over the switch passes the
zone too. The ways out compare sections in the same way.

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

Of the trains of one direction on one track before a zone, only the first asks
for it. A train whose way to the zone entry passes the head of another train of
its direction, which has the zone on its route and has not entered it yet,
cannot enter before that train. It does not ask, and a reservation that it had
lapses. It asks again once the train ahead is in the zone or has left its way.
The train ahead may dwell at a station in front of the zone, so that it does not
ask either; the follower then still does not take the zone from it.

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
direction blocks its way out, and when it has room after the zone (see
[Room after the zone](#room-after-the-zone)). A train of route q blocks the way
out of route r when:

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

## Room after the zone

The ways out keep a train of the other direction off the track after a zone.
Trains of the same direction need a second rule. A train that waits at a loop
for the next zone holds the loop track. If more trains of its direction enter
the zone behind it, the last of them stands in the zone and keeps it held for
its direction. Trains of the other direction then cannot leave the next zone
toward that loop, and the lines on both sides of the station lock up.

### The stretches after a zone

The stretch after a zone on a route is the track between the zone and the zone
that the route passes next (`SingleTrackZone::next`). A train is bound to wait
on that stretch when its route passes it, it does not hold the next zone, and it
is between the entry of the requested zone and the end of the stretch, or holds
the requested zone. A train holds a zone when a part of it is inside or when it
keeps a reservation. A train that dwells at a service stop keeps no
reservation.

The bound trains are queued on the stretch from the entry of the next zone, each
in front of the block under the tail of the train ahead, as fixed blocks hold
them. There is room for the requesting train when it fits behind them clear of
the zone. A stretch that no train is bound for has room: a train alone may still
wait inside the zone for the next one, which the ways out keep free of the other
direction.

When a route of the other direction uses part of the stretch after the next
zone, waiting there would block that route, so the check goes on to the stretch
after the next zone, and so on. It stops at the first stretch that no such route
uses, or at the last zone of the route.

### The rule

A new request of a zone is refused when the requesting train has no room on one
of these stretches (`roomAfter` in `updateSingleTrackLocks`). Requests are
checked in order, and a train that a new request gave the zone earlier in the
step counts as bound. A train that keeps a reservation is never refused. The
refusal closes the route of the train in front of the first way out of the zone,
for a route of the other direction, that the train has not reached yet, or else
at the zone entry. A refused train does not count as running toward a way out
behind the zone. The closure is skipped while a train of the same route that
holds the zone stands between the refused train and the closed section, because
the closure would stop that train too and the refused one cannot pass it.

Because only the first train on a track asks, the trains of a direction get the
zone, and the room after it, in the order in which they reach it. A train that
holds the zone and has still to enter it is never behind a refused train on the
same track, so the closure for the refused train never stops it.

A train whose route does not pass the requested zone but joins the stretch after
it is bound for the stretch once its head is on it.

### Entering the network

A train that is due at its entry and would find no room after its first zone
does not enter the network (`RunInfrastructure::singleTrackKeptOut`,
`Train::checkEntrance`). Otherwise it would wait on its first platform, which at
a terminal is the way out of that zone for the trains that arrive there. Trains
in the network that have still to reach that zone have the room first, then the
waiting trains in the order in which they are due, the order in which
`Train::checkEntrance` lets them in. A train that is let in keeps the room while
it cannot enter yet, so a train due later does not overtake it. A train that is
kept out asks for no zone and keeps no reservation, so it does not hold a zone
that a train in the network needs.

### The invariant

A train enters a zone only when, for every stretch after it up to a place where
it blocks no train of the other direction, the trains of its direction that are
bound for that stretch leave it room there clear of the zones. No train holds a
zone that it cannot enter: a train behind another one on its way to the zone
does not ask for it, and a train kept out of the network keeps no reservation.
So every train in a zone can leave it, and every zone held by a direction is
released again.

### Cases

- The eight trains of Paimpol, twice each, with long dwells: trains of both
  directions wait at Pontrieaux, and trains that are due while the line ahead is
  full wait to enter. All sixteen reach the end of their routes
  (`paimpolLongDwellsTwiceCrossOnSingleTrack` in `tests/test_simulationrun.cpp`).
- The Paimpol restrictions one block shorter at each end: the zones still take
  in the blocks under the switch sections at their ends
  (`paimpolNarrowRestrictionsCrossOnSingleTrack`).
- A follower behind a train that dwells in front of a zone, a follower that asks
  in the same step as the train ahead, a holder that has passed the way out with
  a train behind it, and a train kept out of the network next to a train that
  has still to reach the zone (`singleTrackRoomOrder` in
  `tests/test_operationsbuilder.cpp`).

## Limits

The rule keeps each train out of a zone until there is room for it, but it does
not plan ahead: when the room is short, trains cross in the order in which they
ask, not in the order of the timetable, and a timetable that sends more trains
than the line can hold waits longer than needed. A way out lasts as long as the
two routes share track, so where both directions use the same track through a
station, trains cross only at a station where they use different tracks.
Closures act on a whole route, so a refused train holds back the trains of its
route behind it too. A train counts as ahead of another only when its head is on
a section of the other train's route; a train on a section that only shares
track with it, such as a switch section over a plain block, does not count.
