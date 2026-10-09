# External state sharing (legacy)

With the options `-TSM` and `-RC`, EGTRAIN sends its state to an external module while a simulation runs. Both channels are output only. The receiving module is not part of this repository, and the code does not say what it expects, so this page does not guess. There is no input path: the only thing that comes back is a reply, which is printed and dropped. The messages carry no version and no schema, and no compatibility promise covers them. This is a legacy output channel and not a supported realtime interface.

This page describes the code as it is. Function names are given so that a maintainer can find them.

## Enablement

| Option | Channel | Endpoint |
| --- | --- | --- |
| `-TSM <int>` | traffic state | `tcp://127.0.0.1:5555` |
| `-RC <int>` | passenger route choice | `tcp://127.0.0.1:5556` |

- `parseCmdOptions` (`app/main.cpp`) reads both options with `std::atoi`. Any nonzero value switches the channel on. The default is 0.
- The options exist on the command line only. `--interactive` asks for both when the command line leaves them out (see [Command-line options](../guides/command-line.md)). The window has no control for them, but a window run sends when the option was given at the start. The provenance file of exported results records both values as `tsm_mode` and `route_choice_mode`.
- `-RC 1` needs a usable passenger journey in the prepared run. `DispatchController::prepareScene` rejects the run otherwise, with the diagnostic `scene.route_choice.passengers.none` (`test_route_choice_missing_data`).
- With both options at 0, no socket is created and nothing is bound or sent: `send_external_state` is called only from the two blocks of the step loop that these options guard. The namespace-scope `zmq::context_t` in `io/RailMLParser.cpp` is still constructed when the program starts, because `QEGTRAIN` links the library `egtrain_railml` that holds it.

## When it sends

`DispatchController::Train_Simulation_Mixed_Signalling_With_Passengers` (`app/DispatchController.cpp`) runs the step loop. In every step it sends one message per channel that is on. The messages come after train movement, after train and passenger interaction (which also fills the traffic-state data when `-TSM` is on) and after `printCurrentPassengerStatus`, and before the signalling clean-up of the same step. The traffic-state message goes first.

At the top of each step the loop waits while a pause is requested and ends when a stop is requested, so a paused step sends nothing. Pause, stop and the step delay are read through `SimulationWorker::active()`, so they exist only in a run in the window and not in a `-g 0` run. In a window run the step loop runs on the worker thread (`MainWindow::startSimulation`). In a `-g 0` run it runs on the main thread (`main`).

Before each send, the XML text of the message is printed to standard output, whether or not the send succeeds. It follows the line `Sending the following Traffic State XML file` or `Sending the following Route Choice XML file`. A reply is printed as `Received from <endpoint>:<reply>`. The step loop ignores the result of `send_external_state`.

## Transport

The application connects to the endpoints and does not bind; the module has to bind. The two endpoints are fixed strings in `DispatchController.cpp`, and there is no option for a host or a port.

`send_external_state` (`io/RailMLParser.cpp`) keeps one ZeroMQ REQ socket per endpoint in a process-global map. A send creates the socket of its endpoint when there is none, with linger 0, immediate 1, and a send and receive timeout of 100 ms (`TransportTimeoutMs`). The send itself uses `dontwait`, so the send timeout is not used. The wait for a reply uses the receive timeout.

| Situation | What happens | Result |
| --- | --- | --- |
| No completed connection to a module: no module is running, or the socket was created in this call and its connection is not complete yet | `immediate` is 1 and the send uses `dontwait`, so the send fails at once. Nothing is waited for, and the socket is kept. | `false` |
| Connection complete, no reply within 100 ms | The receive wait ends after 100 ms. The socket is destroyed, and the next send creates a new one. | `false` |
| Reply | The reply is printed and dropped. | `true` |
| Exception | The message is written to standard error, and the socket is destroyed. | `false` |

The receive wait can take up to 100 ms per channel and step. `tests/test_railmlparser.cpp` repeats a send up to 20 times, 10 ms apart, until one returns `true`: a first send on a new socket can return `false` although a module is listening.

## The message

Each message is one ZeroMQ part with UTF-8 JSON text (`nlohmann::json::dump`). It holds the payload object and one string member `xml` with the XML text of the same message. The members of a JSON object are in the sorted order of their keys.

### Traffic state

`time` is the step number `t`. A step is `timestep` seconds long, and `timestep` is 1 (`simulation/Infrastructure.cpp`). `trains` is an object keyed by the train description, which is the service id, `-` and the occurrence. It holds every train of the run in every message, also a train that is not due yet. Each train has these members:

| Member | Unit | How it is made |
| --- | --- | --- |
| `km-point` | km | The head position from `Train::trainXPosition(t)`: the coordinate of the start of the block section that holds the head, plus the distance into that section (minus it on a reversed route), divided by 1000. Rounded up to 0.01 km. It is -1 when the head is in no block section of the route, that is, when the train is not located. |
| `speed` | km/h | `instant_train_speed[t]` times 3.6, rounded up to 0.01. |
| `BlockOccupied` | text | The id of the block section in `Train::Bs`. |
| `lastOccTime` | step number | `Train::ComputeLastOccupationTime_real_time` with the id in `BlockOccupied` and entry time 10. It looks for the first step `p` from 11 up to `t` at which `instant_spatial_position[p]` has reached the start of the block section with that id in the route, and returns `p`. It returns 0 when there is none, so it is never negative. When no block section of the route has the id, it uses the first block section of the route. |
| `direction` | boolean | The `reversed_direction` flag of the route of the train. |
| `depTime` | step number | The `departure_time` of the train. |
| `trackID` | text | Present only once the train is due (`departure_time <= t`). The part of the block section id after its first `-`, without its last character. |
| `inArea` | 0 or 1 | 1 from the step the train is due, and it stays 1 after the train has left the network. |

### Route choice

`routeChoicePayload` (`simulation/Passengers.cpp`) gives `time`, the step number, and `passengers`, an object with one member for each passenger that is in the network and has a current journey. The key is the passenger id followed by `--1.0`. The members are `origin` and `destination`, the ids of the departure and arrival station of the journey, and `departure_time`, the time at which the journey starts, as a whole number of seconds of the day. A passenger starts a journey when the start time of day plus `t` reaches that value.

## The XML

Both builders in `io/RailMLParser.cpp` write the XML declaration (version 1.0, encoding UTF-8, standalone yes), then the comment `Generated by EGTRAIN on` followed by the local date and time at which the message is made, then the root element, indented with two spaces.

`trafficStateMonitoring_xml` writes this outline:

```text
trafficState                  currentTime
  trainStateInArea            one for each train with inArea 1
    trainID                   trainNumber
    trainPosition             trackID travelDirection posOnTrack
                              currentTrackVacancyDetectionSection lastOccupationTime
    speed                     text
  trainStateOutArea           one for each other train
    trainID                   trainNumber expectedEntranceTime
```

- `trainNumber` is the key of the train. The elements of both names stand in one sequence in the sorted order of these keys, not in the order of the timetable.
- `trackID` and `currentTrackVacancyDetectionSection` hold `trackID` and `BlockOccupied`. `travelDirection` is `1` when `direction` is false and `-1` when it is true. `posOnTrack` holds `km-point` as JSON text.
- `lastOccupationTime` is written when `lastOccTime` is not negative, which is the case for every `trainStateInArea`.
- `speed` holds the km/h value as text made with `std::to_string` of a double.
- In `trainStateOutArea` the attribute `expectedEntranceTime` sits on `trainID`.

`routeChoice_xml` writes this outline:

```text
routeChoiceRequest            currentTime
  person                      person_id trip_id
  origin                      text
  destination                 text
  departure_time              text, a whole number
  person ...                  the four elements repeat for the next passenger
```

`person_id` is the key of the passenger in the payload, and `trip_id` is always `1`. The four elements of a passenger are children of the root, one after the other; `origin`, `destination` and `departure_time` are not inside `person`. With no passenger in the payload the root is empty.

`currentTime` is the start time of day plus `t` seconds, on the local calendar date of the machine when the message is made. `lastOccupationTime` and `expectedEntranceTime` are the start time of day plus `lastOccTime` or `depTime` seconds on the same date, without `t`. The start time of day is 23300 s in `trafficStateMonitoring_xml` and `initial_variables.startingSimulationTime` in `routeChoice_xml`. Every time is written as `YYYY-MM-DD HH:MM:SS.000 CEST`, with the literal text `CEST`.

## Known limits

- The traffic-state builder uses a fixed start time of day, 23300 s (06:28:20), and not the start time of the scene. The route-choice builder uses `initial_variables.startingSimulationTime`.
- Both builders write the literal `CEST` whatever the time zone of the machine. The date is the date of the machine, not of the scene.
- The traffic-state JSON object is declared once for the whole step loop and updated in place. A train stays in it after it has left the network, and `inArea` stays 1 because it depends only on `departure_time <= t`.
- The route-choice XML puts `origin`, `destination` and `departure_time` next to the `person` element and not inside it.
- When a module does not answer within 100 ms, the socket is destroyed, and the next send goes through a new socket whose connection may not be complete. That send can return `false` at once. The retry loop in `tests/test_railmlparser.cpp` is the sign that this happens.

## What is not an interface

- `read_rttp_train_view` and `read_rttp_infra_view` in `io/RailMLParser.cpp` read an RTTP XML text and print what they find to standard output. They are not declared in `io/RailMLParser.h`, and the application does not call them. Only `tests/test_railmlparser.cpp` calls `read_rttp_train_view`. Nothing calls `read_rttp_infra_view`.
- The files written during a run are results and not a protocol, and no code in `EGTRAIN/QEGTRAIN` reads them. `printCurrentPassengerStatus` rewrites `PassengerStatus/PassengerStatus.txt` in the output folder at every step. `Train::printTrainArrDepMsg`, called from `Train::checkTrainArrDep`, appends one line to `Rescheduling/EGTRAINOutput.txt` when a train has stopped at its last station.
- The application opens no listening socket, local server or named pipe, and the ZeroMQ code only connects. The reply of a module is printed and not used. The only reads from standard input are the `--interactive` questions in `parseCmdOptions`.
- The window controls for pause, stop and speed exist only for a run in the window.

## Tests and code

- `test_railmlparser` (unit) parses one RTTP train view, checks the parse error text for a broken document, checks that a send without a listener returns `false` in under one second, and checks the JSON envelope (`time` and `xml`) against a REP socket on the loopback address.
- `test_external_sharing_no_listener` (integration) runs the Paimpol scene with `-g 0 -h 1 -TSM 1 -RC 1` and no listener. It runs one step and expects the run to finish.
- `test_route_choice_missing_data` (integration) starts the `Assignment_Gvc_Gdg_Ut` scene with `-RC 1` and expects the diagnostic `scene.route_choice.passengers.none`.
- `test_operationsbuilder` checks `routeChoicePayload`.
- No test covers the content of either XML builder or the traffic-state JSON that the step loop builds.

The library `egtrain_railml` is built from the one source `io/RailMLParser.cpp`. Its header `io/RailMLParser.h` declares `send_external_state`, `trafficStateMonitoring_xml` and `routeChoice_xml`. The library links `egtrain_pugixml`, `nlohmann_json` and cppzmq. See [Libraries](source-layout.md#libraries).
