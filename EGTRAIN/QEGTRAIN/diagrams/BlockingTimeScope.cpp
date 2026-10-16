#include "diagrams/BlockingTimeScope.h"

#include "diagrams/SimulationHeaders.h"
#include "widgets/DialogLayout.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QObject>
#include <QString>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

const char* blockingSegmentTypeName(BlockingTimeSegmentStyle style) {
	switch (style) {
		case BlockingTimeSegmentStyle::Station:
			return "station";
		case BlockingTimeSegmentStyle::Switch:
			return "switch";
		case BlockingTimeSegmentStyle::SwitchStation:
			return "switch/station";
		case BlockingTimeSegmentStyle::Critical:
			return "conflict";
		case BlockingTimeSegmentStyle::CriticalStation:
			return "conflict/station";
		case BlockingTimeSegmentStyle::Default:
		default:
			return "block";
	}
}

const char* blockingSegmentTypeName(const BlockingTimeDiagramSegment& segment) {
	return segment.capacityCritical ? "capacity critical block" : blockingSegmentTypeName(segment.style);
}

BlockingTimeScope defaultBlockingTimeScope(double caseEndSeconds) {
	BlockingTimeScope scope;
	scope.endTime = std::max(0.0, caseEndSeconds);
	return scope;
}

bool chooseBlockingTimeScope(QWidget* parent, const BlockingTimeScopeChoices& choices, BlockingTimeScope& scope) {
	QDialog dialog(parent);
	dialog.setWindowTitle("Blocking-time scope");
	auto* body = new QWidget;
	auto* form = new QFormLayout(body);
	auto* routeCombo = new QComboBox(&dialog);
	routeCombo->addItem("All routes / all corridors", -1);
	for (std::size_t routeIndex = 0; routeIndex < choices.routes.size(); ++routeIndex) {
		const BlockingTimeScopeChoices::Route& route = choices.routes[routeIndex];
		const QString corridor = route.corridor.empty()
			? QStringLiteral("no corridor")
			: QString::fromStdString(route.corridor);
		routeCombo->addItem(QString("%1  (%2)").arg(QString::fromStdString(route.id), corridor),
			static_cast<int>(routeIndex));
	}
	auto* firstBlock = new QComboBox(&dialog);
	auto* lastBlock = new QComboBox(&dialog);
	auto* startTime = new QDoubleSpinBox(&dialog);
	auto* endTime = new QDoubleSpinBox(&dialog);
	const double caseEnd = std::max(0.0, choices.caseEndSeconds);
	const double step = choices.stepSeconds > 0.0 ? choices.stepSeconds : 1.0;
	for (QDoubleSpinBox* spinBox : {startTime, endTime}) {
		spinBox->setRange(0.0, caseEnd);
		spinBox->setDecimals(3);
		spinBox->setSingleStep(step);
		spinBox->setSuffix(" s");
	}
	startTime->setValue(scope.startTime);
	endTime->setValue(scope.endTime);

	const auto refillBlocks = [&choices, routeCombo, firstBlock, lastBlock]() {
		firstBlock->clear();
		lastBlock->clear();
		const int routeIndex = routeCombo->currentData().toInt();
		if (routeIndex < 0 || routeIndex >= static_cast<int>(choices.routes.size())) {
			firstBlock->addItem("All blocks");
			lastBlock->addItem("All blocks");
			firstBlock->setEnabled(false);
			lastBlock->setEnabled(false);
			return;
		}
		const std::vector<std::string>& blockIds = choices.routes[static_cast<std::size_t>(routeIndex)].blockIds;
		for (std::size_t blockIndex = 0; blockIndex < blockIds.size(); ++blockIndex) {
			const QString blockId = QString::fromStdString(blockIds[blockIndex]);
			firstBlock->addItem(QString("%1:%2").arg(blockIndex + 1).arg(blockId));
			lastBlock->addItem(QString("%1:%2").arg(blockIndex + 1).arg(blockId));
		}
		const bool hasBlocks = firstBlock->count() > 0;
		firstBlock->setEnabled(hasBlocks);
		lastBlock->setEnabled(hasBlocks);
		if (hasBlocks)
			lastBlock->setCurrentIndex(lastBlock->count() - 1);
	};
	QObject::connect(routeCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
		[refillBlocks](int) { refillBlocks(); });
	refillBlocks();

	form->addRow("Route / corridor:", routeCombo);
	form->addRow("First block:", firstBlock);
	form->addRow("Last block:", lastBlock);
	form->addRow("Start after case base:", startTime);
	form->addRow("End after case base:", endTime);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	DialogLayout::install(dialog, "Blocking-time scope",
		"Choose the route, blocks and elapsed interval to inspect.", body, buttons);
	if (dialog.exec() != QDialog::Accepted)
		return false;

	scope.startTime = std::min(startTime->value(), endTime->value());
	scope.endTime = std::max(startTime->value(), endTime->value());
	if (scope.endTime <= scope.startTime) {
		if (scope.startTime + step <= caseEnd)
			scope.endTime = scope.startTime + step;
		else
			scope.startTime = std::max(0.0, scope.endTime - step);
	}
	if (scope.endTime <= scope.startTime)
		return false;
	scope.trainIds.clear();
	scope.blockIds.clear();
	scope.sectionIndices.clear();
	const int routeIndex = routeCombo->currentData().toInt();
	scope.routeIndex = routeIndex >= 0 && routeIndex < static_cast<int>(choices.routes.size())
		? routeIndex
		: -1;
	if (scope.routeIndex < 0)
		return true;

	const BlockingTimeScopeChoices::Route& route = choices.routes[static_cast<std::size_t>(scope.routeIndex)];
	scope.sectionIndices = blockSectionRangeIndices(firstBlock->currentIndex(), lastBlock->currentIndex(), static_cast<int>(route.blockIds.size()));
	for (int blockIndex : scope.sectionIndices)
		scope.blockIds.push_back(route.blockIds[static_cast<std::size_t>(blockIndex)]);
	for (const BlockingTimeScopeChoices::Train& train : choices.trains)
		if (train.routeIndex == scope.routeIndex || train.routeId == route.id)
			scope.trainIds.push_back(train.id);
	return true;
}

std::vector<BlockingTimeDiagramSegment> buildAllBlockingTimeSegments() {
	std::vector<std::vector<BlockingTimeDiagramInput>> trains;
	std::vector<std::string> trainNames;
	trains.reserve(static_cast<std::size_t>(std::max(0, numRegions)));
	trainNames.reserve(static_cast<std::size_t>(std::max(0, numRegions)));
	for (int i = 0; i < numRegions; ++i) {
		const Train& t = regional_train[i];
		std::vector<BlockingTimeDiagramInput> blocks;
		blocks.reserve(static_cast<std::size_t>(std::max(0, t.N_BlockSections)));
		for (int j = 0; j < t.N_BlockSections; ++j)
			blocks.push_back(blockingTimeInputFrom(t.BlockTime[j]));
		trains.push_back(blocks);
		trainNames.push_back(t.trainDescription);
	}
	return buildBlockingTimeDiagramSegments(trains, trainNames);
}

// A segment whose ends do not both map is counted in `omitted` and, when `unmapped` is given, kept there unchanged.
std::vector<BlockingTimeDiagramSegment> projectBlockingSegments(
	const std::vector<BlockingTimeDiagramSegment>& segments,
	const std::map<std::string, RouteDiagramProjection>& projections, int& omitted,
	std::vector<BlockingTimeDiagramSegment>* unmapped) {
	std::vector<BlockingTimeDiagramSegment> mapped;
	for (auto segment : segments) {
		const auto projection = projections.find(segment.trainName);
		const auto start = projection == projections.end() ? std::optional<double>()
														   : projection->second.map(segment.startPositionKm);
		const auto end = projection == projections.end() ? std::optional<double>()
														 : projection->second.map(segment.endPositionKm);
		if (!start || !end) {
			++omitted;
			if (unmapped)
				unmapped->push_back(std::move(segment));
			continue;
		}
		segment.startPositionKm = *start;
		segment.endPositionKm = *end;
		segment.midPositionKm = (*start + *end) / 2.0;
		mapped.push_back(std::move(segment));
	}
	return mapped;
}

std::vector<BlockingTimePlannedReference> buildBlockingTimePlannedReferences(
	const BlockingTimeScope& scope, const std::map<int, RouteDiagramPath>& paths,
	const std::map<int, RouteDiagramProjection>& projections, int& omitted) {
	std::vector<BlockingTimePlannedReference> references;
	const auto selectedSections = scope.routeIndex >= 0 && !scope.sectionIndices.empty()
		? scopedBlockSectionExtents(buildBlockSectionExtents(blockSectionInputsForRoute(train_route[scope.routeIndex])), scope.sectionIndices)
		: std::vector<BlockSectionExtent>();
	if (scope.routeIndex >= 0 && scope.trainIds.empty())
		return references;
	for (int i = 0; i < numRegions; ++i) {
		const Train& train = regional_train[i];
		if (!scope.trainIds.empty() && std::find(scope.trainIds.begin(), scope.trainIds.end(), train.trainDescription) == scope.trainIds.end())
			continue;
		if (train.Stations.empty())
			continue;
		const int stationCount = std::min(train.numStations, static_cast<int>(Train::kMaxTimetableStations));
		for (int stationIndex = 0; stationIndex < stationCount; ++stationIndex) {
			if (!train.stationIsOnRoute(stationIndex, scope.blockIds))
				continue;
			const auto path = paths.find(train.indexOfRoute);
			const auto projection = projections.find(train.indexOfRoute);
			const auto positionKm = path == paths.end() || projection == projections.end()
				? std::optional<double>()
				: routeDiagramStopPosition(train, stationIndex, path->second, projection->second);
			if (!positionKm) {
				++omitted;
				references.push_back({train.trainDescription, "", "", std::numeric_limits<double>::quiet_NaN(), 0.0});
				continue;
			}
			if (!scope.sectionIndices.empty() && !positionInBlockSections(*positionKm, selectedSections)) {
				references.push_back({train.trainDescription, "", "", std::numeric_limits<double>::quiet_NaN(), 0.0});
				continue; // Preserve the gap; do not bridge another visit of a repeated resource.
			}
			const auto append = [&](const char* eventType, double time) {
				if (!std::isfinite(time) || time < 0.0)
					return;
				references.push_back({train.trainDescription, train.stationNameForArrivalStats(stationIndex),
					eventType, time, *positionKm});
			};
			append("arrival", train.ScheduledArrivals[stationIndex]);
			append("departure", train.ScheduledDepartures[stationIndex]);
		}
	}
	return references;
}
