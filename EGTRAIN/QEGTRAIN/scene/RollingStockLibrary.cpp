#include "scene/RollingStockLibrary.h"

#include "scene/SceneFieldReader.h"

#include <nlohmann/json.hpp>

#include <string>
#include <utility>

using json = nlohmann::json;

RollingStockLibrary parseRollingStockLibrary(const std::string& text) {
	RollingStockLibrary library;
	const SceneFieldReader reader("rolling_stock_library.json", kSceneFileFieldCodes, library.diagnostics);
	const json root = json::parse(text, nullptr, false);
	if (!root.is_object()) {
		reader.report("scene.json.parse", "The rolling stock library is not a JSON object", "");
		return library;
	}
	// The file has the form of rolling_stock.json, so the loader of the scene reads the units.
	SceneModel scene;
	decodeRollingStock(reader, root, scene);
	// It reads one unit for each entry of train_units, and cases holds a list for each of them, in the same order.
	const bool listed = root.contains("cases") && root["cases"].is_array() && root["cases"].size() == scene.trainUnits.size();
	if (!listed)
		reader.report("scene.field.missing", "cases must hold a list of case folder names for each train unit", "cases");
	for (std::size_t index = 0; index < scene.trainUnits.size(); ++index) {
		RollingStockLibraryUnit libraryUnit{scene.trainUnits[index], {}};
		if (listed) {
			const json& names = root["cases"][index];
			bool valid = names.is_array() && !names.empty();
			for (std::size_t nameIndex = 0; valid && nameIndex < names.size(); ++nameIndex) {
				valid = names[nameIndex].is_string() && !names[nameIndex].get<std::string>().empty();
				if (valid)
					libraryUnit.cases.push_back(names[nameIndex].get<std::string>());
			}
			if (!valid) {
				libraryUnit.cases.clear();
				reader.report("scene.field.missing", "cases must be a list of case folder names",
					"cases[" + std::to_string(index) + "]");
			}
		}
		library.units.push_back(std::move(libraryUnit));
	}
	return library;
}
