#ifndef SCENEDIAGNOSTIC_H
#define SCENEDIAGNOSTIC_H

#include <climits>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

/// How serious a diagnostic is.
enum class SceneSeverity { Info, ///< Notes what an import or export did.
	Warning,					 ///< Data that still runs, such as a historical schedule.
	Error };					 ///< Fails the operation and disables Run.

/// One finding of loading, validation, saving, import, export or migration. Empty text fields do not apply.
struct SceneDiagnostic {
	SceneSeverity severity = SceneSeverity::Error; ///< How serious it is.
	/// Stable id "scene.<area>.<problem>", lower case with dots, such as "scene.ref.unresolved". Areas include import,
	/// export, bundle, migration, compatibility and native (runtime builder). Tests match on codes.
	std::string code;
	std::string message;	  ///< One sentence that names the item.
	std::string file;		  ///< Scene file it concerns, such as "services.json"; empty if scene-wide.
	std::string itemType;	  ///< Kind of item: "service", "station", "stop", "incident" and so on; empty if none.
	std::string itemId;		  ///< Id of the affected item; empty if none.
	std::string path;		  ///< Place inside the file, such as "services[svc.a1].stops[2].departure_seconds"; empty if none.
	std::string relatedId;	  ///< Other item involved, such as the unresolved target id or the duplicate; empty if none.
	std::string suggestedFix; ///< Smallest fix; empty if unknown.
};

/// Number of diagnostics of each severity.
struct SceneDiagnosticCounts {
	int errors = 0;	  ///< Diagnostics of severity Error.
	int warnings = 0; ///< Diagnostics of severity Warning.
	int infos = 0;	  ///< Diagnostics of severity Info.
};

/// Reads a JSON integer into an int. Returns false, leaving output unchanged, when the value is not
/// an integer or lies outside [min, max].
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

std::string severityLabel(SceneSeverity s); ///< "error", "warning" or "info".
/// One line for the log and the console: severity, code, file, item, path, message and suggested fix.
std::string toDisplayText(const SceneDiagnostic& d);
/// The text of a dialog that shows the diagnostic: the message, and the suggested fix as a second paragraph when there is
/// one.
std::string toDialogText(const SceneDiagnostic& d);
/// An object with every field of the diagnostic, the severity as its label, for tools and tests.
nlohmann::json toJson(const SceneDiagnostic& d);
/// True when a diagnostic has severity Error.
bool hasErrors(const std::vector<SceneDiagnostic>& ds);
/// The number of diagnostics of each severity.
SceneDiagnosticCounts countDiagnostics(const std::vector<SceneDiagnostic>& ds);

#endif // SCENEDIAGNOSTIC_H
