#ifndef NETWORKPRESENTER_H
#define NETWORKPRESENTER_H

#include <QElapsedTimer>
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
#include <QTimer>

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "app/GuiSimulationSnapshot.h"
#include "graphics/NetworkView.h"
#include "graphics/items/HighlightEffect.h"
#include "scene/SectionLabels.h"
#include "scene/TrackPreview.h"

class Arc;
class Connections;
class Node;
class PassengerItem;
class PlatformItem;
class QGraphicsItem;
class QGraphicsItemGroup;
class QGraphicsPixmapItem;
class RuntimeGeometrySource;
class Section;
class SignalItem;
class StationOverlayItem;
class TrackLineItem;
class TrainBadgeItem;
class TrainBodyItem;
class TrainItemGroup;
struct GuiPassengerState;
struct GuiPlatformState;
struct GuiSignalState;
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
		/// The train has the place and the shape of the frame; center is trainCenter() of the train: where it is
		/// drawn now, on its way to the place of the frame in a live run, or at that place. Tells whether this moved
		/// the camera to the train.
		virtual bool trainPlaced(int train, const QPointF& center) = 0;
		/// A step of the motion of a live train whose overlay is promoted moved it; center is trainCenter() of the
		/// train as it is drawn now.
		virtual void trainMoved(int train, const QPointF& center) = 0;
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
	/// Stops the motion of the trains.
	~NetworkPresenter();

	/// Sets who takes the parts of a frame that are not on the canvas. The presenter does not own the host.
	/// Null restores a host that selects and follows nothing.
	void setFrameHost(FrameHost* host);

	/// Stops the motion of the trains, deletes every item of the scene and empties every registry. The
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
	///
	/// A live train glides along its route at an even speed from where it is drawn to its new place, drawn wagon by
	/// wagon where it is at the moment, as getTrainPolygon() draws it at an update, with its badge and its callouts. The
	/// glide lasts a little longer than the longest recent update interval, so that the next update, which starts the
	/// next glide from where the train is then drawn, comes before it ends; the train trails its newest place by a
	/// fraction of a glide, and without a new update, as at a pause, it stops there. A train that has just appeared, has
	/// another route or direction, runs backwards, or would move further than the visible view is placed at once, and
	/// so is every train of a replay. The positions are for the time at which the paint that shows them is expected to
	/// end.
	void updateTrainPosition(const GuiSimulationSnapshot& snapshot, int t, Playback playback);

	/// Moves every live train that is in motion to where it is at the moment, and tells the host where a promoted one
	/// is drawn. A train far outside the visible scene is drawn again only at the next update or when it stops. A
	/// timer of the presenter calls it right after each live update and then about once a frame while a train moves,
	/// and every live update moves the trains too; a test calls it with the clock of setClock().
	void stepTrains();
	/// Lets the scene give the view the areas of the items that changed now, rather than from the event loop. When the
	/// camera has moved the view in the same frame, the view can otherwise be painted first and then those areas again,
	/// a second paint on which no train moves. The timer of the steps calls it after each step, the first of which
	/// comes right after each live update. It must not run inside a posted event, such as the one that delivers an
	/// update: a nested sendPostedEvents() lets the outer one go on with the events posted meanwhile, so a busy run
	/// would keep the event loop from timers and input.
	void flushSceneUpdates();
	/// Whether the train moves towards the place of its last update, so that stepTrains() still moves it.
	bool trainMoving(int train) const;
	/// Whether a train moves.
	bool trainsMoving() const;

	/// A clock that returns a time in milliseconds.
	using Clock = std::function<qint64()>;
	/// Replaces the millisecond clock of the motion of the trains and of the time between live updates. An empty
	/// function restores the real clock. What was measured on the old clock is forgotten, and every train goes to the
	/// place of its last update at the next step.
	void setClock(Clock clock);

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

	/// Stops the motion of one train: it stays where it is drawn, and the next update places it at once.
	void stopTrainAnimation(int train);
	/// Stops the motion of every train as stopTrainAnimation() does.
	void stopTrainAnimations();

	/// Whether the labels of the platforms show: the passengers on the platforms show (platformPassengersVisible()) and
	/// the view is zoomed in far enough to read them, to a zoom ratio of at least 3.
	bool paxTextVisible() const;

	/// The line of the preview that the track line with this index in the run is drawn on, or null.
	const TrackPreviewLine* cachedTrackLine(int track) const;
	/// The id in the scene of the track line with this index in the run; empty when the run has none for it.
	std::string sceneTrackId(int track) const;

	/// Adds a train of the run with its badge and train.wagonCount + 1 bodies of equal length, the first at
	/// the head. The train is promoted when it is selected or followed.
	void paintTrain(const GuiTrainState& train, const SceneModel& sceneModel, bool promoted);
	/// The train group of the run with this index, or null.
	TrainItemGroup* trainGroup(int train) const;
	/// The centre of the train as it is drawn, in scene coordinates: on the drawn track, the middle of the part of the
	/// train that is on its route, so half way between the start of the route and the head while the train enters it,
	/// or the centre of its bodies when it has no place on its route. It moves along the track with the train, without
	/// a jump, and never back while the train runs forward. A null point for a train that is not on the canvas.
	QPointF trainCenter(int train) const;

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
	/// The same, with the selection of the infrastructure panel of options.
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
		/// More text that finds the item without being shown: the section ids of a signal.
		QString keys;
	};
	/// The stations, the signals and the trains on the canvas, sorted by label, each label once. The items belong to
	/// the scene and are only valid until it changes.
	QList<FindTarget> findTargets() const;

	/// The line of the preview with this track id in the scene, or null.
	const TrackPreviewLine* trackLine(const std::string& sceneTrackId) const;
	/// The signal of the preview on this track at the start of this section, or null.
	const TrackPreviewSignal* previewSignal(const std::string& trackId, const std::string& sectionId) const;
	/// The factor from the historical presentation distances to the scene coordinates of the preview; 1 when
	/// the preview does not project every visible line from the authored station views.
	qreal presentationScale() const { return m_cachedTrackPreview.presentationScale; }

	/// Adds the overlay of a station at coord with its picture and name. scale is the item scale of the
	/// picture and the name, and decorationOffset, in scene units, moves only the picture and the name;
	/// the overlay stays at the position of the station. StationOverlayItem::applyViewScale() then places the
	/// picture and the name by the station nodes instead. More platforms rank a name higher when names collide.
	void paintStationOverlay(QPointF coord, const StationVisual& visual, const std::string& sname,
		qreal scale = 1.0, QPointF decorationOffset = QPointF(), int platformCount = 0);
	/// The move in scene units of the picture and the name of a station from its anchor on line: the
	/// stationDecorationOffset() times the presentation scale. On a schematic line, whose points come from the
	/// station views and whose display offset holds its level, the move counts from level 0, as in the original, and
	/// a station with a named offset has its offset in units of track_separation, the spacing of the original levels,
	/// so its picture and name stay between the levels that offset was made for.
	QPointF stationArtworkOffset(const TrackPreviewLine& line, const std::vector<SceneStationView>& views,
		const std::string& stationId, const std::string& name) const;

	/// The train groups of the run, in the order in which they were added.
	QList<TrainItemGroup*> allTrains;
	/// The badge of each train by train index.
	QMap<int, TrainBadgeItem*> m_trainBadges;

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
	/// The spacing of the tracks at the stations in scene units, which chooses the look of the station marks:
	/// the median, over the stations with two or more station nodes, of the smallest distance between two of
	/// their nodes. measureStationTrackSpacing() sets it and clear() resets it to 0.
	qreal m_stationTrackSpacing = 0.0;
	/// Measures m_stationTrackSpacing from the node centres of the station marks; track_separation times the
	/// presentation scale when no station has two nodes.
	void measureStationTrackSpacing();
	/// The station marks are in the near look, in which the platform bars show. setStationNearLook() sets it and clear()
	/// resets it to false.
	bool m_stationNearLook = false;
	/// Sets m_stationNearLook and shows or hides the platform bars and the passengers on them (applyPlatformVisibility()).
	void setStationNearLook(bool nearLook);
	/// Shows or hides the platform bars by platformBarsVisible(), the icons of the passengers on them by
	/// platformPassengersVisible() and their counters by paxTextVisible(). Call it after a change of a layer.
	void applyPlatformVisibility();

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
	/// A platform bar shows with the stations in the near look.
	bool platformBarsVisible() const { return m_stationLayerVisible && m_stationNearLook; }
	/// The passengers on a platform show where its bar shows, with the passenger layer.
	bool platformPassengersVisible() const { return platformBarsVisible() && m_passengerLayerVisible; }

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

	// The drawn track along a route between two positions: the points in the scene, every corner of the drawn lines
	// between them included, with their positions along the route in metres and the length of the path up to them in
	// scene units.
	struct RoutePath {
		std::vector<QPointF> points;
		std::vector<double> positions;
		std::vector<double> lengths;
	};
	// A train that is drawn on its route: the state of its last update, and its glide from fromHead, the position of its
	// head along the route in metres at the time fromMs, to the head of that state at toMs. It is drawn with its head at
	// drawnHead, from drawn, the state of the last update with the positions of the wagons there.
	struct TrainMotion {
		GuiTrainState newest;
		GuiTrainState drawn;
		double fromHead = 0.0;
		qint64 fromMs = 0;
		qint64 toMs = 0;
		double drawnHead = 0.0;
	};
	// The motion of each train that is drawn on its route, by train index.
	std::unordered_map<int, TrainMotion> m_trainMotions;
	// Calls stepTrains() while a train moves.
	QTimer m_motionTimer;
	// The last intervals between two live updates in milliseconds, the oldest first.
	std::vector<qint64> m_updateIntervals;
	// The time of the last live update in milliseconds; negative before the first.
	qint64 m_lastUpdateMs = -1;
	// When the trains were last drawn, whether an update drew them since the last paint, and the time from the last
	// drawing to the end of the paint that showed it, in the last paints after an update and in the others, in
	// milliseconds.
	qint64 m_drawnAtMs = -1;
	bool m_updatedSincePaint = false;
	std::vector<qint64> m_updateLeadsMs;
	std::vector<qint64> m_stepLeadsMs;
	// The last time for which the trains were drawn.
	qint64 m_presentedMs = -1;
	Clock m_clock;
	QElapsedTimer m_elapsed;
	// The index of each section of the source by its id. buildTrackIndexes() fills it for the run.
	std::unordered_map<std::string, int> m_sectionIndexById;

	qint64 nowMs() const;
	qint64 presentationMs(bool update);
	void notePaint();
	void noteLiveUpdate();
	qint64 glideDurationMs() const;
	qreal longestGlide() const;
	double middleOnRoute(int routeIndex, double head, double length) const;
	double glideHead(const TrainMotion& motion, qint64 at) const;
	void startGlide(TrainMotion& motion, const GuiTrainState& state, qint64 now, double drawnHead, qint64 glideMs);
	bool drawTrain(TrainItemGroup& trainItem, TrainMotion& motion, double head);
	void moveTrainCallouts(const TrainItemGroup& trainItem);
	bool switchSectionEnds(const Section& section, const Section*& first, const Section*& second, double& firstX,
		double& secondX) const;
	bool sectionPointAt(const Section& section, double x, QPointF& point) const;
	void addSectionCorners(const Section& section, double fromX, double toX, std::vector<double>& xs) const;
	bool routePoint(int routeIndex, bool reversed, double position, QPointF& point) const;
	bool routePath(int routeIndex, bool reversed, double from, double to, RoutePath& path) const;

	QPointF Coord2ScreenPoint(double x, double y, double factor) const;
	void calculateStationCoordAndShift();
	bool hasTrackGeometry(int track) const;
	void neighbourStations(double X, int tracklineID, int* stationIdx) const;
	void egtrainPoint2Screen(double X, int track, double separation, double& graphX, double& graphY) const;
	void egtrainPoint2Screen(Node* node, int track, double separation) const;
	void egtrainPoint2Screen(Connections* connection, int track1, int track2, double separation) const;
	void paintNode(QPointF coord, int size, int track, Node* node);
	PlatformItem* addPlatformBar(QPointF before, QPointF node, QPointF after, qreal size);
	void paintStationPlatform(QPointF before, QPointF coord, QPointF after, int size, Node* node);
	void paintArc(QPointF start, QPointF end, int penWidth, int track, Arc* arc);
	void arcDrawing(QPointF start, QPointF end, int penWidth, int track, Arc* arc);
	void paintConnection(QPointF start, QPointF end, int penWidth, Connections* connection);
	void paintSignal(double X, int size, int penWidth, int track, int separation, int sectionIndex,
		const SceneSectionLabels& labels);
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
	/// The id of the platform; empty for a node that only a station without platforms takes by its position.
	/// Such a station keeps the platform id that an earlier station gave the node.
	std::string platformId;
};
/// The station and the platform of each node of the model by node id. A node belongs to the platforms that list
/// it; a station without platforms takes the nodes at its position, within 1e-8 km. A later station in the model
/// replaces the station id and name of an earlier one.
std::map<std::string, PreviewStationMembership> previewStationMemberships(const SceneModel& model);

/// The point of a preview line at a node, with offset scene units (the display offset of the line) added to its y.
/// Returns false when the line has no such node.
bool previewPointAtNode(const TrackPreviewLine& line, const std::string& nodeId, qreal offset, QPointF& point);
/// The point of a preview line at the position x in kilometres, interpolated between its points, with offset scene
/// units added to its y. Outside the line it is the nearest point. Returns false when there is no finite point.
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
