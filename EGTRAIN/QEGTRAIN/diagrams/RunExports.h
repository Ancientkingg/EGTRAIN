#ifndef RUNEXPORTS_H
#define RUNEXPORTS_H

#include "diagrams/BlockingTimeDiagram.h"
#include "diagrams/RunResults.h"

#include <QString>
#include <QStringList>

#include <functional>
#include <string>
#include <vector>

class Train;

// CSV text of the finished run. Most functions here read the run from the simulation globals
// regional_train, numRegions, train_route, initial_variables and timestep, not from arguments.

std::vector<const Train*> runResultTrainPointers();
std::string csvValue(const RunResultValue& value);

std::string buildTrajectoryCsv(const QStringList& visibleTrainIds);
std::string buildTimetableCsv(const QStringList& visibleTrainIds);
std::string buildBlockingTimeCsv(const QStringList& visibleTrainIds,
	const std::vector<BlockingTimeDiagramSegment>& segments,
	const std::vector<BlockingTimePlannedReference>& plannedReferences,
	const std::vector<std::vector<std::string>>& trajectories = {},
	const std::vector<BlockSectionExtent>& extents = {},
	const std::vector<BlockingTimeDiagramSegment>& unmapped = {});
std::string buildBlockingTimeCsv(const QStringList& visibleTrainIds);

QStringList allTrainIds();
std::function<std::string(const QStringList&)> snapshotCsv(
	const std::function<std::string(const QStringList&)>& provider);
bool writeCsvFileWithProvenance(const QString& path, const std::string& content,
	const RunProvenance& provenance);

#endif
