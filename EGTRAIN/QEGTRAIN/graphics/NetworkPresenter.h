#ifndef NETWORKPRESENTER_H
#define NETWORKPRESENTER_H

#include <QGraphicsScene>
#include <QList>
#include <QMap>
#include <QPen>
#include <QPixmap>
#include <QPointF>
#include <QPointer>
#include <QPolygonF>
#include <QRectF>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "graphics/NetworkView.h"
#include "graphics/items/HighlightEffect.h"
#include "scene/TrackPreview.h"

class Arc;
class Connections;
class Node;
class PassengerItem;
class PlatformItem;
class QGraphicsItem;
class QGraphicsItemGroup;
class QGraphicsPixmapItem;
class QVariantAnimation;
class RuntimeGeometrySource;
class SignalItem;
class StationOverlayItem;
class TrackLineItem;
class TrainBadgeItem;
class TrainBodyItem;
class TrainItemGroup;
struct GuiPassengerState;
struct GuiPlatformState;
struct GuiSignalState;
struct GuiSimulationSnapshot;
struct GuiTrainState;
struct SceneModel;
struct SceneStationView;
struct StationVisual;

// Draws the track preview and the items of a run on the network canvas and keeps track of the items on
// it. The scene owns every item; the lists, maps and pointers below only observe them and are never
// used to delete one. They are public until all the code that fills them is in this class.
//
// clear() deletes every item of the scene and then forgets all the pointers to them. A pointer
// that stays after the scene is cleared would dangle, so the registries belong with the code that
// clears the scene.
class NetworkPresenter {
public:
	// What the preview needs to know besides the model.
	struct PreviewOptions {
		// The facet and the id of the row that the infrastructure panel selects; empty without one.
		std::string selectedFacet;
		std::string selectedId;
		// The infrastructure panel is shown, so the arc or connection that it selects is marked.
		bool markSelection = false;
		// A station node gets a platform bar.
		bool platformBars = false;
	};

	// What showPreview drew.
	struct PreviewOutcome {
		// The model has track lines to preview.
		bool available = false;
		// The track lines that have a drawable shape.
		int trackLines = 0;
		// The view was fitted to the drawn track lines.
		bool fitted = false;
	};

	// What a snapshot is a step of.
	enum class Playback {
		Live,  // the run that is being simulated
		Replay // the replay of a finished run
	};

	// The parts of a frame that are not on the canvas: the follow camera, the legend and the signal inspector.
	// apply() calls them at the points of a frame where they belong. Without a host no train is selected or followed.
	class FrameHost {
	public:
		virtual ~FrameHost() = default;

		// The overlay of the train is drawn in full, because the train is selected or followed.
		virtual bool isTrainOverlayPromoted(int train) const = 0;
		// A train of the snapshot is about to be updated. The frame can be one in which the train has already left.
		virtual void trainWillUpdate(int train) = 0;
		// The train has the shape and the place of the frame. Tells whether this moved the camera to the train.
		virtual bool trainPlaced(int train, const QPointF& center) = 0;
		// The train has left the network and is hidden.
		virtual void trainHidden(int train) = 0;
		// The train was added to the canvas. Tells whether this moved the camera to the train.
		virtual bool trainAdded(int train, bool firstLiveTrain) = 0;
		// The signals show the aspects of the frame.
		virtual void signalsApplied() = 0;
		// Every train has been updated. A train was added, or the camera moved to the followed train.
		virtual void trainsApplied(bool anyAdded, bool cameraMoved) = 0;
	};

	NetworkPresenter(QGraphicsScene* scene, NetworkView* view);
	~NetworkPresenter();

	// Sets who takes the parts of a frame that are not on the canvas. The presenter does not own the host.
	void setFrameHost(FrameHost* host);

	// Stops the movement of the trains, deletes every item of the scene and empties every registry. The
	// signal under inspection is forgotten first, so nothing reads it while the scene deletes its items.
	void clear();

	// Sets where the items of a run take the infrastructure from. Without a source nothing of a run
	// is drawn.
	void setGeometrySource(std::unique_ptr<RuntimeGeometrySource> source);

	// Lays out the stations of the run and draws its connections, track lines, nodes, station
	// overlays, platforms and signals on the scene, which holds no item. It then indexes the signals
	// and the track lines. The caller clears the scene first. A drawn item points to its arc, node
	// or connection in the source, which therefore keeps them until the next clear().
	void showRuntime(const SceneModel& sceneModel);

	// Shows one step of a run: the signal aspects and the trains, and with the passenger GUI the platforms and the
	// passenger information. The signals, the track lines and the platforms of the run are on the canvas by showRuntime.
	void apply(const GuiSimulationSnapshot& snapshot, Playback playback);

	// Adds, moves and hides the trains of the snapshot at time t and shows the state of the sections on the track lines.
	// apply() does this; it is public so that the trains can be updated alone.
	void updateTrainPosition(const GuiSimulationSnapshot& snapshot, int t, Playback playback);

	// Draws the passenger information of the selected train and of the selected passenger icon again, or removes it.
	// The snapshot gives the details of the passenger and can be null. When the passenger layer is shown after steps
	// were applied while it was hidden, the snapshot also brings the platforms up to date.
	void updateTrainPaxInfo();
	void updatePaxIconInfo(const GuiSimulationSnapshot* snapshot, Playback playback);
	void removeTrainPaxInfoIcon();
	void removePaxInfoIcon();

	// Shows every track line as free.
	void clearOperationalTrackStates();

	// Stops the movement of one train, or of every train, between its last two places.
	void stopTrainAnimation(int train);
	void stopTrainAnimations();

	// Whether the labels of the platforms show: the passenger layer is shown and the view is zoomed in far enough to
	// read them.
	bool paxTextVisible() const;

	// The line of the preview that a track line of the run is drawn on, or null.
	const TrackPreviewLine* cachedTrackLine(int track) const;

	// Adds a train of the run with one body for each wagon and its badge. The train is promoted
	// when it is selected or followed.
	void paintTrain(const GuiTrainState& train, const SceneModel& sceneModel, bool promoted);

	// Gives each body of the list the shape of its wagon at the present position of the train, and
	// tells whether one of them has a shape.
	bool getTrainPolygonItemList(QList<TrainBodyItem*>* trainPolygonItemList, const GuiTrainState& train);

	// The shape of one wagon of a train. The polygon stays empty when the train has no route or the
	// wagon has no position.
	void getTrainPolygon(QPolygonF* trainPolygon, int wagon, const GuiTrainState& train);

	// Adds a station node of a track line. The node is darker at the end of a line when its platform
	// is known.
	void paintStationNode(QPointF coord, int size, int track, Node* node, bool lineEnd);

	// Shows the passengers on board above a train, the details of a passenger above its icon, and
	// the message of a virtual coupling above a train.
	void paintTrainPassengerInfo(TrainItemGroup* trainItem);
	void paintPassengerInfoIcon(PassengerItem* paxItem, const GuiPassengerState& passenger, bool replayActive);
	void paintVCouplingMsg(TrainItemGroup* trainItem, const std::string& message);

	// Draws the track lines, the connections, the nodes, the station overlays and the signals of the
	// model on the scene, which holds no item, and fits the view to them. The caller clears the
	// scene first.
	PreviewOutcome showPreview(const SceneModel& sceneModel);
	PreviewOutcome showPreview(const SceneModel& sceneModel, const PreviewOptions& options);

	// Forgets the fit and the selection of the last preview; the preview itself stays.
	void resetPreviewFit();

	// The line of the preview with this track id in the scene, or null.
	const TrackPreviewLine* trackLine(const std::string& sceneTrackId) const;
	// The factor from the historical distances to the scene coordinates of the preview.
	qreal presentationScale() const { return m_cachedTrackPreview.presentationScale; }

	// Adds the overlay of a station with its picture and name. Only the picture and the name move
	// by the offset; the overlay stays at the position of the station.
	void paintStationOverlay(QPointF coord, const StationVisual& visual, const std::string& sname,
		qreal scale = 1.0, QPointF decorationOffset = QPointF(), int platformCount = 0);

	// The train groups, the badge of each train by train index, and the centre of each badge in the
	// previous update by train index.
	QList<TrainItemGroup*> allTrains;
	QMap<int, TrainBadgeItem*> m_trainBadges;
	QMap<int, QPointF> m_prevTrainPositions;

	// The movement of each train between its last two places by train index. The parent of an animation is the scene.
	QMap<int, QVariantAnimation*> m_trainAnimations;

	// The signals, the signals of each section by the id of the section ahead of them, and the
	// decorations of the signals.
	QList<SignalItem*> allSignals;
	std::unordered_map<std::string, QList<SignalItem*>> m_signalsByAheadId;
	QList<QGraphicsItem*> m_signalDecorations;

	// The track lines, the track lines of each signalling section by section id, the track line of
	// each arc by track and start position, and the track lines that show a state other than free.
	QList<TrackLineItem*> allArcs;
	std::map<std::string, std::vector<TrackLineItem*>> m_tracksBySectionId;
	std::map<std::pair<int, double>, TrackLineItem*> m_tracksByOccupiedArc;
	std::set<TrackLineItem*> m_activeTrackItems;

	// The platforms, the station overlays, everything that the station layer shows, the station
	// names, and the picture of each overlay.
	QList<PlatformItem*> allPlatforms;
	QList<StationOverlayItem*> m_stationOverlays;
	QList<QGraphicsItem*> m_stationDecorations;
	QList<QGraphicsItem*> m_stationLabels;
	QMap<StationOverlayItem*, QGraphicsPixmapItem*> m_stationPictures;

	// The vehicle-coupling message of each train by train index.
	QMap<int, QGraphicsItemGroup*> m_vcMessageItems;

	// The signal whose details are shown, the selected train and the group that shows its
	// passenger information, and the selected passenger icon and the group that shows its information.
	SignalItem* m_inspectedSignal = nullptr;
	TrainItemGroup* trainPaxItem = nullptr;
	QGraphicsItemGroup* trainPaxInfoItem = nullptr;
	PassengerItem* paxIconItem = nullptr;
	QGraphicsItemGroup* paxIconInfoItem = nullptr;

	// The effect on the item selected last. The item deletes it with itself.
	QPointer<HighlightEffect> effect;

	// The preview that the scene shows, the bounds and the point that the view fits to, whether the
	// preview marks a selected track, and whether it has signals.
	TrackPreviewResult m_cachedTrackPreview;
	QRectF m_previewFitBounds;
	QPointF m_previewZoomFocus;
	bool m_previewHasSelectedTrack = false;
	bool m_previewHasSignals = false;

	// scene item sizes
	int global_scale = 10;
	int node_size = (int)2.8 * global_scale;
	int station_node_size = (int)2.5 * node_size;
	int station_name_graphID = -8; // to be on the opposite side of tracks
	int line_width = 2;
	int track_separation = 15 * global_scale;
	int station_size = 30 * global_scale;
	int geo_scale = 80000 * global_scale; // scale used to convert latitude,longitude to screen coordinates

	// The icon of a passenger on a platform, as large as a tenth of a station.
	QPixmap pax_pixmap_scaled;

	// Which layers are shown.
	bool m_stationLayerVisible = true;
	bool m_stationNamesVisible = true;
	bool m_trainLayerVisible = true;
	bool m_trainSpeedLabelsVisible = true;
	bool m_signalLayerVisible = true;
	bool m_passengerLayerVisible = true;
	// A platform bar shows with the stations and with the passengers.
	bool platformBarsVisible() const { return m_stationLayerVisible && m_passengerLayerVisible; }

private:
	QPointer<QGraphicsScene> m_scene;
	QPointer<NetworkView> m_view;

	// Where the items of a run take the infrastructure from, and the stations of each region ordered by
	// position along the track.
	std::unique_ptr<RuntimeGeometrySource> m_source;
	std::vector<std::vector<int>> m_regionStations;

	// The train groups by train index, and the platforms by station id and platform id. paintTrain and
	// paintStationPlatform fill them and clear() empties them.
	std::unordered_map<int, TrainItemGroup*> m_trainsByIndex;
	std::map<std::pair<std::string, std::string>, std::vector<PlatformItem*>> m_platformsById;
	// The icons and the counters of the platforms show an earlier step, because steps were applied while the
	// passenger layer was hidden.
	bool m_platformsStale = false;

	// The scene of the run, which gives the colour of each service for the trains that apply() adds. It is the
	// model that showRuntime drew, and the caller keeps it until the next clear().
	const SceneModel* m_runModel = nullptr;
	// Who takes the parts of a frame that are not on the canvas. It is never null.
	FrameHost* m_host;

	QPointF Coord2ScreenPoint(double x, double y, double factor) const;
	void calculateStationCoordAndShift();
	bool hasTrackGeometry(int track) const;
	void neighbourStations(double X, int tracklineID, int* stationIdx) const;
	void egtrainPoint2Screen(double X, int track, double separation, double& graphX, double& graphY) const;
	void egtrainPoint2Screen(Node* node, int track, double separation) const;
	void egtrainPoint2Screen(Connections* connection, int track1, int track2, double separation) const;
	void paintNode(QPointF coord, int size, int track, Node* node);
	void paintStationPlatform(QPointF coord, int size, Node* node);
	void paintArc(QPointF start, QPointF end, int penWidth, int track, Arc* arc);
	void arcDrawing(QPointF start, QPointF end, int penWidth, int track, Arc* arc);
	void paintConnection(QPointF start, QPointF end, int penWidth, Connections* connection);
	void paintSignal(double X, int size, int penWidth, int track, int separation, int sectionIndex);
	void buildSignalIndex();
	void buildTrackIndexes();
	bool passengerGui() const;
	void updateSignalling(const GuiSimulationSnapshot& snapshot);
	void updateSignalAspect(const GuiSignalState& state, std::set<const SignalItem*>& updated);
	void updatePlatforms(const GuiSimulationSnapshot& snapshot);
	void updatePlatform(PlatformItem* platform, const GuiPlatformState& state, std::vector<PassengerItem*>& restack);
	void stackPassengerIcons(const std::vector<PassengerItem*>& restack);
	void showTrackStates(const GuiSimulationSnapshot& snapshot, int t, bool replay);
	void checkVCouplingMsg(TrainItemGroup* trainItem, const GuiTrainState& train, const GuiSimulationSnapshot& snapshot, int t);
};

// The details of a passenger in the snapshot; only the id is known when the snapshot is null or does not list the passenger.
GuiPassengerState passengerStateIn(const GuiSimulationSnapshot* snapshot, const std::string& id);

// The pen of a connection, shared by the preview before Run and a run.
QPen connectionPen(int width);

// The outline of a signal head, a hairline at every zoom, shared by the preview before Run and a run.
QPen signalHeadPen();

// The station and the platform that a node of the preview belongs to.
struct PreviewStationMembership {
	std::string stationId;
	std::string name;
	std::string platformId;
};
std::map<std::string, PreviewStationMembership> previewStationMemberships(const SceneModel& model);

// A point of a preview line, moved by the display offset of the line: the point at a node, and the
// point at a position along the line.
bool previewPointAtNode(const TrackPreviewLine& line, const std::string& nodeId, qreal offset, QPointF& point);
bool previewPointAtX(const TrackPreviewLine& line, double x, qreal offset, QPointF& point);

// The normal of a preview line at a position along it, which tells the side of a signal head.
bool previewSignalNormal(const TrackPreviewLine& line, double rawX, QPointF& normal);

// How far the artwork and the name of a station sit from its anchor.
QPointF stationDecorationOffset(const std::vector<SceneStationView>& views, const std::string& stationId,
	const std::string& name);

#endif // NETWORKPRESENTER_H
