#include "graphics/NetworkPresenter.h"

#include "graphics/SignalItemRoles.h"

#ifdef signals
#define EGTRAIN_RESTORE_SIGNALS_KEYWORD
#undef signals
#endif
#include "scene/SceneModel.h"
#include "scene/SectionInventory.h"
#ifdef EGTRAIN_RESTORE_SIGNALS_KEYWORD
#define signals Q_SIGNALS
#undef EGTRAIN_RESTORE_SIGNALS_KEYWORD
#endif

#include "app/GuiSimulationSnapshot.h"
#include "graphics/AnnotationPlacement.h"
#include "graphics/NetworkScene.h"
#include "graphics/NetworkView.h"
#include "graphics/RuntimeGeometrySource.h"
#include "graphics/SignalGeometry.h"
#include "graphics/VisualPolish.h"
#include "graphics/items/ConnectionItem.h"
#include "graphics/items/NodeItem.h"
#include "graphics/items/PassengerItem.h"
#include "graphics/items/PlatformItem.h"
#include "graphics/items/SignalItem.h"
#include "graphics/items/StationNodeItem.h"
#include "graphics/items/StationOverlayItem.h"
#include "graphics/items/TrackLineItem.h"
#include "graphics/items/TrainBadgeItem.h"
#include "graphics/items/TrainBodyItem.h"
#include "graphics/items/TrainItemGroup.h"
#include "scene/SceneReferences.h"
#include "util/PlaybackProfiler.h"
#include "util/SpeedFormat.h"

// The simulation headers still give warnings (#453, #454, #482), so this strict target includes
// Signalling.h with them switched off, as diagrams/SimulationHeaders.h does.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wall"
#pragma clang diagnostic ignored "-Wextra"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wdeprecated-copy"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "simulation/Signalling.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <QCoreApplication>
#include <QEvent>
#include <QFont>
#include <QGraphicsItemGroup>
#include <QGraphicsLineItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QPainterPath>
#include <QtMath>

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace {

// The value that the station layout of the runtime canvas has always used for pi.
constexpr double kPi = 3.14159265;

// The zoom of the view from which the labels of the platforms are legible.
constexpr qreal kDenseDetailZoom = 3.0;

// A station node stands for a platform: its platform id is neither empty nor "None".
bool hasPlatformId(const std::string& platformId) {
	return !platformId.empty() && platformId != "None";
}

// A station node gets a platform bar for a platform of a station that is not virtual: its name does not hold "virtual",
// as for the station overlays.
bool hasPlatformBar(const std::string& stationName, const std::string& platformId) {
	return hasPlatformId(platformId) && stationName.find("virtual") == std::string::npos;
}

// The glide of a train between two live updates lasts this many times the longest of the last update intervals but
// one, kept between these bounds. Up to 120 ms between updates the next update then comes well before the glide ends,
// and the glide that it starts has nearly the speed of the one before, also when the updates come unevenly or bring
// uneven steps of simulated time. Above that the longest bound applies, so the glide spans fewer updates: at the
// slowest speed, 2x, the updates come every 500 ms and the next one comes 100 ms before the glide ends. The longest
// bound is a little above that interval.
constexpr double kGlideFactor = 5.0;
constexpr qint64 kMinGlideMs = 16;
constexpr qint64 kMaxGlideMs = 600;
// The glide before the update interval is known.
constexpr qint64 kDefaultGlideMs = 120;
// A longer gap between two live updates, as at a pause, is not an update interval.
constexpr qint64 kMaxUpdateIntervalMs = 1000;
// The number of update intervals that the glide is measured over.
constexpr std::size_t kMeasuredIntervals = 8;
// The time between two steps of the motion of the trains, about one frame. The timer runs on at this interval while a
// train moves, whenever the updates come.
constexpr int kMotionStepMs = 16;

// The X in kilometres of the point at the position q along a route, in metres, as the simulation finds the end of a
// wagon (trainXPositionOf()): on the first section of the route that holds it, or -1 where it is on none.
double routeXAt(const Route& route, bool reversed, double q) {
	const int sections = std::min(route.N_Block_Sections, static_cast<int>(route.sequence_of_block_sections.size()));
	for (int i = 0; i < sections; ++i) {
		const Section& section = route.sequence_of_block_sections[static_cast<std::size_t>(i)];
		if (q >= section.start_node.X * 1000 && q < section.end_node.X * 1000) {
			const double portion = q - section.start_node.X * 1000;
			return (reversed ? section.GeoXBegNode - portion : section.GeoXBegNode + portion) / 1000;
		}
	}
	return -1.0;
}

// The middle of the front of a train as it is drawn, in scene coordinates, where its callouts start. Returns false while
// its head has no shape.
bool trainFront(const TrainItemGroup& train, QPointF& front) {
	if (!train.trainPolygonItemList || train.trainPolygonItemList->isEmpty())
		return false;
	const TrainBodyItem* head = train.trainPolygonItemList->at(0);
	if (!head || head->polygon().isEmpty())
		return false;
	front = head->mapToScene((head->polygon().first() + head->polygon().last()) / 2);
	return true;
}

// The positions along a route at which its first section starts and its last section ends, in metres. Returns false
// for a route without sections.
bool routeExtent(const Route* route, double& start, double& end) {
	const int sections = route ? std::min(route->N_Block_Sections, static_cast<int>(route->sequence_of_block_sections.size())) : 0;
	if (sections <= 0)
		return false;
	start = route->sequence_of_block_sections.front().start_node.X * 1000.0;
	end = route->sequence_of_block_sections[static_cast<std::size_t>(sections - 1)].end_node.X * 1000.0;
	return start <= end;
}

// The class of the mark of a station: the connections at its node and its platforms.
StationVisual stationMarkVisual(const SceneModel& sceneModel, const std::string& nodeId, int platformCount) {
	int connections = 0;
	if (!nodeId.empty())
		for (const auto& connection : sceneModel.connections)
			connections += (connection.fromNodeId == nodeId ? 1 : 0) + (connection.toNodeId == nodeId ? 1 : 0);
	return classifyStation(platformCount > 0, connections);
}

// True when the view of the track draws it as a double switch: without its first and last arc and without ordinary node dots.
bool drawnAsDoubleSwitch(const SceneModel& sceneModel, const std::string& trackId) {
	const auto view = std::find_if(sceneModel.trackViews.begin(), sceneModel.trackViews.end(),
		[&trackId](const SceneTrackView& candidate) { return candidate.trackId == trackId; });
	return view != sceneModel.trackViews.end() && view->doubleSwitch;
}

// The host of a presenter that has none: nothing is selected or followed.
class NoFrameHost final : public NetworkPresenter::FrameHost {
public:
	bool isTrainOverlayPromoted(int) const override { return false; }
	void trainWillUpdate(int) override {}
	bool trainPlaced(int, const QPointF&) override { return false; }
	void trainMoved(int, const QPointF&) override {}
	void trainHidden(int) override {}
	bool trainAdded(int, bool) override { return false; }
	void signalsApplied() override {}
	void trainsApplied(bool, bool) override {}
};

NoFrameHost& noFrameHost() {
	static NoFrameHost host;
	return host;
}

// The point at the position X of a track line, shifted to its display level: a linear interpolation of the
// shifted stations on both sides of X in the region of the track line.
void interpolateBetweenStations(Stations& first, Stations& second, int region, double X, double level, double separation,
	double& graphX, double& graphY) {
	// shifted stations
	const double x1 = first.graphX + first.shiftX[region] * level * separation;
	const double x2 = second.graphX + second.shiftX[region] * level * separation;
	const double y1 = first.graphY + first.shiftY[region] * level * separation;
	const double y2 = second.graphY + second.shiftY[region] * level * separation;

	// linear interpolation of geodetic coordinates using known coordinates of closest stations
	graphX = x1 + (((X - first.regionX[region]) / (second.regionX[region] - first.regionX[region])) * (x2 - x1));
	graphY = y1 + (((X - first.regionX[region]) / (second.regionX[region] - first.regionX[region])) * (y2 - y1));
}

void tagPreviewItem(QGraphicsItem* item, const QString& kind, const std::string& id) {
	item->setData(PreviewGraphics::Kind, kind);
	item->setData(PreviewGraphics::Id, QString::fromStdString(id));
}

// Turns a platform bar, which is centred on its own origin, to the direction of the track at node, and puts its centre
// 1.15 times size from node on the upper side of the track, or on the left of a vertical track. The direction is the mean
// of the directions from before to node and from node to after; at the end of a line after is node itself. A segment of
// zero length has no direction, and a bar without one is level. The long side reads left to right, or top to bottom.
void placePlatformBar(QGraphicsItem* bar, const QPointF& before, const QPointF& node, const QPointF& after, qreal size) {
	QPointF direction;
	for (const QLineF& segment : {QLineF(before, node), QLineF(node, after)})
		if (segment.length() > 0)
			direction += (segment.p2() - segment.p1()) / segment.length();
	const qreal length = std::hypot(direction.x(), direction.y());
	direction = length > 0 ? direction / length : QPointF(1, 0);
	if (direction.x() < 0 || (direction.x() == 0 && direction.y() < 0))
		direction = -direction;
	const QPointF up = direction.x() == 0 ? QPointF(-1, 0) : QPointF(direction.y(), -direction.x());
	bar->setPos(node + up * 1.15 * size);
	bar->setRotation(qRadiansToDegrees(std::atan2(direction.y(), direction.x())));
}
} // namespace

NetworkPresenter::NetworkPresenter(QGraphicsScene* scene, NetworkView* view)
	: m_scene(scene), m_view(view), m_host(&noFrameHost()) {
	m_elapsed.start();
	m_motionTimer.setTimerType(Qt::PreciseTimer);
	m_motionTimer.setInterval(kMotionStepMs);
	QObject::connect(&m_motionTimer, &QTimer::timeout, &m_motionTimer, [this]() {
		stepTrains();
#ifndef Q_OS_WIN
		// On Windows a timer this short arrives in a posted event, inside which the flush must not run.
		flushSceneUpdates();
#endif
	});
	// The icon is a resource of the application, which a caller such as a test can lack.
	const QPixmap passenger(QStringLiteral(":/icons/pax_icon.png"));
	if (!passenger.isNull())
		pax_pixmap_scaled = passenger.scaled(QSize(station_size / 10, station_size / 10), Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

NetworkPresenter::~NetworkPresenter() {
	stopTrainAnimations();
}

void NetworkPresenter::setFrameHost(FrameHost* host) {
	m_host = host ? host : &noFrameHost();
}

void NetworkPresenter::clear() {
	m_inspectedSignal = nullptr;
	// The motion of a train points to its items, which the scene is about to delete.
	stopTrainAnimations();
	m_runModel = nullptr;
	if (m_scene)
		m_scene->clear();

	// The scene has deleted the items, so every pointer to one is dropped.
	m_trainBadges.clear();
	m_updateIntervals.clear();
	m_lastUpdateMs = -1;
	allTrains.clear();
	allSignals.clear();
	m_signalDecorations.clear();
	m_stationOverlays.clear();
	m_vcMessageItems.clear();
	m_stationDecorations.clear();
	m_stationLabels.clear();
	m_stationPictures.clear();
	m_stationTrackSpacing = 0.0;
	m_stationNearLook = false;
	m_signalsByAheadId.clear();
	allArcs.clear();
	allPlatforms.clear();
	m_tracksBySectionId.clear();
	m_sectionIndexById.clear();
	m_tracksByOccupiedArc.clear();
	m_activeTrackItems.clear();
	m_regionStations.clear();
	m_trainsByIndex.clear();
	m_platformsById.clear();
	m_platformsStale = false;
	trainPaxInfoItem = nullptr;
	trainPaxItem = nullptr;
	paxIconInfoItem = nullptr;
	paxIconItem = nullptr;
	effect = nullptr;
}

void NetworkPresenter::setGeometrySource(std::unique_ptr<RuntimeGeometrySource> source) {
	m_source = std::move(source);
}

void NetworkPresenter::resetPreviewFit() {
	m_previewFitBounds = QRectF();
	m_previewHasSelectedTrack = false;
	m_previewHasSignals = false;
}

QList<NetworkPresenter::FindTarget> NetworkPresenter::findTargets() const {
	QList<FindTarget> targets;
	if (!m_scene)
		return targets;
	// A station node of a run has the node of its stopping board; one of the preview has none.
	for (QGraphicsItem* item : m_scene->items()) {
		auto* station = qgraphicsitem_cast<StationNodeItem*>(item);
		if (!station || !station->node || station->node->stationName.empty())
			continue;
		const std::string track = sceneTrackId(station->track);
		targets.append({QStringLiteral("Station: %1 (track %2)")
							.arg(QString::fromStdString(station->node->stationName),
								track.empty() ? QString::number(station->track) : QString::fromStdString(track)),
			station, station->sceneBoundingRect().center(), QString()});
	}
	// The overlay of a station of the preview is drawn at a fixed size on the screen, so its anchor is its place.
	for (StationOverlayItem* overlay : m_stationOverlays) {
		if (!m_stationLayerVisible || !overlay || overlay->data(PreviewGraphics::Kind).toString() != QLatin1String("station"))
			continue;
		const QString id = overlay->data(PreviewGraphics::Id).toString();
		const QString name = overlay->stationName();
		targets.append({name.isEmpty() || name == id ? QStringLiteral("Station: %1").arg(id) : QStringLiteral("Station: %1 (%2)").arg(name, id),
			overlay, overlay->stableAnchor(), QString()});
	}
	// A signal of a run knows its sections; the scene gives the ids of the signals that protect them.
	std::map<std::string, QStringList> authoredIds;
	if (m_runModel)
		for (const SceneSignal& source : sceneSignals(*m_runModel))
			authoredIds[source.protectedSection].append(QString::fromStdString(source.id));
	for (QGraphicsItem* item : m_signalDecorations) {
		auto* signal = qgraphicsitem_cast<SignalItem*>(item);
		if (!m_signalLayerVisible || !signal)
			continue;
		QString identity = signal->inspectionIdentity();
		if (!identity.isEmpty())
			identity[0] = identity[0].toLower();
		const auto ids = signal->sectionAheadId.empty() ? authoredIds.end() : authoredIds.find(signal->sectionAheadId);
		const QString name = ids == authoredIds.end() ? QStringLiteral("Signal")
													  : QStringLiteral("Signal %1").arg(ids->second.join(QStringLiteral(", ")));
		// The labels and the ids of the sections find it too.
		targets.append({QStringLiteral("%1: %2, facing %3").arg(name, identity, signal->reversedDirection ? QStringLiteral("left") : QStringLiteral("right")),
			signal, signal->sceneBoundingRect().center(),
			QString::fromStdString(signal->sectionAheadId + " " + signal->sectionBehindId + " " + signal->sectionAheadLabel + " "
				+ signal->sectionBehindLabel)
				.simplified()});
	}
	for (TrainItemGroup* train : allTrains) {
		if (!train || !train->isVisible() || !train->trainPolygonItemList || train->trainPolygonItemList->isEmpty())
			continue;
		targets.append({QStringLiteral("Train: %1").arg(QString::fromStdString(followTrainName(train->trainDescription, train->index))),
			train->trainPolygonItemList->first(), train->sceneBoundingRect().center(), QString()});
	}
	std::stable_sort(targets.begin(), targets.end(), [](const FindTarget& a, const FindTarget& b) { return a.label < b.label; });
	targets.erase(std::unique(targets.begin(), targets.end(), [](const FindTarget& a, const FindTarget& b) { return a.label == b.label; }),
		targets.end());
	return targets;
}

const TrackPreviewLine* NetworkPresenter::trackLine(const std::string& sceneTrackId) const {
	const auto line = std::find_if(m_cachedTrackPreview.lines.begin(),
		m_cachedTrackPreview.lines.end(), [&sceneTrackId](const TrackPreviewLine& candidate) {
			return candidate.id == sceneTrackId;
		});
	return line == m_cachedTrackPreview.lines.end() ? nullptr : &*line;
}

const TrackPreviewSignal* NetworkPresenter::previewSignal(const std::string& trackId, const std::string& sectionId) const {
	const auto& heads = m_cachedTrackPreview.previewSignals;
	const auto found = std::find_if(heads.begin(), heads.end(), [&](const TrackPreviewSignal& head) {
		return head.trackId == trackId && head.sectionId == sectionId;
	});
	return found == heads.end() ? nullptr : &*found;
}

std::string NetworkPresenter::sceneTrackId(int track) const {
	if (!m_source || track < 0 || track >= m_source->trackCount())
		return {};
	return m_source->track(track).sceneTrackId;
}

const TrackPreviewLine* NetworkPresenter::cachedTrackLine(int track) const {
	if (!m_source || track < 0 || track >= m_source->trackCount() || m_source->track(track).sceneTrackId.empty())
		return nullptr;
	return trackLine(m_source->track(track).sceneTrackId);
}

NetworkPresenter::PreviewOutcome NetworkPresenter::showPreview(const SceneModel& sceneModel) {
	return showPreview(sceneModel, PreviewOptions());
}

NetworkPresenter::PreviewOutcome NetworkPresenter::showPreview(const SceneModel& sceneModel, const PreviewOptions& options) {
	QGraphicsScene* const scene = m_scene;
	resetPreviewFit();
	const std::string& selectedFacet = options.selectedFacet;
	const std::string& selectedId = options.selectedId;
	std::set<std::string> selectedTrackIds;
	const auto selectTrack = [&selectedTrackIds](const std::string& id) {
		if (!id.empty())
			selectedTrackIds.insert(id);
	};
	const auto selectNode = [&selectTrack, &sceneModel](const std::string& id) {
		if (id.empty())
			return;
		for (const auto& node : sceneModel.nodes)
			if (node.id == id)
				selectTrack(node.trackId);
	};
	if (selectedFacet == "tracks") {
		selectTrack(selectedId);
	} else if (selectedFacet == "nodes") {
		selectNode(selectedId);
	} else if (selectedFacet == "arcs") {
		for (const auto& arc : sceneModel.arcs)
			if (arc.id == selectedId)
				selectTrack(arc.trackId);
	} else if (selectedFacet == "blocks") {
		for (const auto& block : sceneModel.blocks)
			if (block.id == selectedId)
				selectTrack(block.trackId);
	} else if (selectedFacet == "connections") {
		for (const auto& connection : sceneModel.connections) {
			if (connection.id != selectedId)
				continue;
			for (const auto& node : sceneModel.nodes) {
				if (node.id == connection.fromNodeId || node.id == connection.toNodeId)
					selectTrack(node.trackId);
			}
		}
	} else if (selectedFacet == "stations") {
		for (const auto& station : sceneModel.stations) {
			if (station.id != selectedId)
				continue;
			for (const auto& platform : station.platforms)
				for (const auto& nodeId : platform.nodeIds)
					selectNode(nodeId);
		}
	} else if (selectedFacet == "platforms") {
		const std::size_t separator = selectedId.find('\n');
		if (separator != std::string::npos) {
			const std::string stationId = selectedId.substr(0, separator);
			const std::string platformId = selectedId.substr(separator + 1);
			for (const auto& station : sceneModel.stations) {
				if (station.id != stationId)
					continue;
				for (const auto& platform : station.platforms)
					if (platform.id == platformId)
						for (const auto& nodeId : platform.nodeIds)
							selectNode(nodeId);
			}
		}
	} else if (selectedFacet == "routes") {
		for (const auto& route : sceneModel.routes) {
			if (route.id != selectedId)
				continue;
			for (const auto& token : route.blocks) {
				for (const auto& component : sceneBlockReferenceComponents(token)) {
					for (const auto& block : sceneModel.blocks)
						if (block.id == component)
							selectTrack(block.trackId);
				}
			}
		}
	}
	m_previewHasSelectedTrack = !selectedTrackIds.empty();
	m_cachedTrackPreview = normalizeTrackPreview(loadTrackPreview(sceneModel));
	const TrackPreviewResult& preview = m_cachedTrackPreview;
	if (preview.lines.empty()) {
		if (scene)
			scene->setSceneRect(QRectF());
		return {};
	}

	QRectF previewBounds;
	bool hasPreviewBounds = false;
	const auto includePreviewPoint = [&previewBounds, &hasPreviewBounds](const QPointF& point) {
		if (!hasPreviewBounds) {
			previewBounds = QRectF(point, point);
			hasPreviewBounds = true;
			return;
		}
		previewBounds.setLeft(qMin(previewBounds.left(), point.x()));
		previewBounds.setRight(qMax(previewBounds.right(), point.x()));
		previewBounds.setTop(qMin(previewBounds.top(), point.y()));
		previewBounds.setBottom(qMax(previewBounds.bottom(), point.y()));
	};
	std::map<std::string, std::pair<const TrackPreviewLine*, qreal>> tracks;
	std::vector<const TrackPreviewLine*> drawnLines;
	for (std::size_t index = 0; index < preview.lines.size(); ++index) {
		const auto& line = preview.lines[index];
		if (line.points.size() < 2 || std::any_of(line.points.begin(), line.points.end(), [](const auto& point) {
				return !std::isfinite(point.x) || !std::isfinite(point.y);
			}))
			continue;
		const qreal offset = static_cast<qreal>(line.displayOffset);
		tracks[line.id] = {&line, offset};
		drawnLines.push_back(&line);
		for (const auto& point : line.points)
			includePreviewPoint(QPointF(point.x, point.y + offset));
	}

	for (const auto& connection : preview.connections) {
		const auto first = tracks.find(connection.firstTrackId);
		const auto second = tracks.find(connection.secondTrackId);
		if (first == tracks.end() || second == tracks.end())
			continue;

		QPointF start;
		QPointF end;
		if (!previewPointAtNode(*first->second.first, connection.firstNodeId, first->second.second, start)
			|| !previewPointAtNode(*second->second.first, connection.secondNodeId, second->second.second, end))
			continue;

		auto* item = new ConnectionItem(QLineF(start, end));
		item->setPen(connectionPen(line_width));
		tagPreviewItem(item, "connection", connection.id);
		scene->addItem(item);
		includePreviewPoint(start);
		includePreviewPoint(end);
	}

	// A run adds the connections first and the track lines after them, so a track is drawn over a connection.
	for (const TrackPreviewLine* drawn : drawnLines) {
		const auto& line = *drawn;
		const qreal offset = static_cast<qreal>(line.displayOffset);
		const bool doubleSwitch = drawnAsDoubleSwitch(sceneModel, line.id);
		for (std::size_t point = 1; point < line.points.size(); ++point) {
			if (doubleSwitch && (point == 1 || point + 1 == line.points.size()))
				continue;
			const auto arc = std::find_if(sceneModel.arcs.begin(), sceneModel.arcs.end(), [&](const SceneArc& candidate) {
				return candidate.trackId == line.id
					&& ((candidate.fromNodeId == line.points[point - 1].nodeId
							&& candidate.toNodeId == line.points[point].nodeId)
						|| (candidate.toNodeId == line.points[point - 1].nodeId
							&& candidate.fromNodeId == line.points[point].nodeId));
			});
			const TrackVisual visual = classifyTrackSpeed(arc == sceneModel.arcs.end() ? 0.0 : arc->speedLimitMs);
			QPen pen(selectedTrackIds.count(line.id) > 0 ? kSelectionCueColor : visual.color);
			pen.setWidth(selectedTrackIds.count(line.id) > 0 ? 4 : visual.width);
			pen.setCosmetic(true);
			auto* item = new TrackLineItem(QLineF(line.points[point - 1].x, line.points[point - 1].y + offset,
				line.points[point].x, line.points[point].y + offset));
			item->setPen(pen);
			tagPreviewItem(item, arc == sceneModel.arcs.end() ? "track" : "arc",
				arc == sceneModel.arcs.end() ? line.id : arc->id);
			scene->addItem(item);
		}
	}

	const auto memberships = previewStationMemberships(sceneModel);
	std::map<std::string, int> connectionDegrees;
	for (const auto& connection : sceneModel.connections) {
		++connectionDegrees[connection.fromNodeId];
		++connectionDegrees[connection.toNodeId];
	}
	std::map<std::string, QGraphicsItem*> nodeItems;
	std::map<std::string, const PlatformItem*> platformBars;
	for (const auto& line : preview.lines) {
		if (!tracks.count(line.id))
			continue;
		const bool doubleSwitch = drawnAsDoubleSwitch(sceneModel, line.id);
		for (std::size_t index = 0; index < line.points.size(); ++index) {
			const auto& point = line.points[index];
			const auto membership = memberships.find(point.nodeId);
			// Current runnable-chain presentation: first point is ordinary;
			// four-node ordinary starts paint zero size, but the terminal dot remains. A double switch paints none.
			const bool station = index > 0 && membership != memberships.end();
			const bool noDot = doubleSwitch || (line.points.size() == 4 && index + 1 < line.points.size());
			const qreal size = (station ? station_node_size : (noDot ? 0 : node_size)) * presentationScale();
			QRectF rect(0, 0, size, size);
			rect.moveCenter(QPointF(point.x, point.y + line.displayOffset));
			QAbstractGraphicsShapeItem* item;
			if (station) {
				auto* square = new StationNodeItem(rect);
				const auto& value = membership->second;
				// A platform darkens the square only at the end of the line; an interior one is the ordinary grey.
				const bool lineEnd = index + 1 == line.points.size();
				const StationVisual visual = classifyStation(lineEnd && hasPlatformId(value.platformId),
					connectionDegrees[point.nodeId]);
				square->setPen(QPen(visual.outline, 0));
				square->setBrush(visual.fill);
				item = square;
				tagPreviewItem(item, "station-node", point.nodeId);
				item->setData(PreviewGraphics::StationId, QString::fromStdString(value.stationId));
				item->setData(PreviewGraphics::PlatformId, QString::fromStdString(value.platformId));
				if (hasPlatformBar(value.name, value.platformId)) {
					// The bar is placed by points of the scene, as in the run.
					const auto pointAt = [&line](std::size_t at) {
						return QPointF(line.points[at].x, line.points[at].y + line.displayOffset);
					};
					platformBars[point.nodeId] =
						addPlatformBar(pointAt(index - 1), pointAt(index), lineEnd ? pointAt(index) : pointAt(index + 1), size);
				}
			} else {
				item = new NodeItem(rect);
				item->setPen(QPen(Qt::lightGray, 0));
				item->setBrush(Qt::lightGray);
				tagPreviewItem(item, "node", point.nodeId);
			}
			QPen pen = item->pen();
			pen.setCosmetic(true);
			item->setPen(pen);
			scene->addItem(item);
			nodeItems[point.nodeId] = item;
		}
	}

	// Selected-only editor emphasis is separate from structural boundary marks.
	if (options.markSelection) {
		if (selectedFacet == "arcs") {
			for (const auto& arc : sceneModel.arcs) {
				if (arc.id != selectedId)
					continue;
				const auto track = tracks.find(arc.trackId);
				if (track == tracks.end())
					break;
				QPointF start;
				QPointF end;
				if (!previewPointAtNode(*track->second.first, arc.fromNodeId, track->second.second, start)
					|| !previewPointAtNode(*track->second.first, arc.toNodeId, track->second.second, end))
					break;
				QPainterPath path(start);
				path.lineTo(end);
				QPen highlight(QColor(255, 110, 90));
				highlight.setWidthF(6.0);
				highlight.setCosmetic(true);
				auto* item = scene->addPath(path, highlight);
				item->setAcceptedMouseButtons(Qt::NoButton);
				break;
			}
		}

		if (selectedFacet == "connections") {
			for (const auto& connection : sceneModel.connections) {
				if (connection.id != selectedId)
					continue;
				const auto fromNode = std::find_if(sceneModel.nodes.begin(), sceneModel.nodes.end(),
					[&connection](const SceneNode& node) { return node.id == connection.fromNodeId; });
				const auto toNode = std::find_if(sceneModel.nodes.begin(), sceneModel.nodes.end(),
					[&connection](const SceneNode& node) { return node.id == connection.toNodeId; });
				if (fromNode == sceneModel.nodes.end() || toNode == sceneModel.nodes.end())
					break;
				const auto first = tracks.find(fromNode->trackId);
				const auto second = tracks.find(toNode->trackId);
				if (first == tracks.end() || second == tracks.end())
					break;
				QPointF start;
				QPointF end;
				if (!previewPointAtNode(*first->second.first, fromNode->id, first->second.second, start)
					|| !previewPointAtNode(*second->second.first, toNode->id, second->second.second, end))
					break;
				QPainterPath path(start);
				path.lineTo(end);
				QPen highlight(QColor(255, 110, 90));
				highlight.setWidthF(6.0);
				highlight.setCosmetic(true);
				auto* item = scene->addPath(path, highlight);
				item->setAcceptedMouseButtons(Qt::NoButton);
				break;
			}
		}
	}

	// Station anchors: one fixed-size marker and name per canonical station.
	for (const auto& station : preview.stations) {
		if (station.name.find("virtual") != std::string::npos)
			continue;
		for (const auto& line : preview.lines) {
			if (!tracks.count(line.id))
				continue;
			const auto& points = line.points;
			QPointF anchor;
			if (!station.nodeId.empty()) {
				if (!previewPointAtNode(line, station.nodeId, line.displayOffset, anchor))
					continue;
			} else {
				const double minX = std::min(points.front().rawX, points.back().rawX);
				const double maxX = std::max(points.front().rawX, points.back().rawX);
				if (station.x < minX || station.x > maxX)
					continue;
				if (!previewPointAtX(line, station.x, line.displayOffset, anchor))
					break;
			}
			const auto sceneStation = std::find_if(sceneModel.stations.cbegin(), sceneModel.stations.cend(),
				[&station](const SceneStation& candidate) { return candidate.id == station.id; });
			const int platformCount = sceneStation == sceneModel.stations.cend() ? 0 : static_cast<int>(sceneStation->platforms.size());
			paintStationOverlay(anchor, stationMarkVisual(sceneModel, station.nodeId, platformCount), station.name,
				presentationScale(), stationArtworkOffset(line, sceneModel.stationViews, station.id, station.name),
				platformCount);
			auto* overlay = m_stationOverlays.back();
			tagPreviewItem(overlay, "station", station.id);
			// The mark hangs from the square of the node of the station, which it stacks over. The name stays an item of its
			// own, which stacks over everything.
			if (auto* picture = m_stationPictures.value(overlay, nullptr)) {
				tagPreviewItem(picture, "station", station.id);
				const auto host = nodeItems.find(station.nodeId);
				if (host != nodeItems.end()) {
					const QPointF position = picture->scenePos();
					picture->setParentItem(host->second);
					picture->setPos(host->second->mapFromScene(position));
					host->second->setZValue(StationMarkItem::HostZValue);
				}
				// The mark and its area are placed by the station squares of the platforms, and the area covers their bars. A
				// station without platforms has the squares of the nodes at its position, the nearest to its anchor first, as in
				// a run.
				QList<QPointF> centres;
				QList<QPolygonF> bars;
				const auto addSquare = [&](const std::string& nodeId) {
					const auto node = nodeItems.find(nodeId);
					if (node == nodeItems.end() || !qgraphicsitem_cast<StationNodeItem*>(node->second))
						return;
					const QPointF centre = node->second->sceneBoundingRect().center();
					if (!centres.contains(centre))
						centres.append(centre);
					if (const auto bar = platformBars.find(nodeId); bar != platformBars.end())
						bars.append(bar->second->mapToScene(bar->second->rect()));
				};
				if (sceneStation != sceneModel.stations.cend()) {
					for (const auto& platform : sceneStation->platforms)
						for (const auto& nodeId : platform.nodeIds)
							addSquare(nodeId);
					if (sceneStation->platforms.empty()) {
						for (const auto& [nodeId, membership] : memberships)
							if (membership.stationId == station.id)
								addSquare(nodeId);
						std::stable_sort(centres.begin(), centres.end(), [&anchor](const QPointF& left, const QPointF& right) {
							return QLineF(left, anchor).length() < QLineF(right, anchor).length();
						});
					}
				}
				overlay->setNodeCentres(centres);
				overlay->setPlatformBars(bars);
			}
			break;
		}
	}
	measureStationTrackSpacing();

	// Preview signals are derived from canonical block boundaries.  They stay
	// in the existing decoration list so the layer toggle and teardown own them.
	for (const auto& signal : preview.previewSignals) {
		const auto track = tracks.find(signal.trackId);
		if (track == tracks.end())
			continue;
		QPointF center;
		if ((!signal.nodeId.empty()
				&& !previewPointAtNode(*track->second.first, signal.nodeId,
					track->second.second, center))
			|| (signal.nodeId.empty()
				&& !previewPointAtX(*track->second.first, signal.rawX,
					track->second.second, center)))
			continue;
		QPointF normal;
		if (!previewSignalNormal(*track->second.first, signal.rawX, normal))
			continue;

		QStringList canonicalIds;
		for (const SceneSignal& source : sceneSignals(sceneModel))
			if (source.protectedSection == signal.sectionId)
				canonicalIds.append(QString::fromStdString(source.id));
		QPointF before, after;
		if (!previewPointAtX(*track->second.first, signal.rawX - 0.008,
				track->second.second, before)
			|| !previewPointAtX(*track->second.first, signal.rawX + 0.008,
				track->second.second, after))
			continue;
		const SignalGeometry geometry = signalGeometry(center, before, after, normal, track_separation * presentationScale());
		for (const bool reversed : {true, false}) {
			const QLineF base = reversed ? geometry.reversedBase : geometry.forwardBase;
			const QLineF post = reversed ? geometry.reversedPost : geometry.forwardPost;
			const QPointF head = reversed ? geometry.reversedHead : geometry.forwardHead;
			QPen white(Qt::white, line_width);
			white.setCosmetic(true);
			for (const QLineF& stroke : {base, post}) {
				auto* line = scene->addLine(stroke, white);
				line->setData(kSignalDecorationRole, true);
				line->setData(kSignalTrackRole, -1);
				line->setData(kSignalBaseVisibleRole, true);
				line->setAcceptedMouseButtons(Qt::NoButton);
				m_signalDecorations.push_back(line);
				line->setVisible(m_signalLayerVisible);
			}
			auto* glyph = new SignalItem(QRectF(-node_size * presentationScale() / 2.0, -node_size * presentationScale() / 2.0,
				node_size * presentationScale(), node_size * presentationScale()));
			tagPreviewItem(glyph, "signal", signal.sectionId);
			glyph->setData(PreviewGraphics::TrackId, QString::fromStdString(signal.trackId));
			glyph->setData(PreviewGraphics::Reversed, reversed);
			glyph->X = signal.rawX;
			glyph->sectionAheadId = signal.sectionId;
			glyph->sectionAheadLabel = signal.sectionLabel;
			glyph->setInspectionIdentity(QStringLiteral("Track %1 at %2 km, section %3%4")
					.arg(QString::fromStdString(signal.trackId))
					.arg(signal.rawX, 0, 'g', 10)
					.arg(QString::fromStdString(signal.sectionLabel))
					.arg(canonicalIds.isEmpty() ? QString()
												: QStringLiteral(" (%1 %2)").arg(canonicalIds.size() == 1 ? "signal" : "signals", canonicalIds.join(", "))));
			glyph->setPen(signalHeadPen());
			glyph->setZValue(3.0);
			glyph->setPos(head);
			glyph->setAspectCode(180);
			glyph->setReversedDirection(reversed);
			glyph->setData(kSignalDecorationRole, true);
			glyph->setData(kSignalTrackRole, -1);
			glyph->setData(kSignalBaseVisibleRole, true);
			glyph->setData(kSignalAnchorRole, head);
			glyph->setAcceptedMouseButtons(Qt::NoButton);
			scene->addItem(glyph);
			m_signalDecorations.push_back(glyph);
			glyph->setVisible(m_signalLayerVisible);
		}
	}
	m_previewHasSignals = !preview.previewSignals.empty()
		&& std::any_of(m_signalDecorations.cbegin(), m_signalDecorations.cend(),
			[](QGraphicsItem* item) { return qgraphicsitem_cast<SignalItem*>(item) != nullptr; });

	// Fit the drawn geometry directly; the generic fitView minimum-scale clamp
	// is tuned for case-study coordinates and crops these kilometre-scale paths.
	if (hasPreviewBounds) {
		const QPointF target = previewBounds.center();
		qreal bestDistance = std::numeric_limits<qreal>::max();
		for (const auto& track : tracks) {
			const auto& line = *track.second.first;
			const qreal offset = track.second.second;
			for (std::size_t index = 1; index < line.points.size(); ++index) {
				const QPointF first(line.points[index - 1].x, line.points[index - 1].y + offset);
				const QPointF second(line.points[index].x, line.points[index].y + offset);
				const QPointF segment = second - first;
				const qreal lengthSquared = QPointF::dotProduct(segment, segment);
				const qreal ratio = lengthSquared > 0.0
					? qBound<qreal>(0.0, QPointF::dotProduct(target - first, segment) / lengthSquared, 1.0)
					: 0.0;
				const QPointF candidate = first + ratio * segment;
				const QPointF delta = candidate - target;
				const qreal distance = QPointF::dotProduct(delta, delta);
				if (distance < bestDistance) {
					bestDistance = distance;
					m_previewZoomFocus = candidate;
				}
			}
		}
		if (previewBounds.height() < previewBounds.width() * 0.2) {
			const qreal grow = previewBounds.width() * 0.2 - previewBounds.height();
			previewBounds.adjust(0, -grow / 2.0, 0, grow / 2.0);
		}
		const qreal marginX = previewBounds.width() * 0.05;
		const qreal marginY = previewBounds.height() * 0.1;
		previewBounds.adjust(-marginX, -marginY, marginX, marginY);
		m_previewFitBounds = previewBounds;
		scene->setSceneRect(previewBounds);
		if (m_view)
			m_view->fitToBounds(previewBounds);
	}
	// The scene queues every added item for polishing until the event loop runs, and deleting a queued item
	// leaves an entry that each later deletion searches past. Previews drawn back to back would get slower with
	// every call, so run the queued work now.
	QCoreApplication::sendPostedEvents(scene, QEvent::MetaCall);
	return {true, static_cast<int>(tracks.size()), hasPreviewBounds};
}

// Keep native font/pixmap metrics and scale their scene-space presentation. The
// overlay then enlarges them on screen to their minimum readable size.
void NetworkPresenter::paintStationOverlay(QPointF coord, const StationVisual& visual, const std::string& sname,
	qreal scale, QPointF decorationOffset, int platformCount) {
	QGraphicsScene* const scene = m_scene;
	if (!scene)
		return;
	auto* overlay = new StationOverlayItem(QString::fromStdString(sname), coord, visual);
	overlay->setSceneDecoration(true);
	scene->addItem(overlay);
	m_stationOverlays.push_back(overlay);
	m_stationDecorations.push_back(overlay);
	overlay->setNameVisible(m_stationNamesVisible);
	overlay->setVisible(m_stationLayerVisible);

	// Keep picking/source identity at coord; only artwork and text move.
	const QPointF artworkCoord = coord + decorationOffset;
	auto* picture = new StationMarkItem(visual.cls, station_size);
	scene->addItem(picture);
	picture->setScale(scale);
	picture->setPos(artworkCoord.x() - station_size * scale / 2.0, artworkCoord.y() - station_size * scale);
	picture->setAcceptedMouseButtons(Qt::NoButton);
	m_stationDecorations.push_back(picture);
	m_stationPictures.insert(overlay, picture);
	picture->setVisible(m_stationLayerVisible);
	QFont font;
	font.setPixelSize(station_size / 5);
	auto* label = new StationNameItem(StationOverlayItem::displayName(sname), font);
	scene->addItem(label);
	label->setScale(scale);
	label->setPos(artworkCoord - QPointF(label->boundingRect().width() * scale / 2.0, label->boundingRect().height() * scale / 2.0));
	m_stationLabels.push_back(label);
	label->setVisible(m_stationLayerVisible && m_stationNamesVisible);
	overlay->attachArtwork(picture, label, platformCount);
}

void NetworkPresenter::setStationNearLook(bool nearLook) {
	m_stationNearLook = nearLook;
	applyPlatformVisibility();
}

void NetworkPresenter::applyPlatformVisibility() {
	for (QGraphicsItem* item : std::as_const(m_stationDecorations))
		if (qgraphicsitem_cast<PlatformItem*>(item))
			item->setVisible(platformBarsVisible());
	const bool passengers = platformPassengersVisible();
	const bool counters = paxTextVisible();
	for (PlatformItem* platform : std::as_const(allPlatforms)) {
		if (!platform)
			continue;
		if (platform->textIcon)
			platform->textIcon->setVisible(counters);
		for (PassengerItem* icon : std::as_const(platform->passengerIcons))
			icon->setVisible(passengers);
	}
	// The details of a selected passenger go with its icon.
	if (paxIconInfoItem)
		paxIconInfoItem->setVisible(passengers);
}

void NetworkPresenter::measureStationTrackSpacing() {
	std::vector<qreal> smallest;
	for (const StationOverlayItem* overlay : std::as_const(m_stationOverlays)) {
		const auto* mark = overlay ? dynamic_cast<const StationMarkItem*>(overlay->pictureItem()) : nullptr;
		if (!mark)
			continue;
		const QList<QPointF> centres = mark->nodeCentres();
		qreal nearest = std::numeric_limits<qreal>::max();
		for (qsizetype first = 0; first < centres.size(); ++first)
			for (qsizetype second = first + 1; second < centres.size(); ++second) {
				// Squares at one place are one place, not two tracks.
				const qreal distance = QLineF(centres.at(first), centres.at(second)).length();
				if (distance > 1e-6)
					nearest = std::min(nearest, distance);
			}
		if (nearest < std::numeric_limits<qreal>::max())
			smallest.push_back(nearest);
	}
	if (smallest.empty()) {
		m_stationTrackSpacing = track_separation * presentationScale();
		return;
	}
	const auto median = smallest.begin() + static_cast<std::ptrdiff_t>(smallest.size() / 2);
	std::nth_element(smallest.begin(), median, smallest.end());
	m_stationTrackSpacing = *median;
}

QPointF NetworkPresenter::stationArtworkOffset(const TrackPreviewLine& line,
	const std::vector<SceneStationView>& views, const std::string& stationId, const std::string& name) const {
	const QPointF offset = stationDecorationOffset(views, stationId, name);
	if (!line.authoredStationProjection || !(line.levelSpacing > 0.0))
		return offset * presentationScale();
	const qreal scale = stationNamedYOffset(name) != 0.0 ? line.levelSpacing / track_separation : presentationScale();
	return offset * scale - QPointF(0.0, line.displayOffset);
}

void NetworkPresenter::showRuntime(const SceneModel& sceneModel) {
	if (!m_source)
		return;
	m_runModel = &sceneModel;

	// Reuse drawing levels once track spans stop overlapping when the scene has
	// no authored display layout.
	struct TrackSpan {
		int track;
		double begin;
		double end;
	};
	std::vector<TrackSpan> trackSpans;
	for (int track = 0; track < m_source->trackCount(); ++track) {
		BlockSet& trackSet = m_source->track(track);
		if (trackSet.hasGraphLayout)
			continue;
		if (trackSet.len <= 0) {
			trackSet.graphID = 0;
			continue;
		}
		double begin = std::numeric_limits<double>::max();
		double end = std::numeric_limits<double>::lowest();
		for (int arc = 0; arc < trackSet.len; ++arc) {
			begin = std::min({begin, trackSet.member[arc].startNode.X, trackSet.member[arc].endNode.X});
			end = std::max({end, trackSet.member[arc].startNode.X, trackSet.member[arc].endNode.X});
		}
		trackSpans.push_back({track, begin, end});
	}
	std::sort(trackSpans.begin(), trackSpans.end(), [](const TrackSpan& left, const TrackSpan& right) {
		return left.begin < right.begin || (left.begin == right.begin && left.track < right.track);
	});
	std::vector<double> levelEnds;
	for (const TrackSpan& span : trackSpans) {
		auto available = std::find_if(levelEnds.begin(), levelEnds.end(), [&span](double end) {
			return end < span.begin;
		});
		if (available == levelEnds.end()) {
			m_source->track(span.track).graphID = static_cast<int>(levelEnds.size());
			levelEnds.push_back(span.end);
		} else {
			m_source->track(span.track).graphID = static_cast<int>(std::distance(levelEnds.begin(), available));
			*available = span.end;
		}
	}
	// Use a single fallback region for stations without authored view metadata.
	for (int stationIndex = 0; stationIndex < m_source->stationCount(); ++stationIndex) {
		Stations& legacyStation = m_source->station(stationIndex);
		if (legacyStation.regions.empty()) {
			legacyStation.regions.push_back(0);
			legacyStation.regionX[0] = legacyStation.X;
		}
	}

	// calculate screen coordinates and shifts for each station (graphical levels)
	calculateStationCoordAndShift();
	const bool hasSharedPreview = !m_cachedTrackPreview.lines.empty();
	const auto sharedStationPoint = [this, &sceneModel, hasSharedPreview](int stationIndex, QPointF& point,
										std::string& nodeId, const TrackPreviewLine*& stationLine) {
		if (!hasSharedPreview || stationIndex < 0
			|| stationIndex >= static_cast<int>(sceneModel.stations.size()))
			return false;
		const auto& modelStation = sceneModel.stations[static_cast<std::size_t>(stationIndex)];
		const auto station = std::find_if(m_cachedTrackPreview.stations.begin(),
			m_cachedTrackPreview.stations.end(), [&modelStation](const TrackPreviewStation& candidate) {
				return (!modelStation.id.empty() && candidate.id == modelStation.id)
					|| (modelStation.id.empty() && candidate.name == modelStation.name);
			});
		if (station == m_cachedTrackPreview.stations.end())
			return false;
		nodeId = station->nodeId;
		for (const auto& line : m_cachedTrackPreview.lines) {
			stationLine = &line;
			if (!station->nodeId.empty()) {
				if (previewPointAtNode(line, station->nodeId,
						static_cast<qreal>(line.displayOffset), point))
					return true;
				continue;
			}
			if (line.points.empty())
				continue;
			const double minX = std::min(line.points.front().rawX, line.points.back().rawX);
			const double maxX = std::max(line.points.front().rawX, line.points.back().rawX);
			if (station->x < minX || station->x > maxX)
				continue;
			if (previewPointAtX(line, station->x,
					static_cast<qreal>(line.displayOffset), point))
				return true;
		}
		stationLine = nullptr;
		return false;
	};

	// draw station icons and print names
	QPointF pt;
	for (int i = 0; i < m_source->stationCount(); i++) {
		Stations& legacyStation = m_source->station(i);
		// ignore virtual stations (name contains "virtual")
		if (legacyStation.stationName.find("virtual") != std::string::npos) {
			continue;
		}
		QPointF sharedPoint;
		std::string sharedNodeId;
		const TrackPreviewLine* sharedLine = nullptr;
		if (sharedStationPoint(i, sharedPoint, sharedNodeId, sharedLine)) {
			const int platformCount = static_cast<int>(sceneModel.stations[static_cast<std::size_t>(i)].platforms.size());
			paintStationOverlay(sharedPoint,
				stationMarkVisual(sceneModel, sharedNodeId, platformCount),
				legacyStation.stationName, presentationScale(),
				stationArtworkOffset(*sharedLine, sceneModel.stationViews,
					sceneModel.stations[static_cast<std::size_t>(i)].id, legacyStation.stationName),
				platformCount);
			continue;
		}

		// Keep the native graph position as the source anchor when no shared line resolves.
		pt = QPointF(legacyStation.graphX, legacyStation.graphY);
		QPointF averageShift;
		int validRegions = 0;
		for (int region : legacyStation.regions) {
			const double x = legacyStation.shiftX[region];
			const double y = legacyStation.shiftY[region];
			if (std::isfinite(x) && std::isfinite(y)) {
				averageShift += QPointF(x, y);
				++validRegions;
			}
		}
		if (validRegions > 0)
			averageShift /= validRegions;
		const QPointF decorationOffset = averageShift * (station_name_graphID * track_separation)
			+ QPointF(0.0, stationNamedYOffset(legacyStation.stationName));

		const int platformCount = i < static_cast<int>(sceneModel.stations.size())
			? static_cast<int>(sceneModel.stations[static_cast<std::size_t>(i)].platforms.size())
			: 0;
		paintStationOverlay(pt, classifyStation(platformCount > 0), legacyStation.stationName, 1.0, decorationOffset,
			platformCount);
	}

	// draw connections
	QPointF ptc1, ptc2;
	for (int c = 0; c < m_source->connectionCount(); c++) {
		Connections& connection = m_source->connection(c);
		if (hasSharedPreview && (!cachedTrackLine(connection.idFirstTrackLine) || !cachedTrackLine(connection.idSecondTrackLine)))
			continue;
		// get shifted connections
		egtrainPoint2Screen(&connection, connection.idFirstTrackLine, connection.idSecondTrackLine, track_separation);
		ptc1.setX(connection.graphXFirstNode);
		ptc1.setY(connection.graphYFirstNode);
		ptc2.setX(connection.graphXSecondNode);
		ptc2.setY(connection.graphYSecondNode);

		paintConnection(ptc1, ptc2, line_width, &connection);
	}

	// draw tracklines
	QPointF pt_prev, pte;
	for (int track = 0; track < m_source->trackCount(); track++) {
		if (hasSharedPreview && !cachedTrackLine(track))
			continue;
		BlockSet& trackSet = m_source->track(track);
		// a double switch is drawn without its first and last arc and without ordinary node dots
		const bool doubleSwitch = drawnAsDoubleSwitch(sceneModel, trackSet.sceneTrackId);
		// run over all arcs
		for (int i = 0; i < trackSet.len; i++) {
			// get shifted nodes
			egtrainPoint2Screen(&trackSet.member[i].startNode, track, track_separation);
			egtrainPoint2Screen(&trackSet.member[i].endNode, track, track_separation);
			pt.setX(trackSet.member[i].startNode.graphX);
			pt.setY(trackSet.member[i].startNode.graphY);
			pte.setX(trackSet.member[i].endNode.graphX);
			pte.setY(trackSet.member[i].endNode.graphY);

			// draw nodes
			if (i > 0 && !trackSet.member[i - 1].endNode.stationName.empty()) { // station Node (only visible from end nodes [end_node] -> i>1)
				paintStationNode(pt, station_node_size, track, &trackSet.member[i - 1].endNode, false);
				if (hasPlatformBar(trackSet.member[i - 1].endNode.stationName, trackSet.member[i - 1].endNode.stationPlatformId)) {
					paintStationPlatform(pt_prev, pt, pte, station_node_size, &trackSet.member[i - 1].endNode);
				}
			}
			// if it is a double switch then do not paint nodes Copenhagen case
			else if (doubleSwitch || trackSet.numNodes == 4) {
				paintNode(pt, static_cast<int>(0.1), track, &trackSet.member[i].startNode);
			} else {
				paintNode(pt, node_size, track, &trackSet.member[i].startNode);
			}

			// draw arcs
			if (i > 0 && !(doubleSwitch && i == 1)) {
				paintArc(pt_prev, pt, line_width, track, &trackSet.member[i - 1]);
			}

			// draw last Arc and last Node - end_node Node needed
			if (i == (trackSet.len - 1)) {
				// draw last Arc
				if (!doubleSwitch)
					paintArc(pt, pte, line_width, track, &trackSet.member[i]);

				// draw last end_node Node
				if (!trackSet.member[i].endNode.stationName.empty()) { // station Node
					paintStationNode(pte, station_node_size, track, &trackSet.member[i].endNode, true);
					if (hasPlatformBar(trackSet.member[i].endNode.stationName, trackSet.member[i].endNode.stationPlatformId)) {
						paintStationPlatform(pt, pte, pte, station_node_size, &trackSet.member[i].endNode);
					}
				} else {
					paintNode(pte, doubleSwitch ? 0 : node_size, track, &trackSet.member[i].endNode);
				}
			}

			pt_prev = pt;
		}
	}

	// draw signalling; the signals name their sections by the labels of the scene of the run
	const SceneSectionLabels sectionLabels = sceneSectionLabels(buildSceneSectionInventory(sceneModel));
	for (int i = 0; i < m_source->sectionCount(); i++) {
		const Section& section = m_source->section(i);
		// draw signal
		if (section.trackLineId != -1) { // compound block sections are not drawn
			if (hasSharedPreview && !cachedTrackLine(section.trackLineId))
				continue;

			// ignore first signal of trackline
			if ((i == 0) || (section.trackLineId != m_source->section(i - 1).trackLineId)) {
				continue;
			}

			// ignore 'virtual' signals (in the middle of double switches)
			if (section.start_node.virtualSignal) {
				continue;
			}

			// draw signals at beggining of block section
			paintSignal(section.start_node.X, node_size, line_width, section.trackLineId, track_separation, i, sectionLabels);
		}
	}

	buildSignalIndex();
	buildTrackIndexes();
}

// draws a Node
void NetworkPresenter::paintNode(QPointF coord, int size, int track, Node* node) {
	QPen pen = QPen(Qt::lightGray);
	pen.setWidth(0);
	pen.setCosmetic(true);

	// center the station marker on its network coordinate
	const qreal diameter = size * (cachedTrackLine(track) ? presentationScale() : 1.0);
	QRectF rect = QRectF(0, 0, diameter, diameter);
	rect.moveCenter(coord);

	NodeItem* el = new NodeItem(rect);
	el->setPen(pen);
	el->setBrush(Qt::lightGray);

	// add track and Node pointer to ellipse item
	el->track = track;
	el->node = node;

	// add Node to scene
	m_scene->addItem(el);
}

// draws a station Node; its platform id darkens the square only at the end of a line
void NetworkPresenter::paintStationNode(QPointF coord, int size, int track, Node* node, bool lineEnd) {
	const bool hasPlatform = lineEnd && node && hasPlatformId(node->stationPlatformId);
	StationVisual visual = classifyStation(hasPlatform, node ? node->numConnections : 0);
	QPen pen = QPen(visual.outline);
	pen.setWidth(0);
	pen.setCosmetic(true);

	// draws using rectangle with center on top-left corner (center_x,center_y,width,height)
	const qreal diameter = size * (cachedTrackLine(track) ? presentationScale() : 1.0);
	QRectF rect = QRectF(0, 0, diameter, diameter);
	rect.moveCenter(coord);

	StationNodeItem* el = new StationNodeItem(rect);
	el->setPen(pen);
	el->setBrush(visual.fill);

	// add track and Node pointer to the station item
	el->track = track;
	el->node = node;

	// add station Node to scene
	m_scene->addItem(el);
}

// Adds the white bar of a platform, 5 by 0.9 times size, the side of the square of the station node at node, and puts it
// beside the node with placePlatformBar(), along the track from before through node to after. It is a decoration of the
// stations, which platformBarsVisible() shows.
PlatformItem* NetworkPresenter::addPlatformBar(QPointF before, QPointF node, QPointF after, qreal size) {
	QPen pen = QPen(Qt::white);
	pen.setWidth(0);
	pen.setCosmetic(true);
	// a rectangle centred on the origin of the item, which lies along the track on the upper side of the station Node
	auto* bar = new PlatformItem(QRectF(-2.5 * size, -0.45 * size, 5 * size, 0.9 * size));
	placePlatformBar(bar, before, node, after, size);
	bar->setPen(pen);
	bar->setBrush(Qt::white);
	m_scene->addItem(bar);
	m_stationDecorations.push_back(bar);
	bar->setVisible(platformBarsVisible());
	return bar;
}

// paint platform next to station Node (upper side), along the track from before through coord to after
void NetworkPresenter::paintStationPlatform(QPointF before, QPointF coord, QPointF after, int size, Node* node) {
	PlatformItem* platformItem = addPlatformBar(before, coord, after, size * presentationScale());
	platformItem->node = node;
	allPlatforms.push_back(platformItem);
	// The counter and the passengers of a platform belong to a run with passengers.
	if (!m_source->passengerGui())
		return;

	// find corresponding station platform
	const StationPlatform* platform = m_source->platform(node->stationName, node->stationPlatformId);

	// platform found
	if (platform) {
		platformItem->stationId = platform->StationID;
		platformItem->platformId = platform->ID;
		platformItem->maxVolume = platform->Max_Passenger_Volume;
	}

	// string with pax counter
	std::stringstream ss;
	ss << (platform ? platform->Current_N_Passengers : 0) << " of " << platformItem->maxVolume << " passengers";

	// convert string
	QString text = QString::fromStdString(ss.str());

	// set text size
	QFont font = QFont();
	font.setPixelSize((int)station_size / 15);

	auto* textItem = new QGraphicsTextItem(text);
	textItem->setDefaultTextColor(Qt::white);
	textItem->setFont(font);
	textItem->setScale(presentationScale());
	// the label is level and centred above the bar, whatever the direction of the bar
	textItem->setPos(QPointF(platformItem->sceneBoundingRect().center().x() - textItem->boundingRect().width() * presentationScale() / 2,
		platformItem->sceneBoundingRect().top() - textItem->boundingRect().height() * presentationScale()));
	textItem->setZValue(3); // draw on top of every item

	// add textItem pointer to rect item
	platformItem->textIcon = textItem;

	// add to scene
	m_scene->addItem(textItem);

	// hide textItem at first
	textItem->setVisible(false);

	m_platformsById[{platformItem->stationId, platformItem->platformId}].push_back(platformItem);
}

// paint train passenger info on top of train
void NetworkPresenter::paintTrainPassengerInfo(TrainItemGroup* trainItem) {
	// train head graphical coordinates
	TrainBodyItem* headPolygon = trainItem->trainPolygonItemList->at(0);
	QPointF frontUp = headPolygon->polygon().first();
	QPointF frontDown = headPolygon->polygon().last();

	// create line from train to message, drawn from the front of the train, where the group is
	const QPointF front = headPolygon->mapToScene((frontUp + frontDown) / 2);
	const QPointF start;
	qreal dx = 0;
	qreal dy = -1 * track_separation * presentationScale();
	QPointF end = start + QPointF(dx, dy);

	// paint line
	QPen pen = QPen(QColor(242, 161, 106));
	pen.setWidth(line_width);
	pen.setCosmetic(true);

	// draws a line from start to end with a given line width
	QGraphicsLineItem* line = new QGraphicsLineItem(QLineF(start.x(), start.y(), end.x(), end.y()));
	line->setPen(pen);

	// text box position
	QPointF coord = end; // coord is the center of box and text in x-axis and bottom of box in y-axis

	// set text size
	QFont font = QFont();
	font.setPixelSize((int)station_size / 15);

	// string to display
	std::stringstream ss;
	ss << trainItem->currentOnboardPassengers << " of " << trainItem->maxOnboardPassengers << " on board";

	// paint text
	QGraphicsTextItem* text = new QGraphicsTextItem;
	text->setPlainText(QString::fromStdString(ss.str())); // text without background
	text->setDefaultTextColor(Qt::black);
	text->setFont(font);
	text->setScale(presentationScale());

	// draw box around text
	QGraphicsRectItem* textBox = new QGraphicsRectItem;
	// box 15% bigger than text rect on both directions
	textBox->setRect(QRectF(0, 0, 1.15 * (text->boundingRect().width() * presentationScale()),
		1.15 * (text->boundingRect().height() * presentationScale())));
	textBox->setBrush(QColor(242, 161, 106));
	QPen boxPen = textBox->pen();
	boxPen.setWidthF(presentationScale());
	textBox->setPen(boxPen);
	textBox->setPos(coord.x() - (textBox->boundingRect().width() / 2), coord.y() - (textBox->boundingRect().height()));

	// set text position (center of text box)
	QPointF textPos = textBox->pos();
	textPos.rx() += 0.5 * (textBox->boundingRect().width() - (text->boundingRect().width() * presentationScale()));
	textPos.ry() += 0.5 * (textBox->boundingRect().height() - (text->boundingRect().height() * presentationScale()));
	text->setPos(textPos);

	// create group
	QGraphicsItemGroup* msgGroup = new QGraphicsItemGroup;
	msgGroup->addToGroup(line);
	msgGroup->addToGroup(textBox);
	msgGroup->addToGroup(text);
	msgGroup->setZValue(5);
	msgGroup->setPos(front);

	// add group to scene
	m_scene->addItem(msgGroup);

	// set current items
	trainPaxInfoItem = msgGroup;
	trainPaxItem = trainItem;
}

// paint passenger info on top of passenger icon
void NetworkPresenter::paintPassengerInfoIcon(PassengerItem* paxItem, const GuiPassengerState& passenger, bool replayActive) {
	// create line from icon to message
	QPointF start = QPointF(paxItem->sceneBoundingRect().center().x(), paxItem->sceneBoundingRect().top());
	qreal dx = 0;
	qreal dy = -0.75 * track_separation * presentationScale();
	QPointF end = start + QPointF(dx, dy);

	// paint line
	QPen pen = QPen(QColor(242, 161, 106));
	pen.setWidth(line_width);
	pen.setCosmetic(true);

	// draws a line from start to end with a given line width
	QGraphicsLineItem* line = new QGraphicsLineItem(QLineF(start.x(), start.y(), end.x(), end.y()));
	line->setPen(pen);

	// text box position
	QPointF coord = end; // coord is the center of box and text in x-axis and bottom of box in y-axis

	// set text size
	QFont font = QFont();
	font.setPixelSize((int)station_size / 15);

	// string to display
	std::stringstream ss;
	ss << "Passenger " << passenger.id << "\nStatus: " << passenger.status
	   << " (" << passenger.waitingPlatform << ")"
	   << "\nNext train: " << passenger.nextTrain;

	if (!passenger.nextDestination.empty()) {
		ss << "\nNext destination: " << passenger.nextDestination;
	}
	if (replayActive)
		ss << "\nJourney not shown in the replay";

	// paint text
	QGraphicsTextItem* text = new QGraphicsTextItem;
	text->setPlainText(QString::fromStdString(ss.str())); // text without background
	text->setDefaultTextColor(Qt::black);
	text->setFont(font);
	text->setScale(presentationScale());

	// draw box around text
	QGraphicsRectItem* textBox = new QGraphicsRectItem;
	// box 15% bigger than text rect on both directions
	textBox->setRect(QRectF(0, 0, 1.15 * (text->boundingRect().width() * presentationScale()),
		1.15 * (text->boundingRect().height() * presentationScale())));
	textBox->setBrush(QColor(242, 161, 106));
	QPen boxPen = textBox->pen();
	boxPen.setWidthF(presentationScale());
	textBox->setPen(boxPen);
	textBox->setPos(coord.x() - (textBox->boundingRect().width() / 2), coord.y() - (textBox->boundingRect().height()));

	// set text position (center of text box)
	QPointF textPos = textBox->pos();
	textPos.rx() += 0.5 * (textBox->boundingRect().width() - (text->boundingRect().width() * presentationScale()));
	textPos.ry() += 0.5 * (textBox->boundingRect().height() - (text->boundingRect().height() * presentationScale()));
	text->setPos(textPos);

	// create group
	QGraphicsItemGroup* msgGroup = new QGraphicsItemGroup;
	msgGroup->addToGroup(line);
	msgGroup->addToGroup(textBox);
	msgGroup->addToGroup(text);
	msgGroup->setZValue(5);

	// add group to scene
	m_scene->addItem(msgGroup);

	// set current items
	paxIconInfoItem = msgGroup;
	paxIconItem = paxItem;
}


// draws an Arc
void NetworkPresenter::paintArc(QPointF start, QPointF end, int penWidth, int track, Arc* arc) {
	arcDrawing(start, end, penWidth, track, arc);
}

// Arc drawing
void NetworkPresenter::arcDrawing(QPointF start, QPointF end, int penWidth, int track, Arc* arc) {
	TrackVisual visual = classifyTrackSpeed(arc ? arc->speedLimit : 0.0);
	QPen pen = QPen(visual.color);
	pen.setWidth(std::max(penWidth, visual.width));
	pen.setCosmetic(true);

	// draws a line from start to end with a given line width
	TrackLineItem* line = new TrackLineItem(QLineF(start.x(), start.y(), end.x(), end.y()));
	line->setPen(pen);

	// add track and Arc pointer to line item
	line->track = track;
	line->arc = arc;

	// RIGOS
	allArcs.push_back(line);

	// add item to scene
	m_scene->addItem(line);
}

// draws a connection
void NetworkPresenter::paintConnection(QPointF start, QPointF end, int penWidth, Connections* connection) {
	// draws a line from start to end with a given line width
	ConnectionItem* line = new ConnectionItem(QLineF(start.x(), start.y(), end.x(), end.y()));
	line->setPen(connectionPen(penWidth));

	// add connection pointer to line item
	line->connection = connection;

	// add item to scene
	m_scene->addItem(line);
}

// draws trackside signals (X is the beginning of the block section - except for the last signal of each trackline)
void NetworkPresenter::paintSignal(double X, int size, int penWidth, int track, int separation, int sectionIndex,
	const SceneSectionLabels& labels) {
	const Section& section = m_source->section(sectionIndex);
	const TrackPreviewLine* previewLine = cachedTrackLine(track);
	if (!previewLine && !hasTrackGeometry(track))
		return;

	double centerX, centerY, beforeX, beforeY, afterX, afterY;
	egtrainPoint2Screen(X, track, separation, centerX, centerY);
	egtrainPoint2Screen(X - 0.008, track, separation, beforeX, beforeY);
	egtrainPoint2Screen(X + 0.008, track, separation, afterX, afterY);
	QPointF normal;
	if (previewLine) {
		if (!previewSignalNormal(*previewLine, X, normal))
			return;
	} else {
		int stationIdx[2];
		neighbourStations(X, track, stationIdx);
		const int index = stationIdx[1];
		normal = QPointF(m_source->station(index).signalDeltaX[m_source->track(track).region],
			m_source->station(index).signalDeltaY[m_source->track(track).region]);
	}
	const SignalGeometry geometry = signalGeometry(QPointF(centerX, centerY),
		QPointF(beforeX, beforeY), QPointF(afterX, afterY), normal,
		separation * (previewLine ? presentationScale() : 1.0));

	// post/basis
	QPen penPost = QPen(Qt::white);
	penPost.setWidth(penWidth);
	penPost.setCosmetic(true);

	// plate
	const QPen penPlate = signalHeadPen();
	// draws using rectangle with center on top-left corner (center_x,center_y,width,height)
	const qreal diameter = size * (previewLine ? presentationScale() : 1.0);
	QRectF rect = QRectF(0, 0, diameter, diameter);
	rect.moveCenter(QPointF(0.0, 0.0));

	// post #1
	QGraphicsLineItem* post1 = new QGraphicsLineItem(geometry.reversedPost);
	post1->setPen(penPost);

	// plate #1
	SignalItem* plate1 = new SignalItem(rect);
	plate1->setZValue(3);
	plate1->setPos(geometry.reversedHead);
	plate1->setData(kSignalAnchorRole, plate1->pos());
	plate1->setPen(penPlate);
	plate1->setBrush(Qt::green);
	plate1->setAspectCode(180);

	// add trackID, direction and immutable section display fields to signal item
	plate1->trackID = section.trackLineId;
	plate1->X = X;
	plate1->setReversedDirection(true);
	// last signal of trackline
	if (X == section.end_node.X) {
		plate1->sectionAheadId = section.ID;
		plate1->sectionAheadLength = section.length;
		plate1->sectionAheadTrackId = section.trackLineId;
	}
	// remaining signals
	else {
		if (sectionIndex > 0 && m_source->section(sectionIndex - 1).trackLineId == section.trackLineId) {
			plate1->sectionAheadId = m_source->section(sectionIndex - 1).ID;
			plate1->sectionAheadLength = m_source->section(sectionIndex - 1).length;
			plate1->sectionAheadTrackId = m_source->section(sectionIndex - 1).trackLineId;
		}
		plate1->sectionBehindId = section.ID;
		plate1->sectionBehindLength = section.length;
		plate1->sectionBehindTrackId = section.trackLineId;
	}

	// basis #1, and the opposing direction's post and base
	QGraphicsLineItem* basis1 = new QGraphicsLineItem(geometry.reversedBase);
	basis1->setPen(penPost);
	QGraphicsLineItem* post2 = new QGraphicsLineItem(geometry.forwardPost);
	post2->setPen(penPost);

	SignalItem* plate2 = new SignalItem(rect);
	plate2->setZValue(3);
	plate2->setPos(geometry.forwardHead);
	plate2->setData(kSignalAnchorRole, plate2->pos());
	plate2->setPen(penPlate);
	plate2->setBrush(Qt::green);
	plate2->setAspectCode(180);

	// add trackID, direction and immutable section display fields to signal item
	plate2->trackID = section.trackLineId;
	plate2->X = X;
	plate2->setReversedDirection(false);
	// last signal of trackline
	if (X == section.end_node.X) {
		plate2->sectionBehindId = section.ID;
		plate2->sectionBehindLength = section.length;
		plate2->sectionBehindTrackId = section.trackLineId;
	}
	// remaining signals
	else {
		plate2->sectionAheadId = section.ID;
		plate2->sectionAheadLength = section.length;
		plate2->sectionAheadTrackId = section.trackLineId;
		if (sectionIndex > 0 && m_source->section(sectionIndex - 1).trackLineId == section.trackLineId) {
			plate2->sectionBehindId = m_source->section(sectionIndex - 1).ID;
			plate2->sectionBehindLength = m_source->section(sectionIndex - 1).length;
			plate2->sectionBehindTrackId = m_source->section(sectionIndex - 1).trackLineId;
		}
	}

	QGraphicsLineItem* basis2 = new QGraphicsLineItem(geometry.forwardBase);
	basis2->setPen(penPost);

	// The tooltip and the inspector name the track and the sections as the scene does.
	for (SignalItem* plate : {plate1, plate2}) {
		plate->sceneTrackId = m_source->track(track).sceneTrackId;
		if (!plate->sectionAheadId.empty())
			plate->sectionAheadLabel = labels.label(plate->sectionAheadId);
		if (!plate->sectionBehindId.empty())
			plate->sectionBehindLabel = labels.label(plate->sectionBehindId);
	}

	const auto addSignalDecoration = [this, track](QGraphicsItem* item) {
		item->setData(kSignalDecorationRole, true);
		item->setData(kSignalTrackRole, track);
		item->setData(kSignalBaseVisibleRole, true);
		m_scene->addItem(item);
		m_signalDecorations.push_back(item);
		item->setVisible(m_signalLayerVisible);
	};
	addSignalDecoration(post1);
	addSignalDecoration(plate1);
	addSignalDecoration(basis1);
	addSignalDecoration(post2);
	addSignalDecoration(plate2);
	addSignalDecoration(basis2);

	// add signals to allSignals list
	allSignals.push_back(plate1);
	allSignals.push_back(plate2);
}

// draws a train
void NetworkPresenter::paintTrain(const GuiTrainState& train, const SceneModel& sceneModel, bool promoted) {
	// The scene in the window is the scene that was run: an edit discards the results.
	QColor serviceColor;
	int red = 0, green = 0, blue = 0;
	if (sceneParseVisualizationColor(sceneServiceVisualizationColor(sceneModel, train.serviceId), &red, &green, &blue))
		serviceColor = QColor(red, green, blue);
	TrainVisual visual = resolveTrainVisual(train.type, train.description, serviceColor);
	QPen pen = QPen(visual.outline);
	pen.setWidthF(3 * presentationScale());

	// create train polygon item list
	QList<TrainBodyItem*>* trainPolygonItemList = new QList<TrainBodyItem*>();

	// create group of polygons
	TrainItemGroup* trainPolygonGroup = new TrainItemGroup();

	// get polygon for each wagon
	for (int wagon = 0; wagon <= train.wagonCount; wagon++) {
		QPolygonF trainPolygon;
		getTrainPolygon(&trainPolygon, wagon, train);

		// train graphical item
		TrainBodyItem* trainItem = new TrainBodyItem(trainPolygon);
		trainItem->setPen(pen);
		trainItem->setBrush(visual.fill);
		trainItem->setOpacity(1);

		trainItem->index = train.index;

		// add to polygon group
		trainPolygonGroup->addToGroup(trainItem);

		// add item to list
		trainPolygonItemList->push_back(trainItem);
	}
	const bool hasVisibleGeometry = std::any_of(trainPolygonItemList->cbegin(),
		trainPolygonItemList->cend(), [](const TrainBodyItem* item) {
			return item && !item->polygon().isEmpty();
		});

	// add train pointer and index to the item
	trainPolygonGroup->index = train.index;
	trainPolygonGroup->trainId = train.id;
	trainPolygonGroup->trainDescription = train.description;
	trainPolygonGroup->trainType = train.type;
	trainPolygonGroup->fillColor = visual.fill;
	trainPolygonGroup->outlineColor = visual.outline;
	trainPolygonGroup->serviceId = train.serviceId;
	trainPolygonGroup->trainLength = train.length;
	trainPolygonGroup->wagonCount = train.wagonCount;
	trainPolygonGroup->currentOnboardPassengers = train.currentOnboardPassengers;
	trainPolygonGroup->maxOnboardPassengers = train.maxOnboardPassengers;
	trainPolygonGroup->outOfSimulation = train.outOfSimulation;
	trainPolygonGroup->trainPolygonItemList = trainPolygonItemList;

	// add train to scene
	m_scene->addItem(trainPolygonGroup);
	if (m_view)
		trainPolygonGroup->applyViewScale(m_view->transform().m11());
	trainPolygonGroup->setVisible(m_trainLayerVisible && !train.outOfSimulation && hasVisibleGeometry);

	// add train to allTrains list
	allTrains.push_back(trainPolygonGroup);
	m_trainsByIndex.emplace(train.index, trainPolygonGroup);

	TrainBadgeItem* badge = new TrainBadgeItem();
	badge->setIdentifier(QString::fromStdString(guiTrainDisplayIdentifier(train)));
	badge->setTooltipDetails(QString::fromStdString(train.description),
		QString::fromStdString(train.operatingCode), QString::fromStdString(train.type));
	badge->setSpeedText(QString::fromStdString(formatSpeedLabel(train.speedKmh)));
	badge->setSpeedVisible(m_trainSpeedLabelsVisible);
	badge->setTrainVisual(visual);
	badge->setReversed(train.reversedDirection);
	badge->setPromoted(promoted);
	badge->setPresentation(TrainBadgeItem::presentationForZoom(
		m_view ? m_view->zoomRatio() : 1.0, promoted));
	m_scene->addItem(badge);
	badge->setVisible(m_trainLayerVisible && !train.outOfSimulation && hasVisibleGeometry);
	badge->setPos(trainPolygonGroup->sceneBoundingRect().center());
	m_trainBadges[train.index] = badge;
}

TrainItemGroup* NetworkPresenter::trainGroup(int train) const {
	const auto found = m_trainsByIndex.find(train);
	return found == m_trainsByIndex.end() ? nullptr : found->second;
}

QPointF NetworkPresenter::trainCenter(int train) const {
	const TrainItemGroup* group = trainGroup(train);
	if (!group)
		return QPointF();
	const auto found = m_trainMotions.find(train);
	QPointF center;
	if (found != m_trainMotions.end()) {
		const GuiTrainState& state = found->second.newest;
		if (routePoint(state.routeIndex, state.reversedDirection,
				middleOnRoute(state.routeIndex, found->second.drawnHead, state.length), center))
			return center;
	}
	return group->sceneBoundingRect().center();
}

// converts geodetic to cartesian coordinates
QPointF NetworkPresenter::Coord2ScreenPoint(double x, double y, double factor) const {
	// width and height are from the map (screen)
	double width = m_view ? m_view->width() : 0.0;
	double height = m_view ? m_view->height() : 0.0;
	double mercN = log(tan((kPi / 4) + (x * kPi / 180 / 2)));

	x = fmod((width * (180 + y) / 360), (width + (width / 2)));

	y = (height / 2) - (width * mercN / (2 * kPi));

	return QPointF(factor * x, factor * y);
}

// get station screen coordinates
// find shift vector for each station/region
// find delta X,Y to place trackside signals
void NetworkPresenter::calculateStationCoordAndShift() {
	const auto stationAt = [this](int index) -> Stations& { return m_source->station(index); };
	double alpha;	 // angle between two arcs (with a station in the middle)
	double gamma;	 // angle for shift (considered to be the angle with screen x axis (pointing down), being positive clockwise)
	double beta;	 // angle between first Arc and screen x axis
	double dot, det; // dot product and determinant
	double x1, x2, x3;
	double y1, y2, y3;
	QPointF pt;

	// get station screen coordinates
	for (int i = 0; i < m_source->stationCount(); i++) { // need a separate for as the one below accesses positions ahead of i
		if (std::fabs(stationAt(i).latitude) > 1e-12 || std::fabs(stationAt(i).longitude) > 1e-12) {
			pt = Coord2ScreenPoint(stationAt(i).latitude, stationAt(i).longitude, geo_scale);
			stationAt(i).graphX = pt.x();
			stationAt(i).graphY = pt.y();
		} else {
			// Native scenes may intentionally omit legacy geographic metadata.
			stationAt(i).graphX = stationAt(i).X * 1000.0;
			stationAt(i).graphY = stationAt(i).Y * 1000.0;
		}
		if (stationAt(i).regions.empty())
			stationAt(i).regions.push_back(0);
	}

	// find number of regions
	for (int i = 0; i < m_source->stationCount(); i++) {
		for (int j = 0; j < static_cast<int>(stationAt(i).regions.size()); j++) {
			// increase size if needed
			if (static_cast<std::size_t>(stationAt(i).regions[j]) >= m_regionStations.size()) {
				m_regionStations.resize(stationAt(i).regions[j] + 1);
			}
			m_regionStations[stationAt(i).regions[j]].push_back(i);
		}
	}

	// create X-ordered array of stations per region
	for (int i = 0; i < static_cast<int>(m_regionStations.size()); i++) {
		for (int j = 0; j < static_cast<int>(m_regionStations[i].size()); j++) {
			std::sort(m_regionStations[i].begin(), m_regionStations[i].end(),
				[&stationAt](const int& a, const int& b) -> bool { return stationAt(a).X < stationAt(b).X; });
		}
	}

	// calculate shifts and signal deltas per region
	for (int r = 0; r < static_cast<int>(m_regionStations.size()); r++) {
		if (m_regionStations[r].size() < 2) {
			continue;
		}
		if (m_regionStations[r].size() == 2) {
			int s1 = m_regionStations[r][0];
			int s2 = m_regionStations[r][1];
			x1 = stationAt(s1).graphX;
			y1 = stationAt(s1).graphY;
			x2 = stationAt(s2).graphX;
			y2 = stationAt(s2).graphY;
			beta = atan2(-(y1 - y2), (x1 - x2));
			stationAt(s1).signalDeltaX[r] = sin(beta);
			stationAt(s1).signalDeltaY[r] = cos(beta);
			stationAt(s1).shiftX[r] = cos(kPi / 2 - beta);
			stationAt(s1).shiftY[r] = sin(kPi / 2 - beta);
			stationAt(s2).signalDeltaX[r] = sin(beta);
			stationAt(s2).signalDeltaY[r] = cos(beta);
			stationAt(s2).shiftX[r] = cos(kPi / 2 - beta);
			stationAt(s2).shiftY[r] = sin(kPi / 2 - beta);
			continue;
		}
		for (int i = 0; i < static_cast<int>(m_regionStations[r].size()); i++) {
			if (i == static_cast<int>(m_regionStations[r].size()) - 1) { // special case - last Arc
				// use shifts of previous Arc
				stationAt(m_regionStations[r][i]).shiftX[r] = stationAt(m_regionStations[r][i - 1]).shiftX[r];
				stationAt(m_regionStations[r][i]).shiftY[r] = stationAt(m_regionStations[r][i - 1]).shiftY[r];

				// special case for signal deltas (need to use next Arc)
				x1 = stationAt(m_regionStations[r][i - 1]).graphX;
				x2 = stationAt(m_regionStations[r][i]).graphX;
				y1 = stationAt(m_regionStations[r][i - 1]).graphY;
				y2 = stationAt(m_regionStations[r][i]).graphY;

				beta = atan2(-(y1 - y2), (x1 - x2));

				// calculate trackside signal deltas
				stationAt(m_regionStations[r][i]).signalDeltaX[r] = sin(beta);
				stationAt(m_regionStations[r][i]).signalDeltaY[r] = cos(beta);
			} else {
				if (i == 0) { // special case - first Arc - do the same as 1st regular case
					x1 = stationAt(m_regionStations[r][i]).graphX;
					x2 = stationAt(m_regionStations[r][i + 1]).graphX;
					x3 = stationAt(m_regionStations[r][i + 2]).graphX;
					y1 = stationAt(m_regionStations[r][i]).graphY;
					y2 = stationAt(m_regionStations[r][i + 1]).graphY;
					y3 = stationAt(m_regionStations[r][i + 2]).graphY;

				} else { // regular case
					x1 = stationAt(m_regionStations[r][i - 1]).graphX;
					x2 = stationAt(m_regionStations[r][i]).graphX;
					x3 = stationAt(m_regionStations[r][i + 1]).graphX;
					y1 = stationAt(m_regionStations[r][i - 1]).graphY;
					y2 = stationAt(m_regionStations[r][i]).graphY;
					y3 = stationAt(m_regionStations[r][i + 1]).graphY;
				}

				dot = (x1 - x2) * (x3 - x2) + (y1 - y2) * (y3 - y2); // dot product
				det = (x1 - x2) * (y3 - y2) - (y1 - y2) * (x3 - x2); // determinant
				alpha = atan2(det, dot);							 // atan2(y, x)

				// adjust alpha to 0 to 2*kPi range
				if (alpha < 0) {
					alpha += 2 * kPi;
				}

				beta = atan2(-(y1 - y2), (x1 - x2));

				gamma = alpha / 2 - beta;

				// calculate trackside signal deltas
				stationAt(m_regionStations[r][i]).signalDeltaX[r] = sin(beta);
				stationAt(m_regionStations[r][i]).signalDeltaY[r] = cos(beta);

				// calculate shifts
				stationAt(m_regionStations[r][i]).shiftX[r] = cos(gamma);
				stationAt(m_regionStations[r][i]).shiftY[r] = sin(gamma);
			}
		}
	}
}

bool NetworkPresenter::hasTrackGeometry(int track) const {
	if (track < 0 || track >= m_source->trackCount())
		return false;
	const int region = m_source->track(track).region;
	return region >= 0 && region < static_cast<int>(m_regionStations.size()) && m_regionStations[region].size() >= 2;
}


// finds the indexes of the two closest stations given a point
void NetworkPresenter::neighbourStations(double X, int tracklineID, int* stationIdx) const {
	stationIdx[0] = 0;
	stationIdx[1] = 0;
	if (!hasTrackGeometry(tracklineID))
		return;
	const int region = m_source->track(tracklineID).region;
	std::vector<int> trackStIdx = m_regionStations[region];
	const auto regionX = [this, region](int index) { return m_source->station(index).regionX[region]; };

	// find neighbour stations
	// check if value is inside/outside X range of stations
	if (X < regionX(trackStIdx[0])) { // x before first station (interpolation with fisrt two stations)
		stationIdx[0] = trackStIdx[0];
		stationIdx[1] = trackStIdx[1];
	} else if (X >= regionX(trackStIdx[trackStIdx.size() - 1])) { // x >= last station (interpolation with last two stations)
		stationIdx[0] = trackStIdx[trackStIdx.size() - 2];
		stationIdx[1] = trackStIdx[trackStIdx.size() - 1];
	} else { // point inside range of stations (interpolation with two closest stations)
		// find closest stations
		for (int i = 0; i < static_cast<int>(trackStIdx.size()) - 1; i++) {
			if ((X >= regionX(trackStIdx[i])) && (X < regionX(trackStIdx[i + 1]))) { // right side of the interval must be open
				stationIdx[0] = trackStIdx[i];
				stationIdx[1] = trackStIdx[i + 1];
				break;
			}
		}
	}
}

// get shifted screen coordinates of a point (given 1D X coordinate)
void NetworkPresenter::egtrainPoint2Screen(double X, int track, double separation, double& graphX, double& graphY) const {
	if (const auto* line = cachedTrackLine(track)) {
		TrackPreviewPoint point;
		if (trackPreviewPointAtX(*line, X, point)) {
			graphX = point.x;
			graphY = point.y + line->displayOffset;
			return;
		}
	}
	if (!hasTrackGeometry(track)) {
		graphX = X * 1000.0;
		graphY = -static_cast<double>(m_source->track(track).graphID) * separation;
		return;
	}
	double graphID = (double)m_source->track(track).graphID;

	// find index of closest stations
	int stationIdx[2];
	neighbourStations(X, track, stationIdx);

	interpolateBetweenStations(m_source->station(stationIdx[0]), m_source->station(stationIdx[1]),
		m_source->track(track).region, X, graphID, separation, graphX, graphY);
}

// get shifted screen coordinates of a Node
void NetworkPresenter::egtrainPoint2Screen(Node* node, int track, double separation) const {
	if (const auto* line = cachedTrackLine(track)) {
		TrackPreviewPoint point;
		if ((!node->sceneNodeId.empty()
				&& trackPreviewPointAtNode(*line, node->sceneNodeId, point))
			|| trackPreviewPointAtX(*line, node->X, point)) {
			node->graphX = point.x;
			node->graphY = point.y + line->displayOffset;
			return;
		}
	}
	if (!hasTrackGeometry(track)) {
		node->graphX = node->X * 1000.0;
		node->graphY = node->Y * 1000.0 - static_cast<double>(m_source->track(track).graphID) * separation;
		return;
	}
	int stationIdx[2];
	neighbourStations(node->X, track, stationIdx);
	int graphID = m_source->track(track).graphID;

	interpolateBetweenStations(m_source->station(stationIdx[0]), m_source->station(stationIdx[1]),
		m_source->track(track).region, node->X, graphID, separation, node->graphX, node->graphY);
}

// get shifted screen coordinates of the two ends of a connection
void NetworkPresenter::egtrainPoint2Screen(Connections* connection, int track1, int track2, double separation) const {
	if (const auto* firstLine = cachedTrackLine(track1)) {
		if (const auto* secondLine = cachedTrackLine(track2)) {
			TrackPreviewPoint first;
			TrackPreviewPoint second;
			const bool hasFirst = (!connection->sceneFirstNodeId.empty()
									  && trackPreviewPointAtNode(*firstLine, connection->sceneFirstNodeId, first))
				|| trackPreviewPointAtX(*firstLine, connection->xFirstNode, first);
			const bool hasSecond = (!connection->sceneSecondNodeId.empty()
									   && trackPreviewPointAtNode(*secondLine, connection->sceneSecondNodeId, second))
				|| trackPreviewPointAtX(*secondLine, connection->xSecondNode, second);
			if (hasFirst && hasSecond) {
				connection->graphXFirstNode = first.x;
				connection->graphYFirstNode = first.y + firstLine->displayOffset;
				connection->graphXSecondNode = second.x;
				connection->graphYSecondNode = second.y + secondLine->displayOffset;
				return;
			}
		}
	}
	if (!hasTrackGeometry(track1) || !hasTrackGeometry(track2)) {
		connection->graphXFirstNode = connection->xFirstNode * 1000.0;
		connection->graphYFirstNode = -static_cast<double>(m_source->track(track1).graphID) * separation;
		connection->graphXSecondNode = connection->xSecondNode * 1000.0;
		connection->graphYSecondNode = -static_cast<double>(m_source->track(track2).graphID) * separation;
		return;
	}
	int graphID1 = m_source->track(track1).graphID;
	int graphID2 = m_source->track(track2).graphID;

	// find index of closest stations
	int stationIdx[2];
	neighbourStations(connection->xFirstNode, track1, stationIdx);
	interpolateBetweenStations(m_source->station(stationIdx[0]), m_source->station(stationIdx[1]),
		m_source->track(track1).region, connection->xFirstNode, graphID1, separation,
		connection->graphXFirstNode, connection->graphYFirstNode);

	// find index of closest stations
	neighbourStations(connection->xSecondNode, track2, stationIdx);
	interpolateBetweenStations(m_source->station(stationIdx[0]), m_source->station(stationIdx[1]),
		m_source->track(track2).region, connection->xSecondNode, graphID2, separation,
		connection->graphXSecondNode, connection->graphYSecondNode);
}

// The ends of the connection of a section derived at a switch, whose id is "<first id>@-<x1>/@<second id>@-<x2>": the
// sections of the two tracks that it joins and the X in kilometres of the connection on each. Returns false for another
// section and when the source lacks one of the two sections.
bool NetworkPresenter::switchSectionEnds(const Section& section, const Section*& first, const Section*& second,
	double& firstX, double& secondX) const {
	const std::size_t index1 = section.ID.find("@-");
	const std::size_t index2 = section.ID.find("/@", index1 + 1);
	const std::size_t index3 = section.ID.find("@-", index2 + 1);
	if (index1 == std::string::npos || index2 == std::string::npos || index3 == std::string::npos)
		return false;
	firstX = atof(section.ID.substr(index1 + 2, index2 - index1 - 2).c_str());
	secondX = atof(section.ID.substr(index3 + 2, std::string::npos).c_str());
	const auto sectionWith = [this](const std::string& id) -> const Section* {
		const auto found = m_sectionIndexById.find(id);
		return found == m_sectionIndexById.end() ? nullptr : &m_source->section(found->second);
	};
	first = sectionWith(section.ID.substr(0, index1 + 1));
	second = sectionWith(section.ID.substr(index2 + 1, index3 - index2));
	return first && second && first != second;
}

// The point of the drawn track at the X in kilometres in a section of a route, where getTrainPolygon() puts the end of
// a wagon there: on the track of a single section, and in a section derived at a switch on the first track up to the
// connection, on the straight line between the ends of the connection, and on the second track after it. Returns false
// where the track of the section is not drawn.
bool NetworkPresenter::sectionPointAt(const Section& section, double x, QPointF& point) const {
	const bool hasPreview = !m_cachedTrackPreview.lines.empty();
	const auto drawn = [this, hasPreview](int track) {
		return cachedTrackLine(track) || (!hasPreview && hasTrackGeometry(track));
	};
	const auto onTrack = [this](int track, double trackX) {
		QPointF at;
		egtrainPoint2Screen(trackX, track, track_separation, at.rx(), at.ry());
		return at;
	};
	if (section.ID.find('/') == std::string::npos) {
		if (!drawn(section.trackLineId))
			return false;
		point = onTrack(section.trackLineId, x);
		return true;
	}
	const Section* first = nullptr;
	const Section* second = nullptr;
	double firstX = 0.0;
	double secondX = 0.0;
	if (!switchSectionEnds(section, first, second, firstX, secondX) || !drawn(first->trackLineId)
		|| !drawn(second->trackLineId))
		return false;
	if (x <= firstX)
		point = onTrack(first->trackLineId, x);
	else if (x >= secondX)
		point = onTrack(second->trackLineId, x);
	else {
		const QPointF start = onTrack(first->trackLineId, firstX);
		point = start + (onTrack(second->trackLineId, secondX) - start) * ((x - firstX) / (secondX - firstX));
	}
	return true;
}

// Adds the X in kilometres of every corner of the drawn track of a section of a route that lies strictly between fromX
// and toX: the points of its line in the preview, or else the nodes of its arcs, and in a section derived at a switch
// the two ends of the connection.
void NetworkPresenter::addSectionCorners(const Section& section, double fromX, double toX, std::vector<double>& xs) const {
	const auto addTrack = [this, &xs](int track, double low, double high) {
		const auto add = [&xs, low, high](double x) {
			if (x > low && x < high)
				xs.push_back(x);
		};
		if (const TrackPreviewLine* line = cachedTrackLine(track)) {
			for (const TrackPreviewPoint& point : line->points)
				add(point.rawX);
		} else if (track >= 0 && track < m_source->trackCount()) {
			const BlockSet& arcs = m_source->track(track);
			for (int i = 0; i < std::min(arcs.len, static_cast<int>(arcs.member.size())); ++i) {
				add(arcs.member[static_cast<std::size_t>(i)].startNode.X);
				add(arcs.member[static_cast<std::size_t>(i)].endNode.X);
			}
		}
	};
	const double low = std::min(fromX, toX);
	const double high = std::max(fromX, toX);
	if (section.ID.find('/') == std::string::npos) {
		addTrack(section.trackLineId, low, high);
		return;
	}
	const Section* first = nullptr;
	const Section* second = nullptr;
	double firstX = 0.0;
	double secondX = 0.0;
	if (!switchSectionEnds(section, first, second, firstX, secondX))
		return;
	addTrack(first->trackLineId, low, std::min(high, firstX));
	addTrack(second->trackLineId, std::max(low, secondX), high);
	for (const double end : {firstX, secondX})
		if (end > low && end < high)
			xs.push_back(end);
}

// The point of the drawn track at a position along a route, in metres along the route; at the end of a section, the
// point of that section. Returns false when the position is on no section of the route or has no point on the canvas.
bool NetworkPresenter::routePoint(int routeIndex, bool reversed, double position, QPointF& point) const {
	RoutePath path;
	if (!routePath(routeIndex, reversed, position, position, path))
		return false;
	point = path.points.back();
	return true;
}

// The drawn track along a route, run in reverse when reversed, from the position from to the position to, in metres
// along the route, found section by section as getTrainPolygon() finds the ends of the wagons. A position at the end of
// a section has the point of that section and, unless the path ends there, also that of the next one. Returns false
// when to lies before from, and when a position between them is on no section of the route or has no point on the
// canvas.
bool NetworkPresenter::routePath(int routeIndex, bool reversed, double from, double to, RoutePath& path) const {
	const Route* route = m_source ? m_source->route(routeIndex) : nullptr;
	if (!route || to < from)
		return false;
	path = RoutePath();
	// The sections of a reversed route run against their X.
	const double sign = reversed ? -1.0 : 1.0;
	const auto add = [this, &path](const Section& section, double position, double x) {
		QPointF point;
		if (!sectionPointAt(section, x, point))
			return false;
		path.lengths.push_back(path.points.empty() ? 0.0 : path.lengths.back() + QLineF(path.points.back(), point).length());
		path.points.push_back(point);
		path.positions.push_back(position);
		return true;
	};
	const int sections = std::min(route->N_Block_Sections, static_cast<int>(route->sequence_of_block_sections.size()));
	for (int h = 0; h < sections; ++h) {
		const Section& section = route->sequence_of_block_sections[static_cast<std::size_t>(h)];
		const double start = section.start_node.X * 1000.0;
		const double end = section.end_node.X * 1000.0;
		if (end < from || start > to)
			continue;
		const double first = std::max(from, start);
		const double last = std::min(to, end);
		// The path begins at from, and the sections of the route follow each other without a gap, up to rounding.
		if (first > (path.positions.empty() ? from : path.positions.back()) + 1e-6)
			return false;
		const auto xAt = [&section, start, sign](double position) {
			return (section.GeoXBegNode + sign * (position - start)) / 1000.0;
		};
		std::vector<double> corners;
		addSectionCorners(section, xAt(first), xAt(last), corners);
		std::sort(corners.begin(), corners.end(), [sign](double a, double b) { return sign > 0.0 ? a < b : a > b; });
		if (!add(section, first, xAt(first)))
			return false;
		for (const double x : corners)
			if (!add(section, start + sign * (x * 1000.0 - section.GeoXBegNode), x))
				return false;
		if (last > first && !add(section, last, xAt(last)))
			return false;
		if (last >= to)
			return true;
	}
	return false;
}

void NetworkPresenter::setClock(Clock clock) {
	m_clock = std::move(clock);
	// What was measured on the old clock means nothing on the new one.
	m_updateIntervals.clear();
	m_lastUpdateMs = -1;
	for (auto& entry : m_trainMotions) {
		entry.second.fromHead = entry.second.newest.routeAxisPosition;
		entry.second.fromMs = entry.second.toMs = std::numeric_limits<qint64>::min();
	}
}

qint64 NetworkPresenter::nowMs() const {
	return m_clock ? m_clock() : m_elapsed.elapsed();
}

// Keeps the last intervals between two live updates.
void NetworkPresenter::noteLiveUpdate() {
	const qint64 now = nowMs();
	const qint64 interval = m_lastUpdateMs < 0 ? -1 : now - m_lastUpdateMs;
	m_lastUpdateMs = now;
	if (interval <= 0 || interval > kMaxUpdateIntervalMs)
		return;
	if (m_updateIntervals.size() == kMeasuredIntervals)
		m_updateIntervals.erase(m_updateIntervals.begin());
	m_updateIntervals.push_back(interval);
}

// Several times the longest of the last update intervals but one, so that the next update changes the speed of a glide
// only a little while the intervals vary, and one long gap, after a slow frame, does not make the trains trail far
// behind.
qint64 NetworkPresenter::glideDurationMs() const {
	if (m_updateIntervals.empty())
		return kDefaultGlideMs;
	std::vector<qint64> sorted = m_updateIntervals;
	std::sort(sorted.begin(), sorted.end(), std::greater<>());
	const qint64 longest = sorted[std::min<std::size_t>(1, sorted.size() - 1)];
	return std::clamp<qint64>(std::llround(kGlideFactor * static_cast<double>(longest)), kMinGlideMs, kMaxGlideMs);
}

// The longest move of a train from one update to the next that it glides, in scene units: the larger side of the visible
// scene. Without a view a glide has no limit.
qreal NetworkPresenter::longestGlide() const {
	if (!m_view)
		return std::numeric_limits<qreal>::max();
	const QSizeF visible = m_view->mapToScene(m_view->viewport()->rect()).boundingRect().size();
	return std::max(visible.width(), visible.height());
}

// The middle of the part of a train that is on its route, with its head at head, in metres along the route. It moves
// on without a jump while the train enters its route and while it leaves it.
double NetworkPresenter::middleOnRoute(int routeIndex, double head, double length) const {
	double start = 0.0;
	double end = 0.0;
	if (!routeExtent(m_source ? m_source->route(routeIndex) : nullptr, start, end))
		return head - length / 2.0;
	return (std::clamp(head - length, start, end) + std::clamp(head, start, end)) / 2.0;
}

// The position of the head of a train along its route at the time at: on its way at an even speed from where its glide
// started to the place of its last update, and at that place once the glide has ended.
double NetworkPresenter::glideHead(const TrainMotion& motion, qint64 at) const {
	const double to = motion.newest.routeAxisPosition;
	if (at >= motion.toMs)
		return to;
	if (at <= motion.fromMs)
		return motion.fromHead;
	return motion.fromHead
		+ (to - motion.fromHead) * static_cast<double>(at - motion.fromMs) / static_cast<double>(motion.toMs - motion.fromMs);
}

// Gives a train the state of an update and starts its glide at the time now from drawnHead, where its head is drawn,
// to its new place, over glideMs. A train with no glide stands at the new place.
void NetworkPresenter::startGlide(TrainMotion& motion, const GuiTrainState& state, qint64 now, double drawnHead,
	qint64 glideMs) {
	GuiTrainState& newest = motion.newest;
	newest.routeIndex = state.routeIndex;
	newest.reversedDirection = state.reversedDirection;
	newest.wagonCount = state.wagonCount;
	newest.length = state.length;
	newest.routeAxisPosition = state.routeAxisPosition;
	newest.wagonHeadPositions = state.wagonHeadPositions;
	newest.wagonTailPositions = state.wagonTailPositions;
	motion.fromHead = glideMs > 0 ? drawnHead : state.routeAxisPosition;
	motion.fromMs = now;
	motion.toMs = now + glideMs;
}

// Draws a train with its head at head along its route: at the place of its last update with the positions of the wagons
// there, and elsewhere with every end of a wagon at its distance from the head along the route, found as the simulation
// finds it, -1 where it is on no section of the route. The badge goes to the centre of the bodies and the callouts to
// the front of the train. Tells whether a body has a shape.
bool NetworkPresenter::drawTrain(TrainItemGroup& trainItem, TrainMotion& motion, double head) {
	const GuiTrainState& newest = motion.newest;
	const GuiTrainState* state = &newest;
	if (head != newest.routeAxisPosition) {
		GuiTrainState& drawn = motion.drawn;
		drawn.routeIndex = newest.routeIndex;
		drawn.reversedDirection = newest.reversedDirection;
		drawn.wagonCount = newest.wagonCount;
		drawn.length = newest.length;
		drawn.routeAxisPosition = head;
		const Route* route = m_source ? m_source->route(newest.routeIndex) : nullptr;
		const std::size_t wagons = static_cast<std::size_t>(std::max(newest.wagonCount + 1, 0));
		const double wagonLength = newest.length / (newest.wagonCount + 1);
		drawn.wagonHeadPositions.resize(wagons);
		drawn.wagonTailPositions.resize(wagons);
		for (std::size_t wagon = 0; wagon < wagons; ++wagon) {
			const double front = head - static_cast<double>(wagon) * wagonLength;
			drawn.wagonHeadPositions[wagon] = route ? routeXAt(*route, newest.reversedDirection, front) : -1.0;
			drawn.wagonTailPositions[wagon] = route ? routeXAt(*route, newest.reversedDirection, front - wagonLength) : -1.0;
		}
		state = &drawn;
	}
	motion.drawnHead = head;
	const bool hasVisibleGeometry = getTrainPolygonItemList(trainItem.trainPolygonItemList, *state);
	trainItem.setVisible(m_trainLayerVisible && hasVisibleGeometry);
	if (TrainBadgeItem* badge = m_trainBadges.value(trainItem.index, nullptr)) {
		badge->setVisible(m_trainLayerVisible && hasVisibleGeometry);
		badge->setPos(trainItem.sceneBoundingRect().center());
	}
	moveTrainCallouts(trainItem);
	return hasVisibleGeometry;
}

// Puts the message of a virtual coupling of a train, and the passenger information of the selected train, at the front
// of the train as it is drawn. paintVCouplingMsg() and paintTrainPassengerInfo() draw both from their own origin.
void NetworkPresenter::moveTrainCallouts(const TrainItemGroup& trainItem) {
	QGraphicsItemGroup* message = m_vcMessageItems.value(trainItem.index, nullptr);
	QGraphicsItemGroup* passengers = trainPaxItem == &trainItem ? trainPaxInfoItem : nullptr;
	QPointF front;
	if ((!message && !passengers) || !trainFront(trainItem, front))
		return;
	if (message)
		message->setPos(front);
	if (passengers)
		passengers->setPos(front);
}

void NetworkPresenter::stepTrains() {
	QEGTRAIN_PROFILE_SCOPE("gui/train_motion/step", "gui", "");
	const qint64 at = nowMs();
	// The scene around the visible scene, as far again on every side. The steps do not move a train that is drawn
	// beyond it: the next update draws every train where its glide then is, and a step draws it when its glide ends. A
	// train can be drawn further than this margin behind its newest place, so one that glides into the view from beyond
	// it shows there only at the next update, or at a pause only when its glide ends.
	QRectF around;
	if (m_view) {
		const QRectF visible = m_view->mapToScene(m_view->viewport()->rect()).boundingRect();
		const qreal margin = std::max(visible.width(), visible.height());
		around = visible.adjusted(-margin, -margin, margin, margin);
	}
	bool moving = false;
	for (auto& [train, motion] : m_trainMotions) {
		const double head = glideHead(motion, at);
		const bool stops = head == motion.newest.routeAxisPosition;
		moving = moving || !stops;
		TrainItemGroup* trainItem = head == motion.drawnHead ? nullptr : trainGroup(train);
		if (!trainItem)
			continue;
		const bool promoted = m_host->isTrainOverlayPromoted(train);
		if (!stops && !promoted) {
			const QRectF drawn = trainItem->sceneBoundingRect();
			if (!m_trainLayerVisible || (!around.isNull() && !drawn.isEmpty() && !around.intersects(drawn)))
				continue;
		}
		drawTrain(*trainItem, motion, head);
		if (promoted)
			m_host->trainMoved(train, trainCenter(train));
	}
	if (!moving)
		m_motionTimer.stop();
}

void NetworkPresenter::flushSceneUpdates() {
	// The work of the scene queues more of its own, as its changed() signal and the selection cue that follows it, so it
	// runs in a few rounds.
	for (int round = 0; m_scene && round < 3; ++round)
		QCoreApplication::sendPostedEvents(m_scene, QEvent::MetaCall);
}

bool NetworkPresenter::trainMoving(int train) const {
	const auto found = m_trainMotions.find(train);
	return found != m_trainMotions.end() && found->second.drawnHead != found->second.newest.routeAxisPosition;
}

bool NetworkPresenter::trainsMoving() const {
	return std::any_of(m_trainMotions.begin(), m_trainMotions.end(),
		[](const auto& entry) { return entry.second.drawnHead != entry.second.newest.routeAxisPosition; });
}

// returns the full train shape (list of polygons)
bool NetworkPresenter::getTrainPolygonItemList(QList<TrainBodyItem*>* trainPolygonItemList, const GuiTrainState& train) {
	if (!trainPolygonItemList)
		return false;
	// get the polygon of each wagon
	const int wagons = std::min(train.wagonCount + 1, static_cast<int>(trainPolygonItemList->size()));
	std::vector<QPolygonF> polygons(static_cast<std::size_t>(std::max(wagons, 0)));
	bool changed = false;
	bool hasVisibleGeometry = false;
	for (int wagon = 0; wagon < wagons; wagon++) {
		QPolygonF& trainPolygon = polygons[static_cast<std::size_t>(wagon)];
		getTrainPolygon(&trainPolygon, wagon, train);
		changed = changed || trainPolygon != trainPolygonItemList->at(wagon)->polygon();
		hasVisibleGeometry = hasVisibleGeometry || !trainPolygon.isEmpty();
	}
	// A train that stands still keeps its shape, and so does not invalidate the scene.
	if (!changed)
		return hasVisibleGeometry;
	for (TrainBodyItem* body : *trainPolygonItemList) {
		if (!body)
			continue;
		if (auto* group = qgraphicsitem_cast<TrainItemGroup*>(body->parentItem()))
			group->prepareForChildGeometryChange();
		break;
	}
	for (int wagon = 0; wagon < wagons; wagon++)
		trainPolygonItemList->at(wagon)->setPolygon(polygons[static_cast<std::size_t>(wagon)]);
	return hasVisibleGeometry;
}

// returns the shape of an one-wagon train (polygon)
// used to get each wagon of a multi-wagon train
void NetworkPresenter::getTrainPolygon(QPolygonF* trainPolygon, int wagon, const GuiTrainState& train) {
	const Route* trainRoute = m_source ? m_source->route(train.routeIndex) : nullptr;
	if (!trainPolygon || !trainRoute)
		return;
	double headX, tailX, connectionX1, connectionX2, x0, x1, x2, x3, y0, y1, y2, y3;
	double dot, det, beta, alpha, gamma;
	std::vector<double> posX, routeX;
	QPointF ptTrainEdge, ptConnection1, ptConnection2, ptBegin1, ptEnd2;
	const Section* currBS = nullptr;
	const Section* BS1 = nullptr;
	const Section* BS2 = nullptr;
	QVector<QPointF> trainPointsUp, trainPointsDown, trainPointsComplete;
	QVector<int> trainPointsStIndex, trainPointsStRegion, trainPointsGraphID;
	int revStIndex = 0; // used to add station points to the polygon (needed for reversed routes)
	double total_nW = train.wagonCount + 1;
	const auto addPreviewPoint = [&](const TrackPreviewLine* line, double rawX,
									 const QPointF& point) {
		QPointF normal;
		if (!line || !previewSignalNormal(*line, rawX, normal))
			return false;
		const QPointF offset = normal * (0.10 * track_separation * presentationScale());
		trainPointsUp.push_back(point - offset);
		trainPointsDown.push_front(point + offset);
		trainPointsStIndex.push_back(-1);
		trainPointsStRegion.push_back(-1);
		trainPointsGraphID.push_back(INT_MIN);
		return true;
	};
	const auto addTrackPoint = [&](const QPointF& point, int track, double rawX) {
		if (const auto* line = cachedTrackLine(track))
			return addPreviewPoint(line, rawX, point);
		int stationIdx[2];
		neighbourStations(rawX, track, stationIdx);
		const int prevIndex = stationIdx[0];
		const int index = stationIdx[1];
		const int revPrevIndex = index * (1 - revStIndex) + prevIndex * revStIndex;
		const int region = m_source->track(track).region;
		trainPointsUp.push_back(QPointF(point.x()
				- m_source->station(index).signalDeltaX[region] * 0.10 * track_separation,
			point.y() - m_source->station(index).signalDeltaY[region] * 0.10 * track_separation));
		trainPointsDown.push_front(QPointF(point.x()
				+ m_source->station(index).signalDeltaX[region] * 0.10 * track_separation,
			point.y() + m_source->station(index).signalDeltaY[region] * 0.10 * track_separation));
		trainPointsStIndex.push_back(revPrevIndex);
		trainPointsStRegion.push_back(region);
		trainPointsGraphID.push_back(m_source->track(track).graphID);
		return true;
	};

	// train position on X axis
	// reversed route
	if (wagon >= static_cast<int>(train.wagonHeadPositions.size()) || wagon >= static_cast<int>(train.wagonTailPositions.size()))
		return;
	headX = train.wagonHeadPositions[static_cast<std::size_t>(wagon)];
	tailX = train.wagonTailPositions[static_cast<std::size_t>(wagon)];
	revStIndex = train.reversedDirection ? 1 : 0;
	const Route& route = *trainRoute;
	const double routePosition = train.routeAxisPosition;

	// get occupied block sections (list of index on SeqBS)
	std::vector<int> occupiedBS; // we already know head block sections but not the index on SeqBS

	// find block sections where the train is (full length)
	int headIndex = -1, tailIndex = -1;
	for (int h = route.N_Block_Sections - 1; h >= 0; h--) {
		if (((routePosition - wagon * (train.length / total_nW)) < route.sequence_of_block_sections[h].end_node.X * 1000)
			&& ((routePosition - wagon * (train.length / total_nW)) >= route.sequence_of_block_sections[h].start_node.X * 1000)) {
			occupiedBS.push_back(h);
			headIndex = h;
		}
		if (((routePosition - (wagon + 1) * (train.length / total_nW)) < route.sequence_of_block_sections[h].end_node.X * 1000)
			&& ((routePosition - (wagon + 1) * (train.length / total_nW)) >= route.sequence_of_block_sections[h].start_node.X * 1000)) {
			if (h != headIndex) {
				occupiedBS.push_back(h);
			} // avoid adding the same block sections twice (entire train in the same block sections)
			tailIndex = h;
			break;
		}
		// section in between
		else if (h != headIndex && headIndex != -1) { // already added head block sections
			occupiedBS.push_back(h);
		}
	}

	// get all relevant points from occupied block sections (head, tail and also connection points)
	for (int i = 0; i < static_cast<int>(occupiedBS.size()); i++) { // run from head block sections to tail block sections (can be the same)
		currBS = &route.sequence_of_block_sections[occupiedBS[i]];

		if (headIndex == tailIndex && tailIndex == occupiedBS[i]) {
			posX = {headX, tailX};
			routeX = {(routePosition / 1000) - wagon * ((train.length / 1000) / total_nW), (routePosition / 1000) - (wagon + 1) * ((train.length / 1000) / total_nW)};
		} // head and tail in this section
		else if (occupiedBS[i] == headIndex) {
			posX = {headX};
			routeX = {(routePosition / 1000) - wagon * ((train.length / 1000) / total_nW)};
		} // only head
		else if (occupiedBS[i] == tailIndex) {
			posX = {tailX};
			routeX = {(routePosition / 1000) - (wagon + 1) * ((train.length / 1000) / total_nW)};
		} // only tail
		else {
			posX = {-DBL_MAX};
			routeX = {-DBL_MAX};
		} // section in between (no head or tail)

		// in case head and tail in the same block sections (need to get both)
		for (int j = 0; j < static_cast<int>(posX.size()); j++) {
			// single block sections -> get head or tail
			if (currBS->ID.find('/') == std::string::npos) {
				if (occupiedBS[i] != headIndex && occupiedBS[i] != tailIndex) {
					continue;
				} // single section in between (no relevant points)

				// get point
				if (!sectionPointAt(*currBS, posX[j], ptTrainEdge))
					continue;
				if (!addTrackPoint(ptTrainEdge, currBS->trackLineId, posX[j]))
					return;
			}
			// compound block sections -> get connection points and/or tail or head
			else {
				// the single block sections are needed for the graphID's
				if (switchSectionEnds(*currBS, BS1, BS2, connectionX1, connectionX2)) {
					const TrackPreviewLine* previewLine1 = cachedTrackLine(BS1->trackLineId);
					const TrackPreviewLine* previewLine2 = cachedTrackLine(BS2->trackLineId);
					const bool hasPreview = !m_cachedTrackPreview.lines.empty();
					const qreal lateralSeparation = track_separation * (hasPreview ? presentationScale() : 1.0);
					if ((hasPreview && (!previewLine1 || !previewLine2))
						|| (!hasPreview && (!hasTrackGeometry(BS1->trackLineId) || !hasTrackGeometry(BS2->trackLineId))))
						continue;

					// adds the point of a connection to the polygon, moved to both sides of the track by its shift
					const auto addConnectionPoint = [&](const QPointF& point, double shiftX, double shiftY, int stationIndex,
														const Section* section) {
						trainPointsUp.push_back(QPointF(point.x() - shiftX * 0.10 * lateralSeparation, point.y() - shiftY * 0.10 * lateralSeparation));
						trainPointsDown.push_front(QPointF(point.x() + shiftX * 0.10 * lateralSeparation, point.y() + shiftY * 0.10 * lateralSeparation));
						trainPointsStIndex.push_back(stationIndex);
						trainPointsStRegion.push_back(m_source->track(section->trackLineId).region);
						trainPointsGraphID.push_back(m_source->track(section->trackLineId).graphID);
					};

					// get beginning and end of connection to interpolate
					egtrainPoint2Screen(connectionX1, BS1->trackLineId, track_separation, ptConnection1.rx(), ptConnection1.ry());
					egtrainPoint2Screen(connectionX2, BS2->trackLineId, track_separation, ptConnection2.rx(), ptConnection2.ry());

					// get beginning of BS1 and end of BS2 to calculate shifts
					egtrainPoint2Screen(BS1->start_node.X, BS1->trackLineId, track_separation, ptBegin1.rx(), ptBegin1.ry());
					egtrainPoint2Screen(BS2->end_node.X, BS2->trackLineId, track_separation, ptEnd2.rx(), ptEnd2.ry());

					int revPrevIndexConnection1 = -1;
					int revPrevIndexConnection2 = -1;
					if (!hasPreview) {
						// get station indexes (used to add station points)
						int stationIdx[2];
						neighbourStations(connectionX1, BS1->trackLineId, stationIdx);
						revPrevIndexConnection1 = stationIdx[1] * (1 - revStIndex) + stationIdx[0] * revStIndex;
						neighbourStations(connectionX2, BS2->trackLineId, stationIdx);
						revPrevIndexConnection2 = stationIdx[1] * (1 - revStIndex) + stationIdx[0] * revStIndex;
					}

					// linear interpolation of geodetic coordinates using known coordinates of closest stations
					x0 = ptBegin1.x();
					y0 = ptBegin1.y();
					x1 = ptConnection1.x();
					y1 = ptConnection1.y();
					x2 = ptConnection2.x();
					y2 = ptConnection2.y();
					x3 = ptEnd2.x();
					y3 = ptEnd2.y();

					// shift on beginning of connection
					dot = (x0 - x1) * (x2 - x1) + (y0 - y1) * (y2 - y1); // dot product
					det = (x0 - x1) * (y2 - y1) - (y0 - y1) * (x2 - x1); // determinant
					alpha = atan2(det, dot);							 // atan2(y, x)
					if (alpha < 0) {
						alpha += 2 * kPi;
					} // adjust alpha to 0 to 2*kPi range
					beta = atan2(-(y0 - y1), (x0 - x1));
					gamma = alpha / 2 - beta;
					double connectionStartShiftX = cos(gamma);
					double connectionStartShiftY = sin(gamma);

					// shift on end of connection
					dot = (x1 - x2) * (x3 - x2) + (y1 - y2) * (y3 - y2); // dot product
					det = (x1 - x2) * (y3 - y2) - (y1 - y2) * (x3 - x2); // determinant
					alpha = atan2(det, dot);							 // atan2(y, x)
					if (alpha < 0) {
						alpha += 2 * kPi;
					} // adjust alpha to 0 to 2*kPi range
					beta = atan2(-(y1 - y2), (x1 - x2));
					gamma = alpha / 2 - beta;
					double connectionEndShiftX = cos(gamma);
					double connectionEndShiftY = sin(gamma);
					if (hasPreview) {
						QPointF normal;
						if (!previewSignalNormal(*previewLine1, connectionX1, normal))
							return;
						connectionStartShiftX = normal.x();
						connectionStartShiftY = normal.y();
						if (!previewSignalNormal(*previewLine2, connectionX2, normal))
							return;
						connectionEndShiftX = normal.x();
						connectionEndShiftY = normal.y();
					}

					// calculate connection deltas (inside connection)
					beta = atan2(-(y1 - y2), (x1 - x2));
					double connectionDeltaX = sin(beta);
					double connectionDeltaY = cos(beta);

					// add connection points if it is a middle section
					// add connection points if it is the tail block sections and head already added
					if ((posX[j] == -DBL_MAX) || (posX.size() == 1 && occupiedBS[i] == tailIndex)) { // adding connection points before adding tail of the train
						// non-reversed route (add first connection 2)
						if (!train.reversedDirection) {
							if (headX > connectionX2 && tailX < connectionX2) {
								addConnectionPoint(ptConnection2, connectionEndShiftX, connectionEndShiftY, revPrevIndexConnection2, BS2);
							}
							if (headX > connectionX1 && tailX < connectionX1) {
								addConnectionPoint(ptConnection1, connectionStartShiftX, connectionStartShiftY, revPrevIndexConnection1, BS1);
							}
						}
						// reversed route (add first connection 1)
						else {
							if (tailX > connectionX1 && headX < connectionX1) {
								addConnectionPoint(ptConnection1, connectionStartShiftX, connectionStartShiftY, revPrevIndexConnection1, BS1);
							}
							if (tailX > connectionX2 && headX < connectionX2) {
								addConnectionPoint(ptConnection2, connectionEndShiftX, connectionEndShiftY, revPrevIndexConnection2, BS2);
							}
						}
					}

					// head/tail in the 1st track and before connection
					if (routeX[j] >= currBS->start_node.X && posX[j] <= connectionX1) {
						if (!sectionPointAt(*currBS, posX[j], ptTrainEdge) || !addTrackPoint(ptTrainEdge, BS1->trackLineId, posX[j]))
							return;
					}
					// head/tail inside the connection
					else if (posX[j] > connectionX1 && posX[j] < connectionX2) {
						if (!sectionPointAt(*currBS, posX[j], ptTrainEdge))
							return;

						// train polygon
						trainPointsUp.push_back(QPointF(ptTrainEdge.x() - connectionDeltaX * 0.10 * lateralSeparation,
							ptTrainEdge.y() - connectionDeltaY * 0.10 * lateralSeparation));
						trainPointsDown.push_front(QPointF(ptTrainEdge.x() + connectionDeltaX * 0.10 * lateralSeparation,
							ptTrainEdge.y() + connectionDeltaY * 0.10 * lateralSeparation));
						trainPointsStIndex.push_back(-1);
						trainPointsStRegion.push_back(-1);
						trainPointsGraphID.push_back(INT_MIN);
					}
					// head/tail in the 2nd track and after connection
					else if (posX[j] >= connectionX2 && routeX[j] < currBS->end_node.X) {
						if (!sectionPointAt(*currBS, posX[j], ptTrainEdge) || !addTrackPoint(ptTrainEdge, BS2->trackLineId, posX[j]))
							return;
					}

					// add connection points if it is head section and tail is in another block sections
					// add connection points if head and tail in the same section
					if ((occupiedBS[i] == headIndex && headIndex != tailIndex) || (posX.size() == 2 && j == 0)) { // adding connection points after adding head of the train
						// non-reversed route (add first connection 2)
						if (!train.reversedDirection) {
							if (headX > connectionX2 && tailX < connectionX2) {
								addConnectionPoint(ptConnection2, connectionEndShiftX, connectionEndShiftY, revPrevIndexConnection2, BS2);
							}
							if (headX > connectionX1 && tailX < connectionX1) {
								addConnectionPoint(ptConnection1, connectionStartShiftX, connectionStartShiftY, revPrevIndexConnection1, BS1);
							}
						}
						// reversed route (add first connection 1)
						else {
							if (tailX > connectionX1 && headX < connectionX1) {
								addConnectionPoint(ptConnection1, connectionStartShiftX, connectionStartShiftY, revPrevIndexConnection1, BS1);
							}
							if (tailX > connectionX2 && headX < connectionX2) {
								addConnectionPoint(ptConnection2, connectionEndShiftX, connectionEndShiftY, revPrevIndexConnection2, BS2);
							}
						}
					}
				}
			}
		}
	}

	// add station points to polygon when necessary (change in shifts)
	int added = 0; // needed when adding new points to the vectors
	for (int i = 1; i < static_cast<int>(trainPointsStIndex.size()); i++) {
		// inside connection (no change in shift)
		if (trainPointsStIndex[i] == -1) {
			continue;
		}

		// change in shift when previous point is a connection (assuming no consecutive points inside connection)
		// change in shift when previous point is not a connection
		// ignore change in shift if region also changes
		if ((trainPointsStIndex[i - 1] == -1 && i >= 2 && trainPointsStIndex[i] != trainPointsStIndex[i - 2]
				&& trainPointsStRegion[i] == trainPointsStRegion[i - 2])
			|| (trainPointsStIndex[i - 1] != -1 && trainPointsStIndex[i] != trainPointsStIndex[i - 1]
				&& trainPointsStRegion[i] == trainPointsStRegion[i - 1])) {

			Stations& shiftedStation = m_source->station(trainPointsStIndex[i]);
			const int stationRegion = trainPointsStRegion[i];
			QPointF stationUp = QPointF(shiftedStation.graphX + shiftedStation.shiftX[stationRegion] * track_separation * (trainPointsGraphID[i] - 0.10),
				shiftedStation.graphY + shiftedStation.shiftY[stationRegion] * track_separation * (trainPointsGraphID[i] - 0.10));
			QPointF stationDown = QPointF(shiftedStation.graphX + shiftedStation.shiftX[stationRegion] * track_separation * (trainPointsGraphID[i] + 0.10),
				shiftedStation.graphY + shiftedStation.shiftY[stationRegion] * track_separation * (trainPointsGraphID[i] + 0.10));
			trainPointsUp.insert(i + added, stationUp);
			trainPointsDown.insert(i + added, stationDown);
			added++;
		}
	}

	// join Up and Down vectors to build the polygon
	trainPointsComplete.append(trainPointsUp);
	trainPointsComplete.append(trainPointsDown);

	QPolygonF UpdatedTrainPolygon(trainPointsComplete);
	trainPolygon->swap(UpdatedTrainPolygon);
}

// paint VCoupling notification
void NetworkPresenter::paintVCouplingMsg(TrainItemGroup* trainItem, const std::string& message) {
	if (!trainItem || !trainItem->trainPolygonItemList || trainItem->trainPolygonItemList->isEmpty())
		return;
	TrainBodyItem* headPolygon = trainItem->trainPolygonItemList->at(0);
	if (!headPolygon || headPolygon->polygon().isEmpty())
		return;
	QPointF frontUp = headPolygon->polygon().first();
	QPointF frontDown = headPolygon->polygon().last();

	// The message is drawn from the front of the train, where the group is.
	const QPointF front = headPolygon->mapToScene((frontUp + frontDown) / 2);
	const QPointF start;
	QPointF end = start + QPointF(0, -2 * track_separation * presentationScale());

	QPen pen = QPen(QColor(242, 161, 106));
	pen.setWidth(line_width);
	pen.setCosmetic(true);
	QGraphicsItemGroup* msgGroup = m_vcMessageItems.value(trainItem->index, nullptr);
	QGraphicsLineItem* line = nullptr;
	QGraphicsTextItem* text = nullptr;
	QGraphicsRectItem* textBox = nullptr;
	if (!msgGroup) {
		msgGroup = new QGraphicsItemGroup;
		line = new QGraphicsLineItem;
		textBox = new QGraphicsRectItem;
		text = new QGraphicsTextItem;
		msgGroup->addToGroup(line);
		msgGroup->addToGroup(textBox);
		msgGroup->addToGroup(text);
		// Over the trains (z 4) and their speed (z 4.2 and 4.3), and under the station names (z 4.5): the notice shows
		// by itself, and only what a click opens may cover a name.
		msgGroup->setZValue(4.4);
		m_scene->addItem(msgGroup);
		m_vcMessageItems.insert(trainItem->index, msgGroup);
	} else {
		for (QGraphicsItem* child : msgGroup->childItems()) {
			if (auto* lineChild = qgraphicsitem_cast<QGraphicsLineItem*>(child))
				line = lineChild;
			else if (auto* boxChild = qgraphicsitem_cast<QGraphicsRectItem*>(child))
				textBox = boxChild;
			else if (auto* textChild = qgraphicsitem_cast<QGraphicsTextItem*>(child))
				text = textChild;
		}
	}
	if (!line || !text || !textBox)
		return;

	line->setLine(QLineF(start.x(), start.y(), end.x(), end.y()));
	line->setPen(pen);

	QFont font = QFont();
	font.setPixelSize((int)station_size / 6);
	text->setPlainText(QString::fromStdString(message));
	text->setDefaultTextColor(Qt::white);
	text->setFont(font);
	text->setScale(presentationScale());

	textBox->setRect(QRectF(0, 0, 1.25 * (text->boundingRect().width() * presentationScale()), 1.25 * (text->boundingRect().height() * presentationScale())));
	textBox->setBrush(QColor(242, 161, 106));
	QPen boxPen = textBox->pen();
	boxPen.setWidthF(presentationScale());
	textBox->setPen(boxPen);
	textBox->setPos(end.x() - (textBox->boundingRect().width() / 2), end.y() - textBox->boundingRect().height());

	QPointF textPos = textBox->pos();
	textPos.rx() += 0.5 * (textBox->boundingRect().width() - (text->boundingRect().width() * presentationScale()));
	textPos.ry() += 0.5 * (textBox->boundingRect().height() - (text->boundingRect().height() * presentationScale()));
	text->setPos(textPos);
	msgGroup->setPos(front);
	msgGroup->setVisible(true);
}

GuiPassengerState passengerStateIn(const GuiSimulationSnapshot* snapshot, const std::string& id) {
	GuiPassengerState passenger;
	passenger.id = id;
	if (snapshot) {
		auto passengerIt = std::find_if(snapshot->passengers.begin(), snapshot->passengers.end(),
			[&id](const GuiPassengerState& value) { return value.id == id; });
		if (passengerIt != snapshot->passengers.end())
			passenger = *passengerIt;
	}
	return passenger;
}

bool NetworkPresenter::passengerGui() const {
	return m_source && m_source->passengerGui();
}

bool NetworkPresenter::paxTextVisible() const {
	if (!platformPassengersVisible() || !m_view)
		return false;
	return m_view->zoomRatio() >= kDenseDetailZoom;
}

void NetworkPresenter::apply(const GuiSimulationSnapshot& snapshot, Playback playback) {
	QEGTRAIN_PROFILE_SCOPE("gui/snapshot_delivery/render_frame", "gui", "gui/snapshot_delivery");

	{
		QEGTRAIN_PROFILE_SCOPE("gui/snapshot_delivery/render_frame/signalling", "gui",
			"gui/snapshot_delivery/render_frame");
		updateSignalling(snapshot);
	}
	{
		QEGTRAIN_PROFILE_SCOPE("gui/snapshot_delivery/render_frame/train_position", "gui",
			"gui/snapshot_delivery/render_frame");
		updateTrainPosition(snapshot, snapshot.timestep, playback);
	}

	// pax info
	if (passengerGui()) {
		{
			QEGTRAIN_PROFILE_SCOPE("gui/snapshot_delivery/render_frame/platforms", "gui",
				"gui/snapshot_delivery/render_frame");
			updatePlatforms(snapshot);
		}
		{
			QEGTRAIN_PROFILE_SCOPE("gui/snapshot_delivery/render_frame/passenger_icons", "gui",
				"gui/snapshot_delivery/render_frame");
			updatePaxIconInfo(&snapshot, playback);
		}
		{
			QEGTRAIN_PROFILE_SCOPE("gui/snapshot_delivery/render_frame/train_passenger_info", "gui",
				"gui/snapshot_delivery/render_frame");
			updateTrainPaxInfo();
		}
	}
}

void NetworkPresenter::updateSignalling(const GuiSimulationSnapshot& snapshot) {
	std::set<const SignalItem*> updated;
	for (const GuiSignalState& signal : snapshot.signalStates)
		updateSignalAspect(signal, updated);
	// A head without a state has no route running through its section in its
	// direction. It has nothing to show, so it must not keep an earlier aspect.
	for (SignalItem* signal : allSignals) {
		if (updated.count(signal))
			continue;
		signal->setAspectCode(kGuiSignalUnavailable);
		signal->setFailed(false);
	}
	m_host->signalsApplied();
}

void NetworkPresenter::updateSignalAspect(const GuiSignalState& state, std::set<const SignalItem*>& updated) {
	const auto heads = m_signalsByAheadId.find(state.sectionId);
	if (heads == m_signalsByAheadId.end())
		return;
	for (auto* signal : heads->second) {
		if (signal->reversedDirection != state.reversedDirection)
			continue;
		signal->setAspectCode(guiSignalDisplayCode(state));
		signal->setFailed(state.failed);
		updated.insert(signal);
	}
}

// update the passenger counter and the passenger icons at the platforms
void NetworkPresenter::updatePlatforms(const GuiSimulationSnapshot& snapshot) {
	// show text items from the beginning of the simulation
	if (snapshot.timestep == 0) {
		for (auto* platformIcon : allPlatforms) {
			if (platformIcon->textIcon)
				platformIcon->textIcon->setVisible(paxTextVisible());
		}
	}

	// A hidden layer shows none of this. The platforms are brought up to date when the layer is shown.
	m_platformsStale = !m_passengerLayerVisible;
	if (m_platformsStale)
		return;

	std::vector<PassengerItem*> restack;
	for (const GuiPlatformState& state : snapshot.platforms) {
		const auto platforms = m_platformsById.find({state.stationId, state.platformId});
		if (platforms == m_platformsById.end())
			continue;
		for (PlatformItem* platform : platforms->second)
			updatePlatform(platform, state, restack);
	}
	stackPassengerIcons(restack);
}

// The counter of a platform, and one icon for each passenger on it. An icon stays for as long as its passenger is on the
// platform and moves when the passengers share the length of the platform differently. The icons that this adds, or that
// swap places, are listed in restack.
void NetworkPresenter::updatePlatform(PlatformItem* platform, const GuiPlatformState& state, std::vector<PassengerItem*>& restack) {
	const auto& passengerIds = state.passengerIds;
	const int nPaxIcons = static_cast<int>(passengerIds.size());

	// string with pax counter
	std::stringstream ss;
	ss << nPaxIcons << " of " << state.maxVolume << " passengers";

	// convert string
	QString text = QString::fromStdString(ss.str());

	// update text item
	if (platform->textIcon->toPlainText() != text) {
		auto originalCenter = platform->textIcon->boundingRect().center();
		platform->textIcon->setPlainText(text);
		auto newCenter = platform->textIcon->boundingRect().center();
		auto delta = (originalCenter - newCenter) * presentationScale();
		platform->textIcon->moveBy(delta.x(), delta.y());
	}
	platform->textIcon->setVisible(paxTextVisible());

	// the icons stay when they belong to the same passengers in the same order
	QList<PassengerItem*>& icons = platform->passengerIcons;
	bool sameIcons = icons.size() == nPaxIcons;
	for (int i = 0; sameIcons && i < nPaxIcons; ++i)
		sameIcons = icons[i]->passengerId == passengerIds[static_cast<std::size_t>(i)];

	// the icons of the passengers that are on the platform, with their place in the list
	std::unordered_multimap<std::string_view, std::pair<PassengerItem*, int>> staying;
	if (!sameIcons)
		for (int i = 0; i < icons.size(); ++i)
			staying.emplace(icons[i]->passengerId, std::make_pair(icons[i], i));

	// the icons stand level, spread along the long axis of the bar
	const QRectF platformRect = platform->rect();
	const qreal iconSize = pax_pixmap_scaled.width() * presentationScale();
	const qreal iconSpacing = nPaxIcons > 0 ? platformRect.width() / nPaxIcons : 0.0;
	qreal iconX = platformRect.left() + iconSpacing / 2;
	QList<PassengerItem*> shown;
	shown.reserve(nPaxIcons);
	int previousPlace = -1;
	bool swapped = false;
	for (int i = 0; i < nPaxIcons; ++i, iconX += iconSpacing) {
		const std::string& paxID = passengerIds[static_cast<std::size_t>(i)];
		const QPointF position = platform->mapToScene(QPointF(iconX, platformRect.center().y())) - QPointF(iconSize / 2, iconSize / 2);
		if (sameIcons) {
			icons[i]->setPos(position);
			continue;
		}

		const auto stays = staying.find(paxID);
		if (stays != staying.end()) {
			PassengerItem* icon = stays->second.first;
			swapped = swapped || stays->second.second < previousPlace;
			previousPlace = stays->second.second;
			staying.erase(stays);
			icon->setPos(position);
			shown.push_back(icon);
			continue;
		}

		auto* item = new PassengerItem(pax_pixmap_scaled);
		item->setScale(presentationScale());
		item->setPos(position);
		item->setTransformationMode(Qt::SmoothTransformation);
		item->passengerId = paxID;
		item->setVisible(platformPassengersVisible());
		m_scene->addItem(item);
		shown.push_back(item);
		restack.push_back(item);
	}
	if (sameIcons)
		return;

	// the passengers that left take their icons with them, and with them the selection of the icon
	for (const auto& departed : staying) {
		if (departed.second.first == paxIconItem)
			paxIconItem = nullptr;
		delete departed.second.first;
	}
	if (swapped)
		restack.insert(restack.end(), shown.cbegin(), shown.cend());
	icons = std::move(shown);
}

// New icons are on top of the scene. The icons of the platforms are stacked in the order of the platforms and then of
// the passengers, the way a rebuild of all of them would stack them, so that icons that overlap look the same.
void NetworkPresenter::stackPassengerIcons(const std::vector<PassengerItem*>& restack) {
	if (restack.empty())
		return;
	const std::unordered_set<const PassengerItem*> moved(restack.cbegin(), restack.cend());
	PassengerItem* above = nullptr;
	for (auto platform = allPlatforms.size() - 1; platform >= 0; --platform) {
		const QList<PassengerItem*>& icons = allPlatforms[platform]->passengerIcons;
		for (auto i = icons.size() - 1; i >= 0; --i) {
			if (above && moved.count(icons[i]))
				icons[i]->stackBefore(above);
			above = icons[i];
		}
	}
}

// update the passenger counter of the selected train
void NetworkPresenter::updateTrainPaxInfo() {
	if (!m_passengerLayerVisible) {
		if (trainPaxInfoItem)
			trainPaxInfoItem->setVisible(false);
		return;
	}
	if (trainPaxInfoItem) {
		// remove existing item
		removeTrainPaxInfoIcon();
		if (trainPaxItem)
			paintTrainPassengerInfo(trainPaxItem);
	}
}

// update the passenger info of the selected passenger icon
void NetworkPresenter::updatePaxIconInfo(const GuiSimulationSnapshot* snapshot, Playback playback) {
	if (!m_passengerLayerVisible) {
		if (paxIconInfoItem)
			paxIconInfoItem->setVisible(false);
		return;
	}
	if (m_platformsStale && snapshot)
		updatePlatforms(*snapshot);
	if (paxIconInfoItem) {
		// remove existing item
		removePaxInfoIcon();
		if (paxIconItem)
			paintPassengerInfoIcon(paxIconItem, passengerStateIn(snapshot, paxIconItem->passengerId),
				playback == Playback::Replay);
		if (paxIconInfoItem)
			paxIconInfoItem->setVisible(platformPassengersVisible());
	}
}

void NetworkPresenter::removeTrainPaxInfoIcon() {
	if (trainPaxInfoItem) {
		m_scene->removeItem(trainPaxInfoItem);
		delete trainPaxInfoItem;
		trainPaxInfoItem = nullptr;
	}
}

void NetworkPresenter::removePaxInfoIcon() {
	if (paxIconInfoItem) {
		m_scene->removeItem(paxIconInfoItem);
		delete paxIconInfoItem;
		paxIconInfoItem = nullptr;
	}
}

// Shows the prepared and blocked sections and the occupied arcs on the track lines. An occupied line is red also when its
// section is blocked. A track line that shows what it is to show is not touched, so a step that changes nothing
// repaints none.
void NetworkPresenter::showTrackStates(const GuiSimulationSnapshot& snapshot, int t, bool replay) {
	// The state that each track line is to show, apart from free. The state with the highest priority wins.
	std::map<TrackLineItem*, TrackOperationalState> states;
	std::set<TrackLineItem*> occupied;
	const auto requireState = [&states](TrackLineItem* track, TrackOperationalState state) {
		if (!track)
			return;
		const auto entry = states.emplace(track, state);
		if (!entry.second && trackStatePriority(state) > trackStatePriority(entry.first->second))
			entry.first->second = state;
	};
	const auto requireSectionState = [this, &requireState](const GuiSectionState& section, TrackOperationalState state) {
		const auto tracks = m_tracksBySectionId.find(section.sectionId);
		if (tracks == m_tracksBySectionId.end())
			return;
		for (TrackLineItem* track : tracks->second)
			requireState(track, state);
	};
	for (const GuiSectionState& section : snapshot.sectionStates) {
		if (section.prepared)
			requireSectionState(section, TrackOperationalState::Prepared);
		if (section.blocked)
			requireSectionState(section, TrackOperationalState::Blocked);
	}
	for (const GuiTrainState& train : snapshot.trains) {
		if (replay && !guiReplayTrainHasPosition(train, t))
			continue;
		for (const GuiOccupiedArc& occupiedArc : train.occupiedArcs) {
			const auto track = m_tracksByOccupiedArc.find({occupiedArc.trackId, occupiedArc.startX});
			if (track != m_tracksByOccupiedArc.end()) {
				requireState(track->second, TrackOperationalState::Occupied);
				occupied.insert(track->second);
			}
		}
	}

	for (auto active = m_activeTrackItems.begin(); active != m_activeTrackItems.end();) {
		if (*active && states.count(*active)) {
			++active;
			continue;
		}
		if (*active)
			(*active)->setOperationalState(TrackOperationalState::Free);
		active = m_activeTrackItems.erase(active);
	}
	for (const auto& [track, state] : states) {
		track->setOperationalState(state, occupied.count(track) > 0);
		m_activeTrackItems.insert(track);
	}
}

void NetworkPresenter::clearOperationalTrackStates() {
	for (auto* track : m_activeTrackItems)
		if (track)
			track->setOperationalState(TrackOperationalState::Free);
	m_activeTrackItems.clear();
}

void NetworkPresenter::stopTrainAnimation(int train) {
	m_trainMotions.erase(train);
}

void NetworkPresenter::stopTrainAnimations() {
	m_motionTimer.stop();
	m_trainMotions.clear();
}

// updates train positions given a specific timestep
void NetworkPresenter::updateTrainPosition(const GuiSimulationSnapshot& snapshot, int t, Playback playback) {
	const bool replay = playback == Playback::Replay;
	const qint64 now = nowMs();
	if (!replay)
		noteLiveUpdate();
	bool legendNeedsUpdate = false;
	// The camera was given the position of the followed train in this update.
	bool followedTrainMoved = false;
	for (auto* group : m_vcMessageItems)
		if (group)
			group->setVisible(false);
	// update every train
	showTrackStates(snapshot, t, replay);
	for (const GuiTrainState& state : snapshot.trains) {
		const int train = state.index;
		m_host->trainWillUpdate(train);
		// check if train item exists
		TrainItemGroup* trainItem = trainGroup(train);

		// trainItem found
		if (trainItem) {
			trainItem->outOfSimulation = state.outOfSimulation;
			trainItem->currentOnboardPassengers = state.currentOnboardPassengers;
			trainItem->maxOnboardPassengers = state.maxOnboardPassengers;
			const bool hasPosition = guiReplayTrainHasPosition(state, t);
			trainItem->setVisible(m_trainLayerVisible && (replay ? hasPosition : !state.outOfSimulation));
			if (replay && !hasPosition && m_trainBadges.contains(train))
				m_trainBadges[train]->hide();
			// update train position
			if (!state.outOfSimulation && (!replay || hasPosition)) {
				// A live train goes on from where it is drawn now, unless it was placed otherwise, has another route,
				// direction or shape, runs backwards, or has jumped further than the visible view since the last update.
				const auto found = m_trainMotions.find(train);
				const TrainMotion* last = found == m_trainMotions.end() ? nullptr : &found->second;
				bool moves = !replay && last && last->newest.routeIndex == state.routeIndex
					&& last->newest.reversedDirection == state.reversedDirection && last->newest.wagonCount == state.wagonCount
					&& last->newest.length == state.length && state.routeAxisPosition >= last->newest.routeAxisPosition - 1e-6;
				const double drawnHead = moves ? glideHead(*last, now) : state.routeAxisPosition;
				if (moves) {
					RoutePath path;
					moves = routePath(state.routeIndex, state.reversedDirection,
								middleOnRoute(state.routeIndex, last->newest.routeAxisPosition, state.length),
								middleOnRoute(state.routeIndex, state.routeAxisPosition, state.length), path)
						&& path.lengths.back() <= longestGlide();
				}
				TrainMotion& motion = m_trainMotions[train];
				// An update that leaves the train at its place keeps the glide that runs, so that a train that stops
				// reaches its place when that glide ends and then stands still.
				if (moves && state.routeAxisPosition == motion.newest.routeAxisPosition)
					startGlide(motion, state, motion.fromMs, motion.fromHead, motion.toMs - motion.fromMs);
				else
					startGlide(motion, state, now, drawnHead, moves ? glideDurationMs() : 0);
				const bool hasVisibleGeometry = drawTrain(*trainItem, motion, moves ? drawnHead : state.routeAxisPosition);
				checkVCouplingMsg(trainItem, state, snapshot, t);
				if (TrainBadgeItem* badge = m_trainBadges.value(train, nullptr)) {
					badge->setIdentifier(QString::fromStdString(guiTrainDisplayIdentifier(state)));
					badge->setTooltipDetails(QString::fromStdString(state.description),
						QString::fromStdString(state.operatingCode), QString::fromStdString(state.type));
					badge->setSpeedText(QString::fromStdString(formatSpeedLabel(state.speedKmh)));
					badge->setSpeedVisible(m_trainSpeedLabelsVisible);
					badge->setReversed(state.reversedDirection);
					const bool promoted = m_host->isTrainOverlayPromoted(train);
					badge->setPromoted(promoted);
					badge->setPresentation(TrainBadgeItem::presentationForZoom(
						m_view ? m_view->zoomRatio() : 1.0, promoted));
					badge->setVisible(m_trainLayerVisible && hasVisibleGeometry);
				}
				if (m_host->trainPlaced(train, trainCenter(train)))
					followedTrainMoved = true;
			}
			// hide train whose simulation is finished
			else {
				stopTrainAnimation(train);
				trainItem->hide();
				if (m_trainBadges.contains(train))
					m_trainBadges[train]->setVisible(false);
				m_host->trainHidden(train);
			}
		}
		// new train starting; >= not == because the frame throttle may drop
		// the exact departure frame, and the recorded trajectory (not the live
		// train state) is what says whether the train is on the network at t
		else if (m_runModel
			&& (replay ? guiReplayTrainHasPosition(state, t)
					   : t >= state.departureTime && state.routeAxisPosition != -9999)) {
			const bool firstTrain = allTrains.isEmpty();
			paintTrain(state, *m_runModel, m_host->isTrainOverlayPromoted(state.index));
			// The next update moves the train on from here.
			TrainMotion& motion = m_trainMotions[train];
			startGlide(motion, state, now, state.routeAxisPosition, 0);
			motion.drawnHead = state.routeAxisPosition;
			legendNeedsUpdate = true;
			if (m_host->trainAdded(train, firstTrain && !replay))
				followedTrainMoved = true;
		}
	}
	if (!replay && trainsMoving() && !m_motionTimer.isActive())
		m_motionTimer.start();
	m_host->trainsApplied(legendNeedsUpdate, followedTrainMoved);
}

// manage VCoupling notifications
void NetworkPresenter::checkVCouplingMsg(TrainItemGroup* trainItem, const GuiTrainState& train,
	const GuiSimulationSnapshot& snapshot, int t) {
	for (auto it = snapshot.virtualCouplingMessages.rbegin(); it != snapshot.virtualCouplingMessages.rend(); ++it) {
		if (it->trainDescription == train.description && t >= it->timestep && t <= it->timestep + 40) {
			paintVCouplingMsg(trainItem, it->message);
			break;
		}
	}
}

void NetworkPresenter::buildSignalIndex() {
	m_signalsByAheadId.clear();
	for (auto* signal : allSignals) {
		if (!signal->sectionAheadId.empty()) {
			m_signalsByAheadId[signal->sectionAheadId].push_back(signal);
		}
	}
}

void NetworkPresenter::buildTrackIndexes() {
	m_tracksBySectionId.clear();
	m_sectionIndexById.clear();
	m_tracksByOccupiedArc.clear();

	for (auto* item : allArcs) {
		if (!item || !item->arc)
			continue;
		m_tracksByOccupiedArc[{item->track, item->arc->startNode.X}] = item;
	}

	const auto sameArc = [](const Arc& left, const Arc& right) {
		return (left.ID == right.ID && left.startNode.X == right.startNode.X && left.endNode.X == right.endNode.X)
			|| (left.startNode.X == right.startNode.X && left.endNode.X == right.endNode.X
				&& left.length == right.length);
	};
	for (int sectionIndex = 0; sectionIndex < m_source->sectionCount(); ++sectionIndex) {
		const Section& section = m_source->section(sectionIndex);
		if (section.ID.empty())
			continue;
		// the last section with the id
		m_sectionIndexById[section.ID] = sectionIndex;
		const int arcCount = std::min(section.total_arcs,
			static_cast<int>(sizeof(section.arcs_in_signalling_block_section)
				/ sizeof(section.arcs_in_signalling_block_section[0])));
		auto& tracks = m_tracksBySectionId[section.ID];
		for (int arcIndex = 0; arcIndex < arcCount; ++arcIndex) {
			const Arc& sectionArc = section.arcs_in_signalling_block_section[arcIndex];
			for (auto* item : allArcs) {
				if (!item || !item->arc || !sameArc(*item->arc, sectionArc))
					continue;
				const bool trackMatches = section.trackLineId < 0
					|| item->track == section.trackLineId
					|| item->track == section.FirstConnectedTrackLineID
					|| item->track == section.SecondConnectedTrackLineID;
				if (!trackMatches || std::find(tracks.begin(), tracks.end(), item) != tracks.end())
					continue;
				tracks.push_back(item);
			}
		}
		if (tracks.empty())
			m_tracksBySectionId.erase(section.ID);
	}
}

// The pen of a connection, shared by the preview before Run and a run.
QPen connectionPen(int width) {
	QPen pen(Qt::white);
	pen.setWidth(width);
	pen.setCosmetic(true);
	return pen;
}

// The outline of a signal head, a hairline at every zoom, shared by the preview before Run and a run.
QPen signalHeadPen() {
	QPen pen;
	pen.setWidth(0);
	return pen;
}

std::map<std::string, PreviewStationMembership> previewStationMemberships(const SceneModel& model) {
	std::map<std::string, PreviewStationMembership> result;
	std::set<std::tuple<std::string, std::string, std::string>> assigned;
	for (const auto& station : model.stations) {
		const auto assign = [&](const std::string& nodeId) -> PreviewStationMembership& {
			auto& membership = result[nodeId];
			membership.stationId = station.id;
			membership.name = station.name.empty() ? station.id : station.name;
			return membership;
		};
		if (!station.platforms.empty()) {
			for (const auto& platform : station.platforms)
				for (const auto& nodeId : platform.nodeIds)
					if (assigned.emplace(nodeId, station.id, platform.id).second)
						assign(nodeId).platformId = platform.id;
		} else if (station.hasPosition) {
			for (const auto& node : model.nodes)
				if (std::fabs(node.xKm - station.positionKm) <= 1e-8)
					assign(node.id); // Native position binding retains any earlier platform.
		}
	}
	return result;
}

bool previewPointAtNode(const TrackPreviewLine& line, const std::string& nodeId, qreal offset, QPointF& point) {
	TrackPreviewPoint candidate;
	if (!trackPreviewPointAtNode(line, nodeId, candidate))
		return false;
	point = QPointF(candidate.x, candidate.y + offset);
	return true;
}

bool previewPointAtX(const TrackPreviewLine& line, double x, qreal offset, QPointF& point) {
	TrackPreviewPoint candidate;
	if (!trackPreviewPointAtX(line, x, candidate))
		return false;
	point = QPointF(candidate.x, candidate.y + offset);
	return true;
}

QPointF stationDecorationOffset(const std::vector<SceneStationView>& views,
	const std::string& stationId, const std::string& name) {
	const auto shift = annotationStationShift(views, stationId);
	return QPointF(shift.first, shift.second + stationNamedYOffset(name));
}

bool previewSignalNormal(const TrackPreviewLine& line, double rawX, QPointF& normal) {
	std::pair<double, double> direction;
	if (!annotationSignalNormal(line, rawX, direction))
		return false;
	normal = QPointF(direction.first, direction.second);
	return true;
}
