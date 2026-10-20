#include "app/RuntimeGeometryFromRun.h"

#include "app/DispatchController.h"
#include "simulation/SimulationRun.h"

#include <algorithm>
#include <cstddef>

namespace {

class RuntimeGeometryFromRun final : public RuntimeGeometrySource {
public:
	explicit RuntimeGeometryFromRun(DispatchController& controller) : m_controller(controller) {}

	int trackCount() const override { return run().infra.numTrackLines; }
	BlockSet& track(int index) const override { return run().infra.blockSets[index]; }
	int regionOfTrack(int index) const override { return run().infra.blockSets[index].region; }

	int stationCount() const override { return run().infra.numStations; }
	Stations& station(int index) const override { return run().infra.stations[index]; }

	int connectionCount() const override { return run().infra.numConnections; }
	Connections& connection(int index) const override { return run().infra.connections[index]; }

	int sectionCount() const override { return run().infra.numBlocks; }
	const Section& section(int index) const override { return run().infra.sections[static_cast<std::size_t>(index)]; }

	const Route* route(int index) const override {
		if (index < 0 || index >= static_cast<int>(run().infra.routes.size()))
			return nullptr;
		return &run().infra.routes[static_cast<std::size_t>(index)];
	}

	const StationPlatform* platform(const std::string& stationId, const std::string& platformId) const override {
		const std::list<StationPlatform>& platforms = run().platforms;
		const auto found = std::find_if(platforms.begin(), platforms.end(),
			[&stationId, &platformId](const StationPlatform& candidate) {
				return candidate.StationID == stationId && candidate.ID == platformId;
			});
		return found == platforms.end() ? nullptr : &*found;
	}

	bool passengerGui() const override { return run().settings.PAX_GUI; }

private:
	SimulationRun& run() const { return m_controller.mutableRun(); }
	DispatchController& m_controller;
};

} // namespace

std::unique_ptr<RuntimeGeometrySource> makeRuntimeGeometryFromRun(DispatchController& controller) {
	return std::make_unique<RuntimeGeometryFromRun>(controller);
}
