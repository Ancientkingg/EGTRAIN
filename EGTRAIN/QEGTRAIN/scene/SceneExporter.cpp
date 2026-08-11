#include "scene/SceneExporter.h"
#include "scene/SceneBundle.h"
#include "scene/SceneModel.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <cmath>
#include <regex>
#include <limits>
#include <cctype>

namespace fs = std::filesystem;

bool SceneExportResult::success() const {
	return wroteAll && !hasErrors(diagnostics);
}

static std::string sanitizeFilename(const std::string& name) {
	std::string res;
	for (char c : name) {
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-') {
			res += c;
		} else {
			res += '_';
		}
	}
	return res;
}

static bool isPositionedRouteEndpoint(const std::string& token) {
	size_t first = token.find('@');
	size_t last = token.rfind('@');
	if (first != 0 || last == std::string::npos || first == last
			|| last + 2 >= token.length() || token[last + 1] != '-')
		return false;
	std::string position = token.substr(last + 2);
	size_t i = 0;
	if (position[i] == '+' || position[i] == '-')
		i++;
	bool sawDigit = false;
	bool sawDot = false;
	for (; i < position.length(); ++i) {
		if (std::isdigit(static_cast<unsigned char>(position[i]))) {
			sawDigit = true;
		} else if (position[i] == '.' && !sawDot) {
			sawDot = true;
		} else {
			return false;
		}
	}
	return sawDigit;
}

static bool isSwitchTransitionRouteEntry(const std::string& entry) {
	size_t slash = entry.find('/');
	if (slash == std::string::npos || entry.find('/', slash + 1) != std::string::npos)
		return false;
	return isPositionedRouteEndpoint(entry.substr(0, slash)) && isPositionedRouteEndpoint(entry.substr(slash + 1));
}

static std::string formatNumber(double val) {
	if (std::floor(val) == val) {
		return std::to_string(static_cast<long long>(val));
	}
	std::ostringstream oss;
	oss.precision(17);
	oss << val;
	return oss.str();
}

static std::unordered_map<std::string, std::string> buildLegacyTrackIds(const SceneModel& scene) {
	std::unordered_set<std::string> used;
	for (const auto& track : scene.tracks) {
		if (track.id.size() > 1 && track.id.front() == 'B'
				&& std::all_of(track.id.begin() + 1, track.id.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
			used.insert(track.id);
	}

	std::unordered_map<std::string, std::string> ids;
	std::size_t fallback = 0;
	for (const auto& track : scene.tracks) {
		std::string id = track.id;
		if (id.size() < 2 || id.front() != 'B'
				|| !std::all_of(id.begin() + 1, id.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
			do id = "B" + std::to_string(fallback++);
			while (used.count(id) != 0);
			used.insert(id);
		}
		ids.emplace(track.id, id);
	}
	return ids;
}

static std::unordered_map<std::string, std::string> buildLegacyBlockIds(const SceneModel& scene,
		const std::unordered_map<std::string, std::string>& trackIds) {
	std::unordered_map<std::string, std::string> ids;
	for (const auto& track : scene.tracks) {
		std::size_t index = 0;
		for (const auto& block : scene.blocks) {
			if (block.trackId == track.id)
				ids.emplace(block.id, std::to_string(index++) + "-" + trackIds.at(track.id));
		}
	}
	return ids;
}

static std::string mapLegacyBlockReference(const std::string& reference,
		const std::unordered_map<std::string, std::string>& blockIds) {
	const auto direct = blockIds.find(reference);
	if (direct != blockIds.end())
		return direct->second;

	std::string mapped;
	std::size_t begin = 0;
	while (begin <= reference.size()) {
		const std::size_t slash = reference.find('/', begin);
		std::string part = reference.substr(begin,
				slash == std::string::npos ? std::string::npos : slash - begin);
		std::size_t idBegin = 0;
		std::size_t idEnd = part.find('@');
		if (!part.empty() && part.front() == '@') {
			idBegin = 1;
			idEnd = part.find('@', 1);
		}
		if (idEnd == std::string::npos)
			idEnd = part.size();
		const auto found = blockIds.find(part.substr(idBegin, idEnd - idBegin));
		if (found != blockIds.end())
			part.replace(idBegin, idEnd - idBegin, found->second);
		if (!mapped.empty())
			mapped += '/';
		mapped += part;
		if (slash == std::string::npos)
			break;
		begin = slash + 1;
	}
	return mapped;
}

// Older EGTRAIN variants read signalling levels from
// TrackLines/AreasCaseStudy.txt. Keep exported legacy directories compatible
// with them unless the scene already provides the file.
static void synthesizeSignallingAreas(const std::string& outDir, SceneExportResult& result) {
	auto addDiag = [&](SceneSeverity sev, const std::string& code, const std::string& msg, const std::string& file = "") {
		SceneDiagnostic d;
		d.severity = sev;
		d.code = code;
		d.message = msg;
		d.file = file;
		result.diagnostics.push_back(d);
	};

	fs::path areasFile = fs::path(outDir) / "TrackLines" / "AreasCaseStudy.txt";
	std::error_code ec;
	if (fs::exists(areasFile, ec))
		return;

	double minX = std::numeric_limits<double>::infinity();
	double maxX = -std::numeric_limits<double>::infinity();
	fs::path tracklinesDir = fs::path(outDir) / "TrackLines";
	if (fs::exists(tracklinesDir, ec) && fs::is_directory(tracklinesDir, ec)) {
		for (const auto& entry : fs::directory_iterator(tracklinesDir, ec)) {
			std::error_code dec;
			if (!entry.is_directory(dec) || dec)
				continue;
			std::ifstream nf(entry.path() / "NodiCumPari.txt");
			if (!nf)
				continue;
			std::string nline;
			while (std::getline(nf, nline)) {
				size_t tab1 = nline.find('\t');
				if (tab1 == std::string::npos)
					continue;
				size_t tab2 = nline.find('\t', tab1 + 1);
				if (tab2 == std::string::npos)
					continue;
				double x = std::atof(nline.substr(tab1 + 1, tab2 - tab1 - 1).c_str());
				minX = std::min(minX, x);
				maxX = std::max(maxX, x);
			}
		}
	}
	if (!(minX < maxX)) {
		addDiag(SceneSeverity::Info, "scene.export.info", "no trackline node data so no signalling areas file was generated");
		return;
	}

	std::ofstream out(areasFile);
	if (!out) {
		addDiag(SceneSeverity::Error, "scene.export.write", "Failed to write TrackLines/AreasCaseStudy.txt");
		result.wroteAll = false;
		return;
	}
	out << "Network\t" << formatNumber(minX - 1.0) << "\t" << formatNumber(maxX + 1.0) << "\t3\n";
	addDiag(SceneSeverity::Info, "scene.export.info", "signalling areas file covers the network at ETCS level 3");
}

static void synthesizeCanonicalInfrastructure(const SceneModel& scene, const std::string& outDir,
		const std::unordered_map<std::string, std::string>& outputTracks, SceneExportResult& result) {
	auto addDiag = [&](SceneSeverity sev, const std::string& code, const std::string& msg, const std::string& file = "") {
		SceneDiagnostic d;
		d.severity = sev;
		d.code = code;
		d.message = msg;
		d.file = file;
		result.diagnostics.push_back(d);
	};

	struct LegacyNode {
		std::string track;
		std::string id;
		double x = 0.0;
		double y = 0.0;
	};

	std::unordered_map<std::string, LegacyNode> nodes;
	const fs::path tracklinesDir = fs::path(outDir) / "TrackLines";

	auto numericSuffix = [](const std::string& value, const std::string& marker, const std::string& fallback) {
		const std::size_t pos = value.rfind(marker);
		if (pos == std::string::npos || pos + marker.size() == value.size())
			return fallback;
		const std::string suffix = value.substr(pos + marker.size());
		if (std::all_of(suffix.begin(), suffix.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
			return suffix;
		return fallback;
	};

	for (const SceneTrack& track : scene.tracks) {
		const std::string& outputTrack = outputTracks.at(track.id);

		std::unordered_set<std::string> usedNodeIds;
		std::size_t nodeIndex = 0;
		for (const auto& node : scene.nodes) {
			if (node.trackId != track.id)
				continue;
			std::string legacyId = numericSuffix(node.id, ".node.", std::to_string(nodeIndex));
			while (!usedNodeIds.insert(legacyId).second)
				legacyId = std::to_string(++nodeIndex);
			nodes[node.id] = {outputTrack, legacyId, node.xKm, node.yKm};
			++nodeIndex;
		}

		const fs::path trackDir = tracklinesDir / outputTrack;
		std::error_code ec;
		fs::create_directories(trackDir, ec);
		if (ec) {
			addDiag(SceneSeverity::Error, "scene.export.write", "Failed to create trackline directory: " + ec.message(),
					trackDir.string());
			result.wroteAll = false;
			continue;
		}

		auto writeIfMissing = [&](const fs::path& path, const auto& writer) {
			std::error_code existsEc;
			if (fs::exists(path, existsEc))
				return;
			std::ofstream out(path);
			if (!out) {
				addDiag(SceneSeverity::Error, "scene.export.write", "Failed to write canonical compatibility file", path.string());
				result.wroteAll = false;
				return;
			}
			writer(out);
		};

		writeIfMissing(trackDir / "NodiCumPari.txt", [&](std::ofstream& out) {
			for (const auto& node : scene.nodes) {
				if (node.trackId != track.id)
					continue;
				const auto found = nodes.find(node.id);
				if (found != nodes.end())
					out << found->second.id << "\t" << formatNumber(found->second.x) << "\t"
						<< formatNumber(found->second.y) << "\n";
			}
		});

		writeIfMissing(trackDir / "ArchiCumPari.txt", [&](std::ofstream& out) {
			std::size_t arcIndex = 0;
			for (const auto& arc : scene.arcs) {
				if (arc.trackId != track.id)
					continue;
				const auto from = nodes.find(arc.fromNodeId);
				const auto to = nodes.find(arc.toNodeId);
				if (from == nodes.end() || to == nodes.end()) {
					addDiag(SceneSeverity::Warning, "scene.export.ref", "Arc refers to an unknown canonical node", arc.id);
					++arcIndex;
					continue;
				}
				out << numericSuffix(arc.id, ".arc.", std::to_string(arcIndex)) << "\t"
					<< from->second.id << "\t" << to->second.id << "\t"
					<< formatNumber(arc.curvatureRadiusM) << "\t" << formatNumber(arc.gradientPercent) << "\t"
					<< formatNumber(arc.speedLimitMs) << "\n";
				++arcIndex;
			}
		});

		writeIfMissing(trackDir / "BlockCumPari.txt", [&](std::ofstream& out) {
			std::size_t blockIndex = 0;
			for (const auto& block : scene.blocks) {
				if (block.trackId != track.id)
					continue;
				out << blockIndex++ << "\t" << formatNumber(block.lengthKm) << "\n";
			}
		});
	}

	auto trackNumber = [](const std::string& track) {
		if (track.size() > 1 && track.front() == 'B'
				&& std::all_of(track.begin() + 1, track.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
			return track.substr(1);
		return track;
	};

	std::error_code ec;
	fs::create_directories(tracklinesDir, ec);
	if (ec) {
		addDiag(SceneSeverity::Error, "scene.export.write", "Failed to create TrackLines directory: " + ec.message(),
				tracklinesDir.string());
		result.wroteAll = false;
		return;
	}

	if (!scene.tracks.empty()) {
		const fs::path connectionsFile = tracklinesDir / "Connections.txt";
		if (!fs::exists(connectionsFile)) {
			std::ofstream out(connectionsFile);
			if (!out) {
				addDiag(SceneSeverity::Error, "scene.export.write", "Failed to write TrackLines/Connections.txt");
				result.wroteAll = false;
			} else {
				for (const auto& connection : scene.connections) {
					const auto from = nodes.find(connection.fromNodeId);
					const auto to = nodes.find(connection.toNodeId);
					if (from == nodes.end() || to == nodes.end()) {
						addDiag(SceneSeverity::Warning, "scene.export.ref", "Connection refers to an unknown canonical node", connection.id);
						continue;
					}
					out << trackNumber(from->second.track) << "\t" << formatNumber(from->second.x) << "\t"
						<< trackNumber(to->second.track) << "\t" << formatNumber(to->second.x);
					if (connection.hasSpeedLimit)
						out << "\t" << formatNumber(connection.speedLimitMs);
					out << "\n";
				}
			}
		}
	}

	if (!scene.stations.empty()) {
		const fs::path stationsFile = tracklinesDir / "Stations.txt";
		if (!fs::exists(stationsFile)) {
			std::ofstream out(stationsFile);
			if (!out) {
				addDiag(SceneSeverity::Error, "scene.export.write", "Failed to write TrackLines/Stations.txt");
				result.wroteAll = false;
			} else {
				std::vector<std::pair<double, std::string>> rows;
				for (const auto& station : scene.stations) {
					const std::string name = station.name.empty() ? station.id : station.name;
					std::vector<double> positions;
					for (const auto& platform : station.platforms) {
						for (const auto& nodeId : platform.nodeIds) {
							const auto node = nodes.find(nodeId);
							if (node != nodes.end())
								positions.push_back(node->second.x);
						}
					}
					if (positions.empty() && station.hasPosition && std::isfinite(station.positionKm))
						positions.push_back(station.positionKm);
					std::sort(positions.begin(), positions.end());
					positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
					for (const double position : positions)
						rows.emplace_back(position, name);
				}
				std::sort(rows.begin(), rows.end());
				for (const auto& [position, name] : rows)
					out << formatNumber(position) << "\t" << name << "\n";
			}
		}
	}
}

static std::string legacyStationName(const SceneModel& scene, const std::string& stationId) {
	for (const auto& station : scene.stations) {
		if (station.id == stationId)
			return station.name.empty() ? station.id : station.name;
	}
	return stationId;
}

static std::string legacyBoundaryToken(const std::string& token) {
	if (token.empty() || (token.front() == '@' && token.back() == '@') || isSwitchTransitionRouteEntry(token))
		return token;
	return "@" + token + "@";
}

static bool passengerWindowToken(double start, double end, std::string& token) {
	if (!std::isfinite(start) || !std::isfinite(end) || start < 0.0 || end < start)
		return false;
	const double hourValue = std::floor(start / 3600.0);
	if (hourValue > static_cast<double>(std::numeric_limits<long long>::max()))
		return false;
	const long long hour = static_cast<long long>(hourValue);
	const double hourStart = static_cast<double>(hour) * 3600.0;
	if (start == hourStart && end == start) {
		token = std::to_string(hour);
		return true;
	}
	if (start == hourStart && end == start + 1799.0) {
		token = formatNumber(static_cast<double>(hour) + 0.25);
		return true;
	}
	if (start == hourStart + 1800.0 && end == start + 1799.0) {
		token = formatNumber(static_cast<double>(hour) + 0.75);
		return true;
	}
	return false;
}

static void synthesizeCanonicalCompatibility(const SceneModel& scene, const std::string& outDir,
		const std::unordered_map<std::string, int>& routeIndices,
		const std::unordered_map<std::string, std::string>& blockIds, SceneExportResult& result) {
	auto addError = [&](const std::string& code, const std::string& message, const std::string& file = "") {
		SceneDiagnostic d;
		d.severity = SceneSeverity::Error;
		d.code = code;
		d.message = message;
		d.file = file;
		result.diagnostics.push_back(d);
		result.wroteAll = false;
	};
	const auto addUnsupported = [&](const std::string& message, const std::string& file = "") {
		addError("scene.export.unsupported", message, file);
	};
	const auto addWriteError = [&](const std::string& message, const std::string& file = "") {
		addError("scene.export.write", message, file);
	};

	auto writeIfMissing = [&](const fs::path& path, const auto& writer, bool needed) {
		if (!needed)
			return;
		std::error_code ec;
		if (fs::exists(path, ec))
			return;
		if (ec) {
			addWriteError("Could not inspect compatibility file: " + ec.message(), path.string());
			return;
		}
		fs::create_directories(path.parent_path(), ec);
		if (ec) {
			addWriteError("Could not create compatibility directory: " + ec.message(), path.parent_path().string());
			return;
		}
		std::ofstream out(path);
		if (!out) {
			addWriteError("Failed to write compatibility file", path.string());
			return;
		}
		writer(out);
	};

	std::vector<std::pair<int, std::string>> corridorRows;
	for (const auto& route : scene.routes) {
		if (!route.hasCorridor)
			continue;
		const auto routeIt = routeIndices.find(route.id);
		if (routeIt == routeIndices.end()) {
			addUnsupported("Route corridor refers to an unexported route: " + route.id, "signalling.json");
			continue;
		}
		if (route.corridor.find_first_of("\r\n\t") != std::string::npos) {
			addUnsupported("Route corridor contains a line or tab delimiter: " + route.id, "signalling.json");
			continue;
		}
		corridorRows.emplace_back(routeIt->second, route.corridor);
	}
	writeIfMissing(fs::path(outDir) / "GUI" / "caseStudyRouteCorridors.txt",
			[&](std::ofstream& out) {
				for (const auto& row : corridorRows)
					out << row.first << "\t" << row.second << "\n";
			}, !corridorRows.empty());

	writeIfMissing(fs::path(outDir) / "GUI" / "singleTrackLimits.txt",
			[&](std::ofstream& out) {
				for (const auto& restriction : scene.singleTrackRestrictions) {
					out << legacyBoundaryToken(mapLegacyBlockReference(restriction.startBlock, blockIds)) << "\t"
						<< legacyBoundaryToken(mapLegacyBlockReference(restriction.endBlock, blockIds)) << "\t"
						<< mapLegacyBlockReference(restriction.protectedStartBlock, blockIds) << "\t"
						<< mapLegacyBlockReference(restriction.protectedEndBlock, blockIds) << "\n";
				}
			}, !scene.singleTrackRestrictions.empty());

	writeIfMissing(fs::path(outDir) / "GUI" / "stationBoundarySections.txt",
			[&](std::ofstream& out) {
				for (const auto& boundary : scene.stationBoundaries) {
					out << legacyBoundaryToken(mapLegacyBlockReference(boundary.entranceBlock, blockIds)) << "\t";
					if (boundary.hasExitBlock)
						out << legacyBoundaryToken(mapLegacyBlockReference(boundary.exitBlock, blockIds));
					out << "\t" << (boundary.direction ? 1 : 0) << "\n";
				}
			}, !scene.stationBoundaries.empty());

	if (scene.passengers.empty())
		return;
	const fs::path dasPath = fs::path(outDir) / "Passengers" / "DAS_FrenchCaseStudy.csv";
	const fs::path routeChoicePath = fs::path(outDir) / "Passengers" / "RouteChoiceFC_EQ1.csv";
	std::error_code dasEc, routeChoiceEc;
	const bool hasDas = fs::exists(dasPath, dasEc);
	const bool hasRouteChoice = fs::exists(routeChoicePath, routeChoiceEc);
	if (dasEc || routeChoiceEc) {
		addWriteError("Could not inspect legacy passenger compatibility files",
				(dasEc ? dasPath : routeChoicePath).string());
		return;
	}
	if (hasDas && hasRouteChoice)
		return;
	if (hasDas != hasRouteChoice) {
		addUnsupported("Existing legacy passenger data is missing its paired CSV",
				(hasDas ? routeChoicePath : dasPath).string());
		return;
	}

	std::unordered_set<std::string> passengerIds;
	std::map<std::pair<std::string, std::string>, std::pair<int, bool>> journeyGroups;
	std::unordered_map<std::string, const SceneService*> servicesById;
	std::unordered_map<std::string, int> operatingCodeCounts;
	for (const auto& service : scene.services) {
		servicesById.emplace(service.id, &service);
		const std::string code = service.operatingCode.empty() ? service.id : service.operatingCode;
		++operatingCodeCounts[code];
	}

	struct DasRow {
		std::vector<std::string> fields;
	};
	struct RouteChoiceRow {
		std::string person;
		std::string destination;
		std::vector<std::string> transfers;
		std::vector<std::string> services;
	};
	std::vector<DasRow> dasRows;
	std::vector<RouteChoiceRow> routeChoiceRows;
	std::size_t maxLegs = 0;
	bool valid = true;
	std::size_t tripNo = 1;
	for (const auto& passenger : scene.passengers) {
		if (passenger.id.empty() || passenger.id.find_first_of(",\r\n") != std::string::npos)
			{ addUnsupported("Passenger id cannot be represented in legacy CSV", "passengers.json"); valid = false; }
		if (!passengerIds.insert(passenger.id).second)
			{ addUnsupported("Duplicate passenger id cannot be represented in legacy CSV: " + passenger.id, "passengers.json"); valid = false; }
		if (passenger.journeys.empty()) {
			addUnsupported("Passenger has no journeys and cannot be represented in legacy CSV: " + passenger.id, "passengers.json");
			valid = false;
		}
		std::unordered_set<std::string> journeyIds;
		for (const auto& journey : passenger.journeys) {
			const std::string prefix = passenger.id + ":";
			std::string suffix = journey.id.rfind(prefix, 0) == 0 ? journey.id.substr(prefix.size()) : journey.id;
			if (suffix.empty() || suffix.find_first_of(",\r\n") != std::string::npos) {
				addUnsupported("Passenger journey id cannot be represented in legacy CSV: " + journey.id, "passengers.json");
				valid = false;
			}
			if (!journeyIds.insert(journey.id).second) {
				addUnsupported("Duplicate passenger journey id cannot be represented in legacy CSV: " + journey.id, "passengers.json");
				valid = false;
			}

			const std::string origin = legacyStationName(scene, journey.originStationId);
			const std::string destination = legacyStationName(scene, journey.destinationStationId);
			if (journey.activity.find_first_of(",\r\n") != std::string::npos
					|| origin.find_first_of(",\r\n") != std::string::npos
					|| destination.find_first_of(",\r\n") != std::string::npos) {
				addUnsupported("Passenger CSV field contains a comma or newline: " + journey.id, "passengers.json");
				valid = false;
			}
			std::string arrivalToken;
			std::string departureToken;
			if (!passengerWindowToken(journey.plannedArrivalStartSeconds, journey.plannedArrivalEndSeconds, arrivalToken)) {
				addUnsupported("Passenger arrival window is not representable by legacy DAS buckets: " + journey.id, "passengers.json");
				valid = false;
			}
			if (!passengerWindowToken(journey.plannedDepartureStartSeconds, journey.plannedDepartureEndSeconds, departureToken)) {
				addUnsupported("Passenger departure window is not representable by legacy DAS buckets: " + journey.id, "passengers.json");
				valid = false;
			}
			dasRows.push_back({{
				std::to_string(tripNo++), passenger.id, "1", journey.activity, suffix, journey.activity,
				destination, "", "PT", "TRUE", arrivalToken, "", origin, "", departureToken, "1"}});

			const auto groupKey = std::make_pair(passenger.id, destination);
			auto& group = journeyGroups[groupKey];
			++group.first;
			group.second = group.second || !journey.legs.empty();
			if (journey.legs.empty())
				continue;
			if (journey.legs.size() > maxLegs)
				maxLegs = journey.legs.size();

			RouteChoiceRow routeRow{passenger.id, destination, {}, {}};
			for (std::size_t i = 0; i < journey.legs.size(); ++i) {
				const auto& leg = journey.legs[i];
				if (i > 0 && leg.originStationId != journey.legs[i - 1].destinationStationId) {
					addUnsupported("Passenger legs are not contiguous: " + journey.id, "passengers.json");
					valid = false;
				}
				const std::string legOrigin = legacyStationName(scene, leg.originStationId);
				const std::string legDestination = legacyStationName(scene, leg.destinationStationId);
				if (legOrigin.find_first_of(",\r\n") != std::string::npos
						|| legDestination.find_first_of(",\r\n") != std::string::npos) {
					addUnsupported("Passenger CSV field contains a comma or newline: " + journey.id, "passengers.json");
					valid = false;
				}
				const auto serviceIt = servicesById.find(leg.serviceId);
				if (serviceIt == servicesById.end()) {
					addUnsupported("Passenger leg references unknown service: " + leg.serviceId, "passengers.json");
					valid = false;
					continue;
				}
				const std::string code = serviceIt->second->operatingCode.empty() ? serviceIt->second->id : serviceIt->second->operatingCode;
				if (code.find_first_of(",\r\n") != std::string::npos || operatingCodeCounts[code] != 1 || leg.occurrence < 1) {
					addUnsupported("Passenger leg service token is ambiguous or invalid: " + leg.serviceId, "passengers.json");
					valid = false;
				}
				if (i + 1 < journey.legs.size())
					routeRow.transfers.push_back(legDestination);
				routeRow.services.push_back(code + "-" + std::to_string(leg.occurrence));
			}
			routeChoiceRows.push_back(std::move(routeRow));
		}
	}
	for (const auto& [key, group] : journeyGroups) {
		if (group.first > 1 && group.second) {
			addUnsupported("Duplicate passenger+destination journeys with legs are ambiguous: " + key.first, "passengers.json");
			valid = false;
		}
	}
	if (!valid)
		return;
	const std::size_t maxTransfers = maxLegs == 0 ? 0 : maxLegs - 1;

	writeIfMissing(dasPath, [&](std::ofstream& out) {
		out << "trip_id,person_id,tour_no,tour_type,journey_no,activity,stop_location,stop_zone,stop_mode,primary_stop,arrival_time,departure_time,prev_stop_location,prev_stop_zone,prev_stop_departure_time,pid\n";
		for (const auto& row : dasRows) {
			for (std::size_t i = 0; i < row.fields.size(); ++i)
				out << (i == 0 ? "" : ",") << row.fields[i];
			out << "\n";
		}
	}, true);
	writeIfMissing(routeChoicePath, [&](std::ofstream& out) {
		out << "person_id,destination,nb_transfers";
		for (std::size_t i = 0; i < maxTransfers; ++i)
			out << ",Transfer_N" << (i + 1);
		for (std::size_t i = 0; i < maxLegs; ++i)
			out << ",r_service_lines_id" << (i + 1);
		out << "\n";
		for (const auto& row : routeChoiceRows) {
			out << row.person << "," << row.destination << "," << row.transfers.size();
			for (std::size_t i = 0; i < maxTransfers; ++i)
				out << "," << (i < row.transfers.size() ? row.transfers[i] : "Null");
			for (std::size_t i = 0; i < maxLegs; ++i)
				out << "," << (i < row.services.size() ? row.services[i] : "Null");
			out << "\n";
		}
	}, true);
}

static void synthesizeGuiLayout(const std::string& outDir, SceneExportResult& result) {
	auto addDiag = [&](SceneSeverity sev, const std::string& code, const std::string& msg, const std::string& file = "") {
		SceneDiagnostic d;
		d.severity = sev;
		d.code = code;
		d.message = msg;
		d.file = file;
		result.diagnostics.push_back(d);
	};

	// the two files describe one layout; a half-generated pair would assign
	// regions that contradict the provided coordinates, so skip on either
	fs::path providedSc = fs::path(outDir) / "GUI" / "StationsCoord.txt";
	fs::path providedTd = fs::path(outDir) / "GUI" / "caseStudyTrackData.txt";
	if (fs::exists(providedSc) || fs::exists(providedTd)) {
		addDiag(SceneSeverity::Info, "scene.export.info", "scene-provided GUI layout is used");
		return;
	}

	fs::path stationsFile = fs::path(outDir) / "TrackLines" / "Stations.txt";
	std::ifstream sf(stationsFile);
	if (!sf) {
		addDiag(SceneSeverity::Info, "scene.export.info", "no station anchors so no GUI layout was generated");
		return;
	}

	struct StationEntry {
		double origX = 0;
		std::string name;
		std::vector<int> regions;
		std::vector<double> regionXs;
		double canonical = 0.0;
	};

	std::vector<StationEntry> stations;
	std::string line;
	while (std::getline(sf, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.empty()) continue;

		size_t tab = line.find('\t');
		if (tab != std::string::npos) {
			StationEntry se;
			se.origX = std::atof(line.substr(0, tab).c_str());
			se.name = line.substr(tab + 1);
			stations.push_back(se);
		}
	}

	if (stations.empty()) {
		addDiag(SceneSeverity::Info, "scene.export.info", "no station anchors so no GUI layout was generated");
		return;
	}

	std::map<int, std::vector<double>> tracklineNodes;
	fs::path tracklinesDir = fs::path(outDir) / "TrackLines";
	if (fs::exists(tracklinesDir) && fs::is_directory(tracklinesDir)) {
		std::error_code ec;
		for (const auto& entry : fs::directory_iterator(tracklinesDir, ec)) {
			std::error_code dec;
			if (entry.is_directory(dec) && !dec) {
				std::string dirname = entry.path().filename().string();
				if (dirname.size() > 1 && dirname[0] == 'B') {
					int n = -1;
					std::string num = dirname.substr(1);
					if (num.size() <= 6 && std::all_of(num.begin(), num.end(), [](char value) {
							return std::isdigit(static_cast<unsigned char>(value));
						}))
						n = std::stoi(num);
					if (n >= 0) {
						fs::path nodiFile = entry.path() / "NodiCumPari.txt";
						std::ifstream nf(nodiFile);
						if (nf) {
							std::string nline;
							while (std::getline(nf, nline)) {
								size_t tab1 = nline.find('\t');
								if (tab1 != std::string::npos) {
									size_t tab2 = nline.find('\t', tab1 + 1);
									if (tab2 != std::string::npos) {
										double x = std::atof(nline.substr(tab1 + 1, tab2 - tab1 - 1).c_str());
										tracklineNodes[n].push_back(x);
									}
								}
							}
						}
					}
				}
			}
		}
	}

	if (tracklineNodes.empty()) {
		addDiag(SceneSeverity::Info, "scene.export.info", "no trackline node data so no GUI layout was generated");
		return;
	}

	for (auto& se : stations) {
		for (const auto& [n, nodes] : tracklineNodes) {
			for (double nx : nodes) {
				if (nx == se.origX) { // exact equality
					se.regions.push_back(n);
					se.regionXs.push_back(se.origX);
					break;
				}
			}
		}
		if (se.regions.empty()) {
			addDiag(SceneSeverity::Warning, "scene.export.adjusted", "Station anchor matches no trackline", se.name);
		}
	}

	int minR = -1;
	std::vector<int> sortedTracklines;
	for (const auto& [n, nodes] : tracklineNodes) {
		sortedTracklines.push_back(n);
	}
	std::sort(sortedTracklines.begin(), sortedTracklines.end());

	for (int n : sortedTracklines) {
		bool hasBound = false;
		for (const auto& se : stations) {
			if (std::find(se.regions.begin(), se.regions.end(), n) != se.regions.end()) {
				hasBound = true;
				break;
			}
		}
		if (hasBound) {
			minR = n;
			break;
		}
	}

	double minX_R = std::numeric_limits<double>::infinity();
	if (minR != -1) {
		for (const auto& se : stations) {
			if (std::find(se.regions.begin(), se.regions.end(), minR) != se.regions.end()) {
				if (se.origX < minX_R) minX_R = se.origX;
			}
		}
	}

	// anchor the reference-trackline entries first; the cross-reference pass
	// below must only read canonical values that are already final
	for (auto& se : stations) {
		if (minR != -1 && std::find(se.regions.begin(), se.regions.end(), minR) != se.regions.end()) {
			se.canonical = se.origX - minX_R;
		}
	}

	for (auto& se : stations) {
		if (minR != -1 && std::find(se.regions.begin(), se.regions.end(), minR) != se.regions.end()) {
			continue;
		} else if (!se.regions.empty()) {
			bool foundMatch = false;
			if (minR != -1) {
				for (const auto& other : stations) {
					if (other.name == se.name && std::find(other.regions.begin(), other.regions.end(), minR) != other.regions.end()) {
						se.canonical = other.canonical;
						foundMatch = true;
						break;
					}
				}
			}
			if (!foundMatch) {
				int ownMinR = se.regions.front();
				for (int r : se.regions) {
					if (r < ownMinR) ownMinR = r;
				}

				double minX_ownR = std::numeric_limits<double>::infinity();
				for (const auto& other : stations) {
					if (std::find(other.regions.begin(), other.regions.end(), ownMinR) != other.regions.end()) {
						if (other.origX < minX_ownR) minX_ownR = other.origX;
					}
				}
				se.canonical = se.origX - minX_ownR;
				addDiag(SceneSeverity::Info, "scene.export.info", "Station name has no counterpart on the reference trackline", se.name);
			}
		} else {
			se.canonical = 0;
		}
	}

	std::error_code ec;
	fs::create_directories(fs::path(outDir) / "GUI", ec);

	std::ofstream sc(providedSc);
	if (sc) {
		for (const auto& se : stations) {
			sc << se.name << "\t1\t" << formatNumber(se.canonical / 100.0) << "\t";
			for (size_t i = 0; i < se.regions.size(); ++i) {
				sc << se.regions[i] << (i + 1 == se.regions.size() ? "" : ",");
			}
			sc << "\t";
			for (size_t i = 0; i < se.regionXs.size(); ++i) {
				sc << formatNumber(se.regionXs[i]) << (i + 1 == se.regionXs.size() ? "" : ",");
			}
			sc << "\n";
		}
	}

	std::ofstream td(providedTd);
	if (td) {
		for (const auto& [n, nodes] : tracklineNodes) {
			td << n << "\t" << n << "\t" << n << "\n";
		}
	}
}

SceneExportResult exportLegacyScene(const std::string& sceneDir, const std::string& outDir) {
	SceneExportResult result;

	auto addDiag = [&](SceneSeverity sev, const std::string& code, const std::string& msg, const std::string& file = "") {
		SceneDiagnostic d;
		d.severity = sev;
		d.code = code;
		d.message = msg;
		d.file = file;
		result.diagnostics.push_back(d);
	};

	SceneLoadResult loadRes = loadScenePath(sceneDir);
	if (hasErrors(loadRes.diagnostics)) {
		result.diagnostics = loadRes.diagnostics;
		return result;
	}
	// Also include load warnings
	for (const auto& d : loadRes.diagnostics) {
		if (d.severity != SceneSeverity::Error) {
			result.diagnostics.push_back(d);
		}
	}

	const SceneModel& scene = loadRes.scene;
	const auto legacyTrackIds = buildLegacyTrackIds(scene);
	const auto legacyBlockIds = buildLegacyBlockIds(scene, legacyTrackIds);

	std::error_code ec;
	fs::create_directories(outDir, ec);
	if (ec) {
		addDiag(SceneSeverity::Error, "scene.export.write", "Could not create outDir", outDir);
		return result;
	}

	result.wroteAll = true;

	for (const char* sub : {"Trains", "TimeTable", "TrainData", "Routes"}) {
		fs::create_directories(fs::path(outDir) / sub, ec);
		if (ec) {
			addDiag(SceneSeverity::Error, "scene.export.write", std::string("Could not create ") + sub + ": " + ec.message());
			result.wroteAll = false;
			return result;
		}
	}

	// Build route indices. All ids matching route<N> reuse N (round-trip
	// fidelity); all non-conforming ids get sequential indices; a mix would
	// silently rename some routes on re-import, so it is rejected.
	std::unordered_map<std::string, int> routeIndices;
	size_t conforming = 0;
	for (const auto& r : scene.routes) {
		if (r.id.length() > 5 && r.id.substr(0, 5) == "route") {
			std::string numStr = r.id.substr(5);
			if (!numStr.empty() && numStr.length() <= 9 && std::all_of(numStr.begin(), numStr.end(), [](char value) {
					return std::isdigit(static_cast<unsigned char>(value));
				})) {
				routeIndices[r.id] = std::stoi(numStr);
				++conforming;
			}
		}
	}
	if (conforming != scene.routes.size()) {
		if (conforming > 0) {
			addDiag(SceneSeverity::Error, "scene.export.unsupported",
					"Route ids mix the route<N> pattern with other names; rename them consistently", "signalling.json");
			return result;
		}
		routeIndices.clear();
		int k = 0;
		for (const auto& r : scene.routes) {
			routeIndices[r.id] = k++;
		}
	}

	// Write Routes
	for (const auto& r : scene.routes) {
		int idx = routeIndices[r.id];
		std::string fname = "Route" + std::to_string(idx) + ".txt";
		std::ofstream rf(fs::path(outDir) / "Routes" / fname);
		if (!rf) {
			addDiag(SceneSeverity::Error, "scene.export.write", "Failed to write route file", fname);
			result.wroteAll = false;
			continue;
		}
		for (const auto& b : r.blocks) {
			const std::string mapped = mapLegacyBlockReference(b, legacyBlockIds);
			if (isSwitchTransitionRouteEntry(b)) {
				rf << mapped << "\n";
			} else {
				rf << "@" << mapped << "@\n";
			}
		}
	}

	// Process Services
	std::vector<std::string> trainFiles;
	for (const auto& svc : scene.services) {
		std::string sId = svc.id;
		std::string sFile = sanitizeFilename(sId);
		if (sFile.empty()) {
			addDiag(SceneSeverity::Error, "scene.export.unsupported", "Service id sanitizes to an empty file name", sId);
			result.wroteAll = false;
			continue;
		}
		std::string fname = sFile + ".txt";
		int rename = 2;
		while (std::find(trainFiles.begin(), trainFiles.end(), fname) != trainFiles.end()) {
			fname = sFile + "_" + std::to_string(rename++) + ".txt";
		}
		if (fname != sFile + ".txt") {
			addDiag(SceneSeverity::Warning, "scene.export.adjusted",
					"Service file name collided after sanitizing; using " + fname, svc.id);
		}
		trainFiles.push_back(fname);

		double entryTime = 0.0;
		if (svc.hasEntryTime) {
			entryTime = svc.entryTimeSeconds;
		} else if (!svc.stops.empty()) {
			entryTime = svc.stops[0].plannedDepartureSeconds;
		}

		double headway = 99999999.0;
		if (svc.hasRepeat) {
			headway = svc.headwaySeconds;
		}

		auto rIt = routeIndices.find(svc.route);
		if (rIt == routeIndices.end()) {
			addDiag(SceneSeverity::Error, "scene.export.ref", "Service references unknown route " + svc.route, svc.id);
			result.wroteAll = false;
			trainFiles.pop_back();
			continue;
		}
		int rIndex = rIt->second;

		SceneCompositionRuntime composition;
		std::string compositionDiagnostic;
		if (!buildSceneComposition(scene, svc.composition, composition, compositionDiagnostic)) {
			addDiag(SceneSeverity::Error, "scene.export.ref", compositionDiagnostic, svc.id);
			result.wroteAll = false;
			trainFiles.pop_back();
			continue;
		}

		std::string dataPathRel;
		std::string tracPathRel;
		const SceneTrainPhysical& combinedPhysical = composition.physical;
		const std::vector<std::array<double, 5>>& combinedTraction = composition.tractionCurve;

		if (composition.units.size() == 1) {
			const SceneTrainUnit* u = composition.units[0];
			dataPathRel = u->sourceDataFile;
			if (dataPathRel.empty()) {
				dataPathRel = "TrainData/" + u->id + ".txt";
			} else {
				// Keep the basename and put in TrainData/
				dataPathRel = "TrainData/" + fs::path(dataPathRel).filename().string();
			}
			tracPathRel = u->sourceTractionFile;
			if (tracPathRel.empty()) {
				tracPathRel = "TrainData/T_" + u->id + ".txt";
			} else {
				tracPathRel = "TrainData/" + fs::path(tracPathRel).filename().string();
			}
		} else {
			dataPathRel = "TrainData/" + sanitizeFilename(svc.composition) + ".txt";
			tracPathRel = "TrainData/T_" + sanitizeFilename(svc.composition) + ".txt";
		}

		std::string ttPathRel = "TimeTable/" + fname;

		// Write unit files if not written
		std::ofstream df(fs::path(outDir) / dataPathRel);
		if (df) {
			df << formatNumber(combinedPhysical.mass_of_traction_unit_kg) << "\t"
			   << formatNumber(combinedPhysical.mass_of_a_wagon_kg) << "\t"
			   << formatNumber(combinedPhysical.number_of_wagons) << "\t"
			   << formatNumber(combinedPhysical.max_speed_ms) << "\t"
			   << formatNumber(combinedPhysical.max_deceleration_ms2) << "\t"
			   << formatNumber(combinedPhysical.frontal_area_m2) << "\t"
			   << formatNumber(combinedPhysical.resistance_coefficient) << "\t"
			   << formatNumber(combinedPhysical.jerk_ms3) << "\t"
			   << formatNumber(combinedPhysical.length_m) << "\n";
		} else {
			addDiag(SceneSeverity::Error, "scene.export.write", "Failed to write train data file", dataPathRel);
			result.wroteAll = false;
		}

		std::ofstream tf(fs::path(outDir) / tracPathRel);
		if (tf) {
			for (const auto& row : combinedTraction) {
				tf << formatNumber(row[0]) << "\t"
				   << formatNumber(row[1]) << "\t"
				   << formatNumber(row[2]) << "\t"
				   << formatNumber(row[3]) << "\t"
				   << formatNumber(row[4]) << "\n";
			}
		} else {
			addDiag(SceneSeverity::Error, "scene.export.write", "Failed to write traction data file", tracPathRel);
			result.wroteAll = false;
		}

		std::ofstream ttf(fs::path(outDir) / ttPathRel);
		if (ttf) {
			for (const auto& stop : svc.stops) {
				if (stop.stationId.find_first_of(" \t\r\n") != std::string::npos) {
					addDiag(SceneSeverity::Error, "scene.export.unsupported", "Station ID contains whitespace: " + stop.stationId, svc.id);
					result.wroteAll = false;
				}
				std::string stId = stop.stationId;
				std::string arr = stop.hasPlannedArrival ? formatNumber(stop.plannedArrivalSeconds) : "-1";
				std::string dep = stop.hasPlannedDeparture ? formatNumber(stop.plannedDepartureSeconds) : "-1";
				ttf << stId << "\t" << formatNumber(stop.dwellSeconds) << "\t" << arr << "\t" << dep << "\n";
			}
		} else {
			addDiag(SceneSeverity::Error, "scene.export.write", "Failed to write timetable file", ttPathRel);
			result.wroteAll = false;
		}

		std::ofstream trf(fs::path(outDir) / "Trains" / fname);
		if (trf) {
			trf << (svc.operatingCode.empty() ? svc.id : svc.operatingCode) << "\n"
				<< formatNumber(entryTime) << "\n"
				<< formatNumber(headway) << "\n"
				<< rIndex << "\n"
				<< "/" << dataPathRel << "\n"
				<< "/" << tracPathRel << "\n"
				<< "/" << ttPathRel << "\n";
		} else {
			addDiag(SceneSeverity::Error, "scene.export.write", "Failed to write Trains file", fname);
			result.wroteAll = false;
		}
	}

	std::ofstream tnf(fs::path(outDir) / "trainNames.txt");
	if (tnf) {
		for (const auto& tf : trainFiles) {
			tnf << tf << "\n";
		}
	} else {
		addDiag(SceneSeverity::Error, "scene.export.write", "Failed to write trainNames.txt");
		result.wroteAll = false;
	}

	const auto& incidents = defaultScenarioIncidents(scene);
	if (!incidents.empty()) {
		std::unordered_set<std::string> routeBlockTokens;
		for (const auto& r : scene.routes) {
			for (const auto& b : r.blocks) {
				routeBlockTokens.insert(b);
			}
		}
		std::unordered_map<std::string, std::string> serviceOperatingCodes;
		for (const auto& svc : scene.services) {
			serviceOperatingCodes[svc.id] = svc.operatingCode.empty() ? svc.id : svc.operatingCode;
		}

		std::ofstream inf(fs::path(outDir) / "Incidents.txt");
		if (inf) {
			for (const auto& inc : incidents) {
				if (inc.type != "signal_failure" && inc.type != "train_breakdown") {
					addDiag(SceneSeverity::Warning, "scene.export.adjusted",
							"Incident type " + inc.type + " is not supported and was skipped", inc.id);
					continue;
				}
				if (inc.type == "train_breakdown"
						&& (inc.hasOccurrence || inc.occurrence != 1
							|| inc.hasReducedSpeed || inc.reducedSpeedKmh != 0.0
							|| inc.terminateAtDestination
							|| (!inc.hasEndSeconds && inc.endSeconds == 0.0))) {
					addDiag(SceneSeverity::Warning, "scene.export.compatibility",
							"Enhanced breakdown " + inc.id + " was skipped because the legacy four-column exporter cannot represent its occurrence, speed, recovery, or destination semantics",
							inc.id);
					continue;
				}
				std::string target = inc.target;
				if (inc.type == "signal_failure") {
					const auto signal = std::find_if(scene.signals.begin(), scene.signals.end(),
							[&inc](const SceneSignal& candidate) { return candidate.id == inc.target; });
					if (signal != scene.signals.end() && !signal->protectedSection.empty())
						target = signal->protectedSection;
					if (target.find('/') != std::string::npos) {
						addDiag(SceneSeverity::Warning, "scene.export.compatibility",
								"Signal failure " + inc.id + " was skipped because legacy incidents cannot target compound sections",
								inc.id);
						continue;
					}
					if (target.size() > 2 && target.front() == '@' && target.back() == '@')
						target = target.substr(1, target.size() - 2);
					const bool routeContainsTarget = routeBlockTokens.find(target) != routeBlockTokens.end()
							|| routeBlockTokens.find("@" + target + "@") != routeBlockTokens.end();
					if (!routeContainsTarget)
						addDiag(SceneSeverity::Warning, "scene.export.adjusted",
								"Signal failure target " + target + " matches no route block so the failure will have no effect", inc.id);
					target = mapLegacyBlockReference(target, legacyBlockIds);
				} else if (inc.type == "train_breakdown") {
					const auto service = serviceOperatingCodes.find(inc.target);
					if (service == serviceOperatingCodes.end()) {
						addDiag(SceneSeverity::Warning, "scene.export.adjusted",
								"Service id " + inc.target + " matches no service so the breakdown will have no effect", inc.id);
					} else {
						target = service->second;
					}
				}
				inf << inc.type << "\t" << target << "\t" << formatNumber(inc.startSeconds) << "\t" << formatNumber(inc.endSeconds) << "\n";
			}
		} else {
			addDiag(SceneSeverity::Error, "scene.export.write", "Failed to write Incidents.txt");
			result.wroteAll = false;
		}
	}

	// Manual recursive walk copying legacy/
	fs::path legacyDir = fs::path(sceneDir) / "legacy";
	if (!fs::exists(legacyDir) || !fs::is_directory(legacyDir)) {
		addDiag(SceneSeverity::Info, "scene.export.info", "scene has no legacy passthrough data; compatibility files were generated from canonical data");
	} else {
		auto copyLegacyFile = [&](const fs::path& srcPath) {
			std::string relStr = srcPath.lexically_relative(legacyDir).string();
			std::replace(relStr.begin(), relStr.end(), '\\', '/');

			// Apply casing fixes on top-level folder names
			size_t slashPos = relStr.find('/');
			if (slashPos != std::string::npos) {
				std::string top = relStr.substr(0, slashPos);
				std::string topLower = top;
				std::transform(topLower.begin(), topLower.end(), topLower.begin(), ::tolower);
				if (topLower == "tracklines")
					top = "TrackLines";
				else if (topLower == "timetable")
					top = "TimeTable";
				relStr = top + relStr.substr(slashPos);
			} else {
				std::string topLower = relStr;
				std::transform(topLower.begin(), topLower.end(), topLower.begin(), ::tolower);
				if (topLower == "tracklines")
					relStr = "TrackLines";
				else if (topLower == "timetable")
					relStr = "TimeTable";
			}

			fs::path dstPath = fs::path(outDir) / relStr;
			std::error_code cpec;
			if (!fs::exists(dstPath, cpec)) {
				fs::create_directories(dstPath.parent_path(), cpec);
				fs::copy_file(srcPath, dstPath, fs::copy_options::overwrite_existing, cpec);
				if (cpec) {
					addDiag(SceneSeverity::Error, "scene.export.write", "Failed to copy legacy file " + relStr + ": " + cpec.message());
					result.wroteAll = false;
				}
			}
		};

		fs::recursive_directory_iterator it(legacyDir, ec), endIt;
		while (!ec && it != endIt) {
			if (it->is_regular_file(ec) && !ec) {
				copyLegacyFile(it->path());
			}
			it.increment(ec);
		}
		if (ec) {
			addDiag(SceneSeverity::Error, "scene.export.write", "Failed while walking legacy data: " + ec.message());
			result.wroteAll = false;
		}
	}

	synthesizeCanonicalInfrastructure(scene, outDir, legacyTrackIds, result);
	synthesizeCanonicalCompatibility(scene, outDir, routeIndices, legacyBlockIds, result);

	if (result.success()) {
		synthesizeSignallingAreas(outDir, result);
		synthesizeGuiLayout(outDir, result);
	}

	return result;
}
