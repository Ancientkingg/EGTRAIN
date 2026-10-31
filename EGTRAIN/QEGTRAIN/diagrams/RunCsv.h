#ifndef RUNCSV_H
#define RUNCSV_H

#include "diagrams/BlockingTimeDiagram.h"
#include "diagrams/CapacityAnalysis.h"
#include "diagrams/CapacityScope.h"
#include "diagrams/CompletedRunData.h"
#include "diagrams/RunResults.h"

#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <string>
#include <vector>

// CSV text of a finished run. The functions read nothing but their arguments.
//
// Times are seconds from the start of the run and positions on a reference route are km, unless a name or comment
// says otherwise. An export that takes `visibleTrainIds` lists only those trains, and an export is empty when it has no
// row.

/// The CSV field of a value: the number, or an empty field when it is unavailable.
std::string csvValue(const RunResultValue& value);

/// The blocking-time CSV: the envelopes of the visible trains in `segments`, their planned stop references, the recorded
/// trajectory rows, the envelopes that could not be projected and one row for each section in `extents`. Every row has
/// the columns of blockingTimeCsvHeader(); the geo X columns map the positions through the PositionScale of `extents`.
std::string buildBlockingTimeCsv(const QStringList& visibleTrainIds,
	const std::vector<BlockingTimeDiagramSegment>& segments,
	const std::vector<BlockingTimePlannedReference>& plannedReferences,
	const std::vector<std::vector<std::string>>& trajectories = {},
	const std::vector<BlockSectionExtent>& extents = {},
	const std::vector<BlockingTimeDiagramSegment>& unmapped = {});

/// The ids of the trains of the run, in run order.
QStringList trainIdsFrom(const CompletedRunData& data);
/// The recorded samples: time in s, position in m, speed in m/s, power in kW, tractive effort in kN, energy in kWh and
/// the block, one row for each valid sample.
std::string trajectoryCsvFrom(const CompletedRunData& data, const QStringList& visibleTrainIds);
/// The planned and simulated times and the delays of every stop, in seconds.
std::string timetableCsvFrom(const CompletedRunData& data, const QStringList& visibleTrainIds);

/// The CSV provider of a result window: it shares the run and gives one export of it for the trains that the window
/// shows, whatever the simulation holds by then.
using CompletedRunCsv = std::string (*)(const CompletedRunData& data, const QStringList& visibleTrainIds);
/// A provider that calls `csv` on the shared run with the visible train ids.
std::function<std::string(const QStringList&)> csvProviderFrom(std::shared_ptr<const CompletedRunData> run,
	CompletedRunCsv csv);

/// The evidence as the export writes it, "leader/follower (leader=… s; follower=… s; candidate=… s)", joined with "; ".
/// With `labels` it is the tooltip of the windows instead: one line per item, "leader / follower: leader offset … s,
/// follower offset … s, headway … s", with the blocks named by their labels without internal ids (readableSectionLabel).
std::string capacityEvidenceText(const std::vector<CapacityHeadwayEvidence>& evidence, const SceneSectionLabels* labels = nullptr);
/// The blocks of the evidence by their short ids as the section strip shows them (shortBlockId), "leader / follower",
/// joined with ", ", for the cells of the windows.
std::string capacityEvidenceBlocks(const std::vector<CapacityHeadwayEvidence>& evidence);
/// The capacity CSV of the result, with times in seconds and the cycle share in percent. Empty when the result cannot
/// be analysed or has no cycle time or no cycle share.
std::string capacityAnalysisCsv(const CapacityAnalysisResult& result, const QString& sectionLabel);
/// capacityAnalysisCsv of capacityAnalysisFrom; empty when the scope has an issue.
std::string capacityCsvFrom(const CompletedRunData& data, const CapacityAnalysisScope& scope);

#endif // RUNCSV_H
