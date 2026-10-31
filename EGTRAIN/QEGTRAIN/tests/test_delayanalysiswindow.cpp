// Tests of the delay analysis window and of its export. The rows are typed by hand and the test reads no file.
// The numbers of the line fixture are copied from tests/characterization/expected as in test_delayanalysis.cpp:
// follow-level-0 and the pairs for no signalling level, levels 0 and 3 and level 5. Every expected figure was
// worked out by hand from those rows. Times and delays are in seconds, shares in percent. kNone marks a time that
// does not exist. The test opens the file chooser to check that the window stays in front after it and what the export
// asks for, and closes the warning of an export that fails itself.

#include "diagrams/DelayAnalysisWindow.h"
#include "diagrams/ChartTicks.h"
#include "diagrams/ResultFrame.h"
#include "diagrams/TrainFilterButton.h"
#include "widgets/DisplayPath.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHelpEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QStringList>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QBarSeries>
#include <QtCharts/QBarSet>
#include <QtCharts/QChartView>
#include <QtCharts/QValueAxis>

#include <cmath>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr double kNone = -1.0;

RunResultValue secondsOrNone(double seconds) {
	return seconds < 0.0 ? RunResultValue() : RunResultValue{true, seconds};
}

TimetableValueStatus statusOf(const RunResultValue& planned, const RunResultValue& simulated) {
	if (planned.available)
		return simulated.available ? TimetableValueStatus::Available : TimetableValueStatus::NotReached;
	return simulated.available ? TimetableValueStatus::NoPlan : TimetableValueStatus::NoPlanAndNotReached;
}

struct CallTimes {
	double plannedArrival;
	double plannedDeparture;
	double simulatedArrival;
	double simulatedDeparture;
};

TimetableResultRow makeRow(const char* service, int occurrence, const char* station, int journey, int call, const CallTimes& times) {
	TimetableResultRow result;
	result.trainId = std::string(service) + "-" + std::to_string(occurrence);
	result.serviceId = service;
	result.occurrence = occurrence;
	result.stationId = station;
	result.journeyIndex = journey;
	result.callIndex = call;
	result.plannedArrivalSeconds = secondsOrNone(times.plannedArrival);
	result.plannedDepartureSeconds = secondsOrNone(times.plannedDeparture);
	result.simulatedArrivalSeconds = secondsOrNone(times.simulatedArrival);
	result.simulatedDepartureSeconds = secondsOrNone(times.simulatedDeparture);
	result.arrivalStatus = statusOf(result.plannedArrivalSeconds, result.simulatedArrivalSeconds);
	result.departureStatus = statusOf(result.plannedDepartureSeconds, result.simulatedDepartureSeconds);
	return result;
}

TrainRunResult trainResult(const char* service, int occurrence, double entranceDelay = 0.0, const char* entranceStation = "",
	std::vector<std::string> incidents = {}) {
	TrainRunResult result;
	result.trainId = std::string(service) + "-" + std::to_string(occurrence);
	result.serviceId = service;
	result.occurrence = occurrence;
	result.entranceDelaySeconds = entranceDelay;
	result.entranceDelayStationId = entranceStation;
	result.directIncidentIds = std::move(incidents);
	return result;
}

DelayRunSnapshot snapshot(const char* scenarioId, std::vector<TimetableResultRow> rows, std::vector<TrainRunResult> trains) {
	DelayRunSnapshot result;
	result.caseRevision = "rev-1";
	result.scenarioId = scenarioId;
	result.baseTimeSeconds = 0.0;
	result.durationSeconds = 1500.0;
	result.timestep = 1.0;
	result.provenance.caseName = "Case A";
	result.provenance.appliedScenario = scenarioId;
	result.timetable = std::move(rows);
	result.run.trains = std::move(trains);
	return result;
}

// F1 and F2 call at A, B and C. The planned times are the same in every case, so only the simulated times are
// arguments. Neither train has a simulated time at A.
struct LineTimes {
	double f1BArrival, f1BDeparture, f1CArrival, f1CDeparture;
	double f2BArrival, f2BDeparture, f2CArrival, f2CDeparture;
};

DelayRunSnapshot lineSnapshot(const char* scenarioId, const LineTimes& t, std::vector<std::string> f1Incidents = {}) {
	DelayRunSnapshot result = snapshot(scenarioId,
		{
			makeRow("F1", 1, "A", 1, 1, {kNone, 120.0, kNone, kNone}),
			makeRow("F1", 1, "B", 2, 1, {600.0, 660.0, t.f1BArrival, t.f1BDeparture}),
			makeRow("F1", 1, "C", 3, 1, {1100.0, kNone, t.f1CArrival, t.f1CDeparture}),
			makeRow("F2", 1, "A", 1, 1, {kNone, 180.0, kNone, kNone}),
			makeRow("F2", 1, "B", 2, 1, {660.0, 720.0, t.f2BArrival, t.f2BDeparture}),
			makeRow("F2", 1, "C", 3, 1, {1160.0, kNone, t.f2CArrival, t.f2CDeparture}),
		},
		{trainResult("F1", 1, 0.0, "", std::move(f1Incidents)), trainResult("F2", 1)});
	result.hasIncidents = !result.run.trains[0].directIncidentIds.empty();
	return result;
}

const LineTimes kFollowLevel0 = {331, 660, 933, 934, 817, 877, 1150, 1151};

QTableWidget* table(const DelayAnalysisWindow& window, const char* name) {
	return window.findChild<QTableWidget*>(name);
}

QTabWidget* tabs(const DelayAnalysisWindow& window) {
	return window.findChild<QTabWidget*>("delayAnalysisTabs");
}

void showTab(const DelayAnalysisWindow& window, int index) {
	tabs(window)->setCurrentIndex(index);
	QApplication::processEvents();
}

TrainFilterButton* filterButton(const DelayAnalysisWindow& window, const char* name) {
	return window.findChild<TrainFilterButton*>(name);
}

bool setChecked(const DelayAnalysisWindow& window, const char* buttonName, const QString& label, bool checked) {
	TrainFilterButton* button = filterButton(window, buttonName);
	auto* list = button && button->menu() ? button->menu()->findChild<QListWidget*>() : nullptr;
	for (int index = 0; list && index < list->count(); ++index) {
		if (list->item(index)->text() != label)
			continue;
		list->item(index)->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
		return true;
	}
	return false;
}

void chooseEvents(const DelayAnalysisWindow& window, int index) {
	if (auto* combo = window.findChild<QComboBox*>("delayAnalysisEvents"))
		combo->setCurrentIndex(index);
}

QPushButton* exportButton(const DelayAnalysisWindow& window) {
	return window.findChild<QPushButton*>("delayAnalysisExport");
}

QString labelText(const DelayAnalysisWindow& window, const char* name) {
	const auto* label = window.findChild<QLabel*>(name);
	return label ? label->text() : QString();
}

QString cell(const QTableWidget* tableWidget, int row, int column) {
	return tableWidget && tableWidget->item(row, column) ? tableWidget->item(row, column)->text() : QString();
}

QStringList rowTexts(const QTableWidget* tableWidget, int row) {
	QStringList texts;
	for (int column = 0; tableWidget && column < tableWidget->columnCount(); ++column)
		texts << cell(tableWidget, row, column);
	return texts;
}

QStringList headerTexts(const QTableWidget* tableWidget) {
	QStringList texts;
	for (int column = 0; tableWidget && column < tableWidget->columnCount(); ++column)
		texts << tableWidget->horizontalHeaderItem(column)->text();
	return texts;
}

// The tooltip of the column whose header is `header`, or "missing" without such a column.
QString headerTip(const QTableWidget* tableWidget, const QString& header) {
	const int column = headerTexts(tableWidget).indexOf(header);
	return column < 0 ? QStringLiteral("missing") : tableWidget->horizontalHeaderItem(column)->toolTip();
}

// The row of the table whose first column and column `column` hold the two texts, or -1.
int rowOf(const QTableWidget* tableWidget, const QString& first, int column, const QString& other) {
	for (int row = 0; tableWidget && row < tableWidget->rowCount(); ++row) {
		if (cell(tableWidget, row, 0) == first && cell(tableWidget, row, column) == other)
			return row;
	}
	return -1;
}

int rows(const QTableWidget* tableWidget) {
	return tableWidget ? tableWidget->rowCount() : -1;
}

// Every text that the window shows: labels, tab titles, headers and cells.
QString allText(const DelayAnalysisWindow& window) {
	QString text;
	for (const QLabel* label : window.findChildren<QLabel*>())
		text += label->text() + "\n";
	for (const QTableWidget* tableWidget : window.findChildren<QTableWidget*>()) {
		text += headerTexts(tableWidget).join("\n") + "\n";
		for (int row = 0; row < tableWidget->rowCount(); ++row)
			text += rowTexts(tableWidget, row).join("\n") + "\n";
	}
	for (int index = 0; index < tabs(window)->count(); ++index)
		text += tabs(window)->tabText(index) + "\n";
	return text;
}

// The text of a key figure of the panel: its value and its caption.
QString figureText(const DelayAnalysisWindow& window, const char* name) {
	const auto* figure = window.findChild<ResultFigure*>(name);
	return figure ? figure->value() + " | " + figure->caption() : QStringLiteral("missing");
}

QStringList figureTexts(const DelayAnalysisWindow& window) {
	return {figureText(window, "delayAnalysisSum"), figureText(window, "delayAnalysisMean"), figureText(window, "delayAnalysisEarlyLate")};
}

void pressTab(QWidget* widget) {
	QKeyEvent press(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
	QKeyEvent release(QEvent::KeyRelease, Qt::Key_Tab, Qt::NoModifier);
	QApplication::sendEvent(widget, &press);
	QApplication::sendEvent(widget, &release);
}

QChart* histogramChart(const DelayAnalysisWindow& window) {
	const auto* view = window.findChild<QChartView*>("delayHistogram");
	return view ? view->chart() : nullptr;
}

QBarSet* histogramBars(const DelayAnalysisWindow& window) {
	QChart* chart = histogramChart(window);
	const auto* series = chart && chart->series().size() == 1 ? qobject_cast<QBarSeries*>(chart->series().first()) : nullptr;
	return series && series->count() == 1 ? series->barSets().first() : nullptr;
}

// The height of each bar of the Punctuality tab, from Early to more than 5 min late.
QList<qreal> barValues(const DelayAnalysisWindow& window) {
	QList<qreal> values;
	const QBarSet* bars = histogramBars(window);
	for (int index = 0; bars && index < bars->count(); ++index)
		values << bars->at(index);
	return values;
}

bool histogramShown(const DelayAnalysisWindow& window) {
	const auto* view = window.findChild<QChartView*>("delayHistogram");
	return view && view->isVisible() && window.findChild<QLabel*>("delayHistogramCounts")->isVisible()
		&& !window.findChild<QLabel*>("delayHistogramNote")->isVisible();
}

// The pair at level 0: follow-level-0 against sf-forward-level-0. F1 has direct evidence, F2 has none. F1 at C: 933 to 1218 (+285).
// F2 at B: arrival 817 to 866 (+49), departure 877 to 1013 (+136); at C 1150 to 1373 (+223). Positive and net total 508.
DelayAnalysisWindow levelZeroWindow() {
	return DelayAnalysisWindow(lineSnapshot("signal-failure-forward", {331, 660, 1218, 1219, 866, 1013, 1373, 1374}, {"inc.fwd"}),
		lineSnapshot("baseline", kFollowLevel0));
}

QStringList lines(const QString& path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return {};
	return QString::fromUtf8(file.readAll()).split("\r\n", Qt::SkipEmptyParts);
}

QString fileText(const QString& path) {
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

QJsonObject sidecar(const QString& path) {
	QFile file(path + ".provenance.json");
	return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
}

// The fields of a comparison record: 30 fields, empty except the ones that are given by index.
QStringList record(std::initializer_list<std::pair<int, const char*>> values) {
	QStringList fields;
	for (int index = 0; index < 30; ++index)
		fields << QString();
	for (const std::pair<int, const char*>& value : values)
		fields[value.first] = value.second;
	return fields;
}

// Handles events until `done` holds, for at most about a second.
bool waitFor(const std::function<bool()>& done) {
	for (int round = 0; round < 200 && !done(); ++round) {
		QApplication::processEvents(QEventLoop::AllEvents, 5);
		QThread::msleep(5);
	}
	return done();
}

// What the save dialog of an export showed, and the texts of the warnings after it.
struct ExportDialog {
	QString title;
	QStringList filters;
	QString suggested;
	QStringList warnings;
};

// Clicks `button`, answers the save dialog with `path`, or cancels it for an empty path, and closes the messages that
// follow. A dialog that stays open after the answer is cancelled.
ExportDialog answerExport(QPushButton* button, const QString& path) {
	ExportDialog shown;
	bool answered = false;
	QTimer timer;
	QObject::connect(&timer, &QTimer::timeout, [&shown, &answered, &path] {
		if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
			if (box->icon() == QMessageBox::Warning)
				shown.warnings << box->text();
			box->accept();
			return;
		}
		for (QWidget* widget : QApplication::topLevelWidgets())
			if (auto* dialog = qobject_cast<QFileDialog*>(widget); dialog && dialog->isVisible()) {
				if (answered) {
					dialog->reject();
					continue;
				}
				answered = true;
				shown.title = dialog->windowTitle();
				shown.filters = dialog->nameFilters();
				shown.suggested = QFileInfo(dialog->selectedFiles().value(0)).fileName();
				auto* name = dialog->findChild<QLineEdit*>("fileNameEdit");
				if (path.isEmpty() || !name) {
					dialog->reject();
					continue;
				}
				// The whole path in the name field, which also takes a folder that does not exist.
				name->setText(path);
				// Through QDialog, as QFileDialog::accept is protected.
				static_cast<QDialog*>(dialog)->accept();
			}
	});
	timer.start(0);
	if (button) button->click();
	return shown;
}

} // namespace

class TestDelayAnalysisWindow : public QObject {
	Q_OBJECT

private slots:
	void listsCallsOfFollowLevel0();
	void showsFiguresInPanel();
	void scrollsBodyWithLargeFont();
	void summarizesFollowLevel0();
	void countsStrictlyLaterThanThresholds();
	void showsPunctualityChart();
	void drawsChartLikeOtherResultCharts();
	void roundsTickSteps_data();
	void roundsTickSteps();
	void agreesPunctualityWithSummary();
	void filtersOccurrencesAndStations();
	void comparesAgainstBaseline();
	void alignsNumberColumnsRight();
	void showsEvidenceAndInputDelay();
	void showsMissingCall();
	void listsEveryIncompatibility();
	void saysThatNoBaselineIsSet();
	void opensOnCallsTab();
	void describesReasonsStatusesAndTotals();
	void exportsEveryTab();
	void keepsWindowInFrontAfterExport();
	void asksForCsvFileOnExport();
};

// H1: follow-level-0. The arrival delays are F1 -269 at B and -167 at C, F2 +157 at B and -10 at C. A has no
// arrival to plan or to simulate. The tab order, the columns and the call rows.
void TestDelayAnalysisWindow::listsCallsOfFollowLevel0() {
	DelayAnalysisWindow window(lineSnapshot("follow-level-0", kFollowLevel0), std::nullopt);
	window.show();
	QApplication::processEvents();
	const QTabWidget* tabWidget = tabs(window);
	// the tabs are Calls, Summary, Punctuality and Against baseline, and Calls is open
	if (tabWidget == nullptr)
		QFAIL("the tabs are Calls, Summary, Punctuality and Against baseline, and Calls is open");
	QCOMPARE(tabWidget->count(), 4);
	QCOMPARE(tabWidget->tabText(0), "Calls");
	QCOMPARE(tabWidget->tabText(1), "Summary");
	QCOMPARE(tabWidget->tabText(2), "Punctuality");
	QCOMPARE(tabWidget->tabText(3), "Against baseline");
	QCOMPARE(tabWidget->currentIndex(), 0);
	// the window is not modal
	QCOMPARE(window.windowTitle(), "Delay analysis");
	QVERIFY(!window.isModal());
	for (const QTableWidget* tableWidget : window.findChildren<QTableWidget*>())
		QVERIFY2(!tableWidget->tabKeyNavigation(), "Tab leaves each table of the window");
	// the window title is the only heading and the header names the run, its scenario and the missing baseline
	QVERIFY(!window.findChild<QLabel*>("dialogHeading"));
	QVERIFY(!window.findChild<QLabel*>("dialogContext"));
	QCOMPARE(labelText(window, "delayAnalysisContext"), "Case A / follow-level-0 · no delay baseline set");
	const QTableWidget* calls = table(window, "delayCalls");
	// the columns of the calls
	QCOMPARE(headerTexts(calls),
		QStringList({"Service", "Occurrence", "Train", "Operating code", "Station", "Journey order", "Call", "Event", "Planned (s)",
			"Simulated (s)", "Delay (s)", "Class", "Status"}));
	// the delay header says what a delay is
	QCOMPARE(headerTip(calls, "Delay (s)"), "Simulated minus planned time\nNegative is early");
	QVERIFY(headerTip(calls, "Planned (s)").isEmpty());
	// no help paragraph above the calls and no tooltip on the events choice
	QVERIFY(!allText(window).contains("authored plan"));
	QVERIFY(window.findChild<QComboBox*>("delayAnalysisEvents")->toolTip().isEmpty());
	// the arrivals of two trains at three stations
	QCOMPARE(rows(calls), 6);
	// an arrival without a plan and without a time has no delay, not 0
	QCOMPARE(rowTexts(calls, 0), QStringList({"F1", "1", "F1-1", "", "A", "1", "1", "Arrival", "-", "-", "-", "Not available", "No plan, not reached"}));
	// an early arrival
	QCOMPARE(rowTexts(calls, 1), QStringList({"F1", "1", "F1-1", "", "B", "2", "1", "Arrival", "600", "331", "-269", "Early", "Available"}));
	// a delay is aligned to the right and a service to the left
	QVERIFY(calls->item(1, 10)->textAlignment() & Qt::AlignRight);
	QVERIFY(!(calls->item(1, 0)->textAlignment() & Qt::AlignRight));
	// a late arrival
	QCOMPARE(rowTexts(calls, 4), QStringList({"F2", "1", "F2-1", "", "B", "2", "1", "Arrival", "660", "817", "+157", "Late", "Available"}));
	// a small early arrival
	QCOMPARE(rowTexts(calls, 5), QStringList({"F2", "1", "F2-1", "", "C", "3", "1", "Arrival", "1160", "1150", "-10", "Early", "Available"}));

	// The departures. F1 never left A (not reached), left B on time, and left C without a plan. F2 left B 157 s late.
	chooseEvents(window, 1);
	// the departures of the same calls
	QCOMPARE(rows(calls), 6);
	// a train that never left has no delay, not 0
	QCOMPARE(rowTexts(calls, 0), QStringList({"F1", "1", "F1-1", "", "A", "1", "1", "Departure", "120", "-", "-", "Not available", "Not reached"}));
	// a delay of 0 is a value and is on time
	QCOMPARE(rowTexts(calls, 1), QStringList({"F1", "1", "F1-1", "", "B", "2", "1", "Departure", "660", "660", "0", "On time", "Available"}));
	// a departure without a plan has no delay
	QCOMPARE(rowTexts(calls, 2), QStringList({"F1", "1", "F1-1", "", "C", "3", "1", "Departure", "-", "934", "-", "Not available", "No plan"}));
	// a late departure
	QCOMPARE(cell(calls, 4, 10), "+157");
	QCOMPARE(cell(calls, 4, 11), "Late");
}

// The panel holds the key figures of the selection, the filters and the export. The figures are those of all selected
// stations in the Summary tab: for the arrivals of follow-level-0 a sum of -289 s, a mean of -72.25 s, 3 early and 1 late.
void TestDelayAnalysisWindow::showsFiguresInPanel() {
	DelayAnalysisWindow window(lineSnapshot("follow-level-0", kFollowLevel0), std::nullopt);
	window.show();
	QApplication::processEvents();
	auto* panel = window.findChild<QWidget*>("resultPanel");
	if (panel == nullptr)
		QFAIL("the window has a panel");
	bool inPanel = true;
	for (const char* name : {"delayAnalysisSum", "delayAnalysisMean", "delayAnalysisEarlyLate", "delayAnalysisOccurrences", "delayAnalysisStations",
			 "delayAnalysisEvents", "delayAnalysisExport"})
		inPanel = inPanel && window.findChild<QWidget*>(name) && panel->isAncestorOf(window.findChild<QWidget*>(name));
	// the figures, the filters and the export lie in the panel, beside the tabs
	QVERIFY(inPanel);
	QVERIFY(!panel->isAncestorOf(tabs(window)));
	// the figures of the arrivals
	QCOMPARE(figureTexts(window), QStringList({"-289 s | Sum of delays", "-72.25 s | Mean", "3 / 1 | Early / late"}));
	const auto* earlyLate = window.findChild<ResultFigure*>("delayAnalysisEarlyLate");
	// the tooltips give the on-time count and the number of available arrivals
	if (earlyLate == nullptr)
		QFAIL("the tooltips give the on-time count and the number of available arrivals");
	QVERIFY(earlyLate->toolTip().contains("Early: 3"));
	QVERIFY(earlyLate->toolTip().contains("On time: 0"));
	QVERIFY(earlyLate->toolTip().contains("Late: 1"));
	QVERIFY(window.findChild<ResultFigure*>("delayAnalysisSum")->toolTip().contains("Available arrivals: 4"));
	QStringList headings;
	for (const QLabel* label : panel->findChildren<QLabel*>())
		if (label->property("resultSectionHeading").toBool() && label->isVisible())
			headings << label->text();
	// only the export has a heading
	QCOMPARE(headings, QStringList({"Export"}));
	const QPushButton* csv = exportButton(window);
	const auto* events = window.findChild<QComboBox*>("delayAnalysisEvents");
	// the export reads CSV... and the events choice names what it chooses
	QCOMPARE(csv->text(), "CSV...");
	QCOMPARE(csv->accessibleName(), "Export CSV");
	QCOMPARE(events->currentText(), "Events: Arrivals");
	QCOMPARE(events->accessibleName(), "Events");

	chooseEvents(window, 1);
	// the figures of the departures
	QCOMPARE(figureTexts(window), QStringList({"+157 s | Sum of delays", "+78.5 s | Mean", "0 / 1 | Early / late"}));
	chooseEvents(window, 0);
	QVERIFY2(setChecked(window, "delayAnalysisOccurrences", "F2 #1", false), "F2 can be unchecked");
	QApplication::processEvents();
	// the figures of the arrivals of F1
	QCOMPARE(figureTexts(window), QStringList({"-436 s | Sum of delays", "-218 s | Mean", "2 / 0 | Early / late"}));
	QVERIFY2(setChecked(window, "delayAnalysisOccurrences", "F1 #1", false), "F1 can be unchecked");
	QApplication::processEvents();
	// without a train the sum and the mean are not available
	QCOMPARE(figureTexts(window), QStringList({"- | Sum of delays", "- | Mean", "0 / 0 | Early / late"}));

	setChecked(window, "delayAnalysisOccurrences", "F1 #1", true);
	setChecked(window, "delayAnalysisOccurrences", "F2 #1", true);
	QApplication::processEvents();
	// The tables lie beside a panel of 200 px and scroll sideways; Tab goes through the header, the tabs and the panel.
	const QTableWidget* calls = table(window, "delayCalls");
	// a panel of 200 px and a calls table that scrolls sideways
	QCOMPARE_LE(window.width(), 960);
	QCOMPARE(panel->width(), 200);
	QCOMPARE_GT(calls->horizontalScrollBar()->maximum(), 0);
	window.findChild<QToolButton*>("delayAnalysisPanelToggle")->setFocus(Qt::TabFocusReason);
	QStringList order;
	for (int step = 0; step < 7 && QApplication::focusWidget(); ++step) {
		QWidget* focused = QApplication::focusWidget();
		if (qobject_cast<QTabBar*>(focused))
			order << QStringLiteral("tabs");
		else
			order << focused->objectName();
		pressTab(focused);
	}
	// Tab goes through the header, the tabs, the table and the panel
	QCOMPARE(order, QStringList({"delayAnalysisPanelToggle", "tabs", "delayCalls", "delayAnalysisOccurrences", "delayAnalysisStations", "delayAnalysisEvents", "delayAnalysisExport"}));
}

// At 1.5 times the font in a short window the body of the dialog scrolls to the panel, which keeps its figures and filters
// whole, and Close stays below the body.
void TestDelayAnalysisWindow::scrollsBodyWithLargeFont() {
	const QFont original = QApplication::font();
	const auto restoreFont = qScopeGuard([&original] { QApplication::setFont(original); });
	QFont font = original;
	font.setPointSizeF(font.pointSizeF() * 1.5);
	QApplication::setFont(font);
	DelayAnalysisWindow window(lineSnapshot("follow-level-0", kFollowLevel0), std::nullopt);
	window.show();
	window.resize(window.width(), 300);
	QApplication::processEvents();
	auto* scroll = window.findChild<QScrollArea*>("dialogBodyScroll");
	auto* panel = window.findChild<QWidget*>("resultPanel");
	auto* close = window.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Close);
	// the body scrolls to the whole panel and Close stays in the window
	if (scroll == nullptr)
		QFAIL("the body scrolls to the whole panel and Close stays in the window");
	if (panel == nullptr)
		QFAIL("the body scrolls to the whole panel and Close stays in the window");
	if (close == nullptr)
		QFAIL("the body scrolls to the whole panel and Close stays in the window");
	QCOMPARE_GT(scroll->verticalScrollBar()->maximum(), 0);
	QCOMPARE_GE(panel->height(), panel->minimumSizeHint().height());
	QVERIFY2(close->isVisible(), qPrintable(QString("the body scrolls to the whole panel and Close stays in the window: range %1, panel %2 of %3").arg(scroll->verticalScrollBar()->maximum()).arg(panel->height()).arg(panel->minimumSizeHint().height())));
	QCOMPARE_LE(close->mapTo(&window, QPoint(0, close->height())).y(), window.height());
}

// H1 summary of follow-level-0. B: 2 available, sum -112, mean -56, minimum -269, maximum 157, 1 early and 1 late, 1 of 2 later
// than 0 and than 60 s, none later than 180 or 300 s. C: sum -177, mean -88.5. Network: 4 available, 2 without a plan or a
// time, sum -289, mean -72.25, 1 of 4 later than 0 and than 60 s. A has no figure at all.
void TestDelayAnalysisWindow::summarizesFollowLevel0() {
	DelayAnalysisWindow window(lineSnapshot("follow-level-0", kFollowLevel0), std::nullopt);
	window.show();
	showTab(window, 1);
	const QTableWidget* summary = table(window, "delaySummary");
	// the columns of the summary name the thresholds
	QCOMPARE(headerTexts(summary),
		QStringList({"Station", "Calls", "Available", "No plan", "Not reached", "No plan, not reached", "Early", "On time", "Late", "Sum (s)",
			"Mean (s)", "Minimum (s)", "Maximum (s)", "Later than 0 s", "Later than 60 s", "Later than 180 s", "Later than 300 s"}));
	// a row for each station and one for all selected stations
	QCOMPARE(rows(summary), 4);
	// a station without an available event has no figure and no share, not 0
	QCOMPARE(rowTexts(summary, 0), QStringList({"A", "2", "0", "0", "0", "2", "0", "0", "0", "-", "-", "-", "-", "-", "-", "-", "-"}));
	// station B
	QCOMPARE(rowTexts(summary, 1),
		QStringList({"B", "2", "2", "0", "0", "0", "1", "0", "1", "-112", "-56", "-269", "+157", "50.0% (1 of 2)", "50.0% (1 of 2)",
			"0.0% (0 of 2)", "0.0% (0 of 2)"}));
	// station C
	QCOMPARE(rowTexts(summary, 2),
		QStringList({"C", "2", "2", "0", "0", "0", "2", "0", "0", "-177", "-88.5", "-167", "-10", "0.0% (0 of 2)", "0.0% (0 of 2)",
			"0.0% (0 of 2)", "0.0% (0 of 2)"}));
	// all selected stations
	QCOMPARE(rowTexts(summary, 3),
		QStringList({"All selected stations", "6", "4", "0", "0", "2", "3", "0", "1", "-289", "-72.25", "-269", "+157", "25.0% (1 of 4)",
			"25.0% (1 of 4)", "0.0% (0 of 4)", "0.0% (0 of 4)"}));
	bool tips = true;
	for (const char* header : {"Later than 0 s", "Later than 60 s", "Later than 180 s", "Later than 300 s"})
		tips &= headerTip(summary, header) == "Strictly later\nCounts available events only";
	// each threshold header says that later means strictly later and counts available events only, without a help paragraph
	QVERIFY(tips);
	QVERIFY(headerTip(summary, "Late").isEmpty());
	QVERIFY(!allText(window).contains("not counted as 0"));

	// The departures of follow-level-0: B has the delays 0 and +157, A is not reached and C has no plan.
	chooseEvents(window, 1);
	// departures at B
	QCOMPARE(rowTexts(summary, 1),
		QStringList({"B", "2", "2", "0", "0", "0", "0", "1", "1", "+157", "+78.5", "0", "+157", "50.0% (1 of 2)", "50.0% (1 of 2)",
			"0.0% (0 of 2)", "0.0% (0 of 2)"}));
	// departures at A and C have a count of the missing values and no figure
	QCOMPARE(rowTexts(summary, 0), QStringList({"A", "2", "0", "0", "2", "0", "0", "0", "0", "-", "-", "-", "-", "-", "-", "-", "-"}));
	QCOMPARE(rowTexts(summary, 2), QStringList({"C", "2", "0", "2", "0", "0", "0", "0", "0", "-", "-", "-", "-", "-", "-", "-", "-"}));
	// departures of all selected stations
	QCOMPARE(rowTexts(summary, 3),
		QStringList({"All selected stations", "6", "2", "2", "2", "0", "0", "1", "1", "+157", "+78.5", "0", "+157", "50.0% (1 of 2)",
			"50.0% (1 of 2)", "0.0% (0 of 2)", "0.0% (0 of 2)"}));
}

// Arrivals 0, 60, 180, 300 and 301 s late at five stations. A call is later than a threshold only when its delay is strictly greater:
// later than 0 s: 4 of 5 (80 %); than 60 s: 3 (60 %); than 180 s: 2 (40 %); than 300 s: 1 (20 %). Sum 841, mean 168.2.
void TestDelayAnalysisWindow::countsStrictlyLaterThanThresholds() {
	std::vector<TimetableResultRow> timetable;
	const char* stations[5] = {"S0", "S60", "S180", "S300", "S301"};
	const double delays[5] = {0.0, 60.0, 180.0, 300.0, 301.0};
	for (int index = 0; index < 5; ++index)
		timetable.push_back(makeRow("Z", 1, stations[index], index + 1, 1, {1000.0, kNone, 1000.0 + delays[index], kNone}));
	DelayAnalysisWindow window(snapshot("late", timetable, {trainResult("Z", 1)}), std::nullopt);
	window.show();
	showTab(window, 1);
	const QTableWidget* summary = table(window, "delaySummary");
	// five stations and all selected stations
	QCOMPARE(rows(summary), 6);
	// later than a threshold is strictly later
	QCOMPARE(rowTexts(summary, 5),
		QStringList({"All selected stations", "5", "5", "0", "0", "0", "0", "1", "4", "+841", "+168.2", "0", "+301", "80.0% (4 of 5)",
			"60.0% (3 of 5)", "40.0% (2 of 5)", "20.0% (1 of 5)"}));
	// a delay of 60 s is later than 0 s and not later than 60 s
	QCOMPARE(rowTexts(summary, 1),
		QStringList({"S60", "1", "1", "0", "0", "0", "0", "0", "1", "+60", "+60", "+60", "+60", "100.0% (1 of 1)", "0.0% (0 of 1)",
			"0.0% (0 of 1)", "0.0% (0 of 1)"}));
}

// H1: follow-level-0. The arrivals -269, -167 and -10 are early and +157 is 1 to 3 min late; the two arrivals at A have
// neither a plan nor a time. The departures: 0 at B (on time) and +157 at B, two not reached at A and two without a plan at C.
void TestDelayAnalysisWindow::showsPunctualityChart() {
	DelayAnalysisWindow window(lineSnapshot("follow-level-0", kFollowLevel0), std::nullopt);
	window.show();
	showTab(window, 2);
	QChart* chart = histogramChart(window);
	QBarSet* bars = histogramBars(window);
	// the Punctuality tab has a bar chart with one set of bars
	if (chart == nullptr)
		QFAIL("the Punctuality tab has a bar chart with one set of bars");
	if (bars == nullptr)
		QFAIL("the Punctuality tab has a bar chart with one set of bars");
	const auto* series = qobject_cast<QBarSeries*>(chart->series().first());
	auto* classes = chart->axes(Qt::Horizontal).isEmpty() ? nullptr : qobject_cast<QBarCategoryAxis*>(chart->axes(Qt::Horizontal).first());
	const auto* counts = chart->axes(Qt::Vertical).isEmpty() ? nullptr : qobject_cast<QValueAxis*>(chart->axes(Qt::Vertical).first());
	// a bar for each class, labelled with the class on two lines where it ends in min late
	if (classes == nullptr)
		QFAIL("a bar for each class, labelled with the class on two lines where it ends in min late");
	QCOMPARE(classes->categories(),
		QStringList({"Early", "On time", "Up to 1<br>min late", "1 to 3<br>min late", "3 to 5<br>min late", "Over 5<br>min late"}));
	// the axis of the events has whole-number ticks from 0 and a step of room above the highest bar
	if (counts == nullptr)
		QFAIL("the axis of the events has whole-number ticks from 0 and a step of room above the highest bar");
	QCOMPARE(counts->titleText(), "Events");
	QCOMPARE_EQ(counts->min(), 0.0);
	QCOMPARE_EQ(counts->max(), 4.0);
	QCOMPARE(counts->tickType(), QValueAxis::TicksDynamic);
	QCOMPARE_EQ(counts->tickInterval(), 1.0);
	QCOMPARE_EQ(counts->tickAnchor(), 0.0);
	QCOMPARE(counts->labelFormat(), "%.0f");
	// each bar has its count as a label above it
	QVERIFY(series->isLabelsVisible());
	QCOMPARE(series->labelsFormat(), "@value");
	QCOMPARE(series->labelsPosition(), QAbstractBarSeries::LabelsOutsideEnd);
	// one bar colour, no legend and the background of the other result charts
	QCOMPARE(bars->color(), QColor(0x2a, 0x78, 0xd6));
	QVERIFY(!chart->legend()->isVisible());
	QCOMPARE(chart->backgroundBrush().color(), window.palette().color(QPalette::Base));
	// the arrivals in their classes
	QVERIFY(histogramShown(window));
	QCOMPARE(barValues(window), QList<qreal>({3, 0, 0, 1, 0, 0}));
	// the counts of the arrivals
	QCOMPARE(labelText(window, "delayHistogramCounts"),
		"4 arrivals of 2 trains; 2 not reached or without a plan. No more than 3 min late: 100.0% (4 of 4); no more than 5 min late: "
		"100.0% (4 of 4).");
	QVERIFY2(window.findChild<QComboBox*>("delayAnalysisEvents")->isEnabled(), "the events choice is on for the punctuality");

	// The tooltip of a bar gives the class, the count and the share.
	emit histogramBars(window)->hovered(true, 3);
	// the tooltip of a bar
	QCOMPARE(QToolTip::text(), "1 to 3 min late\n1 of 4 arrivals (25.0%)");
	QWidget* plot = window.findChild<QChartView*>("delayHistogram")->viewport();
	QHelpEvent help(QEvent::ToolTip, QPoint(4, 4), plot->mapToGlobal(QPoint(4, 4)));
	QApplication::sendEvent(plot, &help);
	// the tooltip of a bar stays when the pointer rests
	QCOMPARE(QToolTip::text(), "1 to 3 min late\n1 of 4 arrivals (25.0%)");
	emit histogramBars(window)->hovered(false, 3);

	chooseEvents(window, 1);
	// the events choice shows the departures
	QCOMPARE(barValues(window), QList<qreal>({0, 1, 0, 1, 0, 0}));
	QCOMPARE(labelText(window, "delayHistogramCounts"),
		"2 departures of 2 trains; 4 not reached or without a plan. No more than 3 min late: 100.0% (2 of 2); no more than 5 min "
		"late: 100.0% (2 of 2).");
	chooseEvents(window, 0);

	QVERIFY2(setChecked(window, "delayAnalysisOccurrences", "F2 #1", false), "F2 can be unchecked");
	// the trains choice leaves F2 out
	QCOMPARE(barValues(window), QList<qreal>({2, 0, 0, 0, 0, 0}));
	QCOMPARE_EQ(counts->max(), 3.0);
	QCOMPARE(labelText(window, "delayHistogramCounts"),
		"2 arrivals of 1 train; 1 not reached or without a plan. No more than 3 min late: 100.0% (2 of 2); no more than 5 min "
		"late: 100.0% (2 of 2).");
	QVERIFY2(setChecked(window, "delayAnalysisStations", "C", false), "C can be unchecked");
	// the stations choice leaves C out
	QCOMPARE(barValues(window), QList<qreal>({1, 0, 0, 0, 0, 0}));
	QCOMPARE(labelText(window, "delayHistogramCounts"),
		"1 arrival of 1 train; 1 not reached or without a plan. No more than 3 min late: 100.0% (1 of 1); no more than 5 min late: "
		"100.0% (1 of 1).");

	// Only A, where no arrival has a delay: the tab says so, has no chart and has nothing to export.
	// only A is chosen, for both trains
	QVERIFY(setChecked(window, "delayAnalysisStations", "B", false));
	QVERIFY(setChecked(window, "delayAnalysisOccurrences", "F2 #1", true));
	// without an available arrival the tab shows a note instead of the chart
	QVERIFY(!histogramShown(window));
	QVERIFY(!window.findChild<QChartView*>("delayHistogram")->isVisible());
	QCOMPARE(labelText(window, "delayHistogramNote"), "The selected trains and stations have no arrival with a delay.");
	QVERIFY(!exportButton(window)->isEnabled());
	chooseEvents(window, 1);
	// the note names the events
	QCOMPARE(labelText(window, "delayHistogramNote"), "The selected trains and stations have no departure with a delay.");
	chooseEvents(window, 0);
	// a station with an arrival brings the chart back
	QVERIFY(setChecked(window, "delayAnalysisStations", "B", true));
	QVERIFY(histogramShown(window));
	QVERIFY(exportButton(window)->isEnabled());
	QCOMPARE(barValues(window), QList<qreal>({1, 0, 0, 1, 0, 0}));
}

// The Punctuality chart has the look of the other result charts.
void TestDelayAnalysisWindow::drawsChartLikeOtherResultCharts() {
	DelayAnalysisWindow window(lineSnapshot("follow-level-0", kFollowLevel0), std::nullopt);
	window.show();
	showTab(window, 2);
	QChart* chart = histogramChart(window);
	// the Punctuality tab has a chart with axes
	if (chart == nullptr)
		QFAIL("the Punctuality tab has a chart with axes");
	QVERIFY(!chart->axes().isEmpty());
	const QPalette palette = window.palette();
	// square corners, the margins and the background of the result charts
	QCOMPARE_EQ(chart->backgroundRoundness(), 0);
	QCOMPARE(chart->margins(), QMargins(12, 8, 12, 8));
	QCOMPARE(chart->backgroundBrush().color(), palette.color(QPalette::Base));
	QColor grid = palette.color(QPalette::WindowText);
	grid.setAlpha(30);
	QColor line = palette.color(QPalette::WindowText);
	line.setAlpha(90);
	for (const QAbstractAxis* axis : chart->axes()) {
		// a quiet grid and axis line, and labels and titles in the colour of the text
		QCOMPARE(axis->gridLinePen().color(), grid);
		QCOMPARE(axis->linePen().color(), line);
		QCOMPARE(axis->labelsBrush().color(), palette.color(QPalette::WindowText));
		QCOMPARE(axis->titleBrush().color(), palette.color(QPalette::WindowText));
	}
}

// The steps of the axes are 1, 2 or 5 times a power of ten.
void TestDelayAnalysisWindow::roundsTickSteps_data() {
	QTest::addColumn<double>("span");
	QTest::addColumn<int>("steps");
	QTest::addColumn<double>("step");
	QTest::newRow("8 in 8") << 8.0 << 8 << 1.0;
	QTest::newRow("10 in 8") << 10.0 << 8 << 2.0;
	QTest::newRow("100 in 8") << 100.0 << 8 << 20.0;
	QTest::newRow("0.3 in 8") << 0.3 << 8 << 0.05;
	QTest::newRow("14 in 7") << 14.0 << 7 << 2.0;
	QTest::newRow("15 in 7") << 15.0 << 7 << 5.0;
	QTest::newRow("70 in 7") << 70.0 << 7 << 10.0;
	QTest::newRow("71 in 7") << 71.0 << 7 << 20.0;
}

void TestDelayAnalysisWindow::roundsTickSteps() {
	QFETCH(double, span);
	QFETCH(int, steps);
	QFETCH(double, step);
	QVERIFY2(std::abs(roundTickStep(span, steps) - step) < step * 1e-9,
		qPrintable(QString("%1 in at most %2 steps has steps of %3: %4").arg(span).arg(steps).arg(step).arg(roundTickStep(span, steps))));
}

// The arrivals 0, 60, 180, 300 and 301 s late: no more than 3 min late are those of 0, 60 and 180 s (3 of 5, 60 %), and no more
// than 5 min late also that of 300 s (4 of 5, 80 %). The Summary shows the rest: 40 % later than 180 s and 20 % later than 300 s.
void TestDelayAnalysisWindow::agreesPunctualityWithSummary() {
	std::vector<TimetableResultRow> timetable;
	const double delays[5] = {0.0, 60.0, 180.0, 300.0, 301.0};
	for (int index = 0; index < 5; ++index)
		timetable.push_back(makeRow("Z", 1, "S", index + 1, index + 1, {1000.0, kNone, 1000.0 + delays[index], kNone}));
	DelayAnalysisWindow window(snapshot("late", timetable, {trainResult("Z", 1)}), std::nullopt);
	window.show();
	showTab(window, 2);
	// a delay on a threshold is in the class below it
	QCOMPARE(barValues(window), QList<qreal>({0, 1, 1, 1, 1, 1}));
	// the shares agree with the summary
	QCOMPARE(labelText(window, "delayHistogramCounts"),
		"5 arrivals of 1 train. No more than 3 min late: 60.0% (3 of 5); no more than 5 min late: 80.0% (4 of 5).");
	const QTableWidget* summary = table(window, "delaySummary");
	// the Summary has the rest
	QCOMPARE(cell(summary, 1, 15), "40.0% (2 of 5)");
	QCOMPARE(cell(summary, 1, 16), "20.0% (1 of 5)");
}

// The filter buttons choose occurrences and stations. Nothing chosen shows nothing, not everything.
void TestDelayAnalysisWindow::filtersOccurrencesAndStations() {
	DelayAnalysisWindow window(lineSnapshot("follow-level-0", kFollowLevel0), std::nullopt);
	window.show();
	QApplication::processEvents();
	const TrainFilterButton* occurrences = filterButton(window, "delayAnalysisOccurrences");
	const TrainFilterButton* stations = filterButton(window, "delayAnalysisStations");
	const QTableWidget* calls = table(window, "delayCalls");
	const QTableWidget* summary = table(window, "delaySummary");
	// the buttons list the occurrences and the stations with their counts
	if (occurrences == nullptr)
		QFAIL("the buttons list the occurrences and the stations with their counts");
	if (stations == nullptr)
		QFAIL("the buttons list the occurrences and the stations with their counts");
	QCOMPARE(occurrences->text(), "Trains (2/2)");
	QCOMPARE(stations->text(), "Stations (3/3)");
	QCOMPARE(occurrences->visibleTrainIds(), QStringList({"F1 #1", "F2 #1"}));
	QCOMPARE(stations->visibleTrainIds(), QStringList({"A", "B", "C"}));
	const auto* occurrenceSearch = occurrences ? occurrences->findChild<QLineEdit*>() : nullptr;
	const auto* stationSearch = stations ? stations->findChild<QLineEdit*>() : nullptr;
	// the search boxes and the tooltips name what the buttons list
	if (occurrenceSearch == nullptr)
		QFAIL("the search boxes and the tooltips name what the buttons list");
	QCOMPARE(occurrenceSearch->placeholderText(), "Search trains...");
	if (stationSearch == nullptr)
		QFAIL("the search boxes and the tooltips name what the buttons list");
	QCOMPARE(stationSearch->placeholderText(), "Search stations...");
	QCOMPARE(occurrences->toolTip(), "Choose which trains the tabs show");
	QCOMPARE(stations->toolTip(), "Choose which stations the tabs show");

	// one occurrence is unchecked
	QVERIFY(setChecked(window, "delayAnalysisOccurrences", "F1 #1", false));
	QCOMPARE(occurrences->text(), "Trains (1/2)");
	// only F2 remains in the calls
	QCOMPARE(rows(calls), 3);
	QCOMPARE(cell(calls, 0, 0), "F2");
	QCOMPARE(cell(calls, 2, 0), "F2");
	showTab(window, 1);
	// F2 at B and C: +157 and -10, and A has no arrival: 2 available, sum 147, mean 73.5, minimum -10, maximum 157.
	// the summary counts the chosen occurrence only
	QCOMPARE(rowTexts(summary, 3),
		QStringList({"All selected stations", "3", "2", "0", "0", "1", "1", "0", "1", "+147", "+73.5", "-10", "+157", "50.0% (1 of 2)",
			"50.0% (1 of 2)", "0.0% (0 of 2)", "0.0% (0 of 2)"}));
	// a station that is unchecked leaves the summary
	QVERIFY(setChecked(window, "delayAnalysisStations", "A", false));
	QCOMPARE(stations->text(), "Stations (2/3)");
	QCOMPARE(rows(summary), 3);
	QCOMPARE(rowTexts(summary, 2),
		QStringList({"All selected stations", "2", "2", "0", "0", "0", "1", "0", "1", "+147", "+73.5", "-10", "+157", "50.0% (1 of 2)",
			"50.0% (1 of 2)", "0.0% (0 of 2)", "0.0% (0 of 2)"}));
	showTab(window, 0);
	// a station that is unchecked leaves the calls
	QCOMPARE(rows(calls), 2);
	QCOMPARE(cell(calls, 0, 4), "B");
	QCOMPARE(cell(calls, 1, 4), "C");

	// Nothing chosen: no row, no figure and no share, and nothing to export.
	// both occurrences are unchecked
	QVERIFY(setChecked(window, "delayAnalysisOccurrences", "F1 #1", true));
	QVERIFY(setChecked(window, "delayAnalysisOccurrences", "F2 #1", false));
	QVERIFY(setChecked(window, "delayAnalysisOccurrences", "F1 #1", false));
	// no occurrence chosen shows no event and no figure
	QCOMPARE(rows(calls), 0);
	QCOMPARE(rows(summary), 1);
	QCOMPARE(rowTexts(summary, 0),
		QStringList({"All selected stations", "0", "0", "0", "0", "0", "0", "0", "0", "-", "-", "-", "-", "-", "-", "-", "-"}));
	// nothing to export
	QVERIFY(exportButton(window));
	QVERIFY(!exportButton(window)->isEnabled());
}

void TestDelayAnalysisWindow::comparesAgainstBaseline() {
	DelayAnalysisWindow window = levelZeroWindow();
	window.show();
	showTab(window, 3);
	// a compatible baseline shows the occurrences and the calls
	QVERIFY(window.findChild<QWidget*>("delayBaselineOccurrences")->isVisible());
	QVERIFY(!table(window, "delayBaselineDiagnostics")->isVisible());
	// the header names both runs
	QCOMPARE(labelText(window, "delayAnalysisContext"), "Case A / signal-failure-forward \u00b7 delay baseline Case A / baseline");
	// the totals
	QCOMPARE(labelText(window, "delayBaselineTotals"), "2 trains. At the final arrival: 2 later. Added delay +508 s, net +508 s.");
	const QTableWidget* occurrences = table(window, "delayBaselineOccurrences");
	// the columns of the occurrences
	QCOMPARE(headerTexts(occurrences),
		QStringList({"Service", "Occurrence", "Train", "Operating code", "Input delay (s)", "Input delay station", "Direct incident evidence",
			"Final station", "Baseline final arrival (s)", "Scenario final arrival (s)", "Additional delay (s)", "Change", "Secondary delay (s)"}));
	// the delay headers of the trains say what they mean
	QCOMPARE(headerTip(occurrences, "Input delay (s)"), "Entrance delay set in this scenario");
	QCOMPARE(headerTip(occurrences, "Secondary delay (s)"), "Additional delay at the final arrival minus the input delay");
	QCOMPARE(headerTip(occurrences, "Additional delay (s)"), "This run minus the baseline, same call");
	// the final arrivals, the input delay and the evidence of each occurrence
	QCOMPARE(rows(occurrences), 2);
	QCOMPARE(rowTexts(occurrences, 0), QStringList({"F1", "1", "F1-1", "", "0", "", "Yes: inc.fwd", "C", "933", "1218", "+285", "Later", "+285"}));
	QCOMPARE(rowTexts(occurrences, 1), QStringList({"F2", "1", "F2-1", "", "0", "", "None recorded", "C", "1150", "1373", "+223", "Later", "+223"}));
	const QTableWidget* calls = table(window, "delayBaselineCalls");
	// the columns of the calls
	QCOMPARE(headerTexts(calls),
		QStringList({"Service", "Occurrence", "Station", "Journey order", "Call", "Final call", "Arrival: Baseline (s)", "Arrival: Scenario (s)",
			"Arrival: Additional delay (s)", "Arrival: Change", "Departure: Baseline (s)", "Departure: Scenario (s)",
			"Departure: Additional delay (s)", "Departure: Change"}));
	// the additional delay headers of the calls say what they mean
	QCOMPARE(headerTip(calls, "Arrival: Additional delay (s)"), "This run minus the baseline, same call");
	QCOMPARE(headerTip(calls, "Departure: Additional delay (s)"), "This run minus the baseline, same call");
	// a row for each call of each occurrence
	QCOMPARE(rows(calls), 6);
	// a call without a time in either run is not available, not 0
	QCOMPARE(rowTexts(calls, 0), QStringList({"F1", "1", "A", "1", "1", "No", "-", "-", "-", "Not available", "-", "-", "-", "Not available"}));
	// a call that did not change is Same with a difference of 0
	QCOMPARE(rowTexts(calls, 1), QStringList({"F1", "1", "B", "2", "1", "No", "331", "331", "0", "Same", "660", "660", "0", "Same"}));
	// F2 at B
	QCOMPARE(rowTexts(calls, 4), QStringList({"F2", "1", "B", "2", "1", "No", "817", "866", "+49", "Later", "877", "1013", "+136", "Later"}));
	// the final call
	QCOMPARE(rowTexts(calls, 5), QStringList({"F2", "1", "C", "3", "1", "Yes", "1150", "1373", "+223", "Later", "1151", "1374", "+223", "Later"}));
	// a compatible baseline shows no note above the totals
	QVERIFY(labelText(window, "delayBaselineNote").isEmpty());
	QVERIFY(!window.findChild<QLabel*>("delayBaselineNote")->isVisible());
	QVERIFY2(exportButton(window)->isEnabled(), "the tab can be exported");

	// The occurrence choice limits the occurrences, the calls and the totals. The station choice limits the calls only.
	QVERIFY2(setChecked(window, "delayAnalysisOccurrences", "F1 #1", false), "F1 is unchecked");
	// the totals are those of the chosen occurrences
	QCOMPARE(rows(occurrences), 1);
	QCOMPARE(cell(occurrences, 0, 0), "F2");
	QCOMPARE(rows(calls), 3);
	QCOMPARE(labelText(window, "delayBaselineTotals"), "1 train. At the final arrival: 1 later. Added delay +223 s, net +223 s.");
	// A is unchecked
	QVERIFY(setChecked(window, "delayAnalysisOccurrences", "F1 #1", true));
	QVERIFY(setChecked(window, "delayAnalysisStations", "A", false));
	// an unchecked station leaves the calls and not the occurrences
	QCOMPARE(rows(occurrences), 2);
	QCOMPARE(rows(calls), 4);
	QCOMPARE_LT(rowOf(calls, "F1", 2, "A"), 0);
	QVERIFY(labelText(window, "delayBaselineTotals").contains("Added delay +508 s,"));
	// nothing chosen shows no total and nothing to export
	QVERIFY(setChecked(window, "delayAnalysisOccurrences", "F1 #1", false));
	QVERIFY(setChecked(window, "delayAnalysisOccurrences", "F2 #1", false));
	QCOMPARE(labelText(window, "delayBaselineTotals"), "No train is selected.");
	QVERIFY(!exportButton(window)->isEnabled());

	// The events choice belongs to the other tabs.
	QVERIFY2(!window.findChild<QComboBox*>("delayAnalysisEvents")->isEnabled(), "the events choice is off on the baseline tab");
	showTab(window, 0);
	QVERIFY2(window.findChild<QComboBox*>("delayAnalysisEvents")->isEnabled(), "the events choice is on for the calls");
}

// The columns whose cells lie at the right, which are the columns of numbers: the first row of each table of the window.
void TestDelayAnalysisWindow::alignsNumberColumnsRight() {
	DelayAnalysisWindow window = levelZeroWindow();
	window.show();
	const auto rightColumns = [&window](const char* name) {
		QList<int> columns;
		const QTableWidget* shown = table(window, name);
		for (int column = 0; shown && shown->rowCount() > 0 && column < shown->columnCount(); ++column)
			if (shown->item(0, column)->textAlignment() & Qt::AlignRight)
				columns << column;
		return columns;
	};
	QList<int> summary;
	for (int column = 1; column < table(window, "delaySummary")->columnCount(); ++column)
		summary << column;
	// the calls: the occurrence, the journey order, the call and the times
	QCOMPARE(rightColumns("delayCalls"), QList<int>({1, 5, 6, 8, 9, 10}));
	// the summary: every column after the station
	QCOMPARE_GT(table(window, "delaySummary")->columnCount(), 13);
	QCOMPARE(rightColumns("delaySummary"), summary);
	// the trains against the baseline: the occurrence and the delays and times
	QCOMPARE(rightColumns("delayBaselineOccurrences"), QList<int>({1, 4, 8, 9, 10, 12}));
	// the calls against the baseline: the occurrence, the journey order, the call and the times
	QCOMPARE(rightColumns("delayBaselineCalls"), QList<int>({1, 3, 4, 6, 7, 8, 10, 11, 12}));
}

// The pair at level 3: only F1 has direct evidence. F1 at C 933 to 1218 (+285), F2 1021 to 1249 (+228), total 513.
// The pair of an entrance delay of 120 s at P for train X (P, Q, R): the input delay 120 at P, the additional delay at R +140
// and the secondary delay 140 - 120 = 20.
void TestDelayAnalysisWindow::showsEvidenceAndInputDelay() {
	DelayAnalysisWindow level3(lineSnapshot("signal-failure-forward", {331, 660, 1218, 1219, 688, 748, 1249, 1250}, {"inc.fwd"}),
		lineSnapshot("baseline", {331, 660, 933, 934, 688, 748, 1021, 1022}));
	showTab(level3, 3);
	const QTableWidget* occurrences = table(level3, "delayBaselineOccurrences");
	// only F1 has evidence and F2 is later without it
	QCOMPARE(rowTexts(occurrences, 0)[6], "Yes: inc.fwd");
	QCOMPARE(rowTexts(occurrences, 1)[6], "None recorded");
	QCOMPARE(cell(occurrences, 1, 10), "+228");
	QVERIFY2(labelText(level3, "delayBaselineTotals").contains("Added delay +513 s, net +513 s."), "the total at level 3");

	DelayRunSnapshot baseline = snapshot("baseline",
		{makeRow("X", 1, "P", 1, 1, {kNone, 100.0, kNone, 100.0}), makeRow("X", 1, "Q", 2, 1, {500.0, 560.0, 450.0, 560.0}),
			makeRow("X", 1, "R", 3, 1, {1000.0, kNone, 950.0, 951.0})},
		{trainResult("X", 1)});
	DelayRunSnapshot entrance = snapshot("entrance-delay",
		{makeRow("X", 1, "P", 1, 1, {kNone, 220.0, kNone, 220.0}), makeRow("X", 1, "Q", 2, 1, {500.0, 560.0, 570.0, 680.0}),
			makeRow("X", 1, "R", 3, 1, {1000.0, kNone, 1090.0, 1091.0})},
		{trainResult("X", 1, 120.0, "P")});
	entrance.hasEntranceDelays = true;
	DelayAnalysisWindow window(entrance, baseline);
	showTab(window, 3);
	const QTableWidget* entranceOccurrences = table(window, "delayBaselineOccurrences");
	// the entrance delay is the input delay and the secondary delay is the additional delay minus it
	QCOMPARE(rows(entranceOccurrences), 1);
	QCOMPARE(rowTexts(entranceOccurrences, 0),
		QStringList({"X", "1", "X-1", "", "120", "P", "None recorded", "R", "950", "1090", "+140", "Later", "+20"}));
	// the departure at the delayed stop is 120 s later
	QCOMPARE(rowTexts(table(window, "delayBaselineCalls"), 0),
		QStringList({"X", "1", "P", "1", "1", "No", "-", "-", "-", "Not available", "100", "220", "+120", "Later"}));

	// The delay of the calls against the authored plan: the planned departure at P is 100 and not the moved 220.
	showTab(window, 0);
	chooseEvents(window, 1);
	const QTableWidget* calls = table(window, "delayCalls");
	// the calls measure the entrance delay against the authored plan
	QCOMPARE(rowTexts(calls, 0), QStringList({"X", "1", "X-1", "", "P", "1", "1", "Departure", "100", "220", "+120", "Late", "Available"}));
}

// F2 never arrives at C in the incident run (level 5). F1: arrival at C 933 to 1274 (+341). F2's final arrival has no difference,
// so the totals leave it out and say so. When no occurrence has a difference the sums do not exist and are not 0.
void TestDelayAnalysisWindow::showsMissingCall() {
	DelayAnalysisWindow window(lineSnapshot("signal-failure-forward", {331, 1001, 1274, 1275, 1238, 1298, kNone, kNone}, {"inc.fwd"}),
		lineSnapshot("baseline", {331, 660, 933, 934, 897, 957, 1230, 1231}));
	showTab(window, 3);
	const QTableWidget* occurrences = table(window, "delayBaselineOccurrences");
	// a missing arrival has no difference and no secondary delay
	QCOMPARE(rowTexts(occurrences, 1), QStringList({"F2", "1", "F2-1", "", "0", "", "None recorded", "C", "1230", "-", "-", "Not available", "-"}));
	// the totals are incomplete
	QCOMPARE(labelText(window, "delayBaselineTotals"),
		"2 trains. At the final arrival: 1 later, 1 not available. Added delay +341 s, net +341 s. Sums leave out trains without a final "
		"arrival.");
	// the call that was not reached is not available
	QCOMPARE(cell(table(window, "delayBaselineCalls"), 5, 8), "-");
	QCOMPARE(cell(table(window, "delayBaselineCalls"), 5, 9), "Not available");

	DelayAnalysisWindow none(lineSnapshot("signal-failure-forward", {331, 660, kNone, kNone, 688, 748, kNone, kNone}),
		lineSnapshot("baseline", {331, 660, 933, 934, 688, 748, 1021, 1022}));
	showTab(none, 3);
	const QString totals = labelText(none, "delayBaselineTotals");
	// without a difference the sums are not available, not 0
	QVERIFY(totals.contains("At the final arrival: 2 not available."));
	QVERIFY(totals.contains("Added delay -, net -."));
	QVERIFY(!totals.contains(" 0 s"));
}

// A baseline that cannot be used: every reason is a row with its sentence. Y is only in the baseline, W only in this
// run and Z has other stations.
void TestDelayAnalysisWindow::listsEveryIncompatibility() {
	const CallTimes departureAtP = {kNone, 100.0, kNone, 100.0};
	const CallTimes arrivalAtLast = {200.0, kNone, 200.0, kNone};
	DelayRunSnapshot baseline = snapshot("run",
		{makeRow("X", 1, "P", 1, 1, departureAtP), makeRow("Y", 1, "P", 1, 1, departureAtP), makeRow("Z", 1, "P", 1, 1, departureAtP),
			makeRow("Z", 1, "Q", 2, 1, arrivalAtLast)},
		{trainResult("X", 1), trainResult("Y", 1), trainResult("Z", 1)});
	DelayRunSnapshot scenario = snapshot("run",
		{makeRow("X", 1, "P", 1, 1, departureAtP), makeRow("Z", 1, "P", 1, 1, departureAtP), makeRow("Z", 1, "R", 2, 1, arrivalAtLast),
			makeRow("W", 1, "P", 1, 1, departureAtP)},
		{trainResult("X", 1), trainResult("Z", 1), trainResult("W", 1)});
	scenario.caseRevision = "rev-2";
	scenario.baseTimeSeconds = 60.0;
	scenario.durationSeconds = 1400.0;
	scenario.timestep = 2.0;
	baseline.hasIncidents = true;
	baseline.hasEntranceDelays = true;
	DelayAnalysisWindow window(scenario, baseline);
	window.show();
	showTab(window, 3);
	const QTableWidget* diagnostics = table(window, "delayBaselineDiagnostics");
	// an incompatible baseline shows the reasons and no difference
	QVERIFY(diagnostics->isVisible());
	QVERIFY(!window.findChild<QWidget*>("delayBaselineOccurrences")->isVisible());
	// all ten reasons are listed, not the first
	QCOMPARE(headerTexts(diagnostics), QStringList({"Reason"}));
	QCOMPARE(rows(diagnostics), 10);
	const std::pair<DelayDiagnosticKind, const char*> reasons[] = {{DelayDiagnosticKind::ScenarioIdsEqual, ""},
		{DelayDiagnosticKind::SceneRevisionDiffers, ""}, {DelayDiagnosticKind::BaseTimeDiffers, ""}, {DelayDiagnosticKind::DurationDiffers, ""},
		{DelayDiagnosticKind::TimestepDiffers, ""}, {DelayDiagnosticKind::BaselineHasIncidents, ""},
		{DelayDiagnosticKind::BaselineHasEntranceDelays, ""}, {DelayDiagnosticKind::OccurrenceOnlyInBaseline, "Y"},
		{DelayDiagnosticKind::CallsDiffer, "Z"}, {DelayDiagnosticKind::OccurrenceOnlyInScenario, "W"}};
	for (int index = 0; index < 10; ++index) {
		// the sentence of each reason, in a fixed order, without its code
		QCOMPARE(cell(diagnostics, index, 0), delayDiagnosticText({reasons[index].first, reasons[index].second, 1}));
	}
	// the sentences name the trains
	QCOMPARE(cell(diagnostics, 0, 0), "Both runs have the same scenario id. A run cannot be compared with itself.");
	QCOMPARE(cell(diagnostics, 7, 0), "Train Y #1 is in the baseline run only.");
	QCOMPARE(cell(diagnostics, 8, 0), "Train Z #1 has other calls in the two runs: other stations, another journey order or another call number.");
	QCOMPARE(cell(diagnostics, 9, 0), "Train W #1 is in this run only.");
	// the note says so and there is nothing to export
	QCOMPARE(labelText(window, "delayBaselineNote"), "This run cannot be compared with the baseline:");
	QVERIFY(!exportButton(window)->isEnabled());
	// The choice of occurrences does not hide a reason.
	// the reasons do not depend on the choice
	QVERIFY(setChecked(window, "delayAnalysisOccurrences", "X #1", false));
	QCOMPARE(rows(diagnostics), 10);
}

void TestDelayAnalysisWindow::saysThatNoBaselineIsSet() {
	DelayAnalysisWindow window(lineSnapshot("follow-level-0", kFollowLevel0), std::nullopt);
	window.show();
	showTab(window, 3);
	// without a baseline the tab says so
	QCOMPARE(labelText(window, "delayBaselineNote"),
		"No baseline is set. In Run Results, click Set delay baseline after a run without incidents or entrance delays.");
	QVERIFY(!table(window, "delayBaselineDiagnostics")->isVisible());
	QVERIFY(!window.findChild<QWidget*>("delayBaselineOccurrences")->isVisible());
	QVERIFY2(!exportButton(window)->isEnabled(), "there is nothing to export on the baseline tab");
	showTab(window, 0);
	QVERIFY2(exportButton(window)->isEnabled(), "the calls can be exported");
}

void TestDelayAnalysisWindow::opensOnCallsTab() {
	DelayAnalysisWindow window = levelZeroWindow();
	window.show();
	QApplication::processEvents();
	// the window opens on the Calls tab and offers delay_analysis.csv
	QCOMPARE(tabs(window)->currentIndex(), 0);
	QCOMPARE(window.suggestedFileName(), "delay_analysis.csv");
	window.showBaselineTab();
	// showBaselineTab selects Against baseline, which offers delay_comparison.csv
	QCOMPARE(tabs(window)->currentIndex(), 3);
	QCOMPARE(window.suggestedFileName(), "delay_comparison.csv");
	showTab(window, 1);
	// the Summary tab offers delay_analysis.csv
	QCOMPARE(window.suggestedFileName(), "delay_analysis.csv");
	showTab(window, 2);
	// the Punctuality tab offers delay_histogram.csv
	QCOMPARE(window.suggestedFileName(), "delay_histogram.csv");
}

void TestDelayAnalysisWindow::describesReasonsStatusesAndTotals() {
	const DelayDiagnosticKind kinds[] = {DelayDiagnosticKind::ScenarioIdMissing, DelayDiagnosticKind::ScenarioIdsEqual,
		DelayDiagnosticKind::SceneRevisionDiffers, DelayDiagnosticKind::BaseTimeDiffers, DelayDiagnosticKind::DurationDiffers,
		DelayDiagnosticKind::TimestepDiffers, DelayDiagnosticKind::BaselineHasIncidents, DelayDiagnosticKind::BaselineHasEntranceDelays,
		DelayDiagnosticKind::OccurrenceOnlyInBaseline, DelayDiagnosticKind::OccurrenceOnlyInScenario, DelayDiagnosticKind::CallsDiffer};
	QStringList sentences;
	for (const DelayDiagnosticKind kind : kinds) {
		DelayDiagnostic diagnostic;
		diagnostic.kind = kind;
		diagnostic.serviceId = "S";
		diagnostic.occurrence = 2;
		const QString sentence = delayDiagnosticText(diagnostic);
		// every reason has its own sentence
		QVERIFY(!sentence.isEmpty());
		QVERIFY(!sentences.contains(sentence));
		sentences << sentence;
	}
	QVERIFY2(delayDiagnosticText({DelayDiagnosticKind::CallsDiffer, "S", 2}).contains("S #2"), "the occurrence is named");
	// the statuses
	QCOMPARE(delayStatusText(TimetableValueStatus::Available), "Available");
	QCOMPARE(delayStatusText(TimetableValueStatus::NoPlan), "No plan");
	QCOMPARE(delayStatusText(TimetableValueStatus::NotReached), "Not reached");
	QCOMPARE(delayStatusText(TimetableValueStatus::NoPlanAndNotReached), "No plan, not reached");
	// the classes
	QCOMPARE(delayClassText(DelayClass::Early), "Early");
	QCOMPARE(delayClassText(DelayClass::OnTime), "On time");
	QCOMPARE(delayClassText(DelayClass::Late), "Late");
	QCOMPARE(delayClassText(DelayClass::Unavailable), "Not available");
	// the changes
	QCOMPARE(delayChangeText(DelayChange::Later), "Later");
	QCOMPARE(delayChangeText(DelayChange::Same), "Same");
	QCOMPARE(delayChangeText(DelayChange::Earlier), "Earlier");
	QCOMPARE(delayChangeText(DelayChange::NotAvailable), "Not available");

	// The totals of a few occurrences are those of the comparison when all are given, and unavailable when there are none.
	const DelayBaselineComparison comparison = compareAgainstBaseline(lineSnapshot("baseline", kFollowLevel0),
		lineSnapshot("signal-failure-forward", {331, 660, 1218, 1219, 866, 1013, 1373, 1374}, {"inc.fwd"}));
	const BaselineTotals all = baselineTotals(comparison.occurrences);
	// the totals of all occurrences
	QVERIFY(comparison.compatible);
	QCOMPARE(all.occurrences, comparison.totals.occurrences);
	QCOMPARE(all.later, 2);
	QVERIFY(all.positiveSeconds.available);
	QCOMPARE_EQ(all.positiveSeconds.value, 508.0);
	QCOMPARE_EQ(all.netSeconds.value, 508.0);
	QVERIFY(all.complete);
	const BaselineTotals firstOnly = baselineTotals({comparison.occurrences[0]});
	// the totals of one occurrence
	QCOMPARE(firstOnly.occurrences, 1);
	QCOMPARE_EQ(firstOnly.positiveSeconds.value, 285.0);
	QVERIFY(firstOnly.complete);
	const BaselineTotals nobody = baselineTotals({});
	// no occurrence gives no sum, not 0
	QCOMPARE(nobody.occurrences, 0);
	QVERIFY(!nobody.positiveSeconds.available);
	QVERIFY(!nobody.netSeconds.available);
	QVERIFY(!nobody.complete);
}

void TestDelayAnalysisWindow::exportsEveryTab() {
	QTemporaryDir directory;
	QVERIFY2(directory.isValid(), "temporary directory");
	DelayAnalysisWindow window = levelZeroWindow();
	window.show();
	QApplication::processEvents();

	// Calls: the visible rows, in the words of the window. The file is the text of delayCallsCsv for the arrivals.
	const QString callsPath = QDir(directory.path()).filePath("calls.csv");
	QVERIFY2(window.exportCsvTo(callsPath), "the calls are written");
	QStringList written = lines(callsPath);
	std::vector<DelayEvent> arrivals;
	for (const DelayEvent& event : buildDelayEvents(lineSnapshot("signal-failure-forward", {331, 660, 1218, 1219, 866, 1013, 1373, 1374}, {"inc.fwd"}).timetable,
			 lineSnapshot("signal-failure-forward", {331, 660, 1218, 1219, 866, 1013, 1373, 1374}, {"inc.fwd"}).run)) {
		if (event.kind == DelayEventKind::Arrival)
			arrivals.push_back(event);
	}
	// the file is the text of the module for the visible events
	QCOMPARE(fileText(callsPath).toStdString(), delayCallsCsv(arrivals));
	// a header and a record for each arrival
	QCOMPARE(written.size(), 7);
	QCOMPARE(written[0], "Service,Occurrence,Train,Operating code,Station,Journey order,Call,Event,Planned[s],Simulated[s],Delay[s],Class,Status");
	// a missing value is an empty field and the delay of it is empty, not 0
	QCOMPARE(written.value(1), "F1,1,F1-1,,A,1,1,Arrival,,,,Not available,\"No plan, not reached\"");
	QCOMPARE(written.value(2), "F1,1,F1-1,,B,2,1,Arrival,600,331,-269,Early,Available");
	// the sidecar names the run
	QCOMPARE(sidecar(callsPath).value("run").toObject().value("case_name").toString(), "Case A");
	QCOMPARE(sidecar(callsPath).value("run").toObject().value("applied_scenario").toString(), "signal-failure-forward");
	QVERIFY(!sidecar(callsPath).contains("baseline_run"));
	QCOMPARE(sidecar(callsPath).value("artifact").toObject().value("kind").toString(), "csv");

	// The export follows the choice.
	QVERIFY2(setChecked(window, "delayAnalysisOccurrences", "F1 #1", false), "F1 is unchecked");
	// the file holds the chosen occurrence only
	QVERIFY(window.exportCsvTo(callsPath));
	QCOMPARE(lines(callsPath).size(), 4);
	QVERIFY(lines(callsPath).value(1).startsWith("F2,1,F2-1,,A,"));
	QVERIFY2(setChecked(window, "delayAnalysisOccurrences", "F1 #1", true), "F1 is checked");

	// Summary.
	showTab(window, 1);
	const QString summaryPath = QDir(directory.path()).filePath("summary.csv");
	QVERIFY2(window.exportCsvTo(summaryPath), "the summary is written");
	written = lines(summaryPath);
	// the header of the summary has a count and a percentage for each threshold
	QCOMPARE(written.size(), 5);
	QCOMPARE(written[0],
		"Record type,Event,Station,Calls,Available,No plan,Not reached,\"No plan, not reached\",Early,On time,Late,Sum[s],Mean[s],Minimum[s],Maximum[s],"
		"Later than 0 s,Later than 0 s[%],Later than 60 s,Later than 60 s[%],Later than 180 s,Later than 180 s[%],Later than 300 s,Later than 300 s[%]");
	// F1 is -269 at B and +118 at C, F2 +206 at B and +213 at C. B: sum -63, mean -31.5, 1 early, 1 late. A has no figure: its counts
	// are 0 and its shares are empty. Network: 4 available, sum 268, mean 67, 1 early and 3 late, 3 of 4 later than 0 and than 60 s,
	// 2 of 4 later than 180 s.
	// the records of the summary: counts, figures and shares, empty where there is none
	QCOMPARE(written.value(1), "station,Arrival,A,2,0,0,0,2,0,0,0,,,,,0,,0,,0,,0,");
	QCOMPARE(written.value(2), "station,Arrival,B,2,2,0,0,0,1,0,1,-63,-31.5,-269,206,1,50,1,50,1,50,0,0");
	QCOMPARE(written.value(4), "network,Arrival,,6,4,0,0,2,1,0,3,268,67,-269,213,3,75,3,75,2,50,0,0");
	// the summary has the provenance of the run
	QVERIFY(sidecar(summaryPath).contains("run"));
	QVERIFY(!sidecar(summaryPath).contains("baseline_run"));

	// Punctuality: a record for each class with its bounds, then the count of each status without a delay. The arrivals are
	// -269 (early), +118 (1 to 3 min late), +206 and +213 (3 to 5 min late); A has two without a plan or a time. The classes above
	// 180 s hold 2 of 4, as the summary has 2 later than 180 s.
	showTab(window, 2);
	const QString histogramPath = QDir(directory.path()).filePath("histogram.csv");
	QVERIFY2(window.exportCsvTo(histogramPath), "the punctuality is written");
	// the records of the punctuality
	QCOMPARE(lines(histogramPath),
		QStringList({"Event,Class,From[s],To[s],Events,Share[%]", "Arrival,Early,,0,1,25", "Arrival,On time,0,0,0,0",
			"Arrival,Up to 1 min late,0,60,0,0", "Arrival,1 to 3 min late,60,180,1,25", "Arrival,3 to 5 min late,180,300,2,50",
			"Arrival,Over 5 min late,300,,0,0", "Arrival,No plan,,,0,", "Arrival,Not reached,,,0,", "Arrival,\"No plan, not reached\",,,2,"}));
	// the punctuality has the provenance of the run
	QVERIFY(sidecar(histogramPath).contains("run"));
	QVERIFY(!sidecar(histogramPath).contains("baseline_run"));
	chooseEvents(window, 1);
	// the file follows the events choice
	QVERIFY(window.exportCsvTo(histogramPath));
	QCOMPARE(lines(histogramPath).value(5), "Departure,3 to 5 min late,180,300,1,50");
	QCOMPARE(lines(histogramPath).value(8), "Departure,Not reached,,,2,");
	chooseEvents(window, 0);

	// Against baseline: the provenance of both runs.
	showTab(window, 3);
	const QString baselinePath = QDir(directory.path()).filePath("baseline.csv");
	QVERIFY2(window.exportCsvTo(baselinePath), "the comparison is written");
	written = lines(baselinePath);
	// the header of the comparison
	QCOMPARE(written.size(), 10);
	QCOMPARE(written[0],
		"Record type,Service,Occurrence,Train,Operating code,Station,Journey order,Call,Final call,Baseline arrival[s],Scenario arrival[s],"
		"Arrival difference[s],Arrival change,Baseline departure[s],Scenario departure[s],Departure difference[s],Departure change,"
		"Input delay[s],Input delay station,Direct incident evidence,Incident IDs,Secondary delay[s],Occurrences,Later,Same,Earlier,"
		"Not available,Positive sum[s],Net sum[s],Complete");
	// an occurrence record holds the final arrival, the input delay, the evidence and the secondary delay
	QCOMPARE(written.value(1).split(','),
		record({{0, "occurrence"}, {1, "F1"}, {2, "1"}, {3, "F1-1"}, {5, "C"}, {6, "3"}, {7, "1"}, {9, "933"}, {10, "1218"}, {11, "285"},
			{12, "Later"}, {17, "0"}, {19, "yes"}, {20, "inc.fwd"}, {21, "285"}}));
	QCOMPARE(written.value(2).split(','),
		record({{0, "occurrence"}, {1, "F2"}, {2, "1"}, {3, "F2-1"}, {5, "C"}, {6, "3"}, {7, "1"}, {9, "1150"}, {10, "1373"}, {11, "223"},
			{12, "Later"}, {17, "0"}, {19, "no"}, {21, "223"}}));
	// a call record holds both differences and an empty field where a time does not exist
	QCOMPARE(written.value(3).split(','),
		record({{0, "call"}, {1, "F1"}, {2, "1"}, {3, "F1-1"}, {5, "A"}, {6, "1"}, {7, "1"}, {8, "no"}, {12, "Not available"}, {16, "Not available"}}));
	QCOMPARE(written.value(7).split(','),
		record({{0, "call"}, {1, "F2"}, {2, "1"}, {3, "F2-1"}, {5, "B"}, {6, "2"}, {7, "1"}, {8, "no"}, {9, "817"}, {10, "866"}, {11, "49"},
			{12, "Later"}, {13, "877"}, {14, "1013"}, {15, "136"}, {16, "Later"}}));
	// the total record
	QCOMPARE(written.value(9).split(','),
		record({{0, "total"}, {22, "2"}, {23, "2"}, {24, "0"}, {25, "0"}, {26, "0"}, {27, "508"}, {28, "508"}, {29, "yes"}}));
	const QJsonObject provenance = sidecar(baselinePath);
	// the sidecar of the comparison names both runs
	QCOMPARE(provenance.value("baseline_run").toObject().value("applied_scenario").toString(), "baseline");
	QCOMPARE(provenance.value("scenario_run").toObject().value("applied_scenario").toString(), "signal-failure-forward");
	QVERIFY(!provenance.contains("run"));

	// Nothing to write: no file and no sidecar. A path that cannot be written is a failure without a message box.
	// nothing is chosen
	QVERIFY(setChecked(window, "delayAnalysisOccurrences", "F1 #1", false));
	QVERIFY(setChecked(window, "delayAnalysisOccurrences", "F2 #1", false));
	const QString emptyPath = QDir(directory.path()).filePath("empty.csv");
	// a tab without a row writes nothing
	QVERIFY(!window.exportCsvTo(emptyPath));
	QVERIFY(!QFile::exists(emptyPath));
	QVERIFY(!QFile::exists(emptyPath + ".provenance.json"));
	QVERIFY2(setChecked(window, "delayAnalysisOccurrences", "F1 #1", true), "F1 is checked again");
	QVERIFY2(!window.exportCsvTo(QDir(directory.path()).filePath("missing/baseline.csv")), "a path that cannot be written is a failure");

	DelayAnalysisWindow noBaseline(lineSnapshot("follow-level-0", kFollowLevel0), std::nullopt);
	showTab(noBaseline, 3);
	const QString noBaselinePath = QDir(directory.path()).filePath("no-baseline.csv");
	// without a baseline there is nothing to write
	QVERIFY(!noBaseline.exportCsvTo(noBaselinePath));
	QVERIFY(!QFile::exists(noBaselinePath));
	// no text without a row
	QVERIFY(delayCallsCsv({}).empty());
	QVERIFY(delaySummaryCsv(DelaySummary(), DelayEventKind::Arrival).empty());
	QVERIFY(delayBaselineCsv(DelayBaselineComparison()).empty());
	QVERIFY(delayHistogramCsv(delayHistogram({}), DelayEventKind::Arrival).empty());
}

// After the save dialog of the export closes, cancelled or saved, the window is raised and active again, also when the
// main window took the activation while the dialog was open, as AppKit gives it on macOS.
void TestDelayAnalysisWindow::keepsWindowInFrontAfterExport() {
	QTemporaryDir directory;
	QMainWindow mainWindow;
	mainWindow.resize(500, 400);
	mainWindow.show();
	auto* window = new DelayAnalysisWindow(lineSnapshot("follow-level-0", kFollowLevel0), std::nullopt, &mainWindow);
	window->show();
	window->activateWindow();
	// the window is active before the export
	QVERIFY(directory.isValid());
	QVERIFY(exportButton(*window));
	QVERIFY(exportButton(*window)->isEnabled());
	QVERIFY(waitFor([window] { return QApplication::activeWindow() == window; }));
	const QString saved = QDir(directory.path()).filePath("front.csv");
	for (const QString& path : {QString(), saved}) {
		bool mainActive = false;
		QTimer::singleShot(0, window, [&mainWindow, &mainActive, &path] {
			for (QWidget* widget : QApplication::topLevelWidgets())
				if (auto* dialog = qobject_cast<QFileDialog*>(widget); dialog && dialog->isVisible()) {
					mainWindow.activateWindow();
					mainActive = waitFor([&mainWindow] { return QApplication::activeWindow() == &mainWindow; });
					if (path.isEmpty()) {
						dialog->reject();
					} else {
						dialog->setDirectory(QFileInfo(path).absolutePath());
						dialog->selectFile(QFileInfo(path).fileName());
						// Through QDialog, as QFileDialog::accept is protected.
						static_cast<QDialog*>(dialog)->accept();
					}
				}
		});
		exportButton(*window)->click();
		const bool active = waitFor([window] { return QApplication::activeWindow() == window; });
		const char* message = path.isEmpty() ? "a cancelled export leaves the window active, though the main window took the activation"
											 : "a saved export leaves the window active, though the main window took the activation";
		QVERIFY2(mainActive, message);
		QVERIFY2(active, message);
	}
	QVERIFY2(QFile::exists(saved), "the saved export is written");
}

// The export asks for a CSV file under the name delay_analysis.csv. A write that fails says so with the path as the
// person reads it, here in the Windows form.
void TestDelayAnalysisWindow::asksForCsvFileOnExport() {
	QTemporaryDir directory;
	DelayAnalysisWindow window(lineSnapshot("follow-level-0", kFollowLevel0), std::nullopt);
	window.show();
	QPushButton* button = exportButton(window);
	// the window has its export
	QVERIFY(directory.isValid());
	if (button == nullptr)
		QFAIL("the window has its export");
	QVERIFY(button->isEnabled());
	const QChar separator = display_path::separator();
	display_path::separator() = QLatin1Char('\\');
	const QString path = QDir(directory.path()).filePath("missing/delays.csv");
	const ExportDialog shown = answerExport(button, path);
	const QString expected = "Could not save the data to:\n" + display_path::format(path);
	display_path::separator() = separator;
	// the export asks for a CSV file
	QCOMPARE(shown.title, "Export Data");
	QCOMPARE(shown.filters, QStringList("CSV File (*.csv)"));
	QCOMPARE(shown.suggested, "delay_analysis.csv");
	// a file that cannot be written is named as the person reads it
	QCOMPARE(shown.warnings, QStringList(expected));
}

QTEST_MAIN(TestDelayAnalysisWindow)
#include "test_delayanalysiswindow.moc"
