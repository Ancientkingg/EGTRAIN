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
#include <iostream>

class NetworkView : public QGraphicsView {
	Q_OBJECT

public:
	NetworkView(QWidget* parent = 0);
	~NetworkView();

	void mousePressEvent(QMouseEvent* mouseEvent) override;
	void fitToTopology();
	void fitToBounds(const QRectF& bounds);
	bool zoomBy(qreal factor, const QPointF& viewportAnchor = QPointF(-1.0, -1.0));
	qreal zoomRatio() const;
	qreal fittedScale() const;
	QRectF topologyBounds() const;
	QString zoomLabel() const;

protected:
	void wheelEvent(QWheelEvent* event) override;
	void drawBackground(QPainter* painter, const QRectF& rect) override;
	void resizeEvent(QResizeEvent* event) override;
	void scrollContentsBy(int dx, int dy) override;

private:
	QRectF paintedTopologyBounds(bool* hasBounds) const;
	void applyFit(const QRectF& bounds, bool hasBounds);
	qreal calculateFittedScale() const;
	bool atFit() const;

	QRectF m_topologyBounds;
	qreal m_fittedScale = 1.0;
	bool m_hasTopologyBounds = false;
	bool m_suppressViewportChanged = false;
	QPointF m_viewCenter;
	bool m_hasViewCenter = false;

signals:
	void MousePressedOnView();
	void viewportChanged();
};

#endif // NETWORKVIEW_H
