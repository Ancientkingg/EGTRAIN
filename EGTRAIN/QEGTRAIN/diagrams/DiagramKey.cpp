#include "diagrams/DiagramKey.h"
#include "diagrams/TrainColors.h"

#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPaintEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace {
// The room above and below the text of a row.
constexpr double kRowPadding = 4;
constexpr double kScaleBarHeight = 14;
constexpr double kScaleTickExtend = 3;
constexpr double kScaleGap = 4;
constexpr double kScaleEntriesGap = 6;

QString percentText(double rate) {
	const double percent = rate * 100.0;
	const double rounded = std::round(percent);
	if (std::abs(percent - rounded) < 0.05)
		return QStringLiteral("%1%").arg(static_cast<int>(rounded));
	return QStringLiteral("%1%").arg(percent, 0, 'f', 1);
}

QString loadScaleHeading() {
	return QStringLiteral("On-board load (% of capacity)");
}

QString loadScaleCaption(double threshold) {
	if (threshold >= 1.0)
		return QStringLiteral("Full at 100% (thicker line)");
	return QStringLiteral("Crowded above %1 (thicker line)").arg(percentText(threshold));
}
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
				painter.setBrush(entry.brush.style() == Qt::NoBrush ? QBrush(Qt::white) : entry.brush);
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

double diagramLoadScaleHeight(const QFont& font) {
	const double line = std::ceil(QFontMetricsF(font).height());
	return line + kScaleGap + kScaleTickExtend + kScaleBarHeight + kScaleTickExtend + 2.0 + line + kScaleGap + line + kRowPadding;
}

double diagramLoadScaleWidth(const DiagramLoadScale& scale, const QFont& font) {
	const QFontMetricsF metrics(font);
	const double threshold = clampedCrowdingThreshold(scale.crowdingThreshold);
	return std::ceil(std::max({140.0, metrics.horizontalAdvance(loadScaleHeading()),
		metrics.horizontalAdvance(loadScaleCaption(threshold))}));
}

double paintLoadScale(QPainter& painter, const QRectF& area, const DiagramLoadScale& scale, const QFont& font,
	const QColor& textColour) {
	const double threshold = clampedCrowdingThreshold(scale.crowdingThreshold);
	const QFontMetricsF metrics(font);
	const double line = std::ceil(metrics.height());
	const double width = std::max(1.0, area.width());
	double y = area.top();

	painter.save();
	painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
	painter.setFont(font);
	painter.setPen(textColour);
	painter.drawText(QRectF(area.left(), y, width, line), Qt::AlignLeft | Qt::AlignVCenter,
		metrics.elidedText(loadScaleHeading(), Qt::ElideRight, width));
	y += line + kScaleGap + kScaleTickExtend;

	const QRectF bar(area.left(), y, width, kScaleBarHeight);
	const double splitX = bar.left() + bar.width() * threshold;
	if (threshold > 0.0) {
		const QRectF coolRect(bar.left(), bar.top(), std::max(0.0, splitX - bar.left()), bar.height());
		QLinearGradient cool(coolRect.left(), 0, coolRect.right(), 0);
		cool.setColorAt(0.0, passengerLoadColour(0.0, threshold, scale.darkBackground));
		cool.setColorAt(0.5, passengerLoadColour(threshold * 0.5, threshold, scale.darkBackground));
		cool.setColorAt(1.0, passengerLoadColour(threshold, threshold, scale.darkBackground));
		painter.fillRect(coolRect, cool);
	}
	if (threshold < 1.0) {
		const QRectF warmRect(splitX, bar.top(), std::max(0.0, bar.right() - splitX), bar.height());
		const double justAbove = std::min(1.0, threshold + 1e-6);
		const double midAbove = threshold + (1.0 - threshold) * 0.5;
		QLinearGradient warm(warmRect.left(), 0, warmRect.right(), 0);
		warm.setColorAt(0.0, passengerLoadColour(justAbove, threshold, scale.darkBackground));
		warm.setColorAt(0.5, passengerLoadColour(midAbove, threshold, scale.darkBackground));
		warm.setColorAt(1.0, passengerLoadColour(1.0, threshold, scale.darkBackground));
		painter.fillRect(warmRect, warm);
	}

	QColor border = textColour;
	border.setAlpha(90);
	painter.setPen(QPen(border, 1.0));
	painter.setBrush(Qt::NoBrush);
	painter.drawRect(bar.adjusted(0.5, 0.5, -0.5, -0.5));

	// Mark the crowding threshold with a high-contrast vertical tick across the bar.
	const QColor halo = scale.darkBackground ? QColor(0x18, 0x1c, 0x20, 210) : QColor(0xff, 0xff, 0xff, 210);
	painter.setPen(QPen(halo, 4.0, Qt::SolidLine, Qt::FlatCap));
	painter.drawLine(QPointF(splitX, bar.top() - kScaleTickExtend), QPointF(splitX, bar.bottom() + kScaleTickExtend));
	painter.setPen(QPen(textColour, 2.0, Qt::SolidLine, Qt::FlatCap));
	painter.drawLine(QPointF(splitX, bar.top() - kScaleTickExtend), QPointF(splitX, bar.bottom() + kScaleTickExtend));

	y = bar.bottom() + kScaleTickExtend + 2.0;
	const QString leftLabel = QStringLiteral("0%");
	const QString rightLabel = QStringLiteral("100%");
	const double leftWidth = metrics.horizontalAdvance(leftLabel);
	const double rightWidth = metrics.horizontalAdvance(rightLabel);
	painter.setPen(textColour);
	painter.drawText(QRectF(bar.left(), y, width, line), Qt::AlignLeft | Qt::AlignVCenter, leftLabel);
	painter.drawText(QRectF(bar.left(), y, width, line), Qt::AlignRight | Qt::AlignVCenter, rightLabel);

	if (threshold > 0.0 && threshold < 1.0) {
		QFont boldFont = font;
		boldFont.setBold(true);
		const QFontMetricsF boldMetrics(boldFont);
		const QString markLabel = percentText(threshold);
		const double markWidth = boldMetrics.horizontalAdvance(markLabel);
		const double markLeft = std::clamp(splitX - markWidth * 0.5, bar.left() + leftWidth + 6.0,
			bar.right() - rightWidth - 6.0 - markWidth);
		if (markLeft >= bar.left() + leftWidth + 4.0 && markLeft + markWidth <= bar.right() - rightWidth - 4.0) {
			painter.setFont(boldFont);
			painter.drawText(QRectF(markLeft, y, markWidth + 2.0, line), Qt::AlignCenter, markLabel);
		}
	}

	y += line + kScaleGap;
	QColor captionColour = textColour;
	captionColour.setAlpha(190);
	painter.setFont(font);
	painter.setPen(captionColour);
	painter.drawText(QRectF(area.left(), y, width, line), Qt::AlignLeft | Qt::AlignVCenter,
		metrics.elidedText(loadScaleCaption(threshold), Qt::ElideRight, width));
	painter.restore();
	return diagramLoadScaleHeight(font);
}

DiagramKey::DiagramKey(QWidget* parent)
	: QWidget(parent) {
	setObjectName("diagramKey");
	setAccessibleName("Key");
	setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	setProperty("kinds", QStringList());
}

void DiagramKey::updateProperties() {
	QStringList names;
	if (m_loadScale)
		names << loadScaleCaption(clampedCrowdingThreshold(m_loadScale->crowdingThreshold));
	for (const DiagramKeyEntry& entry : m_entries) names << entry.name;
	setProperty("kinds", names);
	setAccessibleDescription(names.join(QStringLiteral(", ")));
}

void DiagramKey::setEntries(QVector<DiagramKeyEntry> entries) {
	m_entries = std::move(entries);
	updateProperties();
	updateGeometry();
	update();
}

void DiagramKey::setLoadScale(std::optional<DiagramLoadScale> scale) {
	m_loadScale = scale;
	updateProperties();
	updateGeometry();
	update();
}

QSize DiagramKey::sizeHint() const {
	double w = diagramKeyWidth(m_entries, font());
	double h = diagramKeyRowHeight(font()) * static_cast<double>(m_entries.size());
	if (m_loadScale) {
		w = std::max(w, diagramLoadScaleWidth(*m_loadScale, font()));
		h += diagramLoadScaleHeight(font()) + (m_entries.isEmpty() ? 0.0 : kScaleEntriesGap);
	}
	return {static_cast<int>(std::ceil(w)), static_cast<int>(std::ceil(h))};
}

QSize DiagramKey::minimumSizeHint() const {
	return {0, sizeHint().height()};
}

void DiagramKey::paintEvent(QPaintEvent*) {
	QPainter painter(this);
	const QColor text = palette().color(foregroundRole());
	double y = 0;
	if (m_loadScale) {
		// The scale draws the variant of the colours that the lines have, also after a change of palette.
		y += paintLoadScale(painter, QRectF(0, y, width(), height() - y), *m_loadScale, font(), text);
		if (!m_entries.isEmpty()) y += kScaleEntriesGap;
	}
	for (const DiagramKeyEntry& entry : m_entries)
		y += paintKeyRow(painter, QRectF(0, y, width(), height() - y), entry, font(), text);
}
