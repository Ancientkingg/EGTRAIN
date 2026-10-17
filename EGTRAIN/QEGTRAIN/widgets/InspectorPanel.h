#ifndef INSPECTORPANEL_H
#define INSPECTORPANEL_H

#include <QObject>
#include <QPointer>
#include <QString>

#include <string>

class ConnectionItem;
class InfoDockWidget;
class NetworkPresenter;
class NodeItem;
class PassengerItem;
class QGraphicsItem;
class QGraphicsScene;
class QLineEdit;
class QMainWindow;
class QPlainTextEdit;
class QWidget;
class SignalItem;
class StationNodeItem;
class StationOverlayItem;
class TrackLineItem;
class TrainBodyItem;
struct GuiPassengerState;
struct SceneModel;

// The inspector dock, titled after the selected item. It has one form for each kind of item that the user can select on the canvas: a node, an arc,
// a connection, a signal and a train. Selecting an item shows its form, fills it in and puts a highlight on the item.
// Before a run the items are the ones of the track preview, which the form fills in from the scene. During and after a
// run the items belong to the run and the form fills in from them. The panel keeps what is selected, so that the window
// can mark the station overlay and the train of the selection.
class InspectorPanel : public QObject {
	Q_OBJECT

public:
	// What the panel asks of the window.
	class Host {
	public:
		virtual ~Host() = default;

		// The canvas shows the track preview of the scene and not a run.
		virtual bool showingTrackPreview() const = 0;
		// The scene that the document holds, or null while it holds none.
		virtual const SceneModel* loadedScene() const = 0;
		// The revision of the scene that the document holds.
		virtual quint64 loadedSceneRevision() const = 0;
		// The region of the block set of a track of the run.
		virtual int regionOfTrack(int track) const = 0;
		// The run has passengers.
		virtual bool passengerGui() const = 0;
		// The details of a passenger in the displayed snapshot; only the id is known when the snapshot does not list the
		// passenger.
		virtual GuiPassengerState passengerState(const std::string& id) const = 0;
		// The displayed step is a step of a replay.
		virtual bool replayActive() const = 0;

		// Closes the context menu of the scene when one is open.
		virtual void closeSceneContextMenu() = 0;
		// The selection or the highlight changed, so the overlays of the viewport are drawn again.
		virtual void inspectionChanged() = 0;
		// The signal that the panel shows was selected.
		virtual void signalInspected() = 0;
		// Follows a train, or no train for -1.
		virtual void followTrain(int trainIndex) = 0;
	};

	// Adds the dock to the right side of the window and hides it. The window owns the panel and the dock. The scene is the
	// one that the items are on, and the presenter holds the highlight and the passenger information of the selection.
	InspectorPanel(QMainWindow* window, QGraphicsScene* scene, NetworkPresenter& presenter, Host& host);

	InfoDockWidget* dock() const { return infoDockWidget; }

	// double to string with precision
	static std::string to_string_precision(double value, int precision);

	// The train whose details are shown, or -1.
	int selectedTrainIndex() const { return m_selectedTrainIndex; }
	// The station overlay is the one of the selected station.
	bool selectsStationOverlay(const StationOverlayItem* overlay) const;
	// Forget the selected station and the selected train. The window does so when it clears the canvas.
	void clearSelectedStation();
	void clearSelectedTrain();

	// Shows the aspect and the signals at the location again for the signal that is shown, when they changed.
	void refreshInspectedSignal();

	// Closes the context menu of the scene and, when the canvas shows the track preview, the form of the preview, and
	// empties its fields.
	void clearPreviewInspection();

	// The item of the preview with this kind and id, or null. The scene must be the one that the item was found in, which
	// the revision tells.
	QGraphicsItem* resolvePreviewItem(const QString& kind, const QString& id, const QString& track, bool reversed,
		quint64 revision) const;

	// Shows the train and, when changeFollowMode is set, follows it.
	void displayTrainDetails(TrainBodyItem* trainItem, bool changeFollowMode);

public slots:
	// hides all widgets from the dock widget
	// removes highlight from last clicked item
	void handleCloseInfoDockWidget();
	void displayPreviewInfo(QGraphicsItem* item);
	void displayNodeInfo(NodeItem* el);
	void displayStationNodeInfo(StationNodeItem* re);
	void displayArcInfo(TrackLineItem* line);
	void displayConnectionInfo(ConnectionItem* line);
	void displaySignallingInfo(SignalItem* signal);
	void displayTrainInfo(TrainBodyItem* trainItem);
	void displayPassengerInfo(PassengerItem* paxItem);
	void handleDisableHighlight();

private:
	// the scripted GUI runs in tests/e2e
	friend class MainWindowTestAccess;

	QPointer<QGraphicsScene> m_scene;
	// Never null. The presenter outlives the panel.
	NetworkPresenter* m_presenter;
	Host& m_host;

	InfoDockWidget* infoDockWidget = nullptr;
	QWidget* arcInfoWidget = nullptr;
	QLineEdit* arcIDText = nullptr;
	QLineEdit* arcFirstNodeIDText = nullptr;
	QLineEdit* arcSecondNodeIDText = nullptr;
	QLineEdit* arcTrackIDText = nullptr;
	QLineEdit* arcLengthText = nullptr;
	QLineEdit* arcCurvatureText = nullptr;
	QLineEdit* arcGradientText = nullptr;
	QLineEdit* arcSpeedLimitText = nullptr;
	QLineEdit* arcOperationalStateText = nullptr;
	QLineEdit* arcConnectedSignalsText = nullptr;
	QWidget* nodeInfoWidget = nullptr;
	QLineEdit* nodeIDText = nullptr;
	QLineEdit* nodeTrackIDText = nullptr;
	QLineEdit* nodeXText = nullptr;
	QLineEdit* nodeYText = nullptr;
	QLineEdit* nodeStationNameText = nullptr;
	QLineEdit* nodeRegionText = nullptr;
	QLineEdit* nodeConnectedTracksText = nullptr;
	QLineEdit* nodeSignalledText = nullptr;
	QWidget* connectionInfoWidget = nullptr;
	QLineEdit* connectionFirstTrackIDText = nullptr;
	QLineEdit* connectionSecondTrackIDText = nullptr;
	QLineEdit* connectionXFirstNodeText = nullptr;
	QLineEdit* connectionXSecondNodeText = nullptr;
	QWidget* signallingInfoWidget = nullptr;
	QLineEdit* signallingTrackIDText = nullptr;
	QLineEdit* signallingXText = nullptr;
	QLineEdit* signallingIDSectionAheadText = nullptr;
	QLineEdit* signallingLengthSectionAheadText = nullptr;
	QLineEdit* signallingAspectText = nullptr;
	QLineEdit* signallingProtectedSectionText = nullptr;
	QLineEdit* signallingNextTrackText = nullptr;
	QPlainTextEdit* signallingGroupDetails = nullptr;
	QWidget* trainInfoWidget = nullptr;
	QLineEdit* trainIDText = nullptr;
	QLineEdit* trainTypeText = nullptr;
	QLineEdit* trainLengthText = nullptr;
	QLineEdit* trainWagonsText = nullptr;

	// What is selected. A station of a run is known by its node and track, or by its name when the overlay has no node. A
	// station of the preview is known by its id.
	QString m_selectedStationName;
	QString m_previewSelectedStationId;
	bool m_hasSelectedStationIdentity = false;
	double m_selectedStationNodeId = 0.0;
	int m_selectedStationTrack = -1;
	int m_selectedTrainIndex = -1;
};

#endif // INSPECTORPANEL_H
