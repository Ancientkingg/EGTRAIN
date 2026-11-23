#include "graphics/items/PassengerItem.h"

#include <QPainter>

PassengerItem::PassengerItem(const QPixmap& pixmap, QGraphicsItem* parent)
	: QGraphicsPixmapItem(pixmap, parent) {
	setZValue(3); // draw over platforms
	// The mask shape of a pixmap is worked out in device pixels, which is four times too large for a figure
	setShapeMode(QGraphicsPixmapItem::BoundingRectShape);
}

PassengerItem::~PassengerItem() {
}

QColor PassengerItem::crowdingColour(PlatformCrowding crowding) {
	switch (crowding) {
		case PlatformCrowding::Busy:
			return QColor(0xe3, 0xa3, 0x3b);
		case PlatformCrowding::Over:
			return QColor(0xe5, 0x53, 0x4b);
		case PlatformCrowding::Normal:
			break;
	}
	return QColor(0x4f, 0xb3, 0xbf);
}

QPixmap PassengerItem::figure(PlatformCrowding crowding) {
	constexpr qreal ratio = 4.0;
	QPixmap pixmap(QSize(qRound(FigureWidth * ratio), qRound(FigureHeight * ratio)));
	pixmap.setDevicePixelRatio(ratio);
	pixmap.fill(Qt::transparent);
	QPainter painter(&pixmap);
	painter.setRenderHint(QPainter::Antialiasing);
	const QColor fill = crowdingColour(crowding);
	painter.setPen(QPen(QColor(0x0d, 0x14, 0x1b), 1.2));
	painter.setBrush(fill);
	// a head over a rounded body
	painter.drawRoundedRect(QRectF(3.0, 16.0, FigureWidth - 6.0, FigureHeight - 17.0), 7.0, 7.0);
	painter.drawEllipse(QPointF(FigureWidth / 2.0, 8.0), 6.5, 6.5);
	return pixmap;
}
