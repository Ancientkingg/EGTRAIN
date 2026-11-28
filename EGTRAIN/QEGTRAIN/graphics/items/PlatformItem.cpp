#include "graphics/items/PlatformItem.h"

#include <algorithm>

namespace {
// The end cap of a platform over its capacity is this many times the thickness of the bar high and this many wide.
constexpr qreal kCapHeight = 1.6;
constexpr qreal kCapWidth = 0.22;
// The held-at-entrance overflow bar sits past the end cap, at most this fraction of the bar length wide.
constexpr qreal kHeldGap = 0.25;
constexpr qreal kMinHeldWidth = 0.4;
constexpr qreal kMaxHeldFraction = 0.5;
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
		const qreal heldFrac = heldFraction();
		const qreal heldExtra = heldFrac > 0.0
			? rect().height() * (kCapWidth / 2.0 + kHeldGap) + std::max(rect().width() * heldFrac, rect().height() * kMinHeldWidth)
			: 0.0;
		bounds.adjust(-margin, -margin, margin + heldExtra, margin);
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

void PlatformItem::setCrowding(int waiting, int capacity, double threshold, int held) {
	capacity = std::max(capacity, 0);
	held = std::max(held, 0);
	if (m_waiting == waiting && m_capacity == capacity && m_threshold == threshold && m_held == held)
		return;
	const bool geometryChanged = m_gauge && ((m_held > 0) != (held > 0) || (m_held > 0 && (m_held != held || m_capacity != capacity)));
	if (geometryChanged)
		prepareGeometryChange();
	m_waiting = waiting;
	m_capacity = capacity;
	m_threshold = threshold;
	m_held = held;
	update();
}

qreal PlatformItem::fillFraction() const {
	if (m_capacity <= 0)
		return 0.0;
	return std::clamp(static_cast<qreal>(m_waiting) / m_capacity, 0.0, 1.0);
}

qreal PlatformItem::heldFraction() const {
	if (m_capacity <= 0 || m_held <= 0)
		return 0.0;
	return std::clamp(static_cast<qreal>(m_held) / m_capacity, 0.0, kMaxHeldFraction);
}

PlatformCrowding PlatformItem::crowding() const {
	if (m_capacity <= 0)
		return PlatformCrowding::Normal;
	if (m_waiting > m_capacity || m_held > 0)
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
	QPen outline(PassengerItem::troughOutline());
	outline.setCosmetic(true);
	outline.setWidth(0);
	painter->setPen(outline);
	painter->setBrush(PassengerItem::troughFill());
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
	const qreal heldFrac = heldFraction();
	if (heldFrac > 0.0) {
		const qreal offset = bar.height() * (kCapWidth / 2.0 + kHeldGap);
		const qreal width = std::max(bar.width() * heldFrac, bar.height() * kMinHeldWidth);
		painter->setPen(outline);
		painter->setBrush(PassengerItem::crowdingColour(PlatformCrowding::Over));
		painter->drawRect(QRectF(bar.right() + offset, bar.top(), width, bar.height()));
	}
}
