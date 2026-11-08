# Sharing a live run

A run can publish its state to programs on the same machine while it goes on, and a program can ask the run to pause, resume, stop or wait between steps. This page shows how to start a run with the live stream and how to write a client for it. The exact promises of the interface are in [Live contract version 1](../architecture/external-sharing.md#live-contract-version-1).

The live stream is off unless you ask for it. Without `--live` a run opens no port, starts no thread and makes no frame. It listens on `127.0.0.1` only, so a program on another machine cannot reach it.

## Start a run with the stream

```bash
QEGTRAIN -n 2 -g 0 --live 0 --live-every 500 --live-commands
```

| Option | Effect |
| --- | --- |
| `--live 0` | Publishes the state of every run of the process. 0 takes a free port; a number from 1 to 65535 takes that port. |
| `--live-every 500` | Makes one state frame for every 500th step. Without it, every step has a frame. |
| `--live-commands` | Also takes commands. Without it, the stream can only be read. |

The program prints one line on standard error, and the file it names holds what a client needs:

```text
Live stream on tcp://127.0.0.1:62952; the endpoints are in <output folder>/live-endpoint.json
```

```json
{"v": 1, "state": "tcp://127.0.0.1:62952", "command": "tcp://127.0.0.1:62953", "token": "0f3c6a9e1b2d4c5f8a7e90b1c2d3e4f5"}
```

- `state` is the address to subscribe to. `command` and `token` are `null` without `--live-commands`.
- The token is new at every start. Every command carries it, and where the system supports it only the owner of the file can read it.
- The file is in the [output folder](../guides/command-line.md#output-folder) of the scene, and EGTRAIN removes it when it ends.
- A window run takes the same options. The stream stays up while the window is open, and each run gets its own run id.

A port that is in use stops the start with exit code 2 and the reason on standard error. See [Command-line options](../guides/command-line.md#options) for the other exit codes.

## A small client

This client subscribes to the stream, prints the position of each train, asks the run to wait 2 ms at each step, and prints the reply and the events. It needs cppzmq and nlohmann-json, which a build of EGTRAIN already uses. `tests/live_client.cpp` does the same and checks every frame it reads against the contract.

```cpp
#include <nlohmann/json.hpp>
#include <zmq.hpp>

#include <fstream>
#include <iostream>
#include <string>

using nlohmann::json;

int main(int, char** argv) {
	std::ifstream file(argv[1]);
	const json endpoint = json::parse(file);
	zmq::context_t context;
	zmq::socket_t frames(context, zmq::socket_type::sub);
	frames.set(zmq::sockopt::subscribe, "");
	frames.connect(endpoint["state"].get<std::string>());
	zmq::socket_t commands(context, zmq::socket_type::req);
	commands.connect(endpoint["command"].get<std::string>());

	bool asked = false;
	for (;;) {
		zmq::message_t topic, body;
		if (!frames.recv(topic, zmq::recv_flags::none) || !frames.recv(body, zmq::recv_flags::none))
			continue;
		const json frame = json::parse(body.to_string());
		if (!asked) { // one command, as soon as a frame shows that a run is going on
			const json request = {{"v", 1}, {"id", "wait-2ms"}, {"token", endpoint["token"]}, {"cmd", "set_delay_ms"},
				{"args", {{"delay_ms", 2}}}};
			commands.send(zmq::buffer(request.dump()), zmq::send_flags::none);
			zmq::message_t reply;
			if (commands.recv(reply, zmq::recv_flags::none))
				std::cout << "reply " << reply.to_string() << "\n";
			asked = true;
		}
		if (topic.to_string() == "state") {
			for (const json& train : frame["trains"])
				if (!train["route_pos_m"].is_null())
					std::cout << "step " << frame["step"] << " " << train["service_id"].get<std::string>() << "-" << train["occurrence"] << " at "
							  << train["route_pos_m"] << " m, " << train["speed_ms"] << " m/s\n";
		} else if (topic.to_string() == "event") {
			std::cout << "event " << frame.dump() << "\n";
			if (frame["kind"] == "run_finished")
				return 0;
		}
	}
}
```

Build it and run it beside the program, from a second terminal, with the path that the program printed:

```bash
c++ -std=c++17 live_min.cpp -lzmq -o live_min
./live_min path/to/live-endpoint.json
```

On macOS with Homebrew add `-I/opt/homebrew/include -L/opt/homebrew/lib`. The output looks like this:

```text
reply {"id":"wait-2ms","run_id":"27b810e9dfd6fcef","status":"accepted","v":1}
event {"cmd":"set_delay_ms","command_id":"wait-2ms","kind":"applied","run_id":"27b810e9dfd6fcef","seq":2,"step":109,"v":1}
step 500 Guin-Paim-EXPRESS-1-1 at 5968.424015402054 m, 13.639966331242688 m/s
step 1000 Guin-Paim-EXPRESS-1-1 at 13337.63961729874 m, 16.32391482291379 m/s
...
step 4500 Guin-Paim-ALL-STOPS-1-1 at 31215.89130218302 m, 10.368064364331092 m/s
event {"kind":"steps_finished","run_id":"27b810e9dfd6fcef","seq":20,"step":9000,"v":1}
event {"kind":"run_finished","run_id":"27b810e9dfd6fcef","seq":21,"status":"completed","step":9000,"v":1}
```

The run waits 2 ms at each step after the command, so this run of 9000 steps takes about 20 seconds longer. `build/live_client` does the same with `--set-delay-ms 2` and prints one line of JSON for each run, with the reply and the events in it.

How the client works:

- A frame has two ZeroMQ parts: the topic (`hello`, `state` or `event`) and the frame as JSON text. The client reads both.
- Every frame has `v`, `run_id` and `seq`. The `state` frames hold one entry for each train of the run, with `route_pos_m` in metres, `speed_ms` in m/s and `null` for a train that has no position yet.
- The reply `accepted` means that the command is queued. The event `applied` means that the run took it, and `step` is the number of steps that had run. A command that the run does not take gets the event `rejected` with a reason.
- A client that starts after a run has begun does not see the `hello` frame. This one does not need it, because it asks for its command when the first frame of any kind arrives. A client that needs the `hello` sends `describe` (see below).

## Commands

Each command is a JSON object sent to the `command` address with `v` (1), `id` (your own text of at most 64 bytes), `token`, `cmd` and, for `set_delay_ms`, `args`.

| `cmd` | Effect |
| --- | --- |
| `pause` | The run waits at the top of the next step. |
| `resume` | Ends a pause. |
| `stop` | Ends the run as the Stop button does. The status of the run is `cancelled`. |
| `set_delay_ms` | The run waits `args.delay_ms` milliseconds (0 to 1000) at the top of each step. |
| `describe` | Publishes the `hello` frame of the latest run again. It changes nothing in the run. |

No command changes the railway. The commands change when the steps run, and a run that receives only `pause`, `resume` and `set_delay_ms` gives the same results as a run that receives nothing.

The reply always comes at once. It is `accepted`, or `rejected` with one of these reasons:

```json
{"v": 1, "id": "wait-2ms", "status": "rejected", "reason": "bad_argument", "run_id": "f02f6e15890a2dde"}
```

| Reason | What is wrong |
| --- | --- |
| `bad_json`, `too_large` | The request is no JSON object, or it is longer than 4096 bytes. |
| `bad_version` | `v` is not 1. |
| `bad_argument` | `id` is missing, not text or longer than 64 bytes, or `set_delay_ms` has no whole `delay_ms` from 0 to 1000. |
| `bad_token` | `token` is not the one in the file. |
| `unknown_command` | `cmd` is not one of the five names. |
| `queue_full` | 16 commands wait already. |
| `no_active_run` | `describe` came before any run began. |

## When something does not work

| What you see | Why | What to do |
| --- | --- | --- |
| The file `live-endpoint.json` does not exist | The program has not started the stream yet, or it was started without `--live`, or it has ended. | Wait for the line on standard error. The program removes the file when it ends. |
| The client connects and never prints anything | The file is left from a program that was killed: nothing listens at its address, and ZeroMQ does not report that. | Check that the program runs, and start the client from the file of the run you started. |
| The client sees `state` frames but no `hello` | The client subscribed after the `hello` was published. A subscription takes a moment to reach the sender. | Start the client as soon as the line appears, or send `describe`, which publishes the `hello` again. |
| The client waits for `run_finished` and gets nothing | The run ended before the client connected. A client that joins late gets no earlier frame. | `describe` gives the `hello` of the last run and no more. Start the client before the run. |
| `rejected` with `bad_token` | The token is new at every start. | Read the file again. |
| `accepted` and then no `applied` event | The command waits for the top of the next step. A paused run looks again every 50 ms. | Wait for the event. If the run has ended, the event is `rejected` with `run_ended`. |
| `rejected` event with `no_active_run` | The command arrived between two runs. | Send it after the `hello` of the run. |
| `seq` jumps | Frames were lost: the run made them faster than the client read them, or the client was too slow. The run does not wait. | Raise `--live-every`, or read faster. The frames you got are complete. |
| The run is slower with `--live` | The run builds a snapshot and the text of each frame. | Raise `--live-every`. See [Cost](../architecture/external-sharing.md#cost). |
| Exit code 2 at the start | The port is in use. | Use `--live 0` or another port. |
| The window shows a running run after a client paused it | The buttons of the window do not follow commands. | Resume with a command. |

If the stream does not help with what you need, the [External state sharing](../architecture/external-sharing.md) page describes the frames, the threads and the limits in full.
