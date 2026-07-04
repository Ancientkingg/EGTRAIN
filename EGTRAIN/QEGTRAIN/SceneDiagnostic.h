#ifndef SCENEDIAGNOSTIC_H
#define SCENEDIAGNOSTIC_H

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

std::string severityLabel(SceneSeverity s);			 // "error"/"warning"/"info"
std::string toDisplayText(const SceneDiagnostic& d); // one line for log/UI list
nlohmann::json toJson(const SceneDiagnostic& d);	 // for tooling / round-trip test
bool hasErrors(const std::vector<SceneDiagnostic>& ds);
SceneDiagnosticCounts countDiagnostics(const std::vector<SceneDiagnostic>& ds);

#endif // SCENEDIAGNOSTIC_H
