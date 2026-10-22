#ifndef STATIONNODEITEM_H
#define STATIONNODEITEM_H

#include <QGraphicsRectItem>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWidget>
#include <QPolygonF>
#include <QtMath>

#include "simulation/Infrastructure.h"

/// The square of a station node of a track line. It draws over track lines and connections.
class StationNodeItem : public QGraphicsRectItem {
public:
	/// A square in rect, in scene coordinates, without a node.
	StationNodeItem(const QRectF& rect, QGraphicsItem* parent = nullptr);
	~StationNodeItem() override;

	/// Draws the square with the pen and the brush of the item.
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

	/// The index of the track line of the node in the run; -1 in the preview.
	int track;

	/// The node of the run, which belongs to the run; null in the preview.
	Node* node;

	enum { Type = UserType + 5 /**< The item type for qgraphicsitem_cast. */ };
	/// Returns Type.
	int type() const override {
		// Enable the use of qgraphicsitem_cast with this item.
		return Type;
	}
};

#endif // STATIONNODEITEM_H
