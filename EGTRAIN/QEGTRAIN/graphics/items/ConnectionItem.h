#ifndef CONNECTIONITEM_H
#define CONNECTIONITEM_H

#include <QGraphicsLineItem>

#include <QRectF>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPen>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWidget>
#include <QPolygonF>
#include <QtMath>

#include "simulation/Infrastructure.h"

/// The line of a connection between two track lines, in scene coordinates.
class ConnectionItem : public QGraphicsLineItem {
public:
	/// A line without a connection; its click target reaches 20 scene units on each side of it.
	ConnectionItem(const QLineF& line, QGraphicsItem* parent = nullptr);
	~ConnectionItem() override;

	/// The click target: the line widened by 20 scene units on each side.
	QPainterPath shape() const override;
	/// The bounds of the click target.
	QRectF boundingRect() const override;
	/// Draws the line with the pen of the item.
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

	/// The connection of the run, which belongs to the run; null in the preview.
	const Connections* connection;

	enum { Type = UserType + 4 /**< The item type for qgraphicsitem_cast. */ };
	/// Returns Type.
	int type() const override {
		// Enable the use of qgraphicsitem_cast with this item.
		return Type;
	}

private:
	// The half width in scene units of the click target.
	const qreal selectionOffset;
	QPolygonF selectionPolygon;
	void createSelectionPolygon();
};

#endif // CONNECTIONITEM_H
