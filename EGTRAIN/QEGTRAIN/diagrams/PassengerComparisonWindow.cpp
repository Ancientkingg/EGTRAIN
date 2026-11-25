#include "diagrams/PassengerComparisonWindow.h"

#include "diagrams/ResultFrame.h"
#include "widgets/EditorLayout.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QVector>

#include <cmath>
#include <string>
#include <vector>

namespace {

// Up to `decimals` decimal places with trailing zeros stripped.
QString formatDecimal(double value, int decimals) {
	QString text = QString::number(value, 'f', decimals);
	while (text.endsWith('0'))
		text.chop(1);
	if (text.endsWith('.'))
		text.chop(1);
	return text == QStringLiteral("-0") ? QStringLiteral("0") : text;
}

QString formatSignedDecimal(double value, int decimals) {
	const QString text = formatDecimal(value, decimals);
	return value > 0.0 && text != QStringLiteral("0") ? "+" + text : text;
}

QString formatInteger(double value) {
	return QString::number(static_cast<long long>(std::llround(value)));
}

QString formatSignedInteger(double value) {
	const long long rounded = static_cast<long long>(std::llround(value));
	return rounded > 0 ? "+" + QString::number(rounded) : QString::number(rounded);
}

QString formatMetricCell(const RunResultValue& value, bool isInteger, int decimals, bool signedValue) {
	if (!value.available)
		return QStringLiteral("-");
	if (isInteger)
		return signedValue ? formatSignedInteger(value.value) : formatInteger(value.value);
	return signedValue ? formatSignedDecimal(value.value, decimals) : formatDecimal(value.value, decimals);
}

QString metricLabelWithUnit(const PassengerMetricRow& row) {
	if (row.unit == "s")
		return QString::fromStdString(row.label) + QStringLiteral(" (s)");
	if (row.unit == "passenger-hours")
		return QString::fromStdString(row.label) + QStringLiteral(" (pax-h)");
	return QString::fromStdString(row.label);
}

int decimalsForUnit(const std::string& unit) {
	return unit == "passenger-hours" ? 3 : 2;
}

struct Column {
	Column(const char* headerText, bool isNumber = false)
		: header(headerText), number(isNumber) {}
	QString header;
	bool number;
};

constexpr bool kNumber = true;
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
	m_summaryTable = makeTable(content, "passengerComparisonSummary",
		{"Metric", {"Baseline", kNumber}, {"This run", kNumber}, {"Difference", kNumber}});
	m_summaryTable->horizontalHeaderItem(3)->setToolTip(QStringLiteral("This run minus baseline"));
	contentLayout->addWidget(m_summaryTable, 1);

	contentLayout->addWidget(EditorLayout::sectionHeading(QStringLiteral("Most changed origin-destination pairs"), content));
	m_odTable = makeTable(content, "passengerComparisonOd",
		{"Origin", "Destination", {"Baseline journeys", kNumber}, {"This run journeys", kNumber},
			{"Baseline mean delay (s)", kNumber}, {"This run mean delay (s)", kNumber}, {"Mean delay diff (s)", kNumber},
			{"Baseline lost (pax-h)", kNumber}, {"This run lost (pax-h)", kNumber}, {"Lost diff (pax-h)", kNumber}});
	m_odTable->horizontalHeaderItem(9)->setToolTip(
		QStringLiteral("Change in passenger-hours lost (this run minus baseline); rows ranked by largest absolute change, up to 20"));
	contentLayout->addWidget(m_odTable, 1);

	auto* frame = new ResultFrame(resultTableArea({content}), QStringLiteral("passengerComparison"));
	ResultPanel* panel = frame->panel();
	panel->readout()->hide();

	auto* figures = new QWidget(frame);
	auto* figureColumn = new QVBoxLayout(figures);
	figureColumn->setContentsMargins(0, 0, 0, 0);
	figureColumn->setSpacing(8);

	const PassengerRunSummary& base = m_comparison.baseline;
	const PassengerRunSummary& cur = m_comparison.scenario;

	auto* lostFigure = new ResultFigure(QStringLiteral("passengerComparisonHoursLost"), figures);
	const double lostDiff = cur.passengerHoursLost - base.passengerHoursLost;
	lostFigure->setFigure(formatSignedDecimal(lostDiff, 3) + QStringLiteral(" h"), QStringLiteral("Pax-hours lost diff"),
		QStringLiteral("Baseline: %1 h\nThis run: %2 h")
			.arg(formatDecimal(base.passengerHoursLost, 3), formatDecimal(cur.passengerHoursLost, 3)));
	figureColumn->addWidget(lostFigure);

	auto* meanFigure = new ResultFigure(QStringLiteral("passengerComparisonMeanDelay"), figures);
	const bool bothCompleted = base.completedJourneys > 0 && cur.completedJourneys > 0;
	const QString baseMeanText = base.completedJourneys > 0 ? formatDecimal(base.meanDelaySeconds, 2) + QStringLiteral(" s") : QStringLiteral("-");
	const QString curMeanText = cur.completedJourneys > 0 ? formatDecimal(cur.meanDelaySeconds, 2) + QStringLiteral(" s") : QStringLiteral("-");
	meanFigure->setFigure(bothCompleted ? formatSignedDecimal(cur.meanDelaySeconds - base.meanDelaySeconds, 2) + QStringLiteral(" s") : QStringLiteral("-"),
		QStringLiteral("Mean delay diff"),
		QStringLiteral("Baseline: %1\nThis run: %2").arg(baseMeanText, curMeanText));
	figureColumn->addWidget(meanFigure);

	auto* notReachedFigure = new ResultFigure(QStringLiteral("passengerComparisonNotReached"), figures);
	const int notReachedDiff = cur.notReachedJourneys() - base.notReachedJourneys();
	notReachedFigure->setFigure(formatSignedInteger(notReachedDiff), QStringLiteral("Not reached diff"),
		QStringLiteral("Baseline: %1 (%2 no route, %3 in progress)\nThis run: %4 (%5 no route, %6 in progress)")
			.arg(base.notReachedJourneys())
			.arg(base.noRouteJourneys)
			.arg(base.inProgressJourneys)
			.arg(cur.notReachedJourneys())
			.arg(cur.noRouteJourneys)
			.arg(cur.inProgressJourneys));
	figureColumn->addWidget(notReachedFigure);

	panel->addSection(QString(), figures);

	m_exportButton = new QPushButton(QStringLiteral("CSV..."), frame);
	m_exportButton->setObjectName(QStringLiteral("passengerComparisonExport"));
	m_exportButton->setAccessibleName(QStringLiteral("Export CSV"));
	m_exportButton->setToolTip(QStringLiteral("Save summary and origin-destination comparison as CSV"));
	panel->addSection(QStringLiteral("Export"), m_exportButton);

	for (QPushButton* button : frame->findChildren<QPushButton*>())
		button->setAutoDefault(false);
	auto* closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	connect(closeButtons, &QDialogButtonBox::rejected, this, &QDialog::close);
	connect(m_exportButton, &QPushButton::clicked, this, [this]() { exportCsv(); });

	frame->setContext(runContextText(m_scenario.provenance) + QStringLiteral(" \u00b7 baseline ") + runContextText(m_baseline.provenance));
	frame->installInDialog(*this, QStringLiteral("Passenger comparison"), closeButtons);
	resize(qMin(maximumWidth(), 960), qMin(maximumHeight(), 640));

	fillSummaryTable();
	fillOdTable();
}

void PassengerComparisonWindow::fillSummaryTable() {
	std::vector<QStringList> rows;
	rows.reserve(m_comparison.metrics.size());
	for (const PassengerMetricRow& metric : m_comparison.metrics) {
		const int decimals = decimalsForUnit(metric.unit);
		const bool signedBaseline = metric.unit == "s";
		rows.push_back({
			metricLabelWithUnit(metric),
			formatMetricCell(metric.baseline, metric.isInteger, decimals, signedBaseline),
			formatMetricCell(metric.scenario, metric.isInteger, decimals, signedBaseline),
			formatMetricCell(metric.difference, metric.isInteger, decimals, true),
		});
	}
	fillTable(m_summaryTable, rows);
}

void PassengerComparisonWindow::fillOdTable() {
	std::vector<QStringList> rows;
	rows.reserve(m_comparison.topOdPairs.size());
	for (const OriginDestinationChange& od : m_comparison.topOdPairs) {
		rows.push_back({
			QString::fromStdString(od.origin),
			QString::fromStdString(od.destination),
			QString::number(od.baselineJourneys),
			QString::number(od.scenarioJourneys),
			formatMetricCell(od.baselineMeanDelay, false, 2, true),
			formatMetricCell(od.scenarioMeanDelay, false, 2, true),
			formatMetricCell(od.meanDelayDifference, false, 2, true),
			formatDecimal(od.baselinePassengerHoursLost, 3),
			formatDecimal(od.scenarioPassengerHoursLost, 3),
			formatSignedDecimal(od.passengerHoursLostDifference, 3),
		});
	}
	fillTable(m_odTable, rows);
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
