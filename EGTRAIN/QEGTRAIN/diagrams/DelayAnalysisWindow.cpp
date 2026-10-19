#include "diagrams/DelayAnalysisWindow.h"

#include "app/DialogFolders.h"
#include "diagrams/RunExports.h"
#include "diagrams/TrainFilterButton.h"
#include "util/CsvWriter.h"
#include "widgets/ChoiceComboBox.h"
#include "widgets/DialogLayout.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QStringList>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <iterator>

namespace {

constexpr int kCallsTab = 0;
constexpr int kSummaryTab = 1;
constexpr int kBaselineTab = 2;

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

QString runText(const RunProvenance& provenance) {
	const QString caseName = fromStd(provenance.caseName);
	const QString scenario = fromStd(provenance.appliedScenario);
	if (caseName.isEmpty())
		return scenario.isEmpty() ? QStringLiteral("(none)") : scenario;
	return scenario.isEmpty() ? caseName : QStringLiteral("%1 / %2").arg(caseName, scenario);
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
	if (auto* search = button->findChild<QLineEdit*>())
		search->setPlaceholderText(QString("Search %1...").arg(noun.toLower()));
}

void setChoices(TrainFilterButton* button, const QStringList& labels) {
	QVector<QPair<QString, QColor>> choices;
	for (const QString& label : labels)
		choices.append({label, QColor()});
	button->setTrains(choices);
}

QTableWidget* makeTable(QWidget* parent, const char* name, const QStringList& headers) {
	auto* table = new QTableWidget(0, static_cast<int>(headers.size()), parent);
	table->setObjectName(name);
	table->setHorizontalHeaderLabels(headers);
	table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	table->setAlternatingRowColors(true);
	table->horizontalHeader()->setStretchLastSection(true);
	table->verticalHeader()->setVisible(false);
	return table;
}

// The cells of numericColumns are aligned to the right.
void fillTable(QTableWidget* table, const std::vector<QStringList>& rows, const QList<int>& numericColumns = {}) {
	table->setRowCount(static_cast<int>(rows.size()));
	for (int row = 0; row < table->rowCount(); ++row) {
		for (int column = 0; column < table->columnCount(); ++column) {
			auto* item = new QTableWidgetItem(rows[static_cast<std::size_t>(row)].value(column));
			if (numericColumns.contains(column))
				item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
			table->setItem(row, column, item);
		}
	}
	table->resizeColumnsToContents();
}

QLabel* makeHelp(QWidget* parent, const QString& help) {
	auto* label = new QLabel(parent);
	DialogLayout::setHelp(label, help);
	return label;
}

QLabel* makeCaption(QWidget* parent, const QString& caption) {
	auto* label = new QLabel(caption, parent);
	QFont font = label->font();
	font.setBold(true);
	label->setFont(font);
	return label;
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

QString totalsText(const BaselineTotals& totals) {
	if (totals.occurrences == 0)
		return QStringLiteral("No occurrence is selected.");
	QString line = QString("Occurrences: %1. Final arrival later: %2, same: %3, earlier: %4, not available: %5. Positive additional delay: %6. "
						   "Net additional delay: %7.")
					   .arg(totals.occurrences)
					   .arg(totals.later)
					   .arg(totals.same)
					   .arg(totals.earlier)
					   .arg(totals.notAvailable)
					   .arg(figureText(totals.positiveSeconds), figureText(totals.netSeconds));
	if (!totals.complete)
		line += QStringLiteral(" The sums leave out the occurrences that have no final arrival difference.");
	return line;
}

const char* const kCallsHelp =
	"Delay is the simulated time minus the planned time of the authored plan, in seconds since the start of the run. Negative is early, 0 is "
	"on time and positive is late. An entrance delay shows as the delay of the departure that it moves. A missing plan or time gives no "
	"delay. It is shown as - and Status says which is missing.";

const char* const kSummaryHelp =
	"One row for each station and one for all selected stations. Sum, mean, minimum, maximum and the shares count the Available events only, "
	"so a missing event is not counted as 0. Later than a threshold means strictly later: a delay equal to the threshold is not later. "
	"The thresholds are 0 s, 60 s, 180 s and 300 s.";

const char* const kBaselineHelp =
	"The additional delay of a call is its simulated time in this run minus the simulated time of the same call in the baseline run. The "
	"input delay is the entrance delay of this run. It is the primary delay, an input and not a measurement. The secondary delay is the "
	"additional delay at the final arrival minus the input delay, when the delayed stop comes before the final call. It states a "
	"difference and no cause. A delay without recorded direct incident evidence is not shown to be propagated. The station choice applies "
	"to the calls only.";

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
			return QString("Occurrence %1 is in the baseline run only.").arg(occurrence);
		case DelayDiagnosticKind::OccurrenceOnlyInScenario:
			return QString("Occurrence %1 is in this run only.").arg(occurrence);
		case DelayDiagnosticKind::CallsDiffer:
			return QString("Occurrence %1 has other calls in the two runs: other stations, another journey order or another call number.")
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

	auto* body = new QWidget;
	auto* layout = new QVBoxLayout(body);

	m_occurrences = new TrainFilterButton(body);
	m_occurrences->setObjectName("delayAnalysisOccurrences");
	setChoices(m_occurrences, occurrenceLabels);
	m_stations = new TrainFilterButton(body);
	m_stations->setObjectName("delayAnalysisStations");
	setChoices(m_stations, stationLabels);
	m_kind = new ChoiceComboBox(body);
	m_kind->setObjectName("delayAnalysisEvents");
	m_kind->addItem("Arrivals", static_cast<int>(DelayEventKind::Arrival));
	m_kind->addItem("Departures", static_cast<int>(DelayEventKind::Departure));
	m_kind->setToolTip("The events of the Calls and Summary tabs");
	auto* filters = new QHBoxLayout();
	filters->addWidget(m_occurrences);
	filters->addWidget(m_stations);
	filters->addWidget(new QLabel("Events:", body));
	filters->addWidget(m_kind);
	filters->addStretch();
	layout->addLayout(filters);

	m_tabs = new QTabWidget(body);
	m_tabs->setObjectName("delayAnalysisTabs");

	auto* callsPage = new QWidget(m_tabs);
	auto* callsLayout = new QVBoxLayout(callsPage);
	callsLayout->addWidget(makeHelp(callsPage, kCallsHelp));
	m_callsTable = makeTable(callsPage, "delayCalls",
		{"Service", "Occurrence", "Train", "Operating code", "Station", "Journey order", "Call", "Event", "Planned [s]", "Simulated [s]",
			"Delay [s]", "Class", "Status"});
	callsLayout->addWidget(m_callsTable, 1);
	m_tabs->addTab(callsPage, "Calls");

	auto* summaryPage = new QWidget(m_tabs);
	auto* summaryLayout = new QVBoxLayout(summaryPage);
	summaryLayout->addWidget(makeHelp(summaryPage, kSummaryHelp));
	QStringList summaryHeaders = {"Station", "Calls", "Available", "No plan", "Not reached", "No plan, not reached", "Early", "On time", "Late",
		"Sum [s]", "Mean [s]", "Minimum [s]", "Maximum [s]"};
	for (const double threshold : defaultDelayThresholdsSeconds()) {
		DelayShare share;
		share.thresholdSeconds = threshold;
		summaryHeaders << thresholdText(share);
	}
	m_summaryTable = makeTable(summaryPage, "delaySummary", summaryHeaders);
	summaryLayout->addWidget(m_summaryTable, 1);
	m_tabs->addTab(summaryPage, "Summary");

	auto* baselinePage = new QWidget(m_tabs);
	auto* baselineLayout = new QVBoxLayout(baselinePage);
	m_baselineNote = new QLabel(baselinePage);
	m_baselineNote->setObjectName("delayBaselineNote");
	baselineLayout->addWidget(m_baselineNote);
	m_baselineDiagnostics = makeTable(baselinePage, "delayBaselineDiagnostics", {"Code", "Meaning"});
	baselineLayout->addWidget(m_baselineDiagnostics, 1);
	m_baselineResults = new QWidget(baselinePage);
	auto* resultsLayout = new QVBoxLayout(m_baselineResults);
	resultsLayout->setContentsMargins(0, 0, 0, 0);
	m_baselineTotals = new QLabel(m_baselineResults);
	m_baselineTotals->setObjectName("delayBaselineTotals");
	m_baselineTotals->setTextFormat(Qt::PlainText);
	m_baselineTotals->setWordWrap(true);
	resultsLayout->addWidget(m_baselineTotals);
	resultsLayout->addWidget(makeCaption(m_baselineResults, "Occurrences"));
	m_baselineOccurrences = makeTable(m_baselineResults, "delayBaselineOccurrences",
		{"Service", "Occurrence", "Train", "Operating code", "Input delay (primary) [s]", "Input delay at", "Direct incident evidence",
			"Final station", "Baseline final arrival [s]", "Scenario final arrival [s]", "Additional delay [s]", "Change", "Secondary delay [s]"});
	resultsLayout->addWidget(m_baselineOccurrences, 1);
	resultsLayout->addWidget(makeCaption(m_baselineResults, "Calls"));
	const QStringList differenceHeaders = {"Baseline [s]", "Scenario [s]", "Additional delay [s]", "Change"};
	QStringList callHeaders = {"Service", "Occurrence", "Station", "Journey order", "Call", "Final call"};
	for (const QString& header : differenceHeaders)
		callHeaders << "Arrival: " + header;
	for (const QString& header : differenceHeaders)
		callHeaders << "Departure: " + header;
	m_baselineCalls = makeTable(m_baselineResults, "delayBaselineCalls", callHeaders);
	resultsLayout->addWidget(m_baselineCalls, 1);
	baselineLayout->addWidget(m_baselineResults, 1);
	m_tabs->addTab(baselinePage, "Against baseline");
	layout->addWidget(m_tabs, 1);

	m_exportButton = new QPushButton("Export CSV...", body);
	m_exportButton->setObjectName("delayAnalysisExport");
	m_exportButton->setToolTip("Write the visible tab to a CSV file");
	m_exportButton->setAutoDefault(false);
	auto* buttonRow = new QHBoxLayout();
	buttonRow->addWidget(m_exportButton);
	buttonRow->addStretch();
	layout->addLayout(buttonRow);

	auto* closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	connect(closeButtons, &QDialogButtonBox::rejected, this, &QDialog::close);
	connect(m_exportButton, &QPushButton::clicked, this, [this]() { exportCsv(); });
	connect(m_occurrences, &TrainFilterButton::selectionChanged, this, [this]() { refresh(); });
	connect(m_stations, &TrainFilterButton::selectionChanged, this, [this]() { refresh(); });
	connect(m_kind, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { refresh(); });
	connect(m_tabs, &QTabWidget::currentChanged, this, [this](int tab) {
		m_kind->setEnabled(tab != kBaselineTab);
		updateExport();
	});

	const QString baselineContext = m_baseline ? runText(m_baseline->provenance) : QStringLiteral("none set");
	const QString heading = m_scenario.scenarioId.empty() ? QStringLiteral("Delay analysis") : "Delay analysis: " + fromStd(m_scenario.scenarioId);
	DialogLayout::install(*this, heading,
		QString("Run: %1\nBaseline: %2").arg(runText(m_scenario.provenance), baselineContext), body, closeButtons);
	resize(qMin(maximumWidth(), 1100), qMin(maximumHeight(), 640));
	refresh();
}

void DelayAnalysisWindow::refresh() {
	relabel(m_occurrences, "Occurrences", m_occurrenceCount);
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

	fillCalls();
	fillSummary();
	fillBaseline();
	updateExport();
}

void DelayAnalysisWindow::fillCalls() {
	std::vector<QStringList> rows;
	for (const DelayEvent& event : m_shownEvents)
		rows.push_back(callRow(event));
	fillTable(m_callsTable, rows, {1, 5, 6, 8, 9, 10});
}

void DelayAnalysisWindow::fillSummary() {
	std::vector<QStringList> rows;
	for (const StationDelaySummary& station : m_shownSummary.stations)
		rows.push_back(summaryRow(fromStd(station.stationId), station.statistics));
	rows.push_back(summaryRow("All selected stations", m_shownSummary.network));
	QList<int> numericColumns;
	for (int column = 1; column < m_summaryTable->columnCount(); ++column)
		numericColumns << column;
	fillTable(m_summaryTable, rows, numericColumns);
}

void DelayAnalysisWindow::fillBaseline() {
	const bool compatible = m_baseline && m_shownBaseline.compatible;
	m_baselineResults->setVisible(compatible);
	m_baselineDiagnostics->setVisible(m_baseline && !m_shownBaseline.compatible);
	if (!m_baseline) {
		DialogLayout::setStatus(m_baselineNote, DialogLayout::Severity::Neutral,
			"No delay baseline is set. Set a baseline from a completed run with no incidents and no entrance delays, then compare a scenario run with it.");
		return;
	}
	if (!compatible) {
		DialogLayout::setStatus(m_baselineNote, DialogLayout::Severity::Warning,
			"The baseline cannot be compared with this run, so there is no difference to show. Each row below is one reason.");
		std::vector<QStringList> rows;
		for (const DelayDiagnostic& diagnostic : m_shownBaseline.diagnostics)
			rows.push_back({QString::fromLatin1(delayDiagnosticCode(diagnostic.kind)), delayDiagnosticText(diagnostic)});
		fillTable(m_baselineDiagnostics, rows);
		return;
	}
	DialogLayout::setHelp(m_baselineNote, kBaselineHelp);
	m_baselineTotals->setText(totalsText(m_shownBaseline.totals));
	std::vector<QStringList> occurrenceRows;
	for (const BaselineOccurrence& occurrence : m_shownBaseline.occurrences)
		occurrenceRows.push_back(occurrenceRow(occurrence));
	fillTable(m_baselineOccurrences, occurrenceRows, {1, 4, 8, 9, 10, 12});
	std::vector<QStringList> callRows;
	for (const BaselineCall& call : m_shownBaseline.calls)
		callRows.push_back(baselineCallRow(call));
	fillTable(m_baselineCalls, callRows, {1, 3, 4, 6, 7, 8, 10, 11, 12});
}

void DelayAnalysisWindow::updateExport() {
	m_exportButton->setEnabled(!currentCsv().empty());
}

std::string DelayAnalysisWindow::currentCsv() const {
	switch (m_tabs->currentIndex()) {
		case kCallsTab: return delayCallsCsv(m_shownEvents);
		case kSummaryTab: return delaySummaryCsv(m_shownSummary, m_shownKind);
		case kBaselineTab: return delayBaselineCsv(m_shownBaseline);
		default: break;
	}
	return std::string();
}

void DelayAnalysisWindow::showBaselineTab() {
	m_tabs->setCurrentIndex(kBaselineTab);
}

QString DelayAnalysisWindow::suggestedFileName() const {
	return m_tabs->currentIndex() == kBaselineTab ? QStringLiteral("delay_comparison.csv") : QStringLiteral("delay_analysis.csv");
}

bool DelayAnalysisWindow::exportCsvTo(const QString& path) const {
	const std::string content = currentCsv();
	if (content.empty())
		return false;
	if (m_tabs->currentIndex() == kBaselineTab && m_baseline)
		return writeDelayArtifactWithProvenance(path.toStdString(), "csv", content, m_baseline->provenance, m_scenario.provenance);
	return writeRunArtifactWithProvenance(path.toStdString(), "csv", content, m_scenario.provenance);
}

void DelayAnalysisWindow::exportCsv() {
	const auto operation = m_telemetryCapture ? m_telemetryCapture() : telemetry::OperationObservation();
	QString path = QFileDialog::getSaveFileName(this, "Export Data", dialog_folders::suggestedPath(suggestedFileName()), "CSV File (*.csv)");
	if (path.isEmpty())
		return;
	if (QFileInfo(path).suffix().compare("csv", Qt::CaseInsensitive) != 0)
		path += ".csv";
	const bool written = exportCsvTo(path);
	operation.exportFinished(telemetry::ExportKind::Csv, written, true, telemetry::Error::IoFailure);
	if (!written)
		QMessageBox::warning(this, "Export failed", QString("Could not export the data and provenance to:\n%1").arg(path));
}
