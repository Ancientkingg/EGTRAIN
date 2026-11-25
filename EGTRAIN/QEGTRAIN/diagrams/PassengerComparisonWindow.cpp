#include "diagrams/PassengerComparisonWindow.h"

#include "diagrams/ResultFrame.h"
#include "diagrams/ResultTable.h"
#include "widgets/EditorLayout.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QVBoxLayout>

#include <string>
#include <vector>

namespace {

// The decimals of a figure in the tables: three for passenger-hours, two for seconds.
int decimalsForUnit(const std::string& unit) {
	return unit == "passenger-hours" ? 3 : 2;
}

// A whole count, with a plus sign above zero when `signedValue`.
QString countText(const RunResultValue& value, bool signedValue) {
	return signedValue ? signedDecimalText(value, 0) : decimalText(value, 0);
}

QString metricCell(const RunResultValue& value, const PassengerMetricRow& row, bool signedValue) {
	if (row.isInteger)
		return countText(value, signedValue);
	const int decimals = decimalsForUnit(row.unit);
	return signedValue ? signedDecimalText(value, decimals) : decimalText(value, decimals);
}

// The name of a figure in the summary table, with its unit when the name does not give it.
QString metricLabel(const PassengerMetricRow& row) {
	const QString label = QString::fromStdString(row.label);
	return row.unit == "s" ? label + QStringLiteral(" (s)") : label;
}

constexpr int kMeanDelayColumns[] = {6, 7, 8};
constexpr int kLostColumns[] = {9, 10, 11};
constexpr int kLostDifferenceColumn = 11;

} // namespace

PassengerComparisonWindow::PassengerComparisonWindow(DelayRunSnapshot baseline, DelayRunSnapshot scenario, QWidget* parent)
	: QDialog(parent),
	  m_baseline(std::move(baseline)),
	  m_scenario(std::move(scenario)),
	  m_comparison(comparePassengersAgainstBaseline(m_baseline, m_scenario)) {
	setModal(false);
	setObjectName(QStringLiteral("passengerComparisonWindow"));
	setProperty("manualSection", QStringLiteral("run-results"));
	setWindowTitle(QStringLiteral("Passenger comparison"));

	auto* content = new QWidget;
	auto* contentLayout = new QVBoxLayout(content);
	contentLayout->setContentsMargins(0, 0, 0, 0);
	contentLayout->setSpacing(8);

	contentLayout->addWidget(EditorLayout::sectionHeading(QStringLiteral("Summary"), content));
	m_summaryTable = makeResultTable(content, "passengerComparisonSummary",
		{"Metric", {"Baseline", kNumberColumn}, {"This run", kNumberColumn}, {"Difference", kNumberColumn}});
	m_summaryTable->horizontalHeaderItem(3)->setToolTip(QStringLiteral("This run minus the baseline"));
	contentLayout->addWidget(m_summaryTable, 1);

	contentLayout->addWidget(EditorLayout::sectionHeading(QStringLiteral("Origin-destination pairs that changed most"), content));
	m_odTable = makeResultTable(content, "passengerComparisonOd",
		{"Origin", "Destination", {"Baseline journeys", kNumberColumn}, {"This run journeys", kNumberColumn},
			{"Baseline not reached", kNumberColumn}, {"This run not reached", kNumberColumn},
			{"Baseline mean delay (s)", kNumberColumn}, {"This run mean delay (s)", kNumberColumn},
			{"Mean delay difference (s)", kNumberColumn}, {"Baseline passenger-hours lost", kNumberColumn},
			{"This run passenger-hours lost", kNumberColumn}, {"Passenger-hours lost difference", kNumberColumn}});
	for (const int column : kMeanDelayColumns)
		m_odTable->horizontalHeaderItem(column)->setToolTip(QStringLiteral("Mean arrival delay of the completed journeys of the pair"));
	for (const int column : kLostColumns)
		m_odTable->horizontalHeaderItem(column)->setToolTip(
			QStringLiteral("Passenger-hours lost by the completed journeys of the pair; journeys not reached add nothing"));
	m_odTable->horizontalHeaderItem(kLostDifferenceColumn)
		->setToolTip(QStringLiteral("This run minus the baseline, for the completed journeys of the pair. The pairs that changed are "
									"ranked by this change, then by the change in journeys not reached, up to %1")
				.arg(static_cast<int>(kMaxComparedOriginDestinations)));
	contentLayout->addWidget(m_odTable, 1);

	auto* frame = new ResultFrame(resultTableArea({content}), QStringLiteral("passengerComparison"));
	ResultPanel* panel = frame->panel();
	panel->readout()->hide();

	// The delays count the completed journeys only, and the heading of their section says so; the captions are short enough for
	// the panel.
	const auto figureColumn = [frame] {
		auto* figures = new QWidget(frame);
		auto* column = new QVBoxLayout(figures);
		column->setContentsMargins(0, 0, 0, 0);
		column->setSpacing(8);
		return figures;
	};
	QWidget* completedFigures = figureColumn();
	QWidget* allFigures = figureColumn();

	const PassengerRunSummary& base = m_comparison.baseline;
	const PassengerRunSummary& cur = m_comparison.scenario;

	auto* lostFigure = new ResultFigure(QStringLiteral("passengerComparisonHoursLost"), completedFigures);
	lostFigure->setFigure(signedDecimalText(cur.passengerHoursLost - base.passengerHoursLost, 3) + QStringLiteral(" h"),
		QStringLiteral("Passenger-hours lost"),
		QStringLiteral("Change in the passenger-hours lost by the completed journeys\nBaseline: %1 h\nThis run: %2 h")
			.arg(decimalText(base.passengerHoursLost, 3), decimalText(cur.passengerHoursLost, 3)));
	completedFigures->layout()->addWidget(lostFigure);

	auto* meanFigure = new ResultFigure(QStringLiteral("passengerComparisonMeanDelay"), completedFigures);
	const auto meanText = [](const PassengerRunSummary& summary) {
		return summary.completedJourneys > 0 ? decimalText(summary.meanDelaySeconds) + QStringLiteral(" s") : QStringLiteral("-");
	};
	const bool bothCompleted = base.completedJourneys > 0 && cur.completedJourneys > 0;
	meanFigure->setFigure(bothCompleted ? signedDecimalText(cur.meanDelaySeconds - base.meanDelaySeconds) + QStringLiteral(" s") : QStringLiteral("-"),
		QStringLiteral("Mean delay"),
		QStringLiteral("Change in the mean arrival delay of the completed journeys\nBaseline: %1\nThis run: %2").arg(meanText(base), meanText(cur)));
	completedFigures->layout()->addWidget(meanFigure);

	auto* notReachedFigure = new ResultFigure(QStringLiteral("passengerComparisonNotReached"), allFigures);
	notReachedFigure->setFigure(signedDecimalText(cur.notReachedJourneys() - base.notReachedJourneys(), 0), QStringLiteral("Journeys not reached"),
		QStringLiteral("Change in the journeys that did not reach their destination\nBaseline: %1 (%2 no route, %3 in progress)\n"
					   "This run: %4 (%5 no route, %6 in progress)")
			.arg(base.notReachedJourneys())
			.arg(base.noRouteJourneys)
			.arg(base.inProgressJourneys)
			.arg(cur.notReachedJourneys())
			.arg(cur.noRouteJourneys)
			.arg(cur.inProgressJourneys));
	allFigures->layout()->addWidget(notReachedFigure);

	panel->addSection(QStringLiteral("Completed journeys"), completedFigures);
	panel->addSection(QStringLiteral("All journeys"), allFigures);

	m_exportButton = new QPushButton(QStringLiteral("CSV..."), frame);
	m_exportButton->setObjectName(QStringLiteral("passengerComparisonExport"));
	m_exportButton->setAccessibleName(QStringLiteral("Export CSV"));
	m_exportButton->setToolTip(QStringLiteral("Save the summary and the changed origin-destination pairs as CSV"));
	panel->addSection(QStringLiteral("Export"), m_exportButton);

	for (QPushButton* button : frame->findChildren<QPushButton*>())
		button->setAutoDefault(false);
	auto* closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	connect(closeButtons, &QDialogButtonBox::rejected, this, &QDialog::close);
	connect(m_exportButton, &QPushButton::clicked, this, [this]() { exportCsv(); });

	frame->setContext(runContextText(m_scenario.provenance) + QStringLiteral(" · baseline ") + runContextText(m_baseline.provenance));
	frame->installInDialog(*this, QStringLiteral("Passenger comparison"), closeButtons);
	resize(qMin(maximumWidth(), 960), qMin(maximumHeight(), 640));

	fillSummaryTable();
	fillOdTable();
}

void PassengerComparisonWindow::fillSummaryTable() {
	std::vector<QStringList> rows;
	rows.reserve(m_comparison.metrics.size());
	for (const PassengerMetricRow& metric : m_comparison.metrics) {
		// A delay is signed in every column; a count only in the difference.
		const bool signedValues = metric.unit == "s";
		rows.push_back({
			metricLabel(metric),
			metricCell(metric.baseline, metric, signedValues),
			metricCell(metric.scenario, metric, signedValues),
			metricCell(metric.difference, metric, true),
		});
	}
	fillResultTable(m_summaryTable, rows);
}

void PassengerComparisonWindow::fillOdTable() {
	std::vector<QStringList> rows;
	rows.reserve(m_comparison.changedPairs.size());
	for (const OriginDestinationChange& od : m_comparison.changedPairs) {
		rows.push_back({
			QString::fromStdString(od.origin),
			QString::fromStdString(od.destination),
			QString::number(od.baselineJourneys),
			QString::number(od.scenarioJourneys),
			QString::number(od.baselineNotReached),
			QString::number(od.scenarioNotReached),
			signedDecimalText(od.baselineMeanDelay),
			signedDecimalText(od.scenarioMeanDelay),
			signedDecimalText(od.meanDelayDifference),
			decimalText(od.baselinePassengerHoursLost, 3),
			decimalText(od.scenarioPassengerHoursLost, 3),
			signedDecimalText(od.passengerHoursLostDifference, 3),
		});
	}
	fillResultTable(m_odTable, rows);
	if (!rows.empty())
		return;
	// No pair changed: one line across the table says so.
	m_odTable->setRowCount(1);
	auto* empty = new QTableWidgetItem(QStringLiteral("No origin-destination pair changed between the two runs"));
	empty->setFlags(Qt::ItemIsEnabled);
	m_odTable->setItem(0, 0, empty);
	m_odTable->setSpan(0, 0, 1, m_odTable->columnCount());
}

bool PassengerComparisonWindow::exportCsvTo(const QString& path) const {
	return writeCsv(path, passengerComparisonCsv(m_comparison)).isEmpty();
}

QString PassengerComparisonWindow::writeCsv(const QString& path, const std::string& content) const {
	if (content.empty())
		return QStringLiteral("There is no data to export.");
	return QString::fromStdString(
		writeDelayArtifactWithProvenance(path.toStdString(), "csv", content, m_baseline.provenance, m_scenario.provenance));
}

void PassengerComparisonWindow::exportCsv() {
	const auto operation = m_telemetryCapture ? m_telemetryCapture() : telemetry::OperationObservation();
	const auto csv = [this] { return passengerComparisonCsv(m_comparison); };
	const auto write = [this](const QString& path, const char*, const std::string& content) { return writeCsv(path, content); };
	saveExportInteractive(this, *m_prompts, telemetry::ExportKind::Csv, QStringLiteral("Export Data"), suggestedFileName(),
		operation, csv, write);
}
