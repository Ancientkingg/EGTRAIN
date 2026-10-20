#ifndef RUNCONTROL_H
#define RUNCONTROL_H

#include <atomic>
#include <cstddef>
#include <functional>
#include <mutex>
#include <vector>

// What a run can be asked to do at the top of a step.
enum class RunCommandKind {
	Pause,
	Resume,
	Stop,
	SetDelayMs
};

struct RunCommand {
	RunCommandKind kind = RunCommandKind::Pause;
	// For SetDelayMs, in milliseconds.
	int delayMs = 0;
};

// The stop, pause and step delay of one run, and a bounded queue of commands for them. Every run has
// one, also a run without a window. No Qt.
//
// The request functions, the readers and submit can be called from any thread. beginStep and
// applyQueuedCommands are called by the thread that runs the steps only.
//
// A command waits in the queue until the run reaches the top of a step. There beginStep takes every
// waiting command and applies it in the order of the submits, then checks pause, stop and the delay.
// No command takes effect in the middle of a step.
class RunControl {
public:
	// Number of commands that can wait for the next step.
	static constexpr std::size_t kCommandQueueSize = 16;

	// Ends the run at the next step. The stop is also recorded as a cancellation, and lifts a pause so
	// that the thread of the run can see the stop.
	void requestStop();
	void requestPause();
	void requestResume();
	void setDelayMs(int ms);

	bool isStopRequested() const { return m_stop; }
	// Observation-only latch; set by requestStop() and never cleared.
	bool wasCancellationRequested() const { return m_cancelled; }
	bool isPauseRequested() const { return m_pause; }
	int delayMs() const { return m_delayMs; }

	// Queues a command for the top of the next step. Returns false and queues nothing when
	// kCommandQueueSize commands wait. Never waits for the run.
	bool submit(const RunCommand& command);

	// Applies the waiting commands in the order in which they were submitted.
	void applyQueuedCommands();

	// The top of a step. Applies the waiting commands, waits while the run is paused (and applies the
	// commands that arrive meanwhile), then sleeps for the step delay. Returns false when the run is
	// to stop; the delay is not waited for then.
	//
	// fetchCommands, when it is given, is called before the waiting commands are applied, also after
	// each wait of a pause. It lets the owner of the run submit commands that come from another
	// source, which a paused run could not otherwise take.
	bool beginStep(const std::function<void()>& fetchCommands = {});

private:
	std::atomic<bool> m_stop{false};
	std::atomic<bool> m_cancelled{false};
	std::atomic<bool> m_pause{false};
	std::atomic<int> m_delayMs{0};

	// m_queued is the size of m_commands, so a step without commands takes no lock.
	std::atomic<std::size_t> m_queued{0};
	std::mutex m_commandsMutex;
	std::vector<RunCommand> m_commands;
};

#endif // RUNCONTROL_H
