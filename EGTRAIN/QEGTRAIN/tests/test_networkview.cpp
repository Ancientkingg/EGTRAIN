#include "graphics/NetworkView.h"
#include "graphics/items/ConnectionItem.h"
#include "graphics/items/TrackLineItem.h"
#include "graphics/items/VirtualArcItem.h"

#include <QApplication>
#include <QGraphicsRectItem>
#include <QImage>
#include <QPainter>
#include <QWheelEvent>
#include <cmath>
#include <iostream>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

static bool near(qreal left, qreal right, qreal epsilon = 1e-6) {
	return std::abs(left - right) <= epsilon;
}

static bool endpointInside(const NetworkView& view, const QPointF& point) {
	const QPoint mapped = view.mapFromScene(point);
	const QRect viewport = view.viewport()->rect();
	return mapped.x() >= 24 && mapped.y() >= 24
		&& mapped.x() <= viewport.width() - 24
		&& mapped.y() <= viewport.height() - 24;
}

class TestNetworkView : public NetworkView {
public:
	using NetworkView::drawBackground;
};

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	TestNetworkView backgroundView;
	QImage background(160, 160, QImage::Format_RGB32);
	background.fill(Qt::magenta);
	{
		QPainter painter(&background);
		backgroundView.drawBackground(&painter, QRectF(0.0, 0.0, 160.0, 160.0));
	}
	bool uniformBackground = true;
	for (int y = 0; y < background.height() && uniformBackground; ++y) {
		for (int x = 0; x < background.width(); ++x) {
			if (background.pixelColor(x, y) != QColor("#12191F")) {
				uniformBackground = false;
				break;
			}
		}
	}
	NetworkView view;
	QGraphicsScene scene;
	view.setScene(&scene);
	view.resize(640, 480);
	view.show();

	auto* track = new TrackLineItem(QLineF(0.0, 0.0, 100.0, 0.0));
	scene.addItem(track);
	auto* arc = new VirtualArcItem(QLineF(100.0, 0.0, 150.0, 50.0),
		QLineF(150.0, 50.0, 200.0, 0.0));
	scene.addItem(arc);
	auto* connection = new ConnectionItem(QLineF(200.0, 0.0, 240.0, 40.0));
	scene.addItem(connection);
	auto* farOverlay = scene.addRect(QRectF(100000.0, 100000.0, 10.0, 10.0));
	farOverlay->setFlag(QGraphicsItem::ItemIgnoresTransformations);

	bool ok = true;
	ok &= expect(uniformBackground, "operational canvas has one semantic background color and no grid");
	view.fitToTopology();
	const QRectF topology = view.topologyBounds();
	const qreal fittedScale = view.fittedScale();
	ok &= expect(topology == QRectF(0.0, 0.0, 240.0, 50.0),
		"Fit uses the exact painted endpoint union, excluding overlays and selection polygons");
	ok &= expect(endpointInside(view, track->line().p1()) && endpointInside(view, track->line().p2()),
		"track endpoints stay inside the fit inset");
	ok &= expect(endpointInside(view, arc->line().p1()) && endpointInside(view, arc->line().p2()),
		"first virtual-arc segment endpoints stay inside the fit inset");
	ok &= expect(endpointInside(view, arc->secondPaintedLine().p1()) && endpointInside(view, arc->secondPaintedLine().p2()),
		"second virtual-arc segment endpoints stay inside the fit inset");
	ok &= expect(endpointInside(view, connection->line().p1()) && endpointInside(view, connection->line().p2()),
		"connection endpoints stay inside the fit inset");
	ok &= expect(view.horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff
			&& view.verticalScrollBarPolicy() == Qt::ScrollBarAlwaysOff,
		"Fit hides both scrollbars");
	ok &= expect(near(view.zoomRatio(), 1.0) && view.zoomLabel() == "Fit",
		"Fit reports the baseline label");
	const QRectF previewBounds(-20.0, -10.0, 600.0, 120.0);
	view.fitToBounds(previewBounds);
	ok &= expect(view.topologyBounds() == previewBounds, "explicit fit stores preview bounds");
	ok &= expect(near(view.zoomRatio(), 1.0) && view.zoomLabel() == "Fit",
		"explicit fit establishes the preview baseline");
	view.zoomBy(1.15);
	ok &= expect(near(view.zoomRatio(), 1.15, 1e-5), "preview zoom is relative to its fitted baseline");
	view.fitToBounds(previewBounds);
	ok &= expect(near(view.zoomRatio(), 1.0) && view.topologyBounds() == previewBounds,
		"explicit fit restores the preview baseline");

	int updates = 0;
	QObject::connect(&view, &NetworkView::viewportChanged, [&updates]() { ++updates; });
	view.fitToBounds(previewBounds);
	ok &= expect(updates == 1, "explicit fit emits one viewport update");
	ok &= expect(near(view.zoomRatio(), 1.0) && view.topologyBounds() == previewBounds,
		"explicit fit restores the preview baseline after a topology fit");
	view.fitToTopology();
	updates = 0;
	const QPoint wheelPos = view.viewport()->rect().center() + QPoint(20, 0);
	const QPointF scenePointBeforeWheel = view.mapToScene(wheelPos);
	QWheelEvent wheel(wheelPos, view.viewport()->mapToGlobal(wheelPos), QPoint(0, 0), QPoint(0, 120),
		Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
	QApplication::sendEvent(view.viewport(), &wheel);
	ok &= expect(near(view.zoomRatio(), 1.15, 1e-5), "one wheel step applies one 1.15 zoom");
	ok &= expect(QLineF(scenePointBeforeWheel, view.mapToScene(wheelPos)).length() < 0.5,
		"wheel zoom keeps the scene point under the pointer fixed");
	ok &= expect(updates == 1, "one wheel zoom emits one viewport update");

	view.fitToTopology();
	updates = 0;
	auto checkProgrammaticZoom = [&view, &updates, &ok](qreal factor, bool expectedApplied, const char* message) {
		const int before = updates;
		const bool applied = view.zoomBy(factor);
		ok &= expect(applied == expectedApplied, message);
		ok &= expect(updates == before + (expectedApplied ? 1 : 0),
			"each programmatic zoom emits exactly one update when applied");
	};
	checkProgrammaticZoom(1.15, true, "programmatic zoom-in applies");
	checkProgrammaticZoom(1.0 / 1.15, true, "programmatic zoom-out applies");
	checkProgrammaticZoom(1.0 / 1.15, false, "zoom-out at Fit is a no-op");
	checkProgrammaticZoom(2.0, true, "zoom-in above Fit applies");
	const QPointF centerBeforePan = view.mapToScene(view.viewport()->rect().center());
	view.centerOn(QPointF(180.0, 25.0));
	ok &= expect(QLineF(centerBeforePan, view.mapToScene(view.viewport()->rect().center())).length() > 1.0,
		"hidden scrollbars still permit panning above Fit");
	ok &= expect(view.horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff
			&& view.verticalScrollBarPolicy() == Qt::ScrollBarAlwaysOff,
		"zoom above Fit keeps the existing scrollbar policy");
	view.fitToTopology();
	view.zoomBy(2.0);
	updates = 0;
	const QPointF requestedCenter(120.0, 25.0);
	view.centerOn(requestedCenter);
	ok &= expect(updates == 1, "programmatic center emits one viewport update");
	view.resize(800, 600);
	QApplication::processEvents();
	const QPointF centerAfterProgrammaticResize = view.mapToScene(view.viewport()->rect().center());
	ok &= expect(QLineF(requestedCenter, centerAfterProgrammaticResize).length() < 0.5,
		"resize preserves a programmatic scene center");
	view.resize(640, 480);
	QApplication::processEvents();
	checkProgrammaticZoom(100.0, true, "zoom-in clamps at 12x");
	ok &= expect(near(view.zoomRatio(), 12.0, 1e-5) && view.zoomLabel() == "12x",
		"zoom-in clamps at 12x");
	checkProgrammaticZoom(1.15, false, "zoom-in at maximum is a no-op");
	checkProgrammaticZoom(1.0 / 100.0, true, "zoom-out clamps at Fit");
	ok &= expect(near(view.zoomRatio(), 1.0, 1e-5) && view.zoomLabel() == "Fit",
		"zoom-out clamps at Fit");
	checkProgrammaticZoom(1.0 / 1.15, false, "zoom-out at Fit remains a no-op");
	ok &= expect(view.horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff
			&& view.verticalScrollBarPolicy() == Qt::ScrollBarAlwaysOff,
		"Fit hides both scrollbars after zooming");

	view.fitToTopology();
	const qreal firstBaseline = view.fittedScale();
	view.resize(800, 600);
	QApplication::processEvents();
	ok &= expect(near(view.zoomRatio(), 1.0, 1e-5), "resize at Fit remains at Fit");
	ok &= expect(!near(firstBaseline, view.fittedScale(), 1e-5), "resize recomputes the fitted baseline");
	view.zoomBy(3.0);
	const QPointF centerBeforeResize = view.mapToScene(view.viewport()->rect().center());
	view.resize(640, 480);
	QApplication::processEvents();
	const QPointF centerAfterResize = view.mapToScene(view.viewport()->rect().center());
	ok &= expect(near(view.zoomRatio(), 3.0, 1e-5), "resize away from Fit preserves the zoom ratio");
	ok &= expect(QLineF(centerBeforeResize, centerAfterResize).length() < 0.5,
		"resize away from Fit preserves the viewed scene center");

	view.fitToTopology();
	view.zoomBy(2.0);
	ok &= expect(view.zoomLabel() == "2x", "integral zoom labels use map notation");
	view.zoomBy(6.0);
	ok &= expect(view.zoomLabel() == "12x", "maximum zoom label uses map notation");

	Q_UNUSED(fittedScale);
	return ok ? 0 : 1;
}
