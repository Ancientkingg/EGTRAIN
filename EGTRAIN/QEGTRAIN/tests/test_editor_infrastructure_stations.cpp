#include "editors/InfrastructurePanel.h"
#include "editors/infrastructure/StationFacets.h"

#include "scene/SectionInventory.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QTableWidget>

#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

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

// Answers the questions of the panel and records the messages, without a dialog.
class FakePrompts : public UserPrompts {
public:
	int errorCount = 0;
	QString errorMessage;
	int confirmCount = 0;
	QString confirmQuestion;
	bool confirmAnswer = false;

	void showError(QWidget*, const QString&, const QString& message, bool = false) override {
		++errorCount;
		errorMessage = message;
	}
	bool confirm(QWidget*, const QString&, const QString& question) override {
		++confirmCount;
		confirmQuestion = question;
		return confirmAnswer;
	}
};

using Log = std::vector<std::string>;

// A panel with the facets of the stations, over a document that holds the scene. The events are the order in which the
// panel and its document report an edit, and the messages that the panel shows in the status line.
struct Fixture {
	SceneDocument document;
	InfrastructurePanel panel;
	FakePrompts prompts;
	Log log;
	QStringList entities;
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
		QObject::connect(&panel, &InfrastructurePanel::entitiesChanged, &panel, [this](const QString& facet) {
			log.push_back("entitiesChanged");
			entities << facet;
		});
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
	// True when the document has been changed once, under this label, and the panel has said so, and the table was listed
	// again for the entities of this facet.
	bool changedOnce(const char* label, const char* facet) {
		const bool once = log == Log({"documentChanged", "panelChanged", "entitiesChanged"}) && entities == QStringList({facet})
			&& kinds == SceneDocument::Infrastructure && document.lastChangeLabel() == label && document.isDirty();
		log.clear();
		entities.clear();
		return once;
	}
	// True when the document has been changed once under this label by a widget of the table, so that the facet is not told
	// that its entities changed.
	bool editedOnce(const char* label) {
		const bool once = log == Log({"documentChanged", "panelChanged"}) && entities.isEmpty() && kinds == SceneDocument::Infrastructure
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

static bool testFacets() {
	bool ok = true;
	const std::vector<std::unique_ptr<InfrastructureFacet>> facets = makeStationFacets();
	QStringList keys;
	for (const std::unique_ptr<InfrastructureFacet>& facet : facets)
		keys << facet->key();
	ok &= expect(keys == QStringList({"stations", "platforms", "station_boundaries"}), "the stations have three facets");

	Fixture f;
	ok &= expect(f.panel.facet("stations") && f.panel.facet("platforms") && f.panel.facet("station_boundaries") && !f.panel.facet("blocks")
			&& !f.panel.facet("tracks"),
		"the panel has the facets of the stations and no others");
	f.choose("platforms");
	f.document.setEditable(false);
	f.panel.refresh();
	f.panel.addEntity();
	f.panel.deleteEntity();
	f.type(0, 2, "node-a0");
	f.panel.commitPendingEditors();
	ok &= expect(f.untouched() && f.model().stations[0].platforms.size() == 2 && f.model().stations[0].platforms[0].nodeIds == std::vector<std::string>({"node-a1"}),
		"nothing is edited, added or deleted while a run uses the scene");
	return ok;
}

static bool testStations() {
	bool ok = true;
	Fixture f;
	f.choose("stations");
	ok &= expect(f.headers() == QStringList({"ID", "Name", "Has position", "Position km"}) && f.table()->rowCount() == 2
			&& f.rowTexts(0, 4) == QStringList({"station-1", "Alpha", "true", "0.5"}) && f.rowTexts(1, 4) == QStringList({"station-2", "Beta", "false", "0"}),
		"stations are listed with their name and position");
	ok &= expect(f.untouched() && !f.document.isDirty(), "listing stations edits nothing");

	// edit
	f.type(1, 1, "Gamma");
	ok &= expect(f.model().stations[1].name == "Gamma" && f.changedOnce("Edit station", "stations"), "the name of a station is edited");
	f.type(1, 2, "TRUE");
	ok &= expect(f.model().stations[1].hasPosition && f.cell(1, 2) == "true" && f.changedOnce("Edit station", "stations"), "the flag of a station is edited");
	f.type(1, 3, "12.25");
	ok &= expect(f.model().stations[1].positionKm == 12.25 && f.changedOnce("Edit station", "stations"), "the position of a station is edited");
	f.type(1, 1, "Gamma");
	f.type(1, 2, "true");
	f.type(1, 3, " 12.25 ");
	ok &= expect(f.untouched(), "values that are the same change nothing");
	f.type(1, 2, "yes");
	f.type(1, 3, "far");
	ok &= expect(f.untouched() && f.model().stations[1].hasPosition && f.model().stations[1].positionKm == 12.25 && f.cell(1, 2) == "true" && f.cell(1, 3) == "12.25"
			&& f.messages.empty(),
		"a flag and a position that are no values are refused, without a message, and the table shows the model again");

	// rename: stops, entrance delays and passengers refer to a station by its id
	f.type(0, 0, " station-x ");
	const SceneModel& model = f.model();
	ok &= expect(model.stations[0].id == "station-x" && model.services[0].stops[0].stationId == "station-x" && model.services[0].stops[1].stationId == "station-2"
			&& model.scenarios[0].entranceDelays[0].stationId == "station-x" && model.passengers[0].journeys[0].originStationId == "station-x"
			&& model.passengers[0].journeys[0].destinationStationId == "station-2" && model.passengers[0].journeys[0].legs[0].originStationId == "station-x"
			&& model.passengers[0].journeys[0].legs[0].destinationStationId == "station-2",
		"a renamed station keeps its stops, entrance delays and passengers, and leaves the other station as it is");
	ok &= expect(f.changedOnce("Rename station", "stations") && f.panel.selectionId() == "station-x" && f.table()->currentRow() == 0,
		"a rename marks the document once, and the renamed station stays selected");
	f.type(0, 0, "station-2");
	ok &= expect(f.untouched() && f.model().stations[0].id == "station-x" && f.messages == QStringList({"IDs must be unique and not empty"}),
		"an id that another station has is refused");
	f.type(0, 0, "");
	ok &= expect(f.untouched() && f.model().stations[0].id == "station-x" && f.messages.size() == 2, "an empty id is refused");

	// add
	f.panel.addButton()->click();
	const SceneStation& added = f.model().stations.back();
	ok &= expect(f.model().stations.size() == 3 && added.id == "station" && added.name == "station" && !added.hasPosition && added.platforms.empty()
			&& f.changedOnce("Add station", "stations") && f.panel.selectionId() == "station" && f.table()->currentRow() == 2,
		"a station is added with a new id that is also its name, and selected");

	// delete
	f.select(0);
	ok &= expect(f.panel.deleteButton()->isEnabled() && f.panel.deleteButton()->toolTip() == "Delete the selected row.",
		"the delete button of a station that others use has its tooltip");
	f.panel.deleteButton()->click();
	ok &= expect(f.prompts.errorCount == 1 && f.prompts.confirmCount == 0 && f.model().stations.size() == 3 && f.untouched(),
		"a station that others use cannot be deleted");
	ok &= expect(f.prompts.errorMessage
			== blocked("Station", "station-x",
				{"service 'service-1' stop", "entrance delay for service 'service-1' in scenario 'scenario-1'", "passenger 'passenger-1' journey 'journey-1'",
					"passenger 'passenger-1' leg 'leg-1'"}),
		"the message names the stops, entrance delays and passengers that use the station");
	f.select(2);
	f.panel.deleteButton()->click();
	ok &= expect(f.prompts.confirmCount == 1 && f.prompts.confirmQuestion == "Delete station 'station'?" && f.model().stations.size() == 3 && f.untouched(),
		"nothing is deleted when the user answers no");
	f.prompts.confirmAnswer = true;
	f.panel.deleteButton()->click();
	ok &= expect(f.model().stations.size() == 2 && f.changedOnce("Delete station", "stations") && f.panel.selectionId() == "station-2",
		"a station that nothing uses is deleted when the user answers yes");
	return ok;
}

static bool testPlatformList() {
	bool ok = true;
	Fixture f;
	f.choose("platforms");
	ok &= expect(f.headers() == QStringList({"Station ID", "ID", "Nodes", "Length (m)", "Width (m)"}) && f.table()->rowCount() == 4,
		"platforms are listed under their stations");
	ok &= expect(f.rowTexts(0, 3) == QStringList({"station-1", "platform-1", "node-a1"}) && f.rowTexts(1, 3) == QStringList({"station-1", "platform-2", ""})
			&& f.rowTexts(2, 3) == QStringList({"station-2", "platform-3", ""}) && f.rowTexts(3, 3) == QStringList({"station-2", "platform-2", ""}),
		"each row has its station, id and nodes");
	ok &= expect(f.spin(0, 3) && f.spin(0, 4) && f.spin(0, 3)->objectName() == "platformLengthEdit" && f.spin(0, 4)->objectName() == "platformWidthEdit",
		"the length and the width are number fields");
	ok &= expect(f.spin(0, 3)->value() == 150.0 && f.spin(0, 4)->value() == 2.5 && f.spin(1, 3)->value() == 100.0 && f.spin(1, 4)->value() == 2.5,
		"a platform that states no size shows the default size");
	ok &= expect(!f.model().stations[0].platforms[1].hasLength && !f.model().stations[0].platforms[1].hasWidth && f.untouched() && !f.document.isDirty(),
		"listing platforms edits nothing, and states no size that the scene does not");

	// two platforms of different stations may have the same id, and are told apart by their station
	f.table()->setCurrentCell(3, 0);
	ok &= expect(f.panel.selectionId() == "station-2\nplatform-2", "a platform is selected by its station and its id");
	f.table()->setCurrentCell(1, 0);
	ok &= expect(f.panel.selectionId() == "station-1\nplatform-2", "the other platform with that id has its own selection");
	return ok;
}

static bool testPlatformEdit() {
	bool ok = true;
	Fixture f;
	f.choose("platforms");

	// nodes
	f.type(0, 2, " node-a0 , node-a2 ,, ");
	ok &= expect(f.model().stations[0].platforms[0].nodeIds == std::vector<std::string>({"node-a0", "node-a2"}) && f.changedOnce("Edit platform", "platforms"),
		"the nodes of a platform are a list that drops empty items");
	f.type(0, 2, "node-a0,node-a2");
	ok &= expect(f.untouched(), "the same nodes change nothing");
	f.type(0, 2, "");
	ok &= expect(f.model().stations[0].platforms[0].nodeIds.empty() && f.changedOnce("Edit platform", "platforms"), "a platform can have no nodes");

	// rename: only the stops at the platform of that station
	f.type(1, 1, " platform-x ");
	ok &= expect(f.model().stations[0].platforms[1].id == "platform-x" && f.model().services[0].stops[2].platformId == "platform-2"
			&& f.model().services[0].stops[0].platformId == "platform-1" && f.model().stations[1].platforms[1].id == "platform-2",
		"a renamed platform leaves the stops at the platform of another station with that id as they are");
	ok &= expect(f.changedOnce("Rename platform", "platforms") && f.panel.selectionId() == "station-1\nplatform-x" && f.table()->currentRow() == 1,
		"a rename marks the document once, and the renamed platform stays selected");
	f.type(0, 1, "platform-x");
	ok &= expect(f.untouched() && f.model().stations[0].platforms[0].id == "platform-1" && f.messages == QStringList({"IDs must be unique and not empty"}),
		"an id that another platform of the station has is refused");
	f.type(0, 1, "");
	ok &= expect(f.untouched() && f.messages.size() == 2, "an empty id is refused");
	f.type(1, 1, "platform-1");
	ok &= expect(f.untouched() && f.model().stations[0].platforms[1].id == "platform-x" && f.messages.size() == 3, "the id of another platform of the station is refused");
	f.type(3, 1, "platform-1");
	ok &= expect(f.model().stations[1].platforms[1].id == "platform-1" && f.model().services[0].stops[2].platformId == "platform-1"
			&& f.model().services[0].stops[2].stationId == "station-2" && f.model().services[0].stops[0].stationId == "station-1"
			&& f.model().services[0].stops[0].platformId == "platform-1" && f.changedOnce("Rename platform", "platforms"),
		"an id that is free in a station is taken even when another station has it, and only the stops at that station follow");
	return ok;
}

static bool testPlatformMove() {
	bool ok = true;
	Fixture f;
	f.choose("platforms");

	// the platform goes to the end of the platforms of the other station, and its stops go with it
	f.type(0, 0, "station-2");
	const SceneModel& model = f.model();
	ok &= expect(model.stations[0].platforms.size() == 1 && model.stations[0].platforms[0].id == "platform-2" && model.stations[1].platforms.size() == 3
			&& model.stations[1].platforms[2].id == "platform-1" && model.stations[1].platforms[2].nodeIds == std::vector<std::string>({"node-a1"})
			&& model.stations[1].platforms[2].hasLength && model.stations[1].platforms[2].lengthM == 150.0,
		"a platform moves to the station that is named, with its nodes and size");
	ok &= expect(model.services[0].stops[0].stationId == "station-2" && model.services[0].stops[0].platformId == "platform-1"
			&& model.services[0].stops[1].stationId == "station-2" && model.services[0].stops[1].platformId == "platform-3",
		"the stops at the platform move with it");
	ok &= expect(f.changedOnce("Edit platform", "platforms") && f.panel.selectionId() == "station-2\nplatform-1" && f.table()->currentRow() == 3
			&& f.rowTexts(3, 2) == QStringList({"station-2", "platform-1"}),
		"a move marks the document once, and the platform stays selected in its new row");
	f.type(3, 0, "station-2");
	ok &= expect(f.untouched(), "naming the station that the platform is in changes nothing");

	// refused
	f.type(0, 0, "nowhere");
	ok &= expect(f.untouched() && f.model().stations[0].platforms.size() == 1 && f.cell(0, 0) == "station-1"
			&& f.messages == QStringList({"Platform Station ID must name an existing station"}),
		"a station that the scene does not have is refused, with a message, and the table shows the model again");
	f.type(0, 0, "station-2");
	ok &= expect(f.untouched() && f.model().stations[0].platforms.size() == 1 && f.model().stations[1].platforms.size() == 3
			&& f.messages.size() == 2 && f.messages.back() == "Platform ID already exists on the target station",
		"a platform is not moved to a station that has a platform with its id");
	return ok;
}

static bool testPlatformGeometry() {
	bool ok = true;
	Fixture f;
	f.choose("platforms");

	// a field is the widget that is edited, so the table keeps it
	QDoubleSpinBox* length = f.spin(1, 3);
	length->setValue(200.5);
	const ScenePlatform& second = f.model().stations[0].platforms[1];
	ok &= expect(second.hasLength && second.lengthM == 200.5 && !second.hasWidth && f.editedOnce("Edit platform") && f.spin(1, 3) == length,
		"a length is taken as soon as the field changes, and the table keeps the field");
	f.spin(1, 4)->setValue(3.25);
	ok &= expect(second.hasWidth && second.widthM == 3.25 && f.editedOnce("Edit platform"), "a width is taken the same way");
	length->setValue(200.5);
	ok &= expect(f.untouched(), "a value that is the same changes nothing");
	f.spin(0, 4)->setValue(2.75);
	ok &= expect(f.model().stations[0].platforms[0].hasWidth && f.model().stations[0].platforms[0].widthM == 2.75 && f.editedOnce("Edit platform"),
		"a size that the scene did not state is stated once the field changes");

	// a field that has been typed in and not left
	f.panel.show();
	f.panel.activateWindow();
	QApplication::processEvents();
	QDoubleSpinBox* typed = f.spin(2, 3);
	typed->setFocus();
	QApplication::processEvents();
	QLineEdit* text = typed->findChild<QLineEdit*>();
	text->setText("77");
	emit text->textEdited("77");
	f.panel.commitPendingEditors();
	ok &= expect(f.model().stations[1].platforms[0].hasLength && f.model().stations[1].platforms[0].lengthM == 77.0 && f.editedOnce("Edit platform"),
		"a length that has been typed in is taken when the editors are committed");
	f.panel.commitPendingEditors();
	ok &= expect(f.untouched(), "it is taken once");
	f.panel.hide();
	return ok;
}

static bool testPlatformAddAndDelete() {
	bool ok = true;
	Fixture f;
	f.choose("platforms");
	f.panel.addButton()->click();
	const ScenePlatform& added = f.model().stations[0].platforms.back();
	ok &= expect(f.model().stations[0].platforms.size() == 3 && added.id == "platform" && added.nodeIds.empty() && !added.hasLength && !added.hasWidth
			&& f.changedOnce("Add platform", "platforms") && f.panel.selectionId() == "station-1\nplatform" && f.table()->currentRow() == 2,
		"a platform is added to the first station, with a new id, and selected");

	// delete
	f.select(0);
	f.panel.deleteButton()->click();
	ok &= expect(f.prompts.errorCount == 1 && f.prompts.confirmCount == 0 && f.model().stations[0].platforms.size() == 3 && f.untouched()
			&& f.prompts.errorMessage == blocked("Platform", "platform-1", {"service 'service-1' stop at station 'station-1'"}),
		"a platform that a stop uses cannot be deleted");
	f.select(2);
	f.panel.deleteButton()->click();
	ok &= expect(f.prompts.confirmCount == 1 && f.prompts.confirmQuestion == "Delete platform 'platform'?" && f.model().stations[0].platforms.size() == 3 && f.untouched(),
		"nothing is deleted when the user answers no");
	f.prompts.confirmAnswer = true;
	f.panel.deleteButton()->click();
	ok &= expect(f.model().stations[0].platforms.size() == 2 && f.changedOnce("Delete platform", "platforms") && f.panel.selectionId() == "station-2\nplatform-3",
		"a platform that nothing uses is deleted when the user answers yes");
	// a stop at a platform of the other station with the same id does not block this one
	f.select(1);
	f.panel.deleteButton()->click();
	ok &= expect(f.prompts.errorCount == 1 && f.model().stations[0].platforms.size() == 1 && f.changedOnce("Delete platform", "platforms"),
		"the stops at a platform of another station with that id do not block the delete");

	// no station to add to
	Fixture empty;
	empty.edit().stations.clear();
	empty.choose("platforms");
	empty.panel.addEntity();
	ok &= expect(empty.untouched() && empty.messages == QStringList({"Add a station before adding a platform"}), "a platform is not added without a station");
	return ok;
}

static bool testBoundaries() {
	bool ok = true;
	Fixture f;
	f.choose("station_boundaries");
	const std::string a1 = sectionOf(f.model(), "block-a1");
	ok &= expect(f.headers() == QStringList({"Entrance", "Exit", "Direction"}) && f.table()->rowCount() == 1, "station boundaries are listed with their sections");
	ok &= expect(f.combo(0, 0) && f.combo(0, 1) && f.combo(0, 0)->objectName() == "boundaryEntranceCombo" && f.combo(0, 1)->objectName() == "boundaryExitCombo"
			&& f.combo(0, 0)->currentData().toString().toStdString() == a1 && f.combo(0, 1)->currentText() == "(none)" && f.cell(0, 2) == "false",
		"the entrance and the exit are chosen in combos of the sections, and the exit can be none");
	ok &= expect(f.untouched() && !f.document.isDirty(), "listing station boundaries edits nothing");

	// edit
	QComboBox* entrance = f.combo(0, 0);
	f.chooseBlock(0, 0, "block-a2");
	ok &= expect(f.model().stationBoundaries[0].entranceBlock == sectionOf(f.model(), "block-a2") && f.editedOnce("Edit station boundary") && f.combo(0, 0) == entrance,
		"another entrance is chosen, which marks the document once and leaves the combo in the table");
	f.chooseBlock(0, 1, "block-a1");
	const SceneStationBoundary& boundary = f.model().stationBoundaries[0];
	ok &= expect(boundary.hasExitBlock && boundary.exitBlock == a1 && f.editedOnce("Edit station boundary"), "an exit is chosen");
	f.chooseBlock(0, 1, "block-a1");
	ok &= expect(f.untouched(), "choosing the exit that is chosen changes nothing");
	f.combo(0, 1)->setCurrentIndex(f.combo(0, 1)->findData(QString()));
	ok &= expect(!boundary.hasExitBlock && boundary.exitBlock.empty() && f.editedOnce("Edit station boundary"), "choosing none clears the exit and its flag together");
	f.type(0, 2, "True");
	ok &= expect(boundary.direction && f.cell(0, 2) == "true" && f.changedOnce("Edit station boundary", "station_boundaries"), "the direction is edited");
	f.type(0, 2, "up");
	ok &= expect(f.untouched() && boundary.direction && f.cell(0, 2) == "true" && f.messages.empty(),
		"a direction that is neither true nor false is refused and the table shows the model again");

	// add
	f.panel.addButton()->click();
	ok &= expect(f.model().stationBoundaries.size() == 2 && f.model().stationBoundaries[1].entranceBlock.empty() && !f.model().stationBoundaries[1].hasExitBlock
			&& f.changedOnce("Add station boundary", "station_boundaries") && f.table()->currentRow() == 1 && f.panel.selectionId().isEmpty(),
		"a station boundary is added empty, and its row is selected");
	ok &= expect(f.combo(1, 0)->currentText() == "(none)" && f.combo(1, 1)->currentText() == "(none)", "an empty entrance and an empty exit show as none");

	// delete: nothing refers to a station boundary
	f.panel.deleteButton()->click();
	ok &= expect(f.prompts.errorCount == 0 && f.prompts.confirmQuestion == "Delete station boundary 'incomplete row'?" && f.model().stationBoundaries.size() == 2
			&& f.untouched(),
		"a station boundary that has no entrance yet is named an incomplete row, and stays when the user answers no");
	f.prompts.confirmAnswer = true;
	f.panel.deleteButton()->click();
	ok &= expect(f.model().stationBoundaries.size() == 1 && f.changedOnce("Delete station boundary", "station_boundaries"), "a station boundary is deleted when the user answers yes");
	f.select(0);
	f.panel.deleteButton()->click();
	ok &= expect(f.prompts.confirmQuestion == QString::fromStdString("Delete station boundary '" + sectionOf(f.model(), "block-a2") + "'?")
			&& f.model().stationBoundaries.empty(),
		"a station boundary is named by its entrance");
	return ok;
}

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	bool ok = true;
	ok &= testFacets();
	ok &= testStations();
	ok &= testPlatformList();
	ok &= testPlatformEdit();
	ok &= testPlatformMove();
	ok &= testPlatformGeometry();
	ok &= testPlatformAddAndDelete();
	ok &= testBoundaries();
	return ok ? 0 : 1;
}
