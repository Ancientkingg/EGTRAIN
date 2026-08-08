#include "scene/SceneExporter.h"
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

	SceneLoadResult loadRes = loadScene(sceneDir);
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
			if (isSwitchTransitionRouteEntry(b)) {
				rf << b << "\n";
			} else {
				rf << "@" << b << "@\n";
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
				if (inc.type == "signal_failure" && routeBlockTokens.find(inc.target) == routeBlockTokens.end()) {
					addDiag(SceneSeverity::Warning, "scene.export.adjusted",
							"Signal id " + inc.target + " matches no route block so the failure will have no effect", inc.id);
				}
				std::string target = inc.target;
				if (inc.type == "train_breakdown") {
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
		addDiag(SceneSeverity::Info, "scene.export.info", "scene has no legacy data; only train, timetable, rolling stock, and route files were written");
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

	if (result.success()) {
		synthesizeSignallingAreas(outDir, result);
		synthesizeGuiLayout(outDir, result);
	}

	return result;
}
