# Virtual coupling

At signalling level 4 a train may run closer to the train ahead than its braking
distance: it couples virtually to that train and follows it at its speed. This
page describes how a run decides when a train couples and decouples and what a
coupled pair does in a disruption. [Signalling levels](../guides/signalling-levels.md) describes the
levels for users.

The code is in `simulation/RollingStockOccupation.cpp`
(`Train::ReportPositionToRBC`), `simulation/Signalling.cpp` (`elaborateRbcMas`,
`elaborateMaOnBlockSectionsWithSwitchDiv`, `lockSwitchesOnAllConnectedSections`),
`simulation/RollingStockBraking.cpp` (`Train::closestEndOfAuthority`,
`Train::authorityTarget`) and `simulation/RollingStockTrajectory.cpp`
(`Train::updateVirtualCoupling`, `Train::leaveFollowingModeWhenLeaderIsGone`).
The ends of authority of a step are in `SimulationRun::etcsMa`.

## Ends of authority

At levels 3 and 4 every train reports in each step the position of its tail and
of its front, each with the safety margin (`end_of_authority_margin_m`, 50 m),
as ends of authority of the kind `TrainEnd` on the section where that end is.
Each end carries the speed, the acceleration, the section and the next section
of the train that reports it. A train reads the ends on its own section and the
sections after it that it can reach (`closestEndOfAuthority`). It reads only the
ends whose section has the ID of a section of its own route.

A train on a section with a diverging switch reports its ends on that section
and on the plain block before or after the switch that it is on
(`elaborateMaOnBlockSectionsWithSwitchDiv`). A train on a plain block reports
its ends on that block and on each diverging switch section that shares the
point of the block where the end is (`elaborateRbcMas`): the part of the block
before the switch for the first block of the switch section, and the part after
the switch for its second block. A train on a plain block and a train on a
switch section that shares it therefore see each other on the track they share.

A train that crosses the node of a diverging switch also locks the switch on
every switch section that uses it (`lockSwitchesOnAllConnectedSections`), with
ends of the kind `DivergingSwitchOccupied` at both edges of the switch, from the
step its front with the margin reaches the node until its tail with the margin
has passed it.

## Coupling and decoupling

A train couples to the train ahead when its predicted coupling point is within
the coupling distance (`virtual_coupling_distance_m`, 30 m) and its speed differs
from that of the train ahead by less than the speed tolerance
(`virtual_coupling_speed_tolerance_ms`), and both trains have the same next
section, or the train ahead is on the last section of its route
(`authorityTarget`). The window shows the notice **Virtually coupled**. A coupled
train keeps the margin behind the tail of its leader and follows its speed; when
the leader brakes, the follower keeps the braking distance to the point where
the leader would stop.

A coupled train that falls more than the coupling distance behind its leader
starts an unintentional decoupling and the window shows **Unintentional
decoupling**. It couples again, with a new **Virtually coupled**, once the
coupling conditions hold again. In the characterization runs this also happens
for about 2 s each time the leader's tail passes a block boundary.

A coupled train decouples for good when its leader reports no end of authority
on any section of the follower's route (`leaveFollowingModeWhenLeaderIsGone`):
the leader has left the network or has taken another way. The window shows
**Decoupled**.

## Disruptions

A coupled pair meets a disruption through its leader. The cases
`vc-breakdown-hold-level-4`, `vc-breakdown-slow-level-4` and
`vc-signal-failure-level-4` in the
[characterization tests](../development/characterization-tests.md) show the
behaviour below; the `-level-3` cases are the same runs at level 3.

- A breakdown that holds the leader: the leader brakes to a stop and the
  follower brakes with it, as for any braking leader. The follower stands the
  margin behind the leader's tail and stays coupled while both stand. When the
  leader sets off again the follower falls behind it while it starts, decouples
  unintentionally and couples again about 10 s later.
- A breakdown that caps the leader's speed (`reduced_speed_kmh`): the follower
  stays coupled and runs at the capped speed until the cap ends.
- A signal failure ahead: the failed section is a stop target for every train.
  The leader stops in front of it and the follower stands the margin behind the
  leader, as for a breakdown, and couples again after the restart.

At level 3 the follower keeps its braking distance to the leader's tail, so it
starts braking earlier and stands at the same point later. It never couples.
