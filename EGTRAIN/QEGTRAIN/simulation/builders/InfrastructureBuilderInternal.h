#ifndef INFRASTRUCTUREBUILDERINTERNAL_H
#define INFRASTRUCTUREBUILDERINTERNAL_H

// What the infrastructure builder (InfrastructureBuilder.cpp) shares with Signalling.cpp, where both are defined.

struct SimulationRun;

// Fills the connected block IDs of every section. The legacy Copenhagen dependency is added only for true.
void setDependenciesBetweenBlocksInternal(SimulationRun& run, bool includeCopenhagenDependency);

#endif // INFRASTRUCTUREBUILDERINTERNAL_H
