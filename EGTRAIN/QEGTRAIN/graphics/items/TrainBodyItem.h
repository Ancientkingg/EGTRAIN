#ifndef TRAINBODYITEM_H
#define TRAINBODYITEM_H

#include <QGraphicsPolygonItem>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWidget>
#include <QPolygonF>
#include <QtMath>

/// The body of one wagon of a train, a polygon in scene coordinates.
class TrainBodyItem : public QGraphicsPolygonItem {
public:
	/// A body with the shape of polygon and train index -1.
	TrainBodyItem(const QPolygonF& polygon, QGraphicsItem* parent = nullptr);
	~TrainBodyItem() override;

	/// Narrowest on-screen size, in logical pixels, of a body that is still drawn as a mark.
	static constexpr qreal MinPixels = 8.0;

	/// A body keeps its shape and fill at every zoom. When the view shows it, with its outline, thinner than
	/// MinPixels, it is stroked in its fill colour to that thickness, and the stroke is part of the bounding
	/// rect and the shape that a click hits. viewScale is the logical pixels per scene unit of the view.
	void setViewScale(qreal viewScale);

	/// The polygon, with the stroke of a body that is too thin on screen.
	QRectF boundingRect() const override;
	/// The polygon, with the stroke of a body that is too thin on screen.
	QPainterPath shape() const override;
	/// Draws the stroke of a body that is too thin on screen, then the polygon with the pen and the brush.
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

	/// The index of the train in the run; -1 until it is set. A train ID is not unique, the index is.
	int index;

	enum { Type = UserType + 7 /**< The item type for qgraphicsitem_cast. */ };
	/// Returns Type.
	int type() const override {
		// Enable the use of qgraphicsitem_cast with this item.
		return Type;
	}

private:
	// Width in scene units of the stroke that enlarges the body at viewScale, 0 when the body is thick enough.
	qreal markWidth(qreal viewScale) const;

	qreal m_viewScale = 0.0;
};

#endif // TRAINBODYITEM_H
