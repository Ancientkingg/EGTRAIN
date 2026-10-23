#include "diagrams/DiagramKey.h"

#include <QFontMetricsF>
#include <QPaintEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace {
// The room above and below the text of a row.
constexpr double kRowPadding = 4;
} // namespace

double diagramKeyRowHeight(const QFont& font) {
	return std::ceil(std::max(QFontMetricsF(font).height(), kKeySampleHeight) + 2 * kRowPadding);
}

void paintKeySample(QPainter& painter, const QRectF& area, const DiagramKeyEntry& entry) {
	const QRectF sample(area.center().x() - kKeySampleWidth / 2, area.center().y() - kKeySampleHeight / 2, kKeySampleWidth,
		kKeySampleHeight);
	painter.save();
	painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
	switch (entry.shape) {
		case DiagramKeyEntry::Shape::Box:
			painter.setPen(entry.pen);
			painter.setBrush(entry.brush);
			painter.drawRect(sample.adjusted(1, 1, -1, -1));
			break;
		case DiagramKeyEntry::Shape::Cell:
			painter.fillRect(sample, entry.background);
			painter.fillRect(sample, entry.brush);
			if (entry.pen.style() != Qt::NoPen) painter.fillRect(QRectF(sample.left(), sample.top(), sample.width(), 3), entry.pen.color());
			painter.setPen(QPen(entry.background.darker(115), 1));
			painter.setBrush(Qt::NoBrush);
			painter.drawRect(sample.adjusted(0.5, 0.5, -0.5, -0.5));
			break;
		case DiagramKeyEntry::Shape::Tick:
			painter.setPen(entry.pen);
			painter.drawLine(QPointF(sample.center().x(), sample.top()), QPointF(sample.center().x(), sample.bottom()));
			break;
		case DiagramKeyEntry::Shape::Marker:
			painter.setPen(entry.pen);
			painter.setBrush(entry.brush);
			painter.drawEllipse(sample.center(), 3.5, 3.5);
			break;
		case DiagramKeyEntry::Shape::Line: {
			QPen line = entry.pen;
			// The dot covers the middle of the sample, so a dashed line takes dashes of 3 px with gaps of 2 px, one gap
			// centred under the dot, which leave a dash and a gap on each side of it.
			if (entry.marker && line.style() != Qt::SolidLine && line.style() != Qt::NoPen && line.widthF() > 0) {
				const double half = sample.width() / 2 - 1;
				line.setDashPattern({3 / line.widthF(), 2 / line.widthF()});
				line.setDashOffset(std::fmod(9 - std::fmod(half, 5), 5) / line.widthF());
				line.setCapStyle(Qt::FlatCap);
			}
			painter.setPen(line);
			painter.drawLine(QPointF(sample.left() + 1, sample.center().y()), QPointF(sample.right() - 1, sample.center().y()));
			if (entry.marker) {
				QPen outline = entry.pen;
				outline.setStyle(Qt::SolidLine);
				outline.setWidthF(std::min(outline.widthF(), 1.5));
				painter.setPen(outline);
				painter.setBrush(Qt::white);
				painter.drawEllipse(sample.center(), 3, 3);
			}
			break;
		}
	}
	painter.restore();
}

double paintKeyRow(QPainter& painter, const QRectF& area, const DiagramKeyEntry& entry, const QFont& font, const QColor& textColour) {
	const double height = diagramKeyRowHeight(font);
	paintKeySample(painter, QRectF(area.left(), area.top(), kKeySampleWidth, height), entry);
	const QFontMetricsF metrics(font);
	const QRectF text(area.left() + kKeySampleWidth + kKeySampleGap, area.top(), std::max(0.0, area.width() - kKeySampleWidth - kKeySampleGap),
		height);
	painter.save();
	painter.setFont(font);
	painter.setPen(textColour);
	painter.drawText(text, Qt::AlignLeft | Qt::AlignVCenter, metrics.elidedText(entry.name, Qt::ElideRight, text.width()));
	painter.restore();
	return height;
}

double diagramKeyWidth(const QVector<DiagramKeyEntry>& entries, const QFont& font) {
	const QFontMetricsF metrics(font);
	double width = 0;
	for (const DiagramKeyEntry& entry : entries) width = std::max(width, metrics.horizontalAdvance(entry.name));
	return entries.isEmpty() ? 0.0 : std::ceil(kKeySampleWidth + kKeySampleGap + width);
}

DiagramKey::DiagramKey(QWidget* parent)
	: QWidget(parent) {
	setObjectName("diagramKey");
	setAccessibleName("Key");
	setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	setProperty("kinds", QStringList());
}

void DiagramKey::setEntries(QVector<DiagramKeyEntry> entries) {
	m_entries = std::move(entries);
	QStringList names;
	for (const DiagramKeyEntry& entry : m_entries) names << entry.name;
	setProperty("kinds", names);
	setAccessibleDescription(names.join(QStringLiteral(", ")));
	updateGeometry();
	update();
}

QSize DiagramKey::sizeHint() const {
	return {static_cast<int>(diagramKeyWidth(m_entries, font())),
		static_cast<int>(diagramKeyRowHeight(font()) * static_cast<double>(m_entries.size()))};
}

QSize DiagramKey::minimumSizeHint() const {
	return {0, sizeHint().height()};
}

void DiagramKey::paintEvent(QPaintEvent*) {
	QPainter painter(this);
	const QColor text = palette().color(foregroundRole());
	double y = 0;
	for (const DiagramKeyEntry& entry : m_entries)
		y += paintKeyRow(painter, QRectF(0, y, width(), height() - y), entry, font(), text);
}
