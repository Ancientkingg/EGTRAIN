#ifndef SIMULATIONHEADERS_H
#define SIMULATIONHEADERS_H

// The simulation headers still give warnings (#453, #454, #482), so the strict diagrams target
// includes them through this header, which keeps those warnings out of its build. Clang knows
// the groups -Wall and -Wextra. GCC has no pragma for a group, so it lists the warnings that
// these headers give. MSVC lowers the warning level to 0.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wall"
#pragma clang diagnostic ignored "-Wextra"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wdeprecated-copy"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "simulation/RollingStock.h"
#include "simulation/Signalling.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

#endif
