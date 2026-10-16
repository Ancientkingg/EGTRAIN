#pragma once

#include "graphics/RuntimeGeometrySource.h"

#include <memory>

// The source of the runtime canvas that reads the simulation globals: blockSets, StationArray, connections,
// signalling_block_sections, train_route, AllStationPlatforms and initial_variables. It is the only code
// outside the simulation that reads them for the canvas.
std::unique_ptr<RuntimeGeometrySource> makeRuntimeGeometryFromGlobals();
