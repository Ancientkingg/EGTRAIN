#include "graphics/NetworkPresenter.h"

#include "graphics/SignalItemRoles.h"

#ifdef signals
#define EGTRAIN_RESTORE_SIGNALS_KEYWORD
#undef signals
#endif
#include "scene/SceneModel.h"
#ifdef EGTRAIN_RESTORE_SIGNALS_KEYWORD
#define signals Q_SIGNALS
#undef EGTRAIN_RESTORE_SIGNALS_KEYWORD
#endif

#include "graphics/AnnotationPlacement.h"
#include "graphics/NetworkScene.h"
#include "graphics/NetworkView.h"
#include "graphics/SignalGeometry.h"
#include "graphics/VisualPolish.h"
#include "graphics/items/ConnectionItem.h"
#include "graphics/items/NodeItem.h"
#include "graphics/items/PlatformItem.h"
#include "graphics/items/SignalItem.h"
#include "graphics/items/StationNodeItem.h"
#include "graphics/items/StationOverlayItem.h"
#include "graphics/items/TrackLineItem.h"
#include "scene/SceneReferences.h"

#include <QFont>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QIcon>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace {
void tagPreviewItem(QGraphicsItem* item, const QString& kind, const std::string& id) {
	item->setData(PreviewGraphics::Kind, kind);
	item->setData(PreviewGraphics::Id, QString::fromStdString(id));
}
} // namespace

NetworkPresenter::NetworkPresenter(QGraphicsScene* scene, NetworkView* view) : m_scene(scene), m_view(view) {}

void NetworkPresenter::clear() {
	m_inspectedSignal = nullptr;
	if (m_scene)
		m_scene->clear();

	// The scene has deleted the items, so every pointer to one is dropped.
	m_trainBadges.clear();
	m_prevTrainPositions.clear();
	allTrains.clear();
	allSignals.clear();
	m_signalDecorations.clear();
	m_stationOverlays.clear();
	m_vcMessageItems.clear();
	m_stationDecorations.clear();
	m_stationLabels.clear();
	m_stationPictures.clear();
	m_signalsByAheadId.clear();
	allArcs.clear();
	allPlatforms.clear();
	m_tracksBySectionId.clear();
	m_tracksByOccupiedArc.clear();
	m_activeTrackItems.clear();
	trainPaxInfoItem = nullptr;
	trainPaxItem = nullptr;
	paxIconInfoItem = nullptr;
	paxIconItem = nullptr;
	effect = nullptr;
}

void NetworkPresenter::resetPreviewFit() {
	m_previewFitBounds = QRectF();
	m_previewHasSelectedTrack = false;
	m_previewHasSignals = false;
}

const TrackPreviewLine* NetworkPresenter::trackLine(const std::string& sceneTrackId) const {
	const auto line = std::find_if(m_cachedTrackPreview.lines.begin(),
		m_cachedTrackPreview.lines.end(), [&sceneTrackId](const TrackPreviewLine& candidate) {
			return candidate.id == sceneTrackId;
		});
	return line == m_cachedTrackPreview.lines.end() ? nullptr : &*line;
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
		for (std::size_t point = 1; point < line.points.size(); ++point) {
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
	for (const auto& line : preview.lines) {
		if (!tracks.count(line.id))
			continue;
		for (std::size_t index = 0; index < line.points.size(); ++index) {
			const auto& point = line.points[index];
			const auto membership = memberships.find(point.nodeId);
			// Current runnable-chain presentation: first point is ordinary;
			// four-node ordinary starts paint zero size, but the terminal dot remains.
			const bool station = index > 0 && membership != memberships.end();
			const qreal size = (station ? station_node_size
										: (line.points.size() == 4 && index + 1 < line.points.size() ? 0 : node_size))
				* presentationScale();
			QRectF rect(0, 0, size, size);
			rect.moveCenter(QPointF(point.x, point.y + line.displayOffset));
			QAbstractGraphicsShapeItem* item;
			if (station) {
				auto* square = new StationNodeItem(rect);
				const auto& value = membership->second;
				// A platform darkens the square only at the end of the line; an interior one is the ordinary grey.
				const bool lineEnd = index + 1 == line.points.size();
				const StationVisual visual = classifyStation(lineEnd && !value.platformId.empty()
						&& value.platformId != "None",
					connectionDegrees[point.nodeId]);
				square->setPen(QPen(visual.outline, 0));
				square->setBrush(visual.fill);
				item = square;
				tagPreviewItem(item, "station-node", point.nodeId);
				item->setData(PreviewGraphics::StationId, QString::fromStdString(value.stationId));
				item->setData(PreviewGraphics::PlatformId, QString::fromStdString(value.platformId));
				if (options.platformBars) {
					QRectF barRect(0, 0, 5 * size, 0.9 * size);
					barRect.moveCenter(rect.center() - QPointF(0, 1.15 * size));
					auto* bar = new PlatformItem(barRect, square);
					bar->setPen(QPen(Qt::white, 0));
					bar->setBrush(Qt::white);
					m_stationDecorations.push_back(bar);
					bar->setVisible(m_stationLayerVisible);
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
			paintStationOverlay(anchor, classifyStation(), station.name, presentationScale(),
				stationDecorationOffset(sceneModel.stationViews, station.id, station.name) * presentationScale(),
				sceneStation == sceneModel.stations.cend() ? 0 : static_cast<int>(sceneStation->platforms.size()));
			auto* overlay = m_stationOverlays.back();
			tagPreviewItem(overlay, "station", station.id);
			const auto bind = [&](QGraphicsItem* decoration, bool artwork) {
				if (artwork) tagPreviewItem(decoration, "station", station.id);
				const auto node = nodeItems.find(station.nodeId);
				if (node != nodeItems.end()) {
					const QPointF position = decoration->scenePos();
					decoration->setParentItem(node->second);
					decoration->setPos(node->second->mapFromScene(position));
				}
			};
			if (auto* picture = m_stationPictures.value(overlay, nullptr))
				bind(picture, true);
			bind(m_stationLabels.back(), false);
			break;
		}
	}

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
			glyph->setInspectionIdentity(QStringLiteral("Track %1 at %2 km; section %3%4")
					.arg(QString::fromStdString(signal.trackId))
					.arg(signal.rawX, 0, 'g', 10)
					.arg(QString::fromStdString(signal.sectionId))
					.arg(canonicalIds.isEmpty() ? QString() : QStringLiteral("; signal ID %1").arg(canonicalIds.join(", "))));
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
	const int symbolSize = station_size;
	const QPixmap symbol = QIcon(visual.iconResource).pixmap(symbolSize, symbolSize);
	if (!symbol.isNull()) {
		auto* picture = scene->addPixmap(symbol);
		picture->setScale(scale);
		picture->setPos(artworkCoord.x() - symbolSize * scale / 2.0,
			artworkCoord.y() - (station_size + symbolSize) * scale / 2.0);
		picture->setTransformationMode(Qt::SmoothTransformation);
		picture->setAcceptedMouseButtons(Qt::NoButton);
		m_stationDecorations.push_back(picture);
		m_stationPictures.insert(overlay, picture);
		picture->setVisible(m_stationLayerVisible);
	}
	QFont font;
	font.setPixelSize(station_size / 5);
	auto* label = scene->addText(StationOverlayItem::displayName(sname), font);
	label->setDefaultTextColor(Qt::white);
	label->setScale(scale);
	label->setZValue(3);
	label->setPos(artworkCoord - QPointF(label->boundingRect().width() * scale / 2.0, label->boundingRect().height() * scale / 2.0));
	label->setAcceptedMouseButtons(Qt::NoButton);
	m_stationLabels.push_back(label);
	label->setVisible(m_stationLayerVisible && m_stationNamesVisible);
	overlay->attachArtwork(m_stationPictures.value(overlay, nullptr), label, platformCount);
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
