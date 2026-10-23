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
/// The sections of a route that the trains `occurrenceIds` share, in route order: each run of consecutive blocks of the
/// route that every one of the trains occupies as capacityInputFrom needs, ended by a block that one of them does not
/// occupy or by the end of the route. Each is a scope with the blocks of its run and the occurrence ids as given. None
/// when the route does not exist, when there is no id, when an id names no train of the run or when no block is shared
/// by all of them.
std::vector<CapacityAnalysisScope> capacityStretchesFrom(const CompletedRunData& data, int routeIndex,
	const std::vector<std::string>& occurrenceIds);
/// The section of capacityStretchesFrom with the most blocks, the first of them on a tie. A scope of the route and the
/// occurrence ids without blocks when there is none.
CapacityAnalysisScope capacityStretchFrom(const CompletedRunData& data, int routeIndex,
	const std::vector<std::string>& occurrenceIds);
/// The trains, in run order, that occupy the first block, in route and block order, that at least two trains occupy,
/// and the section of its route that capacityStretchFrom gives for them. A scope without a route when there is none.
CapacityAnalysisScope firstCapacityScopeWithPairFrom(const CompletedRunData& data);
/// "route / corridor / first block to last block" without the parts the scope lacks, or "no selected route". The blocks
/// are named by their ids, as the Section column of the capacity export holds them.
QString capacityScopeLabelFrom(const CompletedRunData& data, const CapacityAnalysisScope& scope);
/// For the windows: "route (corridor), blocks 36 to 72" without the parts the scope lacks, or "no selected route". The
/// blocks are named by their numbers in the route, from 1, or by their count when the scope has no numbers.
QString capacityScopeTextFrom(const CompletedRunData& data, const CapacityAnalysisScope& scope);
/// The first and last block of the scope with their numbers and labels, one per line, for the tooltip of
/// capacityScopeTextFrom; empty without blocks.
QString capacityScopeBlocksText(const CompletedRunData& data, const CapacityAnalysisScope& scope);
/// The reference time of a train as a window names it, from the referenceSource and referenceLabel of
/// CapacityAnalysisTrain: "planned departure at <station>", "planned arrival at <station>", "planned entry time" or
/// "simulated entry time"; empty without a source.
QString capacityReferenceText(const std::string& source, const std::string& label);
/// capacityReferenceText for the train `identity` of the result; empty when the result does not have it.
QString capacityReferenceOf(const CapacityAnalysisResult& result, const std::string& identity);
/// An analysis period as the capacity windows name it: "1 h" for whole hours, "30 min" for whole minutes, else "90.5 s".
QString capacityPeriodText(double periodSeconds);

/// Asks for a route, a period, the occurrences to analyse in order, the occurrence that closes the cycle and one of the
/// sections that capacityStretchesFrom gives for the checked occurrences. The longest section is selected; after a
/// change of the checked occurrences or of their order, the section that starts at the block where the selected one
/// started stays selected when there is one. It opens on the route of the scope when the run has it, with the
/// occurrences of the scope checked when it names some, else on the first route. Otherwise, and on a route chosen in
/// the dialog, the trains of the first block of the route that two or more trains occupy are checked. On accept the
/// scope holds the choice. False when the dialog is cancelled or the run has no route.
bool chooseCapacityAnalysisScope(QWidget* parent, const CompletedRunData& run, CapacityAnalysisScope& scope);

#endif
