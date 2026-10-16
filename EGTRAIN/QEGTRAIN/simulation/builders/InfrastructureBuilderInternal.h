#ifndef INFRASTRUCTUREBUILDERINTERNAL_H
#define INFRASTRUCTUREBUILDERINTERNAL_H

// What the infrastructure builder (InfrastructureBuilder.cpp) shares with Signalling.cpp, where both are defined.

// Set when createBlockConn needs more sections than the builder planned. The builder resets and reads it.
extern bool& derivedSectionsExceedPlan;

// Fills the connected block IDs of every section. The legacy Copenhagen dependency is added only for true.
void setDependenciesBetweenBlocksInternal(bool includeCopenhagenDependency);

#endif // INFRASTRUCTUREBUILDERINTERNAL_H
