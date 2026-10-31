#include "editors/InfrastructurePanel.h"
#include "editors/infrastructure/StationFacets.h"

#include "scene/SectionInventory.h"

#include "support/FakePrompts.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QScopeGuard>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTest>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

static SceneNode node(const char* id, const char* trackId, double xKm) {
	SceneNode result;
	result.id = id;
	result.trackId = trackId;
	result.xKm = xKm;
	return result;
}

static SceneArc arc(const char* id, const char* trackId, const char* from, const char* to) {
	SceneArc result;
	result.id = id;
	result.trackId = trackId;
	result.fromNodeId = from;
	result.toNodeId = to;
	result.speedLimitMs = 20.0;
	return result;
}

static SceneBlock block(const char* id, const char* trackId) {
	SceneBlock result;
	result.id = id;
	result.trackId = trackId;
	result.lengthKm = 1.0;
	return result;
}

static ScenePlatform platform(const char* id, std::vector<std::string> nodeIds = {}) {
	ScenePlatform result;
	result.id = id;
	result.nodeIds = std::move(nodeIds);
	return result;
}

static SceneStop stop(const char* stationId, const char* platformId) {
	SceneStop result;
	result.stationId = stationId;
	result.platformId = platformId;
	return result;
}

// The runtime id of the section that a block gives, which is what the combos of sections store.
static std::string sectionOf(const SceneModel& scene, const char* blockId) {
	for (const SceneSectionDescriptor& section : buildSceneSectionInventory(scene).sections)
		if (section.sourceBlockId == blockId)
			return section.id;
	return {};
}

// A track of three nodes and two blocks, and two stations. The first has two platforms, the second a platform of its own and
// one that has the id of the second platform of the first, so that moving a platform can clash. A service stops at the
// first platform of the first station, at the platform of the second and at its second platform. Every id has a name of its
// own, so a test can tell which reference a rewrite reached.
static SceneModel stationScene() {
	SceneModel scene;
	scene.name = "Stations";
	scene.tracks = {{"track-a"}};
	scene.nodes = {node("node-a0", "track-a", 0.0), node("node-a1", "track-a", 1.0), node("node-a2", "track-a", 2.0)};
	scene.arcs = {arc("arc-a1", "track-a", "node-a0", "node-a1"), arc("arc-a2", "track-a", "node-a1", "node-a2")};
	scene.blocks = {block("block-a1", "track-a"), block("block-a2", "track-a")};

	SceneStation first;
	first.id = "station-1";
	first.name = "Alpha";
	first.hasPosition = true;
	first.positionKm = 0.5;
	first.platforms = {platform("platform-1", {"node-a1"}), platform("platform-2")};
	first.platforms[0].hasLength = true;
	first.platforms[0].lengthM = 150.0;
	SceneStation second;
	second.id = "station-2";
	second.name = "Beta";
	second.platforms = {platform("platform-3"), platform("platform-2")};
	scene.stations = {first, second};

	SceneService service;
	service.id = "service-1";
	service.stops = {stop("station-1", "platform-1"), stop("station-2", "platform-3"), stop("station-2", "platform-2")};
	scene.services.push_back(service);
	SceneScenario scenario;
	scenario.id = "scenario-1";
	scenario.entranceDelays.push_back({"service-1", 1, "station-1", 30.0});
	scene.scenarios.push_back(scenario);
	ScenePassengerLeg leg;
	leg.id = "leg-1";
	leg.originStationId = "station-1";
	leg.destinationStationId = "station-2";
	leg.serviceId = "service-1";
	ScenePassengerJourney journey;
	journey.id = "journey-1";
	journey.originStationId = "station-1";
	journey.destinationStationId = "station-2";
	journey.legs.push_back(leg);
	ScenePassenger passenger;
	passenger.id = "passenger-1";
	passenger.journeys.push_back(journey);
	scene.passengers.push_back(passenger);

	SceneStationBoundary boundary;
	boundary.entranceBlock = sectionOf(scene, "block-a1");
	scene.stationBoundaries.push_back(boundary);
	return scene;
}

using Log = std::vector<std::string>;

// A panel with the facets of the stations, over a document that holds the scene. The events are the order in which the
// panel and its document report an edit, and the messages that the panel shows in the status line.
struct Fixture {
	SceneDocument document;
	InfrastructurePanel panel;
	FakePrompts prompts;
	Log log;
	SceneDocument::ChangeKinds kinds;
	QStringList messages;

	explicit Fixture(SceneModel scene = stationScene()) {
		panel.setPrompts(&prompts);
		for (std::unique_ptr<InfrastructureFacet>& facet : makeStationFacets())
			panel.setFacet(std::move(facet));
		panel.setDocument(&document);
		document.replace(std::move(scene), false);
		panel.refresh();
		QObject::connect(&document, &SceneDocument::changed, &panel, [this](SceneDocument::ChangeKinds changedKinds) {
			log.push_back("documentChanged");
			kinds = changedKinds;
		});
		QObject::connect(&panel, &InfrastructurePanel::changed, &panel, [this] { log.push_back("panelChanged"); });
		QObject::connect(&panel, &InfrastructurePanel::statusMessage, &panel,
			[this](const QString& text, int) { messages << text; });
	}

	void choose(const char* key) { panel.facetCombo()->setCurrentIndex(panel.facetCombo()->findData(key)); }
	QTableWidget* table() { return panel.table(); }
	const SceneModel& model() const { return document.model(); }
	SceneModel& edit() { return document.mutableModel(); }
	// Selects the row and lists the entities again, so that the buttons follow the selected row.
	void select(int row) {
		table()->setCurrentCell(row, 0);
		panel.refresh();
	}
	void type(int row, int column, const char* text) { table()->item(row, column)->setText(text); }
	QString cell(int row, int column) { return table()->item(row, column)->text(); }
	// The cells of the row that are text.
	QStringList rowTexts(int row, int columns) {
		QStringList result;
		for (int column = 0; column < columns; ++column)
			result << cell(row, column);
		return result;
	}
	QStringList firstColumn() {
		QStringList result;
		for (int row = 0; row < table()->rowCount(); ++row)
			result << cell(row, 0);
		return result;
	}
	QStringList headers() {
		QStringList result;
		for (int column = 0; column < table()->columnCount(); ++column)
			result << table()->horizontalHeaderItem(column)->text();
		return result;
	}
	QComboBox* combo(int row, int column) { return qobject_cast<QComboBox*>(table()->cellWidget(row, column)); }
	QDoubleSpinBox* spin(int row, int column) { return qobject_cast<QDoubleSpinBox*>(table()->cellWidget(row, column)); }
	// Chooses the section that the block gives in the combo.
	void chooseBlock(int row, int column, const char* blockId) {
		QComboBox* box = combo(row, column);
		box->setCurrentIndex(box->findData(QString::fromStdString(sectionOf(model(), blockId))));
	}
	// True when the document has been changed once, under this label, and the panel has said so.
	bool changedOnce(const char* label) {
		const bool once = log == Log({"documentChanged", "panelChanged"}) && kinds == SceneDocument::Infrastructure
			&& document.lastChangeLabel() == label && document.isDirty();
		log.clear();
		return once;
	}
	// True when no change has been reported since the last check.
	bool untouched() {
		const bool still = log.empty();
		log.clear();
		return still;
	}
};

static QString blocked(const char* kind, const char* id, const QStringList& consumers) {
	return QString("%1 '%2' is used by:\n\xE2\x80\xA2 %3\nRemove or change these first.")
		.arg(QString(kind), QString(id), consumers.join("\n\xE2\x80\xA2 "));
}

class TestStationFacets : public QObject {
	Q_OBJECT

private slots:
	void offersStationFacets();
	void editsStations();
	void listsPlatforms();
	void editsPlatforms();
	void movesPlatforms();
	void editsPlatformSize();
	void addsAndDeletesPlatforms();
	void refusesPlatformWithoutStation();
	void editsStationBoundaries();
};

void TestStationFacets::offersStationFacets() {
	const std::vector<std::unique_ptr<InfrastructureFacet>> facets = makeStationFacets();
	QStringList keys;
	for (const std::unique_ptr<InfrastructureFacet>& facet : facets)
		keys << facet->key();
	// The stations have three facets.
	QCOMPARE(keys, QStringList({"stations", "platforms", "station_boundaries"}));

	Fixture f;
	// The panel has the facets of the stations and no others.
	QVERIFY(f.panel.facet("stations"));
	QVERIFY(f.panel.facet("platforms"));
	QVERIFY(f.panel.facet("station_boundaries"));
	QVERIFY(!f.panel.facet("blocks"));
	QVERIFY(!f.panel.facet("tracks"));
	f.choose("platforms");
	f.document.setEditable(false);
	f.panel.refresh();
	f.panel.addEntity();
	f.panel.deleteEntity();
	f.type(0, 2, "node-a0");
	f.panel.commitPendingEditors();
	// Nothing is edited, added or deleted while a run uses the scene.
	QVERIFY(f.untouched());
	QCOMPARE(f.model().stations[0].platforms.size(), std::size_t(2));
	QCOMPARE(f.model().stations[0].platforms[0].nodeIds, std::vector<std::string>({"node-a1"}));
}

void TestStationFacets::editsStations() {
	Fixture f;
	f.choose("stations");
	// Stations are listed with their name and position.
	QCOMPARE(f.headers(), QStringList({"ID", "Name", "Has position", "Position km"}));
	QCOMPARE(f.table()->rowCount(), 2);
	QCOMPARE(f.rowTexts(0, 4), QStringList({"station-1", "Alpha", "true", "0.5"}));
	QCOMPARE(f.rowTexts(1, 4), QStringList({"station-2", "Beta", "false", "0"}));
	// Listing stations edits nothing.
	QVERIFY(f.untouched());
	QVERIFY(!f.document.isDirty());

	// edit
	f.type(1, 1, "Gamma");
	// The name of a station is edited.
	QCOMPARE(f.model().stations[1].name, "Gamma");
	QVERIFY(f.changedOnce("Edit station"));
	f.type(1, 2, "TRUE");
	// The flag of a station is edited.
	QVERIFY(f.model().stations[1].hasPosition);
	QCOMPARE(f.cell(1, 2), "true");
	QVERIFY(f.changedOnce("Edit station"));
	f.type(1, 3, "12.25");
	// The position of a station is edited.
	QCOMPARE_EQ(f.model().stations[1].positionKm, 12.25);
	QVERIFY(f.changedOnce("Edit station"));
	f.type(1, 1, "Gamma");
	f.type(1, 2, "true");
	f.type(1, 3, " 12.25 ");
	QVERIFY2(f.untouched(), "values that are the same change nothing");
	f.type(1, 2, "yes");
	f.type(1, 3, "far");
	// A flag and a position that are no values are refused, without a message, and the table shows the model again.
	QVERIFY(f.untouched());
	QVERIFY(f.model().stations[1].hasPosition);
	QCOMPARE_EQ(f.model().stations[1].positionKm, 12.25);
	QCOMPARE(f.cell(1, 2), "true");
	QCOMPARE(f.cell(1, 3), "12.25");
	QVERIFY(f.messages.empty());

	// rename: stops, entrance delays and passengers refer to a station by its id
	f.type(0, 0, " station-x ");
	const SceneModel& model = f.model();
	// A renamed station keeps its stops, entrance delays and passengers, and leaves the other station as it is.
	QCOMPARE(model.stations[0].id, "station-x");
	QCOMPARE(model.services[0].stops[0].stationId, "station-x");
	QCOMPARE(model.services[0].stops[1].stationId, "station-2");
	QCOMPARE(model.scenarios[0].entranceDelays[0].stationId, "station-x");
	QCOMPARE(model.passengers[0].journeys[0].originStationId, "station-x");
	QCOMPARE(model.passengers[0].journeys[0].destinationStationId, "station-2");
	QCOMPARE(model.passengers[0].journeys[0].legs[0].originStationId, "station-x");
	QCOMPARE(model.passengers[0].journeys[0].legs[0].destinationStationId, "station-2");
	// A rename marks the document once, and the renamed station stays selected.
	QVERIFY(f.changedOnce("Rename station"));
	QCOMPARE(f.panel.selectionId(), "station-x");
	QCOMPARE(f.table()->currentRow(), 0);
	f.type(0, 0, "station-2");
	// An id that another station has is refused.
	QVERIFY(f.untouched());
	QCOMPARE(f.model().stations[0].id, "station-x");
	QCOMPARE(f.messages, QStringList({"IDs must be unique and not empty"}));
	f.type(0, 0, "");
	// An empty id is refused.
	QVERIFY(f.untouched());
	QCOMPARE(f.model().stations[0].id, "station-x");
	QCOMPARE(f.messages.size(), qsizetype(2));

	// add
	f.panel.addButton()->click();
	const SceneStation& added = f.model().stations.back();
	// A station is added with a new id that is also its name, and selected.
	QCOMPARE(f.model().stations.size(), std::size_t(3));
	QCOMPARE(added.id, "station");
	QCOMPARE(added.name, "station");
	QVERIFY(!added.hasPosition);
	QVERIFY(added.platforms.empty());
	QVERIFY(f.changedOnce("Add station"));
	QCOMPARE(f.panel.selectionId(), "station");
	QCOMPARE(f.table()->currentRow(), 2);

	// delete
	f.select(0);
	// The delete button of a station that others use has its tooltip.
	QVERIFY(f.panel.deleteButton()->isEnabled());
	QCOMPARE(f.panel.deleteButton()->toolTip(), "Delete the selected row.");
	f.panel.deleteButton()->click();
	// A station that others use cannot be deleted.
	QCOMPARE(f.prompts.errorCount, 1);
	QCOMPARE(f.prompts.confirmCount, 0);
	QCOMPARE(f.model().stations.size(), std::size_t(3));
	QVERIFY(f.untouched());
	// The message names the stops, entrance delays and passengers that use the station.
	QCOMPARE(f.prompts.errorMessage,
		blocked("Station", "station-x",
			{"service 'service-1' stop", "entrance delay for service 'service-1' in scenario 'scenario-1'", "passenger 'passenger-1' journey 'journey-1'",
				"passenger 'passenger-1' leg 'leg-1'"}));
	f.select(2);
	f.panel.deleteButton()->click();
	// Nothing is deleted when the user answers no.
	QCOMPARE(f.prompts.confirmCount, 1);
	QCOMPARE(f.prompts.confirmQuestion, "Delete station 'station'?");
	QCOMPARE(f.model().stations.size(), std::size_t(3));
	QVERIFY(f.untouched());
	f.prompts.confirmAnswer = true;
	f.panel.deleteButton()->click();
	// A station that nothing uses is deleted when the user answers yes.
	QCOMPARE(f.model().stations.size(), std::size_t(2));
	QVERIFY(f.changedOnce("Delete station"));
	QCOMPARE(f.panel.selectionId(), "station-2");
}

void TestStationFacets::listsPlatforms() {
	Fixture f;
	f.choose("platforms");
	// Platforms are listed under their stations.
	QCOMPARE(f.headers(), QStringList({"Station ID", "ID", "Nodes", "Length (m)", "Width (m)"}));
	QCOMPARE(f.table()->rowCount(), 4);
	// Each row has its station, id and nodes.
	QCOMPARE(f.rowTexts(0, 3), QStringList({"station-1", "platform-1", "node-a1"}));
	QCOMPARE(f.rowTexts(1, 3), QStringList({"station-1", "platform-2", ""}));
	QCOMPARE(f.rowTexts(2, 3), QStringList({"station-2", "platform-3", ""}));
	QCOMPARE(f.rowTexts(3, 3), QStringList({"station-2", "platform-2", ""}));
	// The length and the width are number fields.
	QVERIFY(f.spin(0, 3));
	QVERIFY(f.spin(0, 4));
	QCOMPARE(f.spin(0, 3)->objectName(), "platformLengthEdit");
	QCOMPARE(f.spin(0, 4)->objectName(), "platformWidthEdit");
	// A platform that states no size shows the default size.
	QCOMPARE_EQ(f.spin(0, 3)->value(), 150.0);
	QCOMPARE_EQ(f.spin(0, 4)->value(), 2.5);
	QCOMPARE_EQ(f.spin(1, 3)->value(), 100.0);
	QCOMPARE_EQ(f.spin(1, 4)->value(), 2.5);
	// Listing platforms edits nothing, and states no size that the scene does not.
	QVERIFY(!f.model().stations[0].platforms[1].hasLength);
	QVERIFY(!f.model().stations[0].platforms[1].hasWidth);
	QVERIFY(f.untouched());
	QVERIFY(!f.document.isDirty());

	// two platforms of different stations may have the same id, and are told apart by their station
	f.table()->setCurrentCell(3, 0);
	// A platform is selected by its station and its id.
	QCOMPARE(f.panel.selectionId(), "station-2\nplatform-2");
	f.table()->setCurrentCell(1, 0);
	// The other platform with that id has its own selection.
	QCOMPARE(f.panel.selectionId(), "station-1\nplatform-2");
}

void TestStationFacets::editsPlatforms() {
	Fixture f;
	f.choose("platforms");

	// nodes
	f.type(0, 2, " node-a0 , node-a2 ,, ");
	// The nodes of a platform are a list that drops empty items.
	QCOMPARE(f.model().stations[0].platforms[0].nodeIds, std::vector<std::string>({"node-a0", "node-a2"}));
	QVERIFY(f.changedOnce("Edit platform"));
	f.type(0, 2, "node-a0,node-a2");
	QVERIFY2(f.untouched(), "the same nodes change nothing");
	f.type(0, 2, "");
	// A platform can have no nodes.
	QVERIFY(f.model().stations[0].platforms[0].nodeIds.empty());
	QVERIFY(f.changedOnce("Edit platform"));

	// rename: only the stops at the platform of that station
	f.type(1, 1, " platform-x ");
	// A renamed platform leaves the stops at the platform of another station with that id as they are.
	QCOMPARE(f.model().stations[0].platforms[1].id, "platform-x");
	QCOMPARE(f.model().services[0].stops[2].platformId, "platform-2");
	QCOMPARE(f.model().services[0].stops[0].platformId, "platform-1");
	QCOMPARE(f.model().stations[1].platforms[1].id, "platform-2");
	// A rename marks the document once, and the renamed platform stays selected.
	QVERIFY(f.changedOnce("Rename platform"));
	QCOMPARE(f.panel.selectionId(), "station-1\nplatform-x");
	QCOMPARE(f.table()->currentRow(), 1);
	f.type(0, 1, "platform-x");
	// An id that another platform of the station has is refused.
	QVERIFY(f.untouched());
	QCOMPARE(f.model().stations[0].platforms[0].id, "platform-1");
	QCOMPARE(f.messages, QStringList({"IDs must be unique and not empty"}));
	f.type(0, 1, "");
	// An empty id is refused.
	QVERIFY(f.untouched());
	QCOMPARE(f.messages.size(), qsizetype(2));
	f.type(1, 1, "platform-1");
	// The id of another platform of the station is refused.
	QVERIFY(f.untouched());
	QCOMPARE(f.model().stations[0].platforms[1].id, "platform-x");
	QCOMPARE(f.messages.size(), qsizetype(3));
	f.type(3, 1, "platform-1");
	// An id that is free in a station is taken even when another station has it, and only the stops at that station follow.
	QCOMPARE(f.model().stations[1].platforms[1].id, "platform-1");
	QCOMPARE(f.model().services[0].stops[2].platformId, "platform-1");
	QCOMPARE(f.model().services[0].stops[2].stationId, "station-2");
	QCOMPARE(f.model().services[0].stops[0].stationId, "station-1");
	QCOMPARE(f.model().services[0].stops[0].platformId, "platform-1");
	QVERIFY(f.changedOnce("Rename platform"));
}

void TestStationFacets::movesPlatforms() {
	Fixture f;
	f.choose("platforms");

	// the platform goes to the end of the platforms of the other station, and its stops go with it
	f.type(0, 0, "station-2");
	const SceneModel& model = f.model();
	// A platform moves to the station that is named, with its nodes and size.
	QCOMPARE(model.stations[0].platforms.size(), std::size_t(1));
	QCOMPARE(model.stations[0].platforms[0].id, "platform-2");
	QCOMPARE(model.stations[1].platforms.size(), std::size_t(3));
	QCOMPARE(model.stations[1].platforms[2].id, "platform-1");
	QCOMPARE(model.stations[1].platforms[2].nodeIds, std::vector<std::string>({"node-a1"}));
	QVERIFY(model.stations[1].platforms[2].hasLength);
	QCOMPARE_EQ(model.stations[1].platforms[2].lengthM, 150.0);
	// The stops at the platform move with it.
	QCOMPARE(model.services[0].stops[0].stationId, "station-2");
	QCOMPARE(model.services[0].stops[0].platformId, "platform-1");
	QCOMPARE(model.services[0].stops[1].stationId, "station-2");
	QCOMPARE(model.services[0].stops[1].platformId, "platform-3");
	// A move marks the document once, and the platform stays selected in its new row.
	QVERIFY(f.changedOnce("Edit platform"));
	QCOMPARE(f.panel.selectionId(), "station-2\nplatform-1");
	QCOMPARE(f.table()->currentRow(), 3);
	QCOMPARE(f.rowTexts(3, 2), QStringList({"station-2", "platform-1"}));
	f.type(3, 0, "station-2");
	QVERIFY2(f.untouched(), "naming the station that the platform is in changes nothing");

	// refused
	f.type(0, 0, "nowhere");
	// A station that the scene does not have is refused, with a message, and the table shows the model again.
	QVERIFY(f.untouched());
	QCOMPARE(f.model().stations[0].platforms.size(), std::size_t(1));
	QCOMPARE(f.cell(0, 0), "station-1");
	QCOMPARE(f.messages, QStringList({"Platform Station ID must name an existing station"}));
	f.type(0, 0, "station-2");
	// A platform is not moved to a station that has a platform with its id.
	QVERIFY(f.untouched());
	QCOMPARE(f.model().stations[0].platforms.size(), std::size_t(1));
	QCOMPARE(f.model().stations[1].platforms.size(), std::size_t(3));
	QCOMPARE(f.messages.size(), qsizetype(2));
	QCOMPARE(f.messages.back(), "Platform ID already exists on the target station");
}

void TestStationFacets::editsPlatformSize() {
	Fixture f;
	f.choose("platforms");

	// a field is the widget that is edited, so the table keeps it
	QDoubleSpinBox* length = f.spin(1, 3);
	length->setValue(200.5);
	const ScenePlatform& second = f.model().stations[0].platforms[1];
	// A length is taken as soon as the field changes, and the table keeps the field.
	QVERIFY(second.hasLength);
	QCOMPARE_EQ(second.lengthM, 200.5);
	QVERIFY(!second.hasWidth);
	QVERIFY(f.changedOnce("Edit platform"));
	QCOMPARE(f.spin(1, 3), length);
	f.spin(1, 4)->setValue(3.25);
	// A width is taken the same way.
	QVERIFY(second.hasWidth);
	QCOMPARE_EQ(second.widthM, 3.25);
	QVERIFY(f.changedOnce("Edit platform"));
	length->setValue(200.5);
	QVERIFY2(f.untouched(), "a value that is the same changes nothing");
	f.spin(0, 4)->setValue(2.75);
	// A size that the scene did not state is stated once the field changes.
	QVERIFY(f.model().stations[0].platforms[0].hasWidth);
	QCOMPARE_EQ(f.model().stations[0].platforms[0].widthM, 2.75);
	QVERIFY(f.changedOnce("Edit platform"));

	// a field that has been typed in and not left
	f.panel.show();
	// The panel is hidden before the fixture destroys it, also when a check fails.
	const auto hide = qScopeGuard([&f] { f.panel.hide(); });
	f.panel.activateWindow();
	QApplication::processEvents();
	QDoubleSpinBox* typed = f.spin(2, 3);
	typed->setFocus();
	QApplication::processEvents();
	QLineEdit* text = typed->findChild<QLineEdit*>();
	text->setText("77");
	emit text->textEdited("77");
	f.panel.commitPendingEditors();
	// A length that has been typed in is taken when the editors are committed.
	QVERIFY(f.model().stations[1].platforms[0].hasLength);
	QCOMPARE_EQ(f.model().stations[1].platforms[0].lengthM, 77.0);
	QVERIFY(f.changedOnce("Edit platform"));
	f.panel.commitPendingEditors();
	QVERIFY2(f.untouched(), "it is taken once");
}

void TestStationFacets::addsAndDeletesPlatforms() {
	Fixture f;
	f.choose("platforms");
	f.panel.addButton()->click();
	const ScenePlatform& added = f.model().stations[0].platforms.back();
	// A platform is added to the first station, with a new id, and selected.
	QCOMPARE(f.model().stations[0].platforms.size(), std::size_t(3));
	QCOMPARE(added.id, "platform");
	QVERIFY(added.nodeIds.empty());
	QVERIFY(!added.hasLength);
	QVERIFY(!added.hasWidth);
	QVERIFY(f.changedOnce("Add platform"));
	QCOMPARE(f.panel.selectionId(), "station-1\nplatform");
	QCOMPARE(f.table()->currentRow(), 2);

	// delete
	f.select(0);
	f.panel.deleteButton()->click();
	// A platform that a stop uses cannot be deleted.
	QCOMPARE(f.prompts.errorCount, 1);
	QCOMPARE(f.prompts.confirmCount, 0);
	QCOMPARE(f.model().stations[0].platforms.size(), std::size_t(3));
	QVERIFY(f.untouched());
	QCOMPARE(f.prompts.errorMessage, blocked("Platform", "platform-1", {"service 'service-1' stop at station 'station-1'"}));
	f.select(2);
	f.panel.deleteButton()->click();
	// Nothing is deleted when the user answers no.
	QCOMPARE(f.prompts.confirmCount, 1);
	QCOMPARE(f.prompts.confirmQuestion, "Delete platform 'platform'?");
	QCOMPARE(f.model().stations[0].platforms.size(), std::size_t(3));
	QVERIFY(f.untouched());
	f.prompts.confirmAnswer = true;
	f.panel.deleteButton()->click();
	// A platform that nothing uses is deleted when the user answers yes.
	QCOMPARE(f.model().stations[0].platforms.size(), std::size_t(2));
	QVERIFY(f.changedOnce("Delete platform"));
	QCOMPARE(f.panel.selectionId(), "station-2\nplatform-3");
	// a stop at a platform of the other station with the same id does not block this one
	f.select(1);
	f.panel.deleteButton()->click();
	// The stops at a platform of another station with that id do not block the delete.
	QCOMPARE(f.prompts.errorCount, 1);
	QCOMPARE(f.model().stations[0].platforms.size(), std::size_t(1));
	QVERIFY(f.changedOnce("Delete platform"));
}

void TestStationFacets::refusesPlatformWithoutStation() {
	Fixture empty;
	empty.edit().stations.clear();
	empty.choose("platforms");
	empty.panel.addEntity();
	// A platform is not added without a station.
	QVERIFY(empty.untouched());
	QCOMPARE(empty.messages, QStringList({"Add a station before adding a platform"}));
}

void TestStationFacets::editsStationBoundaries() {
	Fixture f;
	f.choose("station_boundaries");
	const std::string a1 = sectionOf(f.model(), "block-a1");
	// Station boundaries are listed with their sections.
	QCOMPARE(f.headers(), QStringList({"Entrance", "Exit", "Direction"}));
	QCOMPARE(f.table()->rowCount(), 1);
	// The entrance and the exit are chosen in combos of the sections, and the exit can be none.
	QVERIFY(f.combo(0, 0));
	QVERIFY(f.combo(0, 1));
	QCOMPARE(f.combo(0, 0)->objectName(), "boundaryEntranceCombo");
	QCOMPARE(f.combo(0, 1)->objectName(), "boundaryExitCombo");
	QCOMPARE(f.combo(0, 0)->currentData().toString().toStdString(), a1);
	QCOMPARE(f.combo(0, 1)->currentText(), "(none)");
	QCOMPARE(f.cell(0, 2), "false");
	// Listing station boundaries edits nothing.
	QVERIFY(f.untouched());
	QVERIFY(!f.document.isDirty());

	// edit
	QComboBox* entrance = f.combo(0, 0);
	f.chooseBlock(0, 0, "block-a2");
	// Another entrance is chosen, which marks the document once and leaves the combo in the table.
	QCOMPARE(f.model().stationBoundaries[0].entranceBlock, sectionOf(f.model(), "block-a2"));
	QVERIFY(f.changedOnce("Edit station boundary"));
	QCOMPARE(f.combo(0, 0), entrance);
	f.chooseBlock(0, 1, "block-a1");
	const SceneStationBoundary& boundary = f.model().stationBoundaries[0];
	// An exit is chosen.
	QVERIFY(boundary.hasExitBlock);
	QCOMPARE(boundary.exitBlock, a1);
	QVERIFY(f.changedOnce("Edit station boundary"));
	f.chooseBlock(0, 1, "block-a1");
	QVERIFY2(f.untouched(), "choosing the exit that is chosen changes nothing");
	f.combo(0, 1)->setCurrentIndex(f.combo(0, 1)->findData(QString()));
	// Choosing none clears the exit and its flag together.
	QVERIFY(!boundary.hasExitBlock);
	QVERIFY(boundary.exitBlock.empty());
	QVERIFY(f.changedOnce("Edit station boundary"));
	f.type(0, 2, "True");
	// The direction is edited.
	QVERIFY(boundary.direction);
	QCOMPARE(f.cell(0, 2), "true");
	QVERIFY(f.changedOnce("Edit station boundary"));
	f.type(0, 2, "up");
	// A direction that is neither true nor false is refused and the table shows the model again.
	QVERIFY(f.untouched());
	QVERIFY(boundary.direction);
	QCOMPARE(f.cell(0, 2), "true");
	QVERIFY(f.messages.empty());

	// add
	f.panel.addButton()->click();
	// A station boundary is added empty, and its row is selected.
	QCOMPARE(f.model().stationBoundaries.size(), std::size_t(2));
	QVERIFY(f.model().stationBoundaries[1].entranceBlock.empty());
	QVERIFY(!f.model().stationBoundaries[1].hasExitBlock);
	QVERIFY(f.changedOnce("Add station boundary"));
	QCOMPARE(f.table()->currentRow(), 1);
	QVERIFY(f.panel.selectionId().isEmpty());
	// An empty entrance and an empty exit show as none.
	QCOMPARE(f.combo(1, 0)->currentText(), "(none)");
	QCOMPARE(f.combo(1, 1)->currentText(), "(none)");

	// delete: nothing refers to a station boundary
	f.panel.deleteButton()->click();
	// A station boundary that has no entrance yet is named an incomplete row, and stays when the user answers no.
	QCOMPARE(f.prompts.errorCount, 0);
	QCOMPARE(f.prompts.confirmQuestion, "Delete station boundary 'incomplete row'?");
	QCOMPARE(f.model().stationBoundaries.size(), std::size_t(2));
	QVERIFY(f.untouched());
	f.prompts.confirmAnswer = true;
	f.panel.deleteButton()->click();
	// A station boundary is deleted when the user answers yes.
	QCOMPARE(f.model().stationBoundaries.size(), std::size_t(1));
	QVERIFY(f.changedOnce("Delete station boundary"));
	f.select(0);
	f.panel.deleteButton()->click();
	// A station boundary is named by its entrance.
	QCOMPARE(f.prompts.confirmQuestion, QString::fromStdString("Delete station boundary '" + sectionOf(f.model(), "block-a2") + "'?"));
	QVERIFY(f.model().stationBoundaries.empty());
}

QTEST_MAIN(TestStationFacets)
#include "test_editor_infrastructure_stations.moc"
