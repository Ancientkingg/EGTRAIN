#include "diagrams/DelayAnalysisWindow.h"

#include "diagrams/ResultFrame.h"
#include "diagrams/RunExports.h"
#include "diagrams/TrainFilterButton.h"
#include "util/CsvWriter.h"
#include "widgets/ChoiceComboBox.h"
#include "widgets/DialogLayout.h"
#include "widgets/EditorLayout.h"

#include <QCursor>
#include <QEvent>
#include <QDialogButtonBox>
#include <QFont>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSet>
#include <QStringList>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolTip>
#include <QVBoxLayout>
#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QBarSeries>
#include <QtCharts/QBarSet>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QValueAxis>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <iterator>
#include <utility>

namespace {

constexpr int kCallsTab = 0;
constexpr int kSummaryTab = 1;
constexpr int kPunctualityTab = 2;
constexpr int kBaselineTab = 3;

// Up to two decimals and no trailing zeros: 168.2, -72.25, 301.
QString numberText(double value) {
	QString text = QString::number(value, 'f', 2);
	while (text.endsWith('0'))
		text.chop(1);
	if (text.endsWith('.'))
		text.chop(1);
	return text == QStringLiteral("-0") ? QStringLiteral("0") : text;
}

QString secondsText(const RunResultValue& value) {
	return value.available ? numberText(value.value) : QStringLiteral("-");
}

// A delay or a difference: positive with a plus sign.
QString signedSecondsText(const RunResultValue& value) {
	if (!value.available)
		return QStringLiteral("-");
	const QString text = numberText(value.value);
	return value.value > 0.0 && text != QStringLiteral("0") ? "+" + text : text;
}

// A figure with its unit, or "-".
QString figureText(const RunResultValue& value) {
	return value.available ? signedSecondsText(value) + " s" : QStringLiteral("-");
}

QString shareText(const DelayShare& share) {
	if (!share.percent.available)
		return QStringLiteral("-");
	return QString("%1% (%2 of %3)").arg(QString::number(share.percent.value, 'f', 1)).arg(share.later).arg(share.denominator);
}

QString thresholdText(const DelayShare& share) {
	return "Later than " + numberText(share.thresholdSeconds) + " s";
}

QString eventName(DelayEventKind kind) {
	return kind == DelayEventKind::Arrival ? QStringLiteral("Arrival") : QStringLiteral("Departure");
}

// "arrival", "departures" and so on, for `count` events.
QString eventsText(DelayEventKind kind, int count) {
	return eventName(kind).toLower() + (count == 1 ? QString() : QStringLiteral("s"));
}

QString yesNo(bool value) {
	return value ? QStringLiteral("Yes") : QStringLiteral("No");
}

QString fromStd(const std::string& value) {
	return QString::fromStdString(value);
}

// The service id, or the train id when the run has no service id, with the occurrence.
QString occurrenceLabel(const std::string& serviceOrTrainId, int occurrence) {
	return QString("%1 #%2").arg(fromStd(serviceOrTrainId)).arg(occurrence);
}

template <typename T>
QString occurrenceOf(const T& value) {
	return occurrenceLabel(value.serviceId.empty() ? value.trainId : value.serviceId, value.occurrence);
}

std::string joinIds(const std::vector<std::string>& ids, const char* separator) {
	std::string result;
	for (const std::string& id : ids) {
		if (!result.empty())
			result += separator;
		result += id;
	}
	return result;
}

// The occurrences and stations that the person left checked.
struct Selection {
	QSet<QString> occurrences;
	QSet<QString> stations;

	template <typename T>
	bool hasOccurrence(const T& value) const {
		return occurrences.contains(occurrenceOf(value));
	}
	bool hasStation(const std::string& stationId) const { return stations.contains(fromStd(stationId)); }
};

Selection selectionOf(const TrainFilterButton& occurrences, const TrainFilterButton& stations) {
	Selection selection;
	for (const QString& label : occurrences.visibleTrainIds())
		selection.occurrences.insert(label);
	for (const QString& stationId : stations.visibleTrainIds())
		selection.stations.insert(stationId);
	return selection;
}

// The filter button lists trains and words its caption, tooltip and search box so. These are the words for something else.
void relabel(TrainFilterButton* button, const QString& noun, int total) {
	button->setText(QString("%1 (%2/%3)").arg(noun).arg(button->visibleTrainIds().size()).arg(total));
	button->setToolTip(QString("Choose which %1 the tabs show").arg(noun.toLower()));
	button->setSearchPlaceholder(QString("Search %1...").arg(noun.toLower()));
}

void setChoices(TrainFilterButton* button, const QStringList& labels) {
	QVector<QPair<QString, QColor>> choices;
	for (const QString& label : labels)
		choices.append({label, QColor()});
	button->setTrains(choices);
}

// A column of a table: its header, and whether it holds numbers, which lie at the right.
struct Column {
	Column(const char* header, bool number = false)
		: header(header), number(number) {}
	Column(QString header, bool number)
		: header(std::move(header)), number(number) {}
	QString header;
	bool number;
};
constexpr bool kNumber = true;
// The role of the header item of a column that says whether the column holds numbers.
constexpr int kNumberRole = Qt::UserRole;

QTableWidget* makeTable(QWidget* parent, const char* name, const QVector<Column>& columns) {
	auto* table = new QTableWidget(0, static_cast<int>(columns.size()), parent);
	table->setObjectName(name);
	for (int column = 0; column < table->columnCount(); ++column) {
		auto* header = new QTableWidgetItem(columns[column].header);
		header->setData(kNumberRole, columns[column].number);
		table->setHorizontalHeaderItem(column, header);
	}
	table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	table->setAlternatingRowColors(true);
	// Tab leaves the table for the next control; the arrow keys move between the cells.
	table->setTabKeyNavigation(false);
	table->horizontalHeader()->setStretchLastSection(true);
	table->verticalHeader()->setVisible(false);
	return table;
}

void fillTable(QTableWidget* table, const std::vector<QStringList>& rows) {
	table->setRowCount(static_cast<int>(rows.size()));
	for (int row = 0; row < table->rowCount(); ++row) {
		for (int column = 0; column < table->columnCount(); ++column) {
			auto* item = new QTableWidgetItem(rows[static_cast<std::size_t>(row)].value(column));
			if (table->horizontalHeaderItem(column)->data(kNumberRole).toBool())
				item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
			table->setItem(row, column, item);
		}
	}
	table->resizeColumnsToContents();
}

// Gives every column whose header ends with `header` the tooltip `tip`.
void setHeaderTip(QTableWidget* table, const QString& header, const QString& tip) {
	for (int column = 0; column < table->columnCount(); ++column)
		if (QTableWidgetItem* item = table->horizontalHeaderItem(column); item && item->text().endsWith(header))
			item->setToolTip(tip);
}

QStringList callRow(const DelayEvent& event) {
	return {fromStd(event.serviceId), QString::number(event.occurrence), fromStd(event.trainId), fromStd(event.operatingCode), fromStd(event.stationId),
		QString::number(event.journeyIndex), QString::number(event.callIndex), eventName(event.kind), secondsText(event.plannedSeconds),
		secondsText(event.simulatedSeconds), signedSecondsText(event.delaySeconds), delayClassText(event.delayClass),
		delayStatusText(event.status)};
}

QStringList summaryRow(const QString& label, const DelayStatistics& statistics) {
	QStringList row = {label, QString::number(statistics.calls), QString::number(statistics.available), QString::number(statistics.noPlan),
		QString::number(statistics.notReached), QString::number(statistics.noPlanAndNotReached), QString::number(statistics.early),
		QString::number(statistics.onTime), QString::number(statistics.late), signedSecondsText(statistics.sumSeconds),
		signedSecondsText(statistics.meanSeconds), signedSecondsText(statistics.minimumSeconds), signedSecondsText(statistics.maximumSeconds)};
	for (const DelayShare& share : statistics.shares)
		row << shareText(share);
	return row;
}

QString evidenceText(const BaselineOccurrence& occurrence) {
	if (!occurrence.hasDirectEvidence)
		return QStringLiteral("None recorded");
	return "Yes: " + fromStd(joinIds(occurrence.incidentIds, ", "));
}

QStringList differenceCells(const DelayDifference& difference) {
	return {secondsText(difference.baselineSeconds), secondsText(difference.scenarioSeconds), signedSecondsText(difference.additionalSeconds),
		delayChangeText(difference.change)};
}

QStringList occurrenceRow(const BaselineOccurrence& occurrence) {
	QStringList row = {fromStd(occurrence.serviceId), QString::number(occurrence.occurrence), fromStd(occurrence.trainId), fromStd(occurrence.operatingCode),
		numberText(occurrence.inputDelaySeconds), fromStd(occurrence.inputDelayStationId), evidenceText(occurrence), fromStd(occurrence.finalStationId)};
	row << differenceCells(occurrence.finalArrival) << signedSecondsText(occurrence.secondarySeconds);
	return row;
}

QStringList baselineCallRow(const BaselineCall& call) {
	QStringList row = {fromStd(call.serviceId), QString::number(call.occurrence), fromStd(call.stationId), QString::number(call.journeyIndex),
		QString::number(call.callIndex), yesNo(call.finalCall)};
	row << differenceCells(call.arrival) << differenceCells(call.departure);
	return row;
}

// "3 trains. At the final arrival: 2 later, 1 same. Added delay +60 s, net +45 s.", with the counts that are not 0.
QString totalsText(const BaselineTotals& totals) {
	if (totals.occurrences == 0)
		return QStringLiteral("No train is selected.");
	QStringList counts;
	const std::pair<int, const char*> parts[] = {
		{totals.later, "later"}, {totals.same, "same"}, {totals.earlier, "earlier"}, {totals.notAvailable, "not available"}};
	for (const auto& part : parts)
		if (part.first > 0)
			counts << QString("%1 %2").arg(part.first).arg(part.second);
	QString line = QString(totals.occurrences == 1 ? "%1 train. At the final arrival: %2. Added delay %3, net %4."
												   : "%1 trains. At the final arrival: %2. Added delay %3, net %4.")
					   .arg(totals.occurrences)
					   .arg(counts.join(", "), figureText(totals.positiveSeconds), figureText(totals.netSeconds));
	if (!totals.complete)
		line += QStringLiteral(" Sums leave out trains without a final arrival.");
	return line;
}

// The available events that are no more than `seconds` late, those of the classes up to that bound, as "87.8% (36 of 41)".
QString noMoreThanText(const DelayHistogram& histogram, double seconds) {
	int count = 0;
	for (const DelayHistogramClass& delayClass : histogram.classes)
		if (delayClass.toSeconds.available && delayClass.toSeconds.value <= seconds)
			count += delayClass.events;
	return QString("%1% (%2 of %3)").arg(QString::number(100.0 * count / histogram.available, 'f', 1)).arg(count).arg(histogram.available);
}

// "41 arrivals of 7 trains; 2 not reached or without a plan. No more than 3 min late: 87.8% (36 of 41); no more than 5 min
// late: 95.1% (39 of 41).", without the events that have no delay when there are none. Needs an available event.
QString histogramCountsText(const DelayHistogram& histogram, int trains, DelayEventKind kind) {
	QString line = QString("%1 %2 of %3 %4")
					   .arg(histogram.available)
					   .arg(eventsText(kind, histogram.available))
					   .arg(trains)
					   .arg(trains == 1 ? QStringLiteral("train") : QStringLiteral("trains"));
	const int unavailable = histogram.noPlan + histogram.notReached + histogram.noPlanAndNotReached;
	if (unavailable > 0)
		line += QString("; %1 not reached or without a plan").arg(unavailable);
	return line
		+ QString(". No more than 3 min late: %1; no more than 5 min late: %2.").arg(noMoreThanText(histogram, 180.0), noMoreThanText(histogram, 300.0));
}

// Whole-number ticks from 0 in steps of 1, 2 or 5 times a power of ten, at most eight steps, with a step of room above the
// highest bar for its label.
void setCountRange(QValueAxis* axis, int highest) {
	const int step = highest > 7 ? static_cast<int>(std::lround(roundTickStep(highest, 7))) : 1;
	const int top = (highest / step + 1) * step;
	axis->setRange(0, top);
	axis->setTickType(QValueAxis::TicksDynamic);
	axis->setTickAnchor(0);
	axis->setTickInterval(step);
}

} // namespace

QString delayStatusText(TimetableValueStatus status) {
	switch (status) {
		case TimetableValueStatus::Available: return QStringLiteral("Available");
		case TimetableValueStatus::NoPlan: return QStringLiteral("No plan");
		case TimetableValueStatus::NotReached: return QStringLiteral("Not reached");
		case TimetableValueStatus::NoPlanAndNotReached: return QStringLiteral("No plan, not reached");
	}
	return QString();
}

QString delayClassText(DelayClass delayClass) {
	switch (delayClass) {
		case DelayClass::Unavailable: return QStringLiteral("Not available");
		case DelayClass::Early: return QStringLiteral("Early");
		case DelayClass::OnTime: return QStringLiteral("On time");
		case DelayClass::Late: return QStringLiteral("Late");
	}
	return QString();
}

QString delayChangeText(DelayChange change) {
	switch (change) {
		case DelayChange::NotAvailable: return QStringLiteral("Not available");
		case DelayChange::Earlier: return QStringLiteral("Earlier");
		case DelayChange::Same: return QStringLiteral("Same");
		case DelayChange::Later: return QStringLiteral("Later");
	}
	return QString();
}

QString delayDiagnosticText(const DelayDiagnostic& diagnostic) {
	const QString occurrence = occurrenceLabel(diagnostic.serviceId, diagnostic.occurrence);
	switch (diagnostic.kind) {
		case DelayDiagnosticKind::ScenarioIdMissing:
			return QStringLiteral("A run has no scenario id, so the two runs cannot be told apart.");
		case DelayDiagnosticKind::ScenarioIdsEqual:
			return QStringLiteral("Both runs have the same scenario id. A run cannot be compared with itself.");
		case DelayDiagnosticKind::SceneRevisionDiffers:
			return QStringLiteral("The case was changed between the two runs. Run both again on the same version of the case.");
		case DelayDiagnosticKind::BaseTimeDiffers:
			return QStringLiteral("The two runs start at different times of day.");
		case DelayDiagnosticKind::DurationDiffers:
			return QStringLiteral("The two runs have different durations.");
		case DelayDiagnosticKind::TimestepDiffers:
			return QStringLiteral("The two runs use different time steps.");
		case DelayDiagnosticKind::BaselineHasIncidents:
			return QStringLiteral("The baseline run has incidents. A baseline must be a run without incidents.");
		case DelayDiagnosticKind::BaselineHasEntranceDelays:
			return QStringLiteral("The baseline run has entrance delays. A baseline must be a run without entrance delays.");
		case DelayDiagnosticKind::OccurrenceOnlyInBaseline:
			return QString("Train %1 is in the baseline run only.").arg(occurrence);
		case DelayDiagnosticKind::OccurrenceOnlyInScenario:
			return QString("Train %1 is in this run only.").arg(occurrence);
		case DelayDiagnosticKind::CallsDiffer:
			return QString("Train %1 has other calls in the two runs: other stations, another journey order or another call number.")
				.arg(occurrence);
	}
	return QString();
}

std::string delayCallsCsv(const std::vector<DelayEvent>& events) {
	if (events.empty())
		return std::string();
	std::vector<std::vector<std::string>> rows;
	for (const DelayEvent& event : events) {
		rows.push_back({event.serviceId, std::to_string(event.occurrence), event.trainId, event.operatingCode, event.stationId,
			std::to_string(event.journeyIndex), std::to_string(event.callIndex), eventName(event.kind).toStdString(), csvValue(event.plannedSeconds),
			csvValue(event.simulatedSeconds), csvValue(event.delaySeconds), delayClassText(event.delayClass).toStdString(),
			delayStatusText(event.status).toStdString()});
	}
	return csv::makeDocument({"Service", "Occurrence", "Train", "Operating code", "Station", "Journey order", "Call", "Event", "Planned[s]",
								 "Simulated[s]", "Delay[s]", "Class", "Status"},
		rows);
}

std::string delaySummaryCsv(const DelaySummary& summary, DelayEventKind kind) {
	if (summary.network.calls == 0)
		return std::string();
	std::vector<std::string> header = {"Record type", "Event", "Station", "Calls", "Available", "No plan", "Not reached", "No plan, not reached",
		"Early", "On time", "Late", "Sum[s]", "Mean[s]", "Minimum[s]", "Maximum[s]"};
	for (const DelayShare& share : summary.network.shares) {
		header.push_back(thresholdText(share).toStdString());
		header.push_back(thresholdText(share).toStdString() + "[%]");
	}
	std::vector<std::vector<std::string>> rows;
	const auto record = [&rows, kind](const char* type, const std::string& stationId, const DelayStatistics& statistics) {
		std::vector<std::string> row = {type, eventName(kind).toStdString(), stationId, std::to_string(statistics.calls),
			std::to_string(statistics.available), std::to_string(statistics.noPlan), std::to_string(statistics.notReached),
			std::to_string(statistics.noPlanAndNotReached), std::to_string(statistics.early), std::to_string(statistics.onTime),
			std::to_string(statistics.late), csvValue(statistics.sumSeconds), csvValue(statistics.meanSeconds),
			csvValue(statistics.minimumSeconds), csvValue(statistics.maximumSeconds)};
		for (const DelayShare& share : statistics.shares) {
			row.push_back(std::to_string(share.later));
			row.push_back(csvValue(share.percent));
		}
		rows.push_back(std::move(row));
	};
	for (const StationDelaySummary& station : summary.stations)
		record("station", station.stationId, station.statistics);
	record("network", std::string(), summary.network);
	return csv::makeDocument(header, rows);
}

std::string delayHistogramCsv(const DelayHistogram& histogram, DelayEventKind kind) {
	if (histogram.available == 0)
		return std::string();
	const std::string event = eventName(kind).toStdString();
	std::vector<std::vector<std::string>> rows;
	for (const DelayHistogramClass& delayClass : histogram.classes) {
		rows.push_back({event, delayClass.label, csvValue(delayClass.fromSeconds), csvValue(delayClass.toSeconds), std::to_string(delayClass.events),
			csvValue(delayClass.percent)});
	}
	const std::pair<TimetableValueStatus, int> unavailable[] = {{TimetableValueStatus::NoPlan, histogram.noPlan},
		{TimetableValueStatus::NotReached, histogram.notReached}, {TimetableValueStatus::NoPlanAndNotReached, histogram.noPlanAndNotReached}};
	for (const auto& status : unavailable)
		rows.push_back({event, delayStatusText(status.first).toStdString(), std::string(), std::string(), std::to_string(status.second), std::string()});
	return csv::makeDocument({"Event", "Class", "From[s]", "To[s]", "Events", "Share[%]"}, rows);
}

std::string delayBaselineCsv(const DelayBaselineComparison& comparison) {
	if (!comparison.compatible || comparison.occurrences.empty())
		return std::string();
	enum Column : std::size_t {
		kRecordType,
		kService,
		kOccurrence,
		kTrain,
		kOperatingCode,
		kStation,
		kJourney,
		kCall,
		kFinalCall,
		kBaselineArrival,
		kScenarioArrival,
		kArrivalDifference,
		kArrivalChange,
		kBaselineDeparture,
		kScenarioDeparture,
		kDepartureDifference,
		kDepartureChange,
		kInputDelay,
		kInputStation,
		kEvidence,
		kIncidentIds,
		kSecondary,
		kOccurrences,
		kLater,
		kSame,
		kEarlier,
		kNotAvailable,
		kPositive,
		kNet,
		kComplete,
		kColumnCount
	};
	const char* const names[] = {"Record type", "Service", "Occurrence", "Train", "Operating code", "Station", "Journey order", "Call",
		"Final call", "Baseline arrival[s]", "Scenario arrival[s]", "Arrival difference[s]", "Arrival change", "Baseline departure[s]",
		"Scenario departure[s]", "Departure difference[s]", "Departure change", "Input delay[s]", "Input delay station",
		"Direct incident evidence", "Incident IDs", "Secondary delay[s]", "Occurrences", "Later", "Same", "Earlier", "Not available",
		"Positive sum[s]", "Net sum[s]", "Complete"};
	static_assert(std::size(names) == kColumnCount, "one header name for each column");
	const std::vector<std::string> header(std::begin(names), std::end(names));
	std::vector<std::vector<std::string>> rows;
	const auto newRow = [](const char* type) {
		std::vector<std::string> row(kColumnCount);
		row[kRecordType] = type;
		return row;
	};
	const auto setArrival = [](std::vector<std::string>& row, const DelayDifference& difference) {
		row[kBaselineArrival] = csvValue(difference.baselineSeconds);
		row[kScenarioArrival] = csvValue(difference.scenarioSeconds);
		row[kArrivalDifference] = csvValue(difference.additionalSeconds);
		row[kArrivalChange] = delayChangeText(difference.change).toStdString();
	};

	for (const BaselineOccurrence& occurrence : comparison.occurrences) {
		std::vector<std::string> row = newRow("occurrence");
		row[kService] = occurrence.serviceId;
		row[kOccurrence] = std::to_string(occurrence.occurrence);
		row[kTrain] = occurrence.trainId;
		row[kOperatingCode] = occurrence.operatingCode;
		row[kStation] = occurrence.finalStationId;
		if (!occurrence.finalStationId.empty()) {
			row[kJourney] = std::to_string(occurrence.finalJourneyIndex);
			row[kCall] = std::to_string(occurrence.finalCallIndex);
		}
		setArrival(row, occurrence.finalArrival);
		row[kInputDelay] = csv::formatDouble(occurrence.inputDelaySeconds);
		row[kInputStation] = occurrence.inputDelayStationId;
		row[kEvidence] = occurrence.hasDirectEvidence ? "yes" : "no";
		row[kIncidentIds] = joinIds(occurrence.incidentIds, ";");
		row[kSecondary] = csvValue(occurrence.secondarySeconds);
		rows.push_back(std::move(row));
	}
	for (const BaselineCall& call : comparison.calls) {
		std::vector<std::string> row = newRow("call");
		row[kService] = call.serviceId;
		row[kOccurrence] = std::to_string(call.occurrence);
		row[kTrain] = call.trainId;
		row[kStation] = call.stationId;
		row[kJourney] = std::to_string(call.journeyIndex);
		row[kCall] = std::to_string(call.callIndex);
		row[kFinalCall] = call.finalCall ? "yes" : "no";
		setArrival(row, call.arrival);
		row[kBaselineDeparture] = csvValue(call.departure.baselineSeconds);
		row[kScenarioDeparture] = csvValue(call.departure.scenarioSeconds);
		row[kDepartureDifference] = csvValue(call.departure.additionalSeconds);
		row[kDepartureChange] = delayChangeText(call.departure.change).toStdString();
		rows.push_back(std::move(row));
	}
	std::vector<std::string> total = newRow("total");
	total[kOccurrences] = std::to_string(comparison.totals.occurrences);
	total[kLater] = std::to_string(comparison.totals.later);
	total[kSame] = std::to_string(comparison.totals.same);
	total[kEarlier] = std::to_string(comparison.totals.earlier);
	total[kNotAvailable] = std::to_string(comparison.totals.notAvailable);
	total[kPositive] = csvValue(comparison.totals.positiveSeconds);
	total[kNet] = csvValue(comparison.totals.netSeconds);
	total[kComplete] = comparison.totals.complete ? "yes" : "no";
	rows.push_back(std::move(total));
	return csv::makeDocument(header, rows);
}

DelayAnalysisWindow::DelayAnalysisWindow(DelayRunSnapshot scenario, std::optional<DelayRunSnapshot> baseline, QWidget* parent)
	: QDialog(parent),
	  m_scenario(std::move(scenario)),
	  m_baseline(std::move(baseline)) {
	setModal(false);
	setProperty("manualSection", QStringLiteral("delay-analysis"));
	setWindowTitle("Delay analysis");
	m_events = buildDelayEvents(m_scenario.timetable, m_scenario.run);
	if (m_baseline)
		m_comparison = compareAgainstBaseline(*m_baseline, m_scenario);

	QStringList occurrenceLabels;
	const auto addOccurrence = [&occurrenceLabels](const QString& label) {
		if (!occurrenceLabels.contains(label))
			occurrenceLabels << label;
	};
	for (const TrainRunResult& train : m_scenario.run.trains)
		addOccurrence(occurrenceOf(train));
	QStringList stationLabels;
	for (const DelayEvent& event : m_events) {
		addOccurrence(occurrenceOf(event));
		if (!stationLabels.contains(fromStd(event.stationId)))
			stationLabels << fromStd(event.stationId);
	}
	m_occurrenceCount = static_cast<int>(occurrenceLabels.size());
	m_stationCount = static_cast<int>(stationLabels.size());

	m_tabs = new QTabWidget;
	m_tabs->setObjectName("delayAnalysisTabs");

	auto* callsPage = new QWidget(m_tabs);
	auto* callsLayout = new QVBoxLayout(callsPage);
	m_callsTable = makeTable(callsPage, "delayCalls",
		{"Service", {"Occurrence", kNumber}, "Train", "Operating code", "Station", {"Journey order", kNumber}, {"Call", kNumber}, "Event",
			{"Planned (s)", kNumber}, {"Simulated (s)", kNumber}, {"Delay (s)", kNumber}, "Class", "Status"});
	setHeaderTip(m_callsTable, "Delay (s)", "Simulated minus planned time\nNegative is early");
	callsLayout->addWidget(m_callsTable, 1);
	m_tabs->addTab(callsPage, "Calls");

	auto* summaryPage = new QWidget(m_tabs);
	auto* summaryLayout = new QVBoxLayout(summaryPage);
	// Every column after the station holds numbers.
	QVector<Column> summaryColumns = {"Station"};
	for (const char* header : {"Calls", "Available", "No plan", "Not reached", "No plan, not reached", "Early", "On time", "Late", "Sum (s)",
			 "Mean (s)", "Minimum (s)", "Maximum (s)"})
		summaryColumns.append({header, kNumber});
	for (const double threshold : defaultDelayThresholdsSeconds()) {
		DelayShare share;
		share.thresholdSeconds = threshold;
		summaryColumns.append({thresholdText(share), kNumber});
	}
	m_summaryTable = makeTable(summaryPage, "delaySummary", summaryColumns);
	for (int column = 0; column < m_summaryTable->columnCount(); ++column)
		if (QTableWidgetItem* item = m_summaryTable->horizontalHeaderItem(column); item && item->text().startsWith("Later than"))
			item->setToolTip("Strictly later\nCounts available events only");
	summaryLayout->addWidget(m_summaryTable, 1);
	m_tabs->addTab(summaryPage, "Summary");

	// A bar for each class of delay, with its count above it, under a line of counts.
	auto* punctualityPage = new QWidget(m_tabs);
	auto* punctualityLayout = new QVBoxLayout(punctualityPage);
	m_histogramNote = new QLabel(punctualityPage);
	m_histogramNote->setObjectName("delayHistogramNote");
	punctualityLayout->addWidget(m_histogramNote);
	m_histogramCounts = new QLabel(punctualityPage);
	m_histogramCounts->setObjectName("delayHistogramCounts");
	m_histogramCounts->setTextFormat(Qt::PlainText);
	m_histogramCounts->setWordWrap(true);
	punctualityLayout->addWidget(m_histogramCounts);
	// The first colour of the train palette (TrainColors.h).
	const QColor barColor(0x2a, 0x78, 0xd6);
	m_histogramBars = new QBarSet(QStringLiteral("Events"));
	m_histogramBars->setColor(barColor);
	m_histogramBars->setBorderColor(barColor);
	m_histogramBars->setLabelColor(palette().color(QPalette::WindowText));
	// "min late" on a line of its own, so that the classes keep their whole names under narrow bars. The axis reads its
	// labels as rich text.
	QStringList classLabels;
	for (const DelayHistogramClass& delayClass : delayHistogram({}).classes) {
		classLabels << fromStd(delayClass.label).replace(" min late", "<br>min late");
		m_histogramBars->append(0);
	}
	auto* bars = new QBarSeries;
	bars->append(m_histogramBars);
	bars->setLabelsVisible(true);
	bars->setLabelsPosition(QAbstractBarSeries::LabelsOutsideEnd);
	bars->setLabelsFormat(QStringLiteral("@value"));
	auto* chart = new QChart;
	chart->addSeries(bars);
	chart->legend()->hide();
	auto* classAxis = new QBarCategoryAxis(chart);
	classAxis->append(classLabels);
	chart->addAxis(classAxis, Qt::AlignBottom);
	bars->attachAxis(classAxis);
	m_histogramAxis = new QValueAxis(chart);
	m_histogramAxis->setTitleText(QStringLiteral("Events"));
	m_histogramAxis->setLabelFormat(QStringLiteral("%.0f"));
	chart->addAxis(m_histogramAxis, Qt::AlignLeft);
	bars->attachAxis(m_histogramAxis);
	styleResultChart(chart, palette());
	classAxis->setGridLineVisible(false);
	m_histogramView = new QChartView(chart, punctualityPage);
	m_histogramView->setObjectName("delayHistogram");
	m_histogramView->setRenderHint(QPainter::Antialiasing);
	m_histogramView->setFrameShape(QFrame::NoFrame);
	// The chart takes no keys, so Tab passes over it.
	m_histogramView->setFocusPolicy(Qt::NoFocus);
	m_histogramView->viewport()->installEventFilter(this);
	punctualityLayout->addWidget(m_histogramView, 1);
	// Keeps the note at the top while the chart is hidden.
	punctualityLayout->addStretch();
	m_tabs->addTab(punctualityPage, "Punctuality");

	auto* baselinePage = new QWidget(m_tabs);
	auto* baselineLayout = new QVBoxLayout(baselinePage);
	m_baselineNote = new QLabel(baselinePage);
	m_baselineNote->setObjectName("delayBaselineNote");
	baselineLayout->addWidget(m_baselineNote);
	m_baselineDiagnostics = makeTable(baselinePage, "delayBaselineDiagnostics", {"Reason"});
	baselineLayout->addWidget(m_baselineDiagnostics, 1);
	m_baselineResults = new QWidget(baselinePage);
	auto* resultsLayout = new QVBoxLayout(m_baselineResults);
	resultsLayout->setContentsMargins(0, 0, 0, 0);
	m_baselineTotals = new QLabel(m_baselineResults);
	m_baselineTotals->setObjectName("delayBaselineTotals");
	m_baselineTotals->setTextFormat(Qt::PlainText);
	m_baselineTotals->setWordWrap(true);
	resultsLayout->addWidget(m_baselineTotals);
	resultsLayout->addWidget(EditorLayout::sectionHeading("Trains", m_baselineResults));
	m_baselineOccurrences = makeTable(m_baselineResults, "delayBaselineOccurrences",
		{"Service", {"Occurrence", kNumber}, "Train", "Operating code", {"Input delay (s)", kNumber}, "Input delay station",
			"Direct incident evidence", "Final station", {"Baseline final arrival (s)", kNumber}, {"Scenario final arrival (s)", kNumber},
			{"Additional delay (s)", kNumber}, "Change", {"Secondary delay (s)", kNumber}});
	setHeaderTip(m_baselineOccurrences, "Input delay (s)", "Entrance delay set in this scenario");
	setHeaderTip(m_baselineOccurrences, "Secondary delay (s)", "Additional delay at the final arrival minus the input delay");
	resultsLayout->addWidget(m_baselineOccurrences, 1);
	resultsLayout->addWidget(EditorLayout::sectionHeading("Calls", m_baselineResults));
	const QVector<Column> differences = {{"Baseline (s)", kNumber}, {"Scenario (s)", kNumber}, {"Additional delay (s)", kNumber}, "Change"};
	QVector<Column> callColumns = {"Service", {"Occurrence", kNumber}, "Station", {"Journey order", kNumber}, {"Call", kNumber}, "Final call"};
	for (const char* event : {"Arrival: ", "Departure: "})
		for (const Column& difference : differences)
			callColumns.append({event + difference.header, difference.number});
	m_baselineCalls = makeTable(m_baselineResults, "delayBaselineCalls", callColumns);
	for (QTableWidget* table : {m_baselineOccurrences, m_baselineCalls})
		setHeaderTip(table, "Additional delay (s)", "This run minus the baseline, same call");
	resultsLayout->addWidget(m_baselineCalls, 1);
	baselineLayout->addWidget(m_baselineResults, 1);
	m_tabs->addTab(baselinePage, "Against baseline");

	// The tabs at the left, with the runs in the header. The panel holds the key figures of the selection, the filters and
	// the export.
	auto* frame = new ResultFrame(resultTableArea({m_tabs}), "delayAnalysis");
	ResultPanel* panel = frame->panel();
	panel->readout()->hide();
	auto* figures = new QWidget(frame);
	auto* figureColumn = new QVBoxLayout(figures);
	figureColumn->setContentsMargins(0, 0, 0, 0);
	figureColumn->setSpacing(8);
	m_sumFigure = new ResultFigure("delayAnalysisSum", figures);
	m_meanFigure = new ResultFigure("delayAnalysisMean", figures);
	m_earlyLateFigure = new ResultFigure("delayAnalysisEarlyLate", figures);
	for (ResultFigure* figure : {m_sumFigure, m_meanFigure, m_earlyLateFigure})
		figureColumn->addWidget(figure);
	panel->addSection(QString(), figures);

	auto* filters = new QWidget(frame);
	auto* filterColumn = new QVBoxLayout(filters);
	filterColumn->setContentsMargins(0, 0, 0, 0);
	filterColumn->setSpacing(8);
	m_occurrences = new TrainFilterButton(filters);
	m_occurrences->setObjectName("delayAnalysisOccurrences");
	setChoices(m_occurrences, occurrenceLabels);
	m_stations = new TrainFilterButton(filters);
	m_stations->setObjectName("delayAnalysisStations");
	setChoices(m_stations, stationLabels);
	m_kind = new ChoiceComboBox(filters);
	m_kind->setObjectName("delayAnalysisEvents");
	m_kind->setAccessibleName("Events");
	m_kind->addItem("Events: Arrivals", static_cast<int>(DelayEventKind::Arrival));
	m_kind->addItem("Events: Departures", static_cast<int>(DelayEventKind::Departure));
	// The choice keeps the width of the panel and shortens its text when the panel is narrower.
	m_kind->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_kind->setMinimumContentsLength(8);
	for (QWidget* filter : std::initializer_list<QWidget*>{m_occurrences, m_stations, m_kind}) {
		filter->setSizePolicy(QSizePolicy::Expanding, filter->sizePolicy().verticalPolicy());
		filterColumn->addWidget(filter);
	}
	panel->addSection(QString(), filters);

	m_exportButton = new QPushButton("CSV...", frame);
	m_exportButton->setObjectName("delayAnalysisExport");
	m_exportButton->setAccessibleName("Export CSV");
	m_exportButton->setToolTip("Save the visible tab as CSV");
	panel->addSection("Export", m_exportButton);

	// No button of the window is a default button, so Enter presses none.
	for (QPushButton* button : frame->findChildren<QPushButton*>())
		button->setAutoDefault(false);
	auto* closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	connect(closeButtons, &QDialogButtonBox::rejected, this, &QDialog::close);
	connect(m_exportButton, &QPushButton::clicked, this, [this]() { exportCsv(); });
	connect(m_occurrences, &TrainFilterButton::selectionChanged, this, [this]() { refresh(); });
	connect(m_stations, &TrainFilterButton::selectionChanged, this, [this]() { refresh(); });
	connect(m_kind, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { refresh(); });
	// The tooltip of a bar gives its class, its count and its share.
	connect(m_histogramBars, &QBarSet::hovered, this, [this](bool entered, int index) {
		m_hoveredBar = entered && index >= 0 && index < static_cast<int>(m_shownHistogram.classes.size()) ? index : -1;
		if (m_hoveredBar < 0)
			QToolTip::hideText();
		else
			QToolTip::showText(QCursor::pos(), histogramToolTip(m_hoveredBar), m_histogramView);
	});
	connect(m_tabs, &QTabWidget::currentChanged, this, [this](int tab) {
		m_kind->setEnabled(tab != kBaselineTab);
		updateExport();
	});

	// The window title is the name of the view; the context names the run and the baseline.
	frame->setContext(runContextText(m_scenario.provenance) + QStringLiteral(" \u00b7 ")
		+ (m_baseline ? "delay baseline " + runContextText(m_baseline->provenance) : QStringLiteral("no delay baseline set")));
	frame->installInDialog(*this, QStringLiteral("Delay analysis"), closeButtons);
	resize(qMin(maximumWidth(), 960), qMin(maximumHeight(), 640));
	refresh();
}

QString DelayAnalysisWindow::histogramToolTip(int index) const {
	const DelayHistogramClass& delayClass = m_shownHistogram.classes[static_cast<std::size_t>(index)];
	return QString("%1\n%2 of %3 %4 (%5%)")
		.arg(fromStd(delayClass.label))
		.arg(delayClass.events)
		.arg(m_shownHistogram.available)
		.arg(eventsText(m_shownKind, m_shownHistogram.available))
		.arg(delayClass.percent.available ? QString::number(delayClass.percent.value, 'f', 1) : QStringLiteral("-"));
}

// The chart view answers a tooltip request from its scene, which has no tooltip and would hide that of the bar under the
// pointer, so the window answers it with the tooltip of that bar.
bool DelayAnalysisWindow::eventFilter(QObject* watched, QEvent* event) {
	if (event->type() == QEvent::ToolTip && m_histogramView && watched == m_histogramView->viewport() && m_hoveredBar >= 0) {
		QToolTip::showText(static_cast<QHelpEvent*>(event)->globalPos(), histogramToolTip(m_hoveredBar), m_histogramView);
		return true;
	}
	return QDialog::eventFilter(watched, event);
}

void DelayAnalysisWindow::refresh() {
	relabel(m_occurrences, "Trains", m_occurrenceCount);
	relabel(m_stations, "Stations", m_stationCount);
	const Selection chosen = selectionOf(*m_occurrences, *m_stations);
	m_shownKind = static_cast<DelayEventKind>(m_kind->currentData().toInt());

	m_shownEvents.clear();
	for (const DelayEvent& event : m_events) {
		if (event.kind == m_shownKind && chosen.hasOccurrence(event) && chosen.hasStation(event.stationId))
			m_shownEvents.push_back(event);
	}
	DelayFilter filter;
	filter.kind = m_shownKind;
	m_shownSummary = summarizeDelays(m_shownEvents, filter);
	m_shownHistogram = delayHistogram(m_shownEvents);

	m_shownBaseline = DelayBaselineComparison();
	m_shownBaseline.compatible = m_comparison.compatible;
	m_shownBaseline.diagnostics = m_comparison.diagnostics;
	for (const BaselineOccurrence& occurrence : m_comparison.occurrences) {
		if (chosen.hasOccurrence(occurrence))
			m_shownBaseline.occurrences.push_back(occurrence);
	}
	for (const BaselineCall& call : m_comparison.calls) {
		if (chosen.hasOccurrence(call) && chosen.hasStation(call.stationId))
			m_shownBaseline.calls.push_back(call);
	}
	m_shownBaseline.totals = baselineTotals(m_shownBaseline.occurrences);

	fillFigures();
	fillCalls();
	fillSummary();
	fillHistogram();
	fillBaseline();
	updateExport();
}

// The figures of all selected stations, as the last row of the Summary tab has them.
void DelayAnalysisWindow::fillFigures() {
	const DelayStatistics& network = m_shownSummary.network;
	const QString counted = QString("Available %1: %2")
								.arg(m_shownKind == DelayEventKind::Arrival ? QStringLiteral("arrivals") : QStringLiteral("departures"))
								.arg(network.available);
	m_sumFigure->setFigure(figureText(network.sumSeconds), QStringLiteral("Sum of delays"), counted);
	m_meanFigure->setFigure(figureText(network.meanSeconds), QStringLiteral("Mean"), counted);
	m_earlyLateFigure->setFigure(QString("%1 / %2").arg(network.early).arg(network.late), QStringLiteral("Early / late"),
		QString("Early: %1\nOn time: %2\nLate: %3").arg(network.early).arg(network.onTime).arg(network.late));
}

void DelayAnalysisWindow::fillCalls() {
	std::vector<QStringList> rows;
	for (const DelayEvent& event : m_shownEvents)
		rows.push_back(callRow(event));
	fillTable(m_callsTable, rows);
}

void DelayAnalysisWindow::fillSummary() {
	std::vector<QStringList> rows;
	for (const StationDelaySummary& station : m_shownSummary.stations)
		rows.push_back(summaryRow(fromStd(station.stationId), station.statistics));
	rows.push_back(summaryRow("All selected stations", m_shownSummary.network));
	fillTable(m_summaryTable, rows);
}

void DelayAnalysisWindow::fillHistogram() {
	const bool empty = m_shownHistogram.available == 0;
	DialogLayout::setStatus(m_histogramNote, DialogLayout::Severity::Neutral,
		empty ? QString("The selected trains and stations have no %1 with a delay.").arg(eventsText(m_shownKind, 1)) : QString());
	m_histogramCounts->setVisible(!empty);
	m_histogramView->setVisible(!empty);
	QSet<QString> trains;
	for (const DelayEvent& event : m_shownEvents) {
		if (event.status == TimetableValueStatus::Available)
			trains.insert(occurrenceOf(event));
	}
	m_histogramCounts->setText(empty ? QString() : histogramCountsText(m_shownHistogram, static_cast<int>(trains.size()), m_shownKind));
	int highest = 0;
	for (std::size_t index = 0; index < m_shownHistogram.classes.size(); ++index) {
		const int events = m_shownHistogram.classes[index].events;
		m_histogramBars->replace(static_cast<int>(index), events);
		highest = std::max(highest, events);
	}
	setCountRange(m_histogramAxis, highest);
}

void DelayAnalysisWindow::fillBaseline() {
	const bool compatible = m_baseline && m_shownBaseline.compatible;
	m_baselineResults->setVisible(compatible);
	m_baselineDiagnostics->setVisible(m_baseline && !m_shownBaseline.compatible);
	if (!m_baseline) {
		DialogLayout::setStatus(m_baselineNote, DialogLayout::Severity::Neutral,
			"No baseline is set. In Run Results, click Set delay baseline after a run without incidents or entrance delays.");
		return;
	}
	if (!compatible) {
		DialogLayout::setStatus(m_baselineNote, DialogLayout::Severity::Warning, "This run cannot be compared with the baseline:");
		std::vector<QStringList> rows;
		for (const DelayDiagnostic& diagnostic : m_shownBaseline.diagnostics)
			rows.push_back({delayDiagnosticText(diagnostic)});
		fillTable(m_baselineDiagnostics, rows);
		return;
	}
	DialogLayout::setHelp(m_baselineNote, QString());
	m_baselineTotals->setText(totalsText(m_shownBaseline.totals));
	std::vector<QStringList> occurrenceRows;
	for (const BaselineOccurrence& occurrence : m_shownBaseline.occurrences)
		occurrenceRows.push_back(occurrenceRow(occurrence));
	fillTable(m_baselineOccurrences, occurrenceRows);
	std::vector<QStringList> callRows;
	for (const BaselineCall& call : m_shownBaseline.calls)
		callRows.push_back(baselineCallRow(call));
	fillTable(m_baselineCalls, callRows);
}

void DelayAnalysisWindow::updateExport() {
	m_exportButton->setEnabled(!currentCsv().empty());
}

std::string DelayAnalysisWindow::currentCsv() const {
	switch (m_tabs->currentIndex()) {
		case kCallsTab: return delayCallsCsv(m_shownEvents);
		case kSummaryTab: return delaySummaryCsv(m_shownSummary, m_shownKind);
		case kPunctualityTab: return delayHistogramCsv(m_shownHistogram, m_shownKind);
		case kBaselineTab: return delayBaselineCsv(m_shownBaseline);
		default: break;
	}
	return std::string();
}

void DelayAnalysisWindow::showBaselineTab() {
	m_tabs->setCurrentIndex(kBaselineTab);
}

QString DelayAnalysisWindow::suggestedFileName() const {
	switch (m_tabs->currentIndex()) {
		case kPunctualityTab: return QStringLiteral("delay_histogram.csv");
		case kBaselineTab: return QStringLiteral("delay_comparison.csv");
		default: break;
	}
	return QStringLiteral("delay_analysis.csv");
}

bool DelayAnalysisWindow::exportCsvTo(const QString& path) const {
	return writeCsv(path, currentCsv());
}

// The Against baseline tab writes the provenance of both runs, the other tabs that of the run.
bool DelayAnalysisWindow::writeCsv(const QString& path, const std::string& content) const {
	if (content.empty())
		return false;
	if (m_tabs->currentIndex() == kBaselineTab && m_baseline)
		return writeDelayArtifactWithProvenance(path.toStdString(), "csv", content, m_baseline->provenance, m_scenario.provenance);
	return writeRunArtifactWithProvenance(path.toStdString(), "csv", content, m_scenario.provenance);
}

void DelayAnalysisWindow::exportCsv() {
	const auto operation = m_telemetryCapture ? m_telemetryCapture() : telemetry::OperationObservation();
	const auto csv = [this] { return currentCsv(); };
	const auto write = [this](const QString& path, const char*, const std::string& content) { return writeCsv(path, content); };
	saveExportInteractive(this, telemetry::ExportKind::Csv, "Export Data", suggestedFileName(), operation, csv, write);
}
