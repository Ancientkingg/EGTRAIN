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

#include "simulation/GuiSimulationSnapshot.h"
#include "graphics/AnnotationPlacement.h"
#include "graphics/NetworkScene.h"
#include "graphics/NetworkView.h"
#include "graphics/RuntimeGeometrySource.h"
#include "graphics/SignalGeometry.h"
#include "graphics/VisualPolish.h"
#include "graphics/items/ConnectionItem.h"
#include "graphics/items/HighlightEffect.h"
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
#include "graphics/items/VirtualArcItem.h"
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

// The diameter of the nodes of a double switch, which keep their item but show no dot. It was once written as a
// cast of 0.1, which truncates to 0.
constexpr int kHiddenNodeSize = 0;

// The number of seconds after its step for which a message of virtual coupling shows at its train.
constexpr int kVirtualCouplingMessageSeconds = 40;

// The look of a callout: a line up from the origin of its group to the bottom centre of a box with a text in it.
struct CalloutLook {
	qreal rise;		  // The length of the line, in track separations.
	int fontDivisor;  // The pixel size of the font is station_size divided by this.
	qreal boxFactor;  // The box is this much wider and taller than the text.
	QColor textColor; // The colour of the text.
	qreal z;		  // The stacking of the callout.
};

// The passengers on board of the selected train.
const CalloutLook kTrainPassengersCallout{1.0, 15, 1.15, Qt::black, 5.0};
// The details of the selected passenger.
const CalloutLook kPassengerCallout{0.75, 15, 1.15, Qt::black, 5.0};
// The message of a virtual coupling, over the trains (z 4) and their speed (z 4.2 and 4.3), and under the station
// names (z 4.5): it shows by itself, and only what a click opens may cover a name.
const CalloutLook kVirtualCouplingCallout{2.0, 6, 1.25, Qt::white, 4.4};

// The orange of the line and the box of a callout.
const QColor kCalloutColor(242, 161, 106);

// Shows text in the callout group, which is made and added to scene when it is null, and returns the group. The
// items of the group are laid out again only when the text or the scale changes.
QGraphicsItemGroup* drawCallout(QGraphicsScene* scene, QGraphicsItemGroup* group, const CalloutLook& look,
	const QString& text, qreal scale) {
	const bool fresh = !group;
	QGraphicsLineItem* line = nullptr;
	QGraphicsRectItem* box = nullptr;
	QGraphicsTextItem* label = nullptr;
	if (fresh) {
		group = new QGraphicsItemGroup;
		line = new QGraphicsLineItem;
		box = new QGraphicsRectItem;
		label = new QGraphicsTextItem;
		QPen pen(kCalloutColor);
		pen.setWidth(NetworkPresenter::line_width);
		pen.setCosmetic(true);
		line->setPen(pen);
		box->setBrush(kCalloutColor);
		QFont font;
		font.setPixelSize(NetworkPresenter::station_size / look.fontDivisor);
		label->setFont(font);
		label->setDefaultTextColor(look.textColor);
		group->addToGroup(line);
		group->addToGroup(box);
		group->addToGroup(label);
		group->setZValue(look.z);
		scene->addItem(group);
	} else {
		for (QGraphicsItem* child : group->childItems()) {
			if (auto* lineChild = qgraphicsitem_cast<QGraphicsLineItem*>(child))
				line = lineChild;
			else if (auto* boxChild = qgraphicsitem_cast<QGraphicsRectItem*>(child))
				box = boxChild;
			else if (auto* textChild = qgraphicsitem_cast<QGraphicsTextItem*>(child))
				label = textChild;
		}
	}
	if (!line || !box || !label || (!fresh && label->toPlainText() == text && label->scale() == scale))
		return group;

	const QPointF end(0.0, -look.rise * NetworkPresenter::track_separation * scale);
	line->setLine(QLineF(QPointF(), end));
	label->setPlainText(text);
	label->setScale(scale);
	const QRectF textRect = label->boundingRect();
	box->setRect(QRectF(0, 0, look.boxFactor * (textRect.width() * scale), look.boxFactor * (textRect.height() * scale)));
	QPen boxPen = box->pen();
	boxPen.setWidthF(scale);
	box->setPen(boxPen);
	// the box stands on the end of the line, and the text is in its centre
	const QRectF boxRect = box->boundingRect();
	box->setPos(end.x() - boxRect.width() / 2, end.y() - boxRect.height());
	label->setPos(box->pos()
		+ QPointF(0.5 * (boxRect.width() - textRect.width() * scale), 0.5 * (boxRect.height() - textRect.height() * scale)));
	return group;
}

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
// slowest speed, 2x, the updates come every 500 ms and the next one comes about 100 ms before the glide ends, which
// starts at the update or at the step before it. The longest bound is a little above that interval.
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
// train moves, whenever the updates come. An update leaves a gliding train that the last step drew where it is, so
// that a frame that follows an update shows no more motion than another.
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

// The value of a display region in a map of the layout of a station, or 0 for a region that the map lacks.
double regionValue(const std::map<int, double>& values, int region) {
	const auto found = values.find(region);
	return found == values.end() ? 0.0 : found->second;
}

// The unit vector along which the display levels of the tracks shift at middle, on a line from previous through middle
// to next: the direction of the bisector of the angle at middle. The angle of the shift counts from the x axis of the
// screen, which points down and is positive clockwise.
QPointF bisectorShift(const QPointF& previous, const QPointF& middle, const QPointF& next) {
	const double x1 = previous.x();
	const double y1 = previous.y();
	const double x2 = middle.x();
	const double y2 = middle.y();
	const double x3 = next.x();
	const double y3 = next.y();
	const double dot = (x1 - x2) * (x3 - x2) + (y1 - y2) * (y3 - y2); // dot product
	const double det = (x1 - x2) * (y3 - y2) - (y1 - y2) * (x3 - x2); // determinant
	// the angle between the two arcs, in the range 0 to 2 * kPi
	double alpha = atan2(det, dot);
	if (alpha < 0)
		alpha += 2 * kPi;
	// the angle between the first arc and the x axis of the screen
	const double beta = atan2(-(y1 - y2), (x1 - x2));
	const double gamma = alpha / 2 - beta;
	return QPointF(cos(gamma), sin(gamma));
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
	m_switchEnds.clear();
	m_tracksByOccupiedArc.clear();
	m_activeTrackItems.clear();
	m_regionStations.clear();
	m_trackLevels.clear();
	m_stationLayouts.clear();
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

void NetworkPresenter::restorePreviewFit(const PreviewFit& fit) {
	m_previewFitBounds = fit.bounds;
	m_previewZoomFocus = fit.zoomFocus;
}

bool NetworkPresenter::hasContent() const {
	return !allTrains.isEmpty() || !allArcs.isEmpty() || !allSignals.isEmpty() || !allPlatforms.isEmpty()
		|| !m_stationOverlays.isEmpty() || !m_stationDecorations.isEmpty() || !m_signalDecorations.isEmpty()
		|| !m_vcMessageItems.isEmpty() || trainsMoving() || !m_tracksBySectionId.empty()
		|| !m_tracksByOccupiedArc.empty() || !m_activeTrackItems.empty();
}

void NetworkPresenter::setStationLayerVisible(bool visible) {
	m_stationLayerVisible = visible;
	for (auto* item : std::as_const(m_stationDecorations))
		if (item)
			item->setVisible(visible);
	for (auto* label : std::as_const(m_stationLabels))
		if (label)
			label->setVisible(visible && m_stationNamesVisible);
}

void NetworkPresenter::setStationNamesVisible(bool visible) {
	m_stationNamesVisible = visible;
	for (auto* overlay : std::as_const(m_stationOverlays))
		if (overlay)
			overlay->setNameVisible(visible);
	for (auto* label : std::as_const(m_stationLabels))
		if (label)
			label->setVisible(visible && m_stationLayerVisible);
}

void NetworkPresenter::setTrainLayerVisible(bool visible, const std::function<bool(const TrainItemGroup&)>& present) {
	m_trainLayerVisible = visible;
	for (auto* train : std::as_const(allTrains))
		if (train)
			train->setVisible(visible && present(*train));
	for (auto it = m_trainBadges.cbegin(); it != m_trainBadges.cend(); ++it) {
		if (!it.value())
			continue;
		auto trainIt = std::find_if(allTrains.cbegin(), allTrains.cend(),
			[it](const TrainItemGroup* train) { return train && train->index == it.key(); });
		it.value()->setVisible(visible && trainIt != allTrains.cend() && present(**trainIt));
	}
}

void NetworkPresenter::setTrainSpeedLabelsVisible(bool visible) {
	m_trainSpeedLabelsVisible = visible;
	for (auto* badge : std::as_const(m_trainBadges))
		if (badge)
			badge->setSpeedVisible(visible);
}

void NetworkPresenter::setSignalLayerVisible(bool visible) {
	m_signalLayerVisible = visible;
}

void NetworkPresenter::setPassengerLayerVisible(bool visible) {
	m_passengerLayerVisible = visible;
	applyPlatformVisibility();
	if (trainPaxInfoItem)
		trainPaxInfoItem->setVisible(visible);
}

void NetworkPresenter::updateViewOverlays(const std::function<bool(const StationOverlayItem*)>& selected,
	const StationOverlayItem* followed) {
	if (!m_view)
		return;
	for (auto* overlay : std::as_const(m_stationOverlays)) {
		if (!overlay)
			continue;
		overlay->setSelected(selected(overlay));
		overlay->setVisible(m_stationLayerVisible);
		overlay->setFollowed(overlay == followed);
	}
	const qreal viewScale = std::abs(m_view->transform().m11());
	if (effect)
		effect->setViewScale(viewScale);
	// The station marks give way to the station squares and the platform bars once the tracks of a station are far
	// enough apart on screen.
	const bool nearLook = m_stationTrackSpacing * viewScale >= StationOverlayItem::NearTrackSpacingPixels;
	setStationNearLook(nearLook);
	for (auto* overlay : std::as_const(m_stationOverlays))
		if (overlay)
			overlay->applyViewScale(viewScale, nearLook);
	for (TrainItemGroup* train : std::as_const(allTrains))
		if (train)
			train->applyViewScale(viewScale);
	StationOverlayItem::resolveNameCollisions(m_stationOverlays, m_view->viewportTransform());
	for (auto* overlay : std::as_const(m_stationOverlays))
		if (overlay && overlay->nameItem())
			overlay->nameItem()->setVisible(m_stationLayerVisible && m_stationNamesVisible
				&& !overlay->isNameHiddenByCollision());
}

// Keep every authored signal at its own anchor, including opposing heads.
// The decoration list also owns the posts and bases in preview and playback.
void NetworkPresenter::updateSignalCues() {
	for (QGraphicsItem* item : std::as_const(m_signalDecorations)) {
		if (!item)
			continue;
		const bool visible = m_signalLayerVisible && item->data(kSignalBaseVisibleRole).toBool();
		auto* signal = qgraphicsitem_cast<SignalItem*>(item);
		if (signal) {
			signal->setGroupedSignals({});
			if (item->data(kSignalAnchorRole).isValid())
				signal->setPos(item->data(kSignalAnchorRole).toPointF());
		}
		item->setVisible(visible);
	}
}

void NetworkPresenter::updateTrainBadges() {
	if (!m_view)
		return;
	for (auto it = m_trainBadges.cbegin(); it != m_trainBadges.cend(); ++it) {
		if (!it.value())
			continue;
		const bool promoted = m_host->isTrainOverlayPromoted(it.key());
		it.value()->setPromoted(promoted);
		it.value()->setPresentation(
			TrainBadgeItem::presentationForZoom(m_view->zoomRatio(), promoted));
	}
}

void NetworkPresenter::setHighlight(QGraphicsItem* item) {
	if (!effect) {
		effect = new HighlightEffect(Qt::blue, 1);
	}
	item->setGraphicsEffect(effect);
}

void NetworkPresenter::clearHighlight() {
	if (effect) {
		delete effect;
		effect = nullptr;
	}
}

SignalItem* NetworkPresenter::inspectedSignal() const {
	return m_inspectedSignal && m_signalDecorations.contains(m_inspectedSignal) ? m_inspectedSignal : nullptr;
}

void NetworkPresenter::setInspectedSignal(SignalItem* signal) {
	m_inspectedSignal = signal;
}

void NetworkPresenter::clearSelectedTrain() {
	trainPaxItem = nullptr;
}

void NetworkPresenter::clearSelectedPassengerIcon() {
	paxIconItem = nullptr;
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
	const auto line = m_previewLinesById.find(sceneTrackId);
	return line == m_previewLinesById.end() ? nullptr : line->second;
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

/// What the stages of showPreview() share.
struct NetworkPresenter::PreviewDrawing {
	/// The ids of the tracks that the infrastructure panel selects.
	std::set<std::string> selectedTrackIds;
	/// The line of each track that has a drawable shape, by track id, with its display offset.
	std::map<std::string, std::pair<const TrackPreviewLine*, qreal>> tracks;
	/// The same lines in the order of the preview.
	std::vector<const TrackPreviewLine*> drawnLines;
	/// The bounds of the points of the drawn lines and connections; empty until hasPreviewBounds.
	QRectF previewBounds;
	/// Whether previewBounds holds a point yet.
	bool hasPreviewBounds = false;
	/// The item of each node, by node id.
	std::map<std::string, QGraphicsItem*> nodeItems;
	/// The platform bar of each station node, by node id.
	std::map<std::string, const PlatformItem*> platformBars;
	/// The station and the platform of each node, by node id.
	std::map<std::string, PreviewStationMembership> memberships;

	/// Extends the bounds to point.
	void includePreviewPoint(const QPointF& point) {
		if (!hasPreviewBounds) {
			previewBounds = QRectF(point, point);
			hasPreviewBounds = true;
			return;
		}
		previewBounds.setLeft(qMin(previewBounds.left(), point.x()));
		previewBounds.setRight(qMax(previewBounds.right(), point.x()));
		previewBounds.setTop(qMin(previewBounds.top(), point.y()));
		previewBounds.setBottom(qMax(previewBounds.bottom(), point.y()));
	}
};

NetworkPresenter::PreviewOutcome NetworkPresenter::showPreview(const SceneModel& sceneModel, const PreviewOptions& options) {
	QGraphicsScene* const scene = m_scene;
	resetPreviewFit();
	PreviewDrawing drawing;
	drawing.selectedTrackIds = previewSelectedTracks(sceneModel, options);
	m_previewHasSelectedTrack = !drawing.selectedTrackIds.empty();
	m_cachedTrackPreview = normalizeTrackPreview(loadTrackPreview(sceneModel));
	m_previewLinesById.clear();
	for (const TrackPreviewLine& line : m_cachedTrackPreview.lines)
		m_previewLinesById.emplace(line.id, &line);
	if (m_cachedTrackPreview.lines.empty()) {
		if (scene)
			scene->setSceneRect(QRectF());
		return {};
	}

	collectPreviewLines(drawing);
	drawPreviewConnections(drawing);
	drawPreviewTrackLines(sceneModel, drawing);
	drawPreviewNodes(sceneModel, drawing);
	// Selected-only editor emphasis is separate from structural boundary marks.
	if (options.markSelection)
		markPreviewSelection(sceneModel, options, drawing);
	drawPreviewStations(sceneModel, drawing);
	measureStationTrackSpacing();
	drawPreviewSignals(sceneModel, drawing);
	fitPreview(drawing);
	// The scene queues every added item for polishing until the event loop runs, and deleting a queued item
	// leaves an entry that each later deletion searches past. Previews drawn back to back would get slower with
	// every call, so run the queued work now.
	QCoreApplication::sendPostedEvents(scene, QEvent::MetaCall);
	return {true, static_cast<int>(drawing.tracks.size()), drawing.hasPreviewBounds};
}

// The tracks of the row that the infrastructure panel selects: the track of a track, node, arc or block, the tracks of
// the two nodes of a connection, the tracks of the platform nodes of a station or a platform, and the tracks of the blocks
// of a route.
std::set<std::string> NetworkPresenter::previewSelectedTracks(const SceneModel& sceneModel, const PreviewOptions& options) {
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
	return selectedTrackIds;
}

// Takes the lines of the preview that have a drawable shape, at least two points and all of them finite, and the bounds
// of their points.
void NetworkPresenter::collectPreviewLines(PreviewDrawing& drawing) const {
	const TrackPreviewResult& preview = m_cachedTrackPreview;
	for (std::size_t index = 0; index < preview.lines.size(); ++index) {
		const auto& line = preview.lines[index];
		if (line.points.size() < 2 || std::any_of(line.points.begin(), line.points.end(), [](const auto& point) {
				return !std::isfinite(point.x) || !std::isfinite(point.y);
			}))
			continue;
		const qreal offset = static_cast<qreal>(line.displayOffset);
		drawing.tracks[line.id] = {&line, offset};
		drawing.drawnLines.push_back(&line);
		for (const auto& point : line.points)
			drawing.includePreviewPoint(QPointF(point.x, point.y + offset));
	}
}

// Draws the connections between two drawn lines, which extend the bounds.
void NetworkPresenter::drawPreviewConnections(PreviewDrawing& drawing) {
	const auto& tracks = drawing.tracks;
	for (const auto& connection : m_cachedTrackPreview.connections) {
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
		m_scene->addItem(item);
		drawing.includePreviewPoint(start);
		drawing.includePreviewPoint(end);
	}
}

// Draws the drawn lines arc by arc, in the colour and width of the speed limit of the arc, or with the selection pen on a
// selected track.
void NetworkPresenter::drawPreviewTrackLines(const SceneModel& sceneModel, const PreviewDrawing& drawing) {
	const std::set<std::string>& selectedTrackIds = drawing.selectedTrackIds;
	// The first arc of a track between two nodes, in either direction.
	std::map<std::tuple<std::string_view, std::string_view, std::string_view>, const SceneArc*> arcsByNodes;
	for (const SceneArc& arc : sceneModel.arcs) {
		const std::string_view from = arc.fromNodeId;
		const std::string_view to = arc.toNodeId;
		arcsByNodes.emplace(std::make_tuple(std::string_view(arc.trackId), std::min(from, to), std::max(from, to)), &arc);
	}
	// A run adds the connections first and the track lines after them, so a track is drawn over a connection.
	for (const TrackPreviewLine* drawn : drawing.drawnLines) {
		const auto& line = *drawn;
		const qreal offset = static_cast<qreal>(line.displayOffset);
		const bool doubleSwitch = drawnAsDoubleSwitch(sceneModel, line.id);
		for (std::size_t point = 1; point < line.points.size(); ++point) {
			if (doubleSwitch && (point == 1 || point + 1 == line.points.size()))
				continue;
			const std::string_view from = line.points[point - 1].nodeId;
			const std::string_view to = line.points[point].nodeId;
			const auto found = arcsByNodes.find(std::make_tuple(std::string_view(line.id), std::min(from, to), std::max(from, to)));
			const SceneArc* const arc = found == arcsByNodes.end() ? nullptr : found->second;
			const TrackVisual visual = classifyTrackSpeed(arc ? arc->speedLimitMs : 0.0);
			QPen pen(selectedTrackIds.count(line.id) > 0 ? kSelectionCueColor : visual.color);
			pen.setWidth(selectedTrackIds.count(line.id) > 0 ? 4 : visual.width);
			pen.setCosmetic(true);
			auto* item = new TrackLineItem(QLineF(line.points[point - 1].x, line.points[point - 1].y + offset,
				line.points[point].x, line.points[point].y + offset));
			item->setPen(pen);
			tagPreviewItem(item, arc ? "arc" : "track", arc ? arc->id : line.id);
			m_scene->addItem(item);
		}
	}
}

// Draws the node at each point of a drawn line: the square of a station node, with the bar of its platform, or the dot
// of another node.
void NetworkPresenter::drawPreviewNodes(const SceneModel& sceneModel, PreviewDrawing& drawing) {
	drawing.memberships = previewStationMemberships(sceneModel);
	const auto& memberships = drawing.memberships;
	std::map<std::string, int> connectionDegrees;
	for (const auto& connection : sceneModel.connections) {
		++connectionDegrees[connection.fromNodeId];
		++connectionDegrees[connection.toNodeId];
	}
	for (const auto& line : m_cachedTrackPreview.lines) {
		if (!drawing.tracks.count(line.id))
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
					drawing.platformBars[point.nodeId] =
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
			m_scene->addItem(item);
			drawing.nodeItems[point.nodeId] = item;
		}
	}
}

// Marks the arc or the connection that the infrastructure panel selects with a line between its two ends.
void NetworkPresenter::markPreviewSelection(const SceneModel& sceneModel, const PreviewOptions& options,
	const PreviewDrawing& drawing) {
	const std::string& selectedFacet = options.selectedFacet;
	const std::string& selectedId = options.selectedId;
	const auto& tracks = drawing.tracks;
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
			addSelectionLine(start, end);
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
			addSelectionLine(start, end);
			break;
		}
	}
}

// Adds the mark of a selected arc or connection, a line from start to end that takes no mouse button.
void NetworkPresenter::addSelectionLine(const QPointF& start, const QPointF& end) {
	QPainterPath path(start);
	path.lineTo(end);
	QPen highlight(QColor(255, 110, 90));
	highlight.setWidthF(6.0);
	highlight.setCosmetic(true);
	auto* item = m_scene->addPath(path, highlight);
	item->setAcceptedMouseButtons(Qt::NoButton);
}

// Draws the station anchors: one fixed-size marker and name per canonical station.
void NetworkPresenter::drawPreviewStations(const SceneModel& sceneModel, const PreviewDrawing& drawing) {
	const TrackPreviewResult& preview = m_cachedTrackPreview;
	const auto& tracks = drawing.tracks;
	const auto& memberships = drawing.memberships;
	const auto& nodeItems = drawing.nodeItems;
	const auto& platformBars = drawing.platformBars;
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
}

// Draws the signals of the preview, which are derived from canonical block boundaries. They stay in the existing
// decoration list so the layer toggle and teardown own them.
void NetworkPresenter::drawPreviewSignals(const SceneModel& sceneModel, const PreviewDrawing& drawing) {
	const TrackPreviewResult& preview = m_cachedTrackPreview;
	const auto& tracks = drawing.tracks;
	std::map<std::string, QStringList> signalIdsBySection;
	for (const SceneSignal& source : sceneSignals(sceneModel))
		signalIdsBySection[source.protectedSection].append(QString::fromStdString(source.id));
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

		const auto signalIds = signalIdsBySection.find(signal.sectionId);
		const QStringList canonicalIds = signalIds == signalIdsBySection.end() ? QStringList() : signalIds->second;
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
				auto* line = m_scene->addLine(stroke, white);
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
			m_scene->addItem(glyph);
			m_signalDecorations.push_back(glyph);
			glyph->setVisible(m_signalLayerVisible);
		}
	}
	m_previewHasSignals = !preview.previewSignals.empty()
		&& std::any_of(m_signalDecorations.cbegin(), m_signalDecorations.cend(),
			[](QGraphicsItem* item) { return qgraphicsitem_cast<SignalItem*>(item) != nullptr; });
}

// Fits the view to the bounds of what the preview drew, with margins, and keeps the point of the drawn lines nearest to
// the centre of the bounds as the zoom focus. Does nothing without bounds.
void NetworkPresenter::fitPreview(PreviewDrawing& drawing) {
	if (!drawing.hasPreviewBounds)
		return;
	// Fit the drawn geometry directly; the generic fitView minimum-scale clamp
	// is tuned for case-study coordinates and crops these kilometre-scale paths.
	QRectF& previewBounds = drawing.previewBounds;
	const QPointF target = previewBounds.center();
	qreal bestDistance = std::numeric_limits<qreal>::max();
	for (const auto& track : drawing.tracks) {
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
	m_scene->setSceneRect(previewBounds);
	if (m_view)
		m_view->fitToBounds(previewBounds);
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

void NetworkPresenter::bindStationOverlaySources() {
	if (!m_scene)
		return;
	QList<StationNodeItem*> stationNodes;
	for (QGraphicsItem* item : m_scene->items()) {
		auto* station = qgraphicsitem_cast<StationNodeItem*>(item);
		if (station && station->node)
			stationNodes.append(station);
	}
	QList<QList<StationNodeItem*>> assignments;
	assignments.reserve(m_stationOverlays.size());
	for (int index = 0; index < m_stationOverlays.size(); ++index)
		assignments.append(QList<StationNodeItem*>());
	for (auto* station : stationNodes) {
		const QString stationName = QString::fromStdString(station->node->stationName);
		const QPointF stationCenter = station->sceneBoundingRect().center();
		int nearestIndex = -1;
		qreal nearestDistance = std::numeric_limits<qreal>::max();
		for (int index = 0; index < m_stationOverlays.size(); ++index) {
			const auto* overlay = m_stationOverlays.at(index);
			if (!overlay || overlay->stationName() != stationName)
				continue;
			const QPointF delta = stationCenter - overlay->stableAnchor();
			const qreal distance = delta.x() * delta.x() + delta.y() * delta.y();
			bool nearer = nearestIndex < 0 || distance < nearestDistance;
			if (!nearer && distance == nearestDistance) {
				const QPointF currentAnchor = m_stationOverlays.at(nearestIndex)->stableAnchor();
				const QPointF candidateAnchor = overlay->stableAnchor();
				nearer = candidateAnchor.x() < currentAnchor.x()
					|| (candidateAnchor.x() == currentAnchor.x()
						&& candidateAnchor.y() < currentAnchor.y());
			}
			if (nearer) {
				nearestIndex = index;
				nearestDistance = distance;
			}
		}
		if (nearestIndex >= 0)
			assignments[nearestIndex].append(station);
	}

	for (int index = 0; index < m_stationOverlays.size(); ++index) {
		auto* overlay = m_stationOverlays.at(index);
		if (!overlay)
			continue;
		auto& assigned = assignments[index];
		std::sort(assigned.begin(), assigned.end(), [overlay](const auto* left, const auto* right) {
			const QPointF leftDelta = left->sceneBoundingRect().center() - overlay->stableAnchor();
			const QPointF rightDelta = right->sceneBoundingRect().center() - overlay->stableAnchor();
			const qreal leftDistance = leftDelta.x() * leftDelta.x() + leftDelta.y() * leftDelta.y();
			const qreal rightDistance = rightDelta.x() * rightDelta.x() + rightDelta.y() * rightDelta.y();
			if (leftDistance != rightDistance)
				return leftDistance < rightDistance;
			if (left->track != right->track)
				return left->track < right->track;
			return left->node->ID < right->node->ID;
		});
		QList<StationOverlayItem::SourceIdentity> identities;
		for (const auto* station : assigned)
			identities.append({station->node->ID, station->track});
		overlay->setSourceIdentities(identities);
		// The scene-sized artwork belongs to the real semantic station node.
		// It can be picked at its painted location, not at a stale screen-space
		// collision slot owned by the decorative overlay.
		if (QGraphicsPixmapItem* picture = m_stationPictures.value(overlay, nullptr)) {
			const QPointF scenePosition = picture->scenePos();
			StationNodeItem* target = assigned.isEmpty() ? nullptr : assigned.first();
			picture->setParentItem(target);
			picture->setPos(target ? target->mapFromScene(scenePosition) : scenePosition);
			if (target)
				target->setZValue(StationMarkItem::HostZValue);
			// The mark and its area are placed by the assigned station squares, and the area covers their platform bars.
			QList<QPointF> centres;
			for (const auto* station : assigned)
				centres.append(station->sceneBoundingRect().center());
			overlay->setNodeCentres(centres);
			QList<QPolygonF> bars;
			for (const auto* platform : std::as_const(allPlatforms))
				for (const auto* station : assigned)
					if (platform && platform->node == station->node)
						bars.append(platform->mapToScene(platform->rect()));
			overlay->setPlatformBars(bars);
		}
	}
	measureStationTrackSpacing();
}

void NetworkPresenter::updateStationOverlayDegrees() {
	if (!m_scene)
		return;
	const auto atEndpoint = [](const QPointF& point, const QPointF& endpoint) {
		return QLineF(point, endpoint).length() <= 1e-4;
	};
	const auto segmentDegree = [&](QGraphicsItem* item, const QLineF& line, const QPointF& point) {
		const QPointF first = item->mapToScene(line.p1());
		const QPointF second = item->mapToScene(line.p2());
		return (atEndpoint(point, first) || atEndpoint(point, second)) ? 1 : 0;
	};
	QList<StationNodeItem*> stationNodes;
	QList<QGraphicsItem*> edges;
	for (QGraphicsItem* item : m_scene->items()) {
		if (!item || !item->isVisible())
			continue;
		if (auto* station = qgraphicsitem_cast<StationNodeItem*>(item)) {
			if (station->node)
				stationNodes.append(station);
		} else if (qgraphicsitem_cast<TrackLineItem*>(item)
			|| qgraphicsitem_cast<ConnectionItem*>(item)) {
			edges.append(item);
		}
	}

	for (auto* overlay : std::as_const(m_stationOverlays)) {
		if (!overlay)
			continue;
		int highestDegree = 0;
		bool hasInterchange = false;
		bool hasEndpoint = false;
		for (auto* station : stationNodes) {
			const bool assigned = overlay->hasSourceIdentity()
				? overlay->matchesSourceIdentity(station->node->ID, station->track)
				: QString::fromStdString(station->node->stationName) == overlay->stationName();
			if (!assigned)
				continue;
			const QPointF point = station->sceneBoundingRect().center();
			int degree = 0;
			for (auto* edge : edges) {
				if (auto* track = qgraphicsitem_cast<TrackLineItem*>(edge)) {
					degree += segmentDegree(track, track->line(), point);
					if (auto* virtualArc = dynamic_cast<VirtualArcItem*>(track))
						degree += segmentDegree(virtualArc, virtualArc->secondPaintedLine(), point);
				} else if (auto* connection = qgraphicsitem_cast<ConnectionItem*>(edge)) {
					degree += segmentDegree(connection, connection->line(), point);
				}
			}
			highestDegree = std::max(highestDegree, degree);
			hasInterchange = hasInterchange || degree >= 3;
			hasEndpoint = hasEndpoint || degree == 1;
		}
		overlay->setNetworkDegree(highestDegree, hasInterchange, hasEndpoint);
	}
}

void NetworkPresenter::showRuntime(const SceneModel& sceneModel) {
	if (!m_source)
		return;
	m_runModel = &sceneModel;

	// The layout of this run, which starts from the authored levels and regions of the source.
	m_trackLevels.clear();
	for (int track = 0; track < m_source->trackCount(); ++track)
		m_trackLevels.push_back(m_source->track(track).graphID);
	m_stationLayouts.assign(static_cast<std::size_t>(m_source->stationCount()), StationLayout());

	// Reuse drawing levels once track spans stop overlapping when the scene has
	// no authored display layout.
	struct TrackSpan {
		int track;
		double begin;
		double end;
	};
	std::vector<TrackSpan> trackSpans;
	for (int track = 0; track < m_source->trackCount(); ++track) {
		const BlockSet& trackSet = m_source->track(track);
		if (trackSet.hasGraphLayout)
			continue;
		if (trackSet.len <= 0) {
			m_trackLevels[static_cast<std::size_t>(track)] = 0;
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
			m_trackLevels[static_cast<std::size_t>(span.track)] = static_cast<int>(levelEnds.size());
			levelEnds.push_back(span.end);
		} else {
			m_trackLevels[static_cast<std::size_t>(span.track)] = static_cast<int>(std::distance(levelEnds.begin(), available));
			*available = span.end;
		}
	}
	// Use a single fallback region for stations without authored view metadata.
	for (int stationIndex = 0; stationIndex < m_source->stationCount(); ++stationIndex) {
		const Stations& legacyStation = m_source->station(stationIndex);
		StationLayout& layout = m_stationLayouts[static_cast<std::size_t>(stationIndex)];
		layout.regions = legacyStation.regions;
		layout.regionX = legacyStation.regionX;
		if (layout.regions.empty()) {
			layout.regions.push_back(0);
			layout.regionX[0] = legacyStation.X;
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
		const Stations& legacyStation = m_source->station(i);
		const StationLayout& layout = m_stationLayouts[static_cast<std::size_t>(i)];
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
		pt = QPointF(layout.graphX, layout.graphY);
		QPointF averageShift;
		int validRegions = 0;
		for (int region : layout.regions) {
			const double x = regionValue(layout.shiftX, region);
			const double y = regionValue(layout.shiftY, region);
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
	for (int c = 0; c < m_source->connectionCount(); c++) {
		const Connections& connection = m_source->connection(c);
		if (hasSharedPreview && (!cachedTrackLine(connection.idFirstTrackLine) || !cachedTrackLine(connection.idSecondTrackLine)))
			continue;
		// get shifted connections
		const QLineF ends = egtrainPoint2Screen(connection, connection.idFirstTrackLine, connection.idSecondTrackLine, track_separation);
		paintConnection(ends.p1(), ends.p2(), line_width, &connection);
	}

	// draw tracklines
	QPointF pt_prev, pte;
	for (int track = 0; track < m_source->trackCount(); track++) {
		if (hasSharedPreview && !cachedTrackLine(track))
			continue;
		const BlockSet& trackSet = m_source->track(track);
		// a double switch is drawn without its first and last arc and without ordinary node dots
		const bool doubleSwitch = drawnAsDoubleSwitch(sceneModel, trackSet.sceneTrackId);
		// run over all arcs
		for (int i = 0; i < trackSet.len; i++) {
			// get shifted nodes
			pt = egtrainPoint2Screen(trackSet.member[i].startNode, track, track_separation);
			pte = egtrainPoint2Screen(trackSet.member[i].endNode, track, track_separation);

			// draw nodes
			if (i > 0 && !trackSet.member[i - 1].endNode.stationName.empty()) { // station Node (only visible from end nodes [end_node] -> i>1)
				paintStationNode(pt, station_node_size, track, &trackSet.member[i - 1].endNode, false);
				if (hasPlatformBar(trackSet.member[i - 1].endNode.stationName, trackSet.member[i - 1].endNode.stationPlatformId)) {
					paintStationPlatform(pt_prev, pt, pte, station_node_size, &trackSet.member[i - 1].endNode);
				}
			}
			// if it is a double switch then do not paint nodes Copenhagen case
			else if (doubleSwitch || trackSet.numNodes == 4) {
				paintNode(pt, kHiddenNodeSize, track, &trackSet.member[i].startNode);
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
void NetworkPresenter::paintNode(QPointF coord, int size, int track, const Node* node) {
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
void NetworkPresenter::paintStationNode(QPointF coord, int size, int track, const Node* node, bool lineEnd) {
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
void NetworkPresenter::paintStationPlatform(QPointF before, QPointF coord, QPointF after, int size, const Node* node) {
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
	// The train is selected also while it has no shape, and then shows no passengers.
	trainPaxItem = trainItem;
	QPointF front;
	if (!trainItem || !trainFront(*trainItem, front)) {
		removeTrainPaxInfoIcon();
		return;
	}
	// drawn from the front of the train, where the group is
	trainPaxInfoItem = drawCallout(m_scene, trainPaxInfoItem, kTrainPassengersCallout,
		QStringLiteral("%1 of %2 on board").arg(trainItem->currentOnboardPassengers).arg(trainItem->maxOnboardPassengers),
		presentationScale());
	trainPaxInfoItem->setPos(front);
	trainPaxInfoItem->setVisible(true);
}

// paint passenger info on top of passenger icon
void NetworkPresenter::paintPassengerInfoIcon(PassengerItem* paxItem, const GuiPassengerState& passenger, bool replayActive) {
	std::string text = "Passenger " + passenger.id + "\nStatus: " + passenger.status + " (" + passenger.waitingPlatform
		+ ")\nNext train: " + passenger.nextTrain;
	if (!passenger.nextDestination.empty())
		text += "\nNext destination: " + passenger.nextDestination;
	if (replayActive)
		text += "\nJourney not shown in the replay";

	// drawn from the top centre of the icon
	paxIconInfoItem = drawCallout(m_scene, paxIconInfoItem, kPassengerCallout, QString::fromStdString(text), presentationScale());
	paxIconInfoItem->setPos(paxItem->sceneBoundingRect().center().x(), paxItem->sceneBoundingRect().top());
	paxIconInfoItem->setVisible(true);
	paxIconItem = paxItem;
}


// draws an Arc
void NetworkPresenter::paintArc(QPointF start, QPointF end, int penWidth, int track, const Arc* arc) {
	arcDrawing(start, end, penWidth, track, arc);
}

// Arc drawing
void NetworkPresenter::arcDrawing(QPointF start, QPointF end, int penWidth, int track, const Arc* arc) {
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
void NetworkPresenter::paintConnection(QPointF start, QPointF end, int penWidth, const Connections* connection) {
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
		const StationLayout& station = stationLayout(stationIdx[1]);
		normal = QPointF(regionValue(station.signalDeltaX, m_source->track(track).region),
			regionValue(station.signalDeltaY, m_source->track(track).region));
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
		plate->updateToolTip();
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
	if (found != m_trainMotions.end()) {
		const GuiTrainState& state = found->second.newest;
		const double head = found->second.drawnHead;
		double start = 0.0;
		double end = 0.0;
		RoutePath path;
		if (routeExtent(m_source ? m_source->route(state.routeIndex) : nullptr, start, end)
			&& routePath(state.routeIndex, state.reversedDirection, std::clamp(head - state.length, start, end),
				std::clamp(head, start, end), path)) {
			QPointF sum;
			qreal drawn = 0.0;
			for (std::size_t i = 1; i < path.points.size(); ++i) {
				// Where two sections meet, the path can step from one drawn track to another at a single position.
				// That step is not part of the train.
				if (path.positions[i] <= path.positions[i - 1] + 1e-6)
					continue;
				const qreal length = path.lengths[i] - path.lengths[i - 1];
				sum += (path.points[i - 1] + path.points[i]) * (length / 2.0);
				drawn += length;
			}
			return drawn > 0.0 ? sum / drawn : path.points.back();
		}
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
	const auto stationAt = [this](int index) -> StationLayout& { return m_stationLayouts[static_cast<std::size_t>(index)]; };
	double beta; // angle between first Arc and screen x axis
	double x1, x2, x3;
	double y1, y2, y3;
	QPointF pt;

	// get station screen coordinates
	for (int i = 0; i < m_source->stationCount(); i++) { // need a separate for as the one below accesses positions ahead of i
		const Stations& station = m_source->station(i);
		if (std::fabs(station.latitude) > 1e-12 || std::fabs(station.longitude) > 1e-12) {
			pt = Coord2ScreenPoint(station.latitude, station.longitude, geo_scale);
			stationAt(i).graphX = pt.x();
			stationAt(i).graphY = pt.y();
		} else {
			// Native scenes may intentionally omit legacy geographic metadata.
			stationAt(i).graphX = station.X * 1000.0;
			stationAt(i).graphY = station.Y * 1000.0;
		}
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
	for (auto& region : m_regionStations)
		std::sort(region.begin(), region.end(), [this](int a, int b) { return m_source->station(a).X < m_source->station(b).X; });

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
				stationAt(m_regionStations[r][i]).shiftX[r] = regionValue(stationAt(m_regionStations[r][i - 1]).shiftX, r);
				stationAt(m_regionStations[r][i]).shiftY[r] = regionValue(stationAt(m_regionStations[r][i - 1]).shiftY, r);

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

				beta = atan2(-(y1 - y2), (x1 - x2));

				// calculate trackside signal deltas
				stationAt(m_regionStations[r][i]).signalDeltaX[r] = sin(beta);
				stationAt(m_regionStations[r][i]).signalDeltaY[r] = cos(beta);

				// calculate shifts
				const QPointF shift = bisectorShift(QPointF(x1, y1), QPointF(x2, y2), QPointF(x3, y3));
				stationAt(m_regionStations[r][i]).shiftX[r] = shift.x();
				stationAt(m_regionStations[r][i]).shiftY[r] = shift.y();
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
	const std::vector<int>& trackStIdx = m_regionStations[region];
	const auto regionX = [this, region](int index) { return regionValue(stationLayout(index).regionX, region); };

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

int NetworkPresenter::trackLevel(int track) const {
	if (track >= 0 && track < static_cast<int>(m_trackLevels.size()))
		return m_trackLevels[static_cast<std::size_t>(track)];
	return m_source->track(track).graphID;
}

const NetworkPresenter::StationLayout& NetworkPresenter::stationLayout(int index) const {
	static const StationLayout none;
	if (index < 0 || index >= static_cast<int>(m_stationLayouts.size()))
		return none;
	return m_stationLayouts[static_cast<std::size_t>(index)];
}

// The point at the position X of a track line, shifted to its display level: a linear interpolation of the
// shifted stations on both sides of X in the region of the track line.
void NetworkPresenter::interpolateBetweenStations(int firstIndex, int secondIndex, int region, double X, double level,
	double separation, double& graphX, double& graphY) const {
	const StationLayout& first = stationLayout(firstIndex);
	const StationLayout& second = stationLayout(secondIndex);
	// shifted stations
	const double x1 = first.graphX + regionValue(first.shiftX, region) * level * separation;
	const double x2 = second.graphX + regionValue(second.shiftX, region) * level * separation;
	const double y1 = first.graphY + regionValue(first.shiftY, region) * level * separation;
	const double y2 = second.graphY + regionValue(second.shiftY, region) * level * separation;

	// linear interpolation of geodetic coordinates using known coordinates of closest stations
	const double firstX = regionValue(first.regionX, region);
	const double secondX = regionValue(second.regionX, region);
	graphX = x1 + (((X - firstX) / (secondX - firstX)) * (x2 - x1));
	graphY = y1 + (((X - firstX) / (secondX - firstX)) * (y2 - y1));
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
		graphY = -static_cast<double>(trackLevel(track)) * separation;
		return;
	}
	double graphID = (double)trackLevel(track);

	// find index of closest stations
	int stationIdx[2];
	neighbourStations(X, track, stationIdx);

	interpolateBetweenStations(stationIdx[0], stationIdx[1], m_source->track(track).region, X, graphID, separation, graphX, graphY);
}

// get shifted screen coordinates of a Node
QPointF NetworkPresenter::egtrainPoint2Screen(const Node& node, int track, double separation) const {
	if (const auto* line = cachedTrackLine(track)) {
		TrackPreviewPoint point;
		if ((!node.sceneNodeId.empty()
				&& trackPreviewPointAtNode(*line, node.sceneNodeId, point))
			|| trackPreviewPointAtX(*line, node.X, point)) {
			return QPointF(point.x, point.y + line->displayOffset);
		}
	}
	if (!hasTrackGeometry(track))
		return QPointF(node.X * 1000.0, node.Y * 1000.0 - static_cast<double>(trackLevel(track)) * separation);
	int stationIdx[2];
	neighbourStations(node.X, track, stationIdx);
	int graphID = trackLevel(track);

	double graphX = 0.0;
	double graphY = 0.0;
	interpolateBetweenStations(stationIdx[0], stationIdx[1], m_source->track(track).region, node.X, graphID, separation, graphX,
		graphY);
	return QPointF(graphX, graphY);
}

// get shifted screen coordinates of the two ends of a connection, from the first node to the second
QLineF NetworkPresenter::egtrainPoint2Screen(const Connections& connection, int track1, int track2, double separation) const {
	if (const auto* firstLine = cachedTrackLine(track1)) {
		if (const auto* secondLine = cachedTrackLine(track2)) {
			TrackPreviewPoint first;
			TrackPreviewPoint second;
			const bool hasFirst = (!connection.sceneFirstNodeId.empty()
									  && trackPreviewPointAtNode(*firstLine, connection.sceneFirstNodeId, first))
				|| trackPreviewPointAtX(*firstLine, connection.xFirstNode, first);
			const bool hasSecond = (!connection.sceneSecondNodeId.empty()
									   && trackPreviewPointAtNode(*secondLine, connection.sceneSecondNodeId, second))
				|| trackPreviewPointAtX(*secondLine, connection.xSecondNode, second);
			if (hasFirst && hasSecond)
				return QLineF(first.x, first.y + firstLine->displayOffset, second.x, second.y + secondLine->displayOffset);
		}
	}
	if (!hasTrackGeometry(track1) || !hasTrackGeometry(track2)) {
		return QLineF(connection.xFirstNode * 1000.0, -static_cast<double>(trackLevel(track1)) * separation,
			connection.xSecondNode * 1000.0, -static_cast<double>(trackLevel(track2)) * separation);
	}
	int graphID1 = trackLevel(track1);
	int graphID2 = trackLevel(track2);

	// find index of closest stations
	int stationIdx[2];
	double firstX = 0.0;
	double firstY = 0.0;
	neighbourStations(connection.xFirstNode, track1, stationIdx);
	interpolateBetweenStations(stationIdx[0], stationIdx[1], m_source->track(track1).region, connection.xFirstNode, graphID1,
		separation, firstX, firstY);

	// find index of closest stations
	double secondX = 0.0;
	double secondY = 0.0;
	neighbourStations(connection.xSecondNode, track2, stationIdx);
	interpolateBetweenStations(stationIdx[0], stationIdx[1], m_source->track(track2).region, connection.xSecondNode, graphID2,
		separation, secondX, secondY);
	return QLineF(firstX, firstY, secondX, secondY);
}

// The ends of the connection of a section derived at a switch, whose id is "<first id>@-<x1>/@<second id>@-<x2>": the
// sections of the two tracks that it joins and the X in kilometres of the connection on each. Returns false for another
// section and when the source lacks one of the two sections. The id is read once and its ends kept in m_switchEnds.
bool NetworkPresenter::switchSectionEnds(const Section& section, const Section*& first, const Section*& second,
	double& firstX, double& secondX) const {
	auto ends = m_switchEnds.find(section.ID);
	if (ends == m_switchEnds.end()) {
		SwitchEnds fresh;
		const std::size_t index1 = section.ID.find("@-");
		const std::size_t index2 = section.ID.find("/@", index1 + 1);
		const std::size_t index3 = section.ID.find("@-", index2 + 1);
		fresh.parsed = index1 != std::string::npos && index2 != std::string::npos && index3 != std::string::npos;
		if (fresh.parsed) {
			fresh.firstX = atof(section.ID.substr(index1 + 2, index2 - index1 - 2).c_str());
			fresh.secondX = atof(section.ID.substr(index3 + 2, std::string::npos).c_str());
			const auto indexOf = [this](const std::string& id) {
				const auto found = m_sectionIndexById.find(id);
				return found == m_sectionIndexById.end() ? -1 : found->second;
			};
			fresh.first = indexOf(section.ID.substr(0, index1 + 1));
			fresh.second = indexOf(section.ID.substr(index2 + 1, index3 - index2));
		}
		ends = m_switchEnds.emplace(section.ID, fresh).first;
	}
	const SwitchEnds& read = ends->second;
	if (!read.parsed)
		return false;
	firstX = read.firstX;
	secondX = read.secondX;
	first = read.first < 0 ? nullptr : &m_source->section(read.first);
	second = read.second < 0 ? nullptr : &m_source->section(read.second);
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
		entry.second.fromMs = entry.second.toMs = entry.second.drawnMs = std::numeric_limits<qint64>::min();
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
	// beyond it: the next update draws it where its glide then is, as it draws every train that the last step did not
	// draw, and a step draws it when its glide ends. A train can be drawn further than this margin behind its newest
	// place, so one that glides into the view from beyond it shows there only at the next update, or at a pause only
	// when its glide ends.
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
		motion.drawnMs = std::numeric_limits<qint64>::min();
		TrainItemGroup* trainItem = head == motion.drawnHead ? nullptr : trainGroup(train);
		if (!trainItem)
			continue;
		const bool promoted = m_host->isTrainOverlayPromoted(train);
		if (!stops && !promoted) {
			const QRectF drawn = trainItem->sceneBoundingRect();
			if (!m_trainLayerVisible || (!around.isNull() && !drawn.isEmpty() && !around.intersects(drawn)))
				continue;
		}
		motion.drawnMs = at;
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
	std::vector<QPolygonF>& polygons = m_wagonPolygons;
	polygons.resize(static_cast<std::size_t>(std::max(wagons, 0)));
	bool changed = false;
	bool hasVisibleGeometry = false;
	for (int wagon = 0; wagon < wagons; wagon++) {
		QPolygonF& trainPolygon = polygons[static_cast<std::size_t>(wagon)];
		trainPolygon.clear();
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
	double headX, tailX, connectionX1, connectionX2;
	std::vector<double>& posX = m_wagonScratch.posX;
	std::vector<double>& routeX = m_wagonScratch.routeX;
	QPointF ptTrainEdge, ptConnection1, ptConnection2, ptBegin1, ptEnd2;
	const Section* currBS = nullptr;
	const Section* BS1 = nullptr;
	const Section* BS2 = nullptr;
	QVector<QPointF>& trainPointsUp = m_wagonScratch.up;
	QVector<QPointF>& trainPointsDown = m_wagonScratch.down;
	QVector<int>& trainPointsStIndex = m_wagonScratch.stationIndex;
	QVector<int>& trainPointsStRegion = m_wagonScratch.stationRegion;
	QVector<int>& trainPointsGraphID = m_wagonScratch.graphId;
	posX.clear();
	routeX.clear();
	trainPointsUp.clear();
	trainPointsDown.clear();
	trainPointsStIndex.clear();
	trainPointsStRegion.clear();
	trainPointsGraphID.clear();
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
		const StationLayout& station = stationLayout(index);
		trainPointsUp.push_back(QPointF(point.x()
				- regionValue(station.signalDeltaX, region) * 0.10 * track_separation,
			point.y() - regionValue(station.signalDeltaY, region) * 0.10 * track_separation));
		trainPointsDown.push_front(QPointF(point.x()
				+ regionValue(station.signalDeltaX, region) * 0.10 * track_separation,
			point.y() + regionValue(station.signalDeltaY, region) * 0.10 * track_separation));
		trainPointsStIndex.push_back(revPrevIndex);
		trainPointsStRegion.push_back(region);
		trainPointsGraphID.push_back(trackLevel(track));
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
	std::vector<int>& occupiedBS = m_wagonScratch.occupiedSections; // we already know head block sections but not the index on SeqBS
	occupiedBS.clear();

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
						trainPointsGraphID.push_back(trackLevel(section->trackLineId));
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

					// shifts on the beginning and the end of the connection
					const QPointF connectionStartShift = bisectorShift(ptBegin1, ptConnection1, ptConnection2);
					const QPointF connectionEndShift = bisectorShift(ptConnection1, ptConnection2, ptEnd2);
					double connectionStartShiftX = connectionStartShift.x();
					double connectionStartShiftY = connectionStartShift.y();
					double connectionEndShiftX = connectionEndShift.x();
					double connectionEndShiftY = connectionEndShift.y();
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
					const double beta = atan2(-(ptConnection1.y() - ptConnection2.y()), (ptConnection1.x() - ptConnection2.x()));
					double connectionDeltaX = sin(beta);
					double connectionDeltaY = cos(beta);

					// adds the ends of the connection that lie between the head and the tail of the wagon, in the order in
					// which the train runs over them
					const auto addCrossedConnectionPoints = [&]() {
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
					};

					// add connection points if it is a middle section
					// add connection points if it is the tail block sections and head already added
					if ((posX[j] == -DBL_MAX) || (posX.size() == 1 && occupiedBS[i] == tailIndex)) { // adding connection points before adding tail of the train
						addCrossedConnectionPoints();
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
						addCrossedConnectionPoints();
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

			const StationLayout& shiftedStation = stationLayout(trainPointsStIndex[i]);
			const int stationRegion = trainPointsStRegion[i];
			const double shiftX = regionValue(shiftedStation.shiftX, stationRegion);
			const double shiftY = regionValue(shiftedStation.shiftY, stationRegion);
			QPointF stationUp = QPointF(shiftedStation.graphX + shiftX * track_separation * (trainPointsGraphID[i] - 0.10),
				shiftedStation.graphY + shiftY * track_separation * (trainPointsGraphID[i] - 0.10));
			QPointF stationDown = QPointF(shiftedStation.graphX + shiftX * track_separation * (trainPointsGraphID[i] + 0.10),
				shiftedStation.graphY + shiftY * track_separation * (trainPointsGraphID[i] + 0.10));
			trainPointsUp.insert(i + added, stationUp);
			trainPointsDown.insert(i + added, stationDown);
			added++;
		}
	}

	// join Up and Down vectors to build the polygon
	trainPolygon->clear();
	trainPolygon->reserve(trainPointsUp.size() + trainPointsDown.size());
	trainPolygon->append(trainPointsUp);
	trainPolygon->append(trainPointsDown);
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
	QGraphicsItemGroup*& msgGroup = m_vcMessageItems[trainItem->index];
	msgGroup = drawCallout(m_scene, msgGroup, kVirtualCouplingCallout, QString::fromStdString(message), presentationScale());
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
	if (trainPaxInfoItem)
		paintTrainPassengerInfo(trainPaxItem);
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
	if (!paxIconInfoItem)
		return;
	// the details go with the icon of the passenger, and with the passenger when it left
	if (!paxIconItem) {
		removePaxInfoIcon();
		return;
	}
	paintPassengerInfoIcon(paxIconItem, passengerStateIn(snapshot, paxIconItem->passengerId), playback == Playback::Replay);
	paxIconInfoItem->setVisible(platformPassengersVisible());
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
				if (moves) {
					RoutePath path;
					moves = routePath(state.routeIndex, state.reversedDirection,
								middleOnRoute(state.routeIndex, last->newest.routeAxisPosition, state.length),
								middleOnRoute(state.routeIndex, state.routeAxisPosition, state.length), path)
						&& path.lengths.back() <= longestGlide();
				}
				// A train on its way that the last step drew, with no update since, stays where it is drawn, and its new
				// glide starts there at the time of that step, so that only the steps move it on screen and each step
				// moves it as far as the time since the step before. Any other train that goes on, as one that stands
				// or one that the last step did not draw, because it was far from the view or because no step ran
				// since the last update, goes on from where its glide is now.
				const bool held = moves && last->drawnHead != last->newest.routeAxisPosition
					&& last->drawnMs != std::numeric_limits<qint64>::min();
				const qint64 from = held ? last->drawnMs : now;
				double drawnHead = moves ? glideHead(*last, now) : state.routeAxisPosition;
				if (held)
					drawnHead = last->drawnHead;
				TrainMotion& motion = m_trainMotions[train];
				// An update that leaves the train at its place keeps the glide that runs, so that a train that stops
				// reaches its place when that glide ends and then stands still.
				if (moves && state.routeAxisPosition == motion.newest.routeAxisPosition)
					startGlide(motion, state, motion.fromMs, motion.fromHead, motion.toMs - motion.fromMs);
				else
					startGlide(motion, state, from, drawnHead, moves ? glideDurationMs() : 0);
				motion.drawnMs = std::numeric_limits<qint64>::min();
				const bool hasVisibleGeometry = drawTrain(*trainItem, motion, drawnHead);
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
		if (it->trainDescription == train.description && t >= it->timestep && t <= it->timestep + kVirtualCouplingMessageSeconds) {
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
	m_switchEnds.clear();
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
	// The same arcs have the same X at both ends, so a section arc is compared only with the arcs with its ends, in the
	// order of allArcs. An end at NaN is equal to none.
	const auto hasEnds = [](const Arc& arc) { return !std::isnan(arc.startNode.X) && !std::isnan(arc.endNode.X); };
	std::map<std::pair<double, double>, std::vector<TrackLineItem*>> arcsByEnds;
	for (auto* item : allArcs)
		if (item && item->arc && hasEnds(*item->arc))
			arcsByEnds[{item->arc->startNode.X, item->arc->endNode.X}].push_back(item);
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
			const auto candidates = hasEnds(sectionArc) ? arcsByEnds.find({sectionArc.startNode.X, sectionArc.endNode.X})
														: arcsByEnds.end();
			if (candidates == arcsByEnds.end())
				continue;
			for (auto* item : candidates->second) {
				if (!sameArc(*item->arc, sectionArc))
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
