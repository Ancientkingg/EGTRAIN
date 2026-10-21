#ifndef CAPACITYSCOPE_H
#define CAPACITYSCOPE_H

#include "diagrams/CapacityAnalysis.h"
#include "diagrams/CompletedRunData.h"

#include <QString>

#include <string>
#include <vector>

class QWidget;

// Capacity inputs of the finished run. The functions read nothing but their arguments.

/// The route, blocks, trains and period of a capacity analysis.
struct CapacityAnalysisScope {
	int routeIndex = -1;					///< Index into CompletedRunData::routes, -1 for none
	std::vector<std::string> blockIds;		///< In route order; the first is the reference of every train
	std::vector<int> sectionIndices;		///< Positions of those blocks in the route, from 0
	std::vector<std::string> occurrenceIds; ///< Train ids in chain order; empty for every train with occupations
	std::string cycleEndOccurrenceId;		///< The train that closes the cycle, empty for none
	double periodSeconds = 3600.0;			///< Analysis period in seconds
};

/// The occupations of one train in the scope, with the reference time that the capacity analysis uses.
CapacityAnalysisTrain capacityInputFrom(const CompletedTrain& train, const CapacityAnalysisScope& scope);
/// The trains with occupations in the scope. The order follows scope.occurrenceIds when it is given.
std::vector<CapacityAnalysisTrain> capacityInputsFrom(const CompletedRunData& data, const CapacityAnalysisScope& scope);
/// Why the scope cannot be analysed, empty when it can or has no route or block.
std::string capacityScopeIssueFrom(const CompletedRunData& data, const CapacityAnalysisScope& scope);
/// The scope of the trains `occurrenceIds` on a route: the blocks of the route, in route order, that every one of the
/// trains occupies as capacityInputFrom needs, from the first such block to the last one before a block that one of
/// them does not occupy. The occurrence ids are kept as given. No blocks when the route does not exist, when there is
/// no id, when an id names no train of the run or when no block is shared by all of them.
CapacityAnalysisScope capacityStretchFrom(const CompletedRunData& data, int routeIndex,
	const std::vector<std::string>& occurrenceIds);
/// The first block, in route and block order, that at least two trains occupy, with those trains selected in run
/// order, and the blocks that follow it as capacityStretchFrom gives them. A scope without a route when there is none.
CapacityAnalysisScope firstCapacityScopeWithPairFrom(const CompletedRunData& data);
/// "route / corridor / first block to last block" without the parts the scope lacks, or "no selected route".
QString capacityScopeLabelFrom(const CompletedRunData& data, const CapacityAnalysisScope& scope);

/// Asks for a route, a period, the occurrences to analyse in order and the occurrence that closes the cycle, and shows
/// the blocks that capacityStretchFrom gives for the checked occurrences. It opens on the route of the scope when the
/// run has it, with the occurrences of the scope checked when it names some, else on the first route. Otherwise, and on
/// a route chosen in the dialog, the trains of the first block of the route that two or more trains occupy are checked.
/// On accept the scope holds the choice. False when the dialog is cancelled or the run has no route.
bool chooseCapacityAnalysisScope(QWidget* parent, const CompletedRunData& run, CapacityAnalysisScope& scope);

#endif
