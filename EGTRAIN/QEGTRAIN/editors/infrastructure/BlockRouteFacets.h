#ifndef BLOCKROUTEFACETS_H
#define BLOCKROUTEFACETS_H

#include "editors/infrastructure/InfrastructureFacet.h"

#include <memory>
#include <vector>

// The facets of the blocks and the routes: blocks, routes, block dependencies and single-track restrictions.
//
// The blocks that are listed are the ones on the track that the filter of the panel names, in the order of the model. A
// block can be inserted before the selected one and moved past its neighbour. Renaming a block rewrites what refers to it
// (sceneRewriteBlockReferences), and a block that others use cannot be deleted (sceneDirectDeleteConsumers).
//
// A route lists its sections in a list of the panel, which the section buttons change. Renaming a route rewrites the
// route of the services, and a route that a service uses cannot be deleted.
//
// A block dependency and a single-track restriction name sections in combos and have no id.
std::vector<std::unique_ptr<InfrastructureFacet>> makeBlockRouteFacets();

#endif // BLOCKROUTEFACETS_H
