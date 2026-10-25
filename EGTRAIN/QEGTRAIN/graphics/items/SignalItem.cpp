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
	updateToolTip();
}

int SignalItem::aspectCode() const {
	return m_aspectCode;
}

void SignalItem::setFailed(bool failed) {
	if (m_failed == failed)
		return;
	m_failed = failed;
	update();
	updateToolTip();
}

void SignalItem::setReversedDirection(bool reversed) {
	if (reversedDirection == reversed)
		return;
	reversedDirection = reversed;
	update();
	updateToolTip();
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
	return QStringLiteral("Track %1 at %2 km")
		.arg(sceneTrackId.empty() ? QString::number(trackID) : QString::fromStdString(sceneTrackId))
		.arg(X, 0, 'g', 10);
}

void SignalItem::updateToolTip() {
	setToolTip(QStringLiteral("%1, facing %2\n%3")
			.arg(signalAspectLabel(this), reversedDirection ? QStringLiteral("left") : QStringLiteral("right"), inspectionIdentity()));
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
	const QColor lampColor = m_failed ? QColor(Qt::red) : m_lampColor;
	// Marks tell the states apart without colour. They need room: below
	// kMinMarkPixels device pixels only the colour is left.
	const QTransform& world = painter->worldTransform();
	const qreal devicePixels = lamp.width() * std::hypot(world.m11(), world.m12()) * painter->device()->devicePixelRatioF();
	const bool marked = devicePixels >= kMinMarkPixels;
	if (!unavailable && !marked) {
		// A hairline would hide a small lamp, so a lit or failed head is a disc in its lamp colour as wide as the ring
		// of an unavailable head, 1.5 device pixels wider than the head. A fill costs less to paint than that stroke.
		if (devicePixels > 0.0) {
			const qreal grow = 0.75 * lamp.width() / devicePixels;
			painter->setPen(Qt::NoPen);
			painter->setBrush(lampColor);
			painter->drawEllipse(lamp.adjusted(-grow, -grow, grow, grow));
		}
		return;
	}
	if (unavailable) {
		QPen ring(ringColor);
		ring.setCosmetic(true);
		ring.setWidthF(1.5);
		painter->setPen(ring);
		painter->setBrush(Qt::NoBrush);
	} else {
		painter->setPen(pen());
		painter->setBrush(lampColor);
	}
	painter->drawEllipse(lamp);
	if (!marked)
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

QString signalAspectLabel(const SignalItem* signal) {
	if (signal->failed())
		return QStringLiteral("Failed");
	switch (classifySignalCue(signal->aspectCode())) {
		case SignalCueKind::Stop:
			return QStringLiteral("Stop");
		case SignalCueKind::Caution:
			return QStringLiteral("Caution");
		case SignalCueKind::Proceed:
			return QStringLiteral("Proceed");
		case SignalCueKind::Neutral:
		default:
			return QStringLiteral("Unavailable");
	}
}
