// Tests of passenger run summary in Run Results, the Compare passengers button,
// PassengerComparisonWindow (summary figures, differences and top-20 OD ranking)
// and CSV export with provenance sidecars.

#include "app/RunController.h"
#include "app/ResultViews.h"
#include "app/RunResultsDock.h"
#include "app/SceneSession.h"
#include "diagrams/PassengerComparison.h"
#include "diagrams/PassengerComparisonWindow.h"
#include "diagrams/ResultFrame.h"
#include "editors/SceneDocument.h"

#include <QApplication>
#include <QFile>
#include <QGraphicsView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
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

} // namespace

class TestPassengerComparison : public QObject {
	Q_OBJECT

private slots:
	void summaryShownOnlyWhenRunHasPassengers();
	void comparePassengersButtonStateAndOpensWindow();
	void comparisonWindowShowsFiguresAndDifferences();
	void odRankingSortsByAbsoluteLostChangeAndCapsAtTwenty();
	void exportsPassengerSummaryAndComparisonCsvWithProvenance();
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
	summary.missedConnections = 2;
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
	QVERIFY2(text.contains("total +450 s, mean +50 s, 0.25 passenger-hours lost"), qPrintable(text));
	QVERIFY2(text.contains("2 missed connections, 3 planned again, 4 left behind (3 passengers)"), qPrintable(text));
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
	base.missedConnections = 1;
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
	cur.missedConnections = 4;
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
	QCOMPARE(rowTexts(summaryTable, 5), QStringList({"Total arrival delay (s)", "+180", "+900", "+720"}));
	QCOMPARE(rowTexts(summaryTable, 6), QStringList({"Mean arrival delay (s)", "+10", "+60", "+50"}));
	QCOMPARE(rowTexts(summaryTable, 7), QStringList({"Passenger-hours lost (pax-h)", "0.1", "0.35", "+0.25"}));
	QCOMPARE(rowTexts(summaryTable, 8), QStringList({"Missed connections", "1", "4", "+3"}));
	QCOMPARE(rowTexts(summaryTable, 9), QStringList({"Journeys planned again", "2", "6", "+4"}));
	QCOMPARE(rowTexts(summaryTable, 10), QStringList({"Left behind (refusals)", "1", "5", "+4"}));
	QCOMPARE(rowTexts(summaryTable, 11), QStringList({"Left behind (passengers)", "1", "3", "+2"}));

	// Numbers aligned right, metric name left.
	QVERIFY(!(summaryTable->item(0, 0)->textAlignment() & Qt::AlignRight));
	QVERIFY(summaryTable->item(0, 1)->textAlignment() & Qt::AlignRight);
	QVERIFY(summaryTable->item(0, 3)->textAlignment() & Qt::AlignRight);

	// Side panel figures match differences.
	QCOMPARE(window.findChild<ResultFigure*>("passengerComparisonHoursLost")->value(), QStringLiteral("+0.25 h"));
	QCOMPARE(window.findChild<ResultFigure*>("passengerComparisonMeanDelay")->value(), QStringLiteral("+50 s"));
	QCOMPARE(window.findChild<ResultFigure*>("passengerComparisonNotReached")->value(), QStringLiteral("+3"));

	// When no journey completed in the scenario run, total and mean delay are "-" (never 0).
	PassengerRunSummary emptyCompleted;
	emptyCompleted.journeys = 5;
	emptyCompleted.noRouteJourneys = 5;
	PassengerComparisonWindow noCompletedWindow(
		makeSnapshot("baseline", true, base), makeSnapshot("blackout", true, emptyCompleted));
	const auto* noCompletedTable = noCompletedWindow.findChild<QTableWidget*>("passengerComparisonSummary");
	if (noCompletedTable == nullptr)
		QFAIL("summary table missing");
	QCOMPARE(rowTexts(noCompletedTable, 5), QStringList({"Total arrival delay (s)", "+180", "-", "-"}));
	QCOMPARE(rowTexts(noCompletedTable, 6), QStringList({"Mean arrival delay (s)", "+10", "-", "-"}));
	QCOMPARE(noCompletedWindow.findChild<ResultFigure*>("passengerComparisonMeanDelay")->value(), QStringLiteral("-"));
}

void TestPassengerComparison::odRankingSortsByAbsoluteLostChangeAndCapsAtTwenty() {
	std::vector<OriginDestinationSummary> baseOds;
	std::vector<OriginDestinationSummary> curOds;

	// 25 OD pairs:
	// - "Z" -> "A": -2.0 pax-h change (largest absolute change, negative)
	// - "B" -> "C": +1.5 pax-h change (tied with "A" -> "D" and "A" -> "B")
	// - "A" -> "D": +1.5 pax-h change
	// - "A" -> "B": +1.5 pax-h change
	// - 21 filler pairs "S01".."S21" -> "T" with smaller changes 0.01..0.21 pax-h
	baseOds.push_back(makeOd("Z", "A", 10, 10, 720.0, 2.0));
	curOds.push_back(makeOd("Z", "A", 10, 10, 0.0, 0.0));

	baseOds.push_back(makeOd("B", "C", 5, 5, 0.0, 0.0));
	curOds.push_back(makeOd("B", "C", 6, 6, 900.0, 1.5));

	baseOds.push_back(makeOd("A", "D", 4, 4, 0.0, 0.0));
	curOds.push_back(makeOd("A", "D", 4, 4, 1350.0, 1.5));

	baseOds.push_back(makeOd("A", "B", 8, 8, 0.0, 0.0));
	curOds.push_back(makeOd("A", "B", 8, 8, 675.0, 1.5));

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
	// Capped at 20 rows.
	QCOMPARE(odTable->rowCount(), 20);
	// Rank 0: largest absolute change (-2.0 pax-h).
	QCOMPARE(rowTexts(odTable, 0), QStringList({"Z", "A", "10", "10", "+720", "0", "-720", "2", "0", "-2"}));
	// Ranks 1-3: tied at |1.5| pax-h, broken by origin then destination ascending: (A,B), (A,D), (B,C).
	QCOMPARE(cell(odTable, 1, 0), QStringLiteral("A"));
	QCOMPARE(cell(odTable, 1, 1), QStringLiteral("B"));
	QCOMPARE(cell(odTable, 2, 0), QStringLiteral("A"));
	QCOMPARE(cell(odTable, 2, 1), QStringLiteral("D"));
	QCOMPARE(cell(odTable, 3, 0), QStringLiteral("B"));
	QCOMPARE(cell(odTable, 3, 1), QStringLiteral("C"));
	QCOMPARE(rowTexts(odTable, 3), QStringList({"B", "C", "5", "6", "0", "+900", "+900", "0", "1.5", "+1.5"}));
	// Rank 4: largest filler S21 (0.21 pax-h); S05..S01 dropped beyond top 20.
	QCOMPARE(cell(odTable, 4, 0), QStringLiteral("S21"));
	QCOMPARE(cell(odTable, 19, 0), QStringLiteral("S06"));
}

void TestPassengerComparison::exportsPassengerSummaryAndComparisonCsvWithProvenance() {
	QTemporaryDir dir;
	QVERIFY(dir.isValid());

	// Single-run passenger summary CSV + provenance sidecar.
	PassengerRunSummary base;
	base.journeys = 4;
	base.completedJourneys = 3;
	base.noRouteJourneys = 1;
	base.totalDelaySeconds = 900.0;
	base.meanDelaySeconds = 300.0;
	base.passengerHoursLost = 0.25;
	base.missedConnections = 1;
	base.replannedJourneys = 2;
	base.leftBehindEvents = 2;
	base.refusedPassengers = 1;

	RunProvenance baseProvenance;
	baseProvenance.caseName = "PaxCase";
	baseProvenance.appliedScenario = "baseline";

	const QString summaryPath = dir.filePath(QStringLiteral("passenger_summary.csv"));
	const std::string summaryError = writeRunArtifactWithProvenance(
		summaryPath.toStdString(), "csv", passengerSummaryCsv(base), baseProvenance);
	QVERIFY2(summaryError.empty(), summaryError.c_str());

	const QStringList summaryRows = csvLines(summaryPath);
	QCOMPARE(summaryRows.size(), 13);
	QCOMPARE(summaryRows[0], QStringLiteral("Metric,Value,Unit"));
	QCOMPARE(summaryRows[1], QStringLiteral("Journeys,4,journeys"));
	QCOMPARE(summaryRows[3], QStringLiteral("Not reached (total),1,journeys"));
	QCOMPARE(summaryRows[6], QStringLiteral("Total arrival delay,900,s"));
	QCOMPARE(summaryRows[7], QStringLiteral("Mean arrival delay,300,s"));
	QCOMPARE(summaryRows[8], QStringLiteral("Passenger-hours lost,0.25,passenger-hours"));
	QCOMPARE(summaryRows[9], QStringLiteral("Missed connections,1,journeys"));
	QCOMPARE(summaryRows[10], QStringLiteral("Journeys planned again,2,journeys"));
	QCOMPARE(summaryRows[11], QStringLiteral("Left behind (refusals),2,refusals"));
	QCOMPARE(summaryRows[12], QStringLiteral("Left behind (passengers),1,passengers"));

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
	QVERIFY(cmpRows[0].startsWith(QStringLiteral("Record type,Metric,Unit,Baseline,Scenario,Difference,Origin,Destination")));
	QVERIFY(cmpRows[1].startsWith(QStringLiteral("summary,Journeys,journeys,4,4,0,")));
	QVERIFY(cmpRows[8].startsWith(QStringLiteral("summary,Passenger-hours lost,passenger-hours,0.25,0.5,0.25,")));
	QCOMPARE(cmpRows[13], QStringLiteral("od,,,,,,A,B,4,4,3,4,300,450,150,0.25,0.5,0.25"));

	const QJsonObject cmpSidecar = sidecarJson(comparisonPath);
	QCOMPARE(cmpSidecar.value("baseline_run").toObject().value("applied_scenario").toString(), QStringLiteral("baseline"));
	QCOMPARE(cmpSidecar.value("scenario_run").toObject().value("applied_scenario").toString(), QStringLiteral("incident"));
}

QTEST_MAIN(TestPassengerComparison)
#include "test_passengercomparison.moc"
