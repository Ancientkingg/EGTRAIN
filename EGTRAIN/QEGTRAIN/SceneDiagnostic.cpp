#include "SceneDiagnostic.h"

std::string severityLabel(SceneSeverity s) {
	switch (s) {
		case SceneSeverity::Info:
			return "info";
		case SceneSeverity::Warning:
			return "warning";
		case SceneSeverity::Error:
			return "error";
	}
	return "unknown";
}

std::string toDisplayText(const SceneDiagnostic& d) {
	std::string text = severityLabel(d.severity) + " [" + d.code + "]";
	if (!d.file.empty())
		text += " in " + d.file;
	if (!d.itemId.empty()) {
		text += " (" + (d.itemType.empty() ? "item" : d.itemType) + " " + d.itemId + ")";
	}
	if (!d.path.empty())
		text += " at " + d.path;
	text += ": " + d.message;
	if (!d.suggestedFix.empty())
		text += " (Fix: " + d.suggestedFix + ")";
	return text;
}

nlohmann::json toJson(const SceneDiagnostic& d) {
	nlohmann::json j;
	j["severity"] = severityLabel(d.severity);
	j["code"] = d.code;
	j["message"] = d.message;
	j["file"] = d.file;
	j["itemType"] = d.itemType;
	j["itemId"] = d.itemId;
	j["path"] = d.path;
	j["relatedId"] = d.relatedId;
	j["suggestedFix"] = d.suggestedFix;
	return j;
}

bool hasErrors(const std::vector<SceneDiagnostic>& ds) {
	for (const auto& d : ds) {
		if (d.severity == SceneSeverity::Error) {
			return true;
		}
	}
	return false;
}
