#include "editors/CompositionPanel.h"
#include "editors/EntranceDelayPanel.h"
#include "editors/IncidentPanel.h"
#include "editors/InfrastructurePanel.h"
#include "editors/PassengerPanel.h"
#include "editors/ScenarioPanel.h"
#include "editors/ServicePanel.h"
#include "editors/TrainUnitPanel.h"
#include "editors/infrastructure/BlockRouteFacets.h"
#include "editors/infrastructure/SignalFacets.h"
#include "editors/infrastructure/StationFacets.h"
#include "editors/infrastructure/TopologyFacets.h"

#include "support/EditHistory.h"
#include "support/FakePrompts.h"

#include <QComboBox>
#include <QList>
#include <QListWidget>
#include <QLoggingCategory>
#include <QPushButton>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QVariant>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

static SceneStop stopAt(const char* stationId) {
	SceneStop stop;
	stop.stationId = stationId;
	return stop;
}

// One item of every kind that an editor deletes, and nothing that uses the second scenario, composition, rolling stock
// unit and service: a track with two nodes, an arc and a block, the stations A, with a platform, and B, the units u1 and
// u2, the compositions c1 and c2, the services s1 over A and B and s2, the default scenario base with a breakdown of s1
// and an entrance delay of s1, the scenario alt, and the passenger p1 with the journey j1, whose leg l1 rides s1.
static SceneModel deleteScene() {
	SceneModel scene;
	scene.name = "Deletes";
	scene.tracks = {{"T1"}};
	scene.nodes = {{"n1", "T1", 0.0, 0.0}, {"n2", "T1", 1.0, 0.0}};
	scene.arcs = {{"a1", "T1", "n1", "n2", 0.0, 0.0, 20.0}};
	scene.blocks = {{"b1", "T1", 1.0}};
	SceneStation a;
	a.id = "A";
	a.platforms = {{"P1", {"n1"}}};
	SceneStation b;
	b.id = "B";
	scene.stations = {a, b};
	SceneTrainUnit u1;
	u1.id = "u1";
	SceneTrainUnit u2;
	u2.id = "u2";
	scene.trainUnits = {u1, u2};
	scene.compositions = {{"c1", {}}, {"c2", {}}};
	SceneService s1;
	s1.id = "s1";
	s1.stops = {stopAt("A"), stopAt("B")};
	SceneService s2;
	s2.id = "s2";
	scene.services = {s1, s2};
	SceneScenario base;
	base.id = "base";
	base.name = "Baseline";
	base.incidents = {{"i1", "train_breakdown", "s1", 10.0, 20.0}};
	SceneEntranceDelay delay;
	delay.serviceId = "s1";
	delay.stationId = "A";
	delay.delaySeconds = 30.0;
	base.entranceDelays = {delay};
	SceneScenario alt;
	alt.id = "alt";
	alt.name = "Alternative";
	scene.scenarios = {base, alt};
	scene.defaultScenarioId = "base";
	ScenePassengerLeg leg;
	leg.id = "l1";
	leg.serviceId = "s1";
	leg.originStationId = "A";
	leg.destinationStationId = "B";
	ScenePassengerJourney journey;
	journey.id = "j1";
	journey.originStationId = "A";
	journey.destinationStationId = "B";
	journey.legs = {leg};
	ScenePassenger passenger;
	passenger.id = "p1";
	passenger.journeys = {journey};
	scene.passengers = {passenger};
	return scene;
}

// A panel that shows the scene with one item selected, and the Delete button of that item.
struct Opened {
	std::unique_ptr<SceneEditorPanel> panel;
	QPushButton* button = nullptr;
};

using Open = std::function<Opened(SceneDocument&, UserPrompts&, RunSelection&)>;
using Count = std::function<std::size_t(const SceneModel&)>;

// One kind of item that a Delete button removes.
struct DeleteCase {
	const char* name;
	// Makes the panel on the document, which shows deleteScene(), with the prompts and the run selection, and selects the
	// item.
	Open open;
	// The number of items of the kind in the scene.
	Count count;
	// The status message after the delete.
	QString message;
};

// A panel with the prompts and the run selection on the document.
template <typename Panel>
static std::unique_ptr<Panel> makePanel(SceneDocument& document, UserPrompts& prompts, RunSelection& selection) {
	auto panel = std::make_unique<Panel>();
	panel->setPrompts(&prompts);
	panel->setRunSelection(&selection);
	panel->setDocument(&document);
	return panel;
}

// A panel that lists its items in listWidget(), with the item in `row` selected.
template <typename Panel>
static Open listRow(int row) {
	return [row](SceneDocument& document, UserPrompts& prompts, RunSelection& selection) {
		std::unique_ptr<Panel> panel = makePanel<Panel>(document, prompts, selection);
		panel->listWidget()->setCurrentRow(row);
		QPushButton* button = panel->deleteButton();
		return Opened{std::move(panel), button};
	};
}

// The passenger panel with p1 selected, and with j1 from `depth` 1 and l1 from 2. The button deletes the deepest of them.
static Open passengerRow(int depth) {
	return [depth](SceneDocument& document, UserPrompts& prompts, RunSelection& selection) {
		std::unique_ptr<PassengerPanel> panel = makePanel<PassengerPanel>(document, prompts, selection);
		panel->passengerList()->setCurrentRow(0);
		if (depth > 0)
			panel->journeyList()->setCurrentRow(0);
		if (depth > 1)
			panel->legList()->setCurrentRow(0);
		QPushButton* button = depth == 0 ? panel->deletePassengerButton()
			: depth == 1				 ? panel->deleteJourneyButton()
										 : panel->deleteLegButton();
		return Opened{std::move(panel), button};
	};
}

// The infrastructure panel with the facets of the window, listing `facet`, with the row that Add appends selected. Nothing
// uses a new row.
static Open addedInfrastructureRow(const char* facet) {
	return [facet](SceneDocument& document, UserPrompts& prompts, RunSelection& selection) {
		auto panel = std::make_unique<InfrastructurePanel>();
		for (auto* makeFacets : {&makeTopologyFacets, &makeBlockRouteFacets, &makeStationFacets, &makeSignalFacets})
			for (std::unique_ptr<InfrastructureFacet>& made : (*makeFacets)())
				panel->setFacet(std::move(made));
		panel->setPrompts(&prompts);
		panel->setRunSelection(&selection);
		panel->setDocument(&document);
		panel->facetCombo()->setCurrentIndex(panel->facetCombo()->findData(facet));
		panel->addButton()->click();
		QPushButton* button = panel->deleteButton();
		return Opened{std::move(panel), button};
	};
}

static std::size_t incidentCount(const SceneModel& model) {
	std::size_t count = 0;
	for (const SceneScenario& scenario : model.scenarios)
		count += scenario.incidents.size();
	return count;
}

static std::size_t entranceDelayCount(const SceneModel& model) {
	std::size_t count = 0;
	for (const SceneScenario& scenario : model.scenarios)
		count += scenario.entranceDelays.size();
	return count;
}

static std::size_t journeyCount(const SceneModel& model) {
	std::size_t count = 0;
	for (const ScenePassenger& passenger : model.passengers)
		count += passenger.journeys.size();
	return count;
}

static std::size_t legCount(const SceneModel& model) {
	std::size_t count = 0;
	for (const ScenePassenger& passenger : model.passengers)
		for (const ScenePassengerJourney& journey : passenger.journeys)
			count += journey.legs.size();
	return count;
}

static std::size_t platformCount(const SceneModel& model) {
	std::size_t count = 0;
	for (const SceneStation& station : model.stations)
		count += station.platforms.size();
	return count;
}

static const std::vector<DeleteCase>& deleteCases() {
	static const std::vector<DeleteCase> cases = {
		{"scenario", listRow<ScenarioPanel>(1), [](const SceneModel& model) { return model.scenarios.size(); },
			"Deleted scenario 'alt'. Undo restores it."},
		{"composition", listRow<CompositionPanel>(1), [](const SceneModel& model) { return model.compositions.size(); },
			"Deleted composition 'c2'. Undo restores it."},
		{"entrance delay", listRow<EntranceDelayPanel>(0), entranceDelayCount,
			"Deleted entrance delay for service 's1'. Undo restores it."},
		{"incident", listRow<IncidentPanel>(0), incidentCount, "Deleted incident 'i1'. Undo restores it."},
		{"passenger", passengerRow(0), [](const SceneModel& model) { return model.passengers.size(); },
			"Deleted passenger 'p1' and its journeys. Undo restores them."},
		{"journey", passengerRow(1), journeyCount, "Deleted journey 'j1' and its legs. Undo restores them."},
		{"leg", passengerRow(2), legCount, "Deleted passenger leg 'l1'. Undo restores it."},
		{"service", listRow<ServicePanel>(1), [](const SceneModel& model) { return model.services.size(); },
			"Deleted service 's2'. Undo restores it."},
		{"rolling stock unit", listRow<TrainUnitPanel>(1), [](const SceneModel& model) { return model.trainUnits.size(); },
			"Deleted rolling stock unit 'u2'. Undo restores it."},
		{"track", addedInfrastructureRow("tracks"), [](const SceneModel& model) { return model.tracks.size(); },
			"Deleted track 'track'. Undo restores it."},
		{"node", addedInfrastructureRow("nodes"), [](const SceneModel& model) { return model.nodes.size(); },
			"Deleted node 'node'. Undo restores it."},
		{"arc", addedInfrastructureRow("arcs"), [](const SceneModel& model) { return model.arcs.size(); },
			"Deleted arc 'arc'. Undo restores it."},
		{"block", addedInfrastructureRow("blocks"), [](const SceneModel& model) { return model.blocks.size(); },
			"Deleted block 'block'. Undo restores it."},
		{"connection", addedInfrastructureRow("connections"), [](const SceneModel& model) { return model.connections.size(); },
			"Deleted connection 'connection'. Undo restores it."},
		{"station", addedInfrastructureRow("stations"), [](const SceneModel& model) { return model.stations.size(); },
			"Deleted station 'station'. Undo restores it."},
		{"platform", addedInfrastructureRow("platforms"), platformCount, "Deleted platform 'platform'. Undo restores it."},
		{"signal", addedInfrastructureRow("signals"), [](const SceneModel& model) { return sceneSignals(model).size(); },
			"Deleted signal 'signal'. Undo restores it."},
		{"signalling area", addedInfrastructureRow("signalling_areas"),
			[](const SceneModel& model) { return model.signallingAreas.size(); },
			"Deleted signalling area 'signalling-area'. Undo restores it."},
		{"route", addedInfrastructureRow("routes"), [](const SceneModel& model) { return model.routes.size(); },
			"Deleted route 'route'. Undo restores it."},
		{"block dependency", addedInfrastructureRow("block_dependencies"),
			[](const SceneModel& model) { return model.blockDependencies.size(); },
			"Deleted an incomplete block dependency. Undo restores it."},
		{"single-track restriction", addedInfrastructureRow("single_track_restrictions"),
			[](const SceneModel& model) { return model.singleTrackRestrictions.size(); },
			"Deleted an incomplete single-track restriction. Undo restores it."},
		{"station boundary", addedInfrastructureRow("station_boundaries"),
			[](const SceneModel& model) { return model.stationBoundaries.size(); },
			"Deleted an incomplete station boundary. Undo restores it."},
	};
	return cases;
}

// The prompts that the panel showed, as "title: text"; the prompts keep the last one of each kind.
static QString shownPrompts(const FakePrompts& prompts) {
	QStringList shown;
	if (prompts.confirmCount > 0)
		shown << prompts.confirmTitle + ": " + prompts.confirmQuestion;
	if (prompts.errorCount > 0)
		shown << prompts.errorTitle + ": " + prompts.errorMessage;
	if (prompts.infoCount > 0)
		shown << prompts.infoTitle + ": " + prompts.infoMessage;
	if (prompts.buttonCount > 0)
		shown << prompts.buttonTitle + ": " + prompts.buttonQuestion;
	if (prompts.chooseCount > 0)
		shown << "a choice of " + prompts.offeredItems.join(", ");
	return shown.join("; ");
}

// The texts of the status messages that the spy caught, one per line.
static QString messageTexts(const QSignalSpy& spy) {
	QStringList texts;
	for (const QList<QVariant>& arguments : spy)
		texts << arguments.value(0).toString();
	return texts.join("\n");
}

class TestEditorDelete : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void deletesWithoutQuestion_data();
	void deletesWithoutQuestion();
};

void TestEditorDelete::initTestCase() {
	// The icon of the pick button of the incident panel is a resource of the application, which this test does not have.
	QLoggingCategory::setFilterRules(QStringLiteral("qt.svg.warning=false"));
}

void TestEditorDelete::deletesWithoutQuestion_data() {
	QTest::addColumn<int>("index");
	const std::vector<DeleteCase>& cases = deleteCases();
	for (std::size_t index = 0; index < cases.size(); ++index)
		QTest::newRow(cases[index].name) << static_cast<int>(index);
}

// Delete removes the selected item at once, without a question. The status line names the item and says that Undo restores
// it, and Undo brings it back.
void TestEditorDelete::deletesWithoutQuestion() {
	QFETCH(int, index);
	const DeleteCase& deleted = deleteCases()[static_cast<std::size_t>(index)];
	SceneDocument document;
	FakePrompts prompts;
	RunSelection selection;
	document.replace(deleteScene(), false);
	selection.selectScenario("base");
	const Opened opened = deleted.open(document, prompts, selection);
	EditHistory history(document, selection, *opened.panel);
	// The status messages that the panel sends.
	QSignalSpy messages(opened.panel.get(), SIGNAL(statusMessage(QString, int)));
	const std::size_t before = deleted.count(document.model());
	QVERIFY2(opened.button->isEnabled(), "Delete is on for the selected item");

	opened.button->click();
	// The item is gone, no question was asked and the status line says what was deleted.
	QCOMPARE(shownPrompts(prompts), QString());
	QCOMPARE(deleted.count(document.model()), before - 1);
	QCOMPARE(messageTexts(messages), deleted.message);

	QVERIFY(history.undo());
	// Undo brings the item back.
	QCOMPARE(deleted.count(document.model()), before);
}

QTEST_MAIN(TestEditorDelete)
#include "test_editor_delete.moc"
