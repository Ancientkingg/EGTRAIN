#ifndef NETWORKVIEW_H
#define NETWORKVIEW_H

#include <QGraphicsView>
#include <QMouseEvent>
#include <QGraphicsSceneMouseEvent>
#include <QEvent>
#include <QPoint>
#include <QGraphicsItem>
#include <QGraphicsEllipseItem>
#include <QBrush>
#include <QTransform>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QRectF>
#include <QString>
#include <QDebug>
#include <QScrollBar>
#include <QElapsedTimer>
#include <functional>
#include <iostream>

/// The view of the network canvas. Items have their positions and sizes in scene units. The view scale,
/// the m11() of transform(), is in logical pixels per scene unit. One logical pixel is as wide as the device
/// pixel ratio of the screen in device pixels. The view fits the track to the viewport and zooms between
/// that Fit scale and maximumZoomRatio() times it. A left drag pans the view.
class NetworkView : public QGraphicsView {
	Q_OBJECT

public:
	/// A view with the dark canvas, antialiasing and panning by drag.
	NetworkView(QWidget* parent = nullptr);
	~NetworkView() override;

	using QGraphicsView::centerOn;
	/// Centres the view on a point in scene coordinates and emits viewportChanged() when the centre moved.
	void centerOn(const QPointF& scenePos);
	/// Centres the view on the centre of the bounds of the item in the scene. Nothing happens for null.
	void centerOn(QGraphicsItem* item);
	/// Centres the view on a point in scene coordinates to a fraction of a pixel and emits viewportChanged() when the
	/// centre moved. The scroll bars take the whole pixels and a translation of the transform the rest. An axis on
	/// which the view cannot move that far, at the edge of the scene or with the whole scene in view, is centred to
	/// the pixel as centerOn() does.
	void centerOnExactly(const QPointF& scenePos);
	/// Hands the press to QGraphicsView.
	void mousePressEvent(QMouseEvent* mouseEvent) override;
	/// Hands the release to QGraphicsView and emits interactionFinished().
	void mouseReleaseEvent(QMouseEvent* mouseEvent) override;
	/// Fits the view, as fitToBounds() does, to the ends of the track lines and connections in the scene.
	void fitToTopology();
	/// Fits the view to bounds in scene coordinates, with padding on each side of 24 logical pixels, or of an
	/// eighth of the width or height of the viewport when that is less, and makes it the scene rectangle. Empty
	/// bounds reset the view to scale 1 around the origin.
	void fitToBounds(const QRectF& bounds);
	/// Multiplies the zoom ratio by factor, kept between 1 and maximumZoomRatio(). viewportAnchor, in logical
	/// pixels of the viewport, stays over the same scene point; a negative coordinate means the centre of the
	/// viewport. Returns whether the zoom changed.
	bool zoomBy(qreal factor, const QPointF& viewportAnchor = QPointF(-1.0, -1.0));
	/// The view scale divided by fittedScale(): 1 at Fit, at most maximumZoomRatio().
	qreal zoomRatio() const;
	/// The largest zoom ratio.
	static constexpr qreal maximumZoomRatio() { return 640.0; }
	/// The view scale at Fit in logical pixels per scene unit; 1 before a fit and without bounds.
	qreal fittedScale() const;
	/// The bounds in scene coordinates of the last fit.
	QRectF topologyBounds() const;
	/// The zoom ratio as the view shows it: "Fit", or the ratio with an "x", such as "2x" or "2.5x".
	QString zoomLabel() const;
	/// Starts a timer for the next paint of the viewport, which emits timingPaintCompleted() with kind and
	/// generation.
	void armTimingPaint(const QString& kind, int generation);

protected:
	/// Zooms on a pinch gesture, and times the paint of the viewport for the playback profiler and for
	/// armTimingPaint().
	bool viewportEvent(QEvent* event) override;
	/// A scroll in pixels pans the view. A wheel step zooms by 1.15 per notch of 120 units about the pointer.
	/// With Control held, QGraphicsView handles the wheel.
	void wheelEvent(QWheelEvent* event) override;
	/// Fills the canvas and draws a grid of lines one device pixel wide on multiples of a spacing in scene
	/// units. The spacing starts at 80 and is doubled or halved until it is 24 to 96 logical pixels on screen.
	void drawBackground(QPainter* painter, const QRectF& rect) override;
	/// Keeps a view at Fit fitted to the new size, and keeps the zoom ratio and the centre of any other view.
	void resizeEvent(QResizeEvent* event) override;
	/// Records the new centre and emits viewportChanged(), except while the view moves itself.
	void scrollContentsBy(int dx, int dy) override;

private:
	QRectF paintedTopologyBounds(bool* hasBounds) const;
	void applyFit(const QRectF& bounds, bool hasBounds);
	void centerOnScene(const QPointF& scenePos);
	void moveCenter(const std::function<void()>& move);
	qreal calculateFittedScale() const;
	bool atFit() const;

	QRectF m_topologyBounds;
	qreal m_fittedScale = 1.0;
	bool m_hasTopologyBounds = false;
	bool m_suppressViewportChanged = false;
	QPointF m_viewCenter;
	bool m_hasViewCenter = false;
	QElapsedTimer m_timingPaintTimer;
	QString m_timingPaintKind;
	int m_timingPaintGeneration = -1;

signals:
	/// A mouse button was released over the view.
	void interactionFinished();
	/// The centre or the scale of the view changed.
	void viewportChanged();
	/// The paint that armTimingPaint() waited for has finished, elapsedNanoseconds after the call.
	void timingPaintCompleted(const QString& kind, int generation, qint64 elapsedNanoseconds);
	/// The playback profiler began its measurement after a paint.
	void playbackProfileBegan();
};

#endif // NETWORKVIEW_H
