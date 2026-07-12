#include "scene/SceneImporter.h"
#include "scene/SceneExporter.h"
#include "scene/SceneValidator.h"
#include "scene/SceneModel.h"
#include <iostream>
#include <map>
#include <string>
#include <tuple>
#include <vector>

static void printDiags(const std::vector<SceneDiagnostic>& diags) {
	using Key = std::tuple<SceneSeverity, std::string, std::string, std::string, std::string>;
	std::map<Key, std::vector<SceneDiagnostic>> groups;
	for (const auto& d : diags) {
		groups[{d.severity, d.code, d.file, d.message, d.suggestedFix}].push_back(d);
	}
	for (const auto& entry : groups) {
		const auto& group = entry.second;
		if (group.size() == 1) {
			std::cerr << toDisplayText(group.front()) << "\n";
			continue;
		}
		SceneDiagnostic summary = group.front();
		summary.itemType.clear();
		summary.itemId.clear();
		summary.path.clear();
		std::cerr << group.size() << "x " << toDisplayText(summary) << "\n";
		for (const auto& d : group) {
			std::cerr << "  (" << (d.itemType.empty() ? "item" : d.itemType) << " " << d.itemId << ")";
			if (!d.path.empty())
				std::cerr << " at " << d.path;
			std::cerr << "\n";
		}
	}
}

int main(int argc, char** argv) {
	if (argc < 2) {
		std::cerr << "Usage:\n"
				  << "  scene_tool import <legacyDir> <sceneDir> [sceneName]\n"
				  << "  scene_tool export <sceneDir> <outDir>\n"
				  << "  scene_tool validate <sceneDir>\n";
		return 1;
	}

	std::string cmd = argv[1];

	if (cmd == "import") {
		if (argc < 4) {
			std::cerr << "Usage: scene_tool import <legacyDir> <sceneDir> [sceneName]\n";
			return 1;
		}
		std::string legacyDir = argv[2];
		std::string sceneDir = argv[3];
		std::string sceneName;
		if (argc >= 5) {
			sceneName = argv[4];
		} else {
			std::string temp = legacyDir;
			while (!temp.empty() && (temp.back() == '/' || temp.back() == '\\'))
				temp.pop_back();
			size_t slash = temp.find_last_of("/\\");
			sceneName = (slash == std::string::npos) ? temp : temp.substr(slash + 1);
			if (sceneName.empty())
				sceneName = "scene";
		}
		auto res = importLegacyScene(legacyDir, sceneDir, sceneName);
		printDiags(res.diagnostics);
		return res.success() ? 0 : 1;
	} else if (cmd == "export") {
		if (argc < 4) {
			std::cerr << "Usage: scene_tool export <sceneDir> <outDir>\n";
			return 1;
		}
		std::string sceneDir = argv[2];
		std::string outDir = argv[3];
		auto res = exportLegacyScene(sceneDir, outDir);
		printDiags(res.diagnostics);
		return res.success() ? 0 : 1;
	} else if (cmd == "validate") {
		if (argc < 3) {
			std::cerr << "Usage: scene_tool validate <sceneDir>\n";
			return 1;
		}
		std::string sceneDir = argv[2];
		auto diags = validateSceneDirectory(sceneDir);
		printDiags(diags);
		return hasErrors(diags) ? 1 : 0;
	} else {
		std::cerr << "Unknown command: " << cmd << "\n";
		return 1;
	}
}
