#include "scene/SceneDiagnostic.h"

#include <algorithm>

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

std::string itemLabel(const SceneDiagnostic& d) {
	if (d.itemId.empty())
		return {};
	return (d.itemType.empty() ? "item" : d.itemType) + " " + d.itemId;
}

std::string toDisplayText(const SceneDiagnostic& d) {
	std::string text = severityLabel(d.severity) + " [" + d.code + "]";
	if (!d.file.empty())
		text += " in " + d.file;
	if (!d.itemId.empty())
		text += " (" + itemLabel(d) + ")";
	if (!d.path.empty())
		text += " at " + d.path;
	text += ": " + d.message;
	if (!d.suggestedFix.empty())
		text += " (Fix: " + d.suggestedFix + ")";
	return text;
}

std::string toDialogText(const SceneDiagnostic& d) {
	return d.suggestedFix.empty() ? d.message : d.message + "\n\n" + d.suggestedFix;
}

SceneDiagnostic jsonParseDiagnostic(const std::string& code, const std::string& name, const std::string& file,
	const std::string& content, const nlohmann::json::parse_error& error) {
	// error.byte counts the characters the parser read, the one it stopped at included.
	const std::size_t read = std::min(error.byte, content.size());
	const std::size_t lineStart = content.rfind('\n', read == 0 ? std::string::npos : read - 1);
	const std::size_t newlines = static_cast<std::size_t>(std::count(content.begin(), content.begin() + static_cast<std::ptrdiff_t>(read), '\n'));
	const std::size_t column = lineStart == std::string::npos || read == 0 ? read : read - (lineStart + 1);
	SceneDiagnostic diagnostic;
	diagnostic.code = code;
	diagnostic.file = file;
	diagnostic.message = name + " is not valid JSON (line " + std::to_string(newlines + 1) + ", column " + std::to_string(column) + ").";
	diagnostic.suggestedFix = "Correct the file at that line or restore it from a copy.";
	return diagnostic;
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

SceneDiagnosticCounts countDiagnostics(const std::vector<SceneDiagnostic>& ds) {
	SceneDiagnosticCounts counts;
	for (const auto& d : ds) {
		switch (d.severity) {
			case SceneSeverity::Error:
				++counts.errors;
				break;
			case SceneSeverity::Warning:
				++counts.warnings;
				break;
			case SceneSeverity::Info:
				++counts.infos;
				break;
		}
	}
	return counts;
}
