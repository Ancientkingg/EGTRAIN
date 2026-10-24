#ifndef CAPACITYSCOPE_H
#define CAPACITYSCOPE_H

#include "diagrams/CapacityAnalysis.h"
#include "diagrams/CompletedRunData.h"

#include <QString>

#include <string>
#include <utility>
#include <vector>

class QWidget;

// Capacity inputs of the finished run. The functions read nothing but their arguments.

/// The route, blocks, trains and period of a capacity analysis.
struct CapacityAnalysisScope {
	int routeIndex = -1;					///< Index into CompletedRunData::routes, -1 for none
	std::vector<std::string> blockIds;		///< The analysed blocks, a range of the route, in route order
	std::vector<int> sectionIndices;		///< Positions of those blocks in the route, from 0
	std::vector<std::string> occurrenceIds; ///< Train ids in chain order; empty for every train with occupations
	std::string cycleEndOccurrenceId;		///< The train that closes the cycle, empty for none; unused with repeatFirstTrain
	/// True when the first train one period later (capacityNextPeriodTrain) follows the last train and closes the cycle.
	bool repeatFirstTrain = false;
	double periodSeconds = 3600.0; ///< Analysis period in seconds
};

/// The occupations of one train in the scope: its valid occupations that share a resource with a block of the scope, in
/// its own order. The first of them with a finite StartRunTime of 0 or more, where the train enters the blocks, gives the
/// reference time that the capacity analysis uses. No occupations when there is no such occupation.
CapacityAnalysisTrain capacityInputFrom(const CompletedTrain& train, const CapacityAnalysisScope& scope);
/// The trains with occupations in the scope. The order follows scope.occurrenceIds when it is given, else it is the
/// order of capacityTrainOrder.
std::vector<CapacityAnalysisTrain> capacityInputsFrom(const CompletedRunData& data, const CapacityAnalysisScope& scope);
/// The order in which the trains use the blocks they share, for trains in run order. Of two trains that share a
/// resource, the leader is the one whose occupation starts first in the pair of shared occupations, one of each, with the
/// earliest start; when both start together, the one with the earlier profile reference, then the earlier in run order.
/// Each train comes after its leaders; of the trains that may come next, the one with the earliest profile reference,
/// then the earlier in run order. When none may come next, because the leaders of the trains left form cycles, that
/// rule picks among the trains whose unplaced leaders can all be reached from them through the trains they lead: the
/// trains of a cycle that no other train left leads.
std::vector<CapacityAnalysisTrain> capacityTrainOrder(std::vector<CapacityAnalysisTrain> trains);
/// The first pair of `trains`, in their order, whose later train starts strictly earlier in the pair of shared
/// occupations that capacityTrainOrder compares: (earlier, later) runtime ids, or empty ids when the order has no such
/// pair. Trains that start together there are no conflict.
std::pair<std::string, std::string> capacityOrderConflictFrom(const std::vector<CapacityAnalysisTrain>& trains);
/// analyzeCapacity of the trains of capacityInputsFrom with the period of the scope. With repeatFirstTrain and two or
/// more trains the first train one period later follows the last train and closes the cycle, else cycleEndOccurrenceId
/// closes it.
CapacityAnalysisResult capacityAnalysisFrom(const CompletedRunData& data, const CapacityAnalysisScope& scope);
/// Why the scope cannot be analysed, empty when it can or has no route or block.
std::string capacityScopeIssueFrom(const CompletedRunData& data, const CapacityAnalysisScope& scope);
/// The blocks `first` to `last` of a route, positions from 0 in either order and cut to the route, with every train that
/// has occupations there in the order of capacityTrainOrder and the first train one period later closing the cycle. A
/// scope without blocks or trains when the route does not exist.
CapacityAnalysisScope capacityRangeScopeFrom(const CompletedRunData& data, int routeIndex, int first, int last);
/// capacityRangeScopeFrom over all blocks of the first route that two or more trains have occupations on. A scope without
/// a route when there is none.
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

/// Asks for a route, a range of its blocks, a period, the trains to analyse in order and the train that closes the cycle:
/// the first train one period later or a checked train after the first. The list holds the trains of the range
/// (capacityInputsFrom) in the order of capacityTrainOrder, all checked, and a new route or range lists them again, with
/// the trains that were unchecked still unchecked. It opens on the scope when the run has its route: its range, the
/// trains it names checked and first in their order, the other trains of the range unchecked, and its cycle-closing
/// train. Else it opens on firstCapacityScopeWithPairFrom. On accept the scope holds the choice. False when the dialog is
/// cancelled or the run has no route.
bool chooseCapacityAnalysisScope(QWidget* parent, const CompletedRunData& run, CapacityAnalysisScope& scope);

#endif
