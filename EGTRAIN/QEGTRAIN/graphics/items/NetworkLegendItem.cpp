#include "graphics/items/NetworkLegendItem.h"

#include "graphics/VisualPolish.h"

namespace {

const QRectF legendRect(0.0, 0.0, 190.0, 154.0);

void drawTrackEntry(QPainter* painter, qreal y, const QString& label, TrackOperationalState state) {
	const TrackStateVisual visual = classifyTrackState(state);
	QPen pen(visual.color);
	pen.setWidth(visual.width > 0 ? visual.width : 2);
	pen.setStyle(visual.style == Qt::NoPen ? Qt::SolidLine : visual.style);
	pen.setCosmetic(true);
	painter->setPen(pen);
	painter->drawLine(QLineF(12.0, y, 40.0, y));
	painter->setPen(QColor("#d9e1e8"));
	painter->drawText(QRectF(50.0, y - 9.0, 126.0, 18.0), Qt::AlignVCenter | Qt::AlignLeft, label);
}

void drawTrainEntry(QPainter* painter, qreal y, const QString& label, const TrainVisual& visual) {
	painter->setPen(QPen(visual.outline, 1.0));
	painter->setBrush(visual.fill);
	painter->drawRoundedRect(QRectF(12.0, y - 6.0, 28.0, 12.0), 3.0, 3.0);
	painter->setPen(QColor("#d9e1e8"));
	painter->drawText(QRectF(50.0, y - 9.0, 126.0, 18.0), Qt::AlignVCenter | Qt::AlignLeft, label);
}

} // namespace

NetworkLegendItem::NetworkLegendItem(QGraphicsItem* parent)
	: QGraphicsItem(parent) {
	setFlag(QGraphicsItem::ItemIgnoresTransformations);
	setZValue(8);
}

QRectF NetworkLegendItem::boundingRect() const {
	return legendRect;
}

void NetworkLegendItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
	Q_UNUSED(option);
	Q_UNUSED(widget);

	painter->setPen(QPen(QColor("#536271"), 1.0));
	painter->setBrush(QColor(23, 29, 36, 235));
	painter->drawRoundedRect(legendRect, 5.0, 5.0);

	QFont font = painter->font();
	font.setPointSize(9);
	painter->setFont(font);
	painter->setPen(QColor("#f2f5f7"));
	painter->drawText(QRectF(12.0, 8.0, 166.0, 18.0), Qt::AlignLeft | Qt::AlignVCenter, "Network legend");

	drawTrackEntry(painter, 34.0, "Prepared route", TrackOperationalState::Prepared);
	drawTrackEntry(painter, 54.0, "Occupied section", TrackOperationalState::Occupied);
	drawTrackEntry(painter, 74.0, "Blocked section", TrackOperationalState::Blocked);

	drawTrainEntry(painter, 98.0, "Intercity", classifyTrainType("Intercity", ""));
	drawTrainEntry(painter, 118.0, "Sprinter", classifyTrainType("Sprinter", ""));
	drawTrainEntry(painter, 138.0, "Freight", classifyTrainType("Freight", ""));
}
