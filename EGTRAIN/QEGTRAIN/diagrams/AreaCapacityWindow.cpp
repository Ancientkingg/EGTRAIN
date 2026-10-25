#include "diagrams/AreaCapacityWindow.h"

#include "app/DialogFolders.h"
#include "diagrams/CapacityWindow.h"
#include "diagrams/ResultFrame.h"
#include "diagrams/RunExports.h"
#include "util/CsvWriter.h"
#include "widgets/ChoiceComboBox.h"
#include "widgets/DialogLayout.h"
#include "widgets/EditorLayout.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFont>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QStringList>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

namespace {

constexpr AreaCapacityNoteKind kNoteKinds[] = {
	AreaCapacityNoteKind::EmptyScope,
	AreaCapacityNoteKind::InvalidPeriod,
	AreaCapacityNoteKind::DuplicateIdentity,
	AreaCapacityNoteKind::NoSelectedTraffic,
	AreaCapacityNoteKind::IncompleteOccupation,
	AreaCapacityNoteKind::RepeatedResource,
	AreaCapacityNoteKind::OpposingTrain,
	AreaCapacityNoteKind::InvalidReference,
	AreaCapacityNoteKind::FewerThanTwoTrains,
	AreaCapacityNoteKind::NoSharedResource,
	AreaCapacityNoteKind::ChainConflict,
	AreaCapacityNoteKind::UnselectedTraffic};

// The notes about a train or an occupation that the figures leave out or count only in part.
bool excludesTraffic(AreaCapacityNoteKind kind) {
	return kind == AreaCapacityNoteKind::UnselectedTraffic || kind == AreaCapacityNoteKind::DuplicateIdentity
		|| kind == AreaCapacityNoteKind::IncompleteOccupation || kind == AreaCapacityNoteKind::OpposingTrain;
}

QString secondsText(double seconds) {
	return QString::fromStdString(csv::formatDouble(seconds));
}

// -1.0 marks a percentage that is not available.
QString percentText(double percentage) {
	return percentage < 0.0 ? QStringLiteral("-") : QString::number(percentage, 'f', 1);
}

std::string csvField(double value) {
	return value < 0.0 ? std::string(csv::kMissingValue) : csv::formatDouble(value);
}

QString trafficName(const AreaCapacityResult& result) {
	return result.complete ? QStringLiteral("complete traffic") : QStringLiteral("partial traffic");
}

// The cycle time and its percentage exist only for a chain that can be analysed and has a cycle-closing train.
bool chainHasCycle(const CapacityAnalysisResult& chain) {
	return chain.analyzable && !chain.cycleEndIdentity.empty() && std::isfinite(chain.cycleTime) && chain.cycleTime >= 0.0;
}

// A resource by its short id, as the section strip names it; empty for none.
QString resourceText(const std::string& id) {
	return QString::fromStdString(shortBlockId(id));
}

// Gives each cell of `column`, which names the resource `ids` of its row by its short id, the label of the resource as
// its tooltip.
void setLabelTips(QTableWidget* table, int column, const std::vector<std::string>& ids, const SceneSectionLabels& labels) {
	for (int row = 0; row < table->rowCount() && row < static_cast<int>(ids.size()); ++row)
		if (QTableWidgetItem* item = table->item(row, column); item && !ids[static_cast<std::size_t>(row)].empty())
			item->setToolTip(QString::fromStdString(readableSectionLabel(ids[static_cast<std::size_t>(row)], labels)));
}

// A tab or section title with its count, which it leaves out when it is 0.
QString countedTitle(const QString& title, std::size_t count) {
	return count == 0 ? title : QString("%1 (%2)").arg(title).arg(count);
}

QTableWidget* makeTable(QWidget* parent, const QStringList& headers, const std::vector<QStringList>& rows, const QList<int>& numericColumns = {}) {
	auto* table = new QTableWidget(static_cast<int>(rows.size()), static_cast<int>(headers.size()), parent);
	table->setHorizontalHeaderLabels(headers);
	table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	table->setAlternatingRowColors(true);
	// Tab leaves the table for the next control; the arrow keys move between the cells.
	table->setTabKeyNavigation(false);
	table->horizontalHeader()->setStretchLastSection(true);
	table->verticalHeader()->setVisible(false);
	for (int row = 0; row < table->rowCount(); ++row)
		for (int column = 0; column < headers.size(); ++column) {
			auto* item = new QTableWidgetItem(rows[static_cast<std::size_t>(row)].value(column));
			if (numericColumns.contains(column))
				item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
			table->setItem(row, column, item);
		}
	table->resizeColumnsToContents();
	return table;
}

QLabel* makeHelp(QWidget* parent, const QString& text) {
	auto* label = new QLabel(parent);
	DialogLayout::setHelp(label, text);
	return label;
}

QWidget* resourcesTab(const AreaCapacityResult& result, const SceneSectionLabels& labels, QWidget* parent) {
	auto* page = new QWidget(parent);
	auto* layout = new QVBoxLayout(page);
	std::vector<QStringList> rows;
	std::vector<std::string> ids;
	for (const AreaCapacityResource& resource : result.resources) {
		const QString status = resource.governing
			? QStringLiteral("governing resource")
			: (resource.trainCount == 0 ? QStringLiteral("no traffic") : QString());
		ids.push_back(resource.resourceId);
		rows.push_back({resourceText(resource.resourceId), percentText(resource.occupationPercentage), status,
			QString::number(resource.trainCount), QString::number(resource.occupationCount), secondsText(resource.occupationSeconds),
			secondsText(resource.unionSeconds)});
	}
	QTableWidget* table = makeTable(page,
		{"Resource", "Rate (% of period)", "Status", "Trains", "Occupations", "Occupation (s)", "Union (s)"}, rows, {1, 3, 4, 5, 6});
	table->setObjectName("areaCapacityResources");
	table->horizontalHeaderItem(1)->setToolTip("Occupation time of the checked trains on the resource, divided by the period\n"
											   "The governing resource has the largest rate");
	table->horizontalHeaderItem(6)->setToolTip("The same occupations, with overlapping time counted once");
	setLabelTips(table, 0, ids, labels);
	for (int row = 0; row < table->rowCount(); ++row) {
		if (!result.resources[static_cast<std::size_t>(row)].governing)
			continue;
		for (int column = 0; column < table->columnCount(); ++column) {
			QFont font = table->item(row, column)->font();
			font.setBold(true);
			table->item(row, column)->setFont(font);
		}
	}
	layout->addWidget(table, 1);
	return page;
}

QWidget* chainTab(const AreaCapacityResult& result, const SceneSectionLabels& labels, QWidget* parent) {
	const CapacityAnalysisResult& chain = result.chain;
	auto* page = new QWidget(parent);
	auto* layout = new QVBoxLayout(page);

	QString summary;
	QStringList references;
	if (chainHasCycle(chain)) {
		const QString cycle = chain.cyclePercentage >= 0.0
			? QString("Cycle %1 s of a %2 s period (%3%)")
				  .arg(secondsText(chain.cycleTime), secondsText(chain.periodSeconds), percentText(chain.cyclePercentage))
			: QString("Cycle %1 s, no valid period").arg(secondsText(chain.cycleTime));
		summary = QString("%1, from %2 to %3")
					  .arg(cycle, QString::fromStdString(chain.firstIdentity), QString::fromStdString(chain.cycleEndIdentity));
		for (const std::string& identity : {chain.firstIdentity, chain.cycleEndIdentity}) {
			const QString reference = capacityReferenceOf(chain, identity);
			if (!reference.isEmpty())
				references << QString::fromStdString(identity) + ": " + reference;
		}
	} else if (chain.analyzable && !chain.cycleEndIdentity.empty()) {
		summary = QString("Train %1 shares no resource with an earlier train of the chain, so it closes no cycle.")
					  .arg(QString::fromStdString(chain.cycleEndIdentity));
	} else if (chain.analyzable) {
		summary = QStringLiteral("No cycle-closing train was chosen.");
	} else {
		summary = QStringLiteral("The chain cannot be analysed. See Notes.");
	}
	auto* summaryLabel = makeHelp(page, summary);
	summaryLabel->setObjectName("areaCapacityChainSummary");
	summaryLabel->setToolTip(references.join('\n'));
	layout->addWidget(summaryLabel);

	for (const CapacityChainTable& chainTable :
		capacityChainTables(chain, labels, QStringLiteral("resource"), QStringLiteral("areaCapacity"), page)) {
		layout->addWidget(EditorLayout::sectionHeading(chainTable.title, page));
		layout->addWidget(chainTable.table, 1);
	}
	return page;
}

std::vector<QStringList> noteRows(const AreaCapacityResult& result, bool excludedOnly) {
	std::vector<QStringList> rows;
	for (const AreaCapacityNote& note : result.notes) {
		if (excludedOnly && !excludesTraffic(note.kind))
			continue;
		rows.push_back({areaCapacityStatusName(note.kind), QString::fromStdString(note.trainId), QString::fromStdString(note.otherTrainId),
			resourceText(note.resourceId), areaCapacityStatusText(note, true)});
	}
	return rows;
}

// The resources of the notes of noteRows, by row.
std::vector<std::string> noteResources(const AreaCapacityResult& result, bool excludedOnly) {
	std::vector<std::string> ids;
	for (const AreaCapacityNote& note : result.notes)
		if (!excludedOnly || excludesTraffic(note.kind)) ids.push_back(note.resourceId);
	return ids;
}

QWidget* notesTab(const std::vector<QStringList>& rows, const std::vector<std::string>& resources, const QString& tableName,
	const SceneSectionLabels& labels, QWidget* parent) {
	auto* page = new QWidget(parent);
	auto* layout = new QVBoxLayout(page);
	QTableWidget* table = makeTable(page, {"Status", "Train", "Other train", "Resource", "Details"}, rows);
	table->setObjectName(tableName);
	setLabelTips(table, 3, resources, labels);
	// A detail can be longer than its column.
	for (int row = 0; row < table->rowCount(); ++row)
		table->item(row, 4)->setToolTip(table->item(row, 4)->text());
	layout->addWidget(table, 1);
	return page;
}

// "Statuses: Opposing train (2), Unselected traffic (1)": every status that occurs, with its count; empty for none.
QString statusLine(const AreaCapacityResult& result) {
	QStringList parts;
	for (const AreaCapacityNoteKind kind : kNoteKinds) {
		const auto count = std::count_if(result.notes.begin(), result.notes.end(), [kind](const AreaCapacityNote& note) { return note.kind == kind; });
		if (count > 0)
			parts << QString("%1 (%2)").arg(areaCapacityStatusName(kind)).arg(count);
	}
	return parts.isEmpty() ? QString() : "Statuses: " + parts.join(", ");
}

// The figures of the panel: the rate of the governing resources, named by their labels, the cycle of the chain and its
// share of the period. A figure that is not available reads "-" and its caption says why.
void fillFigures(const AreaCapacityResult& result, const SceneSectionLabels& labels, ResultFigure* governing, ResultFigure* cycle,
	ResultFigure* share) {
	// A switch label holds a comma, so the labels are joined with semicolons.
	QStringList names;
	double percentage = -1.0;
	for (const AreaCapacityResource& resource : result.resources) {
		if (!resource.governing)
			continue;
		names << QString::fromStdString(readableSectionLabel(resource.resourceId, labels));
		percentage = resource.occupationPercentage;
	}
	if (names.isEmpty())
		governing->setFigure(QStringLiteral("-"), QStringLiteral("No governing resource"));
	else
		governing->setFigure(percentage >= 0.0 ? percentText(percentage) + "%" : QStringLiteral("-"), "Governing: " + names.join("; "),
			percentage >= 0.0 ? QString() : QStringLiteral("No valid period"));

	const CapacityAnalysisResult& chain = result.chain;
	const QString period = chain.periodSeconds > 0.0 ? "of " + capacityPeriodText(chain.periodSeconds) : QStringLiteral("No valid period");
	if (!chainHasCycle(chain) && chain.analyzable && !chain.cycleEndIdentity.empty()) {
		cycle->setFigure(QStringLiteral("-"), QStringLiteral("No cycle"),
			QString("Train %1 shares no resource with an earlier train of the chain.").arg(QString::fromStdString(chain.cycleEndIdentity)));
		share->setFigure(QStringLiteral("-"), period);
		return;
	}
	if (!chainHasCycle(chain)) {
		cycle->setFigure(QStringLiteral("-"), chain.analyzable ? QStringLiteral("No cycle-closing train") : QStringLiteral("Chain cannot be analysed"));
		share->setFigure(QStringLiteral("-"), period);
		return;
	}
	cycle->setFigure(QString::number(chain.cycleTime, 'f', 1) + " s", QStringLiteral("Cycle"),
		QString("From %1 to %2").arg(QString::fromStdString(chain.firstIdentity), QString::fromStdString(chain.cycleEndIdentity)));
	share->setFigure(chain.cyclePercentage >= 0.0 ? percentText(chain.cyclePercentage) + "%" : QStringLiteral("-"), period);
}

bool touchesArea(const CompletedTrain& train, const std::vector<std::string>& resources) {
	for (const BlockingTimeDiagramInput& occupation : train.blockTimes)
		for (const std::string& resource : resources)
			if (shareBlockingTimeResource(occupation.blockId, resource))
				return true;
	return false;
}

// The scheduled reference time, else the profile reference time; trains without either come last.
double referenceKey(const CapacityAnalysisTrain& train) {
	if (std::isfinite(train.scheduledReferenceTime) && train.scheduledReferenceTime >= 0.0)
		return train.scheduledReferenceTime;
	if (std::isfinite(train.profileReferenceTime) && train.profileReferenceTime >= 0.0)
		return train.profileReferenceTime;
	return std::numeric_limits<double>::infinity();
}

bool knownRoute(const CompletedRunData& data, int routeIndex) {
	return routeIndex >= 0 && routeIndex < static_cast<int>(data.routes.size());
}

// The positions of the blocks from `first` to `last` in the route, from 0.
std::vector<int> blockRangeIndices(const CompletedRunData& data, int routeIndex, int first, int last) {
	if (!knownRoute(data, routeIndex))
		return {};
	return blockSectionRangeIndices(first, last, static_cast<int>(data.routes[static_cast<std::size_t>(routeIndex)].blocks.size()));
}

std::vector<std::string> blockRange(const CompletedRunData& data, int routeIndex, int first, int last) {
	std::vector<std::string> ids;
	for (const int index : blockRangeIndices(data, routeIndex, first, last))
		ids.push_back(data.routes[static_cast<std::size_t>(routeIndex)].blocks[static_cast<std::size_t>(index)].id);
	return ids;
}

CapacityAnalysisScope referenceScope(const AreaCapacityScope& scope) {
	CapacityAnalysisScope reference;
	reference.routeIndex = scope.routeIndex;
	reference.blockIds = scope.blockIds;
	reference.sectionIndices = scope.sectionIndices;
	return reference;
}

// The first resource of the first range, where capacityInputFrom takes the reference time of a train of the area.
CapacityAnalysisScope entryScope(const AreaCapacityScope& scope) {
	CapacityAnalysisScope entry = referenceScope(scope);
	entry.blockIds.resize(std::min<std::size_t>(entry.blockIds.size(), 1));
	entry.sectionIndices.resize(std::min<std::size_t>(entry.sectionIndices.size(), 1));
	return entry;
}

std::vector<std::string> areaResources(const AreaCapacityScope& scope) {
	std::vector<std::string> resources = scope.blockIds;
	resources.insert(resources.end(), scope.secondBlockIds.begin(), scope.secondBlockIds.end());
	return resources;
}

bool runsReversed(const CompletedRunData& data, const CompletedTrain& train) {
	return knownRoute(data, train.routeIndex) && data.routes[static_cast<std::size_t>(train.routeIndex)].reversedDirection;
}

bool runsReversed(const CompletedRunData& data, const std::string& trainId) {
	const auto train = std::find_if(data.trains.begin(), data.trains.end(), [&trainId](const CompletedTrain& candidate) { return candidate.trainId == trainId; });
	return train != data.trains.end() && runsReversed(data, *train);
}

AreaCapacityTrain areaTrainFor(const CompletedRunData& data, const CompletedTrain& train, const CapacityAnalysisScope& reference) {
	AreaCapacityTrain result;
	result.train = capacityInputFrom(train, reference);
	// capacityInputFrom keeps the usable occupations of the first route only. The analysis picks the occupations of the area and reports the unusable ones.
	result.train.occupations = train.blockTimes;
	result.reversedDirection = runsReversed(data, train);
	return result;
}

QString routeText(const CompletedRoute& route) {
	const QString id = QString::fromStdString(route.id);
	return route.corridor.empty() ? id : QString("%1  (%2)").arg(id, QString::fromStdString(route.corridor));
}

} // namespace

AreaCapacityInput areaCapacityInputFrom(const CompletedRunData& data, const AreaCapacityScope& scope) {
	AreaCapacityInput input;
	input.resourceIds = areaResources(scope);
	input.periodSeconds = scope.periodSeconds;
	input.cycleEndIdentity = scope.cycleEndOccurrenceId;
	const CapacityAnalysisScope reference = entryScope(scope);
	for (const std::string& id : scope.occurrenceIds) {
		const auto train = std::find_if(data.trains.begin(), data.trains.end(), [&id](const CompletedTrain& candidate) { return candidate.trainId == id; });
		if (train != data.trains.end())
			input.selected.push_back(areaTrainFor(data, *train, reference));
	}
	for (const CompletedTrain& train : data.trains) {
		if (std::find(scope.occurrenceIds.begin(), scope.occurrenceIds.end(), train.trainId) == scope.occurrenceIds.end())
			input.otherTraffic.push_back(areaTrainFor(data, train, reference));
	}
	return input;
}

namespace {

QString areaText(const CompletedRunData& data, const AreaCapacityScope& scope,
	QString (*scopeText)(const CompletedRunData&, const CapacityAnalysisScope&)) {
	QString label = scopeText(data, referenceScope(scope));
	if (knownRoute(data, scope.secondRouteIndex)) {
		CapacityAnalysisScope second;
		second.routeIndex = scope.secondRouteIndex;
		second.blockIds = scope.secondBlockIds;
		second.sectionIndices = scope.secondSectionIndices;
		label += " and " + scopeText(data, second);
	}
	return label;
}

} // namespace

QString areaCapacityLabel(const CompletedRunData& data, const AreaCapacityScope& scope) {
	return areaText(data, scope, capacityScopeLabelFrom);
}

QString areaCapacityText(const CompletedRunData& data, const AreaCapacityScope& scope) {
	return areaText(data, scope, capacityScopeTextFrom);
}

QString areaCapacityStatusName(AreaCapacityNoteKind kind) {
	switch (kind) {
		case AreaCapacityNoteKind::EmptyScope: return QStringLiteral("Empty scope");
		case AreaCapacityNoteKind::InvalidPeriod: return QStringLiteral("Invalid period");
		case AreaCapacityNoteKind::DuplicateIdentity: return QStringLiteral("Duplicate train");
		case AreaCapacityNoteKind::NoSelectedTraffic: return QStringLiteral("No selected traffic");
		case AreaCapacityNoteKind::IncompleteOccupation: return QStringLiteral("Incomplete occupation");
		case AreaCapacityNoteKind::RepeatedResource: return QStringLiteral("Repeated resource");
		case AreaCapacityNoteKind::OpposingTrain: return QStringLiteral("Opposing train");
		case AreaCapacityNoteKind::InvalidReference: return QStringLiteral("Invalid reference");
		case AreaCapacityNoteKind::FewerThanTwoTrains: return QStringLiteral("Fewer than two trains");
		case AreaCapacityNoteKind::NoSharedResource: return QStringLiteral("No shared resource");
		case AreaCapacityNoteKind::ChainConflict: return QStringLiteral("Conflict after compression");
		case AreaCapacityNoteKind::UnselectedTraffic: return QStringLiteral("Unselected traffic");
	}
	return QString();
}

QString areaCapacityStatusText(const AreaCapacityNote& note, bool shortIds) {
	const QString train = QString::fromStdString(note.trainId);
	const QString other = QString::fromStdString(note.otherTrainId);
	const QString resource = shortIds ? resourceText(note.resourceId) : QString::fromStdString(note.resourceId);
	switch (note.kind) {
		case AreaCapacityNoteKind::EmptyScope:
			return QStringLiteral("The area has no resource, so nothing is computed.");
		case AreaCapacityNoteKind::InvalidPeriod:
			return QStringLiteral("The period is not finite or not above 0 s, so no percentage is computed.");
		case AreaCapacityNoteKind::DuplicateIdentity:
			return QString("Train %1 appears more than once. Only the first entry is used.").arg(train);
		case AreaCapacityNoteKind::NoSelectedTraffic:
			return QStringLiteral("No selected train has an occupation in the area, so there is no chain.");
		case AreaCapacityNoteKind::IncompleteOccupation:
			return QString("Train %1 has an incomplete occupation of resource %2. It is not used.").arg(train, resource);
		case AreaCapacityNoteKind::RepeatedResource:
			return QString("Train %1 occupies resource %2 more than once. Each occupation counts in the sum and overlapping time counts once in the union.")
				.arg(train, resource);
		case AreaCapacityNoteKind::OpposingTrain:
			return QString("Train %1 runs in the other direction. It counts in the resource rows and is left out of the chain.").arg(train);
		case AreaCapacityNoteKind::InvalidReference:
			return QString("Train %1 has no valid reference time at the first resource, so the chain cannot be analysed.").arg(train);
		case AreaCapacityNoteKind::FewerThanTwoTrains:
			return QStringLiteral("The chain has one train. A chain needs at least two.");
		case AreaCapacityNoteKind::NoSharedResource:
			return QString("Trains %1 and %2 follow each other in the chain and share no resource.").arg(train, other);
		case AreaCapacityNoteKind::ChainConflict:
			return QStringLiteral("The compressed occupations of the chain overlap, so the chain is not conflict free.");
		case AreaCapacityNoteKind::UnselectedTraffic:
			return QString("Train %1 uses the area and is not selected. The figures leave it out.").arg(train);
	}
	return QString();
}

std::string areaCapacityCsv(const AreaCapacityResult& result, const QString& areaLabel) {
	if (result.resources.empty())
		return std::string();
	enum Column : std::size_t {
		kRecordType,
		kArea,
		kTraffic,
		kPeriod,
		kResource,
		kTrains,
		kOccupations,
		kOccupationSeconds,
		kUnionSeconds,
		kRate,
		kGoverning,
		kLeader,
		kFollower,
		kScheduledHeadway,
		kMinimumHeadway,
		kBuffer,
		kEvidence,
		kCycleTime,
		kCyclePercentage,
		kEnvelope,
		kEnvelopePercentage,
		kStatus,
		kTrain,
		kOtherTrain,
		kNotes,
		kColumnCount
	};
	const char* const names[] = {"Record type", "Area", "Traffic", "Period[s]", "Resource", "Trains", "Occupations",
		"Occupation[s]", "Union[s]", "Rate [% of period]", "Governing", "Leader", "Follower", "Scheduled headway[s]",
		"Minimum headway[s]", "Buffer[s]", "Governing resource evidence", "Cycle time[s]", "Cycle / period [%]", "Legacy envelope[s]",
		"Legacy envelope / period [%]", "Status", "Train", "Other train", "Notes"};
	static_assert(std::size(names) == kColumnCount, "one header name for each column");
	const std::vector<std::string> header(std::begin(names), std::end(names));
	std::vector<std::vector<std::string>> rows;
	const auto newRow = [&](const char* type) {
		std::vector<std::string> row(kColumnCount);
		row[kRecordType] = type;
		row[kArea] = areaLabel.toStdString();
		row[kTraffic] = trafficName(result).toStdString();
		return row;
	};

	std::vector<std::string> summary = newRow("summary");
	summary[kPeriod] = csvField(result.periodSeconds);
	summary[kEnvelope] = csvField(result.envelopeSeconds);
	summary[kEnvelopePercentage] = csvField(result.envelopePercentage);
	summary[kLeader] = result.chain.firstIdentity;
	summary[kFollower] = result.chain.cycleEndIdentity;
	if (chainHasCycle(result.chain)) {
		summary[kCycleTime] = csv::formatDouble(result.chain.cycleTime);
		summary[kCyclePercentage] = csvField(result.chain.cyclePercentage);
	}
	rows.push_back(std::move(summary));

	for (const AreaCapacityResource& resource : result.resources) {
		std::vector<std::string> row = newRow("resource");
		row[kPeriod] = csvField(result.periodSeconds);
		row[kResource] = resource.resourceId;
		row[kTrains] = std::to_string(resource.trainCount);
		row[kOccupations] = std::to_string(resource.occupationCount);
		row[kOccupationSeconds] = csv::formatDouble(resource.occupationSeconds);
		row[kUnionSeconds] = csv::formatDouble(resource.unionSeconds);
		row[kRate] = csvField(resource.occupationPercentage);
		row[kGoverning] = resource.governing ? "yes" : "no";
		rows.push_back(std::move(row));
	}

	for (const CapacityPairRow& pair : result.chain.pairs) {
		std::vector<std::string> row = newRow("pair");
		row[kLeader] = pair.leaderIdentity;
		row[kFollower] = pair.followerIdentity;
		if (pair.hasSharedConstraint) {
			row[kScheduledHeadway] = csv::formatDouble(pair.scheduledHeadway);
			row[kMinimumHeadway] = csv::formatDouble(pair.minimumHeadway);
			row[kBuffer] = csv::formatDouble(pair.buffer);
			row[kEvidence] = capacityEvidenceText(pair.governingEvidence);
		}
		rows.push_back(std::move(row));
	}

	for (const AreaCapacityNote& note : result.notes) {
		std::vector<std::string> row = newRow("note");
		row[kStatus] = areaCapacityStatusName(note.kind).toStdString();
		row[kTrain] = note.trainId;
		row[kOtherTrain] = note.otherTrainId;
		row[kResource] = note.resourceId;
		row[kNotes] = areaCapacityStatusText(note).toStdString();
		rows.push_back(std::move(row));
	}
	return csv::makeDocument(header, rows);
}

AreaCapacityScopeDialog::AreaCapacityScopeDialog(const CompletedRunData& data, QWidget* parent)
	: QDialog(parent),
	  m_data(data) {
	setWindowTitle("Capacity by area");
	auto* body = new QWidget;
	auto* layout = new QVBoxLayout(body);
	auto* form = new QFormLayout();
	m_route = new ChoiceComboBox(body);
	m_firstBlock = new ChoiceComboBox(body);
	m_lastBlock = new ChoiceComboBox(body);
	m_secondRoute = new ChoiceComboBox(body);
	m_secondFirstBlock = new ChoiceComboBox(body);
	m_secondLastBlock = new ChoiceComboBox(body);
	m_route->setObjectName("areaCapacityRoute");
	m_firstBlock->setObjectName("areaCapacityFirstBlock");
	m_lastBlock->setObjectName("areaCapacityLastBlock");
	m_secondRoute->setObjectName("areaCapacitySecondRoute");
	m_secondFirstBlock->setObjectName("areaCapacitySecondFirstBlock");
	m_secondLastBlock->setObjectName("areaCapacitySecondLastBlock");
	m_secondRoute->addItem("No second route", -1);
	for (std::size_t index = 0; index < data.routes.size(); ++index) {
		m_route->addItem(routeText(data.routes[index]), static_cast<int>(index));
		m_secondRoute->addItem(routeText(data.routes[index]), static_cast<int>(index));
	}
	m_period = new QDoubleSpinBox(body);
	m_period->setObjectName("areaCapacityPeriod");
	m_period->setRange(0.001, std::numeric_limits<double>::max());
	m_period->setDecimals(3);
	m_period->setSingleStep(60.0);
	m_period->setSuffix(" s");
	m_period->setValue(3600.0);
	m_cycleEnd = new ChoiceComboBox(body);
	m_cycleEnd->setObjectName("areaCapacityCycleEnd");
	m_occurrences = new QListWidget(body);
	m_occurrences->setObjectName("areaCapacityOccurrences");
	m_occurrences->setSelectionMode(QAbstractItemView::SingleSelection);
	m_occurrences->setDragDropMode(QAbstractItemView::InternalMove);
	m_occurrences->setDefaultDropAction(Qt::MoveAction);
	m_occurrences->setMinimumHeight(180);
	form->addRow("Route / corridor:", m_route);
	form->addRow("First block:", m_firstBlock);
	form->addRow("Last block:", m_lastBlock);
	form->addRow("Second route:", m_secondRoute);
	form->addRow("Second route, first block:", m_secondFirstBlock);
	form->addRow("Second route, last block:", m_secondLastBlock);
	form->addRow("Analysis period:", m_period);
	form->addRow("Cycle-closing train:", m_cycleEnd);
	layout->addLayout(form);
	auto* note = new QLabel(body);
	DialogLayout::setHelp(note, "Checked trains are analysed in list order. Drag a row to change the order.");
	layout->addWidget(note);
	layout->addWidget(new QLabel("Trains in the area:", body));
	layout->addWidget(m_occurrences);
	m_status = new QLabel(body);
	m_status->setObjectName("areaCapacityScopeStatus");
	layout->addWidget(m_status);

	m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	connect(m_route, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
		refillBlocks(m_route, m_firstBlock, m_lastBlock);
		refillOccurrences();
	});
	connect(m_secondRoute, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
		refillBlocks(m_secondRoute, m_secondFirstBlock, m_secondLastBlock);
		refillOccurrences();
	});
	for (QComboBox* combo : {m_firstBlock, m_lastBlock, m_secondFirstBlock, m_secondLastBlock})
		connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { refillOccurrences(); });
	connect(m_cycleEnd, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { validate(); });
	connect(m_occurrences, &QListWidget::itemChanged, this, [this](QListWidgetItem*) { validate(); });
	connect(m_occurrences->model(), &QAbstractItemModel::rowsMoved, this, [this]() { validate(); });

	refillBlocks(m_route, m_firstBlock, m_lastBlock);
	refillBlocks(m_secondRoute, m_secondFirstBlock, m_secondLastBlock);
	refillOccurrences();
	DialogLayout::install(*this, "Capacity by area", QString(), body, m_buttons);
}

void AreaCapacityScopeDialog::refillBlocks(QComboBox* routes, QComboBox* first, QComboBox* last) {
	const QSignalBlocker blockFirst(first);
	const QSignalBlocker blockLast(last);
	first->clear();
	last->clear();
	const int routeIndex = routes->count() > 0 ? routes->currentData().toInt() : -1;
	first->setEnabled(knownRoute(m_data, routeIndex));
	last->setEnabled(knownRoute(m_data, routeIndex));
	if (!knownRoute(m_data, routeIndex))
		return;
	const std::vector<BlockSectionInput>& blocks = m_data.routes[static_cast<std::size_t>(routeIndex)].blocks;
	for (std::size_t index = 0; index < blocks.size(); ++index) {
		const QString text = QString("%1: %2").arg(index + 1).arg(QString::fromStdString(readableSectionLabel(blocks[index].id, m_data.sectionLabels)));
		for (QComboBox* combo : {first, last}) {
			combo->addItem(text);
			combo->setItemData(combo->count() - 1, QString::fromStdString(shortBlockId(blocks[index].id)), Qt::ToolTipRole);
		}
	}
	if (last->count() > 0)
		last->setCurrentIndex(last->count() - 1);
}

AreaCapacityScope AreaCapacityScopeDialog::scope() const {
	AreaCapacityScope chosen;
	chosen.routeIndex = m_route->count() > 0 ? m_route->currentData().toInt() : -1;
	chosen.blockIds = blockRange(m_data, chosen.routeIndex, m_firstBlock->currentIndex(), m_lastBlock->currentIndex());
	chosen.sectionIndices = blockRangeIndices(m_data, chosen.routeIndex, m_firstBlock->currentIndex(), m_lastBlock->currentIndex());
	chosen.secondRouteIndex = m_secondRoute->currentData().toInt();
	chosen.secondBlockIds = blockRange(m_data, chosen.secondRouteIndex, m_secondFirstBlock->currentIndex(), m_secondLastBlock->currentIndex());
	chosen.secondSectionIndices = blockRangeIndices(m_data, chosen.secondRouteIndex, m_secondFirstBlock->currentIndex(),
		m_secondLastBlock->currentIndex());
	chosen.periodSeconds = m_period->value();
	for (int row = 0; row < m_occurrences->count(); ++row) {
		const QListWidgetItem* item = m_occurrences->item(row);
		if (item->checkState() == Qt::Checked)
			chosen.occurrenceIds.push_back(item->data(Qt::UserRole).toString().toStdString());
	}
	chosen.cycleEndOccurrenceId = m_cycleEnd->currentData().toString().toStdString();
	return chosen;
}

void AreaCapacityScopeDialog::refillOccurrences() {
	const AreaCapacityScope area = scope();
	const std::vector<std::string> resources = areaResources(area);
	const CapacityAnalysisScope reference = entryScope(area);
	struct Candidate {
		CapacityAnalysisTrain input;
		double key = 0.0;
	};
	std::vector<Candidate> candidates;
	for (const CompletedTrain& train : m_data.trains) {
		if (!touchesArea(train, resources))
			continue;
		Candidate candidate;
		candidate.input = capacityInputFrom(train, reference);
		candidate.key = referenceKey(candidate.input);
		candidates.push_back(std::move(candidate));
	}
	std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.key < b.key; });

	const QSignalBlocker blockOccurrences(m_occurrences);
	const QSignalBlocker blockCycleEnd(m_cycleEnd);
	m_occurrences->clear();
	m_cycleEnd->clear();
	m_cycleEnd->addItem("No cycle-closing train", QString());
	for (const Candidate& candidate : candidates) {
		const QString identity = QString::fromStdString(candidate.input.runtimeId);
		const QString code = QString::fromStdString(candidate.input.operatingCode);
		const QString name = code.isEmpty() || code == identity ? identity : QString("%1 (%2)").arg(identity, code);
		const QString referenceTime = capacityReferenceText(candidate.input.referenceSource, candidate.input.referenceLabel);
		auto* item = new QListWidgetItem(referenceTime.isEmpty() ? name + ", no reference time" : name, m_occurrences);
		item->setData(Qt::UserRole, identity);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled);
		item->setCheckState(Qt::Checked);
		if (!referenceTime.isEmpty())
			item->setToolTip("Reference time: " + referenceTime);
		m_cycleEnd->addItem(name, identity);
	}
	validate();
}

void AreaCapacityScopeDialog::validate() {
	QString error;
	QString warning;
	const AreaCapacityScope current = scope();
	if (m_data.routes.empty()) {
		error = QStringLiteral("The run has no route.");
	} else if (!current.cycleEndOccurrenceId.empty()) {
		const auto position = std::find(current.occurrenceIds.begin(), current.occurrenceIds.end(), current.cycleEndOccurrenceId);
		if (position == current.occurrenceIds.end() || position == current.occurrenceIds.begin())
			error = QStringLiteral("The cycle-closing train must be a checked row below the first.");
		else if (runsReversed(m_data, *position) != runsReversed(m_data, current.occurrenceIds.front()))
			warning = QStringLiteral("The cycle-closing train runs in the other direction and is left out of the chain.");
	}
	if (error.isEmpty() && current.occurrenceIds.empty())
		warning = QStringLiteral("No train is checked.");
	DialogLayout::setStatus(m_status, error.isEmpty() ? DialogLayout::Severity::Warning : DialogLayout::Severity::Error,
		error.isEmpty() ? warning : error);
	m_buttons->button(QDialogButtonBox::Ok)->setEnabled(error.isEmpty());
}

AreaCapacityWindow::AreaCapacityWindow(AreaCapacityResult result, const QString& areaLabel, RunProvenance provenance, QWidget* parent,
	const QString& areaText, const SceneSectionLabels& labels)
	: QDialog(parent),
	  m_result(std::move(result)),
	  m_areaLabel(areaLabel),
	  m_provenance(std::move(provenance)) {
	setModal(false);
	setProperty("manualSection", QStringLiteral("capacity-by-area"));
	setWindowTitle("Capacity by area");
	auto* tabs = new QTabWidget;
	tabs->setObjectName("areaCapacityTabs");
	tabs->addTab(resourcesTab(m_result, labels, tabs), "Resources");
	tabs->addTab(chainTab(m_result, labels, tabs), "Chain");
	const std::vector<QStringList> excluded = noteRows(m_result, true);
	tabs->addTab(notesTab(excluded, noteResources(m_result, true), "areaCapacityExcluded", labels, tabs),
		countedTitle("Excluded traffic", excluded.size()));
	tabs->addTab(notesTab(noteRows(m_result, false), noteResources(m_result, false), "areaCapacityNotes", labels, tabs),
		countedTitle("Notes", m_result.notes.size()));

	// The tabs at the left, with the run, the area and the traffic in the header. The statuses lie above the tabs, with the
	// look of a warning when there is one. The panel holds the figures and the export.
	auto* statuses = new QLabel;
	statuses->setObjectName("areaCapacityStatuses");
	auto* frame = new ResultFrame(resultTableArea({statuses, tabs}), "areaCapacity");
	// Once the label has a parent, so that showing it opens no window of its own.
	DialogLayout::setStatus(statuses, m_result.notes.empty() ? DialogLayout::Severity::Neutral : DialogLayout::Severity::Warning,
		statusLine(m_result));
	ResultPanel* panel = frame->panel();
	panel->readout()->hide();
	auto* figures = new QWidget(frame);
	auto* figureColumn = new QVBoxLayout(figures);
	figureColumn->setContentsMargins(0, 0, 0, 0);
	figureColumn->setSpacing(8);
	auto* governing = new ResultFigure("areaCapacityGoverning", figures);
	auto* cycle = new ResultFigure("areaCapacityCycle", figures);
	auto* share = new ResultFigure("areaCapacityShare", figures);
	fillFigures(m_result, labels, governing, cycle, share);
	for (ResultFigure* figure : {governing, cycle, share})
		figureColumn->addWidget(figure);
	panel->addSection(QString(), figures);

	auto* exportButton = new QPushButton("CSV...", frame);
	exportButton->setObjectName("areaCapacityExport");
	exportButton->setAccessibleName("Export CSV");
	exportButton->setToolTip("Save the tabs as CSV");
	exportButton->setAutoDefault(false);
	exportButton->setEnabled(!m_result.resources.empty());
	connect(exportButton, &QPushButton::clicked, this, [this]() { exportCsv(); });
	panel->addSection("Export", exportButton);

	auto* closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	connect(closeButtons, &QDialogButtonBox::rejected, this, &QDialog::close);
	// The window title names the view; the header names the run, the area and partial traffic.
	QString context = runContextText(m_provenance) + QStringLiteral(" \u00b7 ") + (areaText.isEmpty() ? m_areaLabel : areaText);
	if (!m_result.complete)
		context += QStringLiteral(" \u00b7 partial traffic");
	frame->setContext(context, m_result.complete ? QString() : QStringLiteral("Rates count only the checked trains"));
	frame->installInDialog(*this, QStringLiteral("Capacity by area"), closeButtons);
	resize(qMin(maximumWidth(), 960), qMin(maximumHeight(), 640));
}

bool AreaCapacityWindow::exportCsvTo(const QString& path) const {
	const std::string content = areaCapacityCsv(m_result, m_areaLabel);
	return !content.empty() && writeRunArtifactWithProvenance(path.toStdString(), "csv", content, m_provenance);
}

void AreaCapacityWindow::exportCsv() {
	// The window comes back in front once the save dialog and its messages close.
	const KeepInFront front(this);
	const auto operation = m_telemetryCapture ? m_telemetryCapture() : telemetry::OperationObservation();
	QString path = QFileDialog::getSaveFileName(this, "Export Data", dialog_folders::suggestedPath("area_capacity.csv"), "CSV File (*.csv)");
	if (path.isEmpty())
		return;
	if (QFileInfo(path).suffix().compare("csv", Qt::CaseInsensitive) != 0)
		path += ".csv";
	const bool written = exportCsvTo(path);
	operation.exportFinished(telemetry::ExportKind::Csv, written, true, telemetry::Error::IoFailure);
	if (!written)
		QMessageBox::warning(this, "Export failed", QString("Could not save the data to:\n%1").arg(path));
}

AreaCapacityWindow* openAreaCapacityWindow(QWidget* parent, const CompletedRunData& data, telemetry::CaptureOperation capture) {
	AreaCapacityScopeDialog chooser(data, parent);
	if (chooser.exec() != QDialog::Accepted)
		return nullptr;
	const AreaCapacityScope scope = chooser.scope();
	auto* window = new AreaCapacityWindow(analyzeAreaCapacity(areaCapacityInputFrom(data, scope)), areaCapacityLabel(data, scope),
		data.provenance, parent, areaCapacityText(data, scope), data.sectionLabels);
	window->setTelemetryCapture(std::move(capture));
	window->setAttribute(Qt::WA_DeleteOnClose);
	window->show();
	return window;
}
