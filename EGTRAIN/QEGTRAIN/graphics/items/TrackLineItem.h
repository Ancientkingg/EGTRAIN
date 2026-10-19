#ifndef TRACKLINEITEM_H
#define TRACKLINEITEM_H

#include <QGraphicsLineItem>

#include <string>
#include <sstream>
#include <QRectF>
#include <QLineF>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPen>
#include <iostream>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWidget>
#include <QPolygonF>
#include <QtMath>

#include "simulation/Infrastructure.h"
#include "graphics/VisualPolish.h"

using namespace std;

/// The line of an arc of a track line, in scene coordinates.
class TrackLineItem : public QGraphicsLineItem {
public:
	/// A free line without an arc; its click target reaches 20 scene units on each side of it.
	TrackLineItem(const QLineF& line, QGraphicsItem* parent = nullptr);
	~TrackLineItem() override;

	/// Sets the state of the track during a run. The inspector shows it; paint() does not.
	void setOperationalState(TrackOperationalState state);
	/// The state of the track during a run.
	TrackOperationalState operationalState() const;

	/// The click target: the line widened by 20 scene units on each side.
	QPainterPath shape() const override;
	/// The bounds of the click target.
	QRectF boundingRect() const override;
	/// Draws the line with the pen of the item.
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

	/// The index of the track line in the run; -1 in the preview.
	int track;

	/// The arc of the run, which belongs to the run; null in the preview.
	Arc* arc;

	enum { Type = UserType + 1 /**< The item type for qgraphicsitem_cast. */ };
	/// Returns Type.
	int type() const override {
		// Enable the use of qgraphicsitem_cast with this item.
		return Type;
	}

protected:
	/// The half width in scene units of the click target.
	const qreal selectionOffset;
	/// The click target in item coordinates.
	QPolygonF selectionPolygon;
	/// Builds selectionPolygon around the line.
	void createSelectionPolygon();

private:
	TrackOperationalState m_operationalState;
};

#endif // TRACKLINEITEM_H
