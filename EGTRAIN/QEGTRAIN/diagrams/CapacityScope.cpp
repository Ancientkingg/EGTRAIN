#include "diagrams/CapacityScope.h"

#include "widgets/ChoiceComboBox.h"
#include "widgets/DialogLayout.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QObject>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

CapacityAnalysisTrain capacityInputFrom(const CompletedTrain& train, const CapacityAnalysisScope& scope) {
	CapacityAnalysisTrain result;
	result.runtimeId = train.trainId;
	result.operatingCode = train.operatingCode;
	if (scope.blockIds.empty())
		return result;

	const BlockingTimeDiagramInput* reference = nullptr;
	for (const BlockingTimeDiagramInput& occupation : train.blockTimes) {
		if (!validBlockingTimeDiagramInput(occupation))
			continue;
		if (!reference && shareBlockingTimeResource(occupation.blockId, scope.blockIds.front()))
			reference = &occupation;
		for (const std::string& selectedBlock : scope.blockIds) {
			if (shareBlockingTimeResource(occupation.blockId, selectedBlock)) {
				result.occupations.push_back(occupation);
				break;
			}
		}
	}
	if (!reference || !std::isfinite(reference->startRunTime) || reference->startRunTime < 0.0) {
		result.occupations.clear();
		return result;
	}
	result.profileReferenceTime = reference->startRunTime;
	result.referenceLabel = "block " + scope.blockIds.front() + " entry";
	result.referenceSource = "selected section StartRunTime";

	// A boundary stop is the authored reference when its route position matches
	// the selected section entry. The 5 m tolerance is the native route tolerance.
	if (!train.stops.empty() && reference->posStart >= 0.0) {
		for (const CompletedStop& stop : train.stops) {
			if (!std::isfinite(stop.routePositionMeters) || std::abs(stop.routePositionMeters - reference->posStart) > 5.0)
				continue;
			const double departure = stop.scheduledDeparture;
			const double arrival = stop.scheduledArrival;
			const double authored = departure >= 0.0 ? departure : arrival;
			if (!std::isfinite(authored) || authored < 0.0)
				continue;
			result.scheduledReferenceTime = authored;
			result.referenceLabel = stop.station + " boundary";
			result.referenceSource = departure >= 0.0
				? "authored ScheduledDeparture at boundary station"
				: "authored ScheduledArrival at boundary station (departure absent)";
			return result;
		}
	}

	if (std::isfinite(train.scheduledDepartureTime) && train.scheduledDepartureTime >= 0.0 && train.runStartTime >= 0) {
		result.scheduledReferenceTime = train.scheduledDepartureTime
			+ (reference->startRunTime - static_cast<double>(train.runStartTime));
		result.referenceSource = "canonical scheduled entry + elapsed from RunStartTime";
	} else {
		result.scheduledReferenceTime = result.profileReferenceTime;
		result.referenceSource = "profile reference fallback (scheduled entry unavailable)";
	}
	return result;
}

std::vector<CapacityAnalysisTrain> capacityInputsFrom(const CompletedRunData& data,
	const CapacityAnalysisScope& scope) {
	const auto selected = [&scope](const std::string& runtimeId) {
		return std::find(scope.occurrenceIds.begin(), scope.occurrenceIds.end(), runtimeId) != scope.occurrenceIds.end();
	};
	std::vector<CapacityAnalysisTrain> candidates;
	for (const CompletedTrain& train : data.trains) {
		CapacityAnalysisTrain candidate = capacityInputFrom(train, scope);
		if (candidate.occupations.empty())
			continue;
		if (!scope.occurrenceIds.empty() && !selected(candidate.runtimeId))
			continue;
		candidates.push_back(std::move(candidate));
	}
	if (scope.occurrenceIds.empty())
		return candidates;
	std::vector<CapacityAnalysisTrain> trains;
	trains.reserve(scope.occurrenceIds.size());
	for (const std::string& occurrenceId : scope.occurrenceIds) {
		const auto it = std::find_if(candidates.begin(), candidates.end(),
			[&occurrenceId](const CapacityAnalysisTrain& candidate) {
				return candidate.runtimeId == occurrenceId;
			});
		if (it != candidates.end())
			trains.push_back(*it);
	}
	return trains;
}

// The capacity calculation is resource-id based. Refuse only the selected ambiguity,
// rather than choosing one route occurrence or changing the capacity mathematics.
std::string capacityScopeIssueFrom(const CompletedRunData& data, const CapacityAnalysisScope& scope) {
	if (!data.hasRoute(scope.routeIndex) || scope.blockIds.empty())
		return {};
	const auto extents = buildBlockSectionExtents(data.routes[static_cast<std::size_t>(scope.routeIndex)].blocks);
	std::string issue = blockSectionCapacityScopeIssue(extents, scope.sectionIndices);
	if (!issue.empty())
		return issue;
	for (const CompletedTrain& train : data.trains) {
		if (!scope.occurrenceIds.empty() && std::find(scope.occurrenceIds.begin(), scope.occurrenceIds.end(), train.trainId) == scope.occurrenceIds.end())
			continue;
		if (blockingTimeEntryAmbiguous(train.blockTimes, scope.blockIds.front()))
			return "Occurrence " + train.trainId + " has several records for the selected entry resource; capacity analysis cannot identify one entry.";
	}
	return {};
}

CapacityAnalysisScope firstCapacityScopeWithPairFrom(const CompletedRunData& data) {
	CapacityAnalysisScope empty;
	for (std::size_t routeIndex = 0; routeIndex < data.routes.size(); ++routeIndex) {
		const CompletedRoute& route = data.routes[routeIndex];
		for (std::size_t blockIndex = 0; blockIndex < route.blocks.size(); ++blockIndex) {
			CapacityAnalysisScope candidate;
			candidate.routeIndex = static_cast<int>(routeIndex);
			candidate.blockIds.push_back(route.blocks[blockIndex].id);
			candidate.sectionIndices.push_back(static_cast<int>(blockIndex));
			const auto trains = capacityInputsFrom(data, candidate);
			if (trains.size() < 2)
				continue;
			for (const auto& train : trains)
				candidate.occurrenceIds.push_back(train.runtimeId);
			return candidate;
		}
	}
	return empty;
}

QString capacityScopeLabelFrom(const CompletedRunData& data, const CapacityAnalysisScope& scope) {
	if (!data.hasRoute(scope.routeIndex))
		return QStringLiteral("no selected route");
	const CompletedRoute& route = data.routes[static_cast<std::size_t>(scope.routeIndex)];
	QString label = QString::fromStdString(route.id);
	if (!route.corridor.empty())
		label += QString(" / %1").arg(QString::fromStdString(route.corridor));
	if (!scope.blockIds.empty())
		label += QString(" / %1 to %2").arg(QString::fromStdString(scope.blockIds.front()), QString::fromStdString(scope.blockIds.back()));
	return label;
}

bool chooseCapacityAnalysisScope(QWidget* parent, const CompletedRunData& run, CapacityAnalysisScope& scope) {
	if (run.routes.empty())
		return false;
	QDialog dialog(parent);
	dialog.setWindowTitle("Capacity analysis");
	auto* body = new QWidget;
	auto* layout = new QVBoxLayout(body);
	auto* form = new QFormLayout();
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	auto* routeCombo = new ChoiceComboBox(&dialog);
	for (std::size_t routeIndex = 0; routeIndex < run.routes.size(); ++routeIndex) {
		const CompletedRoute& route = run.routes[routeIndex];
		const QString corridor = route.corridor.empty()
			? QStringLiteral("no corridor")
			: QString::fromStdString(route.corridor);
		routeCombo->addItem(QString("%1  (%2)").arg(QString::fromStdString(route.id), corridor),
			static_cast<int>(routeIndex));
	}
	auto* firstBlock = new ChoiceComboBox(&dialog);
	auto* lastBlock = new ChoiceComboBox(&dialog);
	auto* period = new QDoubleSpinBox(&dialog);
	period->setRange(0.001, std::numeric_limits<double>::max());
	period->setDecimals(3);
	period->setSingleStep(60.0);
	period->setSuffix(" s");
	period->setValue(3600.0);
	auto* cycleEnd = new ChoiceComboBox(&dialog);
	cycleEnd->addItem("Select the first train in the next period...");
	auto* occurrences = new QListWidget(&dialog);
	occurrences->setSelectionMode(QAbstractItemView::SingleSelection);
	occurrences->setDragDropMode(QAbstractItemView::InternalMove);
	occurrences->setDefaultDropAction(Qt::MoveAction);
	occurrences->setMinimumHeight(180);
	form->addRow("Route / corridor:", routeCombo);
	form->addRow("First block:", firstBlock);
	form->addRow("Last block:", lastBlock);
	form->addRow("Analysis period:", period);
	form->addRow("Cycle-closing occurrence:", cycleEnd);
	layout->addLayout(form);
	auto* note = new QLabel(body);
	DialogLayout::setHelp(note,
		"Checked occurrences are analyzed in the displayed order (A → B); selecting exactly two enables the pair workflow. "
		"Where the order of trains changes along the route, analyze each part as a separate section. Select the occurrence of the "
		"first service in the next period that closes the capacity cycle; EGTRAIN does not infer it from the last row.");
	layout->addWidget(note);
	auto* occurrencesCaption = new QLabel("Occurrences with complete occupations on the selected section:", body);
	occurrencesCaption->setWordWrap(true);
	layout->addWidget(occurrencesCaption);
	layout->addWidget(occurrences);
	auto* status = new QLabel(body);
	status->setObjectName("capacityScopeStatus");
	layout->addWidget(status);

	// An empty text hides the label. A text is scrolled into view once the layout has made room for it, because the body
	// of the dialog scrolls and the label is at its end.
	QScrollArea* bodyScroll = nullptr;
	const auto showError = [status, &bodyScroll](const QString& text) {
		DialogLayout::setStatus(status, DialogLayout::Severity::Error, text);
		if (!text.isEmpty() && bodyScroll)
			QTimer::singleShot(0, status, [status, bodyScroll] { bodyScroll->ensureWidgetVisible(status); });
	};
	const auto clearError = [showError, status]() {
		if (!status->text().isEmpty())
			showError(QString());
	};
	showError(QString());

	const auto refillBlocks = [&run, routeCombo, firstBlock, lastBlock]() {
		firstBlock->clear();
		lastBlock->clear();
		const int routeIndex = routeCombo->currentData().toInt();
		if (!run.hasRoute(routeIndex))
			return;
		const CompletedRoute& route = run.routes[static_cast<std::size_t>(routeIndex)];
		for (std::size_t blockIndex = 0; blockIndex < route.blocks.size(); ++blockIndex) {
			const QString blockId = QString::fromStdString(route.blocks[blockIndex].id);
			firstBlock->addItem(QString("%1:%2").arg(blockIndex + 1).arg(blockId));
			lastBlock->addItem(QString("%1:%2").arg(blockIndex + 1).arg(blockId));
		}
		if (firstBlock->count() > 0)
			lastBlock->setCurrentIndex(lastBlock->count() - 1);
	};
	const auto currentScope = [&run, routeCombo, firstBlock, lastBlock, period]() {
		CapacityAnalysisScope candidate;
		candidate.routeIndex = routeCombo->currentData().toInt();
		candidate.periodSeconds = period->value();
		if (!run.hasRoute(candidate.routeIndex))
			return candidate;
		const CompletedRoute& route = run.routes[static_cast<std::size_t>(candidate.routeIndex)];
		candidate.sectionIndices = blockSectionRangeIndices(firstBlock->currentIndex(), lastBlock->currentIndex(), static_cast<int>(route.blocks.size()));
		for (int blockIndex : candidate.sectionIndices)
			candidate.blockIds.push_back(route.blocks[static_cast<std::size_t>(blockIndex)].id);
		return candidate;
	};
	const auto refillOccurrences = [&run, occurrences, cycleEnd, currentScope, clearError]() {
		clearError();
		occurrences->clear();
		cycleEnd->clear();
		cycleEnd->addItem("Select the first train in the next period...");
		const auto trains = capacityInputsFrom(run, currentScope());
		for (const CapacityAnalysisTrain& train : trains) {
			const QString identity = QString::fromStdString(train.runtimeId);
			const QString code = QString::fromStdString(train.operatingCode);
			auto* item = new QListWidgetItem(
				QString("%1 | %2 | %3").arg(identity, code, QString::fromStdString(train.referenceSource)), occurrences);
			item->setData(Qt::UserRole, identity);
			item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled);
			item->setCheckState(Qt::Checked);
			cycleEnd->addItem(QString("%1 | %2").arg(identity, code), identity);
		}
	};
	// A route has its own blocks, so the dialog widens to show them.
	QObject::connect(routeCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
		[refillBlocks, refillOccurrences, &dialog](int) {
			refillBlocks();
			refillOccurrences();
			DialogLayout::fitWidthToContent(dialog);
		});
	QObject::connect(firstBlock, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
		[refillOccurrences](int) { refillOccurrences(); });
	QObject::connect(lastBlock, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
		[refillOccurrences](int) { refillOccurrences(); });
	QObject::connect(period, qOverload<double>(&QDoubleSpinBox::valueChanged), &dialog,
		[clearError](double) { clearError(); });
	QObject::connect(cycleEnd, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
		[clearError](int) { clearError(); });
	QObject::connect(occurrences, &QListWidget::itemChanged, &dialog,
		[clearError](QListWidgetItem*) { clearError(); });
	QObject::connect(occurrences->model(), &QAbstractItemModel::rowsMoved, &dialog,
		[clearError]() { clearError(); });
	if (routeCombo->count() > 0)
		routeCombo->setCurrentIndex(0);
	refillBlocks();
	refillOccurrences();

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	// The caller's scope changes only when the dialog is accepted.
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
		CapacityAnalysisScope chosen = currentScope();
		for (int row = 0; row < occurrences->count(); ++row) {
			const QListWidgetItem* item = occurrences->item(row);
			if (item->checkState() == Qt::Checked)
				chosen.occurrenceIds.push_back(item->data(Qt::UserRole).toString().toStdString());
		}
		if (chosen.occurrenceIds.size() < 2) {
			showError("Select at least two ordered occurrences with a common entry block.");
			return;
		}
		chosen.cycleEndOccurrenceId = cycleEnd->currentData().toString().toStdString();
		const auto selectedCycleEnd = std::find(chosen.occurrenceIds.begin(), chosen.occurrenceIds.end(),
			chosen.cycleEndOccurrenceId);
		if (selectedCycleEnd == chosen.occurrenceIds.end() || selectedCycleEnd == chosen.occurrenceIds.begin()) {
			showError("Choose a checked cycle-closing occurrence after the first row, normally the first train in the next period.");
			return;
		}
		scope = std::move(chosen);
		dialog.accept();
	});
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	bodyScroll = DialogLayout::install(dialog, "Capacity analysis",
		"Choose a route section and ordered service occurrences.", body, buttons);
	DialogLayout::fitWidthToContent(dialog);
	return dialog.exec() == QDialog::Accepted;
}
