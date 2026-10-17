// SceneModel.h has a member named signals, so it comes before the Qt headers.
#include "scene/SceneModel.h"

#include "widgets/InspectorPanel.h"

#include "app/GuiSimulationSnapshot.h"
#include "graphics/NetworkPresenter.h"
#include "graphics/NetworkScene.h"
#include "graphics/VisualPolish.h"
#include "graphics/items/NodeItem.h"
#include "graphics/items/SignalItem.h"
#include "graphics/items/StationNodeItem.h"
#include "graphics/items/StationOverlayItem.h"
#include "graphics/items/TrainBodyItem.h"
#include "graphics/items/TrainItemGroup.h"
#include "simulation/Infrastructure.h"
#include "widgets/InfoDockWidget.h"

#include <QApplication>
#include <QGraphicsEllipseItem>
#include <QGraphicsScene>
#include <QLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPlainTextEdit>

#include <iostream>
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

	panel.handleDisableHighlight();
	ok &= expect(!panel.dock()->isVisible() && formHidden(panel, "Arc ID") && !arcPreview->graphicsEffect() && !presenter.effect,
		"clearing the highlight hides the dock and the form and removes the highlight");

	panel.displayPreviewInfo(nodePreview);
	panel.clearPreviewInspection();
	ok &= expect(!panel.dock()->isVisible() && text(panel, "Node ID").isEmpty(), "clearing the preview empties the fields");

	host.previewing = false;
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
	ok &= expect(host.followed.size() == 1, "showing a train without the follow mode does not follow it");
	panel.clearSelectedTrain();
	ok &= expect(panel.selectedTrainIndex() == -1, "the window can forget the selected train");

	SignalItem signal(QRectF(0.0, 0.0, 4.0, 4.0));
	signal.setAspectCode(0);
	signal.setToolTip("first");
	presenter.m_signalDecorations.append(&signal);
	scene.addItem(&signal);
	panel.displaySignallingInfo(&signal);
	QWidget* signallingBox = field(panel, "Signals at location");
	ok &= expect(host.signalInspections == 1 && text(panel, "Aspect") == "Stop" && text(panel, "Signals at location") == "first",
		"selecting a signal shows its aspect and its details");
	signal.setAspectCode(180);
	signal.setToolTip("second");
	panel.refreshInspectedSignal();
	ok &= expect(text(panel, "Aspect") == "Proceed" && text(panel, "Signals at location") == "second",
		"the signal that is shown follows its aspect");

	// In a narrow dock the box has the width of the form and does not share it with the label. The label column of the
	// form wraps at one width and not at another, so the widths around 300 are tried.
	QWidget* signalForm = signallingBox->parentWidget();
	const QMargins margins = signalForm->layout()->contentsMargins();
	bool boxUsesForm = true;
	for (int width : {300, 340, 380}) {
		panel.dock()->setFixedWidth(width);
		QCoreApplication::processEvents();
		boxUsesForm &= signallingBox->width() >= signalForm->width() - margins.left() - margins.right();
	}
	ok &= expect(boxUsesForm, "in a narrow dock the signals at location use the width of the form");
	panel.handleCloseInfoDockWidget();
	presenter.m_signalDecorations.clear();

	return ok ? 0 : 1;
}
