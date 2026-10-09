#include "graphics/items/SignalItem.h"

#include "graphics/VisualPolish.h"

#include <algorithm>
#include <cmath>

namespace {
// Narrowest head, in device pixels, that still draws its mark.
constexpr qreal kMinMarkPixels = 6.0;
} // namespace

SignalItem::SignalItem(const QRectF& rect, QGraphicsItem* parent)
	: QGraphicsEllipseItem(rect, parent), m_aspectCode(-1), m_lampColor(QColor(128, 128, 128)) {
	setZValue(2); // draw over arcs and connections (which have z = 0), and nodes (z = 1)
	// Invisible picking geometry stays device-sized while the actual head scales
	// with the authored scene. NetworkScene resolves child hits to this signal.
	auto* target = new QGraphicsEllipseItem(QRectF(-6.0, -6.0, 12.0, 12.0), this);
	target->setPos(rect.center());
	target->setFlag(QGraphicsItem::ItemIgnoresTransformations);
	target->setPen(Qt::NoPen);
	target->setBrush(Qt::NoBrush);
	target->setAcceptedMouseButtons(Qt::NoButton);

	// initialize parameters
	trackID = -1;
	X = -1;
	sectionAheadLength = sectionBehindLength = 0.0;
	sectionAheadTrackId = sectionBehindTrackId = -1;
	reversedDirection = false;
	setAspectCode(180);
}

SignalItem::~SignalItem() {
}

void SignalItem::setAspectCode(int code) {
	if (m_aspectCode == code)
		return;
	m_aspectCode = code;
	m_lampColor = classifySignalAspect(code).lamp;
	update();
}

int SignalItem::aspectCode() const {
	return m_aspectCode;
}

void SignalItem::setFailed(bool failed) {
	if (m_failed == failed)
		return;
	m_failed = failed;
	update();
}

void SignalItem::setReversedDirection(bool reversed) {
	if (reversedDirection == reversed)
		return;
	reversedDirection = reversed;
	update();
}

void SignalItem::setGroupedSignals(const QVector<QPair<int, bool>>& aspects) {
	if (std::equal(m_groupedSignals.cbegin(), m_groupedSignals.cend(), aspects.cbegin(), aspects.cend()))
		return;
	m_groupedSignals = aspects;
	update();
}

QString SignalItem::inspectionIdentity() const {
	if (!m_inspectionIdentity.isEmpty())
		return m_inspectionIdentity;
	return QStringLiteral("Track %1 at %2 km; ahead %3; behind %4")
		.arg(trackID)
		.arg(X, 0, 'g', 10)
		.arg(QString::fromStdString(sectionAheadId.empty() ? "none" : sectionAheadId))
		.arg(QString::fromStdString(sectionBehindId.empty() ? "none" : sectionBehindId));
}

QRectF SignalItem::boundingRect() const {
	return QGraphicsEllipseItem::boundingRect().adjusted(-2.0, -2.0, 2.0, 2.0);
}

void SignalItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
	Q_UNUSED(option);
	Q_UNUSED(widget);

	const QRectF lamp = rect();
	const SignalCueKind cue = classifySignalCue(m_aspectCode);
	// Without signalling data the head is an empty ring, not a lit lamp.
	const bool unavailable = !m_failed && cue == SignalCueKind::Neutral;
	const QColor ringColor(150, 150, 150);
	if (unavailable) {
		QPen ring(ringColor);
		ring.setCosmetic(true);
		ring.setWidthF(1.5);
		painter->setPen(ring);
		painter->setBrush(Qt::NoBrush);
	} else {
		painter->setPen(pen());
		painter->setBrush(m_failed ? QColor(Qt::red) : m_lampColor);
	}
	painter->drawEllipse(lamp);

	// Marks tell the states apart without colour. They need room: below
	// kMinMarkPixels device pixels only the colour is left.
	const QTransform& world = painter->worldTransform();
	const qreal devicePixels = lamp.width() * std::hypot(world.m11(), world.m12()) * painter->device()->devicePixelRatioF();
	if (devicePixels < kMinMarkPixels)
		return;
	const QPointF c = lamp.center();
	const qreal r = lamp.width() / 2.0;
	QPen mark(m_failed ? QColor(Qt::white) : unavailable ? ringColor
														 : QColor(30, 30, 30));
	mark.setCosmetic(true);
	mark.setWidthF(1.5);
	painter->setPen(mark);
	if (m_failed) {
		painter->drawLine(QLineF(c.x() - 0.5 * r, c.y() - 0.5 * r, c.x() + 0.5 * r, c.y() + 0.5 * r));
		painter->drawLine(QLineF(c.x() - 0.5 * r, c.y() + 0.5 * r, c.x() + 0.5 * r, c.y() - 0.5 * r));
	} else if (cue == SignalCueKind::Stop) {
		painter->drawLine(QLineF(c.x() - 0.6 * r, c.y(), c.x() + 0.6 * r, c.y()));
	} else if (cue == SignalCueKind::Caution) {
		painter->setBrush(QColor(30, 30, 30));
		painter->drawEllipse(c, 0.2 * r, 0.2 * r);
	} else if (unavailable) {
		painter->drawLine(QLineF(c.x() - 0.4 * r, c.y(), c.x() + 0.4 * r, c.y()));
	}
}
