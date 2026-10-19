#ifndef INFRASTRUCTUREBUILDERINTERNAL_H
#define INFRASTRUCTUREBUILDERINTERNAL_H

// What the infrastructure builder (InfrastructureBuilder.cpp) uses from Signalling.cpp, where it is defined.

struct SimulationRun;

/// Fills the connected block IDs of every section of run.infra. With includeCopenhagenDependency it also adds the legacy
/// Copenhagen connection of section @5-B6@ and sets the legacy connected block IDs of the nodes; the scene builder passes false.
void setDependenciesBetweenBlocksInternal(SimulationRun& run, bool includeCopenhagenDependency);

#endif // INFRASTRUCTUREBUILDERINTERNAL_H
