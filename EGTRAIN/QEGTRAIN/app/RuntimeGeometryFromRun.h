#pragma once

#include "graphics/RuntimeGeometrySource.h"

#include <memory>

class DispatchController;

/// The source of the runtime canvas that reads the infrastructure of the run of the controller: its tracks, stations,
/// connections, block sections, routes and platforms, and whether the run shows the passengers. The presenter only
/// reads them, so the source takes the run through run(). It keeps the controller, which has to outlive it, and reads
/// the run that the controller holds at each call.
std::unique_ptr<RuntimeGeometrySource> makeRuntimeGeometryFromRun(const DispatchController& controller);
