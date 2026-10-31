// SceneModel.h has a member named signals, so it comes before the Qt headers.
#include "scene/SceneModel.h"

#include "widgets/InspectorPanel.h"

#include "simulation/GuiSimulationSnapshot.h"
#include "graphics/NetworkPresenter.h"
#include "tests/NetworkPresenterTestAccess.h"
#include "graphics/NetworkScene.h"
#include "graphics/RuntimeGeometrySource.h"
#include "graphics/VisualPolish.h"
#include "graphics/items/ConnectionItem.h"
#include "graphics/items/NodeItem.h"
#include "graphics/items/SignalItem.h"
#include "graphics/items/StationNodeItem.h"
#include "graphics/items/StationOverlayItem.h"
#include "graphics/items/TrackLineItem.h"
#include "graphics/items/TrainBodyItem.h"
#include "graphics/items/TrainItemGroup.h"
#include "simulation/Infrastructure.h"
#include "simulation/Signalling.h"
#include "widgets/FindDialog.h"
#include "widgets/InfoDockWidget.h"

#include <QAction>
#include <QApplication>
#include <QGraphicsEllipseItem>
#include <QGraphicsScene>
#include <QLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTest>
#include <QTextDocument>

#include <algorithm>
#include <memory>
#include <vector>

// A window that shows the track preview of one scene, or a run, and records what the panel asks of it.
class FakeHost final : public InspectorPanel::Host {
public:
	bool previewing = true;
	bool hasScene = true;
	quint64 revision = 1;
	SceneModel model;
	int inspectionChanges = 0;
	int contextMenuCloses = 0;
	int signalInspections = 0;
	std::vector<int> followed;

	bool showingTrackPreview() const override { return previewing; }
	const SceneModel* loadedScene() const override { return hasScene ? &model : nullptr; }
	quint64 loadedSceneRevision() const override { return revision; }
	int regionOfTrack(int track) const override { return track + 10; }
	bool passengerGui() const override { return false; }
	GuiPassengerState passengerState(const std::string& id) const override {
		GuiPassengerState passenger;
		passenger.id = id;
		return passenger;
	}
	bool replayActive() const override { return false; }
	void closeSceneContextMenu() override { ++contextMenuCloses; }
	void inspectionChanged() override { ++inspectionChanges; }
	void signalInspected() override { ++signalInspections; }
	void followTrain(int trainIndex) override { followed.push_back(trainIndex); }
};

// The tracks of a run of six track lines, which the run numbers by their ids sorted as text: B10 is track 2 and B11
// track 5. The run has nothing else.
class SixTracks final : public RuntimeGeometrySource {
public:
	SixTracks() : m_tracks(6) {
		const char* ids[] = {"B0", "B1", "B10", "B100", "B101", "B11"};
		for (std::size_t track = 0; track < m_tracks.size(); ++track)
			m_tracks[track].sceneTrackId = ids[track];
	}

	int trackCount() const override { return static_cast<int>(m_tracks.size()); }
	BlockSet& track(int index) const override { return m_tracks[static_cast<std::size_t>(index)]; }
	int regionOfTrack(int) const override { return 0; }
	int stationCount() const override { return 0; }
	Stations& station(int) const override { return m_station; }
	int connectionCount() const override { return 0; }
	Connections& connection(int) const override { return m_connection; }
	int sectionCount() const override { return 0; }
	const Section& section(int) const override { return m_section; }
	const Route* route(int) const override { return nullptr; }
	const StationPlatform* platform(const std::string&, const std::string&) const override { return nullptr; }
	bool passengerGui() const override { return false; }

private:
	mutable std::vector<BlockSet> m_tracks;
	mutable Stations m_station;
	mutable Connections m_connection;
	Section m_section;
};

// The field of the form row with this label: the one that is shown when several forms have such a row.
static QWidget* field(const InspectorPanel& panel, const QString& label) {
	QWidget* first = nullptr;
	for (QLabel* candidate : panel.dock()->findChildren<QLabel*>()) {
		if (candidate->text() != label || !candidate->buddy())
			continue;
		if (candidate->buddy()->isVisible())
			return candidate->buddy();
		if (!first)
			first = candidate->buddy();
	}
	return first;
}

// Whether the form that holds the field has been hidden, whatever its parent shows.
static bool formHidden(const InspectorPanel& panel, const QString& label) {
	QWidget* widget = field(panel, label);
	return widget && widget->parentWidget() && widget->parentWidget()->isHidden();
}

// The tooltip of the field of the row, as plain text.
static QString tip(const InspectorPanel& panel, const QString& label) {
	QWidget* widget = field(panel, label);
	if (!widget)
		return QStringLiteral("<no field>");
	QTextDocument document;
	document.setHtml(widget->toolTip());
	return document.toPlainText();
}

// The grey text of the empty field of the row.
static QString placeholder(const InspectorPanel& panel, const QString& label) {
	auto* edit = qobject_cast<QLineEdit*>(field(panel, label));
	return edit ? edit->placeholderText() : QStringLiteral("<no field>");
}

static QString text(const InspectorPanel& panel, const QString& label) {
	QWidget* widget = field(panel, label);
	if (auto* edit = qobject_cast<QLineEdit*>(widget))
		return edit->text();
	if (auto* details = qobject_cast<QPlainTextEdit*>(widget))
		return details->toPlainText();
	return QStringLiteral("<no field>");
}

// Whether the form that holds the field is shown.
static bool formShown(const InspectorPanel& panel, const QString& label) {
	QWidget* widget = field(panel, label);
	return widget && widget->parentWidget() && widget->parentWidget()->isVisible();
}

static QGraphicsEllipseItem* previewItem(QGraphicsScene& scene, const QString& kind, const QString& id) {
	auto* item = new QGraphicsEllipseItem(QRectF(0.0, 0.0, 4.0, 4.0));
	item->setData(PreviewGraphics::Kind, kind);
	item->setData(PreviewGraphics::Id, id);
	scene.addItem(item);
	return item;
}

// A shown window with the panel over a scene whose document holds the node n1 and the arc a1 of track T1. The canvas
// shows the track preview of that scene.
struct PanelFixture {
	QMainWindow window;
	QGraphicsScene scene;
	NetworkPresenter presenter{&scene, nullptr};
	FakeHost host;
	InspectorPanel panel{&window, &scene, presenter, host};

	PanelFixture() {
		SceneNode node;
		node.id = "n1";
		node.trackId = "T1";
		node.xKm = 1.0;
		node.yKm = 2.5;
		host.model.nodes.push_back(node);
		SceneArc arc;
		arc.id = "a1";
		arc.trackId = "T1";
		arc.fromNodeId = "n1";
		arc.toNodeId = "n1";
		arc.speedLimitMs = 20.0;
		host.model.arcs.push_back(arc);
		window.show();
	}

	// The canvas shows a run of six tracks instead of the preview.
	void showRun() {
		host.previewing = false;
		presenter.setGeometrySource(std::make_unique<SixTracks>());
	}

	// Puts a signal head of the run on the canvas and in the signal layer, and selects it.
	void inspectSignal(SignalItem& signal) {
		PresenterAccess::signalDecorations(presenter).append(&signal);
		scene.addItem(&signal);
		panel.displaySignallingInfo(&signal);
	}
};

// A head on track B10 at 1 km that shows Stop, between the sections @b@ behind it and @a@ ahead of it.
static void prepareSignal(SignalItem& signal) {
	signal.setAspectCode(0);
	signal.setToolTip("first");
	signal.trackID = 2;
	signal.sceneTrackId = "B10";
	signal.X = 1.0;
	signal.sectionAheadId = "@a@";
	signal.sectionAheadLabel = "B10 1.000 to 2.000 km";
	signal.sectionAheadLength = 1.0;
	signal.sectionAheadTrackId = 2;
	signal.sectionBehindId = "@b@";
	signal.sectionBehindLabel = "B10 0.000 to 1.000 km";
	signal.sectionBehindTrackId = 2;
}

class TestInspectorPanel : public QObject {
	Q_OBJECT

private slots:
	void writesFixedDecimals();
	void dockStartsHidden();
	void inspectsPreviewNodeAndArc();
	void inspectsPreviewSignal();
	void clearsPreviewInspection();
	void showsRunNode();
	void showsRunStation();
	void showsAndFollowsTrain();
	void showsRunSignal();
	void findsSignalBySection();
	void signalFormUsesDockWidth_data();
	void signalFormUsesDockWidth();
	void showsIncidentButton();
	void protectsSectionBehind();
	void listsSectionsOfArcSignals();
	void namesConnectionTracks();
};

void TestInspectorPanel::writesFixedDecimals() {
	// Numbers are written with a fixed number of decimals.
	QCOMPARE(InspectorPanel::toStringPrecision(1234.5678, 2), "1234.57");
	QCOMPARE(InspectorPanel::toStringPrecision(7.0, 0), "7");
}

void TestInspectorPanel::dockStartsHidden() {
	PanelFixture f;
	// The dock is on the right and hidden until an item is selected.
	QCOMPARE(f.window.dockWidgetArea(f.panel.dock()), Qt::RightDockWidgetArea);
	QVERIFY(!f.panel.dock()->isVisible());
	QCOMPARE(f.panel.dock()->windowTitle(), "Details");
	QCOMPARE(f.panel.dock()->toggleViewAction()->text(), "Details");
	// No train is selected at first.
	QCOMPARE(f.panel.selectedTrainIndex(), -1);
}

void TestInspectorPanel::inspectsPreviewNodeAndArc() {
	PanelFixture f;
	QGraphicsEllipseItem* nodePreview = previewItem(f.scene, "node", "n1");
	QGraphicsEllipseItem* arcPreview = previewItem(f.scene, "arc", "a1");
	f.panel.displayPreviewInfo(nodePreview);
	// Selecting a node of the preview shows the dock with its title.
	QVERIFY(f.panel.dock()->isVisible());
	QCOMPARE(f.panel.dock()->windowTitle(), "Node n1");
	// The menu entry of the dock keeps its name when the title names the item.
	QCOMPARE(f.panel.dock()->toggleViewAction()->text(), "Details");
	// A field that only a run fills is empty, with a placeholder that says so.
	QVERIFY(text(f.panel, "Region").isEmpty());
	QCOMPARE(placeholder(f.panel, "Region"), "After a run");
	// The node form shows the node of the scene.
	QCOMPARE(text(f.panel, "Node"), "n1");
	QCOMPARE(text(f.panel, "Connected tracks"), "T1");
	// Only the node form is shown and the node is highlighted.
	QVERIFY(formShown(f.panel, "Node"));
	QVERIFY(!formShown(f.panel, "Arc"));
	QVERIFY(nodePreview->graphicsEffect() != nullptr);
	// The window is told that the selection changed.
	QCOMPARE_GT(f.host.inspectionChanges, 0);
	// Selecting an item of the preview closes the context menu.
	QCOMPARE_GT(f.host.contextMenuCloses, 0);

	f.panel.displayPreviewInfo(arcPreview);
	// The arc form shows the arc of the scene.
	QCOMPARE(text(f.panel, "Arc"), "a1");
	QCOMPARE(text(f.panel, "Speed limit (m/s)"), "20");
	// Selecting an arc replaces the node form.
	QVERIFY(formShown(f.panel, "Arc"));
	QVERIFY(!formShown(f.panel, "Node"));
	// The highlight moves to the arc.
	QVERIFY(nodePreview->graphicsEffect() == nullptr);
	QVERIFY(arcPreview->graphicsEffect() != nullptr);

	QVERIFY2(f.panel.resolvePreviewItem("node", "n1", QString(), false, 1) == nodePreview, "an item of the preview is found by kind and id");
	QVERIFY2(!f.panel.resolvePreviewItem("node", "n1", QString(), false, 2), "an item of an earlier revision is not found");
	f.host.hasScene = false;
	QVERIFY2(!f.panel.resolvePreviewItem("node", "n1", QString(), false, 1), "no item is found without a scene");
	f.host.hasScene = true;

	// The panel keeps the kind and the id of the selected item of the preview.
	QCOMPARE(f.panel.previewSelectionKind(), "arc");
	QCOMPARE(f.panel.previewSelectionId(), "a1");
}

void TestInspectorPanel::inspectsPreviewSignal() {
	PanelFixture f;
	QGraphicsEllipseItem* arcPreview = previewItem(f.scene, "arc", "a1");
	f.panel.displayPreviewInfo(arcPreview);
	QGraphicsEllipseItem* signalPreview = previewItem(f.scene, "signal", "@b1@");
	signalPreview->setData(PreviewGraphics::TrackId, "T1");
	TrackPreviewSignal head;
	head.trackId = "T1";
	head.sectionId = "@b1@";
	head.sectionLabel = "T1 0.000 to 1.000 km";
	head.label = "sig1 (T1 0.000 to 1.000 km)";
	PresenterAccess::trackPreview(f.presenter).previewSignals.push_back(head);
	sceneSignals(f.host.model).push_back({"sig1", "@b1@"});
	f.panel.displayPreviewInfo(signalPreview);
	// A selected signal head of the preview is known by the section it stands at.
	QCOMPARE(f.panel.previewSelectionKind(), "signal");
	QCOMPARE(f.panel.previewSelectionId(), "@b1@");
	// The title names a signal of the preview by its authored id and the label of its section.
	QCOMPARE(f.panel.dock()->windowTitle(), "Signal sig1 (T1 0.000 to 1.000 km)");
	// The fields of a signal of the preview name its section by its label.
	QCOMPARE(text(f.panel, "Protected section"), "T1 0.000 to 1.000 km");
	QCOMPARE(text(f.panel, "Block section"), "T1 0.000 to 1.000 km");
	// The tooltips of those fields give the id of the section.
	QCOMPARE(tip(f.panel, "Protected section"), "T1 0.000 to 1.000 km\nSection ID: @b1@");
	QCOMPARE(tip(f.panel, "Block section"), "T1 0.000 to 1.000 km\nSection ID: @b1@");
	// The details of a signal of the preview keep the id of its section and name its signals on a line of their own.
	QCOMPARE(text(f.panel, "Signals at location"), "Track T1, section @b1@, forward\nSignals: sig1");
	sceneSignals(f.host.model).clear();
	f.panel.displayPreviewInfo(signalPreview);
	// The details of a signal of the preview leave out the line of signals when the section has none.
	QCOMPARE(text(f.panel, "Signals at location"), "Track T1, section @b1@, forward");
	f.panel.displayPreviewInfo(previewItem(f.scene, "node", "n9"));
	// An item of the preview that the scene does not have leaves nothing selected.
	QVERIFY(f.panel.previewSelectionKind().isEmpty());
	QVERIFY(f.panel.previewSelectionId().isEmpty());
}

void TestInspectorPanel::clearsPreviewInspection() {
	PanelFixture f;
	QGraphicsEllipseItem* nodePreview = previewItem(f.scene, "node", "n1");
	QGraphicsEllipseItem* arcPreview = previewItem(f.scene, "arc", "a1");
	f.panel.displayPreviewInfo(arcPreview);

	f.panel.handleDisableHighlight();
	// Clearing the highlight hides the dock and the form and removes the highlight.
	QVERIFY(!f.panel.dock()->isVisible());
	QVERIFY(formHidden(f.panel, "Arc"));
	QVERIFY(arcPreview->graphicsEffect() == nullptr);
	QVERIFY(!f.presenter.hasHighlight());
	// Clearing the highlight forgets the selected item of the preview.
	QVERIFY(f.panel.previewSelectionKind().isEmpty());
	QVERIFY(f.panel.previewSelectionId().isEmpty());

	f.panel.displayPreviewInfo(nodePreview);
	f.panel.clearPreviewInspection();
	const QList<QLineEdit*> fields = f.panel.dock()->findChildren<QLineEdit*>();
	// Clearing the preview empties the fields of every form.
	QVERIFY(!f.panel.dock()->isVisible());
	QVERIFY(text(f.panel, "Node").isEmpty());
	QVERIFY(std::all_of(fields.begin(), fields.end(), [](const QLineEdit* field) { return field->text().isEmpty(); }));
}

void TestInspectorPanel::showsRunNode() {
	PanelFixture f;
	// A node of the preview was shown before, so its placeholders were set.
	f.panel.displayPreviewInfo(previewItem(f.scene, "node", "n1"));
	f.panel.clearPreviewInspection();
	f.showRun();
	NodeItem runNode(QRectF(0.0, 0.0, 4.0, 4.0));
	Node runData;
	runData.ID = 7;
	runData.X = 1.5;
	runData.Y = 2.25;
	runNode.node = &runData;
	runNode.track = 2;
	f.scene.addItem(&runNode);
	f.panel.displayNodeInfo(&runNode);
	// A node of a run is shown with its position in metres.
	QCOMPARE(f.panel.dock()->windowTitle(), "Node 7");
	QCOMPARE(text(f.panel, "Node"), "7");
	QCOMPARE(text(f.panel, "Y (m)"), "2250.00");
	// A node of a run has no placeholder of the preview.
	QVERIFY(placeholder(f.panel, "Region").isEmpty());
	// A node of a run names its track by its id in the scene, with its number in the tooltip.
	QCOMPARE(text(f.panel, "Track"), "B10");
	QCOMPARE(tip(f.panel, "Track"), "B10\nTrack number: 2");
	// The other fields keep their text as tooltip.
	QCOMPARE(tip(f.panel, "Y (m)"), "2250.00");
	QVERIFY2(f.panel.previewSelectionKind().isEmpty(), "an item of a run is no selected item of the preview");
	f.panel.clearPreviewInspection();
	// The fields of a run stay when the canvas shows no preview.
	QCOMPARE(text(f.panel, "Node"), "7");
}

void TestInspectorPanel::showsRunStation() {
	PanelFixture f;
	f.showRun();
	StationNodeItem runStation(QRectF(0.0, 0.0, 4.0, 4.0));
	Node stationData;
	stationData.ID = 8;
	stationData.stationName = "Alpha";
	stationData.isSignalled = true;
	stationData.numConnections = 1;
	stationData.connectIdBlockSet[0] = 5;
	runStation.node = &stationData;
	runStation.track = 2;
	f.scene.addItem(&runStation);
	f.panel.displayStationNodeInfo(&runStation);
	// A station node shows its name, region, signalling and tracks.
	QCOMPARE(f.panel.dock()->windowTitle(), "Station Alpha");
	QCOMPARE(text(f.panel, "Name"), "Alpha");
	QCOMPARE(text(f.panel, "Region"), "12");
	QCOMPARE(text(f.panel, "Signalled"), "Yes");
	QCOMPARE(text(f.panel, "Connected tracks"), "B10, B11");
	// A station node names its tracks by their ids in the scene, with their numbers in the tooltip.
	QCOMPARE(text(f.panel, "Track"), "B10");
	QCOMPARE(tip(f.panel, "Connected tracks"), "B10, B11\nTrack numbers: 2, 5");
	StationOverlayItem selectedOverlay("Alpha", QPointF(0.0, 0.0), classifyStation());
	selectedOverlay.setSourceIdentities({{8.0, 2}});
	StationOverlayItem otherOverlay("Beta", QPointF(0.0, 0.0), classifyStation());
	otherOverlay.setSourceIdentities({{9.0, 2}});
	// The overlay of the selected station is the one that is marked.
	QVERIFY(f.panel.selectsStationOverlay(&selectedOverlay));
	QVERIFY(!f.panel.selectsStationOverlay(&otherOverlay));
	f.panel.handleCloseInfoDockWidget();
	QVERIFY2(!f.panel.selectsStationOverlay(&selectedOverlay), "closing the dock forgets the selected station");
}

void TestInspectorPanel::showsAndFollowsTrain() {
	PanelFixture f;
	f.showRun();
	TrainItemGroup trainGroup;
	TrainBodyItem trainBody(QPolygonF(), &trainGroup);
	trainBody.index = 3;
	trainGroup.index = 3;
	trainGroup.trainDescription = "IC_DEMO-12";
	trainGroup.trainId = 12;
	trainGroup.trainType = "IC";
	f.scene.addItem(&trainGroup);
	f.panel.displayTrainDetails(&trainBody, true);
	// Selecting a train shows it and follows it.
	QCOMPARE(f.panel.selectedTrainIndex(), 3);
	QVERIFY(f.host.followed == std::vector<int>{3});
	QCOMPARE(text(f.panel, "ID"), "12");
	QCOMPARE(text(f.panel, "Type"), "IC");
	// The train is named as the train list and the Follow status name it.
	QCOMPARE(text(f.panel, "Train"), "IC_DEMO-12");
	trainGroup.trainDescription = "None";
	f.panel.displayTrainDetails(&trainBody, false);
	// A train without a description is named by its place in the list.
	QCOMPARE(text(f.panel, "Train"), "Train 4");
	trainGroup.trainDescription.clear();
	f.panel.displayTrainDetails(&trainBody, false);
	// A train with an empty description is named by its place in the list.
	QCOMPARE(text(f.panel, "Train"), "Train 4");
	// Showing a train without the follow mode does not follow it.
	QCOMPARE(f.host.followed.size(), std::size_t(1));
	f.panel.clearSelectedTrain();
	// The window can forget the selected train.
	QCOMPARE(f.panel.selectedTrainIndex(), -1);
}

void TestInspectorPanel::showsRunSignal() {
	PanelFixture f;
	f.showRun();
	SignalItem signal(QRectF(0.0, 0.0, 4.0, 4.0));
	prepareSignal(signal);
	f.inspectSignal(signal);
	// Selecting a signal shows its aspect and its details.
	QCOMPARE(f.host.signalInspections, 1);
	QCOMPARE(text(f.panel, "Aspect"), "Stop");
	QCOMPARE(text(f.panel, "Signals at location"), "first");
	// The signal form names the tracks by their ids in the scene, with their numbers in the tooltips.
	QCOMPARE(text(f.panel, "Track"), "B10");
	QCOMPARE(tip(f.panel, "Track"), "B10\nTrack number: 2");
	QCOMPARE(text(f.panel, "Next track"), "B10");
	QCOMPARE(tip(f.panel, "Next track"), "B10\nTrack number: 2");
	// The signal form names the section ahead by its label, with its id in the tooltips.
	QCOMPARE(text(f.panel, "Block section"), "B10 1.000 to 2.000 km");
	QCOMPARE(tip(f.panel, "Block section"), "B10 1.000 to 2.000 km\nSection ID: @a@");
	QCOMPARE(text(f.panel, "Protected section"), "B10 1.000 to 2.000 km");
	QCOMPARE(tip(f.panel, "Protected section"), "B10 1.000 to 2.000 km\nSection ID: @a@");
	// The other fields of the signal form keep their text as tooltip.
	QCOMPARE(text(f.panel, "Block section length (m)"), "1000.00");
	QCOMPARE(tip(f.panel, "X (m)"), "1000.00");

	signal.setAspectCode(180);
	signal.setToolTip("second");
	f.panel.refreshInspectedSignal();
	// The signal that is shown follows its aspect.
	QCOMPARE(text(f.panel, "Aspect"), "Proceed");
	QCOMPARE(text(f.panel, "Signals at location"), "second");
}

// Find names the head by its track and position, and the labels and the ids of its sections find it too.
void TestInspectorPanel::findsSignalBySection() {
	PanelFixture f;
	f.showRun();
	StationNodeItem runStation(QRectF(0.0, 0.0, 4.0, 4.0));
	Node stationData;
	stationData.ID = 8;
	stationData.stationName = "Alpha";
	stationData.isSignalled = true;
	stationData.numConnections = 1;
	stationData.connectIdBlockSet[0] = 5;
	runStation.node = &stationData;
	runStation.track = 2;
	f.scene.addItem(&runStation);
	SignalItem signal(QRectF(0.0, 0.0, 4.0, 4.0));
	prepareSignal(signal);
	f.inspectSignal(signal);
	QStringList labels;
	QStringList keys;
	for (const NetworkPresenter::FindTarget& target : f.presenter.findTargets()) {
		labels << target.label;
		keys << target.keys;
	}
	const QString signalLabel("Signal: track B10 at 1 km, facing right");
	// Find names a signal by its track id, position and direction and a station node by its track id.
	QVERIFY(labels.contains(signalLabel));
	QVERIFY(labels.contains("Station: Alpha (track B10)"));
	FindDialog find;
	find.setLabels(labels, keys);
	auto* findField = find.findChild<QLineEdit*>();
	auto* results = find.findChild<QListWidget*>();
	if (findField == nullptr || results == nullptr)
		QFAIL("Find finds a signal by the label of its section");
	const auto matches = [findField, results](const QString& words) {
		findField->setText(words);
		QStringList shown;
		for (int row = 0; row < results->count(); ++row)
			shown << results->item(row)->text();
		return shown;
	};
	// Find finds a signal by the label of its section.
	QCOMPARE(matches("b10 1.000 to 2.000"), QStringList{signalLabel});
	// Find finds a signal by the id of its section, also with words of its name.
	QCOMPARE(matches("@a@"), QStringList{signalLabel});
	QCOMPARE(matches("signal @B@ right"), QStringList{signalLabel});
	// A section id finds no other item.
	QVERIFY(matches("@c@").isEmpty());
	QCOMPARE(matches("Alpha"), QStringList{"Station: Alpha (track B10)"});
}

void TestInspectorPanel::signalFormUsesDockWidth_data() {
	QTest::addColumn<int>("width");
	for (int width : {300, 340, 380})
		QTest::addRow("%d px", width) << width;
}

// In a narrow dock the box has the width of the form and does not share it with the label. The label column of the
// form wraps at one width and not at another, so the widths around 300 are tried.
void TestInspectorPanel::signalFormUsesDockWidth() {
	QFETCH(int, width);
	PanelFixture f;
	f.showRun();
	SignalItem signal(QRectF(0.0, 0.0, 4.0, 4.0));
	prepareSignal(signal);
	f.inspectSignal(signal);
	QWidget* signallingBox = field(f.panel, "Signals at location");
	if (signallingBox == nullptr)
		QFAIL("the signal form has signals at location");
	QWidget* signalForm = signallingBox->parentWidget();
	const QMargins margins = signalForm->layout()->contentsMargins();
	f.panel.dock()->setFixedWidth(width);
	QCoreApplication::processEvents();
	// In a dock this wide the signals at location use the width of the form.
	QCOMPARE_GT(signalForm->width(), 150);
	QCOMPARE_GE(signallingBox->width(), signalForm->width() - margins.left() - margins.right());
}

// Under the form the window can show a button that adds an incident for the signal. A disabled one says why in its
// tooltip, a click tells the window, and closing the form hides it.
void TestInspectorPanel::showsIncidentButton() {
	PanelFixture f;
	f.showRun();
	SignalItem signal(QRectF(0.0, 0.0, 4.0, 4.0));
	prepareSignal(signal);
	f.inspectSignal(signal);
	auto* incident = f.panel.dock()->findChild<QPushButton*>("inspectorIncidentButton");
	int requests = 0;
	QObject::connect(&f.panel, &InspectorPanel::incidentRequested, &f.panel, [&requests] { ++requests; });
	if (incident == nullptr)
		QFAIL("the panel shows no incident button until the window asks for one");
	QVERIFY2(incident->isHidden(), "the panel shows no incident button until the window asks for one");
	f.panel.showIncidentAction("Add signal failure", QString());
	// The window shows the incident button under the form of the signal.
	QVERIFY(incident->isVisible());
	QVERIFY(incident->isEnabled());
	QCOMPARE(incident->text(), "Add signal failure");
	QCOMPARE(incident->accessibleName(), "Add signal failure");
	QVERIFY(incident->toolTip().isEmpty());
	incident->click();
	// A click on the incident button tells the window.
	QCOMPARE(requests, 1);
	f.panel.showIncidentAction("Add signal failure", "Not available while the case runs");
	// A disabled incident button says why in its tooltip.
	QVERIFY(!incident->isEnabled());
	QCOMPARE(incident->toolTip(), "Not available while the case runs");
	f.panel.handleCloseInfoDockWidget();
	QVERIFY2(incident->isHidden(), "closing the form hides the incident button");
}

// A head with a section behind it only names that one as the protected section.
void TestInspectorPanel::protectsSectionBehind() {
	PanelFixture f;
	f.showRun();
	SignalItem signal(QRectF(0.0, 0.0, 4.0, 4.0));
	prepareSignal(signal);
	signal.sectionAheadId.clear();
	signal.sectionAheadLabel.clear();
	signal.sectionAheadTrackId = -1;
	f.inspectSignal(signal);
	// A head without a section ahead protects the section behind it.
	QCOMPARE(text(f.panel, "Protected section"), "B10 0.000 to 1.000 km");
	QCOMPARE(tip(f.panel, "Protected section"), "B10 0.000 to 1.000 km\nSection ID: @b@");
	QCOMPARE(text(f.panel, "Block section"), "None");
	QCOMPARE(tip(f.panel, "Block section"), "None");
}

// An arc lists the sections of the signals on it by their labels, with the ids in the tooltip. The signal on it has
// the sections ahead and behind it, and no track ahead.
void TestInspectorPanel::listsSectionsOfArcSignals() {
	PanelFixture f;
	f.showRun();
	SignalItem signal(QRectF(0.0, 0.0, 4.0, 4.0));
	prepareSignal(signal);
	signal.sectionAheadTrackId = -1;
	f.inspectSignal(signal);
	Arc arcData;
	arcData.ID = 3;
	arcData.startNode.X = 0.5;
	arcData.endNode.X = 1.5;
	TrackLineItem runArc(QLineF(0.0, 0.0, 4.0, 0.0));
	runArc.arc = &arcData;
	runArc.track = 2;
	f.scene.addItem(&runArc);
	PresenterAccess::allSignals(f.presenter).append(&signal);
	f.panel.displayArcInfo(&runArc);
	// An arc of a run names its track by its id in the scene.
	QCOMPARE(f.panel.dock()->windowTitle(), "Arc 3");
	QCOMPARE(text(f.panel, "Track"), "B10");
	QCOMPARE(tip(f.panel, "Track"), "B10\nTrack number: 2");
	// An arc names the sections of its signals by their labels, with their ids in the tooltip.
	QCOMPARE(text(f.panel, "Connected signals"), "B10 1.000 to 2.000 km, B10 0.000 to 1.000 km");
	QCOMPARE(tip(f.panel, "Connected signals"), "B10 1.000 to 2.000 km, B10 0.000 to 1.000 km\nSection IDs: @a@, @b@");
}

// A connection names its two tracks by their ids in the scene.
void TestInspectorPanel::namesConnectionTracks() {
	PanelFixture f;
	f.showRun();
	Connections connectionData;
	connectionData.idFirstTrackLine = 2;
	connectionData.idSecondTrackLine = 5;
	connectionData.xFirstNode = 1.0;
	connectionData.xSecondNode = 2.0;
	ConnectionItem runConnection(QLineF(0.0, 0.0, 4.0, 4.0));
	runConnection.connection = &connectionData;
	f.scene.addItem(&runConnection);
	f.panel.displayConnectionInfo(&runConnection);
	// A connection names its tracks by their ids in the scene, with their numbers in the tooltips.
	QCOMPARE(f.panel.dock()->windowTitle(), "Connection");
	QCOMPARE(text(f.panel, "First track"), "B10");
	QCOMPARE(text(f.panel, "Second track"), "B11");
	QCOMPARE(tip(f.panel, "Second track"), "B11\nTrack number: 5");
	QCOMPARE(text(f.panel, "First node X (m)"), "1000.00");
}

QTEST_MAIN(TestInspectorPanel)
#include "test_inspectorpanel.moc"
