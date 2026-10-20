#include "simulation/RunControl.h"

#include <chrono>
#include <thread>

namespace {
constexpr std::chrono::milliseconds kPauseCheckInterval{50};
}

void RunControl::requestStop() {
	m_cancelled = true;
	m_stop = true;
	m_pause = false; // unpause so the thread of the run can see the stop flag
}

void RunControl::requestPause() {
	m_pause = true;
}

void RunControl::requestResume() {
	m_pause = false;
}

void RunControl::setDelayMs(int ms) {
	m_delayMs = ms;
}

bool RunControl::submit(const RunCommand& command) {
	const std::lock_guard<std::mutex> lock(m_commandsMutex);
	if (m_commands.size() >= kCommandQueueSize)
		return false;
	m_commands.push_back(command);
	m_queued = m_commands.size();
	return true;
}

void RunControl::applyQueuedCommands() {
	if (m_queued == 0)
		return;
	std::vector<RunCommand> commands;
	{
		const std::lock_guard<std::mutex> lock(m_commandsMutex);
		commands.swap(m_commands);
		m_queued = 0;
	}
	for (const RunCommand& command : commands) {
		switch (command.kind) {
			case RunCommandKind::Pause:
				requestPause();
				break;
			case RunCommandKind::Resume:
				requestResume();
				break;
			case RunCommandKind::Stop:
				requestStop();
				break;
			case RunCommandKind::SetDelayMs:
				setDelayMs(command.delayMs);
				break;
		}
	}
}

bool RunControl::beginStep(const std::function<void()>& fetchCommands) {
	if (fetchCommands)
		fetchCommands();
	applyQueuedCommands();
	while (m_pause && !m_stop) {
		std::this_thread::sleep_for(kPauseCheckInterval);
		if (fetchCommands)
			fetchCommands();
		applyQueuedCommands();
	}
	if (m_stop)
		return false;
	if (const int delay = m_delayMs)
		std::this_thread::sleep_for(std::chrono::milliseconds(delay));
	return true;
}
