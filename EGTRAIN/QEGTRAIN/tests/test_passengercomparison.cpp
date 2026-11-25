// Tests of passenger run summary in Run Results, the Compare passengers button,
// PassengerComparisonWindow (summary figures, differences and the ranking of the
// origin-destination pairs that changed) and the CSV exports with their provenance sidecars.

#include "app/RunController.h"
#include "app/ResultViews.h"
#include "app/RunResultsDock.h"
#include "app/SceneSession.h"
#include "diagrams/PassengerComparison.h"
#include "diagrams/PassengerComparisonWindow.h"
#include "diagrams/ResultFrame.h"
#include "editors/SceneDocument.h"

#include <QApplication>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QStringList>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Friend of RunController to populate completed runs and baselines directly without starting a simulation worker.
class MainWindowTestAccess {
public:
	static void setCompletedRun(RunController& controller, std::shared_ptr<const CompletedRunData> run) {
		controller.m_completedRun = std::move(run);
		controller.m_state = controller.m_completedRun ? RunController::RunState::Completed : RunController::RunState::NotBuilt;
		controller.m_sceneChangedDuringRun = false;
	}
	static void setDelayBaseline(RunController& controller, std::optional<DelayRunSnapshot> baseline) {
		controller.m_delayBaseline = std::move(baseline);
	}
};

namespace {

class StubSessionHost : public SceneSession::Host {
public:
	telemetry::OperationObservation captureTelemetryOperation() const noexcept override { return {}; }
	bool dialogsSuppressed() const override { return true; }
	void commitPendingEdits() override {}
	bool afterRunStopped(std::function<void()>) override { return false; }
	void bringToFront() override {}
	void offerUpdateCheck() override {}
	QString recoveredSceneSource() const override { return {}; }
	SceneLoadResult readScene(const QString&, bool& identityOk) override {
		identityOk = true;
		return {};
	}
	bool sceneOpened(const SceneSession::OpenedScene&) override { return true; }
};

class StubRunHost : public RunController::Host {
public:
	telemetry::OperationObservation captureTelemetryOperation() const noexcept override { return {}; }
	bool dialogsSuppressed() const override { return true; }
	bool advancedDetailsEnabled() const override { return false; }
	bool telemetryActive() const override { return false; }
	int stepDelayMs() const override { return 0; }
	double serviceOccurrenceDuration() const override { return 3600.0; }
	int totalServiceOccurrences() const override { return 1; }
	const SceneScenario* selectedScenario() const override { return nullptr; }
	const std::vector<SceneIncident>& selectedScenarioIncidents() const override { return m_incidents; }
	QString scenarioContext() const override { return QStringLiteral("baseline"); }
	void validateScene() override {}
	void showLoadedData() override {}
	void showValidation() override {}
	void teardownForRun() override {}
	void invalidateRunResults() override {}
	std::vector<SceneDiagnostic> timedPreparation(const std::function<std::vector<SceneDiagnostic>()>& prepare, bool& identityOk) override {
		identityOk = true;
		return prepare();
	}
	void buildRunCanvas() override {}
	bool startupTimingHoldsRun(bool) override { return false; }
	bool endFollowing() override { return false; }
	bool runFinished() override { return false; }
	void replayReady(GuiReplayHistory) override {}

private:
	std::vector<SceneIncident> m_incidents;
};

class StubViewsHost : public ResultViews::Host {
public:
	explicit StubViewsHost(const SceneDocument& document) : m_document(document) {}
	telemetry::OperationObservation captureTelemetryOperation() const noexcept override { return {}; }
	bool dialogsSuppressed() const override { return true; }
	const SceneModel& sceneModel() const override { return m_document.model(); }
	void resultMessage(const QString&, int) override {}

private:
	const SceneDocument& m_document;
};

class StubDockHost : public RunResultsDock::Host {
public:
	telemetry::OperationObservation captureTelemetryOperation() const noexcept override { return {}; }
	bool advancedDetailsEnabled() const override { return false; }
};

struct DockFixture {
	QMainWindow window;
	SceneDocument document;
	StubSessionHost sessionHost;
	StubRunHost runHost;
	StubViewsHost viewsHost{document};
	StubDockHost dockHost;
	RunSelection selection;
	SceneSession* session = new SceneSession(&window, &document, sessionHost);
	RunController* controller = new RunController(&window, &document, session, selection, runHost);
	ResultViews* views = new ResultViews(&window, controller, viewsHost);
	RunResultsDock dock{&window, controller, views, dockHost};

	DockFixture() {
		auto* central = new QWidget(&window);
		auto* layout = new QVBoxLayout(central);
		layout->addWidget(new QGraphicsView(central));
		window.setCentralWidget(central);
		SceneModel scene = makeNewSceneModel();
		scene.name = "PaxCase";
		document.replace(scene, false);
		window.resize(1280, 800);
		window.show();
		QApplication::processEvents();
	}
};

CompletedRunData makeRunData(const std::string& scenario, bool hasPassengers, const PassengerRunSummary& summary = {},
	std::vector<OriginDestinationSummary> ods = {}) {
	CompletedRunData data;
	data.provenance.caseName = "PaxCase";
	data.provenance.appliedScenario = scenario;
	data.settings.caseName = "PaxCase";
	data.settings.hasPassengers = hasPassengers;
	data.passengerSummary = summary;
	data.originDestinations = std::move(ods);
	TrainRunResult train;
	train.trainId = "T1-1";
	data.results.trains.push_back(train);
	return data;
}

DelayRunSnapshot makeSnapshot(const std::string& scenario, bool hasPassengers, const PassengerRunSummary& summary = {},
	std::vector<OriginDestinationSummary> ods = {}) {
	DelayRunSnapshot snap;
	snap.caseRevision = "1";
	snap.scenarioId = scenario;
	snap.provenance.caseName = "PaxCase";
	snap.provenance.appliedScenario = scenario;
	snap.hasPassengers = hasPassengers;
	snap.passengerSummary = summary;
	snap.originDestinations = std::move(ods);
	TrainRunResult train;
	train.trainId = "T1-1";
	snap.run.trains.push_back(train);
	return snap;
}

OriginDestinationSummary makeOd(const std::string& origin, const std::string& destination, int journeys, int completed,
	double meanDelay, double passengerHoursLost) {
	OriginDestinationSummary od;
	od.origin = origin;
	od.destination = destination;
	od.journeys = journeys;
	od.completed = completed;
	od.meanDelay = meanDelay;
	od.passengerHoursLost = passengerHoursLost;
	return od;
}

QString cell(const QTableWidget* table, int row, int column) {
	return table && table->item(row, column) ? table->item(row, column)->text() : QString();
}

QStringList rowTexts(const QTableWidget* table, int row) {
	QStringList values;
	for (int c = 0; table && c < table->columnCount(); ++c)
		values << cell(table, row, c);
	return values;
}

QStringList csvLines(const QString& path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return {};
	return QString::fromUtf8(file.readAll()).split("\r\n", Qt::SkipEmptyParts);
}

QJsonObject sidecarJson(const QString& path) {
	QFile file(path + QStringLiteral(".provenance.json"));
	return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
}

// Clicks `button` and answers its save dialog with `path`, closing any message that follows. Returns the file name that the
// dialog suggested.
QString answerSave(QPushButton* button, const QString& path) {
	QString suggested;
	bool answered = false;
	QTimer timer;
	QObject::connect(&timer, &QTimer::timeout, [&suggested, &answered, &path] {
		if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
			box->accept();
			return;
		}
		for (QWidget* widget : QApplication::topLevelWidgets())
			if (auto* dialog = qobject_cast<QFileDialog*>(widget); dialog && dialog->isVisible()) {
				auto* name = dialog->findChild<QLineEdit*>("fileNameEdit");
				if (answered || !name) {
					dialog->reject();
					continue;
				}
				answered = true;
				suggested = QFileInfo(dialog->selectedFiles().value(0)).fileName();
				name->setText(path);
				// Through QDialog, as QFileDialog::accept is protected.
				static_cast<QDialog*>(dialog)->accept();
			}
	});
	timer.start(0);
	button->click();
	return suggested;
}

} // namespace

class TestPassengerComparison : public QObject {
	Q_OBJECT

private slots:
	void summaryShownOnlyWhenRunHasPassengers();
	void comparePassengersButtonStateAndOpensWindow();
	void comparisonWindowShowsFiguresAndDifferences();
	void windowOpensWideEnoughForTheSummary();
	void odRankingListsChangedPairsAndCapsAtTwenty();
	void odTableSaysWhenNoPairChanged();
	void exportsPassengerSummaryAndComparisonCsvWithProvenance();
	void dockExportsPassengerCsvThroughItsButton();
};

void TestPassengerComparison::summaryShownOnlyWhenRunHasPassengers() {
	DockFixture fixture;

	// Run without passengers: nothing new is shown.
	MainWindowTestAccess::setCompletedRun(*fixture.controller, std::make_shared<const CompletedRunData>(makeRunData("baseline", false)));
	fixture.dock.refresh();
	fixture.dock.updateAvailability(true);
	QApplication::processEvents();
	QVERIFY(!fixture.dock.passengerSummaryLabel()->isVisible());
	QVERIFY(fixture.dock.passengerSummaryLabel()->text().isEmpty());
	QVERIFY(!fixture.dock.exportPassengerCsvButton()->isVisible());
	QVERIFY(!fixture.dock.comparePassengersButton()->isVisible());
	QVERIFY(!fixture.dock.comparePassengersButton()->isEnabled());

	// Run with passengers: summary and passenger buttons appear.
	PassengerRunSummary summary;
	summary.journeys = 12;
	summary.completedJourneys = 9;
	summary.noRouteJourneys = 2;
	summary.inProgressJourneys = 1;
	summary.totalDelaySeconds = 450.0;
	summary.meanDelaySeconds = 50.0;
	summary.passengerHoursLost = 0.25;
	summary.missedConnectionJourneys = 2;
	summary.replannedJourneys = 3;
	summary.leftBehindEvents = 4;
	summary.refusedPassengers = 3;
	MainWindowTestAccess::setCompletedRun(*fixture.controller,
		std::make_shared<const CompletedRunData>(makeRunData("disruption", true, summary)));
	fixture.dock.refresh();
	fixture.dock.updateAvailability(true);
	QApplication::processEvents();
	QVERIFY(fixture.dock.passengerSummaryLabel()->isVisible());
	const QString text = fixture.dock.passengerSummaryLabel()->text();
	QVERIFY2(text.contains("12 journeys (9 completed, 3 not reached: 2 no route, 1 in progress)"), qPrintable(text));
	QVERIFY2(text.contains("Delay of the completed journeys: total +450 s, mean +50 s, 0.25 passenger-hours lost"), qPrintable(text));
	QVERIFY2(text.contains("2 journeys with a missed connection, 3 planned again, 4 left behind (3 passengers)"), qPrintable(text));
	QVERIFY(fixture.dock.exportPassengerCsvButton()->isVisible());
	QVERIFY(fixture.dock.exportPassengerCsvButton()->isEnabled());
	QVERIFY(fixture.dock.comparePassengersButton()->isVisible());
	QVERIFY(!fixture.dock.comparePassengersButton()->isEnabled());
	QCOMPARE(fixture.dock.comparePassengersButton()->toolTip(), QStringLiteral("Set a baseline first"));
}

void TestPassengerComparison::comparePassengersButtonStateAndOpensWindow() {
	DockFixture fixture;

	PassengerRunSummary currentSummary;
	currentSummary.journeys = 10;
	currentSummary.completedJourneys = 10;
	currentSummary.totalDelaySeconds = 600.0;
	currentSummary.meanDelaySeconds = 60.0;
	currentSummary.passengerHoursLost = 600.0 / 3600.0;
	MainWindowTestAccess::setCompletedRun(*fixture.controller,
		std::make_shared<const CompletedRunData>(makeRunData("incident", true, currentSummary,
			{makeOd("A", "B", 10, 10, 60.0, 600.0 / 3600.0)})));

	// Baseline without passengers -> Compare passengers disabled with clear tooltip.
	MainWindowTestAccess::setDelayBaseline(*fixture.controller, makeSnapshot("baseline", false));
	fixture.dock.refresh();
	fixture.dock.updateAvailability(true);
	QApplication::processEvents();
	QVERIFY(fixture.dock.comparePassengersButton()->isVisible());
	QVERIFY(!fixture.dock.comparePassengersButton()->isEnabled());
	QCOMPARE(fixture.dock.comparePassengersButton()->toolTip(), QStringLiteral("The baseline run has no passengers"));

	// Baseline with passengers -> Compare passengers enabled and opens comparison window.
	PassengerRunSummary baseSummary;
	baseSummary.journeys = 10;
	baseSummary.completedJourneys = 10;
	baseSummary.totalDelaySeconds = 0.0;
	baseSummary.meanDelaySeconds = 0.0;
	baseSummary.passengerHoursLost = 0.0;
	MainWindowTestAccess::setDelayBaseline(*fixture.controller,
		makeSnapshot("baseline", true, baseSummary, {makeOd("A", "B", 10, 10, 0.0, 0.0)}));
	fixture.dock.refresh();
	fixture.dock.updateAvailability(true);
	QApplication::processEvents();
	QVERIFY(fixture.dock.comparePassengersButton()->isEnabled());

	fixture.dock.comparePassengersButton()->click();
	QApplication::processEvents();
	auto* opened = fixture.window.findChild<QDialog*>(QStringLiteral("passengerComparisonWindow"));
	if (opened == nullptr)
		QFAIL("Compare passengers did not open PassengerComparisonWindow");
	QVERIFY(opened->isVisible());
	QCOMPARE(opened->windowTitle(), QStringLiteral("Passenger comparison"));
	opened->close();
}

void TestPassengerComparison::comparisonWindowShowsFiguresAndDifferences() {
	PassengerRunSummary base;
	base.journeys = 20;
	base.completedJourneys = 18;
	base.noRouteJourneys = 1;
	base.inProgressJourneys = 1;
	base.totalDelaySeconds = 180.0;
	base.meanDelaySeconds = 10.0;
	base.passengerHoursLost = 0.1;
	base.missedConnectionJourneys = 1;
	base.replannedJourneys = 2;
	base.leftBehindEvents = 1;
	base.refusedPassengers = 1;

	PassengerRunSummary cur;
	cur.journeys = 20;
	cur.completedJourneys = 15;
	cur.noRouteJourneys = 3;
	cur.inProgressJourneys = 2;
	cur.totalDelaySeconds = 900.0;
	cur.meanDelaySeconds = 60.0;
	cur.passengerHoursLost = 0.35;
	cur.missedConnectionJourneys = 4;
	cur.replannedJourneys = 6;
	cur.leftBehindEvents = 5;
	cur.refusedPassengers = 3;

	PassengerComparisonWindow window(
		makeSnapshot("baseline", true, base, {makeOd("A", "B", 20, 18, 10.0, 0.1)}),
		makeSnapshot("incident", true, cur, {makeOd("A", "B", 20, 15, 60.0, 0.35)}));
	window.show();
	QApplication::processEvents();

	QCOMPARE(window.findChild<QLabel*>("passengerComparisonContext")->text(),
		QStringLiteral("PaxCase / incident · baseline PaxCase / baseline"));

	const auto* summaryTable = window.findChild<QTableWidget*>("passengerComparisonSummary");
	if (summaryTable == nullptr)
		QFAIL("summary table missing");
	QCOMPARE(summaryTable->rowCount(), 12);
	QCOMPARE(rowTexts(summaryTable, 0), QStringList({"Journeys", "20", "20", "0"}));
	QCOMPARE(rowTexts(summaryTable, 1), QStringList({"Completed journeys", "18", "15", "-3"}));
	QCOMPARE(rowTexts(summaryTable, 2), QStringList({"Not reached (total)", "2", "5", "+3"}));
	QCOMPARE(rowTexts(summaryTable, 3), QStringList({"Not reached: no route", "1", "3", "+2"}));
	QCOMPARE(rowTexts(summaryTable, 4), QStringList({"Not reached: in progress", "1", "2", "+1"}));
	QCOMPARE(rowTexts(summaryTable, 5), QStringList({"Total arrival delay of completed journeys (s)", "+180", "+900", "+720"}));
	QCOMPARE(rowTexts(summaryTable, 6), QStringList({"Mean arrival delay of completed journeys (s)", "+10", "+60", "+50"}));
	QCOMPARE(rowTexts(summaryTable, 7), QStringList({"Passenger-hours lost by completed journeys", "0.1", "0.35", "+0.25"}));
	QCOMPARE(rowTexts(summaryTable, 8), QStringList({"Journeys with a missed connection", "1", "4", "+3"}));
	QCOMPARE(rowTexts(summaryTable, 9), QStringList({"Journeys planned again", "2", "6", "+4"}));
	QCOMPARE(rowTexts(summaryTable, 10), QStringList({"Left behind (refusals)", "1", "5", "+4"}));
	QCOMPARE(rowTexts(summaryTable, 11), QStringList({"Left behind (passengers)", "1", "3", "+2"}));

	// Numbers aligned right, metric name left.
	QVERIFY(!(summaryTable->item(0, 0)->textAlignment() & Qt::AlignRight));
	QVERIFY(summaryTable->item(0, 1)->textAlignment() & Qt::AlignRight);
	QVERIFY(summaryTable->item(0, 3)->textAlignment() & Qt::AlignRight);

	// Side panel figures match differences, and the delays say that they count the completed journeys only.
	const auto* lost = window.findChild<ResultFigure*>("passengerComparisonHoursLost");
	const auto* mean = window.findChild<ResultFigure*>("passengerComparisonMeanDelay");
	QCOMPARE(lost->value(), QStringLiteral("+0.25 h"));
	QCOMPARE(lost->caption(), QStringLiteral("Passenger-hours lost"));
	QCOMPARE(mean->value(), QStringLiteral("+50 s"));
	QCOMPARE(mean->caption(), QStringLiteral("Mean delay"));
	QVERIFY(lost->toolTip().contains("completed journeys"));
	QVERIFY(mean->toolTip().contains("completed journeys"));
	// Both figures are in the section headed Completed journeys, so the heading right above them says what they count.
	const QWidget* section = lost->parentWidget()->parentWidget();
	QCOMPARE(mean->parentWidget(), lost->parentWidget());
	QStringList headings;
	for (const QLabel* label : section->findChildren<QLabel*>())
		if (label->property("resultSectionHeading").toBool())
			headings << label->text();
	QCOMPARE(headings, QStringList("Completed journeys"));
	QCOMPARE(window.findChild<ResultFigure*>("passengerComparisonNotReached")->value(), QStringLiteral("+3"));

	// The one pair changed; its delay columns say in their tooltips that they count the completed journeys only.
	const auto* odTable = window.findChild<QTableWidget*>("passengerComparisonOd");
	if (odTable == nullptr)
		QFAIL("OD table missing");
	QCOMPARE(odTable->rowCount(), 1);
	// The change that ranks the pairs and the change in journeys not reached come right after the pair.
	QCOMPARE(rowTexts(odTable, 0), QStringList({"A", "B", "+0.25", "+3", "20", "20", "2", "5", "+10", "+60", "+50", "0.1", "0.35"}));
	QCOMPARE(odTable->horizontalHeaderItem(2)->text(), QStringLiteral("Passenger-hours lost difference"));
	QCOMPARE(odTable->horizontalHeaderItem(3)->text(), QStringLiteral("Not reached difference"));
	for (const int column : {2, 8, 9, 10, 11, 12})
		QVERIFY2(odTable->horizontalHeaderItem(column)->toolTip().contains("completed journeys of the pair"),
			qPrintable(odTable->horizontalHeaderItem(column)->text()));

	// When no journey completed in the scenario run, total and mean delay are "-" (never 0).
	PassengerRunSummary emptyCompleted;
	emptyCompleted.journeys = 5;
	emptyCompleted.noRouteJourneys = 5;
	PassengerComparisonWindow noCompletedWindow(
		makeSnapshot("baseline", true, base), makeSnapshot("blackout", true, emptyCompleted));
	const auto* noCompletedTable = noCompletedWindow.findChild<QTableWidget*>("passengerComparisonSummary");
	if (noCompletedTable == nullptr)
		QFAIL("summary table missing");
	QCOMPARE(rowTexts(noCompletedTable, 5), QStringList({"Total arrival delay of completed journeys (s)", "+180", "-", "-"}));
	QCOMPARE(rowTexts(noCompletedTable, 6), QStringList({"Mean arrival delay of completed journeys (s)", "+10", "-", "-"}));
	QCOMPARE(noCompletedWindow.findChild<ResultFigure*>("passengerComparisonMeanDelay")->value(), QStringLiteral("-"));
}

// The window opens wide enough for the four columns of the summary beside the panel, and at least as wide as the Delay analysis
// window. The screen of the offscreen platform is narrower than that, so the test lifts the limit that the screen sets.
void TestPassengerComparison::windowOpensWideEnoughForTheSummary() {
	PassengerRunSummary base;
	base.journeys = 5050;
	base.completedJourneys = 1333;
	base.noRouteJourneys = 3441;
	base.inProgressJourneys = 276;
	base.totalDelaySeconds = 471226.0;
	base.meanDelaySeconds = 353.51;
	base.passengerHoursLost = 131.134;
	PassengerRunSummary cur = base;
	cur.completedJourneys = 1342;
	cur.noRouteJourneys = 3432;
	cur.totalDelaySeconds = 473512.0;
	cur.meanDelaySeconds = 352.84;
	cur.passengerHoursLost = 134.676;
	PassengerComparisonWindow window(makeSnapshot("baseline", true, base), makeSnapshot("incident", true, cur));
	const int fit = window.summaryFitWidth();
	QCOMPARE(window.width(), qMin(window.maximumWidth(), qMax(960, fit)));

	const auto* summary = window.findChild<QTableWidget*>("passengerComparisonSummary");
	if (summary == nullptr)
		QFAIL("summary table missing");
	const auto layOut = [&window](int width) {
		window.resize(width, 640);
		for (int round = 0; round < 4; ++round) {
			QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
			QApplication::processEvents();
		}
	};
	const int metricWidth = summary->columnWidth(0);
	window.setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
	window.show();
	for (const int width : {fit, qMax(960, fit)}) {
		layOut(width);
		QVERIFY2(!summary->horizontalScrollBar()->isVisible(), qPrintable(QString::number(width)));
	}
	// The metric column keeps the width of its longest name; the last column takes the room that is left.
	QCOMPARE(summary->columnWidth(0), metricWidth);
	// A little narrower than the width that fits, the columns scroll: the width is not more than the summary needs.
	if (fit < 1000) {
		layOut(fit - 8);
		QVERIFY2(summary->horizontalScrollBar()->isVisible(), qPrintable(QString::number(fit)));
	}
}

void TestPassengerComparison::odRankingListsChangedPairsAndCapsAtTwenty() {
	std::vector<OriginDestinationSummary> baseOds;
	std::vector<OriginDestinationSummary> curOds;

	// - "Z" -> "A": -2.0 pax-h change, the largest absolute change.
	// - "B" -> "C", "A" -> "D" and "A" -> "B": +1.5 pax-h each. "B" -> "C" also has two journeys more that did not reach C, so it
	//   comes first; the other two follow by origin and destination.
	// - "Q" -> "R": only in the baseline, so this run counts no journeys for it: -0.3 pax-h.
	// - "U" -> "V": the same in both runs, so it is not listed.
	// - 21 filler pairs "S01".."S21" -> "T" with changes of 0.01..0.21 pax-h.
	baseOds.push_back(makeOd("Z", "A", 10, 10, 720.0, 2.0));
	curOds.push_back(makeOd("Z", "A", 10, 10, 0.0, 0.0));

	baseOds.push_back(makeOd("B", "C", 5, 5, 0.0, 0.0));
	curOds.push_back(makeOd("B", "C", 6, 4, 1350.0, 1.5));

	baseOds.push_back(makeOd("A", "D", 4, 4, 0.0, 0.0));
	curOds.push_back(makeOd("A", "D", 4, 4, 1350.0, 1.5));

	baseOds.push_back(makeOd("A", "B", 8, 8, 0.0, 0.0));
	curOds.push_back(makeOd("A", "B", 8, 8, 675.0, 1.5));

	baseOds.push_back(makeOd("Q", "R", 2, 2, 540.0, 0.3));

	baseOds.push_back(makeOd("U", "V", 3, 3, 30.0, 0.025));
	curOds.push_back(makeOd("U", "V", 3, 3, 30.0, 0.025));

	for (int i = 1; i <= 21; ++i) {
		const std::string origin = std::string("S") + (i < 10 ? "0" : "") + std::to_string(i);
		baseOds.push_back(makeOd(origin, "T", 2, 2, 0.0, 0.0));
		curOds.push_back(makeOd(origin, "T", 2, 2, static_cast<double>(i) * 18.0, static_cast<double>(i) * 0.01));
	}

	PassengerComparisonWindow window(
		makeSnapshot("baseline", true, {}, std::move(baseOds)),
		makeSnapshot("incident", true, {}, std::move(curOds)));
	window.show();
	QApplication::processEvents();

	const auto* odTable = window.findChild<QTableWidget*>("passengerComparisonOd");
	if (odTable == nullptr)
		QFAIL("OD table missing");
	// 26 pairs changed; the table keeps 20.
	QCOMPARE(odTable->rowCount(), 20);
	QCOMPARE(rowTexts(odTable, 0), QStringList({"Z", "A", "-2", "0", "10", "10", "0", "0", "+720", "0", "-720", "2", "0"}));
	QCOMPARE(rowTexts(odTable, 1), QStringList({"B", "C", "+1.5", "+2", "5", "6", "0", "2", "0", "+1350", "+1350", "0", "1.5"}));
	QCOMPARE(cell(odTable, 2, 0) + cell(odTable, 2, 1), QStringLiteral("AB"));
	QCOMPARE(cell(odTable, 3, 0) + cell(odTable, 3, 1), QStringLiteral("AD"));
	QCOMPARE(rowTexts(odTable, 4), QStringList({"Q", "R", "-0.3", "0", "2", "0", "0", "0", "+540", "-", "-", "0.3", "0"}));
	QCOMPARE(cell(odTable, 5, 0), QStringLiteral("S21"));
	QCOMPARE(cell(odTable, 19, 0), QStringLiteral("S07"));
	for (int row = 0; row < odTable->rowCount(); ++row)
		QVERIFY2(cell(odTable, row, 0) != QStringLiteral("U"), "an unchanged pair is listed");
	QCOMPARE(window.comparison().changedPairs.size(), kMaxComparedOriginDestinations);
}

void TestPassengerComparison::odTableSaysWhenNoPairChanged() {
	const std::vector<OriginDestinationSummary> same = {makeOd("A", "B", 4, 4, 60.0, 0.05), makeOd("B", "A", 2, 1, 0.0, 0.0)};
	PassengerComparisonWindow window(makeSnapshot("baseline", true, {}, same), makeSnapshot("rerun", true, {}, same));
	window.show();
	QApplication::processEvents();

	const auto* odTable = window.findChild<QTableWidget*>("passengerComparisonOd");
	if (odTable == nullptr)
		QFAIL("OD table missing");
	QVERIFY(window.comparison().changedPairs.empty());
	QCOMPARE(odTable->rowCount(), 1);
	QCOMPARE(cell(odTable, 0, 0), QStringLiteral("No origin-destination pair changed between the two runs"));
	QCOMPARE(odTable->columnSpan(0, 0), odTable->columnCount());
	// The CSV has the summary records and no pair.
	const std::string csv = passengerComparisonCsv(window.comparison());
	QVERIFY(csv.find("\r\nod,") == std::string::npos);
}

void TestPassengerComparison::exportsPassengerSummaryAndComparisonCsvWithProvenance() {
	QTemporaryDir dir;
	QVERIFY(dir.isValid());

	// Single-run passenger CSV: the summary records and one record for each origin-destination pair.
	PassengerRunSummary base;
	base.journeys = 4;
	base.completedJourneys = 3;
	base.noRouteJourneys = 1;
	base.totalDelaySeconds = 900.0;
	base.meanDelaySeconds = 300.0;
	base.passengerHoursLost = 0.25;
	base.missedConnectionJourneys = 1;
	base.replannedJourneys = 2;
	base.leftBehindEvents = 2;
	base.refusedPassengers = 1;

	RunProvenance baseProvenance;
	baseProvenance.caseName = "PaxCase";
	baseProvenance.appliedScenario = "baseline";

	const QString summaryPath = dir.filePath(QStringLiteral("passenger_summary.csv"));
	const std::string summaryError = writeRunArtifactWithProvenance(summaryPath.toStdString(), "csv",
		passengerSummaryCsv(base, {makeOd("A", "B", 3, 3, 300.0, 0.25), makeOd("B", "A", 1, 0, -1.0, 0.0)}), baseProvenance);
	QVERIFY2(summaryError.empty(), summaryError.c_str());

	const QStringList summaryRows = csvLines(summaryPath);
	QCOMPARE(summaryRows.size(), 15); // header + 12 summary records + 2 pairs
	QCOMPARE(summaryRows[0], QStringLiteral("Record type,Metric,Unit,Value,Origin,Destination,Journeys,Completed,Not reached,"
											"Mean delay of completed journeys[s],Passenger-hours lost by completed journeys[pax-h]"));
	QCOMPARE(summaryRows[1], QStringLiteral("summary,Journeys,journeys,4,,,,,,,"));
	QCOMPARE(summaryRows[3], QStringLiteral("summary,Not reached (total),journeys,1,,,,,,,"));
	QCOMPARE(summaryRows[6], QStringLiteral("summary,Total arrival delay of completed journeys,s,900,,,,,,,"));
	QCOMPARE(summaryRows[7], QStringLiteral("summary,Mean arrival delay of completed journeys,s,300,,,,,,,"));
	QCOMPARE(summaryRows[8], QStringLiteral("summary,Passenger-hours lost by completed journeys,passenger-hours,0.25,,,,,,,"));
	QCOMPARE(summaryRows[9], QStringLiteral("summary,Journeys with a missed connection,journeys,1,,,,,,,"));
	QCOMPARE(summaryRows[10], QStringLiteral("summary,Journeys planned again,journeys,2,,,,,,,"));
	QCOMPARE(summaryRows[11], QStringLiteral("summary,Left behind (refusals),refusals,2,,,,,,,"));
	QCOMPARE(summaryRows[12], QStringLiteral("summary,Left behind (passengers),passengers,1,,,,,,,"));
	QCOMPARE(summaryRows[13], QStringLiteral("od,,,,A,B,3,3,0,300,0.25"));
	// No journey of B to A completed, so it has no mean delay.
	QCOMPARE(summaryRows[14], QStringLiteral("od,,,,B,A,1,0,1,,0"));

	const QJsonObject summarySidecar = sidecarJson(summaryPath);
	QCOMPARE(summarySidecar.value("artifact").toObject().value("kind").toString(), QStringLiteral("csv"));
	QCOMPARE(summarySidecar.value("run").toObject().value("applied_scenario").toString(), QStringLiteral("baseline"));

	// Comparison CSV + dual-run provenance sidecar.
	PassengerRunSummary cur = base;
	cur.completedJourneys = 4;
	cur.noRouteJourneys = 0;
	cur.totalDelaySeconds = 1800.0;
	cur.meanDelaySeconds = 450.0;
	cur.passengerHoursLost = 0.5;

	PassengerComparisonWindow window(
		makeSnapshot("baseline", true, base, {makeOd("A", "B", 4, 3, 300.0, 0.25)}),
		makeSnapshot("incident", true, cur, {makeOd("A", "B", 4, 4, 450.0, 0.5)}));
	const QString comparisonPath = dir.filePath(window.suggestedFileName());
	QVERIFY(window.exportCsvTo(comparisonPath));

	const QStringList cmpRows = csvLines(comparisonPath);
	QCOMPARE(cmpRows.size(), 14); // header + 12 summary rows + 1 OD row
	QCOMPARE(cmpRows[0], QStringLiteral("Record type,Metric,Unit,Baseline,Scenario,Difference,Origin,Destination,Baseline journeys,"
										"Scenario journeys,Baseline completed,Scenario completed,Baseline not reached,Scenario not reached,"
										"Baseline mean delay of completed journeys[s],Scenario mean delay of completed journeys[s],"
										"Mean delay difference[s],Baseline passenger-hours lost by completed journeys[pax-h],"
										"Scenario passenger-hours lost by completed journeys[pax-h],Passenger-hours lost difference[pax-h]"));
	QVERIFY(cmpRows[1].startsWith(QStringLiteral("summary,Journeys,journeys,4,4,0,")));
	QVERIFY(cmpRows[8].startsWith(QStringLiteral("summary,Passenger-hours lost by completed journeys,passenger-hours,0.25,0.5,0.25,")));
	QCOMPARE(cmpRows[13], QStringLiteral("od,,,,,,A,B,4,4,3,4,1,0,300,450,150,0.25,0.5,0.25"));

	const QJsonObject cmpSidecar = sidecarJson(comparisonPath);
	QCOMPARE(cmpSidecar.value("baseline_run").toObject().value("applied_scenario").toString(), QStringLiteral("baseline"));
	QCOMPARE(cmpSidecar.value("scenario_run").toObject().value("applied_scenario").toString(), QStringLiteral("incident"));
}

// Export passenger CSV... in Run Results writes the passengers of the completed run through the save dialog, with the provenance
// of that run beside it.
void TestPassengerComparison::dockExportsPassengerCsvThroughItsButton() {
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	DockFixture fixture;
	PassengerRunSummary summary;
	summary.journeys = 3;
	summary.completedJourneys = 2;
	summary.inProgressJourneys = 1;
	summary.totalDelaySeconds = 120.0;
	summary.meanDelaySeconds = 60.0;
	summary.passengerHoursLost = 0.25;
	summary.missedConnectionJourneys = 1;
	MainWindowTestAccess::setCompletedRun(*fixture.controller,
		std::make_shared<const CompletedRunData>(makeRunData("disruption", true, summary, {makeOd("A", "B", 3, 2, 60.0, 0.25)})));
	fixture.dock.refresh();
	fixture.dock.updateAvailability(true);
	QApplication::processEvents();
	QPushButton* button = fixture.dock.exportPassengerCsvButton();
	QVERIFY(button->isVisible() && button->isEnabled());

	const QString path = dir.filePath(QStringLiteral("exported.csv"));
	QCOMPARE(answerSave(button, path), QStringLiteral("passenger_summary.csv"));
	const QStringList rows = csvLines(path);
	QCOMPARE(rows.size(), 14); // header + 12 summary records + 1 pair
	QCOMPARE(rows[1], QStringLiteral("summary,Journeys,journeys,3,,,,,,,"));
	QCOMPARE(rows[9], QStringLiteral("summary,Journeys with a missed connection,journeys,1,,,,,,,"));
	QCOMPARE(rows[13], QStringLiteral("od,,,,A,B,3,2,1,60,0.25"));
	const QJsonObject sidecar = sidecarJson(path);
	QCOMPARE(sidecar.value("artifact").toObject().value("kind").toString(), QStringLiteral("csv"));
	QCOMPARE(sidecar.value("run").toObject().value("case_name").toString(), QStringLiteral("PaxCase"));
	QCOMPARE(sidecar.value("run").toObject().value("applied_scenario").toString(), QStringLiteral("disruption"));
}

QTEST_MAIN(TestPassengerComparison)
#include "test_passengercomparison.moc"
