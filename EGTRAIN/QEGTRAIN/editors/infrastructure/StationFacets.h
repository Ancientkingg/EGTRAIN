#ifndef STATIONFACETS_H
#define STATIONFACETS_H

#include "editors/infrastructure/InfrastructureFacet.h"

#include <memory>
#include <vector>

// The facets of the stations: stations, platforms and station boundaries.
//
// Renaming a station rewrites what refers to it (sceneRewriteStationReferences), and a station that services, entrance
// delays or passengers use cannot be deleted (sceneDirectDeleteConsumers).
//
// A platform is listed under its station. Its station can be changed to another station, which takes the platform and
// the stops at it along (sceneMovePlatformReferences). The nodes of a platform are a list of ids, and its length and width
// are number fields in the table that take their value when they are left. A platform that a stop uses cannot be deleted.
//
// A station boundary names its entrance and exit sections in combos and has no id.
std::vector<std::unique_ptr<InfrastructureFacet>> makeStationFacets();

#endif // STATIONFACETS_H
