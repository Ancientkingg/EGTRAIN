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
#include <QString>

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

/// Draws the track preview and the items of a run on the network canvas and keeps track of the items on
/// it. The scene owns every item; the lists, maps and pointers below only observe them and are never
/// used to delete one. They are public until all the code that fills them is in this class.
///
/// clear() deletes every item of the scene and then forgets all the pointers to them. A pointer
/// that stays after the scene is cleared would dangle, so the registries belong with the code that
/// clears the scene.
///
/// Positions and sizes are in scene units. The preview puts a track line at the normalized coordinates
/// of the track preview. A run without the preview puts it between its stations, which are projected
/// from their latitude and longitude or else placed at their position in kilometres times 1000. A track
/// line in a region of fewer than two stations is at its position in kilometres times 1000. A scene unit
/// therefore has no fixed length in metres. A train is given by its index in the run.
class NetworkPresenter {
public:
	/// What the preview needs to know besides the model.
	struct PreviewOptions {
		/// The facet of the row that the infrastructure panel selects: "tracks", "nodes", "arcs", "blocks",
		/// "connections", "stations", "platforms" or "routes". Empty without one.
		std::string selectedFacet;
		/// The id of that row; for a platform the station id and the platform id with a newline between
		/// them. Empty without one.
		std::string selectedId;
		/// The infrastructure panel is shown, so the arc or connection that it selects is marked.
		bool markSelection = false;
		/// A station node gets a platform bar.
		bool platformBars = false;
	};

	/// What showPreview drew.
	struct PreviewOutcome {
		/// The model has track lines to preview.
		bool available = false;
		/// The number of track lines that have a drawable shape: at least two points, all finite.
		int trackLines = 0;
		/// The view was fitted to the drawn track lines.
		bool fitted = false;
	};

	/// What a snapshot is a step of.
	enum class Playback {
		Live,  ///< the run that is being simulated
		Replay ///< the replay of a finished run
	};

	/// The parts of a frame that are not on the canvas: the follow camera, the legend and the signal inspector.
	/// apply() calls them at the points of a frame where they belong. Without a host no train is selected or followed.
	class FrameHost {
	public:
		virtual ~FrameHost() = default;

		/// The overlay of the train is drawn in full, because the train is selected or followed.
		virtual bool isTrainOverlayPromoted(int train) const = 0;
		/// A train of the snapshot is about to be updated. The frame can be one in which the train has already left.
		virtual void trainWillUpdate(int train) = 0;
		/// The train has the shape and the place of the frame; center is the centre of its bodies in scene
		/// coordinates. Tells whether this moved the camera to the train.
		virtual bool trainPlaced(int train, const QPointF& center) = 0;
		/// The train has left the network and is hidden.
		virtual void trainHidden(int train) = 0;
		/// The train was added to the canvas; firstLiveTrain is true for the first train of a live run.
		/// Tells whether this moved the camera to the train.
		virtual bool trainAdded(int train, bool firstLiveTrain) = 0;
		/// The signals show the aspects of the frame.
		virtual void signalsApplied() = 0;
		/// Every train has been updated. anyAdded tells whether a train was added, cameraMoved whether the
		/// camera moved to the followed train.
		virtual void trainsApplied(bool anyAdded, bool cameraMoved) = 0;
	};

	/// Draws on scene and fits view, which may be null. The presenter owns neither and holds both through
	/// QPointer.
	NetworkPresenter(QGraphicsScene* scene, NetworkView* view);
	/// Stops the movement of the trains.
	~NetworkPresenter();

	/// Sets who takes the parts of a frame that are not on the canvas. The presenter does not own the host.
	/// Null restores a host that selects and follows nothing.
	void setFrameHost(FrameHost* host);

	/// Stops the movement of the trains, deletes every item of the scene and empties every registry. The
	/// signal under inspection is forgotten first, so nothing reads it while the scene deletes its items.
	void clear();

	/// Sets where the items of a run take the infrastructure from, and takes ownership of it. Without a
	/// source nothing of a run is drawn.
	void setGeometrySource(std::unique_ptr<RuntimeGeometrySource> source);

	/// Lays out the stations of the run and draws its connections, track lines, nodes, station
	/// overlays, platforms and signals on the scene, which holds no item. It then indexes the signals
	/// and the track lines. The caller clears the scene first. A drawn item points to its arc, node
	/// or connection in the source, which therefore keeps them until the next clear(). The caller also
	/// keeps sceneModel until then: the trains that apply() adds take their service colour from it.
	void showRuntime(const SceneModel& sceneModel);

	/// Shows one step of a run: the signal aspects and the trains, and with the passenger GUI the platforms and the
	/// passenger information. The signals, the track lines and the platforms of the run are on the canvas by showRuntime.
	void apply(const GuiSimulationSnapshot& snapshot, Playback playback);

	/// Adds, moves and hides the trains of the snapshot at step t, in seconds from the start of the run, and sets the
	/// state of the sections and of the occupied arcs on the track lines. apply() does this; it is public so that the
	/// trains can be updated alone.
	void updateTrainPosition(const GuiSimulationSnapshot& snapshot, int t, Playback playback);

	/// Draws the passenger information of the selected train again, or hides it while the passenger layer is hidden.
	void updateTrainPaxInfo();
	/// Draws the passenger information of the selected passenger icon again, or hides it while the passenger layer is
	/// hidden. The snapshot gives the details of the passenger and can be null. When the passenger layer is shown after
	/// steps were applied while it was hidden, the snapshot also brings the platforms up to date.
	void updatePaxIconInfo(const GuiSimulationSnapshot* snapshot, Playback playback);
	/// Deletes the passenger information of the selected train. The train stays selected.
	void removeTrainPaxInfoIcon();
	/// Deletes the passenger information of the selected passenger icon. The icon stays selected.
	void removePaxInfoIcon();

	/// Sets every track line back to the free state.
	void clearOperationalTrackStates();

	/// Stops the movement of one train between its last two places.
	void stopTrainAnimation(int train);
	/// Stops the movement of every train between its last two places.
	void stopTrainAnimations();

	/// Whether the labels of the platforms show: the passenger layer is shown and the view is zoomed in far enough to
	/// read them, to a zoom ratio of at least 3.
	bool paxTextVisible() const;

	/// The line of the preview that the track line with this index in the run is drawn on, or null.
	const TrackPreviewLine* cachedTrackLine(int track) const;

	/// Adds a train of the run with its badge and train.wagonCount + 1 bodies of equal length, the first at
	/// the head. The train is promoted when it is selected or followed.
	void paintTrain(const GuiTrainState& train, const SceneModel& sceneModel, bool promoted);

	/// Gives each body of the list the shape of its wagon at the present position of the train, and
	/// tells whether one of them has a shape.
	bool getTrainPolygonItemList(QList<TrainBodyItem*>* trainPolygonItemList, const GuiTrainState& train);

	/// The shape, in scene coordinates, of one body of a train, counted from 0 at the head. The polygon
	/// stays empty when the train has no route or the wagon has no position.
	void getTrainPolygon(QPolygonF* trainPolygon, int wagon, const GuiTrainState& train);

	/// Adds the square of a station node of a track line, centred on coord. size is its side in scene
	/// units, times the presentation scale when the track line has a line in the preview. The item points
	/// to node, which belongs to the run. The node is darker at the end of a line when its platform
	/// is known.
	void paintStationNode(QPointF coord, int size, int track, Node* node, bool lineEnd);

	/// Shows the passengers on board above a train.
	void paintTrainPassengerInfo(TrainItemGroup* trainItem);
	/// Shows the details of a passenger above its icon; a replay adds that the journey details are unavailable.
	void paintPassengerInfoIcon(PassengerItem* paxItem, const GuiPassengerState& passenger, bool replayActive);
	/// Shows the message of a virtual coupling above a train.
	void paintVCouplingMsg(TrainItemGroup* trainItem, const std::string& message);

	/// Draws the track lines, the connections, the nodes, the station overlays and the signals of the
	/// model on the scene, which holds no item, and fits the view to them. The caller clears the
	/// scene first.
	PreviewOutcome showPreview(const SceneModel& sceneModel);
	/// The same, with the selection of the infrastructure panel and the platform bars of options.
	PreviewOutcome showPreview(const SceneModel& sceneModel, const PreviewOptions& options);

	/// Forgets the fit and the selection of the last preview; the preview itself stays.
	void resetPreviewFit();

	/// An item that Find offers.
	struct FindTarget {
		/// Its name with its kind and enough context to tell it apart.
		QString label;
		/// The item that a click on it selects.
		QGraphicsItem* item;
		/// The point of the scene to centre on.
		QPointF center;
	};
	/// The stations, the signals and the trains on the canvas, sorted by label, each label once. The items belong to
	/// the scene and are only valid until it changes.
	QList<FindTarget> findTargets() const;

	/// The line of the preview with this track id in the scene, or null.
	const TrackPreviewLine* trackLine(const std::string& sceneTrackId) const;
	/// The factor from the historical presentation distances to the scene coordinates of the preview; 1 when
	/// the preview does not project every visible line from the authored station views.
	qreal presentationScale() const { return m_cachedTrackPreview.presentationScale; }

	/// Adds the overlay of a station at coord with its picture and name. scale is the item scale of the
	/// picture and the name, and decorationOffset, in scene units, moves only the picture and the name;
	/// the overlay stays at the position of the station. More platforms rank a name higher when names collide.
	void paintStationOverlay(QPointF coord, const StationVisual& visual, const std::string& sname,
		qreal scale = 1.0, QPointF decorationOffset = QPointF(), int platformCount = 0);

	/// The train groups of the run, in the order in which they were added.
	QList<TrainItemGroup*> allTrains;
	/// The badge of each train by train index.
	QMap<int, TrainBadgeItem*> m_trainBadges;
	/// The centre of each train in the scene after its previous update, by train index; its badge sits there.
	QMap<int, QPointF> m_prevTrainPositions;

	/// The movement of each train between its last two places by train index. The parent of an animation is the scene.
	QMap<int, QVariantAnimation*> m_trainAnimations;

	/// The signal heads of a run.
	QList<SignalItem*> allSignals;
	/// The signal heads of a run by the id of the section ahead of them.
	std::unordered_map<std::string, QList<SignalItem*>> m_signalsByAheadId;
	/// The heads, posts and bases of the signals of the preview and of a run.
	QList<QGraphicsItem*> m_signalDecorations;

	/// The track lines of a run.
	QList<TrackLineItem*> allArcs;
	/// The track lines of each signalling section by section id.
	std::map<std::string, std::vector<TrackLineItem*>> m_tracksBySectionId;
	/// The track line of each arc by track line index and the position of the start node of the arc in kilometres.
	std::map<std::pair<int, double>, TrackLineItem*> m_tracksByOccupiedArc;
	/// The track lines whose state is not free.
	std::set<TrackLineItem*> m_activeTrackItems;

	/// The platforms of a run.
	QList<PlatformItem*> allPlatforms;
	/// The station overlays of the preview and of a run.
	QList<StationOverlayItem*> m_stationOverlays;
	/// Everything that the station layer shows.
	QList<QGraphicsItem*> m_stationDecorations;
	/// The station names.
	QList<QGraphicsItem*> m_stationLabels;
	/// The picture of each overlay.
	QMap<StationOverlayItem*, QGraphicsPixmapItem*> m_stationPictures;

	/// The virtual coupling message of each train by train index.
	QMap<int, QGraphicsItemGroup*> m_vcMessageItems;

	/// The signal whose details are shown, or null.
	SignalItem* m_inspectedSignal = nullptr;
	/// The selected train, or null.
	TrainItemGroup* trainPaxItem = nullptr;
	/// The group that shows the passenger information of the selected train, or null.
	QGraphicsItemGroup* trainPaxInfoItem = nullptr;
	/// The selected passenger icon, or null.
	PassengerItem* paxIconItem = nullptr;
	/// The group that shows the information of the selected passenger icon, or null.
	QGraphicsItemGroup* paxIconInfoItem = nullptr;

	/// The effect on the item selected last. The item deletes it with itself.
	QPointer<HighlightEffect> effect;

	/// The preview that the scene shows.
	TrackPreviewResult m_cachedTrackPreview;
	/// The scene rectangle, margins included, that the view fits the preview to; empty without one.
	QRectF m_previewFitBounds;
	/// The point of the drawn track lines nearest to the centre of the preview, in scene coordinates.
	QPointF m_previewZoomFocus;
	/// The preview marks a selected track.
	bool m_previewHasSelectedTrack = false;
	/// The preview has signals.
	bool m_previewHasSignals = false;

	// scene item sizes
	/// The factor of the sizes below.
	int global_scale = 10;
	/// The diameter of a node and of a signal head in scene units. The cast truncates 2.8 to 2, so it is 20.
	int node_size = (int)2.8 * global_scale;
	/// The side of the square of a station node in scene units. The cast truncates 2.5 to 2, so it is 40.
	int station_node_size = (int)2.5 * node_size;
	/// The multiple of track_separation by which a run without the preview moves the picture and the name of a
	/// station along the shift of its region; negative puts them on the side opposite the shifted track lines.
	int station_name_graphID = -8;
	/// The width in pixels of the cosmetic pens of connections, signal posts and callout lines, and the smallest
	/// width of a track line.
	int line_width = 2;
	/// The distance in scene units between two display levels of track lines. Signal posts, train bodies and
	/// callout lines are sized from it.
	int track_separation = 15 * global_scale;
	/// The height of a station picture in scene units. A station name uses a font of a fifth of it in pixels,
	/// the labels of platforms and the passenger information a fifteenth.
	int station_size = 30 * global_scale;
	/// The factor of the Mercator projection that places a station with a latitude and longitude in a run
	/// without the preview. The projection also uses the width and height of the view in pixels.
	int geo_scale = 80000 * global_scale;

	/// The icon of a passenger on a platform, a tenth of station_size in pixels on each side. It is null when the
	/// application resource is missing.
	QPixmap pax_pixmap_scaled;

	/// The station layer is shown.
	bool m_stationLayerVisible = true;
	/// The station names are shown.
	bool m_stationNamesVisible = true;
	/// The train layer is shown.
	bool m_trainLayerVisible = true;
	/// The badges of the trains hold their speed.
	bool m_trainSpeedLabelsVisible = true;
	/// The signal layer is shown.
	bool m_signalLayerVisible = true;
	/// The passenger layer is shown.
	bool m_passengerLayerVisible = true;
	/// A platform bar shows with the stations and with the passengers.
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

/// The details of a passenger in the snapshot; only the id is known when the snapshot is null or does not list the passenger.
GuiPassengerState passengerStateIn(const GuiSimulationSnapshot* snapshot, const std::string& id);

/// The white cosmetic pen of a connection, width pixels wide, shared by the preview before Run and a run.
QPen connectionPen(int width);

/// The outline of a signal head, a hairline at every zoom, shared by the preview before Run and a run.
QPen signalHeadPen();

/// The station and the platform that a node of the preview belongs to.
struct PreviewStationMembership {
	/// The id of the station.
	std::string stationId;
	/// The name of the station, or its id when it has no name.
	std::string name;
	/// The id of the platform; empty for a node that a station without platforms takes by its position.
	std::string platformId;
};
/// The station and the platform of each node of the model by node id. A node belongs to the platforms that list
/// it; a station without platforms takes the nodes at its position, within 1e-8 km. A later station in the model
/// replaces an earlier one.
std::map<std::string, PreviewStationMembership> previewStationMemberships(const SceneModel& model);

/// The point of a preview line at a node, moved down by offset scene units (the display offset of the line).
/// Returns false when the line has no such node.
bool previewPointAtNode(const TrackPreviewLine& line, const std::string& nodeId, qreal offset, QPointF& point);
/// The point of a preview line at the position x in kilometres, interpolated between its points and moved down by
/// offset scene units. Outside the line it is the nearest point. Returns false when there is no finite point.
bool previewPointAtX(const TrackPreviewLine& line, double x, qreal offset, QPointF& point);

/// The unit normal of a preview line at the position rawX in kilometres, which tells the side of a signal head.
/// Returns false when no segment of the line covers rawX.
bool previewSignalNormal(const TrackPreviewLine& line, double rawX, QPointF& normal);

/// How far the artwork and the name of a station sit from its anchor, in historical presentation distances: the
/// regional shift of annotationStationShift() plus the named offset of stationNamedYOffset(). The callers multiply
/// it by the presentation scale.
QPointF stationDecorationOffset(const std::vector<SceneStationView>& views, const std::string& stationId,
	const std::string& name);

#endif // NETWORKPRESENTER_H
