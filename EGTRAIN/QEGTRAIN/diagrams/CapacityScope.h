#ifndef CAPACITYSCOPE_H
#define CAPACITYSCOPE_H

#include "diagrams/CapacityAnalysis.h"
#include "diagrams/CompletedRunData.h"

#include <QString>

#include <string>
#include <vector>

class QWidget;

// Capacity inputs of the finished run. The functions read nothing but their arguments.

struct CapacityAnalysisScope {
	int routeIndex = -1;
	std::vector<std::string> blockIds;
	std::vector<int> sectionIndices;
	std::vector<std::string> occurrenceIds;
	std::string cycleEndOccurrenceId;
	double periodSeconds = 3600.0;
};

// The occupations of one train in the scope, with the reference time that the capacity analysis uses.
CapacityAnalysisTrain capacityInputFrom(const CompletedTrain& train, const CapacityAnalysisScope& scope);
// The trains with occupations in the scope. The order follows scope.occurrenceIds when it is given.
std::vector<CapacityAnalysisTrain> capacityInputsFrom(const CompletedRunData& data, const CapacityAnalysisScope& scope);
std::string capacityScopeIssueFrom(const CompletedRunData& data, const CapacityAnalysisScope& scope);
CapacityAnalysisScope firstCapacityScopeWithPairFrom(const CompletedRunData& data);
QString capacityScopeLabelFrom(const CompletedRunData& data, const CapacityAnalysisScope& scope);

// Asks for a route, a range of its blocks, a period, the occurrences to analyse in order and the occurrence that closes
// the cycle. On accept the scope holds the choice. False when the dialog is cancelled or the run has no route.
bool chooseCapacityAnalysisScope(QWidget* parent, const CompletedRunData& run, CapacityAnalysisScope& scope);

#endif
