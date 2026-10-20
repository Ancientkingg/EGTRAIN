#ifndef SCENEDIAGNOSTIC_H
#define SCENEDIAGNOSTIC_H

#include <climits>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

enum class SceneSeverity { Info,
	Warning,
	Error };

struct SceneDiagnostic {
	SceneSeverity severity = SceneSeverity::Error;
	std::string code;		  // stable id, e.g. "scene.ref.unresolved"
	std::string message;	  // human sentence naming the item
	std::string file;		  // scene file it concerns, e.g. "services.json" ("" if scene-wide)
	std::string itemType;	  // "service", "station", "stop", "incident", ... ("" if none)
	std::string itemId;		  // affected item id ("" if none)
	std::string path;		  // location inside the file, e.g. "services[svc.a1].stops[2].departure_seconds" ("" if N/A)
	std::string relatedId;	  // other item involved, e.g. the unresolved target id or the duplicate peer ("" if none)
	std::string suggestedFix; // smallest fix, "" if unknown
};

struct SceneDiagnosticCounts {
	int errors = 0;
	int warnings = 0;
	int infos = 0;
};

// Reads a JSON integer into an int. Returns false, leaving output unchanged, when the value is not
// an integer or lies outside [min, max].
inline bool readJsonInt(const nlohmann::json& value, int min, int max, int& output) {
	long long number = 0;
	if (value.is_number_unsigned()) {
		const unsigned long long unsignedNumber = value.get<unsigned long long>();
		if (unsignedNumber > static_cast<unsigned long long>(LLONG_MAX))
			return false;
		number = static_cast<long long>(unsignedNumber);
	} else if (value.is_number_integer()) {
		number = value.get<long long>();
	} else {
		return false;
	}
	if (number < min || number > max)
		return false;
	output = static_cast<int>(number);
	return true;
}

std::string severityLabel(SceneSeverity s);			 // "error"/"warning"/"info"
std::string toDisplayText(const SceneDiagnostic& d); // one line for log/UI list
nlohmann::json toJson(const SceneDiagnostic& d);	 // for tooling / round-trip test
bool hasErrors(const std::vector<SceneDiagnostic>& ds);
SceneDiagnosticCounts countDiagnostics(const std::vector<SceneDiagnostic>& ds);

#endif // SCENEDIAGNOSTIC_H
