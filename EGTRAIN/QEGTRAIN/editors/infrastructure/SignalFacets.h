#ifndef SIGNALFACETS_H
#define SIGNALFACETS_H

#include "editors/infrastructure/InfrastructureFacet.h"

#include <memory>
#include <vector>

// The facets of the signalling: signals and signalling areas.
//
// A signal names the section that it protects in a combo of the sections, and none leaves it unbound. Renaming a signal
// rewrites the signal-failure incidents that target it (sceneRewriteSignalReferences), and a signal that such an
// incident targets cannot be deleted (sceneDirectDeleteConsumers).
//
// A signalling area has a start and an end in kilometres, and a signalling system and a track that are chosen in combos.
// A new area covers the blocks of the network and has no system yet. Nothing refers to an area.
std::vector<std::unique_ptr<InfrastructureFacet>> makeSignalFacets();

#endif // SIGNALFACETS_H
