#include "graphics/NetworkView.h"
#include "graphics/items/ConnectionItem.h"
#include "graphics/items/TrackLineItem.h"
#include "graphics/items/VirtualArcItem.h"

#include <QApplication>
#include <QGraphicsRectItem>
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
		&& mapped.x() <= viewport.right() - 24
		&& mapped.y() <= viewport.bottom() - 24;
}

int main(int argc, char** argv) {
	QApplication app(argc, argv);
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
	view.fitToTopology();
	const QRectF topology = view.topologyBounds();
	const qreal fittedScale = view.fittedScale();
	ok &= expect(topology.left() <= 0.0 && topology.right() >= 240.0,
		"Fit includes track, both virtual-arc segments, and connection endpoints");
	ok &= expect(topology.top() <= 0.0 && topology.bottom() >= 50.0,
		"Fit includes the painted vertical extent");
	ok &= expect(topology.right() < 1000.0, "Fit ignores far-away overlays and selection polygons");
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

	int updates = 0;
	QObject::connect(&view, &NetworkView::viewportChanged, [&updates]() { ++updates; });
	const QPoint wheelPos(view.viewport()->rect().center());
	QWheelEvent wheel(wheelPos, view.viewport()->mapToGlobal(wheelPos), QPoint(0, 0), QPoint(0, 120),
		Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
	QApplication::sendEvent(view.viewport(), &wheel);
	ok &= expect(near(view.zoomRatio(), 1.15, 1e-5), "one wheel step applies one 1.15 zoom");
	ok &= expect(updates == 1, "one wheel zoom emits one viewport update");

	view.fitToTopology();
	updates = 0;
	for (int i = 0; i < 100; ++i)
		view.zoomBy(1.15);
	ok &= expect(near(view.zoomRatio(), 12.0, 1e-5) && view.zoomLabel() == "12x",
		"zoom-in clamps at 12x");
	const int atMaximum = updates;
	view.zoomBy(1.15);
	ok &= expect(updates == atMaximum, "zoom-in at maximum is a no-op without notification");
	for (int i = 0; i < 100; ++i)
		view.zoomBy(1.0 / 1.15);
	ok &= expect(near(view.zoomRatio(), 1.0, 1e-5) && view.zoomLabel() == "Fit",
		"zoom-out clamps at Fit");
	const int atFit = updates;
	view.zoomBy(1.0 / 1.15);
	ok &= expect(updates == atFit, "zoom-out at Fit is a no-op without notification");

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
