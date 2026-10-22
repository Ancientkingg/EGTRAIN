// SceneModel.h has a member named signals, so it comes before the Qt headers.
#include "scene/SceneModel.h"

#include "widgets/InspectorPanel.h"

#include "app/GuiSimulationSnapshot.h"
#include "graphics/NetworkPresenter.h"
#include "graphics/NetworkScene.h"
#include "graphics/RuntimeGeometrySource.h"
#include "graphics/VisualPolish.h"
#include "graphics/items/NodeItem.h"
#include "graphics/items/SignalItem.h"
#include "graphics/items/StationNodeItem.h"
#include "graphics/items/StationOverlayItem.h"
#include "graphics/items/TrainBodyItem.h"
#include "graphics/items/TrainItemGroup.h"
#include "simulation/Infrastructure.h"
#include "simulation/Signalling.h"
#include "widgets/FindDialog.h"
#include "widgets/InfoDockWidget.h"

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
#include <QTextDocument>

#include <iostream>
#include <memory>
#include <vector>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

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

// The field of the form row with this label. The label names one row of the dock.
static QWidget* field(const InspectorPanel& panel, const QString& label) {
	for (QLabel* candidate : panel.dock()->findChildren<QLabel*>())
		if (candidate->text() == label && candidate->buddy())
			return candidate->buddy();
	return nullptr;
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

int main(int argc, char* argv[]) {
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv);
	bool ok = true;

	ok &= expect(InspectorPanel::to_string_precision(1234.5678, 2) == "1234.57" && InspectorPanel::to_string_precision(7.0, 0) == "7",
		"numbers are written with a fixed number of decimals");

	QMainWindow window;
	QGraphicsScene scene;
	NetworkPresenter presenter(&scene, nullptr);
	FakeHost host;
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
	InspectorPanel panel(&window, &scene, presenter, host);
	window.show();

	ok &= expect(window.dockWidgetArea(panel.dock()) == Qt::RightDockWidgetArea && !panel.dock()->isVisible()
			&& panel.dock()->windowTitle() == "Simulation Info",
		"the dock is on the right and hidden until an item is selected");
	ok &= expect(panel.selectedTrainIndex() == -1, "no train is selected at first");

	QGraphicsEllipseItem* nodePreview = previewItem(scene, "node", "n1");
	QGraphicsEllipseItem* arcPreview = previewItem(scene, "arc", "a1");
	panel.displayPreviewInfo(nodePreview);
	ok &= expect(panel.dock()->isVisible() && panel.dock()->windowTitle() == "Preview: Node n1 (not running)",
		"selecting a node of the preview shows the dock with its title");
	ok &= expect(text(panel, "Node ID") == "n1" && text(panel, "Connected tracks") == "T1", "the node form shows the node of the scene");
	ok &= expect(formShown(panel, "Node ID") && !formShown(panel, "Arc ID") && nodePreview->graphicsEffect(),
		"only the node form is shown and the node is highlighted");
	ok &= expect(host.inspectionChanges > 0, "the window is told that the selection changed");
	ok &= expect(host.contextMenuCloses > 0, "selecting an item of the preview closes the context menu");

	panel.displayPreviewInfo(arcPreview);
	ok &= expect(text(panel, "Arc ID") == "a1" && text(panel, "Speed Limit (m/s)") == "20", "the arc form shows the arc of the scene");
	ok &= expect(formShown(panel, "Arc ID") && !formShown(panel, "Node ID"), "selecting an arc replaces the node form");
	ok &= expect(!nodePreview->graphicsEffect() && arcPreview->graphicsEffect(), "the highlight moves to the arc");

	ok &= expect(panel.resolvePreviewItem("node", "n1", QString(), false, 1) == nodePreview, "an item of the preview is found by kind and id");
	ok &= expect(!panel.resolvePreviewItem("node", "n1", QString(), false, 2), "an item of an earlier revision is not found");
	host.hasScene = false;
	ok &= expect(!panel.resolvePreviewItem("node", "n1", QString(), false, 1), "no item is found without a scene");
	host.hasScene = true;

	ok &= expect(panel.previewSelectionKind() == "arc" && panel.previewSelectionId() == "a1",
		"the panel keeps the kind and the id of the selected item of the preview");
	QGraphicsEllipseItem* signalPreview = previewItem(scene, "signal", "@b1@");
	signalPreview->setData(PreviewGraphics::TrackId, "T1");
	TrackPreviewSignal head;
	head.trackId = "T1";
	head.sectionId = "@b1@";
	head.sectionLabel = "T1 0.000 to 1.000 km";
	head.label = "sig1 (T1 0.000 to 1.000 km)";
	presenter.m_cachedTrackPreview.previewSignals.push_back(head);
	sceneSignals(host.model).push_back({"sig1", "@b1@"});
	panel.displayPreviewInfo(signalPreview);
	ok &= expect(panel.previewSelectionKind() == "signal" && panel.previewSelectionId() == "@b1@",
		"a selected signal head of the preview is known by the section it stands at");
	ok &= expect(panel.dock()->windowTitle() == "Preview: Signal sig1 (T1 0.000 to 1.000 km) (not running)",
		"the title names a signal of the preview by its authored id and the label of its section");
	ok &= expect(text(panel, "Protected section") == "T1 0.000 to 1.000 km" && text(panel, "ID of Block Section") == "T1 0.000 to 1.000 km",
		"the fields of a signal of the preview name its section by its label");
	ok &= expect(tip(panel, "Protected section") == "T1 0.000 to 1.000 km\nSection ID: @b1@"
			&& tip(panel, "ID of Block Section") == "T1 0.000 to 1.000 km\nSection ID: @b1@",
		"the tooltips of those fields give the id of the section");
	ok &= expect(text(panel, "Signals at location") == "Track T1; section @b1@; forward; authored signal IDs: sig1",
		"the details of a signal of the preview keep the id of its section");
	panel.displayPreviewInfo(previewItem(scene, "node", "n9"));
	ok &= expect(panel.previewSelectionKind().isEmpty() && panel.previewSelectionId().isEmpty(),
		"an item of the preview that the scene does not have leaves nothing selected");
	panel.displayPreviewInfo(arcPreview);

	panel.handleDisableHighlight();
	ok &= expect(!panel.dock()->isVisible() && formHidden(panel, "Arc ID") && !arcPreview->graphicsEffect() && !presenter.effect,
		"clearing the highlight hides the dock and the form and removes the highlight");
	ok &= expect(panel.previewSelectionKind().isEmpty() && panel.previewSelectionId().isEmpty(),
		"clearing the highlight forgets the selected item of the preview");

	panel.displayPreviewInfo(nodePreview);
	panel.clearPreviewInspection();
	ok &= expect(!panel.dock()->isVisible() && text(panel, "Node ID").isEmpty(), "clearing the preview empties the fields");

	host.previewing = false;
	presenter.setGeometrySource(std::make_unique<SixTracks>());
	NodeItem runNode(QRectF(0.0, 0.0, 4.0, 4.0));
	Node runData;
	runData.ID = 7;
	runData.X = 1.5;
	runData.Y = 2.25;
	runNode.node = &runData;
	runNode.track = 2;
	scene.addItem(&runNode);
	panel.displayNodeInfo(&runNode);
	ok &= expect(panel.dock()->windowTitle() == "Node Info" && text(panel, "Node ID") == "7" && text(panel, "Y (m)") == "2250.00",
		"a node of a run is shown with its position in metres");
	ok &= expect(panel.previewSelectionKind().isEmpty(), "an item of a run is no selected item of the preview");
	panel.clearPreviewInspection();
	ok &= expect(text(panel, "Node ID") == "7", "the fields of a run stay when the canvas shows no preview");

	StationNodeItem runStation(QRectF(0.0, 0.0, 4.0, 4.0));
	Node stationData;
	stationData.ID = 8;
	stationData.stationName = "Alpha";
	stationData.isSignalled = true;
	stationData.numConnections = 1;
	stationData.connectIdBlockSet[0] = 5;
	runStation.node = &stationData;
	runStation.track = 2;
	scene.addItem(&runStation);
	panel.displayStationNodeInfo(&runStation);
	ok &= expect(panel.dock()->windowTitle() == "Station Node Info" && text(panel, "Name") == "Alpha" && text(panel, "Region") == "12"
			&& text(panel, "Signalled") == "Yes" && text(panel, "Connected tracks") == "2, 5",
		"a station node shows its name, region, signalling and tracks");
	StationOverlayItem selectedOverlay("Alpha", QPointF(0.0, 0.0), classifyStation());
	selectedOverlay.setSourceIdentities({{8.0, 2}});
	StationOverlayItem otherOverlay("Beta", QPointF(0.0, 0.0), classifyStation());
	otherOverlay.setSourceIdentities({{9.0, 2}});
	ok &= expect(panel.selectsStationOverlay(&selectedOverlay) && !panel.selectsStationOverlay(&otherOverlay),
		"the overlay of the selected station is the one that is marked");
	panel.handleCloseInfoDockWidget();
	ok &= expect(!panel.selectsStationOverlay(&selectedOverlay), "closing the dock forgets the selected station");

	TrainItemGroup trainGroup;
	TrainBodyItem trainBody(QPolygonF(), &trainGroup);
	trainBody.index = 3;
	trainGroup.index = 3;
	trainGroup.trainDescription = "IC_DEMO-12";
	trainGroup.trainId = 12;
	trainGroup.trainType = "IC";
	scene.addItem(&trainGroup);
	panel.displayTrainDetails(&trainBody, true);
	ok &= expect(panel.selectedTrainIndex() == 3 && host.followed == std::vector<int>{3} && text(panel, "ID") == "12"
			&& text(panel, "Type") == "IC",
		"selecting a train shows it and follows it");
	ok &= expect(text(panel, "Train") == "IC_DEMO-12", "the train is named as the train list and the Follow status name it");
	trainGroup.trainDescription = "None";
	panel.displayTrainDetails(&trainBody, false);
	ok &= expect(text(panel, "Train") == "Train 4", "a train without a description is named by its place in the list");
	trainGroup.trainDescription.clear();
	panel.displayTrainDetails(&trainBody, false);
	ok &= expect(text(panel, "Train") == "Train 4", "a train with an empty description is named by its place in the list");
	ok &= expect(host.followed.size() == 1, "showing a train without the follow mode does not follow it");
	panel.clearSelectedTrain();
	ok &= expect(panel.selectedTrainIndex() == -1, "the window can forget the selected train");

	SignalItem signal(QRectF(0.0, 0.0, 4.0, 4.0));
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
	presenter.m_signalDecorations.append(&signal);
	scene.addItem(&signal);
	panel.displaySignallingInfo(&signal);
	QWidget* signallingBox = field(panel, "Signals at location");
	ok &= expect(host.signalInspections == 1 && text(panel, "Aspect") == "Stop" && text(panel, "Signals at location") == "first",
		"selecting a signal shows its aspect and its details");

	// Find names the head by its labels, and the ids of its sections find it too.
	{
		QStringList labels;
		QStringList keys;
		for (const NetworkPresenter::FindTarget& target : presenter.findTargets()) {
			labels << target.label;
			keys << target.keys;
		}
		const QString signalLabel("Signal: Track B10 at 1 km; ahead B10 1.000 to 2.000 km; behind B10 0.000 to 1.000 km, right");
		ok &= expect(labels.contains(signalLabel) && labels.contains("Station: Alpha (track B10)"),
			"Find names a signal by its track id and section labels and a station node by its track id");
		FindDialog find;
		find.setLabels(labels, keys);
		auto* findField = find.findChild<QLineEdit*>();
		auto* results = find.findChild<QListWidget*>();
		const auto matches = [findField, results](const QString& words) {
			findField->setText(words);
			QStringList shown;
			for (int row = 0; row < results->count(); ++row)
				shown << results->item(row)->text();
			return shown;
		};
		ok &= expect(findField && results && matches("b10 1.000 to 2.000") == QStringList{signalLabel},
			"Find finds a signal by the label of its section");
		ok &= expect(matches("@a@") == QStringList{signalLabel} && matches("signal @B@ right") == QStringList{signalLabel},
			"Find finds a signal by the id of its section, also with words of its name");
		ok &= expect(matches("@c@").isEmpty() && matches("Alpha") == QStringList{"Station: Alpha (track B10)"},
			"a section id finds no other item");
	}
	signal.setAspectCode(180);
	signal.setToolTip("second");
	panel.refreshInspectedSignal();
	ok &= expect(text(panel, "Aspect") == "Proceed" && text(panel, "Signals at location") == "second",
		"the signal that is shown follows its aspect");

	// In a narrow dock the box has the width of the form and does not share it with the label. The label column of the
	// form wraps at one width and not at another, so the widths around 300 are tried.
	QWidget* signalForm = signallingBox->parentWidget();
	const QMargins margins = signalForm->layout()->contentsMargins();
	for (int width : {300, 340, 380}) {
		panel.dock()->setFixedWidth(width);
		QCoreApplication::processEvents();
		ok &= expect(signalForm->width() > 150 && signallingBox->width() >= signalForm->width() - margins.left() - margins.right(),
			QString("in a dock %1 px wide the signals at location use the width of the form").arg(width).toStdString().c_str());
	}
	// Under the form the window can show a button that adds an incident for the signal. A disabled one says why in its
	// tooltip, a click tells the window, and closing the form hides it.
	auto* incident = panel.dock()->findChild<QPushButton*>("inspectorIncidentButton");
	int requests = 0;
	QObject::connect(&panel, &InspectorPanel::incidentRequested, &panel, [&requests] { ++requests; });
	ok &= expect(incident && incident->isHidden(), "the panel shows no incident button until the window asks for one");
	panel.showIncidentAction("Add signal failure", QString());
	ok &= expect(incident && incident->isVisible() && incident->isEnabled() && incident->text() == "Add signal failure"
			&& incident->accessibleName() == "Add signal failure" && incident->toolTip().isEmpty(),
		"the window shows the incident button under the form of the signal");
	if (incident)
		incident->click();
	ok &= expect(requests == 1, "a click on the incident button tells the window");
	panel.showIncidentAction("Add signal failure", "Not available while the case runs");
	ok &= expect(incident && !incident->isEnabled() && incident->toolTip() == "Not available while the case runs",
		"a disabled incident button says why in its tooltip");
	panel.handleCloseInfoDockWidget();
	ok &= expect(incident && incident->isHidden(), "closing the form hides the incident button");
	presenter.m_signalDecorations.clear();

	return ok ? 0 : 1;
}
