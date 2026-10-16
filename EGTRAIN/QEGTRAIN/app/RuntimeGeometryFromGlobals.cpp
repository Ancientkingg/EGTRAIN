#include "app/RuntimeGeometryFromGlobals.h"

#include "simulation/Infrastructure.h"
#include "simulation/InitialParameters.h"
#include "simulation/Signalling.h"

#include <algorithm>
#include <cstddef>

extern InitialParameters initial_variables;

namespace {

class RuntimeGeometryFromGlobals final : public RuntimeGeometrySource {
public:
	int trackCount() const override { return numTrackLines; }
	BlockSet& track(int index) const override { return blockSets[index]; }

	int stationCount() const override { return numStations; }
	Stations& station(int index) const override { return StationArray[index]; }

	int connectionCount() const override { return numConnections; }
	Connections& connection(int index) const override { return connections[index]; }

	int sectionCount() const override { return Blocks; }
	const Section& section(int index) const override { return signalling_block_sections[static_cast<std::size_t>(index)]; }

	const Route* route(int index) const override {
		if (index < 0 || index >= static_cast<int>(train_route.size()))
			return nullptr;
		return &train_route[static_cast<std::size_t>(index)];
	}

	const StationPlatform* platform(const std::string& stationId, const std::string& platformId) const override {
		const auto found = std::find_if(AllStationPlatforms.begin(), AllStationPlatforms.end(),
			[&stationId, &platformId](const StationPlatform& candidate) {
				return candidate.StationID == stationId && candidate.ID == platformId;
			});
		return found == AllStationPlatforms.end() ? nullptr : &*found;
	}

	bool passengerGui() const override { return initial_variables.PAX_GUI; }
};

} // namespace

std::unique_ptr<RuntimeGeometrySource> makeRuntimeGeometryFromGlobals() {
	return std::make_unique<RuntimeGeometryFromGlobals>();
}
