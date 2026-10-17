#ifndef TOPOLOGYFACETS_H
#define TOPOLOGYFACETS_H

#include "editors/infrastructure/InfrastructureFacet.h"

#include <memory>
#include <vector>

// The facets of the network itself: tracks, nodes, arcs and connections. Each lists one entity in a row, and edits,
// adds and deletes entities of its own kind. Renaming a track or a node rewrites what refers to it
// (sceneRewriteTrackReferences, sceneRewriteNodeReferences), and an entity that other items use cannot be deleted
// (sceneDirectDeleteConsumers).
std::vector<std::unique_ptr<InfrastructureFacet>> makeTopologyFacets();

#endif // TOPOLOGYFACETS_H
