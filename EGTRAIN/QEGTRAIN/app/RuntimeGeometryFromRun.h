#pragma once

#include "graphics/RuntimeGeometrySource.h"

#include <memory>

class DispatchController;

// The source of the runtime canvas that reads the infrastructure of the run of the controller: its tracks, stations,
// connections, block sections, routes and platforms, and whether the run shows the passengers. The presenter writes
// its layout into the tracks, stations and connections, so the source takes the run through mutableRun(). It keeps the
// controller, which has to outlive it, and reads the run that the controller holds at each call.
std::unique_ptr<RuntimeGeometrySource> makeRuntimeGeometryFromRun(DispatchController& controller);
