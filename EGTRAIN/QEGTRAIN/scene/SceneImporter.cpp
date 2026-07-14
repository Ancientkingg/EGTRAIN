#include "scene/SceneImporter.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <chrono>
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

static fs::path uniqueSiblingPath(const fs::path& target, const std::string& tag) {
	fs::path parent = target.parent_path();
	if (parent.empty())
		parent = ".";
	std::string base = target.filename().string();
	if (base.empty())
		base = "scene";
	const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	for (unsigned int suffix = 0;; ++suffix) {
		fs::path candidate = parent / (base + "." + tag + "." + std::to_string(stamp) + "." + std::to_string(suffix));
		std::error_code ec;
		if (!fs::exists(candidate, ec) && !ec)
			return candidate;
	}
}

SceneImportResult importLegacyScene(const std::string& legacyDir,
									const std::string& sceneDir,
									const std::string& sceneName) {
	SceneImportResult result;

	auto addDiag = [&](SceneSeverity sev, const std::string& code, const std::string& msg, const std::string& file = "") {
		SceneDiagnostic d;
		d.severity = sev;
		d.code = code;
		d.message = msg;
		d.file = file;
		result.diagnostics.push_back(d);
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
	if (!fs::exists(legacyPath) || !fs::is_directory(legacyPath)) {
		addDiag(SceneSeverity::Error, "scene.import.missing", "Legacy directory missing or invalid", legacyPath.string());
		return result;
	}

	std::vector<std::string> trainFiles;
	fs::path manifestPath = resolvePath(legacyPath, "trainNames.txt");
	if (fs::exists(manifestPath)) {
		std::string content;
		if (readFile(manifestPath, content)) {
			std::stringstream ss(content);
			std::string line;
			while (std::getline(ss, line)) {
				line = trim(line);
				if (line.empty())
					continue;
				std::string lowerLine = line;
				std::transform(lowerLine.begin(), lowerLine.end(), lowerLine.begin(), ::tolower);
				// The legacy loader skips manifest entries without "txt" in the
				// name; folders and other files are filtered the same way here.
				if (lowerLine.find("txt") != std::string::npos) {
					trainFiles.push_back("Trains/" + line);
				}
			}
		}
	} else {
		addDiag(SceneSeverity::Info, "scene.import.info", "trainNames.txt missing, falling back to directory enumeration");
		fs::path trainsDir = resolvePath(legacyPath, "Trains");
		std::error_code ec;
		if (fs::is_directory(trainsDir, ec)) {
			for (const auto& entry : fs::directory_iterator(trainsDir, ec)) {
				if (entry.is_regular_file()) {
					std::string name = entry.path().filename().string();
					std::string ext = entry.path().extension().string();
					std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
					if (ext == ".txt" && name != "trainNames.txt") {
						trainFiles.push_back("Trains/" + name);
					}
				}
			}
		}
	}

	json servicesArr = json::array();
	std::unordered_set<std::string> referencedRoutes;
	std::unordered_map<std::string, std::pair<std::string, std::string>> trainUnitsMap; // stem -> {data, traction}

	for (const auto& tf : trainFiles) {
		fs::path tfPath = resolvePath(legacyPath, tf);
		std::string content;
		if (!readFile(tfPath, content)) {
			addDiag(SceneSeverity::Error, "scene.import.missing", "Missing train file: " + tf, tfPath.string());
			continue;
		}
		std::vector<std::string> tokens = readTokens(content);
		if (tokens.size() < 7) {
			addDiag(SceneSeverity::Error, "scene.import.parse", "Malformed Trains file, need 7 tokens", tfPath.string());
			continue;
		}

		std::string sName = tokens[0];
		double entryTime = 0.0;
		double headway = 0.0;
		int routeIndex = 0;
		try {
			entryTime = std::stod(tokens[1]);
			headway = std::stod(tokens[2]);
			routeIndex = std::stoi(tokens[3]);
		} catch (...) {
			addDiag(SceneSeverity::Error, "scene.import.parse", "Invalid numbers in Trains file", tfPath.string());
			continue;
		}

		std::string origName = tokens[0];
		sName = origName;
		int suffix = 2;
		while (true) {
			bool conflict = false;
			for (const auto& existing : servicesArr) {
				if (existing["id"] == sName) {
					conflict = true;
					break;
				}
			}
			if (!conflict)
				break;
			sName = origName + "_" + std::to_string(suffix++);
		}
		if (sName != origName) {
			addDiag(SceneSeverity::Warning, "scene.import.adjusted",
					"Duplicate service name " + origName + " renamed to " + sName, tf);
		}

		std::string routeId = "route" + std::to_string(routeIndex);
		std::string dataPath = tokens[4];
		std::string tractionPath = tokens[5];
		std::string timetablePath = tokens[6];

		referencedRoutes.insert(routeId);

		fs::path dp(dataPath);
		std::string stem = dp.stem().string();
		trainUnitsMap[stem] = {dataPath, tractionPath};

		fs::path ttPath = resolvePath(legacyPath, timetablePath);
		std::string ttContent;
		if (!readFile(ttPath, ttContent)) {
			addDiag(SceneSeverity::Error, "scene.import.ref", "Missing timetable file: " + timetablePath, ttPath.string());
			continue;
		}

		json stopsArr = json::array();
		std::stringstream ttSs(ttContent);
		std::string ttLine;
		int row = 0;
		while (std::getline(ttSs, ttLine)) {
			std::vector<std::string> ttTokens = readTokens(ttLine);
			if (ttTokens.empty())
				continue;
			if (ttTokens.size() < 4) {
				addDiag(SceneSeverity::Error, "scene.import.parse", "Malformed timetable row " + std::to_string(row), ttPath.string());
				continue;
			}

			size_t n = ttTokens.size();
			double dwell = 0, arrival = 0, departure = 0;
			try {
				dwell = std::stod(ttTokens[n - 3]);
				arrival = std::stod(ttTokens[n - 2]);
				departure = std::stod(ttTokens[n - 1]);
			} catch (...) {
				addDiag(SceneSeverity::Error, "scene.import.parse", "Invalid numbers in timetable row " + std::to_string(row), ttPath.string());
				continue;
			}

			// Multi-token station names are joined without a separator: the
			// legacy data writes e.g. "Morengo Bariano" in timetables for the
			// station "MorengoBariano" in Stations.txt.
			std::string stationName = ttTokens[0];
			for (size_t i = 1; i < n - 3; ++i) {
				stationName += ttTokens[i];
			}

			json stopObj = {
				{"station", stationName},
				{"dwell_seconds", dwell}};
			// -1 means "not defined" in legacy timetables: typically the first
			// stop has no arrival and the last stop has no departure, but some
			// mid-service stops also carry no scheduled arrival.
			if (arrival != -1) {
				stopObj["arrival_seconds"] = arrival;
			}
			// Whether this is the last stop is only known after the loop, so the
			// departure is kept for every stop and removed from the terminus below.
			double outDeparture = departure;
			if (departure == -1 && arrival != -1) {
				outDeparture = arrival;
			} else if (departure != -1 && arrival != -1 && departure < arrival) {
				outDeparture = arrival;
				addDiag(SceneSeverity::Warning, "scene.import.adjusted",
						"Departure " + ttTokens[n - 1] + " before arrival " + ttTokens[n - 2] + " at " + stationName + " of " + sName + "; departure raised to the arrival", timetablePath);
			}
			stopObj["departure_seconds"] = outDeparture;
			stopObj["_orig_departure"] = departure;
			stopsArr.push_back(stopObj);
			row++;
		}

		if (!stopsArr.empty()) {
			auto& lastStop = stopsArr.back();
			if (lastStop["_orig_departure"] == -1.0) {
				lastStop.erase("departure_seconds");
			}
			// A non-terminus stop whose departure was -1 borrowed its arrival above.
			for (size_t i = 0; i + 1 < stopsArr.size(); ++i) {
				if (stopsArr[i]["_orig_departure"] == -1.0) {
					addDiag(SceneSeverity::Warning, "scene.import.adjusted",
							"Departure -1 on non-last stop of " + sName + "; departure set to the arrival", timetablePath);
				}
			}
			for (auto& s : stopsArr) {
				s.erase("_orig_departure");
			}
		}
		// A service without stops is a through service (freight and long
		// distance trains cross the corridor without scheduled stops). Mark
		// it so the validator does not ask for stops.

		json serviceObj = {
			{"id", sName},
			{"composition", stem},
			{"route", routeId},
			{"entry_time_seconds", entryTime},
			{"stops", stopsArr}};
		if (stopsArr.empty()) {
			serviceObj["through"] = true;
		}
		if (headway < 99999999) {
			serviceObj["repeat"] = {{"headway_seconds", headway}};
		}
		servicesArr.push_back(serviceObj);
	}

	json trainUnitsArr = json::array();
	json compositionsArr = json::array();
	for (const auto& kv : trainUnitsMap) {
		std::string stem = kv.first;
		std::string dataPath = kv.second.first;
		std::string tractionPath = kv.second.second;

		fs::path resolvedData = resolvePath(legacyPath, dataPath);
		std::string dContent;
		if (!readFile(resolvedData, dContent)) {
			addDiag(SceneSeverity::Error, "scene.import.ref", "Missing train data file: " + dataPath, resolvedData.string());
			continue;
		}
		std::vector<std::string> dTokens = readTokens(dContent);
		if (dTokens.size() < 9) {
			addDiag(SceneSeverity::Error, "scene.import.parse", "Malformed train data file", resolvedData.string());
			continue;
		}

		std::vector<double> phys;
		try {
			for (int i = 0; i < 9; ++i)
				phys.push_back(std::stod(dTokens[i]));
		} catch (...) {
			addDiag(SceneSeverity::Error, "scene.import.parse", "Invalid numbers in train data file", resolvedData.string());
			continue;
		}

		fs::path resolvedTraction = resolvePath(legacyPath, tractionPath);
		std::string tContent;
		json tracArr = json::array();
		if (!readFile(resolvedTraction, tContent)) {
			addDiag(SceneSeverity::Error, "scene.import.ref", "Missing traction file: " + tractionPath, resolvedTraction.string());
			continue;
		}

		std::stringstream tSs(tContent);
		std::string tLine;
		int row = 0;
		while (std::getline(tSs, tLine)) {
			std::vector<std::string> tTokens = readTokens(tLine);
			if (tTokens.empty())
				continue;
			if (tTokens.size() < 5) {
				addDiag(SceneSeverity::Error, "scene.import.parse", "Malformed traction row " + std::to_string(row), resolvedTraction.string());
				continue;
			}
			try {
				tracArr.push_back({std::stod(tTokens[0]), std::stod(tTokens[1]), std::stod(tTokens[2]), std::stod(tTokens[3]), std::stod(tTokens[4])});
			} catch (...) {
				addDiag(SceneSeverity::Error, "scene.import.parse", "Invalid numbers in traction row " + std::to_string(row), resolvedTraction.string());
				continue;
			}
			row++;
		}

		trainUnitsArr.push_back({{"id", stem},
								 {"physical", {{"mass_of_traction_unit_kg", phys[0]}, {"mass_of_a_wagon_kg", phys[1]}, {"number_of_wagons", phys[2]}, {"max_speed_ms", phys[3]}, {"max_deceleration_ms2", phys[4]}, {"frontal_area_m2", phys[5]}, {"resistance_coefficient", phys[6]}, {"jerk_ms3", phys[7]}, {"length_m", phys[8]}}},
								 {"traction_curve", tracArr},
								 {"source", {{"data_file", dataPath}, {"traction_file", tractionPath}}}});

		compositionsArr.push_back({{"id", stem},
								   {"units", {stem}}});
	}

	json routesArr = json::array();
	fs::path rDirForScan = resolvePath(legacyPath, "Routes");
	std::vector<std::pair<int, fs::path>> routeFiles;
	std::error_code scanEc;
	if (fs::is_directory(rDirForScan, scanEc)) {
		for (const auto& entry : fs::directory_iterator(rDirForScan, scanEc)) {
			if (entry.is_regular_file()) {
				std::string name = entry.path().filename().string();
				std::string ext = entry.path().extension().string();
				std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
				if (ext == ".txt") {
					std::string stem = entry.path().stem().string();
					if (stem.length() > 5 && stem.substr(0, 5) == "Route") {
						std::string numPart = stem.substr(5);
						bool isNum = !numPart.empty() && numPart.length() <= 9 &&
									 std::all_of(numPart.begin(), numPart.end(), [](char value) {
										 return std::isdigit(static_cast<unsigned char>(value));
									 });
						if (isNum) {
							routeFiles.push_back({std::stoi(numPart), entry.path()});
						}
					}
				}
			}
		}
	}
	std::sort(routeFiles.begin(), routeFiles.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

	std::unordered_set<std::string> loadedRoutes;
	for (const auto& pair : routeFiles) {
		int routeIdx = pair.first;
		std::string rId = "route" + std::to_string(routeIdx);
		std::string rPath = "Routes/" + pair.second.filename().string();
		std::string rContent;
		if (!readFile(pair.second, rContent)) {
			addDiag(SceneSeverity::Error, "scene.import.ref", "Missing route file: " + rPath, pair.second.string());
			continue;
		}

		json blocksArr = json::array();
		std::stringstream rSs(rContent);
		std::string rLine;
		int row = 0;
		while (std::getline(rSs, rLine)) {
			std::vector<std::string> rTokens = readTokens(rLine);
			if (rTokens.empty())
				continue;
			std::string token = rTokens[0];
			size_t first = token.find('@');
			size_t last = token.rfind('@');
			if (first != std::string::npos && last != std::string::npos && first != last && first == 0 && last == token.length() - 1) {
				blocksArr.push_back(token.substr(1, token.length() - 2));
			} else if (isSwitchTransitionRouteToken(token)) {
				// Netherlands route files use switch-transition tokens such
				// as @a@-x/@b@-y. V1 stores route entries as opaque strings.
				blocksArr.push_back(token);
			} else {
				addDiag(SceneSeverity::Warning, "scene.import.parse", "Malformed route block token at row " + std::to_string(row), rPath);
				blocksArr.push_back(token);
			}
			row++;
		}

		routesArr.push_back({{"id", rId},
							 {"blocks", blocksArr}});
		loadedRoutes.insert(rId);
	}

	for (const auto& rId : referencedRoutes) {
		if (loadedRoutes.find(rId) == loadedRoutes.end()) {
			std::string idxStr = rId.substr(5);
			std::string rPath = "Routes/Route" + idxStr + ".txt";
			addDiag(SceneSeverity::Error, "scene.import.ref", "Missing route file: " + rPath,
					resolvePath(legacyPath, rPath).string());
		}
	}

	json stationsArr = json::array();
	fs::path stationsPath = resolvePath(legacyPath, "Tracklines/Stations.txt");
	if (!fs::exists(stationsPath)) {
		stationsPath = resolvePath(legacyPath, "TrackLines/Stations.txt");
	}
	const bool flatInfrastructure = !fs::exists(stationsPath)
		&& fs::is_regular_file(resolvePath(legacyPath, "Stations.txt"));
	if (flatInfrastructure)
		stationsPath = resolvePath(legacyPath, "Stations.txt");
	if (fs::exists(stationsPath)) {
		std::string stContent;
		if (readFile(stationsPath, stContent)) {
			std::stringstream stSs(stContent);
			std::string stLine;
			int row = 0;
			while (std::getline(stSs, stLine)) {
				if (trim(stLine).empty())
					continue;
				size_t tabPos = stLine.find('\t');
				std::string name;
				double pos = 0;
				if (tabPos != std::string::npos) {
					std::string posStr = stLine.substr(0, tabPos);
					name = stLine.substr(tabPos + 1);
					try {
						pos = std::stod(trim(posStr));
					} catch (...) {
						addDiag(SceneSeverity::Error, "scene.import.parse", "Invalid pos in station row " + std::to_string(row), stationsPath.string());
						continue;
					}
				} else {
					std::vector<std::string> stTokens = readTokens(stLine);
					if (stTokens.size() < 2) {
						addDiag(SceneSeverity::Error, "scene.import.parse", "Malformed station row " + std::to_string(row), stationsPath.string());
						continue;
					}
					try {
						pos = std::stod(stTokens[0]);
					} catch (...) {
						addDiag(SceneSeverity::Error, "scene.import.parse", "Invalid pos in station row " + std::to_string(row), stationsPath.string());
						continue;
					}
					name = stLine.substr(stLine.find(stTokens[1]));
				}
				name = trim(name);
				if (name.empty()) {
					addDiag(SceneSeverity::Error, "scene.import.parse", "Empty station name in row " + std::to_string(row), stationsPath.string());
					continue;
				}

				stationsArr.push_back({{"id", name},
									   {"name", name},
									   {"position_km", pos},
									   {"platforms", json::array()}});
				row++;
			}
		}
	} else {
		addDiag(SceneSeverity::Error, "scene.import.missing", "Missing Tracklines/Stations.txt", stationsPath.string());
	}

	json sceneJson = {
		{"schema_version", 1},
		{"name", sceneName},
		{"legacy_source", legacyDir}};
	json infraJson = {
		{"nodes", json::array()},
		{"arcs", json::array()}};
	json signallingJson = {
		{"signals", json::array()},
		{"routes", routesArr}};
	json rollingJson = {
		{"train_units", trainUnitsArr},
		{"compositions", compositionsArr}};
	json outServicesJson = {
		{"services", servicesArr}};
	json outStationsJson = {
		{"stations", stationsArr}};

	// Do not touch the selected destination after any parser/reference error.
	if (hasErrors(result.diagnostics))
		return result;

	std::error_code ec;
	fs::path sceneParent = scenePath.parent_path();
	if (sceneParent.empty())
		sceneParent = ".";
	fs::create_directories(sceneParent, ec);
	if (ec) {
		addDiag(SceneSeverity::Error, "scene.import.missing", "Cannot create scene parent directory: " + ec.message(), scenePath.string());
		return result;
	}

	const fs::path stagingPath = uniqueSiblingPath(scenePath, "importing");
	fs::path backupPath;
	auto removeStaging = [&]() {
		std::error_code cleanupEc;
		fs::remove_all(stagingPath, cleanupEc);
	};

	fs::create_directories(stagingPath, ec);
	if (ec) {
		addDiag(SceneSeverity::Error, "scene.import.missing", "Cannot create staging directory: " + ec.message(), scenePath.string());
		removeStaging();
		return result;
	}

	// Reimports replace the passthrough subtree exactly inside staging; the
	// existing destination remains untouched until the final publish rename.
	fs::path outLegacy = stagingPath / "legacy";
	fs::create_directories(outLegacy, ec);
	if (ec) {
		addDiag(SceneSeverity::Error, "scene.import.missing",
				"Cannot create legacy passthrough directory: " + ec.message(), (scenePath / "legacy").string());
		removeStaging();
		return result;
	}

	bool allWritten = true;
	auto writeJson = [&](const std::string& filename, const json& j) {
		const fs::path outputPath = stagingPath / filename;
		std::ofstream out(outputPath);
		out << j.dump(4) << "\n";
		if (!out.good()) {
			addDiag(SceneSeverity::Error, "scene.import.missing", "Failed to write " + filename,
					(scenePath / filename).string());
			allWritten = false;
		}
	};

	writeJson("scene.json", sceneJson);
	writeJson("infrastructure.json", infraJson);
	writeJson("signalling.json", signallingJson);
	writeJson("rolling_stock.json", rollingJson);
	writeJson("services.json", outServicesJson);
	writeJson("stations.json", outStationsJson);
	if (!allWritten) {
		removeStaging();
		return result;
	}

	// Legacy passthrough: unconverted runtime inputs travel with the scene so
	// the future exporter can rebuild a runnable legacy folder from it alone.
	auto copyInto = [&](const fs::path& from, const fs::path& to) {
		std::error_code copyEc;
		fs::path failedPath = from;
		if (fs::is_directory(from, copyEc)) {
			fs::create_directories(to, copyEc);
			if (!copyEc) {
				for (fs::recursive_directory_iterator it(from, copyEc), end; it != end && !copyEc; it.increment(copyEc)) {
					failedPath = it->path();
					const fs::path relative = fs::relative(it->path(), from, copyEc);
					if (copyEc)
						break;
					const fs::path target = to / relative;
					if (it->is_directory(copyEc)) {
						fs::create_directories(target, copyEc);
					} else if (it->is_regular_file(copyEc)) {
						fs::create_directories(target.parent_path(), copyEc);
						if (!copyEc)
							fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing, copyEc);
					}
				}
			}
		} else {
			fs::create_directories(to.parent_path(), copyEc);
			if (!copyEc)
				fs::copy_file(from, to, fs::copy_options::overwrite_existing, copyEc);
		}
		if (copyEc) {
			addDiag(SceneSeverity::Error, "scene.import.missing",
					"Could not copy legacy data " + from.filename().string() + ": " + copyEc.message(), failedPath.string());
		}
	};
	std::vector<std::string> passDirs = {"Tracklines", "TrackLines", "TMS", "TDS", "GUI", "Rescheduling", "Passengers", "RoutesToWrite"};
	std::unordered_set<std::string> copiedDirs;
	for (const auto& d : passDirs) {
		fs::path p = resolvePath(legacyPath, d);
		if (fs::is_directory(p, ec)) {
			// Canonical keys stop the same directory being copied twice when the
			// filesystem is case-insensitive and both name variants resolve.
			fs::path canon = fs::weakly_canonical(p, ec);
			std::string key = ec ? p.lexically_normal().string() : canon.string();
			if (copiedDirs.insert(key).second) {
				copyInto(p, outLegacy / p.filename());
			}
		}
	}
	if (flatInfrastructure) {
		const fs::path flatTracklines = outLegacy / "Tracklines";
		const std::vector<std::string> flatFiles = {"Stations.txt", "Connections.txt", "TrackandStations.txt"};
		for (const auto& name : flatFiles) {
			fs::path source = resolvePath(legacyPath, name);
			if (fs::is_regular_file(source, ec))
				copyInto(source, flatTracklines / source.filename());
		}
		for (const auto& entry : fs::directory_iterator(legacyPath, ec)) {
			if (!entry.is_directory())
				continue;
			const std::string name = entry.path().filename().string();
			const bool isBlock = name.size() > 1 && name[0] == 'B'
				&& std::all_of(name.begin() + 1, name.end(), [](char c) {
					return std::isdigit(static_cast<unsigned char>(c));
				});
			if (isBlock)
				copyInto(entry.path(), flatTracklines / name);
		}
	}
	// Scenario folders live inside the timetable directory next to the
	// per-service files; only the folders are passed through.
	fs::path ttDir = resolvePath(legacyPath, "Timetable");
	if (fs::is_directory(ttDir, ec)) {
		for (const auto& entry : fs::directory_iterator(ttDir, ec)) {
			if (entry.is_directory()) {
				std::string dirName = entry.path().filename().string();
				if (dirName.find("Scenarios_") == 0) {
					copyInto(entry.path(), outLegacy / "Timetable" / dirName);
				}
			}
		}
	}
	// Routes/ keeps its non-Route<N> support files.
	fs::path rDir = resolvePath(legacyPath, "Routes");
	if (fs::is_directory(rDir, ec)) {
		for (const auto& entry : fs::directory_iterator(rDir, ec)) {
			if (entry.is_regular_file()) {
				std::string name = entry.path().filename().string();
				if (name.find("Route") != 0 && name.find("route") != 0) {
					copyInto(entry.path(), outLegacy / "Routes" / name);
				}
			}
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
	if (destinationExists) {
		backupPath = uniqueSiblingPath(scenePath, "backup");
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
			if (restoreEc)
				addDiag(SceneSeverity::Error, "scene.import.missing", "Cannot restore existing scene: " + restoreEc.message(), backupPath.string());
			else
				backupPath.clear();
		}
		return result;
	}

	if (!backupPath.empty()) {
		std::error_code cleanupEc;
		fs::remove_all(backupPath, cleanupEc);
		if (cleanupEc) {
			// Publishing already succeeded; leave the valid destination in place
			// and report cleanup as a warning so callers can retry removal.
			addDiag(SceneSeverity::Warning, "scene.import.cleanup",
					"Could not remove import backup: " + cleanupEc.message(), backupPath.string());
		}
	}
	result.wroteScene = true;
	return result;
}
