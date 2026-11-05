#include "diagrams/BlockSectionOverlay.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QtCharts/QChart>
#include <QtCharts/QValueAxis>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace {

// The look of the section strip and of the break marks of the position axis.
const QColor kStripFill(238, 242, 245);
const QColor kStationBar(91, 104, 113);
const QColor kSwitchHatch(160, 172, 180);
const QColor kCriticalBar(227, 162, 26);
const QColor kConflictBar(158, 27, 27);
constexpr double kStripMarkHeight = 3; // the bars of a station, a critical block and a conflict
constexpr double kStripGap = 4;		   // below the strip
constexpr double kBreakMarkReach = 7;  // how near the pointer must be to a break mark for its hover
constexpr double kCellReach = 3;	   // how near the pointer must be to a cell of the strip for its hover

// The layer under the series of a chart that `paint` draws in, inside the plot. QChart stacks its grid at 2, its axes at 3
// and its series at 4.
class UnderSeriesLayer : public QGraphicsObject {
public:
	UnderSeriesLayer(QChart* chart, QObject* owner, const QString& name, std::function<void(QPainter*)> paint)
		: QGraphicsObject(chart), m_chart(chart), m_paint(std::move(paint)) {
		QObject::setParent(owner);
		setObjectName(name);
		setAcceptedMouseButtons(Qt::NoButton);
		setZValue(3.5);
		connect(chart, &QChart::plotAreaChanged, this, [this] {
			prepareGeometryChange();
			update();
		});
	}
	QRectF boundingRect() const override { return m_chart->plotArea(); }
	void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override { m_paint(painter); }

private:
	QChart* m_chart;
	std::function<void(QPainter*)> m_paint;
};

} // namespace

BlockSectionOverlay::BlockSectionOverlay(QChart* chart, std::vector<BlockSectionExtent> sections, std::vector<BlockSectionMarks> marks,
	const QFont& font, const QPalette& palette)
	: QGraphicsObject(chart), m_chart(chart), m_sections(std::move(sections)), m_marks(std::move(marks)), m_palette(palette) {
	m_marks.resize(m_sections.size());
	m_headerHeight = m_sections.empty() ? 0.0 : QFontMetricsF(font).height() + 6 + kStripGap;
	// For the tests: how many sections the strip marks as station sections.
	setProperty("stationSections", static_cast<int>(std::count_if(m_marks.begin(), m_marks.end(), [](const BlockSectionMarks& marks) { return marks.station; })));
	QObject::setParent(chart);
	setObjectName("blockSectionOverlay");
	setAcceptedMouseButtons(Qt::NoButton);
	setZValue(100);
	setProperty("sectionCount", static_cast<int>(m_sections.size()));
	setProperty("headerHeight", m_headerHeight);
	m_boundaries = new UnderSeriesLayer(chart, this, QStringLiteral("blockBoundaryLines"), [this](QPainter* painter) { paintBoundaries(painter); });
	connect(chart, &QChart::plotAreaChanged, this, [this] {
		prepareGeometryChange();
		m_bounds = m_chart->boundingRect();
		update();
	});
	m_bounds = chart->boundingRect();
	watchAxes();
}

void BlockSectionOverlay::watchAxes() {
	for (auto* axis : m_chart->axes()) {
		if (auto* value = qobject_cast<QValueAxis*>(axis)) {
			// Time-axis replacement changes chart layout as well as ranges.
			if (m_axes.contains(value)) continue;
			m_axes.insert(value);
			connect(value, &QValueAxis::rangeChanged, this, [this] { refresh(); });
			connect(value, &QAbstractAxis::reverseChanged, this, [this] { refresh(); });
			connect(value, &QObject::destroyed, this, [this, value] { m_axes.remove(value); });
		}
	}
	refresh();
}

// Paints the strip and the layer of the boundaries again.
void BlockSectionOverlay::refresh() {
	update();
	m_boundaries->update();
}

void BlockSectionOverlay::setJumps(std::vector<PositionScale::Jump> jumps) {
	m_jumps = std::move(jumps);
	update();
}

QVector<DiagramKeyEntry> BlockSectionOverlay::keyEntries() const {
	bool station = false, switchSection = false;
	for (std::size_t i = 0; i < m_sections.size(); ++i)
		if (m_sections[i].geometryValid) {
			station = station || m_marks[i].station;
			switchSection = switchSection || m_sections[i].isSwitch;
		}
	QVector<DiagramKeyEntry> entries;
	if (station) entries.push_back({QStringLiteral("Station section"), DiagramKeyEntry::Shape::Cell, QPen(kStationBar), QBrush(), kStripFill});
	if (switchSection)
		entries.push_back({QStringLiteral("Switch section"), DiagramKeyEntry::Shape::Cell, QPen(Qt::NoPen),
			QBrush(kSwitchHatch, Qt::BDiagPattern), kStripFill});
	return entries;
}

QString BlockSectionOverlay::inspect(const QPointF& point, bool headerOnly) const {
	const auto* axis = distanceAxis();
	const QRectF plot = m_chart->plotArea();
	if (axis)
		for (const PositionScale::Jump& jump : m_jumps) {
			const double x = pixelX(jump.routeKm, axis);
			if (x >= plot.left() && x <= plot.right() && std::abs(point.x() - x) <= kBreakMarkReach
				&& std::abs(point.y() - plot.bottom()) <= kBreakMarkReach)
				return QString("Position jumps\n%1 km continues at %2 km").arg(jump.geoBeforeKm, 0, 'f', 3).arg(jump.geoAfterKm, 0, 'f', 3);
		}
	const bool inStrip = !m_sections.empty() && headerRect().contains(point);
	if (!axis || (!inStrip && (headerOnly || !plot.contains(point)))) return {};
	QStringList details;
	if (inStrip) {
		// The cell under the pointer, or the nearest within kCellReach, so that a narrow cell can be hovered too. Coincident
		// sections lie at the same distance.
		double nearest = kCellReach;
		std::vector<std::size_t> hits;
		for (std::size_t i = 0; i < m_sections.size(); ++i) {
			const BlockSectionExtent& section = m_sections[i];
			if (!section.geometryValid) continue;
			const double start = pixelX(section.startKm, axis), end = pixelX(section.endKm, axis);
			const double distance = std::max({0.0, std::min(start, end) - point.x(), point.x() - std::max(start, end)});
			if (distance < nearest) {
				nearest = distance;
				hits.clear();
			}
			if (distance == nearest) hits.push_back(i);
		}
		for (const std::size_t i : hits) details << sectionDetails(i);
	} else {
		for (std::size_t i = 0; i < m_sections.size(); ++i) {
			const BlockSectionExtent& section = m_sections[i];
			if (!section.geometryValid) continue;
			const double start = pixelX(section.startKm, axis), end = pixelX(section.endKm, axis);
			if (std::min(std::abs(point.x() - start), std::abs(point.x() - end)) <= 4) details << sectionDetails(i);
		}
	}
	return details.join('\n');
}

// The dotted boundaries of the sections in the plot, under the series.
void BlockSectionOverlay::paintBoundaries(QPainter* painter) const {
	const QRectF plot = m_chart->plotArea();
	const auto* axis = distanceAxis();
	if (!axis) return;
	painter->save();
	painter->setClipRect(plot);
	painter->setPen(boundaryPen());
	for (const auto& section : m_sections) {
		if (!section.geometryValid) continue;
		for (double position : {section.startKm, section.endKm}) {
			const double x = pixelX(position, axis);
			painter->drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
		}
	}
	painter->restore();
}

void BlockSectionOverlay::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
	const QRectF plot = m_chart->plotArea();
	const auto* axis = distanceAxis();
	painter->save();
	if (!m_sections.empty()) {
		const QRectF header = headerRect();
		painter->setClipRect(header);
		painter->fillRect(header, m_palette.brush(QPalette::Base));
		const QRectF strip(header.left(), header.top(), header.width(), header.height() - kStripGap);
		painter->fillRect(strip, kStripFill);
		if (axis)
			for (std::size_t i = 0; i < m_sections.size(); ++i) {
				const BlockSectionExtent& section = m_sections[i];
				if (!section.geometryValid) continue;
				const double start = pixelX(section.startKm, axis), end = pixelX(section.endKm, axis);
				const double left = std::max(plot.left(), std::min(start, end));
				const double right = std::min(plot.right(), std::max(start, end));
				if (right <= left) continue;
				const QRectF cell(left, strip.top(), right - left, strip.height());
				if (section.isSwitch) painter->fillRect(cell, QBrush(kSwitchHatch, Qt::BDiagPattern));
				if (m_marks[i].station) painter->fillRect(QRectF(left, strip.top(), right - left, kStripMarkHeight), kStationBar);
				if (m_marks[i].critical)
					painter->fillRect(QRectF(left, strip.bottom() - kStripMarkHeight, right - left, kStripMarkHeight), kCriticalBar);
				if (m_marks[i].conflict)
					painter->fillRect(QRectF(left, strip.bottom() - kStripMarkHeight, right - left, kStripMarkHeight), kConflictBar);
				painter->setPen(QPen(kStripRule, 1));
				for (double x : {start, end})
					if (x >= plot.left() && x <= plot.right()) painter->drawLine(QPointF(x, strip.top()), QPointF(x, strip.bottom()));
			}
	}
	// The break marks interrupt the bottom edge of the plot, so they paint over the axis line.
	if (axis && !m_jumps.empty()) {
		painter->setClipRect(QRectF(plot.left() - kBreakMarkReach, plot.bottom() - kBreakMarkReach, plot.width() + 2 * kBreakMarkReach,
			2 * kBreakMarkReach));
		QColor line = m_palette.color(QPalette::WindowText);
		line.setAlpha(160);
		for (const PositionScale::Jump& jump : m_jumps) {
			const double x = pixelX(jump.routeKm, axis);
			if (x < plot.left() || x > plot.right()) continue;
			const double y = plot.bottom();
			painter->fillRect(QRectF(x - 4, y - 6, 8, 12), m_palette.brush(QPalette::Base));
			painter->setPen(QPen(line, 1.3));
			painter->drawLine(QPointF(x - 5, y + 5), QPointF(x - 1, y - 5));
			painter->drawLine(QPointF(x + 1, y + 5), QPointF(x + 5, y - 5));
		}
	}
	painter->restore();
}

// The dotted line of a section boundary in the plot.
QPen BlockSectionOverlay::boundaryPen() const {
	QColor stroke = m_palette.color(QPalette::Text);
	stroke.setAlpha(100);
	return QPen(stroke, 1, Qt::DotLine);
}

// The hover of a section: its short id, then its geo extent.
QString BlockSectionOverlay::sectionDetails(std::size_t index) const {
	const BlockSectionExtent& section = m_sections[index];
	return QString::fromStdString(shortBlockId(section.id)) + '\n'
		+ QString("Position %1 to %2 km").arg(section.geoStartKm, 0, 'f', 3).arg(section.geoEndKm, 0, 'f', 3);
}

// The strip and the gap below it, above the plot.
QRectF BlockSectionOverlay::headerRect() const {
	const QRectF plot = m_chart->plotArea();
	return QRectF(plot.left(), plot.top() - m_headerHeight, plot.width(), m_headerHeight);
}

QValueAxis* BlockSectionOverlay::distanceAxis() const {
	const auto axes = m_chart->axes(Qt::Horizontal);
	return axes.isEmpty() ? nullptr : qobject_cast<QValueAxis*>(axes.first());
}

double BlockSectionOverlay::pixelX(double km, const QValueAxis* axis) const {
	const QRectF plot = m_chart->plotArea();
	const double fraction = (km - axis->min()) / (axis->max() - axis->min());
	return plot.left() + (axis->isReverse() ? 1 - fraction : fraction) * plot.width();
}
