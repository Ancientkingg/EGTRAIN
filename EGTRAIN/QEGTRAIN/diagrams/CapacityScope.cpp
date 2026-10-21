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
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSize>
#include <QTimer>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstddef>
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

namespace {

// True when the train has an occupation of the block that capacityInputFrom can take as its reference.
bool occupiesWithReference(const CompletedTrain& train, const std::string& blockId) {
	CapacityAnalysisScope block;
	block.blockIds = {blockId};
	return !capacityInputFrom(train, block).occupations.empty();
}

// The trains, in run order, that occupy the first block of the route that two or more trains occupy. Empty when no
// block has two.
std::vector<std::string> firstPairOn(const CompletedRunData& data, const CompletedRoute& route) {
	for (const BlockSectionInput& block : route.blocks) {
		std::vector<std::string> ids;
		for (const CompletedTrain& train : data.trains)
			if (occupiesWithReference(train, block.id))
				ids.push_back(train.trainId);
		if (ids.size() >= 2)
			return ids;
	}
	return {};
}

// The first of the scopes with the most blocks, the end when there is none.
std::vector<CapacityAnalysisScope>::const_iterator longestOf(const std::vector<CapacityAnalysisScope>& scopes) {
	return std::max_element(scopes.cbegin(), scopes.cend(), [](const CapacityAnalysisScope& first, const CapacityAnalysisScope& second) {
		return first.blockIds.size() < second.blockIds.size();
	});
}

} // namespace

std::vector<CapacityAnalysisScope> capacityStretchesFrom(const CompletedRunData& data, int routeIndex,
	const std::vector<std::string>& occurrenceIds) {
	std::vector<CapacityAnalysisScope> stretches;
	if (!data.hasRoute(routeIndex) || occurrenceIds.empty())
		return stretches;
	std::vector<const CompletedTrain*> trains;
	for (const std::string& id : occurrenceIds) {
		const auto train = std::find_if(data.trains.begin(), data.trains.end(),
			[&id](const CompletedTrain& candidate) { return candidate.trainId == id; });
		if (train == data.trains.end())
			return stretches;
		trains.push_back(&*train);
	}
	const CompletedRoute& route = data.routes[static_cast<std::size_t>(routeIndex)];
	for (std::size_t blockIndex = 0; blockIndex < route.blocks.size(); ++blockIndex) {
		const std::string& blockId = route.blocks[blockIndex].id;
		if (!std::all_of(trains.begin(), trains.end(),
				[&blockId](const CompletedTrain* train) { return occupiesWithReference(*train, blockId); }))
			continue;
		// A shared block right after the last one of a section extends it; any other starts a section.
		if (stretches.empty() || stretches.back().sectionIndices.back() + 1 != static_cast<int>(blockIndex)) {
			stretches.emplace_back();
			stretches.back().routeIndex = routeIndex;
			stretches.back().occurrenceIds = occurrenceIds;
		}
		stretches.back().blockIds.push_back(blockId);
		stretches.back().sectionIndices.push_back(static_cast<int>(blockIndex));
	}
	return stretches;
}

CapacityAnalysisScope capacityStretchFrom(const CompletedRunData& data, int routeIndex,
	const std::vector<std::string>& occurrenceIds) {
	const std::vector<CapacityAnalysisScope> stretches = capacityStretchesFrom(data, routeIndex, occurrenceIds);
	const auto longest = longestOf(stretches);
	if (longest != stretches.end())
		return *longest;
	CapacityAnalysisScope scope;
	scope.routeIndex = routeIndex;
	scope.occurrenceIds = occurrenceIds;
	return scope;
}

CapacityAnalysisScope firstCapacityScopeWithPairFrom(const CompletedRunData& data) {
	for (std::size_t routeIndex = 0; routeIndex < data.routes.size(); ++routeIndex) {
		const std::vector<std::string> ids = firstPairOn(data, data.routes[routeIndex]);
		if (!ids.empty())
			return capacityStretchFrom(data, static_cast<int>(routeIndex), ids);
	}
	return CapacityAnalysisScope();
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
	dialog.setWindowTitle("Capacity");
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
	// One entry for each section that the checked occurrences share. A long entry is cut in the middle; the tooltip has
	// all of it.
	auto* blocks = new ChoiceComboBox(&dialog);
	blocks->setObjectName("capacityBlocks");
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
	form->addRow("Analyzed blocks:", blocks);
	form->addRow("Analysis period:", period);
	form->addRow("Cycle-closing occurrence:", cycleEnd);
	layout->addLayout(form);
	auto* note = new QLabel(body);
	DialogLayout::setHelp(note,
		"Checked occurrences are analyzed in the displayed order (A → B) on every analyzed block; selecting exactly two enables "
		"the pair workflow. The analyzed blocks are one section of the route: consecutive blocks that all checked occurrences "
		"occupy, up to a block that one of them does not occupy. Analyzed blocks lists each such section in route order and "
		"selects the longest. Select the occurrence of the first service in the next period that closes the capacity cycle; "
		"EGTRAIN does not infer it from the last row.");
	layout->addWidget(note);
	auto* occurrencesCaption = new QLabel("Occurrences with complete occupations on the route:", body);
	occurrencesCaption->setWordWrap(true);
	layout->addWidget(occurrencesCaption);
	layout->addWidget(occurrences);
	auto* status = new QLabel(body);
	status->setObjectName("capacityScopeStatus");
	layout->addWidget(status);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	QPushButton* ok = buttons->button(QDialogButtonBox::Ok);

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

	const auto checkedIds = [occurrences]() {
		std::vector<std::string> checked;
		for (int row = 0; row < occurrences->count(); ++row) {
			const QListWidgetItem* item = occurrences->item(row);
			if (item->checkState() == Qt::Checked)
				checked.push_back(item->data(Qt::UserRole).toString().toStdString());
		}
		return checked;
	};
	// The sections that the checked occurrences share, in the order of the entries of `blocks`.
	std::vector<CapacityAnalysisScope> sections;
	const auto currentScope = [&sections, routeCombo, blocks, period, checkedIds]() {
		CapacityAnalysisScope candidate;
		const int index = blocks->currentIndex();
		if (index >= 0 && index < static_cast<int>(sections.size()))
			candidate = sections[static_cast<std::size_t>(index)];
		candidate.routeIndex = routeCombo->currentData().toInt();
		candidate.occurrenceIds = checkedIds();
		candidate.periodSeconds = period->value();
		return candidate;
	};
	// Any change takes a refusal of OK away. Two or more checked occurrences that share no block of the route cannot be
	// analyzed, so OK is then disabled and the status says why.
	const auto showScopeStatus = [&sections, ok, checkedIds, showError, clearError]() {
		const bool unshared = sections.empty() && checkedIds().size() >= 2;
		ok->setEnabled(!unshared);
		if (unshared)
			showError("No block of the route is shared by all checked occurrences.");
		else
			clearError();
	};
	// Lists the sections of the checked occurrences, each as "1:B to 2:C (2 blocks)", or "none". The section that starts
	// at the block where the selected one started stays selected, else the longest is. With fewer than two sections there
	// is nothing to choose, so the field is disabled.
	const auto refillSections = [&run, &sections, routeCombo, blocks, checkedIds, showScopeStatus]() {
		const int previous = blocks->currentIndex();
		const int start = previous >= 0 && previous < static_cast<int>(sections.size())
			? sections[static_cast<std::size_t>(previous)].sectionIndices.front()
			: -1;
		sections = capacityStretchesFrom(run, routeCombo->currentData().toInt(), checkedIds());
		const QSignalBlocker blocker(blocks);
		blocks->clear();
		for (const CapacityAnalysisScope& section : sections) {
			const std::size_t count = section.blockIds.size();
			const auto block = [&section](std::size_t index) {
				return QString("%1:%2").arg(section.sectionIndices[index] + 1).arg(QString::fromStdString(section.blockIds[index]));
			};
			const QString text = count == 1 ? QString("%1 (1 block)").arg(block(0))
											: QString("%1 to %2 (%3 blocks)").arg(block(0), block(count - 1)).arg(count);
			blocks->addItem(text);
			blocks->setItemData(blocks->count() - 1, text, Qt::ToolTipRole);
		}
		if (sections.empty())
			blocks->addItem("none");
		auto selected = std::find_if(sections.cbegin(), sections.cend(),
			[start](const CapacityAnalysisScope& section) { return section.sectionIndices.front() == start; });
		if (selected == sections.cend())
			selected = longestOf(sections);
		if (selected != sections.cend())
			blocks->setCurrentIndex(static_cast<int>(selected - sections.cbegin()));
		blocks->setEnabled(sections.size() > 1);
		showScopeStatus();
	};
	// With exactly two checked occurrences only the second one can close the cycle, so it is chosen.
	const auto chooseOnlyCycleEnd = [occurrences, cycleEnd]() {
		QVariant second;
		int checked = 0;
		for (int row = 0; row < occurrences->count(); ++row) {
			const QListWidgetItem* item = occurrences->item(row);
			if (item->checkState() != Qt::Checked)
				continue;
			if (++checked == 2)
				second = item->data(Qt::UserRole);
		}
		const int index = checked == 2 ? cycleEnd->findData(second) : -1;
		if (index >= 0)
			cycleEnd->setCurrentIndex(index);
	};
	// The trains that occupy a block of the route, the ones of `checked` first and checked, in that order, and the others
	// in run order.
	const auto refillOccurrences = [&run, &sections, routeCombo, occurrences, cycleEnd, refillSections, chooseOnlyCycleEnd](
									   const std::vector<std::string>& checked) {
		// The sections of another route start at other blocks, so the longest one is selected.
		sections.clear();
		occurrences->clear();
		cycleEnd->clear();
		cycleEnd->addItem("Select the first train in the next period...");
		std::vector<const CompletedTrain*> listed;
		const int routeIndex = routeCombo->currentData().toInt();
		if (run.hasRoute(routeIndex)) {
			const CompletedRoute& route = run.routes[static_cast<std::size_t>(routeIndex)];
			for (const CompletedTrain& train : run.trains)
				if (std::any_of(route.blocks.begin(), route.blocks.end(),
						[&train](const BlockSectionInput& block) { return occupiesWithReference(train, block.id); }))
					listed.push_back(&train);
		}
		const auto rank = [&checked](const CompletedTrain* train) {
			return std::find(checked.begin(), checked.end(), train->trainId) - checked.begin();
		};
		std::stable_sort(listed.begin(), listed.end(),
			[&rank](const CompletedTrain* first, const CompletedTrain* second) { return rank(first) < rank(second); });
		const QSignalBlocker blocker(occurrences);
		for (const CompletedTrain* train : listed) {
			const QString identity = QString::fromStdString(train->trainId);
			const QString code = QString::fromStdString(train->operatingCode);
			auto* item = new QListWidgetItem(QString("%1 | %2").arg(identity, code), occurrences);
			item->setData(Qt::UserRole, identity);
			item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled);
			item->setCheckState(rank(train) < static_cast<std::ptrdiff_t>(checked.size()) ? Qt::Checked : Qt::Unchecked);
			// A row as wide as the list cuts a long text with an ellipsis; the tooltip has all of it.
			item->setToolTip(item->text());
			item->setSizeHint(QSize(1, occurrences->sizeHintForRow(occurrences->row(item))));
			cycleEnd->addItem(QString("%1 | %2").arg(identity, code), identity);
		}
		refillSections();
		chooseOnlyCycleEnd();
	};
	// The dialog opens on the route and the occurrences of the given scope when the run has that route. Another route
	// opens with the trains of its first block that two or more trains occupy.
	const auto routeDefault = [&run, routeCombo]() {
		const int routeIndex = routeCombo->currentData().toInt();
		return run.hasRoute(routeIndex) ? firstPairOn(run, run.routes[static_cast<std::size_t>(routeIndex)]) : std::vector<std::string>();
	};
	const bool givenRoute = run.hasRoute(scope.routeIndex);
	routeCombo->setCurrentIndex(givenRoute ? scope.routeIndex : 0);
	refillOccurrences(givenRoute && !scope.occurrenceIds.empty() ? scope.occurrenceIds : routeDefault());
	// A route has its own trains and blocks, so the dialog widens to show them.
	QObject::connect(routeCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
		[refillOccurrences, routeDefault, &dialog](int) {
			refillOccurrences(routeDefault());
			DialogLayout::fitWidthToContent(dialog);
		});
	QObject::connect(blocks, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
		[showScopeStatus](int) { showScopeStatus(); });
	QObject::connect(period, qOverload<double>(&QDoubleSpinBox::valueChanged), &dialog,
		[showScopeStatus](double) { showScopeStatus(); });
	QObject::connect(cycleEnd, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
		[showScopeStatus](int) { showScopeStatus(); });
	QObject::connect(occurrences, &QListWidget::itemChanged, &dialog,
		[refillSections, chooseOnlyCycleEnd](QListWidgetItem*) {
			refillSections();
			chooseOnlyCycleEnd();
		});
	QObject::connect(occurrences->model(), &QAbstractItemModel::rowsMoved, &dialog,
		[refillSections, chooseOnlyCycleEnd]() {
			refillSections();
			chooseOnlyCycleEnd();
		});

	// The caller's scope changes only when the dialog is accepted.
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
		CapacityAnalysisScope chosen = currentScope();
		if (chosen.occurrenceIds.size() < 2) {
			showError("Check at least two occurrences.");
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
	bodyScroll = DialogLayout::install(dialog, "Capacity",
		"Choose a route and ordered service occurrences.", body, buttons);
	DialogLayout::fitWidthToContent(dialog);
	return dialog.exec() == QDialog::Accepted;
}
