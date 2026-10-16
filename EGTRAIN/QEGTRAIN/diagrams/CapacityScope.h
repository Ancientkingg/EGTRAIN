#ifndef CAPACITYSCOPE_H
#define CAPACITYSCOPE_H

#include "diagrams/CapacityAnalysis.h"

#include <QString>

#include <string>
#include <vector>

class Train;

// Capacity inputs of the finished run. Most functions here read the run from the simulation globals
// regional_train, numRegions and train_route, not from arguments.

struct CapacityAnalysisScope {
	int routeIndex = -1;
	std::vector<std::string> blockIds;
	std::vector<int> sectionIndices;
	std::vector<std::string> occurrenceIds;
	std::string cycleEndOccurrenceId;
	double periodSeconds = 3600.0;
};

std::string capacityScopeIssue(const CapacityAnalysisScope& scope);
CapacityAnalysisTrain capacityTrainForScope(const Train& train, const CapacityAnalysisScope& scope);
std::vector<CapacityAnalysisTrain> capacityTrainsForScope(const CapacityAnalysisScope& scope);
CapacityAnalysisScope firstCapacityScopeWithPair();
QString capacityScopeLabel(const CapacityAnalysisScope& scope);

#endif
