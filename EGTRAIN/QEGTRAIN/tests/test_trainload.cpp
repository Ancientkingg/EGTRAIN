#include "app/DispatchController.h"
#include "diagrams/CompletedRunData.h"
#include "diagrams/DiagramKey.h"
#include "diagrams/DiagramWindow.h"
#include "diagrams/ResultFrame.h"
#include "diagrams/RouteCharts.h"
#include "diagrams/RouteDiagramData.h"
#include "diagrams/SeriesMeta.h"
#include "diagrams/TrainColors.h"
#include "diagrams/TrainKeyList.h"
#include "diagrams/TrainLoadData.h"
#include "scene/SceneModel.h"
#include "simulation/InitialParameters.h"
#include "simulation/SimulationRun.h"
#include "util/CsvWriter.h"
#ifdef signals
#undef signals
#endif

#include <QApplication>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QPalette>
#include <QPixmap>
#include <QRadioButton>
#include <QTemporaryDir>
#include <QTest>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>

#include <cmath>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

// A small SimulationRun with one 10 km route, three stops and a populated StopCallLog.
std::unique_ptr<SimulationRun> buildSmallPassengerRun() {
	auto run = std::make_unique<SimulationRun>();
	run->settings.times = 80.0;
	run->settings.startingSimulationTime = 28800; // 08:00:00
	run->settings.passengerOnboardCrowdingThreshold = 0.7;

	Route route;
	route.ID = "route0";
	route.N_Block_Sections = 2;
	route.sequence_of_block_sections.resize(2);

	Node delft;
	delft.sceneNodeId = "n0";
	delft.X = 0.0;
	delft.station = true;
	delft.stationName = "Delft";
	Node schiedam;
	schiedam.sceneNodeId = "n1";
	schiedam.X = 5.0;
	schiedam.station = true;
	schiedam.stationName = "Schiedam";
	Node rotterdam;
	rotterdam.sceneNodeId = "n2";
	rotterdam.X = 10.0;
	rotterdam.station = true;
	rotterdam.stationName = "Rotterdam";

	route.sequence_of_block_sections[0].ID = "b0";
	route.sequence_of_block_sections[0].start_node = delft;
	route.sequence_of_block_sections[0].end_node = schiedam;
	route.sequence_of_block_sections[0].total_arcs = 0;

	route.sequence_of_block_sections[1].ID = "b1";
	route.sequence_of_block_sections[1].start_node = schiedam;
	route.sequence_of_block_sections[1].end_node = rotterdam;
	route.sequence_of_block_sections[1].total_arcs = 0;
	run->infra.routes.push_back(route);

	run->numTrains = 1;
	run->trains.resize(1);
	Train& train = run->trains[0];
	train.run_ = run.get();
	train.trainDescription = "IC 100";
	train.serviceId = "IC";
	train.indexOfRoute = 0;
	train.TrainRouteID = "route0";
	train.MAX_OnBoard_Passengers = 100;
	train.numStations = 3;
	train.Stations = {delft, schiedam, rotterdam};
	train.Stations[0].StepStopped = 10;
	train.Stations[1].StepStopped = 10;
	train.Stations[2].StepStopped = 5;
	train.StationStandStarts[0] = 10.0;
	train.StationDepartures[0] = 20.0;
	train.StationStandStarts[1] = 40.0;
	train.StationDepartures[1] = 50.0;
	train.StationStandStarts[2] = 70.0;
	train.StationDepartures[2] = -1.0;

	train.earliestActiveTrajectoryIndex = 0;
	train.End_Time = 80;
	train.instant_spatial_position.resize(81, 0.0);
	train.instant_train_speed.resize(81, 0.0);
	for (int t = 0; t <= 80; ++t) {
		double km = 0.0;
		if (t <= 20)
			km = 0.0;
		else if (t <= 40)
			km = 5.0 * static_cast<double>(t - 20) / 20.0;
		else if (t <= 50)
			km = 5.0;
		else if (t <= 70)
			km = 5.0 + 5.0 * static_cast<double>(t - 50) / 20.0;
		else
			km = 10.0;
		train.instant_spatial_position[static_cast<std::size_t>(t)] = km * 1000.0;
	}

	// Mark that the run simulated passengers and fill StopCallLog directly.
	run->passengers.emplace_back();
	StopCallCounts& atDelft = run->stopCalls.at(0, 0);
	atDelft.boarded = 40;
	atDelft.loadAfter = 40;
	StopCallCounts& atSchiedam = run->stopCalls.at(0, 1);
	atSchiedam.alighted = 10;
	atSchiedam.boarded = 55;
	atSchiedam.leftBehind = 5;
	atSchiedam.loadAfter = 85;
	StopCallCounts& atRotterdam = run->stopCalls.at(0, 2);
	atRotterdam.alighted = 85;
	atRotterdam.loadAfter = 0;

	return run;
}

void moveTo(QChartView* view, const QPointF& value, QAbstractSeries* series) {
	const QPoint position = view->mapFromScene(view->chart()->mapToScene(view->chart()->mapToPosition(value, series)));
	QMouseEvent event(QEvent::MouseMove, position, view->viewport()->mapToGlobal(position), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
	QApplication::sendEvent(view->viewport(), &event);
}

int distinctColours(const QImage& image) {
	std::set<QRgb> seen;
	for (int y = 0; y < image.height(); y += 4)
		for (int x = 0; x < image.width(); x += 4)
			seen.insert(image.pixel(x, y));
	return static_cast<int>(seen.size());
}

} // namespace

class TestTrainLoad : public QObject {
	Q_OBJECT

private slots:
	void buildsSegmentsFromStopCallLog();
	void handlesZeroCapacityAndMissingRoute();
	void coloursAdaptToThresholdAndBackground();
	void rendersOffscreenWindowHoversAndExportsPng();
	void rendersPaimpolPassengerLoad();
};

void TestTrainLoad::buildsSegmentsFromStopCallLog() {
	const std::unique_ptr<SimulationRun> run = buildSmallPassengerRun();
	RunProvenance provenance;
	provenance.caseName = "SmallLine";
	const CompletedRunData captured = captureCompletedRun(*run, nullptr, 28800, provenance);

	QVERIFY(captured.settings.hasPassengers);
	QCOMPARE(captured.settings.passengerOnboardCrowdingThreshold, 0.7);
	QCOMPARE(captured.trains.size(), std::size_t(1));
	QCOMPARE(captured.trains[0].passengerCapacity, 100);
	QCOMPARE(captured.trains[0].stops.size(), std::size_t(3));
	QVERIFY(captured.trains[0].stops[1].hasStopCall);
	QCOMPARE(captured.trains[0].stops[1].loadAfter, 85);
	QCOMPARE(captured.trains[0].stops[1].leftBehind, 5);

	const std::optional<TrainLoadDiagramData> data = trainLoadDiagramData(captured, 0);
	if (!data) QFAIL("expected train load diagram data for route 0");
	QCOMPARE(data->crowdingThreshold, 0.7);
	QVERIFY(data->hasPassengers);
	// Four contiguous segments: before Delft (0..10), Delft->Schiedam (10..40), Schiedam->Rotterdam (40..70), after Rotterdam (70..80).
	QCOMPARE(data->segments.size(), std::size_t(4));

	const TrainLoadSegment& entry = data->segments[0];
	QVERIFY(!entry.hasCall);
	QCOMPARE(entry.fromStation, std::string());
	QCOMPARE(entry.toStation, std::string("Delft"));
	QCOMPARE(entry.load, 0);
	QCOMPARE(entry.points.front().seconds, 0.0);
	QCOMPARE(entry.points.back().seconds, 10.0);

	const TrainLoadSegment& firstLeg = data->segments[1];
	QVERIFY(firstLeg.hasCall);
	QCOMPARE(firstLeg.fromStation, std::string("Delft"));
	QCOMPARE(firstLeg.toStation, std::string("Schiedam"));
	QCOMPARE(firstLeg.load, 40);
	QCOMPARE(firstLeg.capacity, 100);
	QCOMPARE(firstLeg.occupancyRate(), 0.4);
	QVERIFY(!firstLeg.isCrowded(data->crowdingThreshold));
	QCOMPARE(firstLeg.boarded, 40);
	QCOMPARE(firstLeg.points.front().seconds, 10.0);
	QCOMPARE(firstLeg.points.back().seconds, 40.0);
	// Adjacent segments share the boundary point so the path has no gap.
	QCOMPARE(firstLeg.points.front().positionKm, entry.points.back().positionKm);

	const TrainLoadSegment& crowdedLeg = data->segments[2];
	QVERIFY(crowdedLeg.hasCall);
	QCOMPARE(crowdedLeg.fromStation, std::string("Schiedam"));
	QCOMPARE(crowdedLeg.toStation, std::string("Rotterdam"));
	QCOMPARE(crowdedLeg.load, 85);
	QCOMPARE(crowdedLeg.occupancyRate(), 0.85);
	QVERIFY(crowdedLeg.isCrowded(data->crowdingThreshold));
	QCOMPARE(crowdedLeg.alighted, 10);
	QCOMPARE(crowdedLeg.boarded, 55);
	QCOMPARE(crowdedLeg.leftBehind, 5);
	QCOMPARE(crowdedLeg.points.front().seconds, 40.0);
	QCOMPARE(crowdedLeg.points.back().seconds, 70.0);
	QCOMPARE(crowdedLeg.points.front().positionKm, firstLeg.points.back().positionKm);

	const TrainLoadSegment& terminus = data->segments[3];
	QVERIFY(terminus.hasCall);
	QCOMPARE(terminus.fromStation, std::string("Rotterdam"));
	QCOMPARE(terminus.toStation, std::string());
	QCOMPARE(terminus.load, 0);
	QCOMPARE(terminus.alighted, 85);
	QCOMPARE(terminus.points.front().seconds, 70.0);
	QCOMPARE(terminus.points.back().seconds, 80.0);

	// CSV export includes segments for visible trains and omits hidden ones.
	const std::string csvAll = buildTrainLoadCsv(*data, {"IC 100"});
	QVERIFY(csvAll.find("Train,Reference route,Source route,From station,To station") != std::string::npos);
	const std::string expectedRow =
		"Schiedam,Rotterdam,40,70,85,100," + csv::formatDouble(85.0 / 100.0) + ",yes,55,10,5";
	QVERIFY2(csvAll.find(expectedRow) != std::string::npos, csvAll.c_str());
	QVERIFY(buildTrainLoadCsv(*data, {"Other"}).empty());
}

void TestTrainLoad::handlesZeroCapacityAndMissingRoute() {
	const std::unique_ptr<SimulationRun> run = buildSmallPassengerRun();
	run->trains[0].MAX_OnBoard_Passengers = 0;
	const CompletedRunData captured = captureCompletedRun(*run, nullptr, 0, RunProvenance());

	QVERIFY(!trainLoadDiagramData(captured, -1).has_value());
	QVERIFY(!trainLoadDiagramData(captured, 5).has_value());

	const std::optional<TrainLoadDiagramData> data = trainLoadDiagramData(captured, 0);
	if (!data) QFAIL("expected data for route 0");
	QVERIFY(!data->segments.empty());
	QVERIFY(data->segments[1].occupancyRate() < 0.0);
	QVERIFY(!data->segments[1].isCrowded(data->crowdingThreshold));

	const RouteDiagramData emptyCalls;
	const RouteChart built = buildTrainLoadChart(*data, emptyCalls, 80.0, false);
	const std::unique_ptr<QChart> chart(built.chart);
	QCOMPARE(chart->series().size(), 4);
	auto* leg = qobject_cast<QLineSeries*>(chart->series()[1]);
	if (leg == nullptr) QFAIL("expected QLineSeries");
	QCOMPARE(leg->pen().style(), Qt::DotLine);
	QCOMPARE(seriesMeta(leg).keyKind.value_or(QString()), QStringLiteral("No capacity"));
	QVERIFY(seriesMeta(leg).inspectionInterval.contains(QStringLiteral("Load: 40 (no capacity)")));
}

void TestTrainLoad::coloursAdaptToThresholdAndBackground() {
	QPalette light;
	light.setColor(QPalette::Base, QColor(Qt::white));
	QPalette dark;
	dark.setColor(QPalette::Base, QColor(0x1e, 0x1e, 0x1e));
	QVERIFY(!isDarkBackground(light));
	QVERIFY(isDarkBackground(dark));

	for (const bool darkBg : {false, true}) {
		const QColor low = passengerLoadColour(0.0, 0.7, darkBg);
		const QColor atThreshold = passengerLoadColour(0.7, 0.7, darkBg);
		const QColor crowded = passengerLoadColour(0.75, 0.7, darkBg);
		const QColor full = passengerLoadColour(1.0, 0.7, darkBg);
		// Cool blue at/below threshold (blue > red), warm amber/crimson above threshold (red > blue).
		QVERIFY(low.blue() > low.red());
		QVERIFY(atThreshold.blue() > atThreshold.red());
		QVERIFY(crowded.red() > crowded.blue());
		QVERIFY(full.red() > full.blue());
		if (darkBg) {
			QVERIFY(low.lightness() > 80);
			QVERIFY(full.lightness() > 100);
		} else {
			QVERIFY(low.lightness() < 210);
			QVERIFY(full.lightness() < 90);
		}
	}
	// Non-colour secondary cue: thicker line above crowding threshold and at full capacity.
	QCOMPARE(passengerLoadLineWidth(0.0, 0.7), 2.5);
	QCOMPARE(passengerLoadLineWidth(0.7, 0.7), 2.5);
	QCOMPARE(passengerLoadLineWidth(0.71, 0.7), 4.0);
	QCOMPARE(passengerLoadLineWidth(1.0, 0.7), 4.5);
	QCOMPARE(passengerLoadLineWidth(-1.0, 0.7), 2.0);
}

void TestTrainLoad::rendersOffscreenWindowHoversAndExportsPng() {
	const std::unique_ptr<SimulationRun> run = buildSmallPassengerRun();
	RunProvenance provenance;
	provenance.caseName = "SmallLine";
	const CompletedRunData captured = captureCompletedRun(*run, nullptr, 28800, provenance);
	const std::optional<TrainLoadDiagramData> data = trainLoadDiagramData(captured, 0);
	if (!data) QFAIL("expected train load diagram data");
	const RouteDiagramData emptyCalls;
	RouteChart built = buildTrainLoadChart(*data, emptyCalls, captured.settings.horizonSeconds(), false);

	DiagramWindow window("Passenger load");
	window.setPresentation("Passenger load", "SmallLine \u00b7 " + built.referenceRoute);
	window.setChart(built.chart);
	if (built.stationAxis) window.setStationAxis(built.stationAxis, std::move(built.stationTicks));
	window.setLoadScale(DiagramLoadScale{data->crowdingThreshold, false});
	window.setTimeAxisY(true, captured.settings.startOffsetSeconds);

	QString switchedTo;
	window.setDisplayModes(
		{
			{QStringLiteral("paths"), QStringLiteral("Train paths"), true, QString()},
			{QStringLiteral("load"), QStringLiteral("Passenger load"), true, QString()},
		},
		QStringLiteral("load"),
		[&switchedTo](const QString& modeId) { switchedTo = modeId; });

	window.setMaximumSize(1200, 720);
	window.resize(1200, 720);
	window.show();
	QApplication::processEvents();

	auto* key = window.findChild<DiagramKey*>("diagramKey");
	if (key == nullptr) QFAIL("expected DiagramKey");
	QVERIFY(key->isVisible());
	QVERIFY(key->loadScale().has_value());
	QVERIFY(key->property("kinds").toStringList().contains(QStringLiteral("Crowded above 70% (thicker line)")));

	auto* loadRadio = window.findChild<QRadioButton*>("diagramMode_load");
	auto* pathsRadio = window.findChild<QRadioButton*>("diagramMode_paths");
	if (loadRadio == nullptr || pathsRadio == nullptr) QFAIL("expected display mode radio buttons");
	QVERIFY(loadRadio->isChecked());
	QVERIFY(!pathsRadio->isChecked());

	// Hover the crowded Schiedam -> Rotterdam segment (series index 2, midpoint around 7.5 km, 60 s).
	auto* view = window.findChild<QChartView*>();
	if (view == nullptr) QFAIL("expected QChartView");
	QAbstractSeries* crowdedSeries = view->chart()->series().value(2);
	if (crowdedSeries == nullptr) QFAIL("expected crowded segment series");
	moveTo(view, QPointF(7.5, 60.0), crowdedSeries);
	QApplication::processEvents();
	auto* tooltip = window.findChild<QLabel*>("diagramTooltip");
	if (tooltip == nullptr) QFAIL("expected diagramTooltip");
	QVERIFY(tooltip->isVisible());
	QVERIFY2(tooltip->text().contains(QStringLiteral("IC 100")), qPrintable(tooltip->text()));
	QVERIFY2(tooltip->text().contains(QStringLiteral("Schiedam to Rotterdam")), qPrintable(tooltip->text()));
	QVERIFY2(tooltip->text().contains(QStringLiteral("Load: 85 / 100 (85%, crowded)")), qPrintable(tooltip->text()));
	QVERIFY2(tooltip->text().contains(QStringLiteral("Left behind: 5")), qPrintable(tooltip->text()));

	// Exported PNG figure renders cleanly at 2x DPR with both the chart and the load key column.
	const QImage figure = window.exportedFigure();
	QVERIFY(!figure.isNull());
	QVERIFY(figure.width() > 1000);
	QVERIFY(figure.height() > 600);
	QVERIFY(distinctColours(figure) > 20);
	const std::optional<std::string> encoded = pngBytes(figure);
	QVERIFY(encoded.has_value() && encoded->size() > 1000);

	// Dark palette also renders cleanly with distinct colours.
	QPalette dark = window.palette();
	dark.setColor(QPalette::Base, QColor(0x1e, 0x1e, 0x1e));
	dark.setColor(QPalette::Window, QColor(0x25, 0x25, 0x26));
	dark.setColor(QPalette::WindowText, QColor(0xeb, 0xeb, 0xeb));
	window.setPalette(dark);
	QApplication::processEvents();
	const QImage darkFigure = window.exportedFigure();
	QVERIFY(!darkFigure.isNull());
	QVERIFY(distinctColours(darkFigure) > 20);

	// Switching display mode emits the callback and preserves visible axis ranges via visibleRange/setVisibleRange.
	window.setVisibleRange(Qt::Horizontal, 2.0, 8.0);
	const auto xRange = window.visibleRange(Qt::Horizontal);
	QVERIFY(xRange.has_value());
	QCOMPARE(xRange->first, 2.0);
	QCOMPARE(xRange->second, 8.0);
	pathsRadio->click();
	QApplication::processEvents();
	QCOMPARE(switchedTo, QStringLiteral("paths"));
}

void TestTrainLoad::rendersPaimpolPassengerLoad() {
	QTemporaryDir outputDir;
	QVERIFY(outputDir.isValid());
	const std::string sceneDir = std::string(EGTRAIN_SCENES_DIR) + "/Paimpol";
	const SceneLoadResult loaded = loadScene(sceneDir);
	QVERIFY2(!hasErrors(loaded.diagnostics), "Paimpol loads cleanly");

	initial_variables.GUI = 0;
	initial_variables.PAX_GUI = false;
	initial_variables.exportDetailedTrajectories = false;
	initial_variables.OutputMainFolder = outputDir.path().toStdString();
	initial_variables.InputMainFolder.clear();

	std::ostringstream sink;
	std::streambuf* saved = std::cout.rdbuf(sink.rdbuf());
	const std::vector<SceneDiagnostic> diagnostics = simulation.prepareScene(loaded.scene);
	if (!hasErrors(diagnostics)) {
		simulation.setSnapshotsEnabled(false);
		simulation.runSimulation();
	}
	std::cout.rdbuf(saved);
	QVERIFY2(!hasErrors(diagnostics), "Paimpol prepares cleanly");
	QVERIFY(simulation.run().numTrains > 0);

	RunProvenance provenance;
	provenance.caseName = loaded.scene.name;
	provenance.appliedScenario = "baseline";
	const CompletedRunData captured =
		captureCompletedRun(simulation.run(), &loaded.scene, initial_variables.startingSimulationTime, provenance);
	QVERIFY(captured.settings.hasPassengers);
	const auto used = usedRouteReferencesFrom(captured);
	QVERIFY(!used.empty());
	const int refIndex = used.front().first;

	const std::optional<TrainLoadDiagramData> data = trainLoadDiagramData(captured, refIndex);
	if (!data) QFAIL("expected load diagram data for Paimpol");
	bool anyNonZeroLoad = false;
	bool anyCrowded = false;
	for (const TrainLoadSegment& segment : data->segments) {
		anyNonZeroLoad = anyNonZeroLoad || segment.load > 0;
		anyCrowded = anyCrowded || segment.isCrowded(data->crowdingThreshold);
	}
	QVERIFY2(anyNonZeroLoad, "Paimpol must have segments with on-board passengers");
	QVERIFY2(anyCrowded, "Paimpol must have crowded segments above the threshold");

	const std::optional<RouteDiagramData> stops = routeDiagramData(captured, true, refIndex);
	const RouteDiagramData emptyCalls;
	RouteChart built = buildTrainLoadChart(*data, stops ? *stops : emptyCalls, captured.settings.horizonSeconds(), false);

	DiagramWindow window("Passenger load");
	window.setPresentation("Passenger load",
		QStringLiteral("Paimpol / baseline \u00b7 ") + built.referenceRoute);
	window.setChart(built.chart);
	if (built.stationAxis) window.setStationAxis(built.stationAxis, std::move(built.stationTicks));
	window.setTrainServices(trainServicesForRun(captured.results));
	window.setLoadScale(DiagramLoadScale{data->crowdingThreshold, false});
	window.setDisplayModes(
		{
			{QStringLiteral("paths"), QStringLiteral("Train paths"), true, QString()},
			{QStringLiteral("load"), QStringLiteral("Passenger load"), true, QString()},
		},
		QStringLiteral("load"), {});
	window.setTimeAxisY(true, captured.settings.startOffsetSeconds);
	window.setMaximumSize(1200, 720);
	window.resize(1200, 720);
	window.show();
	for (int round = 0; round < 4; ++round) {
		QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
		QApplication::processEvents();
	}

	const QImage figure = window.exportedFigure();
	QVERIFY(!figure.isNull());
	QVERIFY(distinctColours(figure) > 25);

	const QString savePath = qEnvironmentVariable("EGTRAIN_SAVE_PAIMPOL_LOAD_PNG");
	if (!savePath.isEmpty())
		QVERIFY(figure.save(savePath));
	const QString windowSavePath = qEnvironmentVariable("EGTRAIN_SAVE_PAIMPOL_LOAD_WINDOW_PNG");
	if (!windowSavePath.isEmpty())
		QVERIFY(window.grab().save(windowSavePath));
}

QTEST_MAIN(TestTrainLoad)
#include "test_trainload.moc"
