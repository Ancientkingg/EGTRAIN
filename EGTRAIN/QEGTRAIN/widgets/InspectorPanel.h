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

/// The inspector dock, titled after the selected item. It has one form for each kind of item that the user can select on the canvas: a node, an arc,
/// a connection, a signal and a train. Selecting an item shows its form, fills it in and puts a highlight on the item.
/// Before a run the items are the ones of the track preview, which the form fills in from the scene. During and after a
/// run the items belong to the run and the form fills in from them. The panel keeps what is selected, so that the window
/// can mark the station overlay and the train of the selection.
class InspectorPanel : public QObject {
	Q_OBJECT

public:
	/// What the panel asks of the window.
	class Host {
	public:
		virtual ~Host() = default;

		/// True while the canvas shows the track preview of the scene and not a run.
		virtual bool showingTrackPreview() const = 0;
		/// The scene that the document holds, or null while it holds none.
		virtual const SceneModel* loadedScene() const = 0;
		/// The revision of the scene that the document holds.
		virtual quint64 loadedSceneRevision() const = 0;
		/// The region of the block set of a track of the run.
		virtual int regionOfTrack(int track) const = 0;
		/// True when the run has passengers.
		virtual bool passengerGui() const = 0;
		/// The details of a passenger in the displayed snapshot; only the id is known when the snapshot does not list the
		/// passenger.
		virtual GuiPassengerState passengerState(const std::string& id) const = 0;
		/// True when the displayed step is a step of a replay.
		virtual bool replayActive() const = 0;

		/// Closes the context menu of the scene when one is open.
		virtual void closeSceneContextMenu() = 0;
		/// The selection or the highlight changed, so the overlays of the viewport are drawn again.
		virtual void inspectionChanged() = 0;
		/// The signal that the panel shows was selected.
		virtual void signalInspected() = 0;
		/// Follows the train with this index, or no train for -1.
		virtual void followTrain(int trainIndex) = 0;
	};

	/// Adds the dock to the right side of the window and hides it. The window owns the panel and the dock. The scene is the
	/// one that the items are on, and the presenter holds the highlight and the passenger information of the selection.
	/// The presenter and the host must stay valid while the panel handles calls and signals.
	InspectorPanel(QMainWindow* window, QGraphicsScene* scene, NetworkPresenter& presenter, Host& host);

	/// The dock that holds the forms; owned by the window.
	InfoDockWidget* dock() const { return infoDockWidget; }

	/// The value in fixed notation with precision digits after the decimal point.
	static std::string to_string_precision(double value, int precision);

	/// The index of the train whose details are shown, or -1.
	int selectedTrainIndex() const { return m_selectedTrainIndex; }
	/// True when the overlay belongs to the selected station.
	bool selectsStationOverlay(const StationOverlayItem* overlay) const;
	/// Forgets the selected station; the window calls this and clearSelectedTrain when it clears the canvas.
	void clearSelectedStation();
	/// Forgets the selected train.
	void clearSelectedTrain();

	/// Shows the aspect and the signals at the location again for the signal that is shown, when they changed.
	void refreshInspectedSignal();

	/// Closes the context menu of the scene and, when the canvas shows the track preview, the form of the preview, and
	/// empties its fields.
	void clearPreviewInspection();

	/// The item of the preview with this kind and id, or null. The scene must be the one that the item was found in, which
	/// the revision tells. Track and reversed only narrow the search for a signal.
	QGraphicsItem* resolvePreviewItem(const QString& kind, const QString& id, const QString& track, bool reversed,
		quint64 revision) const;

	/// Shows the train and, when changeFollowMode is set, follows it.
	void displayTrainDetails(TrainBodyItem* trainItem, bool changeFollowMode);

public slots:
	/// Hides every form, forgets the selection, removes the passenger information and the highlight. The dock itself stays
	/// as it is. Runs when the dock is closed.
	void handleCloseInfoDockWidget();
	/// Shows the form of an item of the track preview, filled in from the scene. Does nothing for an item of another scene
	/// or while the canvas shows a run.
	void displayPreviewInfo(QGraphicsItem* item);
	/// Shows the form of a node of the run, with its coordinates in metres.
	void displayNodeInfo(NodeItem* el);
	/// Shows the form of a station node of the run and selects its station.
	void displayStationNodeInfo(StationNodeItem* re);
	/// Shows the form of an arc of the run: length in metres, curvature radius in metres, gradient as rise per length and
	/// speed limit in m/s.
	void displayArcInfo(TrackLineItem* line);
	/// Shows the form of a connection of the run, with the positions of its nodes in metres.
	void displayConnectionInfo(ConnectionItem* line);
	/// Shows the form of a signal of the run, with its position and the length of the section ahead in metres.
	void displaySignallingInfo(SignalItem* signal);
	/// Shows the train and follows it.
	void displayTrainInfo(TrainBodyItem* trainItem);
	/// Hides the dock and shows the information of the passenger on the canvas instead.
	void displayPassengerInfo(PassengerItem* paxItem);
	/// Does what handleCloseInfoDockWidget does and hides the dock.
	void handleDisableHighlight();

private:
	// the scripted GUI runs in tests/e2e
	friend class MainWindowTestAccess;

	QPointer<QGraphicsScene> m_scene;
	// Never null. The presenter stays valid while the panel is in use.
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
	QLineEdit* trainNameText = nullptr;
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
