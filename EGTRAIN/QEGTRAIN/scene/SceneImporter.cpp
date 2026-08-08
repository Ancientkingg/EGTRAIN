#include "scene/SceneImporter.h"
#include "scene/SceneModel.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <map>
#include <regex>
#include <set>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

bool SceneImportResult::success() const {
	return wroteScene && !hasErrors(diagnostics);
}

// Case-insensitive path resolution helper
static fs::path resolvePath(const fs::path& base, const std::string& relPath) {
	fs::path current = base;
	std::string relStr = relPath;
	std::replace(relStr.begin(), relStr.end(), '\\', '/');

	size_t pos = 0;
	while (pos < relStr.length()) {
		if (relStr[pos] == '/') {
			pos++;
			continue;
		}
		size_t nextPos = relStr.find('/', pos);
		std::string component = (nextPos == std::string::npos) ? relStr.substr(pos) : relStr.substr(pos, nextPos - pos);
		pos = nextPos;

		if (component == ".")
			continue;
		if (component == "..") {
			current = current.parent_path();
			continue;
		}

		std::error_code ec;
		fs::path next = current / component;
		if (fs::exists(next, ec)) {
			current = next;
			continue;
		}

		bool found = false;
		if (fs::is_directory(current, ec)) {
			std::string lowerComp = component;
			std::transform(lowerComp.begin(), lowerComp.end(), lowerComp.begin(), ::tolower);
			for (const auto& entry : fs::directory_iterator(current, ec)) {
				std::string name = entry.path().filename().string();
				std::string lowerName = name;
				std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
				if (lowerName == lowerComp) {
					current = entry.path();
					found = true;
					break;
				}
			}
		}
		if (!found) {
			fs::path unresolved = current / component;
			if (nextPos != std::string::npos) {
				size_t restPos = nextPos;
				while (restPos < relStr.length()) {
					while (restPos < relStr.length() && relStr[restPos] == '/')
						++restPos;
					if (restPos >= relStr.length())
						break;
					size_t restEnd = relStr.find('/', restPos);
					unresolved /= relStr.substr(restPos,
							restEnd == std::string::npos ? std::string::npos : restEnd - restPos);
					restPos = restEnd;
				}
			}
			return unresolved; // preserve the source-relative path for missing references
		}
	}
	return current;
}

static std::vector<std::string> readTokens(const std::string& line) {
	std::vector<std::string> tokens;
	std::stringstream ss(line);
	std::string token;
	while (ss >> token) {
		tokens.push_back(token);
	}
	return tokens;
}

static bool isPositionedRouteEndpoint(const std::string& token) {
	size_t first = token.find('@');
	size_t last = token.rfind('@');
	if (first != 0 || last == std::string::npos || first == last || last + 1 >= token.length())
		return false;
	std::string position = token.substr(last + 1);
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

static bool isSwitchTransitionRouteToken(const std::string& token) {
	size_t slash = token.find('/');
	if (slash == std::string::npos || token.find('/', slash + 1) != std::string::npos)
		return false;
	return isPositionedRouteEndpoint(token.substr(0, slash)) && isPositionedRouteEndpoint(token.substr(slash + 1));
}

static bool readFile(const fs::path& path, std::string& content) {
	std::ifstream f(path);
	if (!f.good())
		return false;
	content.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	return true;
}

static std::string trimRight(const std::string& s) {
	std::string res = s;
	while (!res.empty() && std::isspace(res.back())) {
		res.pop_back();
	}
	return res;
}

static std::string trim(const std::string& s) {
	size_t start = 0;
	while (start < s.length() && std::isspace(s[start]))
		start++;
	std::string res = s.substr(start);
	return trimRight(res);
}

static fs::path comparablePath(const fs::path& path) {
	std::error_code ec;
	fs::path resolved = fs::weakly_canonical(path, ec);
	if (!ec)
		return resolved;
	resolved = fs::absolute(path, ec);
	return ec ? path.lexically_normal() : resolved.lexically_normal();
}

static bool containsPath(const fs::path& parent, const fs::path& child) {
	if (parent == child)
		return true;
	auto parentIt = parent.begin();
	auto childIt = child.begin();
	for (; parentIt != parent.end() && childIt != child.end(); ++parentIt, ++childIt) {
		if (*parentIt != *childIt)
			return false;
	}
	return parentIt == parent.end();
}

static fs::path uniqueSiblingPath(const fs::path& target, const std::string& tag, std::error_code& error) {
	error.clear();
	fs::path parent = target.parent_path();
	if (parent.empty())
		parent = ".";
	std::string base = target.filename().string();
	if (base.empty())
		base = "scene";
	const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	for (unsigned int suffix = 0;; ++suffix) {
		fs::path candidate = parent / (base + "." + tag + "." + std::to_string(stamp) + "." + std::to_string(suffix));
		const bool exists = fs::exists(candidate, error);
		if (error)
			return {};
		if (!exists)
			return candidate;
	}
}

struct ReportBuilder {
	std::vector<SceneImportReportRow> rows;

	SceneImportReportRow& row(const std::string& category, const std::string& sourceFile) {
		for (auto& value : rows)
			if (value.category == category && value.sourceFile == sourceFile)
				return value;
		SceneImportReportRow value;
		value.category = category;
		value.sourceFile = sourceFile;
		rows.push_back(value);
		return rows.back();
	}

	void source(const std::string& category, const std::string& sourceFile) {
		++row(category, sourceFile).sourceCount;
	}
	void converted(const std::string& category, const std::string& sourceFile) {
		++row(category, sourceFile).convertedCount;
	}
	void skipped(const std::string& category, const std::string& sourceFile) {
		++row(category, sourceFile).skippedCount;
	}
	void unresolved(const std::string& category, const std::string& sourceFile) {
		++row(category, sourceFile).unresolvedReferences;
	}
};

static std::string lowerCopy(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return value;
}

static fs::path findChild(const fs::path& parent, const std::string& wanted) {
	std::error_code ec;
	fs::path exact = parent / wanted;
	if (fs::exists(exact, ec))
		return exact;
	if (!fs::is_directory(parent, ec))
		return {};
	const std::string lowerWanted = lowerCopy(wanted);
	for (const auto& entry : fs::directory_iterator(parent, ec)) {
		if (lowerCopy(entry.path().filename().string()) == lowerWanted)
			return entry.path();
	}
	return {};
}

static std::vector<std::string> splitTab(const std::string& line) {
	std::vector<std::string> values;
	std::stringstream stream(line);
	std::string value;
	while (std::getline(stream, value, '\t'))
		values.push_back(trim(value));
	return values;
}

static std::vector<std::string> splitCsvNaive(const std::string& line) {
	std::vector<std::string> values;
	std::stringstream stream(line);
	std::string value;
	while (std::getline(stream, value, ','))
		values.push_back(trim(value));
	return values;
}

static bool parseDoubleToken(const std::string& token, double& value) {
	try {
		std::size_t used = 0;
		value = std::stod(token, &used);
		return used == token.size();
	} catch (...) {
		return false;
	}
}

static bool parseIntegerToken(const std::string& token, int& value) {
	try {
		std::size_t used = 0;
		value = std::stoi(token, &used);
		return used == token.size();
	} catch (...) {
		return false;
	}
}

static std::string stripOuterAt(const std::string& token) {
	if (token.size() >= 2 && token.front() == '@' && token.back() == '@')
		return token.substr(1, token.size() - 2);
	return token;
}

static std::string normaliseStationName(std::string value) {
	value = lowerCopy(value);
	for (char& character : value) {
		if (character == ' ' || character == '-') character = '_';
	}
	return value;
}

static std::string baseTimeForCase(const std::string& sceneName, const fs::path& legacyPath,
		double& duration, bool& known) {
	const std::string name = lowerCopy(sceneName);
	const std::string folder = lowerCopy(legacyPath.filename().string());
	known = true;
	if (name == "netherlands" || folder == "input_egtrain_netherlands") {
		duration = 8000.0;
		return "06:28:20";
	}
	if (name == "paimpol" || name == "paimpol alternative journeys"
			|| folder == "input_egtrain_paimpol" || folder == "input_egtrain_paimpol - alternativejourneys") {
		duration = 9000.0;
		return "07:10:40";
	}
	if (name == "copenhagen" || name == "banedanmark"
			|| folder == "input_egtrain_copenhagen" || folder == "input_egtrain_banedanmark") {
		duration = 8000.0;
		return "06:28:20";
	}
	if (name == "brescia" || name == "milano_brescia"
			|| folder == "input_egtrain_milano_brescia") {
		duration = 4000.0;
		return "06:28:20";
	}
	if (name == "assignment" || folder == "input_egtrain_assignment") {
		duration = 10000.0;
		return "07:00:00";
	}
	known = false;
	duration = 0.0;
	return {};
}

static std::pair<double, double> passengerTimeWindow(const std::string& token) {
	double value = 0.0;
	if (!parseDoubleToken(token, value) || value < 0.0)
		return {0.0, 0.0};
	const int hour = static_cast<int>(value);
	const double fraction = value - hour;
	if (std::fabs(fraction - 0.25) < 1e-9)
		return {hour * 3600.0, hour * 3600.0 + 1799.0};
	if (std::fabs(fraction - 0.75) < 1e-9)
		return {hour * 3600.0 + 1800.0, hour * 3600.0 + 3599.0};
	return {hour * 3600.0, hour * 3600.0};
}

SceneImportResult importLegacyScene(const std::string& legacyDir,
		const std::string& sceneDir, const std::string& sceneName) {
	SceneImportResult result;
	ReportBuilder report;
	auto addDiag = [&](SceneSeverity severity, const std::string& code, const std::string& message,
			const std::string& file = "", const std::string& category = "", bool unresolved = false) {
		SceneDiagnostic diagnostic;
		diagnostic.severity = severity;
		diagnostic.code = code;
		diagnostic.message = message;
		diagnostic.file = file;
		result.diagnostics.push_back(diagnostic);
		if (unresolved && !category.empty()) report.unresolved(category, file);
	};
	fs::path legacyPath(legacyDir);
	fs::path scenePath(sceneDir);
	const fs::path comparableLegacyPath = comparablePath(legacyPath);
	const fs::path comparableScenePath = comparablePath(scenePath);
	if (containsPath(comparableLegacyPath, comparableScenePath)
			|| containsPath(comparableScenePath, comparableLegacyPath)) {
		addDiag(SceneSeverity::Error, "scene.import.path",
				"Legacy directory and scene destination must be separate, non-overlapping directories", scenePath.string());
		return result;
	}
	if (!fs::is_directory(legacyPath)) {
		addDiag(SceneSeverity::Error, "scene.import.missing", "Legacy directory missing or invalid", legacyPath.string());
		return result;
	}

	// Keep the source root visible in the report because source provenance is
	// represented by import_report rather than a SceneModel field.
	report.row("legacy_root", legacyDir).sourceCount = 1;
	report.converted("legacy_root", legacyDir);

	json infrastructure = { {"tracks", json::array()}, {"nodes", json::array()},
		{"arcs", json::array()}, {"blocks", json::array()}, {"connections", json::array()} };
	json stations = json::array();
	json routes = json::array();
	json trainUnits = json::array();
	json compositions = json::array();
	json services = json::array();
	json blockDependencies = json::array();
	json singleTrackRestrictions = json::array();
	json stationBoundaries = json::array();
	json scenarios = json::array();
	json passengers = json::array();

	// InitialParameters is compiled into the current runtime, not stored in
	// the legacy case directory. Only these known case/name pairs are safe to
	// convert; unknown cases remain importable without guessed defaults.
	double durationSeconds = 0.0;
	bool knownSettings = false;
	const std::string baseTime = baseTimeForCase(sceneName, legacyPath, durationSeconds, knownSettings);
	if (knownSettings) {
		report.source("simulation_settings", "compiled InitialParameters");
		report.converted("simulation_settings", "compiled InitialParameters");
	} else {
		report.source("simulation_settings", "compiled InitialParameters");
		report.skipped("simulation_settings", "compiled InitialParameters");
		report.unresolved("simulation_settings", "compiled InitialParameters");
		addDiag(SceneSeverity::Warning, "scene.import.settings",
				"No concrete InitialParameters mapping for case " + sceneName + "; simulation settings omitted",
				sceneName, "simulation_settings");
	}

	struct NodeRef {
		std::string id;
		double x = 0.0;
	};
	std::unordered_map<std::string, std::vector<NodeRef>> nodesByTrack;
	std::vector<NodeRef> allNodes;
	std::set<std::string> blockIds;
	std::unordered_map<std::string, int> arcIdentityCounts;

	fs::path trackRoot = findChild(legacyPath, "TrackLines");
	if (trackRoot.empty()) trackRoot = findChild(legacyPath, "Tracklines");
	std::vector<std::pair<int, fs::path>> trackDirs;
	auto scanTrackDirs = [&](const fs::path& root) {
		std::error_code scanEc;
		if (!fs::is_directory(root, scanEc)) return;
		for (const auto& entry : fs::directory_iterator(root, scanEc)) {
			if (!entry.is_directory(scanEc)) continue;
			const std::string name = entry.path().filename().string();
			if (name.size() < 2 || lowerCopy(name.substr(0, 1)) != "b") continue;
			const std::string number = name.substr(1);
			if (number.empty() || !std::all_of(number.begin(), number.end(), [](unsigned char c) { return std::isdigit(c); })) continue;
			int index = 0;
			if (parseIntegerToken(number, index)) trackDirs.push_back({index, entry.path()});
		}
	};
	if (!trackRoot.empty()) scanTrackDirs(trackRoot);
	if (trackDirs.empty()) {
		scanTrackDirs(legacyPath);
		if (trackRoot.empty()) trackRoot = legacyPath;
	}
	std::sort(trackDirs.begin(), trackDirs.end(), [](const auto& a, const auto& b) {
		if (a.first != b.first) return a.first < b.first;
		return a.second.string() < b.second.string();
	});
	trackDirs.erase(std::unique(trackDirs.begin(), trackDirs.end(), [](const auto& a, const auto& b) {
		return a.first == b.first && comparablePath(a.second) == comparablePath(b.second);
	}), trackDirs.end());

	const std::string trackSource = trackRoot.filename().empty() ? trackRoot.string() : trackRoot.filename().string();
	report.row("infrastructure.tracks", trackSource).sourceCount = static_cast<int>(trackDirs.size());
	for (const auto& track : trackDirs) {
		const int trackNumber = track.first;
		const std::string trackId = "B" + std::to_string(trackNumber);
		infrastructure["tracks"].push_back({{"id", trackId}});
		report.converted("infrastructure.tracks", trackSource);

		const fs::path nodesPath = findChild(track.second, "NodiCumPari.txt");
		const fs::path arcsPath = findChild(track.second, "ArchiCumPari.txt");
		const fs::path blocksPath = findChild(track.second, "BlockCumPari.txt");
		const std::string nodeSource = (track.second / "NodiCumPari.txt").lexically_normal().string();
		const std::string arcSource = (track.second / "ArchiCumPari.txt").lexically_normal().string();
		const std::string blockSource = (track.second / "BlockCumPari.txt").lexically_normal().string();

		if (!nodesPath.empty()) {
			std::string content;
			readFile(nodesPath, content);
			std::stringstream input(content);
			std::string line;
			int rowIndex = 0;
			while (std::getline(input, line)) {
				if (trim(line).empty()) continue;
				report.source("infrastructure.nodes", nodeSource);
				const auto tokens = readTokens(line);
				if (tokens.size() < 3) {
					report.skipped("infrastructure.nodes", nodeSource);
					addDiag(SceneSeverity::Warning, "scene.import.parse", "Malformed node row " + std::to_string(rowIndex), nodesPath.string());
					++rowIndex;
					continue;
				}
				double legacyId = 0.0, x = 0.0, y = 0.0;
				if (!parseDoubleToken(tokens[0], legacyId) || !parseDoubleToken(tokens[1], x) || !parseDoubleToken(tokens[2], y)) {
					report.skipped("infrastructure.nodes", nodeSource);
					addDiag(SceneSeverity::Warning, "scene.import.parse", "Invalid node row " + std::to_string(rowIndex), nodesPath.string());
					++rowIndex;
					continue;
				}
				const std::string nodeId = trackId + ".node." + tokens[0];
				infrastructure["nodes"].push_back({{"id", nodeId}, {"track", trackId}, {"x_km", x}, {"y_km", y}});
				nodesByTrack[trackId].push_back({nodeId, x});
				allNodes.push_back({nodeId, x});
				report.converted("infrastructure.nodes", nodeSource);
				++rowIndex;
			}
		} else {
			report.row("infrastructure.nodes", nodeSource).skippedCount++;
			addDiag(SceneSeverity::Warning, "scene.import.missing", "Missing NodiCumPari.txt", nodeSource);
		}

		if (!arcsPath.empty()) {
			std::string content;
			readFile(arcsPath, content);
			std::stringstream input(content);
			std::string line;
			int rowIndex = 0;
			while (std::getline(input, line)) {
				if (trim(line).empty()) continue;
				report.source("infrastructure.arcs", arcSource);
				const auto tokens = readTokens(line);
				if (tokens.size() < 6) {
					report.skipped("infrastructure.arcs", arcSource);
					addDiag(SceneSeverity::Warning, "scene.import.parse", "Malformed arc row " + std::to_string(rowIndex), arcsPath.string());
					++rowIndex;
					continue;
				}
				double legacyId = 0.0, from = 0.0, to = 0.0, radius = 0.0, gradient = 0.0, speed = 0.0;
				if (!parseDoubleToken(tokens[0], legacyId) || !parseDoubleToken(tokens[1], from) || !parseDoubleToken(tokens[2], to)
						|| !parseDoubleToken(tokens[3], radius) || !parseDoubleToken(tokens[4], gradient) || !parseDoubleToken(tokens[5], speed)) {
					report.skipped("infrastructure.arcs", arcSource);
					addDiag(SceneSeverity::Warning, "scene.import.parse", "Invalid arc row " + std::to_string(rowIndex), arcsPath.string());
					++rowIndex;
					continue;
				}
				const std::string fromToken = tokens[1];
				const std::string toToken = tokens[2];
				const std::string fromId = trackId + ".node." + fromToken;
				const std::string toId = trackId + ".node." + toToken;
				bool fromKnown = false, toKnown = false;
				for (const auto& node : nodesByTrack[trackId]) {
					if (node.id == fromId) fromKnown = true;
					if (node.id == toId) toKnown = true;
				}
				const std::string arcBaseId = trackId + ".arc." + tokens[0];
				const int arcOccurrence = arcIdentityCounts[arcBaseId]++;
				const std::string arcId = arcOccurrence == 0 ? arcBaseId
						: arcBaseId + "." + std::to_string(arcOccurrence + 1);
				infrastructure["arcs"].push_back({{"id", arcId}, {"track", trackId}, {"from", fromId}, {"to", toId},
					{"curvature_radius_m", radius}, {"gradient_percent", gradient}, {"speed_limit_ms", speed}});
				report.converted("infrastructure.arcs", arcSource);
				if (!fromKnown || !toKnown) {
					addDiag(SceneSeverity::Warning, "scene.import.ref", "Arc refers to an unknown node: " + arcId,
						arcsPath.string(), "infrastructure.arcs", true);
				}
				++rowIndex;
			}
		} else {
			report.row("infrastructure.arcs", arcSource).skippedCount++;
			addDiag(SceneSeverity::Warning, "scene.import.missing", "Missing ArchiCumPari.txt", arcSource);
		}

		if (!blocksPath.empty()) {
			std::string content;
			readFile(blocksPath, content);
			std::stringstream input(content);
			std::string line;
			int rowIndex = 0;
			while (std::getline(input, line)) {
				if (trim(line).empty()) continue;
				report.source("infrastructure.blocks", blockSource);
				const auto tokens = readTokens(line);
				double ignoredIdentity = 0.0, length = 0.0;
				if (tokens.size() < 2 || !parseDoubleToken(tokens[0], ignoredIdentity) || !parseDoubleToken(tokens[1], length)) {
					report.skipped("infrastructure.blocks", blockSource);
					addDiag(SceneSeverity::Warning, "scene.import.parse", "Malformed block row " + std::to_string(rowIndex), blocksPath.string());
					++rowIndex;
					continue;
				}
				const std::string blockId = std::to_string(rowIndex) + "-" + trackId;
				infrastructure["blocks"].push_back({{"id", blockId}, {"track", trackId}, {"length_km", length}});
				blockIds.insert(blockId);
				report.converted("infrastructure.blocks", blockSource);
				++rowIndex;
			}
		} else {
			report.row("infrastructure.blocks", blockSource).skippedCount++;
			addDiag(SceneSeverity::Warning, "scene.import.missing", "Missing BlockCumPari.txt", blockSource);
		}
	}
	auto blockReferenceComponents = [](const std::string& reference) {
		std::vector<std::string> components;
		std::size_t begin = 0;
		while (begin <= reference.size()) {
			const std::size_t slash = reference.find('/', begin);
			const std::string part = reference.substr(begin,
					slash == std::string::npos ? std::string::npos : slash - begin);
			if (!part.empty()) {
				std::string id = part;
				if (part.front() == '@') {
					const std::size_t end = part.find('@', 1);
					if (end != std::string::npos) id = part.substr(1, end - 1);
				} else {
					const std::size_t at = part.find('@');
					if (at != std::string::npos) id = part.substr(0, at);
				}
				if (!id.empty()) components.push_back(id);
			}
			if (slash == std::string::npos) break;
			begin = slash + 1;
		}
		return components;
	};
	auto blockReferenceKnown = [&](const std::string& reference) {
		const auto components = blockReferenceComponents(reference);
		return !components.empty() && std::all_of(components.begin(), components.end(),
				[&](const std::string& component) { return blockIds.count(component) != 0; });
	};

	const fs::path connectionsPath = findChild(trackRoot, "Connections.txt");
	const std::string connectionsSource = (trackRoot / "Connections.txt").lexically_normal().string();
	auto findNodeAt = [&](const std::string& trackId, double x) {
		std::vector<NodeRef> matches;
		for (const auto& node : nodesByTrack[trackId])
			if (node.x == x) matches.push_back(node); // runtime uses exact equality
		return matches;
	};
	if (!connectionsPath.empty()) {
		std::string content;
		readFile(connectionsPath, content);
		std::stringstream input(content);
		std::string line;
		int rowIndex = 0;
		while (std::getline(input, line)) {
			if (trim(line).empty()) continue;
			report.source("infrastructure.connections", connectionsSource);
			const auto tokens = readTokens(line);
			if (tokens.size() < 4 || tokens.size() > 5) {
				report.skipped("infrastructure.connections", connectionsSource);
				addDiag(SceneSeverity::Warning, "scene.import.parse", "Malformed connection row " + std::to_string(rowIndex), connectionsPath.string());
				++rowIndex;
				continue;
			}
			int firstTrack = 0, secondTrack = 0;
			double firstX = 0.0, secondX = 0.0, speed = 0.0;
			const bool valid = parseIntegerToken(tokens[0], firstTrack) && parseDoubleToken(tokens[1], firstX)
					&& parseIntegerToken(tokens[2], secondTrack) && parseDoubleToken(tokens[3], secondX)
					&& (tokens.size() == 4 || parseDoubleToken(tokens[4], speed));
			if (!valid) {
				report.skipped("infrastructure.connections", connectionsSource);
				addDiag(SceneSeverity::Warning, "scene.import.parse", "Invalid connection row " + std::to_string(rowIndex), connectionsPath.string());
				++rowIndex;
				continue;
			}
			const std::string firstTrackId = "B" + std::to_string(firstTrack);
			const std::string secondTrackId = "B" + std::to_string(secondTrack);
			const auto firstMatches = findNodeAt(firstTrackId, firstX);
			const auto secondMatches = findNodeAt(secondTrackId, secondX);
			if (firstMatches.size() != 1 || secondMatches.size() != 1) {
				report.skipped("infrastructure.connections", connectionsSource);
				addDiag(SceneSeverity::Warning, "scene.import.coordinate",
						"Connection row " + std::to_string(rowIndex) + " has "
						+ std::to_string(firstMatches.size()) + "/" + std::to_string(secondMatches.size())
						+ " exact node matches; no connection guessed", connectionsPath.string(),
						"infrastructure.connections", true);
				++rowIndex;
				continue;
			}
			const std::string connectionId = "connection." + std::to_string(rowIndex);
			json value = {{"id", connectionId}, {"from", firstMatches[0].id}, {"to", secondMatches[0].id}};
			if (tokens.size() == 5) value["speed_limit_ms"] = speed;
			infrastructure["connections"].push_back(value);
			report.converted("infrastructure.connections", connectionsSource);
			++rowIndex;
		}
	} else if (!trackDirs.empty()) {
		report.row("infrastructure.connections", connectionsSource).skippedCount++;
		addDiag(SceneSeverity::Warning, "scene.import.missing", "Missing Connections.txt", connectionsSource);
	}

	// Stations are merged by their legacy name. Runtime station assignment uses
	// exact X equality; every exact node match becomes a platform, while zero or
	// multiple matches are reported instead of guessed.
	fs::path stationsPath = findChild(trackRoot, "Stations.txt");
	if (stationsPath.empty() && trackRoot != legacyPath) stationsPath = findChild(legacyPath, "Stations.txt");
	const std::string stationsSource = stationsPath.empty()
		? (trackRoot / "Stations.txt").lexically_normal().string() : stationsPath.string();
	std::unordered_map<std::string, std::size_t> stationIndex;
	auto stationIdFor = [&](const std::string& raw, bool& ambiguous) {
		ambiguous = false;
		for (std::size_t i = 0; i < stations.size(); ++i)
			if (stations[i]["id"] == raw) return raw;
		const std::string normalized = normaliseStationName(raw);
		std::vector<std::size_t> matches;
		for (std::size_t i = 0; i < stations.size(); ++i)
			if (normaliseStationName(stations[i]["id"].get<std::string>()) == normalized) matches.push_back(i);
		if (matches.size() == 1) return stations[matches[0]]["id"].get<std::string>();
		if (matches.size() > 1) ambiguous = true;
		return raw;
	};
	if (!stationsPath.empty()) {
		std::string content;
		readFile(stationsPath, content);
		std::stringstream input(content);
		std::string line;
		int rowIndex = 0;
		while (std::getline(input, line)) {
			if (trim(line).empty()) continue;
			report.source("stations", stationsSource);
			std::string positionToken;
			std::string stationName;
			const auto tabTokens = splitTab(line);
			if (tabTokens.size() >= 2) {
				positionToken = tabTokens[0];
				stationName = tabTokens[1];
			} else {
				const auto tokens = readTokens(line);
				if (tokens.size() < 2) {
					report.skipped("stations", stationsSource);
					addDiag(SceneSeverity::Warning, "scene.import.parse", "Malformed station row " + std::to_string(rowIndex), stationsPath.string());
					++rowIndex;
					continue;
				}
				positionToken = tokens[0];
				const std::size_t nameStart = line.find(tokens[1]);
				stationName = nameStart == std::string::npos ? tokens[1] : trim(line.substr(nameStart));
			}
			double position = 0.0;
			if (stationName.empty() || !parseDoubleToken(positionToken, position)) {
				report.skipped("stations", stationsSource);
				addDiag(SceneSeverity::Warning, "scene.import.parse", "Invalid station row " + std::to_string(rowIndex), stationsPath.string());
				++rowIndex;
				continue;
			}
			std::size_t index = 0;
			const auto found = stationIndex.find(stationName);
			if (found == stationIndex.end()) {
				index = stations.size();
				stationIndex[stationName] = index;
				stations.push_back({{"id", stationName}, {"name", stationName}, {"position_km", position}, {"platforms", json::array()}});
			} else {
				index = found->second;
			}
			const auto matches = allNodes;
			int matched = 0;
			for (const auto& node : matches) {
				if (node.x != position) continue;
				++matched;
				const std::string platformId = stationName + ".platform." + std::to_string(stations[index]["platforms"].size() + 1);
				stations[index]["platforms"].push_back({{"id", platformId}, {"nodes", {node.id}}});
			}
			if (matched == 0) {
				addDiag(SceneSeverity::Warning, "scene.import.coordinate",
						"Station " + stationName + " at " + positionToken + " has no exact node match",
						stationsPath.string(), "stations", true);
			}
			report.converted("stations", stationsSource);
			++rowIndex;
		}
	} else {
		report.row("stations", stationsSource).skippedCount++;
		addDiag(SceneSeverity::Error, "scene.import.missing", "Missing TrackLines/Stations.txt", stationsSource);
	}
	// Read the explicit train manifest before routes so services can retain
	// their route references and rolling-stock relationships independently.
	struct Relation {
		std::string serviceId;
		std::string operatingCode;
		std::string compositionId;
		std::string timetablePath;
		double entryTime = 0.0;
		double headway = 0.0;
		bool hasRepeat = false;
		int routeIndex = 0;
	};
	struct UnitRelation {
		std::string id;
		std::string dataPath;
		std::string tractionPath;
	};
	std::vector<Relation> relations;
	std::vector<UnitRelation> unitRelations;
	std::set<std::string> unitIds;
	std::set<std::string> serviceIds;
	std::vector<std::string> trainFiles;
	const fs::path trainsDir = findChild(legacyPath, "Trains");
	const fs::path manifestPath = findChild(legacyPath, "trainNames.txt");
	const std::string manifestSource = manifestPath.empty() ? (legacyPath / "trainNames.txt").string() : manifestPath.string();
	if (!manifestPath.empty()) {
		std::string content;
		readFile(manifestPath, content);
		std::stringstream input(content);
		std::string line;
		while (std::getline(input, line)) {
			line = trim(line);
			if (line.empty()) continue;
			const std::string lowerLine = lowerCopy(line);
			if (lowerLine.find("txt") != std::string::npos) trainFiles.push_back("Trains/" + line);
		}
		report.row("rolling_stock.manifest", manifestSource).sourceCount = static_cast<int>(trainFiles.size());
	} else {
		addDiag(SceneSeverity::Info, "scene.import.info", "trainNames.txt missing, falling back to Trains directory enumeration", manifestSource);
		std::error_code scanEc;
		if (fs::is_directory(trainsDir, scanEc)) {
			for (const auto& entry : fs::directory_iterator(trainsDir, scanEc)) {
				if (!entry.is_regular_file(scanEc)) continue;
				const std::string name = entry.path().filename().string();
				if (lowerCopy(entry.path().extension().string()) == ".txt" && lowerCopy(name) != "trainnames.txt")
					trainFiles.push_back("Trains/" + name);
			}
		}
		std::sort(trainFiles.begin(), trainFiles.end());
		report.row("rolling_stock.manifest", manifestSource).sourceCount = static_cast<int>(trainFiles.size());
	}

	std::unordered_set<std::string> referencedRoutes;
	for (const auto& trainFile : trainFiles) {
		const fs::path trainPath = resolvePath(legacyPath, trainFile);
		std::string content;
		if (!readFile(trainPath, content)) {
			report.skipped("rolling_stock.manifest", manifestSource);
			addDiag(SceneSeverity::Error, "scene.import.missing", "Missing train file: " + trainFile, trainPath.string());
			continue;
		}
		const auto tokens = readTokens(content);
		if (tokens.size() < 7) {
			report.skipped("rolling_stock.manifest", manifestSource);
			addDiag(SceneSeverity::Error, "scene.import.parse", "Malformed Trains file, need 7 tokens", trainPath.string());
			continue;
		}
		double entryTime = 0.0, headway = 0.0;
		int routeIndex = 0;
		if (!parseDoubleToken(tokens[1], entryTime) || !parseDoubleToken(tokens[2], headway) || !parseIntegerToken(tokens[3], routeIndex)) {
			report.skipped("rolling_stock.manifest", manifestSource);
			addDiag(SceneSeverity::Error, "scene.import.parse", "Invalid numbers in Trains file", trainPath.string());
			continue;
		}
		std::string serviceId = tokens[0];
		const std::string originalServiceId = serviceId;
		for (int suffix = 2; serviceIds.count(serviceId); ++suffix) serviceId = originalServiceId + "_" + std::to_string(suffix);
		if (serviceId != originalServiceId)
			addDiag(SceneSeverity::Warning, "scene.import.adjusted", "Duplicate service name " + originalServiceId + " renamed to " + serviceId, trainPath.string());
		serviceIds.insert(serviceId);
		const std::string dataPath = tokens[4];
		const std::string tractionPath = tokens[5];
		const fs::path dataFile(dataPath);
		std::string baseId = dataFile.stem().string();
		if (baseId.empty()) baseId = "train_unit";
		std::string unitId = baseId;
		for (int suffix = 2; unitIds.count(unitId); ++suffix) unitId = baseId + "_" + std::to_string(suffix);
		const auto existingUnit = std::find_if(unitRelations.begin(), unitRelations.end(), [&](const UnitRelation& value) {
			return value.dataPath == dataPath && value.tractionPath == tractionPath;
		});
		if (existingUnit != unitRelations.end()) unitId = existingUnit->id;
		else {
			unitIds.insert(unitId);
			unitRelations.push_back({unitId, dataPath, tractionPath});
		}
		const std::string routeId = "route" + std::to_string(routeIndex);
		referencedRoutes.insert(routeId);
		Relation relation;
		relation.serviceId = serviceId;
		relation.operatingCode = originalServiceId;
		relation.compositionId = unitId;
		relation.timetablePath = tokens[6];
		relation.entryTime = entryTime;
		relation.headway = headway;
		relation.hasRepeat = headway < 99999999.0;
		relation.routeIndex = routeIndex;
		relations.push_back(relation);
		report.converted("rolling_stock.manifest", manifestSource);
	}

	// Base routes are numeric and deterministic. Keep basic block IDs without
	// their @ delimiters; retain composite switch tokens exactly as runtime input.
	const fs::path routesDir = findChild(legacyPath, "Routes");
	std::vector<std::pair<int, fs::path>> routeFiles;
	std::error_code routeEc;
	if (!routesDir.empty() && fs::is_directory(routesDir, routeEc)) {
		for (const auto& entry : fs::directory_iterator(routesDir, routeEc)) {
			if (!entry.is_regular_file(routeEc)) continue;
			const std::string stem = entry.path().stem().string();
			const std::string lowerStem = lowerCopy(stem);
			if (lowerStem.size() <= 5 || lowerStem.substr(0, 5) != "route") continue;
			const std::string number = stem.substr(5);
			if (number.empty() || !std::all_of(number.begin(), number.end(), [](unsigned char c) { return std::isdigit(c); })) continue;
			int index = 0;
			if (parseIntegerToken(number, index)) routeFiles.push_back({index, entry.path()});
		}
	}
	std::sort(routeFiles.begin(), routeFiles.end(), [](const auto& a, const auto& b) {
		if (a.first != b.first) return a.first < b.first;
		return a.second.string() < b.second.string();
	});
	std::set<std::string> loadedRouteIds;
	for (const auto& routeFile : routeFiles) {
		const std::string routeId = "route" + std::to_string(routeFile.first);
		const std::string source = routeFile.second.string();
		std::string content;
		if (!readFile(routeFile.second, content)) {
			report.skipped("signalling.routes", source);
			addDiag(SceneSeverity::Error, "scene.import.ref", "Missing route file: " + source, source);
			continue;
		}
		json blockList = json::array();
		std::stringstream input(content);
		std::string line;
		int rowIndex = 0;
		while (std::getline(input, line)) {
			if (trim(line).empty()) continue;
			report.source("signalling.routes", source);
			const auto tokens = readTokens(line);
			if (tokens.empty()) {
				report.skipped("signalling.routes", source);
				++rowIndex;
				continue;
			}
			const std::string token = tokens[0];
			const bool basic = token.size() >= 2 && token.front() == '@' && token.back() == '@' && token.find('/', 1) == std::string::npos;
			if (!basic && !isSwitchTransitionRouteToken(token)) {
				report.skipped("signalling.routes", source);
				addDiag(SceneSeverity::Warning, "scene.import.parse", "Malformed route block token at row " + std::to_string(rowIndex), source);
			}
			blockList.push_back(basic ? stripOuterAt(token) : token);
			report.converted("signalling.routes", source);
			++rowIndex;
		}
		routes.push_back({{"id", routeId}, {"blocks", blockList}});
		loadedRouteIds.insert(routeId);
	}
	for (const auto& routeId : referencedRoutes) {
		if (loadedRouteIds.count(routeId)) continue;
		const std::string number = routeId.substr(5);
		const fs::path missing = routesDir.empty() ? legacyPath / "Routes" / ("Route" + number + ".txt")
				: routesDir / ("Route" + number + ".txt");
		addDiag(SceneSeverity::Error, "scene.import.ref", "Missing route file: " + missing.string(), missing.string(), "signalling.routes", true);
	}

	// Route corridor metadata is an active GUI input, unlike generated signal
	// and TDS caches, so retain it directly on the matching route.
	const fs::path guiDir = findChild(legacyPath, "GUI");
	const fs::path corridorPath = guiDir.empty() ? fs::path() : findChild(guiDir, "caseStudyRouteCorridors.txt");
	const std::string corridorSource = corridorPath.empty() ? (legacyPath / "GUI/caseStudyRouteCorridors.txt").string() : corridorPath.string();
	if (!corridorPath.empty()) {
		std::string content;
		readFile(corridorPath, content);
		std::stringstream input(content);
		std::string line;
		while (std::getline(input, line)) {
			if (trim(line).empty()) continue;
			report.source("signalling.corridors", corridorSource);
			const auto tokens = splitTab(line);
			int routeIndex = 0;
			if (tokens.size() < 2 || !parseIntegerToken(tokens[0], routeIndex)) {
				report.skipped("signalling.corridors", corridorSource);
				addDiag(SceneSeverity::Warning, "scene.import.parse", "Malformed route corridor row", corridorPath.string());
				continue;
			}
			const std::string id = "route" + std::to_string(routeIndex);
			bool found = false;
			for (auto& route : routes) {
				if (route["id"] == id) {
					route["corridor"] = tokens[1];
					found = true;
					break;
				}
			}
			if (!found) {
				addDiag(SceneSeverity::Warning, "scene.import.ref", "Route corridor refers to unknown route " + id,
						corridorPath.string(), "signalling.corridors", true);
			} else report.converted("signalling.corridors", corridorSource);
		}
	}

	// Materialise joined routes at the append position, following the runtime
	// row order and optional Reverse token.
	int nextJoinedRouteIndex = routeFiles.empty() ? 0 : routeFiles.back().first + 1;
	const fs::path routesToWrite = findChild(legacyPath, "RoutesToWrite");
	const fs::path joinsPath = routesToWrite.empty() ? fs::path() : findChild(routesToWrite, "RoutesToJoin.txt");
	const std::string joinsSource = joinsPath.empty() ? (legacyPath / "RoutesToWrite/RoutesToJoin.txt").string() : joinsPath.string();
	if (!joinsPath.empty()) {
		std::string content;
		readFile(joinsPath, content);
		std::stringstream input(content);
		std::string line;
		while (std::getline(input, line)) {
			if (trim(line).empty()) continue;
			report.source("signalling.joined_routes", joinsSource);
			const auto tokens = readTokens(line);
			std::vector<int> refs;
			bool reversed = false;
			for (const auto& token : tokens) {
				if (lowerCopy(token) == "reverse") {
					reversed = true;
					continue;
				}
				std::string number = token;
				if (number.size() >= 5 && lowerCopy(number.substr(0, 5)) == "route") number = number.substr(5);
				int ref = -1;
				if (!parseIntegerToken(number, ref)) refs.clear();
				else refs.push_back(ref);
				if (refs.empty() && !number.empty() && !parseIntegerToken(number, ref)) break;
			}
			bool valid = !refs.empty();
			json joinedBlocks = json::array();
			if (valid) {
				for (const int ref : refs) {
					const std::string id = "route" + std::to_string(ref);
					const auto found = std::find_if(routes.begin(), routes.end(), [&](const json& route) { return route["id"] == id; });
					if (found == routes.end()) {
						valid = false;
						addDiag(SceneSeverity::Warning, "scene.import.ref", "Joined route refers to unknown route " + id,
								joinsPath.string(), "signalling.joined_routes", true);
						break;
					}
					for (const auto& block : (*found)["blocks"]) joinedBlocks.push_back(block);
				}
			}
			if (!valid) {
				report.skipped("signalling.joined_routes", joinsSource);
				if (tokens.empty()) addDiag(SceneSeverity::Warning, "scene.import.parse", "Malformed joined route row", joinsPath.string());
				continue;
			}
			while (loadedRouteIds.count("route" + std::to_string(nextJoinedRouteIndex)))
				++nextJoinedRouteIndex;
			const std::string joinedId = "route" + std::to_string(nextJoinedRouteIndex++);
			json joined = {{"id", joinedId}, {"blocks", joinedBlocks}};
			if (reversed) joined["reversed"] = true;
			routes.push_back(joined);
			loadedRouteIds.insert(joinedId);
			report.converted("signalling.joined_routes", joinsSource);
		}
	}

	// Only the dependency proven by the current Copenhagen signalling path is
	// persisted. Other signals, virtual signals and TDS rows remain derived.
	if (blockIds.count("1-B30") && blockIds.count("5-B6") && blockIds.count("5-B7")) {
		blockDependencies.push_back({{"block", "5-B6"},
			{"depends_on", "@1-B30@-4.592000/@5-B7@-4.620000"}});
		report.source("signalling.dependencies", "Copenhagen hard-coded dependency");
		report.converted("signalling.dependencies", "Copenhagen hard-coded dependency");
	}

	const fs::path singleTrackPath = guiDir.empty() ? fs::path() : findChild(guiDir, "singleTrackLimits.txt");
	const std::string singleTrackSource = singleTrackPath.empty() ? (legacyPath / "GUI/singleTrackLimits.txt").string() : singleTrackPath.string();
	if (!singleTrackPath.empty()) {
		std::string content;
		readFile(singleTrackPath, content);
		std::stringstream input(content);
		std::string line;
		while (std::getline(input, line)) {
			if (trim(line).empty()) continue;
			report.source("signalling.single_track_restrictions", singleTrackSource);
			auto tokens = splitTab(line);
			if (tokens.size() < 4) tokens = readTokens(line);
			if (tokens.size() < 4) {
				report.skipped("signalling.single_track_restrictions", singleTrackSource);
				addDiag(SceneSeverity::Warning, "scene.import.parse", "Malformed single-track restriction row", singleTrackPath.string());
				continue;
			}
			json restriction = {{"start_block", stripOuterAt(tokens[0])}, {"end_block", stripOuterAt(tokens[1])},
				{"protected_start_block", tokens[2]}, {"protected_end_block", tokens[3]}};
			singleTrackRestrictions.push_back(restriction);
			report.converted("signalling.single_track_restrictions", singleTrackSource);
			for (int i = 0; i < 4; ++i) {
				const std::string ref = i < 2 ? stripOuterAt(tokens[i]) : tokens[i];
				if (!ref.empty() && !blockReferenceKnown(ref)) {
					addDiag(SceneSeverity::Warning, "scene.import.ref", "Single-track restriction refers to unknown block " + ref,
							singleTrackPath.string(), "signalling.single_track_restrictions", true);
				}
			}
		}
	}

	const fs::path boundaryPath = guiDir.empty() ? fs::path() : findChild(guiDir, "stationBoundarySections.txt");
	const std::string boundarySource = boundaryPath.empty() ? (legacyPath / "GUI/stationBoundarySections.txt").string() : boundaryPath.string();
	if (!boundaryPath.empty()) {
		std::string content;
		readFile(boundaryPath, content);
		std::stringstream input(content);
		std::string line;
		while (std::getline(input, line)) {
			if (trim(line).empty()) continue;
			report.source("signalling.station_boundaries", boundarySource);
			auto tokens = splitTab(line);
			if (tokens.size() < 3) tokens = readTokens(line);
			if (tokens.size() < 3) {
				report.skipped("signalling.station_boundaries", boundarySource);
				addDiag(SceneSeverity::Warning, "scene.import.parse", "Malformed station-boundary row", boundaryPath.string());
				continue;
			}
			int direction = 0;
			if (!parseIntegerToken(tokens[2], direction) || (direction != 0 && direction != 1)) {
				report.skipped("signalling.station_boundaries", boundarySource);
				addDiag(SceneSeverity::Warning, "scene.import.parse", "Invalid station-boundary direction", boundaryPath.string(), "signalling.station_boundaries", true);
				continue;
			}
			json boundary = {{"entrance_block", stripOuterAt(tokens[0])}, {"direction", direction != 0}};
			if (!tokens[1].empty()) {
				boundary["exit_block"] = stripOuterAt(tokens[1]);
				if (!blockReferenceKnown(tokens[1])) {
					addDiag(SceneSeverity::Warning, "scene.import.ref", "Station boundary refers to unknown exit block " + tokens[1],
							boundaryPath.string(), "signalling.station_boundaries", true);
				}
			}
			if (!blockReferenceKnown(tokens[0])) {
				addDiag(SceneSeverity::Warning, "scene.import.ref", "Station boundary refers to unknown entrance block " + tokens[0],
						boundaryPath.string(), "signalling.station_boundaries", true);
			}
			stationBoundaries.push_back(boundary);
			report.converted("signalling.station_boundaries", boundarySource);
		}
	}

	// Timetables preserve arrival and departure presence independently. The
	// legacy -1 marker is omitted from its corresponding canonical field.
	for (const auto& relation : relations) {
		const fs::path timetablePath = resolvePath(legacyPath, relation.timetablePath);
		const std::string timetableSource = relation.timetablePath;
		std::string content;
		if (!readFile(timetablePath, content)) {
			report.skipped("timetable", timetableSource);
			addDiag(SceneSeverity::Error, "scene.import.ref", "Missing timetable file: " + relation.timetablePath, timetablePath.string());
			continue;
		}
		json stops = json::array();
		std::stringstream input(content);
		std::string line;
		int rowIndex = 0;
		while (std::getline(input, line)) {
			if (trim(line).empty()) continue;
			report.source("timetable", timetableSource);
			const auto tokens = readTokens(line);
			if (tokens.size() < 4) {
				report.skipped("timetable", timetableSource);
				addDiag(SceneSeverity::Warning, "scene.import.parse", "Malformed timetable row " + std::to_string(rowIndex), timetablePath.string());
				++rowIndex;
				continue;
			}
			double dwell = 0.0, arrival = -1.0, departure = -1.0;
			if (!parseDoubleToken(tokens[tokens.size() - 3], dwell)
					|| !parseDoubleToken(tokens[tokens.size() - 2], arrival)
					|| !parseDoubleToken(tokens[tokens.size() - 1], departure)) {
				report.skipped("timetable", timetableSource);
				addDiag(SceneSeverity::Warning, "scene.import.parse", "Invalid timetable row " + std::to_string(rowIndex), timetablePath.string());
				++rowIndex;
				continue;
			}
			std::string stationName = tokens[0];
			for (std::size_t i = 1; i + 3 < tokens.size(); ++i) stationName += tokens[i];
			bool ambiguous = false;
			const std::string stationId = stationIdFor(stationName, ambiguous);
			if (ambiguous || std::none_of(stations.begin(), stations.end(), [&](const json& station) { return station["id"] == stationId; })) {
				addDiag(SceneSeverity::Warning, "scene.import.ref", "Timetable refers to unresolved station " + stationName,
						timetablePath.string(), "timetable", true);
			}
			json stop = {{"station", stationId}, {"dwell_seconds", dwell}};
			if (arrival != -1.0) {
				stop["planned_arrival_seconds"] = arrival;
			}
			if (departure != -1.0) {
				stop["planned_departure_seconds"] = departure;
			}
			if (arrival != -1.0 && departure != -1.0 && departure < arrival)
				addDiag(SceneSeverity::Warning, "scene.import.timetable", "Legacy departure precedes arrival at " + stationName + "; values preserved", timetablePath.string());
			stops.push_back(stop);
			report.converted("timetable", timetableSource);
			++rowIndex;
		}
		json service = {{"id", relation.serviceId}, {"operating_code", relation.operatingCode},
			{"composition", relation.compositionId},
			{"route", "route" + std::to_string(relation.routeIndex)}, {"entry_time_seconds", relation.entryTime}, {"stops", stops}};
		if (stops.empty()) service["through"] = true;
		if (relation.hasRepeat) service["repeat"] = {{"headway_seconds", relation.headway}};
		services.push_back(service);
		report.source("services", relation.serviceId);
		report.converted("services", relation.serviceId);
	}

	// Parse each explicitly referenced parameter/tractive-effort pair exactly
	// once. Pair identity, not filename convention, owns the association.
	for (const auto& relation : unitRelations) {
		const fs::path dataPath = resolvePath(legacyPath, relation.dataPath);
		const fs::path tractionPath = resolvePath(legacyPath, relation.tractionPath);
		const std::string dataSource = relation.dataPath;
		const std::string tractionSource = relation.tractionPath;
		std::string dataContent;
		if (!readFile(dataPath, dataContent)) {
			report.skipped("rolling_stock.data", dataSource);
			addDiag(SceneSeverity::Error, "scene.import.ref", "Missing train data file: " + relation.dataPath, dataPath.string());
			continue;
		}
		const auto dataTokens = readTokens(dataContent);
		if (dataTokens.size() < 9) {
			report.skipped("rolling_stock.data", dataSource);
			addDiag(SceneSeverity::Error, "scene.import.parse", "Malformed train data file", dataPath.string());
			continue;
		}
		json physical = json::array();
		bool physicalValid = true;
		for (int i = 0; i < 9; ++i) {
			double value = 0.0;
			if (!parseDoubleToken(dataTokens[static_cast<std::size_t>(i)], value)) physicalValid = false;
			physical.push_back(value);
		}
		if (!physicalValid) {
			report.skipped("rolling_stock.data", dataSource);
			addDiag(SceneSeverity::Error, "scene.import.parse", "Invalid numbers in train data file", dataPath.string());
			continue;
		}
		report.source("rolling_stock.data", dataSource);
		report.converted("rolling_stock.data", dataSource);

		std::string tractionContent;
		if (!readFile(tractionPath, tractionContent)) {
			report.skipped("rolling_stock.traction", tractionSource);
			addDiag(SceneSeverity::Error, "scene.import.ref", "Missing traction file: " + relation.tractionPath, tractionPath.string());
			continue;
		}
		json curve = json::array();
		std::stringstream tractionInput(tractionContent);
		std::string line;
		int rowIndex = 0;
		while (std::getline(tractionInput, line)) {
			if (trim(line).empty()) continue;
			report.source("rolling_stock.traction", tractionSource);
			const auto tokens = readTokens(line);
			if (tokens.size() < 5) {
				report.skipped("rolling_stock.traction", tractionSource);
				addDiag(SceneSeverity::Warning, "scene.import.parse", "Malformed traction row " + std::to_string(rowIndex), tractionPath.string());
				++rowIndex;
				continue;
			}
			json row = json::array();
			bool valid = true;
			for (int i = 0; i < 5; ++i) {
				double value = 0.0;
				if (!parseDoubleToken(tokens[static_cast<std::size_t>(i)], value)) valid = false;
				row.push_back(value);
			}
			if (!valid) {
				report.skipped("rolling_stock.traction", tractionSource);
				addDiag(SceneSeverity::Warning, "scene.import.parse", "Invalid traction row " + std::to_string(rowIndex), tractionPath.string());
			} else {
				curve.push_back(row);
				report.converted("rolling_stock.traction", tractionSource);
			}
			++rowIndex;
		}
		if (curve.empty()) {
			addDiag(SceneSeverity::Error, "scene.import.parse", "Train traction file has no valid rows", tractionPath.string());
			continue;
		}
		trainUnits.push_back({{"id", relation.id},
			{"physical", {{"mass_of_traction_unit_kg", physical[0]}, {"mass_of_a_wagon_kg", physical[1]},
			{"number_of_wagons", physical[2]}, {"max_speed_ms", physical[3]}, {"max_deceleration_ms2", physical[4]},
			{"frontal_area_m2", physical[5]}, {"resistance_coefficient", physical[6]}, {"jerk_ms3", physical[7]}, {"length_m", physical[8]}}},
			{"traction_curve", curve}, {"source", {{"data_file", relation.dataPath}, {"traction_file", relation.tractionPath}}}});
		compositions.push_back({{"id", relation.id}, {"units", {relation.id}}});
		report.source("rolling_stock.compositions", relation.id);
		report.converted("rolling_stock.compositions", relation.id);
	}

	// Baseline incidents are the only flat legacy scenario input. Rollout files
	// contain positional delay vectors; map them in manifest order and retain
	// the same value at the two stations used by the runtime scenario loader.
	json baseline = {{"id", "baseline"}, {"name", "Baseline"}, {"incidents", json::array()}, {"entrance_delays", json::array()}};
	const fs::path incidentsPath = findChild(legacyPath, "Incidents.txt");
	const std::string incidentsSource = incidentsPath.empty() ? (legacyPath / "Incidents.txt").string() : incidentsPath.string();
	if (!incidentsPath.empty()) {
		std::string content;
		readFile(incidentsPath, content);
		std::stringstream input(content);
		std::string line;
		int lineNo = 0;
		while (std::getline(input, line)) {
			++lineNo;
			if (trim(line).empty()) continue;
			report.source("scenarios.incidents", incidentsSource);
			auto tokens = splitTab(line);
			if (tokens.size() < 4) tokens = readTokens(line);
			double start = 0.0, end = 0.0;
			if (tokens.size() < 4 || !parseDoubleToken(tokens[2], start) || !parseDoubleToken(tokens[3], end)) {
				report.skipped("scenarios.incidents", incidentsSource);
				addDiag(SceneSeverity::Warning, "scene.import.parse", "Malformed incident row " + std::to_string(lineNo), incidentsPath.string());
				continue;
			}
			std::vector<std::string> targets = {tokens[1]};
			if (tokens[0] == "train_breakdown") {
				targets.clear();
				for (const auto& relation : relations) {
					if (relation.operatingCode == tokens[1])
						targets.push_back(relation.serviceId);
				}
				if (targets.empty()) {
					targets.push_back(tokens[1]);
					addDiag(SceneSeverity::Warning, "scene.import.ref",
							"Train-breakdown incident refers to an unknown service code " + tokens[1],
							incidentsPath.string(), "scenarios.incidents", true);
				} else if (targets.size() > 1) {
					addDiag(SceneSeverity::Warning, "scene.import.expanded",
							"Train-breakdown incident code " + tokens[1] + " matches "
									+ std::to_string(targets.size())
									+ " services; expanded to preserve legacy prefix-match semantics",
							incidentsPath.string());
				}
			}
			for (std::size_t targetIndex = 0; targetIndex < targets.size(); ++targetIndex) {
				std::string id = "incident." + std::to_string(lineNo);
				if (targets.size() > 1)
					id += "." + std::to_string(targetIndex + 1);
				baseline["incidents"].push_back({{"id", id}, {"type", tokens[0]},
					{"target", targets[targetIndex]}, {"start_seconds", start}, {"end_seconds", end}});
			}
			report.converted("scenarios.incidents", incidentsSource);
		}
	}

	const fs::path timetableRoot = findChild(legacyPath, "TimeTable");
	const fs::path rolloutDir = timetableRoot.empty() ? fs::path() : findChild(timetableRoot, "Scenarios_Entrance_Delays");
	std::vector<std::pair<int, fs::path>> rolloutFiles;
	std::error_code rolloutEc;
	if (!rolloutDir.empty() && fs::is_directory(rolloutDir, rolloutEc)) {
		const std::regex rolloutPattern("^Rollout_([0-9]+)\\.txt$", std::regex::icase);
		for (const auto& entry : fs::directory_iterator(rolloutDir, rolloutEc)) {
			if (!entry.is_regular_file(rolloutEc)) continue;
			std::smatch match;
			const std::string filename = entry.path().filename().string();
			if (!std::regex_match(filename, match, rolloutPattern)) continue;
			int index = 0;
			if (parseIntegerToken(match[1].str(), index)) rolloutFiles.push_back({index, entry.path()});
		}
	}
	std::sort(rolloutFiles.begin(), rolloutFiles.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
	for (const auto& rollout : rolloutFiles) {
		const std::string source = rollout.second.string();
		std::string content;
		readFile(rollout.second, content);
		const auto values = readTokens(content);
		const std::string scenarioId = "rollout_" + std::to_string(rollout.first);
		json rolloutIncidents = baseline["incidents"];
		for (auto& incident : rolloutIncidents)
			incident["id"] = scenarioId + "." + incident["id"].get<std::string>();
		json scenario = {{"id", scenarioId}, {"name", "Rollout " + std::to_string(rollout.first)},
			{"incidents", rolloutIncidents}, {"entrance_delays", json::array()}};
		std::size_t valueIndex = 0;
		if (!knownSettings) {
			const int valuesCount = static_cast<int>(values.size());
			auto& row = report.row("scenarios.rollouts", source);
			row.sourceCount += valuesCount;
			row.skippedCount += valuesCount;
			row.unresolvedReferences++;
			addDiag(SceneSeverity::Warning, "scene.import.scenario", "Cannot map rollout without known duration", source);
			scenarios.push_back(scenario);
			continue;
		}
		for (const auto& relation : relations) {
			int occurrences = 1;
			if (relation.hasRepeat && relation.headway > 0.0)
				occurrences = std::max(1, static_cast<int>(std::ceil(durationSeconds / relation.headway)));
			for (int occurrence = 1; occurrence <= occurrences; ++occurrence) {
				report.source("scenarios.rollouts", source);
				if (valueIndex >= values.size()) {
					report.skipped("scenarios.rollouts", source);
					addDiag(SceneSeverity::Warning, "scene.import.scenario", "Rollout vector is shorter than the service occurrence mapping", source, "scenarios.rollouts", true);
					++valueIndex;
					continue;
				}
				double delay = 0.0;
				if (!parseDoubleToken(values[valueIndex], delay)) {
					report.skipped("scenarios.rollouts", source);
					addDiag(SceneSeverity::Warning, "scene.import.parse", "Invalid rollout delay value", source, "scenarios.rollouts", true);
					++valueIndex;
					continue;
				}
				++valueIndex;
				json serviceStops;
				const auto serviceIt = std::find_if(services.begin(), services.end(), [&](const json& service) { return service["id"] == relation.serviceId; });
				if (serviceIt != services.end()) serviceStops = (*serviceIt)["stops"];
				bool emitted = false;
				for (const auto& stop : serviceStops) {
					const std::string station = stop["station"].get<std::string>();
					if (station != "Guingamp" && station != "Paimpol") continue;
					scenario["entrance_delays"].push_back({{"service", relation.serviceId}, {"occurrence", occurrence},
						{"station", station}, {"delay_seconds", delay}});
					emitted = true;
				}
				if (emitted) report.converted("scenarios.rollouts", source);
				else report.skipped("scenarios.rollouts", source);
			}
		}
		if (valueIndex < values.size()) {
			report.skipped("scenarios.rollouts", source);
			addDiag(SceneSeverity::Warning, "scene.import.scenario", "Rollout vector has extra values", source, "scenarios.rollouts", true);
		}
		scenarios.push_back(scenario);
	}
	scenarios.insert(scenarios.begin(), baseline);
	report.row("scenarios", "scenarios.json").sourceCount = static_cast<int>(scenarios.size());
	report.row("scenarios", "scenarios.json").convertedCount = static_cast<int>(scenarios.size());

	// The passenger importer is intentionally gated by the same two exact files
	// used by DispatchController. It keeps deterministic windows, never random
	// draws or generated runtime results.
	const fs::path passengerDir = findChild(legacyPath, "Passengers");
	const fs::path dasPath = passengerDir.empty() ? fs::path() : passengerDir / "DAS_FrenchCaseStudy.csv";
	const fs::path routeChoicePath = passengerDir.empty() ? fs::path() : passengerDir / "RouteChoiceFC_EQ1.csv";
	const bool hasDas = !passengerDir.empty() && fs::is_regular_file(dasPath);
	const bool hasRouteChoice = !passengerDir.empty() && fs::is_regular_file(routeChoicePath);
	const std::string dasSource = (passengerDir / "DAS_FrenchCaseStudy.csv").string();
	const std::string routeChoiceSource = (passengerDir / "RouteChoiceFC_EQ1.csv").string();
	if (hasDas && hasRouteChoice) {
		std::unordered_map<std::string, std::size_t> passengerIndex;
		std::unordered_map<std::string, std::vector<std::pair<std::size_t, std::size_t>>> journeysByPersonDestination;
		std::string content;
		readFile(dasPath, content);
		std::stringstream input(content);
		std::string line;
		bool header = true;
		int rowNo = 0;
		while (std::getline(input, line)) {
			if (header) { header = false; continue; }
			if (trim(line).empty()) continue;
			++rowNo;
			report.source("passengers.das", dasSource);
			const auto fields = splitCsvNaive(line);
			if (fields.size() <= 14) {
				report.skipped("passengers.das", dasSource);
				addDiag(SceneSeverity::Warning, "scene.import.parse", "Malformed passenger DAS row " + std::to_string(rowNo), dasPath.string());
				continue;
			}
			const std::string personId = fields[1];
			if (personId.empty()) {
				report.skipped("passengers.das", dasSource);
				addDiag(SceneSeverity::Warning, "scene.import.ref", "Passenger row has no person id", dasPath.string(), "passengers.das", true);
				continue;
			}
			std::size_t pIndex = 0;
			const auto foundPassenger = passengerIndex.find(personId);
			if (foundPassenger == passengerIndex.end()) {
				pIndex = passengers.size();
				passengerIndex[personId] = pIndex;
				passengers.push_back({{"id", personId}, {"journeys", json::array()}});
			} else pIndex = foundPassenger->second;
			std::string journeyId = personId + ":" + (fields[4].empty() ? std::to_string(rowNo) : fields[4]);
			const std::string originalJourneyId = journeyId;
			for (int suffix = 2;; ++suffix) {
				bool duplicate = false;
				for (const auto& journey : passengers[pIndex]["journeys"])
					if (journey["id"] == journeyId) duplicate = true;
				if (!duplicate) break;
				journeyId = originalJourneyId + "_" + std::to_string(suffix);
			}
			bool ambiguousOrigin = false, ambiguousDestination = false;
			const std::string origin = stationIdFor(fields[12], ambiguousOrigin);
			const std::string destination = stationIdFor(fields[6], ambiguousDestination);
			if (ambiguousOrigin || ambiguousDestination
					|| std::none_of(stations.begin(), stations.end(), [&](const json& value) { return value["id"] == origin; })
					|| std::none_of(stations.begin(), stations.end(), [&](const json& value) { return value["id"] == destination; })) {
				addDiag(SceneSeverity::Warning, "scene.import.ref", "Passenger journey has an unresolved station reference", dasPath.string(), "passengers.das", true);
			}
			const auto departure = passengerTimeWindow(fields[14]);
			const auto arrival = passengerTimeWindow(fields[10]);
			json journey = {{"id", journeyId}, {"activity", fields[5]}, {"origin", origin}, {"destination", destination},
				{"planned_departure", {{"start_seconds", departure.first}, {"end_seconds", departure.second}}},
				{"planned_arrival", {{"start_seconds", arrival.first}, {"end_seconds", arrival.second}}}, {"legs", json::array()}};
			const std::size_t jIndex = passengers[pIndex]["journeys"].size();
			passengers[pIndex]["journeys"].push_back(journey);
			journeysByPersonDestination[personId + "\n" + destination].push_back({pIndex, jIndex});
			report.converted("passengers.das", dasSource);
		}

		readFile(routeChoicePath, content);
		std::stringstream routeInput(content);
		std::string routeLine;
		std::vector<std::string> headers;
		std::vector<std::size_t> transferColumns;
		std::vector<std::size_t> serviceColumns;
		bool routeHeader = true;
		int routeRow = 0;
		while (std::getline(routeInput, routeLine)) {
			if (routeHeader) {
				routeHeader = false;
				headers = splitCsvNaive(routeLine);
				for (std::size_t i = 0; i < headers.size(); ++i) {
					const std::string lower = lowerCopy(headers[i]);
					if (lower.rfind("transfer_n", 0) == 0) transferColumns.push_back(i);
					if (lower.rfind("r_service_lines_id", 0) == 0) serviceColumns.push_back(i);
				}
				continue;
			}
			if (trim(routeLine).empty()) continue;
			++routeRow;
			report.source("passengers.route_choice", routeChoiceSource);
			const auto fields = splitCsvNaive(routeLine);
			if (fields.size() < headers.size()) {
				report.skipped("passengers.route_choice", routeChoiceSource);
				addDiag(SceneSeverity::Warning, "scene.import.parse", "Malformed passenger route-choice row " + std::to_string(routeRow), routeChoicePath.string());
				continue;
			}
			auto column = [&](std::initializer_list<const char*> wanted) -> std::size_t {
				for (std::size_t i = 0; i < headers.size(); ++i) {
					const std::string header = lowerCopy(headers[i]);
					for (const char* value : wanted)
						if (header == value) return i;
				}
				return headers.size();
			};
			const std::size_t personColumn = column({"person_id", "person"});
			const std::size_t destinationColumn = column({"destination"});
			const std::size_t transferCountColumn = column({"nb_transfers"});
			int transferCount = 0;
			if (personColumn == headers.size() || destinationColumn == headers.size() || transferCountColumn == headers.size()
				|| !parseIntegerToken(fields[transferCountColumn], transferCount) || transferCount < 0) {
				report.skipped("passengers.route_choice", routeChoiceSource);
				addDiag(SceneSeverity::Warning, "scene.import.parse", "Passenger route-choice header or transfer count is invalid", routeChoicePath.string(), "passengers.route_choice", true);
				continue;
			}
			const std::string personId = fields[personColumn];
			bool destinationAmbiguous = false;
			const std::string destination = stationIdFor(fields[destinationColumn], destinationAmbiguous);
			const auto journeyIt = journeysByPersonDestination.find(personId + "\n" + destination);
			if (destinationAmbiguous || journeyIt == journeysByPersonDestination.end() || journeyIt->second.size() != 1) {
				report.skipped("passengers.route_choice", routeChoiceSource);
				addDiag(SceneSeverity::Warning, "scene.import.ref", "Passenger route-choice journey reference is missing or ambiguous", routeChoicePath.string(), "passengers.route_choice", true);
				continue;
			}
			const auto target = journeyIt->second.front();
			std::vector<std::string> transferStations;
			for (int i = 0; i < transferCount; ++i) {
				if (static_cast<std::size_t>(i) >= transferColumns.size()) break;
				transferStations.push_back(fields[transferColumns[static_cast<std::size_t>(i)]]);
			}
			if (static_cast<int>(transferStations.size()) != transferCount || static_cast<std::size_t>(transferCount + 1) > serviceColumns.size()) {
				report.skipped("passengers.route_choice", routeChoiceSource);
				addDiag(SceneSeverity::Warning, "scene.import.ref", "Passenger route-choice leg columns are incomplete", routeChoicePath.string(), "passengers.route_choice", true);
				continue;
			}
			std::vector<std::string> legServices;
			for (int i = 0; i <= transferCount; ++i) legServices.push_back(fields[serviceColumns[static_cast<std::size_t>(i)]]);
			std::vector<std::string> legStations;
			legStations.push_back(passengers[target.first]["journeys"][target.second]["origin"].get<std::string>());
			bool transferAmbiguous = false;
			for (const auto& transfer : transferStations) {
				bool ambiguous = false;
				legStations.push_back(stationIdFor(transfer, ambiguous));
				transferAmbiguous = transferAmbiguous || ambiguous;
			}
			legStations.push_back(passengers[target.first]["journeys"][target.second]["destination"].get<std::string>());
			if (transferAmbiguous) {
				addDiag(SceneSeverity::Warning, "scene.import.ref", "Passenger route-choice transfer station is ambiguous",
						routeChoicePath.string(), "passengers.route_choice", true);
			}
			for (std::size_t i = 0; i < legServices.size(); ++i) {
				const std::string token = legServices[i];
				std::string serviceId;
				int occurrence = 1;
				bool serviceAmbiguous = false;
				for (const auto& service : services) {
					const std::string prefix = service.value("operating_code",
							service["id"].get<std::string>()) + "-";
					if (token.rfind(prefix, 0) != 0) continue;
					int candidateOccurrence = 0;
					if (!parseIntegerToken(token.substr(prefix.size()), candidateOccurrence) || candidateOccurrence < 1) continue;
					if (!serviceId.empty()) serviceAmbiguous = true;
					serviceId = service["id"].get<std::string>();
					occurrence = candidateOccurrence;
				}
				if (serviceAmbiguous)
					serviceId.clear();
				bool originAmbiguous = false, destinationAmbiguous = false;
				if (i >= legStations.size() - 1) break;
				const std::string origin = stationIdFor(legStations[i], originAmbiguous);
				const std::string destinationStation = stationIdFor(legStations[i + 1], destinationAmbiguous);
				if (serviceId.empty() || serviceAmbiguous || originAmbiguous || destinationAmbiguous) {
					addDiag(SceneSeverity::Warning, "scene.import.ref", "Passenger route-choice service or station reference is ambiguous", routeChoicePath.string(), "passengers.route_choice", true);
				}
				passengers[target.first]["journeys"][target.second]["legs"].push_back({
					{"id", passengers[target.first]["journeys"][target.second]["id"].get<std::string>() + ".leg." + std::to_string(i + 1)},
					{"origin", origin}, {"destination", destinationStation}, {"service", serviceId.empty() ? token : serviceId}, {"occurrence", occurrence}});
			}
			report.converted("passengers.route_choice", routeChoiceSource);
		}
	} else if (hasDas || hasRouteChoice) {
		if (hasDas) report.skipped("passengers.das", dasSource);
		if (hasRouteChoice) report.skipped("passengers.route_choice", routeChoiceSource);
		addDiag(SceneSeverity::Warning, "scene.import.passengers", "Passenger import requires both exact DAS and route-choice files", passengerDir.string(), "passengers", true);
	}

	json sceneJson = {{"schema_version", 1}, {"name", sceneName},
		{"units", {{"distance", "m"}, {"time", "s"}, {"speed", "m/s"}}}};
	if (knownSettings) {
		sceneJson["base_time"] = baseTime;
		sceneJson["simulation_settings"] = {{"duration_seconds", durationSeconds}, {"buffer_time_seconds", 0.0}, {"recovery_time_percent", 0.0}};
	}
	sceneJson["import_report"] = json::array();
	for (const auto& row : report.rows) {
		sceneJson["import_report"].push_back({{"category", row.category}, {"source_file", row.sourceFile},
			{"source_count", row.sourceCount}, {"converted_count", row.convertedCount},
			{"skipped_count", row.skippedCount}, {"unresolved_references", row.unresolvedReferences}});
	}
	json signalling = {{"signals", json::array()}, {"routes", routes}, {"block_dependencies", blockDependencies},
		{"single_track_restrictions", singleTrackRestrictions}, {"station_boundaries", stationBoundaries}};
	json rollingStock = {{"train_units", trainUnits}, {"compositions", compositions}};
	json servicesFile = {{"services", services}};
	json stationsFile = {{"stations", stations}};
	json scenariosFile = {{"default_scenario_id", "baseline"}, {"scenarios", scenarios}};

	// Preserve atomic staging/publish semantics: no destination is touched while
	// parsing, validation, or passthrough copying can still fail.
	if (hasErrors(result.diagnostics)) return result;
	std::error_code ec;
	fs::path sceneParent = scenePath.parent_path();
	if (sceneParent.empty()) sceneParent = ".";
	fs::create_directories(sceneParent, ec);
	if (ec) {
		addDiag(SceneSeverity::Error, "scene.import.missing", "Cannot create scene parent directory: " + ec.message(), scenePath.string());
		return result;
	}
	std::error_code stagingPathEc;
	const fs::path stagingPath = uniqueSiblingPath(scenePath, "importing", stagingPathEc);
	if (stagingPathEc) {
		addDiag(SceneSeverity::Error, "scene.import.missing", "Cannot inspect scene destination: " + stagingPathEc.message(), scenePath.string());
		return result;
	}
	auto removeStaging = [&]() {
		if (stagingPath.empty()) return;
		std::error_code cleanupEc;
		fs::remove_all(stagingPath, cleanupEc);
	};
	fs::create_directories(stagingPath, ec);
	if (ec) {
		addDiag(SceneSeverity::Error, "scene.import.missing", "Cannot create staging directory: " + ec.message(), scenePath.string());
		removeStaging();
		return result;
	}
	fs::path outLegacy = stagingPath / "legacy";
	fs::create_directories(outLegacy, ec);
	if (ec) {
		addDiag(SceneSeverity::Error, "scene.import.missing", "Cannot create legacy passthrough directory: " + ec.message(), (scenePath / "legacy").string());
		removeStaging();
		return result;
	}
	bool allWritten = true;
	auto writeJson = [&](const std::string& filename, const json& value) {
		std::ofstream output(stagingPath / filename);
		if (!output) {
			addDiag(SceneSeverity::Error, "scene.import.write", "Failed to write " + filename, (scenePath / filename).string());
			allWritten = false;
			return;
		}
		output << value.dump(4) << "\n";
		if (!output.good()) {
			addDiag(SceneSeverity::Error, "scene.import.write", "Failed to write " + filename, (scenePath / filename).string());
			allWritten = false;
		}
	};
	writeJson("scene.json", sceneJson);
	writeJson("infrastructure.json", infrastructure);
	writeJson("stations.json", stationsFile);
	writeJson("signalling.json", signalling);
	writeJson("rolling_stock.json", rollingStock);
	writeJson("services.json", servicesFile);
	writeJson("scenarios.json", scenariosFile);
	if (hasDas && hasRouteChoice) writeJson("passengers.json", {{"passengers", passengers}});
	if (!allWritten) {
		removeStaging();
		return result;
	}

	// Keep the existing passthrough boundary. Canonical files own converted
	// fields; unconverted runtime support folders remain available to the
	// directional exporter without serialising generated caches.
	auto copyInto = [&](const fs::path& from, const fs::path& to) {
		std::error_code copyEc;
		fs::path failedPath = from;
		if (fs::is_directory(from, copyEc)) {
			fs::create_directories(to, copyEc);
			if (!copyEc) {
				for (fs::recursive_directory_iterator it(from, copyEc), end; it != end && !copyEc; it.increment(copyEc)) {
					failedPath = it->path();
					const fs::path relative = fs::relative(it->path(), from, copyEc);
					if (copyEc) break;
					const fs::path target = to / relative;
					if (it->is_directory(copyEc)) fs::create_directories(target, copyEc);
					else if (it->is_regular_file(copyEc)) {
						fs::create_directories(target.parent_path(), copyEc);
						if (!copyEc) fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing, copyEc);
					}
				}
			}
		} else {
			fs::create_directories(to.parent_path(), copyEc);
			if (!copyEc) fs::copy_file(from, to, fs::copy_options::overwrite_existing, copyEc);
		}
		if (copyEc) addDiag(SceneSeverity::Error, "scene.import.missing", "Could not copy legacy data: " + copyEc.message(), failedPath.string());
	};
	std::vector<std::string> passDirs = {"TrackLines", "TMS", "TDS", "GUI", "Rescheduling", "Passengers", "RoutesToWrite"};
	std::unordered_set<std::string> copiedDirs;
	for (const auto& directory : passDirs) {
		const fs::path source = resolvePath(legacyPath, directory);
		if (!fs::is_directory(source, ec)) continue;
		const fs::path canonical = fs::weakly_canonical(source, ec);
		const std::string key = ec ? source.lexically_normal().string() : canonical.string();
		if (copiedDirs.insert(key).second) copyInto(source, outLegacy / source.filename());
	}
	const bool flatInfrastructure = trackRoot == legacyPath;
	if (flatInfrastructure) {
		const fs::path flatTracklines = outLegacy / "Tracklines";
		for (const auto& name : {"Stations.txt", "Connections.txt", "TrackandStations.txt"}) {
			const fs::path source = resolvePath(legacyPath, name);
			if (fs::is_regular_file(source, ec)) copyInto(source, flatTracklines / source.filename());
		}
		for (const auto& track : trackDirs) copyInto(track.second, flatTracklines / track.second.filename());
	}
	if (!timetableRoot.empty() && fs::is_directory(timetableRoot, ec)) {
		for (const auto& entry : fs::directory_iterator(timetableRoot, ec)) {
			if (!entry.is_directory(ec)) continue;
			if (lowerCopy(entry.path().filename().string()).rfind("scenarios_", 0) == 0)
				copyInto(entry.path(), outLegacy / "Timetable" / entry.path().filename());
		}
	}
	if (!routesDir.empty() && fs::is_directory(routesDir, ec)) {
		for (const auto& entry : fs::directory_iterator(routesDir, ec)) {
			if (!entry.is_regular_file(ec)) continue;
			const std::string name = entry.path().filename().string();
			if (lowerCopy(name).rfind("route", 0) != 0) copyInto(entry.path(), outLegacy / "Routes" / name);
		}
	}
	if (hasErrors(result.diagnostics)) {
		removeStaging();
		return result;
	}

	std::error_code destinationEc;
	const bool destinationExists = fs::exists(scenePath, destinationEc);
	if (destinationEc) {
		addDiag(SceneSeverity::Error, "scene.import.missing", "Cannot inspect scene destination: " + destinationEc.message(), scenePath.string());
		removeStaging();
		return result;
	}
	fs::path backupPath;
	if (destinationExists) {
		std::error_code backupEc;
		backupPath = uniqueSiblingPath(scenePath, "backup", backupEc);
		if (backupEc) {
			addDiag(SceneSeverity::Error, "scene.import.missing", "Cannot inspect scene destination: " + backupEc.message(), scenePath.string());
			removeStaging();
			return result;
		}
		fs::rename(scenePath, backupPath, destinationEc);
		if (destinationEc) {
			addDiag(SceneSeverity::Error, "scene.import.missing", "Cannot move existing scene to backup: " + destinationEc.message(), scenePath.string());
			removeStaging();
			return result;
		}
	}
	fs::rename(stagingPath, scenePath, destinationEc);
	if (destinationEc) {
		addDiag(SceneSeverity::Error, "scene.import.missing", "Cannot publish scene destination: " + destinationEc.message(), scenePath.string());
		removeStaging();
		if (!backupPath.empty()) {
			std::error_code restoreEc;
			fs::rename(backupPath, scenePath, restoreEc);
			if (restoreEc) addDiag(SceneSeverity::Error, "scene.import.missing", "Cannot restore existing scene: " + restoreEc.message(), backupPath.string());
		}
		return result;
	}
	if (!backupPath.empty()) {
		std::error_code cleanupEc;
		fs::remove_all(backupPath, cleanupEc);
		if (cleanupEc) addDiag(SceneSeverity::Warning, "scene.import.cleanup", "Could not remove import backup: " + cleanupEc.message(), backupPath.string());
	}
	result.wroteScene = true;
	return result;
}
