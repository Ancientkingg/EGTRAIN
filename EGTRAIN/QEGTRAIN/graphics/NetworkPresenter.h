#ifndef NETWORKPRESENTER_H
#define NETWORKPRESENTER_H

#include <QGraphicsScene>
#include <QList>
#include <QMap>
#include <QPen>
#include <QPointF>
#include <QPointer>
#include <QRectF>

#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "graphics/NetworkView.h"
#include "graphics/items/HighlightEffect.h"
#include "scene/TrackPreview.h"

class PassengerItem;
class PlatformItem;
class QGraphicsItem;
class QGraphicsItemGroup;
class QGraphicsPixmapItem;
class SignalItem;
class StationOverlayItem;
class TrackLineItem;
class TrainBadgeItem;
class TrainItemGroup;
struct SceneModel;
struct SceneStationView;
struct StationVisual;

// Draws the track preview on the network canvas and keeps track of the items on it. The scene owns
// every item; the lists, maps and pointers below only observe them and are never used to delete
// one. They are public until all the code that fills them is in this class.
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

	NetworkPresenter(QGraphicsScene* scene, NetworkView* view);

	// Deletes every item of the scene and empties every registry. The signal under inspection is
	// forgotten first, so nothing reads it while the scene deletes its items.
	void clear();

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

	// Which layers are shown.
	bool m_stationLayerVisible = true;
	bool m_stationNamesVisible = true;
	bool m_trainLayerVisible = true;
	bool m_trainSpeedLabelsVisible = true;
	bool m_signalLayerVisible = true;
	bool m_passengerLayerVisible = true;

private:
	QPointer<QGraphicsScene> m_scene;
	QPointer<NetworkView> m_view;
};

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
