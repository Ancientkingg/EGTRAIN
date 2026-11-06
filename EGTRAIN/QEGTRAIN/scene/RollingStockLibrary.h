#pragma once

#include "scene/SceneDiagnostic.h"
#include "scene/SceneModel.h"

#include <string>
#include <vector>

/// A rolling stock unit of the library and the bundled cases that hold it.
struct RollingStockLibraryUnit {
	SceneTrainUnit unit;			///< The unit as its cases store it.
	std::vector<std::string> cases; ///< The folder names of those cases in Scenes, at least one.
};

/// What parseRollingStockLibrary read.
struct RollingStockLibrary {
	std::vector<RollingStockLibraryUnit> units; ///< The units, in the order of the file.
	std::vector<SceneDiagnostic> diagnostics;	///< What could not be read. The units are not complete after an error.
};

/// Reads rolling_stock_library.json, which tools/rolling_stock_library.py writes from the bundled cases. It has the form
/// of rolling_stock.json with no compositions, and is read as that file is, so a unit gets the values it has in its
/// cases. Its key cases holds, for each train unit in the same order, the list of the case folders that hold the unit.
RollingStockLibrary parseRollingStockLibrary(const std::string& text);
