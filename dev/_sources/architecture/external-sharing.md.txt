# External state sharing

A run shares its state as the [live stream](#live-stream) of `--live`, whose promises are in [Live contract version 1](#live-contract-version-1). [Sharing a live run](../guides/live-sharing.md) is the guide for a client. The options `-TSM` and `-RC` are removed: a start that passes either stops with exit code 1 and an error that names `--live`.

This page describes the code as it is. Function names are given so that a maintainer can find them.

## The step loop

`DispatchController::Train_Simulation_Mixed_Signalling_With_Passengers` (`app/DispatchController.cpp`) runs the step loop. At the top of each step the loop waits while a pause is requested and ends when a stop is requested. Pause, stop, the step delay and a bounded queue of 16 commands belong to a [`RunControl`] (`simulation/RunControl.h`), which every run has. At the top of the step, before any train moves, `RunControl::beginStep` applies the queued commands in the order in which they were submitted, and then checks pause, stop and the delay. In a window run the control belongs to the [`SimulationWorker`], which forwards the window's requests to it, and `runSimulation` finds it through `SimulationWorker::active()`. A `-g 0` run makes a control for itself. With `--live-commands`, `DispatchController` hands the commands of live clients to that control (see [Commands](#commands)); nothing else in the application submits a command to it. In a window run the step loop runs on the worker thread ([`RunController::startSimulation`]). In a `-g 0` run it runs on the main thread (`main`).

## What is not an interface

- The files written during a run are results and not a protocol, and no code in `EGTRAIN/QEGTRAIN` reads them. `printCurrentPassengerStatus` writes `PassengerStatus/PassengerStatus.txt` once, after the step loop. `Train::printTrainArrDepMsg`, called from `Train::checkTrainArrDep`, appends one line to `Rescheduling/EGTRAINOutput.txt` when a train has stopped at its last station.
- Without `--live` the application opens no listening socket, local server or named pipe. The live server below binds sockets, and only a start with `--live` creates it. The only reads from standard input are the `--interactive` questions in `parseCmdOptions`.
- The window controls for pause, stop and speed exist only for a run in the window. A run without a window, or a window run that a client controls, takes the commands of the live stream instead (see [Commands](#commands)).

## Live stream

`--live <port>` makes every run of the process publish its state. `main` starts the live server (see [Live server](#live-server)) after the output folder is known and before the scene is prepared, and stops it when `main` returns. `DispatchController` owns it (`startLive`, `stopLive`) and feeds it from the step loop.

| Option | Effect |
| --- | --- |
| `--live <port>` | Binds the state socket to `127.0.0.1:<port>`. 0 takes a free port. |
| `--live-every <steps>` | Makes a state frame for each `<steps>`-th step, counted from step 0. The default is 1. |
| `--live-commands` | Also binds the command socket, and writes the token to the endpoint file. |

`--live-every` and `--live-commands` without `--live`, a port that is not a whole number from 0 to 65535, and an interval below 1 stop the start with exit code 1. A server that cannot start, for example on a port that is in use, stops it with exit code 2 and the line `ERROR: Cannot start the live stream: <reason>` on standard error; a window launch also shows the reason in a message box. The start never leaves a run half shared.

`live-endpoint.json` is written to the output folder of the scene that the command line names (see [Output folder](../guides/command-line.md#output-folder)), and the start prints `Live stream on <endpoint>; the endpoints are in <path>` on standard error. Nothing is printed on standard output, so the standard output of a run is the same with and without `--live`. `stop` removes the file when `main` returns. A process that is killed leaves it behind, with a port that nothing listens on.

Without `--live`, `DispatchController` holds no server. The process has no live thread, no ZeroMQ context for it and no socket, and a run builds no frame and no extra snapshot: each place that publishes tests one null pointer.

### What a run publishes

Each run begins with a new run id, 16 hexadecimal digits from `std::random_device`, and `seq` starts at 0. Every frame of the run carries the run id and its `seq`, and the frames leave in the order of the table below. The frames are those of `io/LiveFrame.h`; their members and units are in `docs/live/frame-v1.schema.json`.

| Frame | Sent | Holds |
| --- | --- | --- |
| `hello` | at the start of `runSimulation`, after `executionBegan` and before the first step | `scene_name` from the [`SceneModel`] that `prepareScene` was given, `base_time_s` (`startingSimulationTime` of the settings of the run), `horizon_steps` (`times` of the settings of the run), `step_s` (`timestep`), and one entry for each train of the run, in the order of [`SimulationRun::trains`], with its service id, occurrence, operating code and route index |
| `state` | at the end of each step `t` that is a multiple of the interval, in `publishSimulationSnapshot` | the trains of that step, and the signal list when it is due |
| `event` `steps_finished` | when the step loop has ended, with the number of steps that ran | nothing else; the positions are final |
| `event` `run_finished` | at the end of `runSimulation`, just before `executionReturned` is emitted, or `executionFailed` when an error ended the run after the `hello` frame | `status` `completed`, or `cancelled` when a stop was requested or an error ended the run |
| `event` `applied`, `rejected` | when the run takes a command, or turns it down; see [Commands](#commands) | `command_id`, `cmd` and, for `rejected`, `reason` |

`run_finished` means that the stages of `runSimulation` have ended. It does not mean that the output folder is complete: `printLastTrainServicePathDiagram` writes `TrainTrajectories/` after `runSimulation` returns (in `main` for a headless run, in the handler of `executionReturned` for a window run), and a window run does more end-of-run work there. A client must not read the output folder on this event.

A run in which no train was loaded publishes nothing. A run that is stopped publishes `steps_finished` with the steps that ran (0 when the stop came first) and then `run_finished` with `cancelled`.

The signal list is part of the first state frame of a run and of each later state frame in which it differs from the list of the last frame that held one (`liveSignalsChanged`). A subscriber that joins late, or that sees a gap in `seq`, has no signal list until the next frame that holds one; the list is not repeated for it.

A state frame is made for a sampled step only. The snapshot of a step comes from `buildGuiSimulationSnapshot`, as for the window. A window run has built it for the window already and the frame shares it. A run with `-g 0` builds no snapshot unless a frame is due, builds the one for that step, and drops it after the frame is made; the replay history and the mailbox of the window stay unused.

### Threads and loss

- The thread that runs the steps (the worker thread of a window run, the main thread of a `-g 0` run) makes the text of each frame and passes it to `pushHello`, `pushState` or `pushEvent`, and takes the commands with `tryPopCommand`. It never calls ZeroMQ and never waits for a socket, a subscriber or a client, so a stream that nobody reads, a client that leaves and a client that is slow do not slow a run beyond the cost of making the frames. `seq` is given by that thread alone.
- `startLive` and `stopLive` are called while no run goes on. A new run in the same process (a second run in the window) gets a new run id and a new hello, and the subscribers stay connected.
- The live server keeps 8 state frames. A run that makes frames faster than the I/O thread sends them (a few thousand frames a second) loses the oldest, and the gap shows in `seq`. `hello` and `event` frames are never dropped to make room for a state frame. The scripted window run of the 600-step Assignment scene is fast enough to lose frames at an interval of 10, and loses none at an interval of 100.
- A subscriber that joins after the hello has not seen it. With `--live-commands` it sends `describe`, and the server publishes the hello of the latest run again. Without commands it waits for the next run, or reads state frames without a hello.

### Cost

The frames are made on the thread of the run, so a run with `--live` takes longer than one without, also when nobody reads. The cost has two parts: building the snapshot of the step, and making the JSON text of the frame.

Measured once on a macOS machine that other jobs were using, a headless run of a committed scene with `--live 0` and no client took 2% (Copenhagen) to 140% (Amsterdam_Hilversum_Student) longer than a run without `--live`, and with `--live-every 10` it took 2% to 29% longer. These numbers show the order of the cost and are not a promise.

A scene with cheap steps and many signal sections pays most per frame, because the snapshot walks all sections of all routes and the signal list is the largest part of a frame. A scene with expensive steps, such as Copenhagen, pays little. `--live-every` is the control: a run that is read once every 10 steps makes a tenth of the frames.

The size of a state frame depends on the number of trains and on the signal list, which is the largest part of a frame that holds it. At an interval of 10 most frames of Netherlands hold it (711 of 800); of Paimpol, 307 of the 849 frames that a client received hold it.

| Scene | Trains | Hello | State frame without signals | State frame with signals |
| --- | ---: | ---: | ---: | ---: |
| Paimpol | 2 | 414 B | 507 B | 22165 B |
| Amsterdam_Hilversum_Student | 4 | 620 B | 865 B | 107341 B |
| Assignment_Gvc_Gdg_Ut | 24 | 2187 B | 4256 B | 9246 B |
| Netherlands | 40 | 3436 B | 6802 B | 113301 B |
| Copenhagen | 52 | 6311 B | 10953 B | 114950 B |
| Milano_Brescia | 65 | 5480 B | 11174 B | 32199 B |

The service id and occurrence of a train are part of the layout that the replay history keeps once for a run, so they add nothing to a frame of it: the history of a whole run of Copenhagen in the window holds 82722909 of the 134217728 bytes that it may hold.

### Commands

With `--live-commands` the server also answers `describe`, `pause`, `resume`, `stop` and `set_delay_ms` as described under [Requests](#requests), and the run takes the four commands that control it. None of them changes the railway: they change when the steps run, or end the run. A run that receives only `pause`, `resume` and `set_delay_ms` gives the same result files, the same standard output and the same state at every step as a run that receives nothing.

| Command | Effect |
| --- | --- |
| `pause` | The run stops at the top of the step and waits. No step runs and no state frame is made. |
| `resume` | Ends a pause. The run goes on with the step at which it stopped. Without a pause it changes nothing. |
| `stop` | Ends the run as the Stop button of the window does: `steps_finished` with the steps that ran, then `run_finished` with the status `cancelled`. The stages after the steps are skipped, so the energy, delay and blocking-time files are not written. A headless run still ends with exit code 0. |
| `set_delay_ms` | Sets the time that the run waits at the top of each step, in milliseconds from 0 to 1000. A window run does not show it on its speed slider or the readout next to it. |

- The effect point is the top of a step. `Train_Simulation_Mixed_Signalling_With_Passengers` calls `RunControl::beginStep` with a function that takes the accepted commands from the server (`tryPopCommand`) and submits them to the control, in the order in which the server accepted them. `beginStep` applies them before any train moves. A paused run calls the function again every 50 ms, so a `resume` or a `stop` reaches it. A command never acts in the middle of a step.
- Each command is reported as an `event` frame, made by the thread of the run. `applied` means that the run takes the command before the next step; `step` is the number of steps that have run then. `rejected` means that it does not take it, and `reason` says why:

| Reason | The command |
| --- | --- |
| `run_ended` | was still waiting when the steps of the run ended. This includes a command that arrives while the result files are written. The frame comes before `steps_finished`, or before `run_finished` for a command that arrived later. |
| `no_active_run` | was accepted while no run went on, before the first run or between two runs. The frame comes right after the hello of the next run, so the command never reaches that run. |
| `queue_full` | found 16 commands in the control that it had not applied yet. |

- Both frames carry `command_id`, the id that the client gave the request, and `cmd`, the name of the command. The members are in `docs/live/frame-v1.schema.json`.
- The accepted reply of the server means only that the command is queued. A client that needs to know that the run took it waits for the event with its `command_id`.
- A window run has the same control, so a command works there as well. A command does not change the buttons of the window: a run that a client has paused shows the buttons of a running run.

### A client and its test

`tests/live_client.cpp` is a small client with cppzmq and nlohmann-json and nothing else. It reads `live-endpoint.json`, subscribes to the state endpoint, sends `describe` until the hello of the run arrives when the file names a command endpoint, and checks each frame it reads against what this page promises: the version, one run id for a run, a `seq` that increases, steps that increase, a time axis that agrees with the hello, and an occurrence of each train that is counted from 1. It writes one JSON line for each run it saw and exits with 1 on the first broken promise. With `--set-delay-ms <ms>` it also sends one `set_delay_ms` request as soon as it has the hello of a run, and the line of that run holds the reply of the server (`command`: the status and, for a rejection, the reason) and the `applied` and `rejected` events of the run. The comment at the top of the file lists the options.

`test_live_stream_smoke` (`tools/e2e/live_stream_smoke.py`, integration) starts the application and the client and runs these cases:

- A headless Paimpol run that the client follows from start to end, and a run without `--live`: the two output folders are equal file by file, except `Computing_Times.txt`, the standard output is equal, and no endpoint file is left.
- A client that sends `set_delay_ms` with `--set-delay-ms`: the server accepts it, the run reports one `applied` event for it, and the output folder and the standard output are equal to those of a run without `--live`. A delay above the limit is answered with `bad_argument`, and the run reports no event for it.
- A client that leaves after 3 frames, in the first half of the run, and a run that no client reads: the same equality.
- A headless run of the whole Assignment scene, which repeats each of its services: the trains of the hello differ in service id and occurrence. The hello of every run that a case reads names the start time of its scene and trains that are services of that scene.
- A window run (`QEGTRAIN_AUTOSTART`) and a client that connects after the run has ended: it gets the hello of that run and no state frame, and the application goes on.
- A window run that the test closes at its first displayed step (`QEGTRAIN_E2E_CLOSE_DURING_RUN`): the client sees `steps_finished` with fewer steps than the horizon and then `run_finished` with `cancelled`.
- A scripted window run that runs the case twice (`QEGTRAIN_E2E_OPERATIONAL_COMPLETION`), registered on macOS only like the other scripted window runs: the client sees two run ids, each with its hello and its frames.
- Each option that is not valid, and a port that is in use.
- `-TSM` and `-RC`: refused with exit code 1 and an error that names `--live`.

## Live contract version 1

This section is what a consumer of the live stream can rely on. It holds for contract version 1, the value of `v` in every frame, request, reply and in `live-endpoint.json`. The sections around it describe the code that keeps these promises. [Sharing a live run](../guides/live-sharing.md) shows how to start a run with the stream and has a small client.

### Transport

- The run publishes frames on a ZeroMQ PUB socket and, with `--live-commands`, takes requests on a REP socket. Both listen on `127.0.0.1`. There is no host option and no other address, so only programs on the same machine can connect.
- A client subscribes to the PUB socket, and sends each request through a REQ socket. The addresses and the token of the requests are in `live-endpoint.json` in the output folder of the scene (see [Sockets](#sockets)).
- Without `--live` nothing is bound and nothing listens.

### Frames

A frame is two ZeroMQ parts: the topic, which is `hello`, `state` or `event`, and the frame as UTF-8 JSON text. A subscriber can filter on the topic. Every frame is a JSON object with these members:

| Member | Meaning |
| --- | --- |
| `v` | The contract version, 1. |
| `run_id` | 16 hexadecimal digits that name the run. All frames of a run carry the same value, and each run has a new one. |
| `seq` | The position of the frame in its run, counted from 0 across all kinds of frame. |

`hello` opens a run, and the server publishes it again when a client sends `describe`:

| Member | Unit | Meaning |
| --- | --- | --- |
| `scene_name` | text | The name of the scene. |
| `base_time_s` | s | The time of day of step 0, in seconds from midnight. |
| `horizon_steps` | steps | The number of steps of the run. |
| `step_s` | s | The seconds in one step. |
| `units` | | The unit of each kind of value: `distance` `m`, `time` `s`, `speed` `m/s`, `x` `km`. |
| `trains` | | Every train of the run, also one that has not entered, in a fixed order. Each has `service_id`, `occurrence` (counted from 1), `operating_code` and `route_index`. |

A train is named by `service_id` and `occurrence` together, for the whole run.

`state` is the state of one step. `trains` has one entry for each train of the `hello`, in the same order:

| Member | Unit | Meaning |
| --- | --- | --- |
| `step` | steps | The number of the step, counted from 0. |
| `sim_time_s` | s | `step` times `step_s`. |
| `clock_s` | s | `base_time_s` plus `sim_time_s`. It is not reduced to one day. |
| `trains` | | One entry for each train: `service_id`, `occurrence`, `operating_code`, `in_network`, `direction`, `route_pos_m`, `x_km`, `speed_ms` and `onboard`. |
| `signals` | | The signal state of every section and direction, as entries with `section_id`, `code`, `level`, `failed` and `reversed`. It is in the first `state` frame of a run and in each later frame in which it differs from the last frame that had it. |

`event` reports a change in the life of the run. Its `kind` says which:

| `kind` | Other members | Meaning |
| --- | --- | --- |
| `steps_finished` | `step` | The step loop has ended and the positions are final. `step` is the number of steps that ran. The result files may still be written. |
| `run_finished` | `step`, `status` | The stages of the run have ended. `status` is `completed`, or `cancelled` when the run was stopped or an error ended it. Not every result file is written yet. |
| `applied` | `step`, `command_id`, `cmd` | The run takes the command before the next step. |
| `rejected` | `step`, `command_id`, `cmd`, `reason` | The run does not take a command that the server accepted. See [Errors](#errors). |

### Units and coordinates

- Time is in seconds. `clock_s` is the time of day of the scene, with no date and no time zone, and it keeps counting past 24 hours.
- `route_pos_m` is the position of the head of the train on the axis of its route, in metres. A route has its own axis, so positions compare for trains with the same `route_index` of the `hello` only. It is a position and not the distance that the train has run.
- `x_km` is the coordinate of the head along the line, in km: the x of the block section that holds the head plus the distance into it, minus the distance on a reversed route. `direction` is `reversed` for a train on a reversed route.
- `speed_ms` is in metres per second. The simulation holds km/h, and the frame divides it by 3.6.
- A value that the run does not hold is `null`: the position, the coordinate and the speed of a train that has not entered or has left, and a coordinate of a head that lies in no block section. The internal marks for these (`-9999`, `-1`) never leave the process. A number that is not finite is `null` too.
- `in_network` is true from the step in which the train is due until it has left the simulation.
- `code` of a signal is the signalling code of the section (270 green, 180 yellow, 75 red, 751 red_red, 0 occupied or failed; see [Signalling levels](signalling-levels.md)). `level` is the signalling level, 0 to 5. Both are `null` for a section without a level. The frames have no aspect ahead of a train and no section of a train: only the list of signals.

### Ordering

- The thread of the run gives `seq`, so it is one order for the whole run: the `hello` is 0, and each frame that the run makes takes the next number, also a `state` frame that the server then drops.
- Frames reach a subscriber in the order of `seq`. A `hello` that was published again keeps its `seq`, and it can arrive in the middle of a run.
- A `state` frame is made at the end of its step. An `applied` event with `step` N comes after the `state` frame of step N-1 and before that of step N, because the run takes a command at the top of a step. The `steps_finished` event comes after the last `state` frame, and `run_finished` is the last frame of the run.
- A subscriber that joins later gets no earlier frame. A subscriber that is too slow loses frames, and the gap shows in `seq`. The run does not wait for it.

### Sampling

- `--live-every N` makes a `state` frame for the steps 0, N, 2N and so on. The last step has a frame only if it is a multiple of N. `hello` and `event` frames are never thinned.
- With N above 1 the `signals` of a frame can differ from those of the frame before in more than one section, because the frames between them were not made.
- The stream does not set the pace of a run. A run goes as fast as it can, and `set_delay_ms` slows it for a viewer. The server keeps 8 `state` frames for its I/O thread and drops the oldest when the run is faster; see [Threads and loss](#threads-and-loss).

### Compatibility

- The server speaks one version. It sets `v` to 1 in every frame, reply and in the endpoint file, and it rejects a request with another `v` as `bad_version`.
- Within version 1 a frame, a reply or the endpoint file can get new members, an `event` can get new kinds, and a request can get new commands. A consumer ignores a member and an event kind that it does not know.
- A change that removes a member, changes a unit or the meaning of a member, or changes the rules of ordering or sampling raises `v`. A consumer stops when it sees a `v` that it does not know.
- `docs/live/frame-v1.schema.json` names every member of every frame. `test_liveframe` checks the frames of the code against it and fails for a member that the schema does not name.

### Threads and where commands act

- One thread runs the steps: the worker thread of a window run, the main thread of a `-g 0` run. It builds the snapshot of a step, makes the text of each frame, gives `seq`, pushes the frame to the server and takes the commands. It never calls ZeroMQ and never waits for a socket or a client.
- One I/O thread owns every ZeroMQ socket. It sends the frames, and it checks each request, answers it within about 10 ms and queues the commands that it accepts. It never touches the run.
- A command acts at the top of a step, before any train moves, never in the middle of one. The thread of the run takes the accepted commands from the server and submits them to the `RunControl` of the run, which applies them in the order of arrival. While the run is paused it looks again every 50 ms.
- A command that is accepted while no step is going to start, because no run goes on or the steps have ended, does not act. It gets a `rejected` event.
- Any number of clients can subscribe. Each has its own buffer, so a slow client loses frames without effect on the run or on other clients.

### Errors

| Where | What | What happens |
| --- | --- | --- |
| Start | A `--live` option that is not valid. | The start stops with exit code 1. |
| Start | The port is in use, or the endpoint file cannot be written. | The start stops with exit code 2 and the reason on standard error, before a run begins. |
| Reply | The server rejects the request with `bad_json`, `too_large`, `bad_version`, `bad_argument`, `bad_token`, `unknown_command` or `queue_full`. | The reply has `status` `rejected` and the `reason`. Nothing is queued and the run does not see it. [Requests](#requests) gives the order of the checks. |
| Reply | `describe` when no run has begun. | `rejected` with `no_active_run`. |
| Event | An accepted command was waiting when the steps of the run ended. | `rejected` with `run_ended`. |
| Event | A command was accepted between two runs, or before the first. | `rejected` with `no_active_run` right after the `hello` of the next run. |
| Event | The control of the run held 16 commands that it had not applied. | `rejected` with `queue_full`. |
| Stream | A client disconnects, never reads, or is slow. | The run and the other clients go on. The client loses frames. |
| Stream | The I/O thread cannot hand a frame to its socket, or a socket call fails. | It logs the problem once for each kind in the category `io.zmq` and goes on. |
| Stream | Text that is not UTF-8. | The frame has U+FFFD in its place. |

A client that sends garbage, a request that is too large or a request with the wrong token cannot stop or change a run. Only an accepted command reaches it, and an accepted command is one of the four in [Commands](#commands).

## Live server

[`LiveServer`] (`io/LiveServer.h`) is the transport of the live contract. The frames are made by `io/LiveFrame.h` and described in `docs/live/frame-v1.schema.json`; the server carries the text of a frame and neither reads nor changes it. A `LiveServer` that is not started holds no thread, no ZeroMQ context and no socket.

### Sockets

| Socket | Created by | Endpoint |
| --- | --- | --- |
| PUB | `start` | `tcp://127.0.0.1:<port>`; port 0 takes a free port |
| REP | `start` with commands enabled | `tcp://127.0.0.1:<port>`, always a free port |

Both sockets listen on the loopback address, and there is no host option. A published frame is two parts: a topic (`state`, `hello` or `event`) and the frame text.

`start` writes `live-endpoint.json` to the output directory it is given, and creates the directory when it is missing:

```json
{"v": 1, "state": "tcp://127.0.0.1:40123", "command": "tcp://127.0.0.1:40124", "token": "0123456789abcdef0123456789abcdef"}
```

`command` and `token` are `null` without commands. The token is 128 random bits made at every start, in 32 hexadecimal digits. Where the platform supports it, only the owner of the file can read it. `stop` removes the file. `start` fails, and holds nothing, when the port is not valid or is in use, or when the file cannot be written.

### Threading

- One I/O thread, started by `start`, creates, binds, uses and closes both sockets. A ZeroMQ socket is never shared between threads. The thread creates the ZeroMQ context and ends it after the sockets are closed.
- Any other thread calls `pushState`, `pushHello`, `pushEvent`, `tryPopCommand`, `droppedStates`, `running` and the endpoint functions. Each takes the mutex of the server for a few instructions, or reads an atomic, and does not call ZeroMQ, wait for the I/O thread, or wait for a subscriber or a client. A step of the simulation therefore cannot stall on the transport. `start` and `stop` are called by one thread at a time.
- The caller makes the text of a frame before it pushes it, so making the text takes time on the thread of the caller.
- The I/O thread sends the waiting frames, then answers the requests that have arrived. After a pass that sent nothing it waits up to 10 ms for a request, and after a pass that sent frames it waits up to 1 ms. A pushed frame is therefore sent within about 10 ms, a request is answered within about 10 ms, and neither waits for the simulation.
- An exception on the I/O thread does not leave it. A problem with a socket is logged once for each kind of text, in the category `io.zmq`, and the thread waits one poll interval before it goes on.

### Queues and the drop policy

| Queue | Size | When it is full |
| --- | --- | --- |
| State frames | 8 | The oldest waiting state frame is dropped, and `droppedStates` grows by one. |
| Hello and event frames | 64 | The push returns `false` and the frame is not queued. |
| Accepted commands | 16 | The request is rejected with `queue_full`. |

- The frames of the three kinds leave in the order of the pushes, so an event pushed after a state frame never arrives before it.
- The caller gives each frame its `seq`. A state frame that was dropped shows as a gap in `seq`.
- The queue is emptied once in each pass of the I/O thread, so a producer that pushes more than a few thousand state frames a second loses the rest.
- A subscriber that reads too slowly loses frames when 1000 messages, which is 500 frames, wait for it. ZeroMQ drops further frames for that subscriber only, and the I/O thread never waits (`dontwait`).
- A subscriber that joins late does not get earlier frames. A client that sends `describe` makes the server publish the latest hello again. Any subscriber can get the hello twice.

### Requests

A request is one JSON object of at most 4096 bytes with the members `v` (1), `id` (a string of at most 64 bytes), `token`, `cmd` and, for `set_delay_ms`, `args` with `delay_ms`, a whole number from 0 to 1000. The commands are `pause`, `resume`, `stop`, `set_delay_ms` and `describe`. Every request gets a reply, which has the members `v`, `id` (`null` when the request had none that is valid), `status`, `reason` (only when rejected) and `run_id` (the run of the latest hello, or `null`).

The checks run in this order, and the first that fails gives the reason:

| Reason | The request |
| --- | --- |
| `too_large` | has more than 4096 bytes. This is checked before the text is parsed. |
| `bad_json` | is no JSON text, or is JSON that is not an object. |
| `bad_version` | has no `v`, or a `v` that is not the integer 1. |
| `bad_argument` | has no `id`, an `id` that is not a string, or one that is too long; or `set_delay_ms` has no `args.delay_ms` that is a whole number from 0 to 1000. |
| `bad_token` | has no `token` equal to the token in `live-endpoint.json`. The comparison takes a time that depends on the length only. |
| `unknown_command` | has no `cmd` that is a string, or a name that is not in the list. |
| `queue_full` | is valid, and 16 commands already wait. |
| `no_active_run` | is `describe`, and no hello was pushed since the server started. |

A rejected request is never queued. An accepted request is queued, and the reply says nothing more: the server does not apply a command. The owner of the server takes the commands with `tryPopCommand`, which never waits, and applies them; `DispatchController` does this at the top of each step (see [Commands](#commands)). `describe` is not queued: the hello is published before the reply is sent.

A message of more than 64 KiB closes the connection of the client that sent it, without a reply. Parts after the first part of a request are ignored.

### Stopping and starting again

`stop` sets a flag, and the I/O thread sends the frames that are still queued, closes both sockets with a linger of 500 ms, and then ends the context. Stopping therefore takes about half a second longer at most when a subscriber does not read. `stop` removes `live-endpoint.json`, and the destructor calls `stop`.

After a stop, `start` begins with empty queues, no hello, no command and a drop count of 0. It makes a new token, and takes new ports unless a port is given. A given port can be used again at once.

## Tests and code

- `test_liveserver` (unit) starts a `LiveServer` in the test process and talks to it through ZeroMQ sockets on the loopback address. It covers the endpoint file, the order of frames, the drop policy, a subscriber that never reads, a subscriber that disconnects and one that joins later, restart on the same port, a port in use, malformed, oversized and unauthorised requests, a full command queue, the frames that are sent when the server stops, stopping with clients connected, and producers on four threads. The synthetic producer is the test itself. A new subscriber is made ready by asking for `describe` until the hello arrives, not by waiting.
- `test_livecommand` (unit) checks `parseLiveRequest` (`io/LiveCommand.h`) on every request that the contract names: each accepted command, each reason, the limits of the size, the id and the delay, the order of the checks, and the id that comes back. It also checks the event frame that reports a command. It uses no socket, thread or run.
- `test_livereplay` (integration) plays the commands of the JSON-lines files in `tests/live/commands` against the line fixture in the test process. A `LiveServer` with commands takes the requests, which the test sends through a REQ socket from the thread of the run just before the step each line names, and a reader on another thread follows the stream. The test compares a run that receives `pause`, `resume` and `set_delay_ms` with a run that receives nothing: the same files, standard output and state at each of the 1500 steps. It checks the events with their step numbers, that no state frame comes while the run is paused, a `stop` before step 100 (`cancelled`, 100 steps), a command that arrives before the run (`no_active_run`), a command that arrives after the last step (`run_ended`) and a full control (`queue_full`). It also checks the `fetchCommands` argument of `RunControl::beginStep`.
- `test_live_stream_smoke` (integration) runs the application with `--live` and reads the stream with `live_client`; see [A client and its test](#a-client-and-its-test).

The live server is in `egtrain_live`, which is built from `io/LiveCommand.cpp`, `io/LiveFrame.cpp` and `io/LiveServer.cpp` and links `nlohmann_json`, `egtrain_util` and cppzmq. It does not use Qt. `egtrain_dispatch` links it, and `DispatchController` is its only user in the application.

[`RunControl`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_run_control.html
[`SimulationWorker`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_simulation_worker.html
[`RunController::startSimulation`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_run_controller.html
[`SceneModel`]: https://ancientkingg.github.io/EGTRAIN/latest/api/struct_scene_model.html
[`SimulationRun::trains`]: https://ancientkingg.github.io/EGTRAIN/latest/api/struct_simulation_run.html
[`LiveServer`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_live_server.html
