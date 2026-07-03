#include "SceneExporter.h"
#include "SceneModel.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <regex>

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

static std::string formatNumber(double val) {
	if (std::floor(val) == val) {
		return std::to_string(static_cast<long long>(val));
	}
	std::ostringstream oss;
	oss.precision(17);
	oss << val;
	return oss.str();
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

	std::unordered_map<std::string, const SceneTrainUnit*> unitMap;
	for (const auto& u : scene.trainUnits) {
		unitMap[u.id] = &u;
	}

	std::unordered_map<std::string, const SceneComposition*> compMap;
	for (const auto& c : scene.compositions) {
		compMap[c.id] = &c;
	}

	// Build route indices. All ids matching route<N> reuse N (round-trip
	// fidelity); all non-conforming ids get sequential indices; a mix would
	// silently rename some routes on re-import, so it is rejected.
	std::unordered_map<std::string, int> routeIndices;
	size_t conforming = 0;
	for (const auto& r : scene.routes) {
		if (r.id.length() > 5 && r.id.substr(0, 5) == "route") {
			std::string numStr = r.id.substr(5);
			if (!numStr.empty() && numStr.length() <= 9 && std::all_of(numStr.begin(), numStr.end(), ::isdigit)) {
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
			rf << "@" << b << "@\n";
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
			entryTime = svc.stops[0].departureSeconds;
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

		const SceneComposition* comp = nullptr;
		if (compMap.find(svc.composition) != compMap.end()) {
			comp = compMap[svc.composition];
		}

		const SceneTrainUnit* unit = nullptr;
		if (comp && !comp->units.empty() && unitMap.find(comp->units[0]) != unitMap.end()) {
			unit = unitMap[comp->units[0]];
		}

		if (!unit) {
			addDiag(SceneSeverity::Error, "scene.export.ref", "Missing unit for service", svc.id);
			result.wroteAll = false;
			trainFiles.pop_back();
			continue;
		}

		if (!unit->hasPhysical || unit->tractionCurve.empty()) {
			addDiag(SceneSeverity::Error, "scene.export.ref", "Unit missing physical or traction data: " + unit->id, svc.id);
			result.wroteAll = false;
			trainFiles.pop_back();
			continue;
		}

		std::string dataPathRel = unit->sourceDataFile;
		if (dataPathRel.empty()) {
			dataPathRel = "TrainData/" + unit->id + ".txt";
		} else {
			// Keep the basename and put in TrainData/
			dataPathRel = "TrainData/" + fs::path(dataPathRel).filename().string();
		}

		std::string tracPathRel = unit->sourceTractionFile;
		if (tracPathRel.empty()) {
			tracPathRel = "TrainData/T_" + unit->id + ".txt";
		} else {
			tracPathRel = "TrainData/" + fs::path(tracPathRel).filename().string();
		}

		std::string ttPathRel = "TimeTable/" + fname;

		// Write unit files if not written
		std::ofstream df(fs::path(outDir) / dataPathRel);
		if (df) {
			df << formatNumber(unit->physical.mass_of_traction_unit_kg) << "\t"
			   << formatNumber(unit->physical.mass_of_a_wagon_kg) << "\t"
			   << formatNumber(unit->physical.number_of_wagons) << "\t"
			   << formatNumber(unit->physical.max_speed_ms) << "\t"
			   << formatNumber(unit->physical.max_deceleration_ms2) << "\t"
			   << formatNumber(unit->physical.frontal_area_m2) << "\t"
			   << formatNumber(unit->physical.resistance_coefficient) << "\t"
			   << formatNumber(unit->physical.jerk_ms3) << "\t"
			   << formatNumber(unit->physical.length_m) << "\n";
		} else {
			addDiag(SceneSeverity::Error, "scene.export.write", "Failed to write train data file", dataPathRel);
			result.wroteAll = false;
		}

		std::ofstream tf(fs::path(outDir) / tracPathRel);
		if (tf) {
			for (const auto& row : unit->tractionCurve) {
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
				std::string arr = stop.hasArrival ? formatNumber(stop.arrivalSeconds) : "-1";
				std::string dep = stop.hasDeparture ? formatNumber(stop.departureSeconds) : "-1";
				ttf << stId << "\t" << formatNumber(stop.dwellSeconds) << "\t" << arr << "\t" << dep << "\n";
			}
		} else {
			addDiag(SceneSeverity::Error, "scene.export.write", "Failed to write timetable file", ttPathRel);
			result.wroteAll = false;
		}

		std::ofstream trf(fs::path(outDir) / "Trains" / fname);
		if (trf) {
			trf << svc.id << "\n"
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

	return result;
}
