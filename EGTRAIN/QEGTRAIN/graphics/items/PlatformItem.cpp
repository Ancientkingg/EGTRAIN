#include "graphics/items/PlatformItem.h"

#include <algorithm>

namespace {
// The colours of the trough of a gauge.
const QColor kTroughFill(0x2c, 0x38, 0x46);
const QColor kTroughOutline(0x5c, 0x6a, 0x78);
// The end cap of a platform over its capacity is this many times the thickness of the bar high and this many wide.
constexpr qreal kCapHeight = 1.6;
constexpr qreal kCapWidth = 0.22;
}

PlatformItem::PlatformItem(const QRectF& rect, QGraphicsItem* parent)
	: QGraphicsRectItem(rect, parent), node(nullptr), textIcon(nullptr) {
	setZValue(2); // draw over arcs and connections (which have z = 0) and nodes (z = 1)
}

PlatformItem::~PlatformItem() {
}

QRectF PlatformItem::boundingRect() const {
	QRectF bounds = QGraphicsRectItem::boundingRect();
	if (m_gauge) {
		const qreal margin = rect().height() * kCapHeight / 2.0;
		bounds.adjust(-margin, -margin, margin, margin);
	}
	return bounds;
}

void PlatformItem::setGauge(bool enabled) {
	if (m_gauge == enabled)
		return;
	prepareGeometryChange();
	m_gauge = enabled;
	update();
}

void PlatformItem::setCrowding(int waiting, int capacity, double threshold) {
	capacity = std::max(capacity, 0);
	if (m_waiting == waiting && m_capacity == capacity && m_threshold == threshold)
		return;
	m_waiting = waiting;
	m_capacity = capacity;
	m_threshold = threshold;
	update();
}

qreal PlatformItem::fillFraction() const {
	if (m_capacity <= 0)
		return 0.0;
	return std::clamp(static_cast<qreal>(m_waiting) / m_capacity, 0.0, 1.0);
}

PlatformCrowding PlatformItem::crowding() const {
	if (m_capacity <= 0)
		return PlatformCrowding::Normal;
	if (m_waiting > m_capacity)
		return PlatformCrowding::Over;
	return static_cast<qreal>(m_waiting) / m_capacity > m_threshold ? PlatformCrowding::Busy : PlatformCrowding::Normal;
}

void PlatformItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
	Q_UNUSED(option);
	Q_UNUSED(widget);

	const QRectF bar = rect();
	if (!m_gauge) {
		painter->setPen(pen());
		painter->setBrush(brush());
		painter->drawRect(bar);
		return;
	}
	QPen outline(kTroughOutline);
	outline.setCosmetic(true);
	outline.setWidth(0);
	painter->setPen(outline);
	painter->setBrush(kTroughFill);
	painter->drawRect(bar);
	const qreal fraction = fillFraction();
	if (fraction > 0.0) {
		painter->setPen(Qt::NoPen);
		painter->setBrush(PassengerItem::crowdingColour(crowding()));
		painter->drawRect(QRectF(bar.left(), bar.top(), bar.width() * fraction, bar.height()));
	}
	if (crowding() == PlatformCrowding::Over) {
		const qreal height = bar.height() * kCapHeight;
		const qreal width = bar.height() * kCapWidth;
		painter->setPen(Qt::NoPen);
		painter->setBrush(PassengerItem::crowdingColour(PlatformCrowding::Over));
		painter->drawRect(QRectF(bar.right() - width / 2.0, bar.center().y() - height / 2.0, width, height));
	}
}
