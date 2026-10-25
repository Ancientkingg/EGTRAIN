#include "tests/e2e/MainWindowTestAccess.h"

#include "graphics/SignalItemRoles.h"

#include "graphics/NetworkScene.h"
#include "graphics/NetworkView.h"
#include "graphics/VisualPolish.h"
#include "graphics/items/SelectionCueItem.h"
#include "graphics/items/SignalItem.h"
#include "graphics/items/StationNodeItem.h"
#include "graphics/items/StationOverlayItem.h"
#include "simulation/Infrastructure.h"
#include "simulation/SimulationWorker.h"
#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QEvent>
#include <QGraphicsItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsTextItem>
#include <QHash>
#include <QImage>
#include <QLineF>
#include <QList>
#include <QMenu>
#include <QObject>
#include <QPainterPath>
#include <QPoint>
#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QSet>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QTransform>
#include <QWindow>
#include <algorithm>
#include <cstdio>
#include <limits>
#include <utility>


// Checks the station artwork and names of the open scene at Fit, 3x and 12x: their size on screen, their
// anchors, collisions between names, the hit target of the artwork, the name layer toggle and the name of a
// selected station. It waits every 500 ms, at most 120 times, for the scene and its station items, and with
// QEGTRAIN_AUTOSTART for the first snapshot. QEGTRAIN_E2E_STATION_SCREENSHOT_BASE names the files for pictures
// of the view. It prints E2E_STATION_OVERLAY_OK and exits with 0, or prints the failures and exits with 2.
void MainWindowTestAccess::runStationOverlay(MainWindow& w) {
	if (scenarioStarted)
		return;
	const bool waitingForAutostart = qEnvironmentVariableIsSet("QEGTRAIN_AUTOSTART") && !w.m_snapshot;
	if ((!w.m_document->isLoaded() || !w.networkView || w.m_presenter->m_stationOverlays.isEmpty() || waitingForAutostart)
		&& scenarioAttempts < 120) {
		++scenarioAttempts;
		QTimer::singleShot(500, &w, [&w] { MainWindowTestAccess::runStationOverlay(w); });
		return;
	}
	scenarioStarted = true;
	QStringList failures;
	const auto marker = [](const QString& text) {
		std::fprintf(stdout, "%s\n", text.toStdString().c_str());
		std::fflush(stdout);
	};
	const auto finish = [&w](int code) {
		if (w.m_runController->m_worker && w.m_runController->m_workerThread && w.m_runController->m_workerThread->isRunning()) {
			QObject::connect(w.m_runController->m_workerThread, &QThread::finished, qApp, [code]() { QCoreApplication::exit(code); });
			w.m_runController->m_worker->requestStop();
		} else {
			QCoreApplication::exit(code);
		}
	};
	if (!w.scene || !w.networkView || w.m_presenter->m_stationOverlays.isEmpty()) {
		failures << "station renderer scene is empty";
	} else {
		w.resize(1200, 800);
		QApplication::processEvents();
		const QString caseName = QString::fromStdString(w.m_document->mutableModel().name);
		const QString screenshotBase = qEnvironmentVariable("QEGTRAIN_E2E_STATION_SCREENSHOT_BASE");
		if (caseName == QLatin1String("Copenhagen")) {
			const auto kb = std::find_if(w.m_presenter->m_stationOverlays.cbegin(), w.m_presenter->m_stationOverlays.cend(),
				[](const StationOverlayItem* station) { return station && station->stationName() == QLatin1String("KBHallen"); });
			if (kb == w.m_presenter->m_stationOverlays.cend() || (*kb)->displayName() != QLatin1String("KB Hallen")
				|| (*kb)->sourceIdentityCount() != 2)
				failures << "Copenhagen station name or source identity changed";
			else {
				marker("E2E_STATION_DISPLAY_KBHALLEN_OK");
				marker("E2E_STATION_BINDING_KBHALLEN_OK");
			}
		}
		// Independent case evidence, not a per-case product multiplier.
		const qreal expectedScale = caseName == "Paimpol" ? 0.45
														  : (caseName == "Netherlands" ? 0.444237158416
																					   : (caseName == "Copenhagen" ? 0.397828421659 : w.m_presenter->presentationScale()));
		if (qAbs(w.m_presenter->presentationScale() - expectedScale) > 1e-6)
			failures << "authored projection presentation factor mismatch";
		marker(
			QString("E2E_PRESENTATION_UNITS_%1_S_%2_K_%3")
				.arg(caseName)
				.arg(w.m_presenter->m_cachedTrackPreview.normalizationScale, 0, 'f', 6)
				.arg(w.m_presenter->presentationScale(), 0, 'f', 9));
		const QRectF topologyBounds = w.networkView->topologyBounds();
		const auto pictureNativeSize = [](const QGraphicsPixmapItem* picture) {
			return QSizeF(picture->pixmap().size()) / picture->pixmap().devicePixelRatio();
		};
		const auto pictureDeviceHeight = [&](const QGraphicsPixmapItem* picture) {
			return w.networkView->viewportTransform().mapRect(picture->mapRectToScene(
																  QRectF(picture->offset(), pictureNativeSize(picture))))
				.height();
		};
		const auto pictureAnchor = [&](const QGraphicsPixmapItem* picture) {
			const QSizeF size = pictureNativeSize(picture);
			return picture->mapToScene(picture->offset() + QPointF(size.width() / 2.0, size.height()));
		};
		const auto nameDeviceFontPixels = [&](const QGraphicsTextItem* name) {
			return name->font().pixelSize() * name->sceneTransform().m22()
				* qAbs(w.networkView->viewportTransform().m22());
		};
		const auto nameDeviceRect = [&](const QGraphicsTextItem* name) {
			return w.networkView->viewportTransform().mapRect(name->sceneBoundingRect());
		};
		const auto nameAnchor = [](const QGraphicsTextItem* name) {
			return name->mapToScene(QPointF(name->boundingRect().width() / 2.0, 0.0));
		};
		const qreal minPictureHeight = StationOverlayItem::MinPictogramPixels - 0.5;
		const qreal minNameFont = StationOverlayItem::MinNamePixels - 0.5;
		// The box of the node centres of a station mark.
		const auto centresBox = [](const QList<QPointF>& centres) {
			QRectF box(centres.first(), centres.first());
			for (const QPointF& centre : centres)
				box = QRectF(QPointF(std::min(box.left(), centre.x()), std::min(box.top(), centre.y())),
					QPointF(std::max(box.right(), centre.x()), std::max(box.bottom(), centre.y())));
			return box;
		};
		// The top edge at x of the convex hull of the node centres of a station mark: the smallest y over the pairs of
		// centres around x, where a pair of one x counts at that x.
		const auto hullTop = [](const QList<QPointF>& centres, qreal x) {
			qreal top = std::numeric_limits<qreal>::infinity();
			for (const QPointF& a : centres)
				for (const QPointF& b : centres) {
					const bool upright = qAbs(a.x() - b.x()) < 1e-9;
					if (upright ? qAbs(a.x() - x) < 1e-6 : std::min(a.x(), b.x()) <= x && x <= std::max(a.x(), b.x()))
						top = std::min(top, upright ? std::min(a.y(), b.y()) : a.y() + (b.y() - a.y()) * (x - a.x()) / (b.x() - a.x()));
				}
			return top;
		};
		// The node centres of a station mark, or its anchor, which places a mark without squares at its place.
		const auto placeOf = [](const StationOverlayItem* overlay, const StationMarkItem* mark) {
			return mark->nodeCentres().isEmpty() ? QList<QPointF>{overlay->stableAnchor()} : mark->nodeCentres();
		};
		// The x of a station mark and of a name centred on it: the centre of the footprint of its squares, whose centre does
		// not depend on the size of the frame.
		const auto markX = [&](const StationOverlayItem* overlay, const StationMarkItem* mark) {
			return stationFootprintRect(placeOf(overlay, mark), mark->stationClass(), 1.0).center().x();
		};
		QHash<const StationOverlayItem*, QPointF> pictureAnchors, nameAnchors;
		bool minimumOk = true, anchorsOk = true, collisionOk = true, nearEmpty = true;
		// The run gives each station mark the centres of the squares of its nodes at its place.
		bool centred = true;
		for (const StationOverlayItem* overlay : std::as_const(w.m_presenter->m_stationOverlays)) {
			const auto* mark = overlay ? dynamic_cast<const StationMarkItem*>(overlay->pictureItem()) : nullptr;
			const QList<QPointF> centres = mark ? mark->nodeCentres() : QList<QPointF>();
			centred = centred && (!overlay || (mark && !centres.isEmpty()));
			for (const QPointF& centre : centres) {
				bool found = false;
				for (const QGraphicsItem* item : w.scene->items(centre)) {
					const auto* square = qgraphicsitem_cast<const StationNodeItem*>(item);
					found = found
						|| (square && square->node && overlay->matchesSourceIdentity(square->node->ID, square->track)
							&& square->sceneBoundingRect().center() == centre);
				}
				centred = centred && found;
			}
		}
		if (!centred)
			failures << "a station mark lacks the centres of its station squares";
		// The selection ring of a station square of the run holds the mark of its station only where the square is at the
		// place of the mark.
		bool ringed = true;
		for (const QGraphicsItem* item : w.scene->items()) {
			const auto* square = qgraphicsitem_cast<const StationNodeItem*>(item);
			if (!square || !square->node)
				continue;
			for (const StationOverlayItem* overlay : std::as_const(w.m_presenter->m_stationOverlays)) {
				const auto* mark = overlay ? dynamic_cast<const StationMarkItem*>(overlay->pictureItem()) : nullptr;
				if (!mark || !mark->isVisible() || mark->parentItem() == square
					|| !overlay->matchesSourceIdentity(square->node->ID, square->track))
					continue;
				const bool atPlace = mark->nodeCentres().contains(square->sceneBoundingRect().center());
				ringed = ringed
					&& SelectionCueItem(square).targetRect().contains(mark->sceneBoundingRect()) == (atPlace && !mark->isNearLook());
			}
		}
		if (!ringed)
			failures << "the selection ring of a station square holds a mark that is not at its place";
		// The detail captures are centred on the station with most platforms.
		const auto detailStation = std::max_element(w.m_presenter->m_stationOverlays.cbegin(), w.m_presenter->m_stationOverlays.cend(),
			[](const StationOverlayItem* left, const StationOverlayItem* right) {
				return (left ? left->platformCount() : -1) < (right ? right->platformCount() : -1);
			});
		for (const auto& zoom : {std::make_pair(1.0, "FIT"), std::make_pair(3.0, "3X"),
				 std::make_pair(12.0, "12X")}) {
			w.fitView();
			if (zoom.first > 1.0) {
				w.networkView->zoomBy(zoom.first);
				if (detailStation != w.m_presenter->m_stationOverlays.cend() && *detailStation)
					w.networkView->centerOn((*detailStation)->stableAnchor());
			}
			w.updateViewportOverlays();
			QApplication::processEvents();
			if (qAbs(w.networkView->zoomRatio() - zoom.first) > 1e-5
				|| w.networkView->topologyBounds() != topologyBounds)
				failures << QString("%1 changed zoom or topology bounds").arg(zoom.second);
			bool foundPicture = false;
			int pictures = 0, pins = 0, namedStations = 0, shownNames = 0, hiddenNames = 0;
			QList<QRectF> shownRects;
			for (const StationOverlayItem* overlay : std::as_const(w.m_presenter->m_stationOverlays)) {
				const QGraphicsPixmapItem* picture = overlay->pictureItem();
				const auto* mark = dynamic_cast<const StationMarkItem*>(picture);
				const bool nearLook = mark && mark->isNearLook();
				if (picture && picture->isVisible()) {
					++pictures;
					// The mark is sized by its frame in both looks, and the near look draws nothing.
					const qreal viewScale = qAbs(w.networkView->transform().m11());
					const qreal height = pictureDeviceHeight(picture);
					const qreal sceneHeight = pictureNativeSize(picture).height() * w.m_presenter->presentationScale() * viewScale;
					if (height > qMax(minPictureHeight + 1.0, sceneHeight) + 0.5)
						minimumOk = false;
					if (nearLook) {
						if (!mark->boundingRect().isEmpty() || !mark->shape().isEmpty())
							nearEmpty = false;
					} else {
						++pins;
						if (height < minPictureHeight)
							minimumOk = false;
					}
					if (!foundPicture) {
						foundPicture = true;
						if (picture->pixmap().width() != w.m_presenter->station_size
							|| !qgraphicsitem_cast<StationNodeItem*>(picture->parentItem()))
							failures << QString("%1 artwork lost its station identity").arg(zoom.second);
					}
					// The mark keeps its place in both looks: its frame stands on the top edge of the hull of the station
					// nodes at the middle of their footprint.
					const QPointF anchor = pictureAnchor(picture);
					if (!pictureAnchors.contains(overlay))
						pictureAnchors.insert(overlay, anchor);
					else if (QLineF(pictureAnchors.value(overlay), anchor).length() > 1e-6)
						anchorsOk = false;
					const qreal x = mark ? markX(overlay, mark) : 0.0;
					if (mark && QLineF(anchor, QPointF(x, hullTop(placeOf(overlay, mark), x))).length() > 1e-6)
						anchorsOk = false;
				}
				const QGraphicsTextItem* name = overlay->nameItem();
				if (!name)
					continue;
				++namedStations;
				const QPointF anchor = nameAnchor(name);
				if (mark) {
					// The name of a mark keeps its x under the squares or above the mark, or lies beside the squares at
					// their height.
					const QRectF box = centresBox(placeOf(overlay, mark));
					const QRectF rect = name->sceneBoundingRect();
					if (qAbs(anchor.x() - markX(overlay, mark)) > 1e-6
						&& !((rect.left() > box.right() || rect.right() < box.left()) && rect.top() < box.center().y()
							&& rect.bottom() > box.center().y()))
						anchorsOk = false;
				} else if (!nameAnchors.contains(overlay)) {
					nameAnchors.insert(overlay, anchor);
				} else if (QLineF(nameAnchors.value(overlay), anchor).length() > 1e-6) {
					anchorsOk = false;
				}
				if (!name->isVisible()) {
					++hiddenNames;
					if (w.m_presenter->m_stationLayerVisible && w.m_presenter->m_stationNamesVisible && !overlay->isNameHiddenByCollision())
						collisionOk = false;
					continue;
				}
				++shownNames;
				if (nameDeviceFontPixels(name) < minNameFont)
					minimumOk = false;
				const QRectF rect = nameDeviceRect(name);
				for (const QRectF& other : std::as_const(shownRects))
					if (rect.intersects(other))
						collisionOk = false;
				shownRects.append(rect);
			}
			// A name that is shown does not cover the pictogram of another station.
			for (const StationOverlayItem* overlay : std::as_const(w.m_presenter->m_stationOverlays)) {
				const QGraphicsTextItem* name = overlay->nameItem();
				if (!name || !name->isVisible() || overlay->isSelected() || overlay->isFollowed())
					continue;
				const QRectF nameRect = nameDeviceRect(name);
				for (const StationOverlayItem* other : std::as_const(w.m_presenter->m_stationOverlays)) {
					const QGraphicsPixmapItem* picture = other != overlay ? other->pictureItem() : nullptr;
					if (!picture || !picture->isVisible())
						continue;
					const QRectF shared = nameRect.intersected(
						w.networkView->viewportTransform().mapRect(picture->sceneBoundingRect()));
					if (shared.width() > 2.0 && shared.height() > 2.0)
						collisionOk = false;
				}
			}
			if (!foundPicture || w.m_presenter->m_stationLabels.size() != w.m_presenter->m_stationOverlays.size())
				failures << QString("%1 has no scene-scaled station picture/name").arg(zoom.second);
			if (pictures != w.m_presenter->m_stationPictures.size())
				failures << QString("%1 hides a station pictogram").arg(zoom.second);
			if (namedStations > 0 && shownNames < 1)
				failures << QString("%1 shows no station name").arg(zoom.second);
			if (caseName == QLatin1String("Copenhagen") && zoom.first == 1.0 && hiddenNames < 1)
				failures << "Copenhagen at Fit hides no colliding station name";
			if (!screenshotBase.isEmpty()) {
				const QString suffix = QString::fromLatin1(zoom.second).toLower();
				if (!w.networkView->grab().save(QString("%1-%2.png").arg(screenshotBase, suffix)))
					failures << QString("%1 station screenshot failed").arg(zoom.second);
			}
			marker(QString("E2E_STATION_NAMES_%1_%2 shown=%3 hidden=%4 pictograms=%5 pins=%6")
					.arg(caseName, zoom.second)
					.arg(shownNames)
					.arg(hiddenNames)
					.arg(pictures)
					.arg(pins));
			marker(QString("E2E_STATION_OVERLAY_%1_%2_OK").arg(caseName, zoom.second));
		}
		if (!minimumOk)
			failures << "a station pictogram or name is smaller than its minimum size on screen";
		else
			marker("E2E_STATION_MIN_SIZE_OK");
		if (!anchorsOk)
			failures << "station artwork anchor moved with the zoom";
		else
			marker("E2E_STATION_ANCHOR_STABLE_OK");
		if (!collisionOk)
			failures << "station names overlap, cover a pictogram or are hidden without a collision";
		else
			marker("E2E_STATION_NAME_COLLISION_OK");
		if (!nearEmpty)
			failures << "a station mark of the near look has bounds or a shape";
		// Far above Fit the scene size exceeds the minimum, so the artwork keeps its scene scale.
		w.fitView();
		w.networkView->zoomBy(100.0);
		w.updateViewportOverlays();
		QApplication::processEvents();
		// The marks are in the near look there, and the marks and the names only move.
		bool sceneSized = !w.m_presenter->m_stationPictures.isEmpty();
		for (const StationOverlayItem* overlay : std::as_const(w.m_presenter->m_stationOverlays)) {
			const QGraphicsPixmapItem* picture = overlay->pictureItem();
			const auto* mark = dynamic_cast<const StationMarkItem*>(picture);
			const QGraphicsTextItem* name = overlay->nameItem();
			if ((picture && (!mark || !mark->isNearLook() || picture->transform().type() > QTransform::TxTranslate))
				|| (name && name->transform().type() > QTransform::TxTranslate))
				sceneSized = false;
		}
		if (!sceneSized)
			failures << "station artwork is enlarged although its scene size is above the minimum";
		else
			marker("E2E_STATION_SCENE_SIZE_OK");
		w.fitView();
		w.networkView->zoomBy(12.0);
		w.updateViewportOverlays();
		QApplication::processEvents();
		marker(QString("E2E_STATION_OVERLAY_DPR_%1").arg(w.windowHandle() ? w.windowHandle()->devicePixelRatio() : 1.0, 0, 'f', 1));
		if (caseName == QLatin1String("Netherlands")) {
			int independent = 0;
			for (auto* decoration : w.m_presenter->m_signalDecorations) {
				auto* signal = decoration ? qgraphicsitem_cast<SignalItem*>(decoration) : nullptr;
				if (!signal || !signal->isVisible())
					continue;
				++independent;
				if (signal->groupedSignalCount() != 0
					|| signal->scenePos() != signal->data(kSignalAnchorRole).toPointF()
					|| signal->toolTip().isEmpty())
					failures << "Netherlands signal lost independent aspect or anchor";
			}
			if (independent < 30)
				failures << "Netherlands signal fixture too sparse";
			else
				marker(QString("E2E_NETHERLANDS_SIGNALS_INDIVIDUAL_OK heads=%1").arg(independent));
		}
		bool exactPicture = false;
		for (auto it = w.m_presenter->m_stationPictures.cbegin(); it != w.m_presenter->m_stationPictures.cend(); ++it) {
			auto* picture = it.value();
			auto* target = picture ? qgraphicsitem_cast<StationNodeItem*>(picture->parentItem()) : nullptr;
			if (!target || !target->node || !picture->isVisible())
				continue;
			// The far mark is picked on its pin, which covers the centre of its frame. The near mark draws nothing and is
			// never picked, so its square is picked at its centre.
			const auto* mark = dynamic_cast<const StationMarkItem*>(picture);
			const bool nearLook = mark && mark->isNearLook();
			const QPointF point = nearLook ? target->sceneBoundingRect().center()
										   : picture->mapToScene(QRectF(picture->offset(), pictureNativeSize(picture)).center());
			const QList<QGraphicsItem*> hits = w.scene->items(point, Qt::IntersectsItemShape, Qt::DescendingOrder,
				w.networkView->viewportTransform());
			exactPicture = nearLook ? hits.contains(target) && !hits.contains(picture)
									: std::any_of(hits.cbegin(), hits.cend(), [&](const QGraphicsItem* hit) {
										  return hit == picture || (hit && hit->parentItem() == target);
									  });
			if (exactPicture)
				break;
		}
		if (!exactPicture)
			failures << "station picture no longer resolves its painted hit target";
		else
			marker("E2E_STATION_ARTWORK_CLICK_OK");
		// An obsolete displaced slot has no shape and cannot override context.
		auto* ghost = new StationOverlayItem("Ghost", w.networkView->mapToScene(w.networkView->viewport()->rect().center()), classifyStation());
		ghost->setSceneDecoration(true);
		Node ghostSource;
		ghostSource.ID = -987.0;
		ghostSource.station = true;
		auto* ghostStation = new StationNodeItem(QRectF(-5, -5, 10, 10));
		ghostStation->track = -987;
		ghostStation->node = &ghostSource;
		ghostStation->setPos(ghost->stableAnchor());
		ghost->setSourceIdentities({{-987.0, -987}});
		w.scene->addItem(ghostStation);
		w.scene->addItem(ghost);
		w.m_presenter->m_stationOverlays.append(ghost);
		const QPointF oldSlot = ghost->stableAnchor()
			+ w.networkView->viewportTransform().inverted().map(QPointF(60.0, 0.0))
			- w.networkView->viewportTransform().inverted().map(QPointF());
		QGraphicsItem* contextTarget = nullptr;
		const auto observer = QObject::connect(w.scene, &NetworkScene::ContextMenuRequested, &w,
			[&](QGraphicsItem* item, const QPointF&, const QPoint&, bool) { contextTarget = item; });
		QGraphicsSceneContextMenuEvent context(QEvent::GraphicsSceneContextMenu);
		context.setReason(QGraphicsSceneContextMenuEvent::Mouse);
		context.setScenePos(oldSlot);
		context.setScreenPos(w.networkView->viewport()->mapToGlobal(w.networkView->mapFromScene(oldSlot)));
		context.setWidget(w.networkView->viewport());
		w.scene->contextMenuEvent(&context);
		QObject::disconnect(observer);
		if (!ghost->shape().isEmpty() || w.scene->items(oldSlot, Qt::IntersectsItemShape, Qt::DescendingOrder, w.networkView->viewportTransform()).contains(ghost)
			|| contextTarget == ghostStation || (w.m_sceneContextMenu && w.m_sceneContextMenu->title() == QLatin1String("Station")))
			failures << "old screen-sized station collision slot overrides context";
		else
			marker("E2E_STATION_NO_GHOST_CONTEXT_OK");
		if (w.m_sceneContextMenu) {
			w.m_sceneContextMenu->close();
			w.m_sceneContextMenu.clear();
		}
		w.m_presenter->m_stationOverlays.removeAll(ghost);
		w.scene->removeItem(ghost);
		delete ghost;
		w.scene->removeItem(ghostStation);
		delete ghostStation;
		marker("E2E_STATION_MULTI_SOURCE_BINDING_OK");
		const auto shownNames = [&w]() {
			QSet<const StationOverlayItem*> stations;
			for (const StationOverlayItem* overlay : std::as_const(w.m_presenter->m_stationOverlays))
				if (overlay->nameItem() && overlay->nameItem()->isVisible())
					stations.insert(overlay);
			return stations;
		};
		// The run can end while the events below are handled. The results then appear and resize the view, and other
		// names collide. The names before and after the toggle are compared only for one view, and taken again otherwise.
		QSet<const StationOverlayItem*> shownNameStations;
		QSet<const StationOverlayItem*> restoredNameStations;
		bool namesHidden = false;
		for (int attempt = 0; attempt < 3; ++attempt) {
			w.fitView();
			w.updateViewportOverlays();
			QApplication::processEvents();
			const QSize viewSize = w.networkView->viewport()->size();
			const QTransform viewTransform = w.networkView->viewportTransform();
			shownNameStations = shownNames();
			w.m_stationNamesCheck->setChecked(false);
			QApplication::processEvents();
			namesHidden = shownNames().isEmpty();
			w.m_stationNamesCheck->setChecked(true);
			QApplication::processEvents();
			restoredNameStations = shownNames();
			if (w.networkView->viewport()->size() == viewSize && w.networkView->viewportTransform() == viewTransform)
				break;
		}
		if (!namesHidden || shownNameStations.isEmpty() || restoredNameStations != shownNameStations)
			failures << QString("station name layer toggle does not restore the same names (before=%1 hiddenByToggle=%2 after=%3 zoom=%4)")
							.arg(shownNameStations.size())
							.arg(namesHidden)
							.arg(restoredNameStations.size())
							.arg(w.networkView->zoomRatio());
		else
			marker("E2E_STATION_NAMES_TOGGLE_OK");
		// A selected station keeps its name even where it would collide at Fit.
		StationOverlayItem* chosen = nullptr;
		for (auto* overlay : std::as_const(w.m_presenter->m_stationOverlays))
			if (overlay->nameItem() && overlay->hasSourceIdentity()
				&& (!chosen || overlay->isNameHiddenByCollision())) {
				chosen = overlay;
				if (overlay->isNameHiddenByCollision())
					break;
			}
		StationNodeItem* chosenNode = chosen
			? w.resolveStationNodeItem(chosen->sourceNodeId(), chosen->sourceTrack())
			: nullptr;
		if (!chosen || !chosenNode) {
			failures << "no station to select for the name check";
		} else {
			const bool wasHidden = chosen->isNameHiddenByCollision();
			w.m_inspector->displayStationNodeInfo(chosenNode);
			w.updateViewportOverlays();
			QApplication::processEvents();
			if (!chosen->isSelected() || !chosen->nameItem()->isVisible() || chosen->isNameHiddenByCollision()
				|| (caseName == QLatin1String("Copenhagen") && !wasHidden))
				failures << "selected station lost its name at Fit";
			else
				marker(QString("E2E_STATION_SELECTED_NAME_OK was_hidden=%1").arg(wasHidden ? 1 : 0));
		}
	}
	if (failures.isEmpty()) {
		marker("E2E_STATION_OVERLAY_OK");
		finish(0);
	} else {
		std::fprintf(stderr, "E2E_STATION_OVERLAY_FAIL: %s\n", failures.join(", ").toStdString().c_str());
		finish(2);
	}
}
