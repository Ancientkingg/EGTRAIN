#ifndef NODEITEM_H
#define NODEITEM_H

#include <QGraphicsEllipseItem>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWidget>
#include <QPolygonF>
#include <QtMath>

#include "simulation/Infrastructure.h"

/// The dot of a node of a track line. It draws over track lines and connections. A transparent child of 3 by 3
/// logical pixels at its centre keeps it clickable when the dot is smaller on screen.
class NodeItem : public QGraphicsEllipseItem {
public:
	/// A dot in rect, in scene coordinates, without a node.
	NodeItem(const QRectF& rect, QGraphicsItem* parent = nullptr);
	~NodeItem() override;

	/// Draws the dot with the pen and the brush of the item.
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

	/// The index of the track line of the node in the run; -1 in the preview.
	int track;

	/// The node of the run, which belongs to the run; null in the preview.
	Node* node;

	enum { Type = UserType + 2 /**< The item type for qgraphicsitem_cast. */ };
	/// Returns Type.
	int type() const override {
		// Enable the use of qgraphicsitem_cast with this item.
		return Type;
	}
};

#endif // NODEITEM_H
