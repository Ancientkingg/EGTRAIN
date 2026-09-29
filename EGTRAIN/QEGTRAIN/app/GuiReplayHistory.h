#ifndef GUIREPLAYHISTORY_H
#define GUIREPLAYHISTORY_H

#include "app/GuiSimulationSnapshot.h"
#include <algorithm>
#include <cstddef>
#include <deque>
#include <utility>

// Accounted payload bounds include vector and string capacities, but not allocator
// bookkeeping, shared_ptr control blocks or container-node overhead. This is not RSS.
class GuiReplayHistory {
public:
	static constexpr int cadenceSeconds = 5;
	static constexpr std::size_t payloadLimit = 64u * 1024u * 1024u;
	static constexpr std::size_t frameLimit = 8192;

	void clear() {
		frames_.clear();
		bytes_ = 0;
		truncated_ = false;
		oversize_ = false;
	}

	void record(std::shared_ptr<const GuiSimulationSnapshot> frame) {
		if (!frame || oversize_)
			return;
		const bool final = frame->timestep >= frame->totalTimesteps - 1;
		if (!frames_.empty() && !final && frame->timestep % cadenceSeconds != 0)
			return;
		if (!frames_.empty() && frames_.back().first->timestep == frame->timestep)
			return;
		const std::size_t size = payloadBytes(*frame);
		if (size > payloadLimit) {
			clear();
			oversize_ = true;
			return;
		}
		while (!frames_.empty() && (frames_.size() >= frameLimit || bytes_ > payloadLimit - size)) {
			bytes_ -= frames_.front().second;
			frames_.pop_front();
			truncated_ = true;
		}
		bytes_ += size;
		frames_.emplace_back(std::move(frame), size);
	}

	bool empty() const { return frames_.empty(); }
	bool oversize() const { return oversize_; }
	bool truncated() const { return truncated_; }
	std::size_t size() const { return frames_.size(); }
	std::size_t payloadBytes() const { return bytes_; }
	int firstTime() const { return frames_.empty() ? 0 : frames_.front().first->timestep; }
	int lastTime() const { return frames_.empty() ? 0 : frames_.back().first->timestep; }
	std::shared_ptr<const GuiSimulationSnapshot> atOrBefore(int time) const {
		if (frames_.empty())
			return {};
		auto it = std::upper_bound(frames_.begin(), frames_.end(), time,
			[](int value, const auto& entry) { return value < entry.first->timestep; });
		return (it == frames_.begin() ? it : std::prev(it))->first;
	}

private:
	static std::size_t payloadBytes(const GuiSimulationSnapshot& frame) {
		const auto stringBytes = [](const std::string& text) { return text.capacity() + 1; };
		std::size_t bytes = sizeof(frame);
		bytes += frame.trains.capacity() * sizeof(GuiTrainState);
		for (const auto& train : frame.trains) {
			bytes += stringBytes(train.type) + stringBytes(train.description) + stringBytes(train.operatingCode);
			bytes += train.wagonHeadPositions.capacity() * sizeof(double);
			bytes += train.wagonTailPositions.capacity() * sizeof(double);
			bytes += train.occupiedArcs.capacity() * sizeof(GuiOccupiedArc);
		}
		bytes += frame.signalStates.capacity() * sizeof(GuiSignalState);
		for (const auto& signal : frame.signalStates) bytes += stringBytes(signal.sectionId);
		bytes += frame.sectionStates.capacity() * sizeof(GuiSectionState);
		for (const auto& section : frame.sectionStates) bytes += stringBytes(section.sectionId);
		bytes += frame.platforms.capacity() * sizeof(GuiPlatformState);
		for (const auto& platform : frame.platforms) {
			bytes += stringBytes(platform.stationId) + stringBytes(platform.platformId);
			bytes += platform.passengerIds.capacity() * sizeof(std::string);
			for (const auto& id : platform.passengerIds) bytes += stringBytes(id);
		}
		bytes += frame.passengers.capacity() * sizeof(GuiPassengerState);
		for (const auto& passenger : frame.passengers)
			bytes += stringBytes(passenger.id) + stringBytes(passenger.status)
				+ stringBytes(passenger.waitingPlatform) + stringBytes(passenger.nextTrain)
				+ stringBytes(passenger.nextDestination);
		bytes += frame.virtualCouplingMessages.capacity() * sizeof(GuiVirtualCouplingState);
		for (const auto& message : frame.virtualCouplingMessages)
			bytes += stringBytes(message.trainDescription) + stringBytes(message.message);
		return bytes;
	}

	std::deque<std::pair<std::shared_ptr<const GuiSimulationSnapshot>, std::size_t>> frames_;
	std::size_t bytes_ = 0;
	bool truncated_ = false;
	bool oversize_ = false;
};

#endif
