#include "graphics/items/SignalItem.h"

SignalItem::SignalItem(const QRectF& rect, QGraphicsItem* parent)
	: QGraphicsEllipseItem(rect, parent), m_aspectCode(-1), m_aspectIcon(":/icons/signal-neutral.svg"), m_compact(false) {
	setFlag(QGraphicsItem::ItemIgnoresTransformations);
	setZValue(2); // draw over arcs and connections (which have z = 0), and nodes (z = 1)
	QRectF fixedHousing = rect;
	fixedHousing.setSize(QSizeF(12.0, 16.0));
	fixedHousing.moveCenter(rect.center());
	setRect(fixedHousing);

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
	const SignalVisual visual = classifySignalAspect(code);
	m_aspectIcon = QPixmap(visual.iconResource);
	m_lampColor = visual.lamp;
	update();
}

int SignalItem::aspectCode() const {
	return m_aspectCode;
}

void SignalItem::setReversedDirection(bool reversed) {
	if (reversedDirection == reversed)
		return;
	reversedDirection = reversed;
	update();
}

void SignalItem::setCompact(bool compact) {
	if (m_compact == compact)
		return;
	m_compact = compact;
	update();
}

void SignalItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
	Q_UNUSED(option);
	Q_UNUSED(widget);

	// At overview zoom the fixed-size housings overlap into solid bands, so
	// keep stop and caution distinct while repeated proceed signals recede.
	if (m_compact) {
		const SignalCueKind cue = classifySignalCue(m_aspectCode);
		const QPointF center = rect().center();
		if (cue == SignalCueKind::Stop) {
			const QRectF lamp(center.x() - 4.0, center.y() - 4.0, 8.0, 8.0);
			painter->setPen(QPen(QColor("#0D131A"), 1.0));
			painter->setBrush(m_lampColor);
			painter->drawEllipse(lamp);
			painter->drawLine(QLineF(lamp.left() + 2.0, center.y(), lamp.right() - 2.0, center.y()));
		} else if (cue == SignalCueKind::Caution) {
			QPolygonF triangle;
			triangle << QPointF(center.x(), center.y() - 4.0)
					 << QPointF(center.x() + 4.0, center.y() + 3.5)
					 << QPointF(center.x() - 4.0, center.y() + 3.5);
			painter->setPen(QPen(QColor("#0D131A"), 1.0));
			painter->setBrush(m_lampColor);
			painter->drawPolygon(triangle);
		} else {
			painter->setPen(Qt::NoPen);
			painter->setBrush(cue == SignalCueKind::Proceed
				? QColor("#4F7A65") : QColor("#66747D"));
			QRectF tick(center.x() - 1.0, center.y() - 3.0, 2.0, 6.0);
			painter->drawRect(tick);
		}
		return;
	}

	QPen housingPen(QColor("#0d131a"));
	housingPen.setWidthF(1.0);
	painter->setPen(housingPen);
	painter->setBrush(QColor("#26313b"));
	painter->drawRoundedRect(rect(), 3.0, 3.0);

	painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
	const QRectF iconRect(rect().left(), rect().top(), rect().width(), rect().width());
	painter->drawPixmap(iconRect, m_aspectIcon, m_aspectIcon.rect());

	const qreal y = rect().bottom() - 3.0;
	QPolygonF cue;
	if (reversedDirection) {
		cue << QPointF(rect().left() + 3.0, y) << QPointF(rect().left() + 7.0, y - 2.5) << QPointF(rect().left() + 7.0, y + 2.5);
	} else {
		cue << QPointF(rect().right() - 3.0, y) << QPointF(rect().right() - 7.0, y - 2.5) << QPointF(rect().right() - 7.0, y + 2.5);
	}
	painter->setBrush(QColor("#f2f5f7"));
	painter->drawPolygon(cue);
}
