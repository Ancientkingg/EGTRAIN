#include "diagrams/BlockingTimeScope.h"

#include "widgets/ChoiceComboBox.h"
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
	dialog.setWindowTitle("Blocking time");
	auto* body = new QWidget;
	auto* form = new QFormLayout(body);
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	auto* routeCombo = new ChoiceComboBox(&dialog);
	routeCombo->addItem("All routes", -1);
	for (std::size_t routeIndex = 0; routeIndex < choices.routes.size(); ++routeIndex) {
		const BlockingTimeScopeChoices::Route& route = choices.routes[routeIndex];
		const QString id = QString::fromStdString(route.id);
		routeCombo->addItem(route.corridor.empty() ? id : QString("%1  (%2)").arg(id, QString::fromStdString(route.corridor)),
			static_cast<int>(routeIndex));
	}
	auto* firstBlock = new ChoiceComboBox(&dialog);
	auto* lastBlock = new ChoiceComboBox(&dialog);
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
			const QString blockId = QString::fromStdString(shortBlockId(blockIds[blockIndex]));
			const QString text = QString("%1: %2").arg(blockIndex + 1).arg(QString::fromStdString(readableSectionLabel(blockIds[blockIndex], choices.sectionLabels)));
			for (QComboBox* combo : {firstBlock, lastBlock}) {
				combo->addItem(text);
				combo->setItemData(combo->count() - 1, blockId, Qt::ToolTipRole);
			}
		}
		const bool hasBlocks = firstBlock->count() > 0;
		firstBlock->setEnabled(hasBlocks);
		lastBlock->setEnabled(hasBlocks);
		if (hasBlocks)
			lastBlock->setCurrentIndex(lastBlock->count() - 1);
	};
	// The refill clears the block combos, so the dialog widens again for the blocks of the new route.
	QObject::connect(routeCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
		[refillBlocks, &dialog](int) {
			refillBlocks();
			DialogLayout::fitWidthToContent(dialog);
		});
	refillBlocks();

	form->addRow("Route / corridor:", routeCombo);
	form->addRow("First block:", firstBlock);
	form->addRow("Last block:", lastBlock);
	form->addRow("From (after run start):", startTime);
	form->addRow("To (after run start):", endTime);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	DialogLayout::install(dialog, "Blocking time", QString(), body, buttons);
	DialogLayout::fitWidthToContent(dialog);
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
	return true;
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
