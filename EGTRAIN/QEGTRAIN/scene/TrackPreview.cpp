#include "scene/TrackPreview.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace fs = std::filesystem;

namespace {

bool parseTrackId(const std::string& name, int& id) {
	if (name.size() < 2 || name.front() != 'B')
		return false;

	const char* begin = name.data() + 1;
	const char* end = name.data() + name.size();
	const auto parsed = std::from_chars(begin, end, id);
	return parsed.ec == std::errc() && parsed.ptr == end;
}

} // namespace

TrackPreviewResult loadTrackPreview(const std::string& sceneDir) {
	TrackPreviewResult result;

	try {
		fs::path root = fs::path(sceneDir) / "legacy" / "TrackLines";
		if (!fs::is_directory(root))
			root = fs::path(sceneDir) / "legacy" / "Tracklines";
		if (!fs::is_directory(root)) {
			result.warnings.push_back("legacy track preview directory is missing");
			return result;
		}

		std::vector<std::pair<int, fs::path>> trackDirs;
		for (const auto& entry : fs::directory_iterator(root)) {
			if (!entry.is_directory())
				continue;

			int id = -1;
			if (!parseTrackId(entry.path().filename().string(), id)) {
				result.warnings.push_back("ignored nonnumeric track directory " + entry.path().filename().string());
				continue;
			}
			trackDirs.emplace_back(id, entry.path());
		}
		std::sort(trackDirs.begin(), trackDirs.end(), [](const auto& left, const auto& right) {
			return left.first < right.first;
		});

		for (const auto& trackDir : trackDirs) {
			TrackPreviewLine line;
			line.id = trackDir.first;
			const fs::path nodesPath = trackDir.second / "NodiCumPari.txt";
			std::ifstream nodes(nodesPath);
			if (!nodes) {
				result.warnings.push_back("could not read " + nodesPath.string());
				continue;
			}

			std::string row;
			while (std::getline(nodes, row)) {
				std::istringstream values(row);
				int nodeId = -1;
				TrackPreviewPoint point;
				if (!(values >> nodeId >> point.x >> point.y)
					|| !std::isfinite(point.x) || !std::isfinite(point.y)) {
					result.warnings.push_back("ignored malformed node row in B" + std::to_string(line.id));
					continue;
				}
				line.points.push_back(point);
			}

			if (line.points.size() < 2) {
				result.warnings.push_back("ignored track B" + std::to_string(line.id) + " with fewer than two points");
				continue;
			}
			result.lines.push_back(std::move(line));
		}

		const fs::path stationsPath = root / "Stations.txt";
		std::ifstream stations(stationsPath);
		std::string stationRow;
		while (stations && std::getline(stations, stationRow)) {
			if (!stationRow.empty() && stationRow.back() == '\r')
				stationRow.pop_back();
			const std::size_t tab = stationRow.find('\t');
			if (tab == std::string::npos || tab == 0 || tab + 1 >= stationRow.size())
				continue;
			TrackPreviewStation station;
			try {
				station.x = std::stod(stationRow.substr(0, tab));
			} catch (...) {
				result.warnings.push_back("ignored malformed station row");
				continue;
			}
			station.name = stationRow.substr(tab + 1);
			if (std::isfinite(station.x) && !station.name.empty())
				result.stations.push_back(std::move(station));
		}

		const fs::path connectionsPath = root / "Connections.txt";
		std::ifstream connections(connectionsPath);
		std::string row;
		while (connections && std::getline(connections, row)) {
			std::istringstream values(row);
			TrackPreviewConnection connection;
			if (!(values >> connection.firstTrackId >> connection.firstX
						 >> connection.secondTrackId >> connection.secondX)
				|| !std::isfinite(connection.firstX) || !std::isfinite(connection.secondX)) {
				result.warnings.push_back("ignored malformed connection row");
				continue;
			}
			result.connections.push_back(connection);
		}

		if (result.lines.empty())
			result.warnings.push_back("no valid legacy track preview is available");
	} catch (const fs::filesystem_error& error) {
		result.warnings.push_back(error.what());
	}

	return result;
}
