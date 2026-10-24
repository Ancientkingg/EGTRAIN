#include "diagrams/CapacityScope.h"

#include "util/CsvWriter.h"
#include "util/TimeFormat.h"
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
#include <optional>
#include <set>
#include <utility>

CapacityAnalysisTrain capacityInputFrom(const CompletedTrain& train, const CapacityAnalysisScope& scope) {
	CapacityAnalysisTrain result;
	result.runtimeId = train.trainId;
	result.operatingCode = train.operatingCode;
	if (scope.blockIds.empty())
		return result;

	const BlockingTimeDiagramInput* reference = nullptr;
	std::string entryBlock;
	for (const BlockingTimeDiagramInput& occupation : train.blockTimes) {
		if (!validBlockingTimeDiagramInput(occupation))
			continue;
		const auto selectedBlock = std::find_if(scope.blockIds.begin(), scope.blockIds.end(),
			[&occupation](const std::string& blockId) { return shareBlockingTimeResource(occupation.blockId, blockId); });
		if (selectedBlock == scope.blockIds.end())
			continue;
		result.occupations.push_back(occupation);
		if (!reference && std::isfinite(occupation.startRunTime) && occupation.startRunTime >= 0.0) {
			reference = &occupation;
			entryBlock = *selectedBlock;
		}
	}
	if (!reference) {
		result.occupations.clear();
		return result;
	}
	result.profileReferenceTime = reference->startRunTime;
	result.referenceLabel = "block " + entryBlock + " entry";
	result.referenceSource = "selected section StartRunTime";

	// A boundary stop is the authored reference when its route position matches
	// the entry of the train into the selected blocks. The 5 m tolerance is the native route tolerance.
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
	std::vector<CapacityAnalysisTrain> candidates;
	for (const CompletedTrain& train : data.trains) {
		CapacityAnalysisTrain candidate = capacityInputFrom(train, scope);
		if (!candidate.occupations.empty())
			candidates.push_back(std::move(candidate));
	}
	if (scope.occurrenceIds.empty())
		return capacityTrainOrder(std::move(candidates));
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

namespace {

// The blocks `first` to `last` of the route, in route order, without trains.
CapacityAnalysisScope blockRangeOf(const CompletedRunData& data, int routeIndex, int first, int last) {
	CapacityAnalysisScope scope;
	scope.routeIndex = routeIndex;
	scope.repeatFirstTrain = true;
	if (!data.hasRoute(routeIndex))
		return scope;
	const CompletedRoute& route = data.routes[static_cast<std::size_t>(routeIndex)];
	scope.sectionIndices = blockSectionRangeIndices(first, last, static_cast<int>(route.blocks.size()));
	for (const int index : scope.sectionIndices)
		scope.blockIds.push_back(route.blocks[static_cast<std::size_t>(index)].id);
	return scope;
}

// The starts of `first` and `second` in the pair of shared occupations, one of each train, with the earliest start.
// Empty when the trains share no resource.
std::optional<std::pair<double, double>> earliestSharedStarts(const CapacityAnalysisTrain& first, const CapacityAnalysisTrain& second) {
	std::optional<std::pair<double, double>> earliest;
	for (const BlockingTimeDiagramInput& own : first.occupations)
		for (const BlockingTimeDiagramInput& other : second.occupations)
			if (shareBlockingTimeResource(own, other)
				&& (!earliest || std::min(own.startOccTime, other.startOccTime) < std::min(earliest->first, earliest->second)))
				earliest = std::make_pair(own.startOccTime, other.startOccTime);
	return earliest;
}

// Whether `second` is the leader of `first` in the resources they share: its occupation starts first in the pair of
// earliestSharedStarts, or both start together and its profile reference is earlier. Empty when the trains share no
// resource.
std::optional<bool> secondLeads(const CapacityAnalysisTrain& first, const CapacityAnalysisTrain& second) {
	const std::optional<std::pair<double, double>> starts = earliestSharedStarts(first, second);
	if (!starts)
		return std::nullopt;
	if (starts->first != starts->second)
		return starts->second < starts->first;
	return second.profileReferenceTime < first.profileReferenceTime;
}

} // namespace

std::vector<CapacityAnalysisTrain> capacityTrainOrder(std::vector<CapacityAnalysisTrain> trains) {
	const std::size_t count = trains.size();
	// The leaders of each train; of two trains that share a resource and tie, the earlier in run order leads.
	std::vector<std::vector<std::size_t>> leaders(count);
	for (std::size_t first = 0; first < count; ++first)
		for (std::size_t second = first + 1; second < count; ++second) {
			const std::optional<bool> leads = secondLeads(trains[first], trains[second]);
			if (leads && *leads)
				leaders[first].push_back(second);
			else if (leads)
				leaders[second].push_back(first);
		}
	std::vector<std::vector<std::size_t>> followers(count);
	for (std::size_t index = 0; index < count; ++index)
		for (const std::size_t leader : leaders[index])
			followers[leader].push_back(index);
	std::vector<bool> placed(count, false);
	const auto leadersPlaced = [&placed, &leaders](std::size_t index) {
		return std::all_of(leaders[index].begin(), leaders[index].end(), [&placed](std::size_t leader) { return placed[leader]; });
	};
	// Whether every unplaced leader of the train can be reached from it through the trains it leads, and the trains they
	// lead, among the unplaced trains: it lies in a cycle of leaders that no other unplaced train leads.
	const auto opensCycle = [&placed, &leaders, &followers, count](std::size_t index) {
		std::vector<bool> reached(count, false);
		std::vector<std::size_t> pending = {index};
		reached[index] = true;
		while (!pending.empty()) {
			const std::size_t current = pending.back();
			pending.pop_back();
			for (const std::size_t follower : followers[current])
				if (!placed[follower] && !reached[follower]) {
					reached[follower] = true;
					pending.push_back(follower);
				}
		}
		return std::all_of(leaders[index].begin(), leaders[index].end(),
			[&placed, &reached](std::size_t leader) { return placed[leader] || reached[leader]; });
	};
	// The unplaced train that `eligible` accepts with the earliest profile reference, then the earlier in run order;
	// count when there is none.
	const auto earliest = [&trains, &placed, count](const auto& eligible) {
		std::size_t chosen = count;
		for (std::size_t index = 0; index < count; ++index)
			if (!placed[index] && eligible(index)
				&& (chosen == count || trains[index].profileReferenceTime < trains[chosen].profileReferenceTime))
				chosen = index;
		return chosen;
	};
	std::vector<CapacityAnalysisTrain> ordered;
	ordered.reserve(count);
	while (ordered.size() < count) {
		// A train whose leaders are placed, else, when the leaders of the trains left form cycles, a train of such a cycle.
		std::size_t chosen = earliest(leadersPlaced);
		if (chosen == count)
			chosen = earliest(opensCycle);
		placed[chosen] = true;
		ordered.push_back(std::move(trains[chosen]));
	}
	return ordered;
}

std::pair<std::string, std::string> capacityOrderConflictFrom(const std::vector<CapacityAnalysisTrain>& trains) {
	for (std::size_t first = 0; first < trains.size(); ++first)
		for (std::size_t second = first + 1; second < trains.size(); ++second) {
			const std::optional<std::pair<double, double>> starts = earliestSharedStarts(trains[first], trains[second]);
			if (starts && starts->second < starts->first)
				return {trains[first].runtimeId, trains[second].runtimeId};
		}
	return {};
}

CapacityAnalysisResult capacityAnalysisFrom(const CompletedRunData& data, const CapacityAnalysisScope& scope) {
	std::vector<CapacityAnalysisTrain> trains = capacityInputsFrom(data, scope);
	if (!scope.repeatFirstTrain || trains.size() < 2)
		return analyzeCapacity(trains, scope.periodSeconds, scope.cycleEndOccurrenceId);
	trains.push_back(capacityNextPeriodTrain(trains.front(), scope.periodSeconds));
	const std::string cycleEnd = trains.back().runtimeId;
	return analyzeCapacity(trains, scope.periodSeconds, cycleEnd);
}

// The capacity calculation is resource-id based. Refuse only the selected ambiguity,
// rather than choosing one route occurrence or changing the capacity mathematics.
std::string capacityScopeIssueFrom(const CompletedRunData& data, const CapacityAnalysisScope& scope) {
	if (!data.hasRoute(scope.routeIndex) || scope.blockIds.empty())
		return {};
	const auto extents = buildBlockSectionExtents(data.routes[static_cast<std::size_t>(scope.routeIndex)].blocks);
	return blockSectionCapacityScopeIssue(extents, scope.sectionIndices);
}

CapacityAnalysisScope capacityRangeScopeFrom(const CompletedRunData& data, int routeIndex, int first, int last) {
	CapacityAnalysisScope scope = blockRangeOf(data, routeIndex, first, last);
	for (const CapacityAnalysisTrain& train : capacityInputsFrom(data, scope))
		scope.occurrenceIds.push_back(train.runtimeId);
	return scope;
}

CapacityAnalysisScope firstCapacityScopeWithPairFrom(const CompletedRunData& data) {
	for (std::size_t routeIndex = 0; routeIndex < data.routes.size(); ++routeIndex) {
		const int count = static_cast<int>(data.routes[routeIndex].blocks.size());
		CapacityAnalysisScope scope = capacityRangeScopeFrom(data, static_cast<int>(routeIndex), 0, count - 1);
		if (scope.occurrenceIds.size() >= 2)
			return scope;
	}
	return CapacityAnalysisScope();
}

namespace {

// True when the scope numbers each of its blocks.
bool numbered(const CapacityAnalysisScope& scope) {
	return !scope.blockIds.empty() && scope.sectionIndices.size() == scope.blockIds.size();
}

} // namespace

QString capacityScopeLabelFrom(const CompletedRunData& data, const CapacityAnalysisScope& scope) {
	if (!data.hasRoute(scope.routeIndex))
		return QStringLiteral("no selected route");
	const CompletedRoute& route = data.routes[static_cast<std::size_t>(scope.routeIndex)];
	QString label = QString::fromStdString(route.id);
	if (!route.corridor.empty())
		label += QString(" / %1").arg(QString::fromStdString(route.corridor));
	if (scope.blockIds.empty())
		return label;
	return label + QString(" / %1 to %2").arg(QString::fromStdString(scope.blockIds.front()), QString::fromStdString(scope.blockIds.back()));
}

QString capacityScopeTextFrom(const CompletedRunData& data, const CapacityAnalysisScope& scope) {
	if (!data.hasRoute(scope.routeIndex))
		return QStringLiteral("no selected route");
	const CompletedRoute& route = data.routes[static_cast<std::size_t>(scope.routeIndex)];
	QString text = QString::fromStdString(route.id);
	if (!route.corridor.empty())
		text += QString(" (%1)").arg(QString::fromStdString(route.corridor));
	const std::size_t count = scope.blockIds.size();
	if (count == 0)
		return text;
	if (!numbered(scope))
		return text + (count == 1 ? QString(", 1 block") : QString(", %1 blocks").arg(count));
	if (count == 1)
		return text + QString(", block %1").arg(scope.sectionIndices.front() + 1);
	return text + QString(", blocks %1 to %2").arg(scope.sectionIndices.front() + 1).arg(scope.sectionIndices.back() + 1);
}

QString capacityScopeBlocksText(const CompletedRunData& data, const CapacityAnalysisScope& scope) {
	if (scope.blockIds.empty())
		return {};
	const auto block = [&data, &scope](std::size_t index) {
		const QString label = QString::fromStdString(readableSectionLabel(scope.blockIds[index], data.sectionLabels));
		return numbered(scope) ? QString("%1: %2").arg(scope.sectionIndices[index] + 1).arg(label) : label;
	};
	if (scope.blockIds.size() == 1)
		return "Block " + block(0);
	return "First block " + block(0) + "\nLast block " + block(scope.blockIds.size() - 1);
}

QString capacityReferenceText(const std::string& source, const std::string& label) {
	// A boundary station is the reference of the label "<station> boundary".
	const std::string suffix = " boundary";
	const bool boundary = label.size() > suffix.size() && label.compare(label.size() - suffix.size(), suffix.size(), suffix) == 0;
	const QString station = QString::fromStdString(boundary ? label.substr(0, label.size() - suffix.size()) : label);
	if (source == "authored ScheduledDeparture at boundary station")
		return "planned departure at " + station;
	if (source == "authored ScheduledArrival at boundary station (departure absent)")
		return "planned arrival at " + station;
	if (source == "canonical scheduled entry + elapsed from RunStartTime")
		return QStringLiteral("planned entry time");
	return source.empty() ? QString() : QStringLiteral("simulated entry time");
}

QString capacityReferenceOf(const CapacityAnalysisResult& result, const std::string& identity) {
	for (std::size_t index = 0; index < result.trainIdentities.size(); ++index)
		if (result.trainIdentities[index] == identity && index < result.referenceSources.size() && index < result.referenceLabels.size())
			return capacityReferenceText(result.referenceSources[index], result.referenceLabels[index]);
	return {};
}

QString capacityPeriodText(double periodSeconds) {
	if (periodSeconds > 0.0 && std::fmod(periodSeconds, 3600.0) == 0.0)
		return QString::fromStdString(csv::formatDouble(periodSeconds / 3600.0)) + " h";
	if (periodSeconds > 0.0 && std::fmod(periodSeconds, 60.0) == 0.0)
		return QString::fromStdString(csv::formatDouble(periodSeconds / 60.0)) + " min";
	return QString::fromStdString(csv::formatDouble(periodSeconds)) + " s";
}

namespace {

// True when an occupation of each train shares a resource.
bool shareResource(const CapacityAnalysisTrain& first, const CapacityAnalysisTrain& second) {
	return std::any_of(first.occupations.begin(), first.occupations.end(), [&second](const BlockingTimeDiagramInput& own) {
		return std::any_of(second.occupations.begin(), second.occupations.end(),
			[&own](const BlockingTimeDiagramInput& other) { return shareBlockingTimeResource(own, other); });
	});
}

// A train as the chooser lists it: "06:28:34  23001-1 (23001), blocks 8 to 18", the clock time of its entry into the
// blocks of the scope, the runtime id, the operating code when it is not empty or the id, and the numbers on the route of
// the first and the last of those blocks that the train occupies.
QString trainRowText(const CompletedRunData& run, const CapacityAnalysisScope& scope, const CapacityAnalysisTrain& train) {
	const QString identity = QString::fromStdString(train.runtimeId);
	const QString code = QString::fromStdString(train.operatingCode);
	QString text = QString::fromStdString(formatSimTime(static_cast<long long>(train.profileReferenceTime), run.settings.startOffsetSeconds))
		+ "  " + (code.isEmpty() || code == identity ? identity : QString("%1 (%2)").arg(identity, code));
	std::vector<int> used;
	for (std::size_t index = 0; index < scope.blockIds.size() && index < scope.sectionIndices.size(); ++index)
		if (std::any_of(train.occupations.begin(), train.occupations.end(), [&scope, index](const BlockingTimeDiagramInput& occupation) {
				return shareBlockingTimeResource(occupation.blockId, scope.blockIds[index]);
			}))
			used.push_back(scope.sectionIndices[index] + 1);
	if (used.size() == 1)
		text += QString(", block %1").arg(used.front());
	else if (!used.empty())
		text += QString(", blocks %1 to %2").arg(used.front()).arg(used.back());
	return text;
}

} // namespace

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
		const QString id = QString::fromStdString(route.id);
		routeCombo->addItem(route.corridor.empty() ? id : QString("%1  (%2)").arg(id, QString::fromStdString(route.corridor)),
			static_cast<int>(routeIndex));
	}
	// The first and the last analysed block, by their labels; a long entry is cut in the middle. The tooltip of an entry
	// is the short id of its block.
	auto* fromBlock = new ChoiceComboBox(&dialog);
	fromBlock->setObjectName("capacityFromBlock");
	auto* toBlock = new ChoiceComboBox(&dialog);
	toBlock->setObjectName("capacityToBlock");
	auto* period = new QDoubleSpinBox(&dialog);
	period->setRange(0.001, std::numeric_limits<double>::max());
	period->setDecimals(3);
	period->setSingleStep(60.0);
	period->setSuffix(" s");
	period->setValue(3600.0);
	auto* cycleEnd = new ChoiceComboBox(&dialog);
	cycleEnd->setObjectName("capacityCycleEnd");
	auto* occurrences = new QListWidget(&dialog);
	occurrences->setSelectionMode(QAbstractItemView::SingleSelection);
	occurrences->setDragDropMode(QAbstractItemView::InternalMove);
	occurrences->setDefaultDropAction(Qt::MoveAction);
	occurrences->setMinimumHeight(180);
	form->addRow("Route / corridor:", routeCombo);
	form->addRow("From block:", fromBlock);
	form->addRow("To block:", toBlock);
	form->addRow("Analysis period:", period);
	form->addRow("Cycle-closing train:", cycleEnd);
	layout->addLayout(form);
	auto* note = new QLabel(body);
	DialogLayout::setHelp(note, "Checked trains are analysed in list order. The cycle runs from the first checked train to the "
								"cycle-closing train, the first train of the next period. Drag a row to change the order.");
	layout->addWidget(note);
	auto* occurrencesCaption = new QLabel("Trains in these blocks:", body);
	occurrencesCaption->setWordWrap(true);
	layout->addWidget(occurrencesCaption);
	layout->addWidget(occurrences);
	auto* firstTrain = new QLabel(body);
	firstTrain->setObjectName("capacityFirstTrain");
	firstTrain->setWordWrap(true);
	layout->addWidget(firstTrain);
	auto* status = new QLabel(body);
	status->setObjectName("capacityScopeStatus");
	layout->addWidget(status);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	QPushButton* ok = buttons->button(QDialogButtonBox::Ok);

	// An empty text hides the label. A text is scrolled into view once the layout has made room for it, because the body
	// of the dialog scrolls and the label is at its end.
	QScrollArea* bodyScroll = nullptr;
	const auto showStatus = [status, &bodyScroll](DialogLayout::Severity severity, const QString& text) {
		DialogLayout::setStatus(status, severity, text);
		if (!text.isEmpty() && bodyScroll)
			QTimer::singleShot(0, status, [status, bodyScroll] { bodyScroll->ensureWidgetVisible(status); });
	};
	showStatus(DialogLayout::Severity::Error, QString());

	// The analysed blocks and their trains in the order of capacityTrainOrder; the list shows those trains.
	CapacityAnalysisScope range;
	std::vector<CapacityAnalysisTrain> trains;
	// The trains that the user unchecked. They stay unchecked when another route or range lists them again.
	std::set<std::string> unchecked;
	const auto trainOf = [&trains](const QListWidgetItem* item) {
		const std::string id = item->data(Qt::UserRole).toString().toStdString();
		return std::find_if(trains.cbegin(), trains.cend(), [&id](const CapacityAnalysisTrain& train) { return train.runtimeId == id; });
	};
	const auto checkedTrains = [occurrences, &trains, trainOf]() {
		std::vector<CapacityAnalysisTrain> checked;
		for (int row = 0; row < occurrences->count(); ++row) {
			const QListWidgetItem* item = occurrences->item(row);
			const auto train = trainOf(item);
			if (item->checkState() == Qt::Checked && train != trains.cend())
				checked.push_back(*train);
		}
		return checked;
	};
	// The first checked row starts the cycle. The cycle-closing train is the first train one period later or a checked
	// row after the first; a choice that is no longer a checked row after the first falls back to the first entry.
	// Fewer than two checked trains, or a cycle-closing train that shares no block with an earlier checked train, cannot
	// be analysed, so OK is then disabled and the status says why. A train listed before one that runs ahead of it is a
	// warning only.
	const auto refreshChoices = [occurrences, firstTrain, cycleEnd, ok, showStatus, checkedTrains]() {
		const QVariant previous = cycleEnd->currentData();
		const QSignalBlocker blocker(cycleEnd);
		cycleEnd->clear();
		cycleEnd->addItem("First train, one period later");
		QString first;
		for (int row = 0; row < occurrences->count(); ++row) {
			const QListWidgetItem* item = occurrences->item(row);
			if (item->checkState() != Qt::Checked)
				continue;
			if (first.isEmpty()) {
				first = item->text();
				continue;
			}
			cycleEnd->addItem(item->text(), item->data(Qt::UserRole));
			cycleEnd->setItemData(cycleEnd->count() - 1, item->text(), Qt::ToolTipRole);
		}
		firstTrain->setText("First train of the cycle: " + (first.isEmpty() ? QStringLiteral("none") : first));
		firstTrain->setToolTip(first);
		cycleEnd->setCurrentIndex(previous.isValid() ? std::max(0, cycleEnd->findData(previous)) : 0);

		const std::vector<CapacityAnalysisTrain> checked = checkedTrains();
		const std::string closing = cycleEnd->currentData().toString().toStdString();
		const auto closingTrain = std::find_if(checked.begin(), checked.end(),
			[&closing](const CapacityAnalysisTrain& train) { return !closing.empty() && train.runtimeId == closing; });
		const bool opensNoCycle = closingTrain != checked.end()
			&& std::none_of(checked.begin(), closingTrain, [&closingTrain](const CapacityAnalysisTrain& earlier) { return shareResource(earlier, *closingTrain); });
		const auto conflict = capacityOrderConflictFrom(checked);
		ok->setEnabled(checked.size() >= 2 && !opensNoCycle);
		if (checked.size() < 2)
			showStatus(DialogLayout::Severity::Error, "Check at least two trains.");
		else if (opensNoCycle)
			showStatus(DialogLayout::Severity::Error,
				QString("%1 shares no block with an earlier checked train, so it closes no cycle. Choose another cycle-closing train.")
					.arg(QString::fromStdString(closing)));
		else if (!conflict.first.empty())
			showStatus(DialogLayout::Severity::Warning,
				QString("%1 runs ahead of %2 in these blocks; the analysis keeps the order of the list.")
					.arg(QString::fromStdString(conflict.second), QString::fromStdString(conflict.first)));
		else
			showStatus(DialogLayout::Severity::Error, QString());
	};
	// Lists the trains of the range: those of `first` checked and first, in that order, then the others in the order of
	// capacityTrainOrder, checked unless the user unchecked them.
	const auto refillTrains = [&run, &range, &trains, &unchecked, routeCombo, fromBlock, toBlock, occurrences, refreshChoices](
								  const std::vector<std::string>& first) {
		range = blockRangeOf(run, routeCombo->currentData().toInt(), fromBlock->currentIndex(), toBlock->currentIndex());
		trains = capacityInputsFrom(run, range);
		const auto rank = [&first](const CapacityAnalysisTrain& train) {
			return std::find(first.begin(), first.end(), train.runtimeId) - first.begin();
		};
		std::vector<const CapacityAnalysisTrain*> listed;
		for (const CapacityAnalysisTrain& train : trains)
			listed.push_back(&train);
		std::stable_sort(listed.begin(), listed.end(),
			[&rank](const CapacityAnalysisTrain* earlier, const CapacityAnalysisTrain* later) { return rank(*earlier) < rank(*later); });
		const QSignalBlocker blocker(occurrences);
		occurrences->clear();
		for (const CapacityAnalysisTrain* train : listed) {
			auto* item = new QListWidgetItem(trainRowText(run, range, *train), occurrences);
			item->setData(Qt::UserRole, QString::fromStdString(train->runtimeId));
			item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled);
			item->setCheckState(unchecked.count(train->runtimeId) ? Qt::Unchecked : Qt::Checked);
			// A row as wide as the list cuts a long text with an ellipsis; the tooltip has all of it.
			item->setToolTip(item->text());
			item->setSizeHint(QSize(1, occurrences->sizeHintForRow(occurrences->row(item))));
		}
		refreshChoices();
	};
	// The blocks of the route as "1: <label>", from the first to the last.
	const auto refillBlocks = [&run, routeCombo, fromBlock, toBlock]() {
		const QSignalBlocker blockFrom(fromBlock);
		const QSignalBlocker blockTo(toBlock);
		fromBlock->clear();
		toBlock->clear();
		const int routeIndex = routeCombo->currentData().toInt();
		if (!run.hasRoute(routeIndex))
			return;
		const std::vector<BlockSectionInput>& blocks = run.routes[static_cast<std::size_t>(routeIndex)].blocks;
		for (std::size_t index = 0; index < blocks.size(); ++index) {
			const QString text = QString("%1: %2").arg(index + 1).arg(QString::fromStdString(readableSectionLabel(blocks[index].id, run.sectionLabels)));
			for (QComboBox* combo : {fromBlock, toBlock}) {
				combo->addItem(text);
				combo->setItemData(combo->count() - 1, QString::fromStdString(shortBlockId(blocks[index].id)), Qt::ToolTipRole);
			}
		}
		toBlock->setCurrentIndex(toBlock->count() - 1);
	};

	// The dialog opens on the given scope when the run has its route, else on the whole of the first route.
	const bool givenRoute = run.hasRoute(scope.routeIndex);
	const CapacityAnalysisScope opening = givenRoute ? scope : firstCapacityScopeWithPairFrom(run);
	routeCombo->setCurrentIndex(std::max(0, routeCombo->findData(opening.routeIndex)));
	refillBlocks();
	if (givenRoute && !scope.sectionIndices.empty()) {
		const auto [low, high] = std::minmax_element(scope.sectionIndices.begin(), scope.sectionIndices.end());
		fromBlock->setCurrentIndex(std::min(*low, fromBlock->count() - 1));
		toBlock->setCurrentIndex(std::min(*high, toBlock->count() - 1));
	}
	period->setValue(opening.periodSeconds);
	refillTrains(givenRoute ? scope.occurrenceIds : std::vector<std::string>());
	// A given scope that names its trains leaves the other trains of its range unchecked.
	if (givenRoute && !scope.occurrenceIds.empty()) {
		const QSignalBlocker blocker(occurrences);
		for (int row = 0; row < occurrences->count(); ++row) {
			QListWidgetItem* item = occurrences->item(row);
			const std::string id = item->data(Qt::UserRole).toString().toStdString();
			if (std::find(scope.occurrenceIds.begin(), scope.occurrenceIds.end(), id) == scope.occurrenceIds.end()) {
				item->setCheckState(Qt::Unchecked);
				unchecked.insert(id);
			}
		}
	}
	if (givenRoute && !scope.repeatFirstTrain)
		cycleEnd->setCurrentIndex(std::max(0, cycleEnd->findData(QString::fromStdString(scope.cycleEndOccurrenceId))));
	refreshChoices();

	// A route has its own trains and blocks, so the dialog widens to show them.
	QObject::connect(routeCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
		[refillBlocks, refillTrains, &dialog](int) {
			refillBlocks();
			refillTrains({});
			DialogLayout::fitWidthToContent(dialog);
		});
	// A first block after the last moves the last block to it, and a last block before the first moves the first block.
	QObject::connect(fromBlock, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [toBlock, refillTrains](int index) {
		if (index > toBlock->currentIndex()) {
			const QSignalBlocker blocker(toBlock);
			toBlock->setCurrentIndex(index);
		}
		refillTrains({});
	});
	QObject::connect(toBlock, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [fromBlock, refillTrains](int index) {
		if (index < fromBlock->currentIndex()) {
			const QSignalBlocker blocker(fromBlock);
			fromBlock->setCurrentIndex(index);
		}
		refillTrains({});
	});
	QObject::connect(cycleEnd, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [refreshChoices](int) { refreshChoices(); });
	QObject::connect(occurrences, &QListWidget::itemChanged, &dialog, [&unchecked, refreshChoices](QListWidgetItem* item) {
		const std::string id = item->data(Qt::UserRole).toString().toStdString();
		if (item->checkState() == Qt::Checked)
			unchecked.erase(id);
		else
			unchecked.insert(id);
		refreshChoices();
	});
	QObject::connect(occurrences->model(), &QAbstractItemModel::rowsMoved, &dialog, [refreshChoices]() { refreshChoices(); });

	// The caller's scope changes only when the dialog is accepted.
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
		CapacityAnalysisScope chosen = range;
		for (const CapacityAnalysisTrain& train : checkedTrains())
			chosen.occurrenceIds.push_back(train.runtimeId);
		chosen.repeatFirstTrain = cycleEnd->currentIndex() <= 0;
		chosen.cycleEndOccurrenceId = chosen.repeatFirstTrain ? std::string() : cycleEnd->currentData().toString().toStdString();
		chosen.periodSeconds = period->value();
		scope = std::move(chosen);
		dialog.accept();
	});
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	bodyScroll = DialogLayout::install(dialog, "Capacity", QString(), body, buttons);
	DialogLayout::fitWidthToContent(dialog);
	return dialog.exec() == QDialog::Accepted;
}
