#include "diagrams/BlockSectionOverlay.h"
#include "diagrams/ChartTicks.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QtCharts/QChart>
#include <QtCharts/QValueAxis>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

// The look of the section strip and of the break marks of the position axis.
const QColor kStripFill(238, 242, 245);
const QColor kStripRowRule(221, 228, 233);
const QColor kStripText(63, 77, 86);
const QColor kStationBar(91, 104, 113);
const QColor kSwitchHatch(160, 172, 180);
const QColor kCriticalBar(227, 162, 26);
const QColor kConflictBar(158, 27, 27);
constexpr double kStripMarkHeight = 3; // the bars of a station, a critical block and a conflict
constexpr double kStripGap = 4;		   // below the strip
constexpr double kBreakMarkReach = 7;  // how near the pointer must be to a break mark for its hover

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
	int routeSectionCount, const QString& route, const SceneSectionLabels* labels, const QFont& font, const QPalette& palette)
	: QGraphicsObject(chart), m_chart(chart), m_sections(std::move(sections)), m_marks(std::move(marks)),
	  m_route(route), m_font(font), m_strongFont(font), m_palette(palette) {
	m_strongFont.setBold(true);
	m_marks.resize(m_sections.size());
	// For the tests: how many sections the strip marks as station sections.
	setProperty("stationSections", static_cast<int>(std::count_if(m_marks.begin(), m_marks.end(), [](const BlockSectionMarks& marks) { return marks.station; })));
	m_routeSectionCount = routeSectionCount > 0 ? routeSectionCount : static_cast<int>(m_sections.size());
	// Only the labels of the shown sections are kept, so the overlay does not hold those of the whole scene.
	if (labels)
		for (const BlockSectionExtent& section : m_sections)
			m_labels.byReference.emplace(section.id, labels->label(section.id));
	if (const auto bounds = blockSectionBounds(m_sections)) {
		m_fullLow = bounds->first;
		m_fullHigh = bounds->second;
	}
	QObject::setParent(chart);
	setObjectName("blockSectionOverlay");
	setAcceptedMouseButtons(Qt::NoButton);
	setZValue(100);
	setProperty("sectionCount", static_cast<int>(m_sections.size()));
	m_notice = m_sections.empty() ? "No section boundaries for this route" : QString::fromStdString(blockSectionGeometryNote(m_sections));
	setProperty("geometryNotice", m_notice);
	m_boundaries = new UnderSeriesLayer(chart, this, QStringLiteral("blockBoundaryLines"), [this](QPainter* painter) { paintBoundaries(painter); });
	connect(chart, &QChart::plotAreaChanged, this, [this] {
		prepareGeometryChange();
		m_bounds = m_chart->boundingRect();
		// Zooming can change the width of the axis labels and with it that of the plot, which must not change the rows.
		if (m_chart->size() != m_rowsChartSize) chooseRows();
		update();
	});
	m_bounds = chart->boundingRect();
	chooseRows();
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

void BlockSectionOverlay::setFullRange(double low, double high) {
	m_fullLow = low;
	m_fullHigh = high;
	chooseRows();
	refresh();
}

void BlockSectionOverlay::setJumps(std::vector<PositionScale::Jump> jumps) {
	m_jumps = std::move(jumps);
	update();
}

bool BlockSectionOverlay::drawsBoundaries() const {
	return std::any_of(m_sections.begin(), m_sections.end(), [](const BlockSectionExtent& section) { return section.geometryValid; });
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
	if (drawsBoundaries()) entries.push_back({QStringLiteral("Block boundary"), DiagramKeyEntry::Shape::Tick, boundaryPen(), QBrush()});
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
	const QRectF header = headerRect();
	if (!plot.contains(point) && !header.contains(point)) return {};
	const bool inStrip = !m_sections.empty() && header.contains(point) && point.y() < header.top() + stripHeight();
	const bool inNotice = header.contains(point) && !m_notice.isEmpty() && !inStrip;
	if (headerOnly && !inStrip && !inNotice) return {};
	QStringList details;
	const BlockSectionExtent* labelHit = nullptr;
	if (inStrip)
		for (const auto& label : renderedLabels())
			if (label.rect.contains(point)) {
				labelHit = label.section;
				break;
			}
	if (axis)
		for (std::size_t i = 0; i < m_sections.size(); ++i) {
			const BlockSectionExtent& section = m_sections[i];
			if (!section.geometryValid) continue;
			const double start = pixelX(section.startKm, axis), end = pixelX(section.endKm, axis);
			// A visible label wins over neighbouring cells; coincident occurrences remain inspectable.
			const bool labelMatches = labelHit && section.lowerKm() == labelHit->lowerKm() && section.upperKm() == labelHit->upperKm();
			if ((inStrip && (labelHit ? labelMatches : point.x() >= std::min(start, end) - 3 && point.x() <= std::max(start, end) + 3))
				|| (!headerOnly && plot.contains(point) && std::min(std::abs(point.x() - start), std::abs(point.x() - end)) <= 4))
				details << sectionDetails(i);
		}
	if (inNotice) {
		details << m_notice;
		for (const auto& section : m_sections)
			if (!section.geometryValid)
				details << QString("Section %1, %2: %3").arg(section.order).arg(shortId(section), QString::fromStdString(section.issue));
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
	const QRectF header = headerRect();
	painter->setClipRect(header);
	painter->fillRect(header, m_palette.brush(QPalette::Base));
	const QRectF strip(header.left(), header.top(), header.width(), stripHeight() - kStripGap);
	if (!m_sections.empty()) painter->fillRect(strip, kStripFill);
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
	if (m_rows > 1 && !m_sections.empty()) {
		painter->setPen(QPen(kStripRowRule, 1));
		painter->drawLine(QPointF(strip.left(), strip.top() + rowHeight()), QPointF(strip.right(), strip.top() + rowHeight()));
	}
	const std::vector<Label> labels = renderedLabels();
	QStringList shown;
	for (const auto& label : labels) {
		shown << label.text;
		painter->setFont(label.strong ? m_strongFont : m_font);
		painter->setPen(label.strong ? kStripStrongText : kStripText);
		painter->drawText(label.rect, Qt::AlignCenter, label.text);
	}
	setProperty("stripLabels", shown);
	painter->setFont(m_font);
	painter->setPen(QPen(m_palette.color(QPalette::Text), 1));
	const QFontMetricsF metrics(m_font);
	if (!m_notice.isEmpty()) {
		const QRectF notice(plot.left(), header.top() + stripHeight(), plot.width(), noticeHeight());
		painter->fillRect(notice, m_palette.brush(QPalette::Base));
		painter->drawText(notice.adjusted(4, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
			metrics.elidedText(m_notice, Qt::ElideRight, plot.width() - 8));
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

// The short id that the strip shows.
QString BlockSectionOverlay::shortId(const BlockSectionExtent& section) {
	return QString::fromStdString(shortBlockId(section.id));
}

// The hover of a section, one fact per line: the id, the label, the kind, the marks, its place in the route, the geo
// extent and the route distance.
QString BlockSectionOverlay::sectionDetails(std::size_t index) const {
	const BlockSectionExtent& section = m_sections[index];
	const BlockSectionMarks& marks = m_marks[index];
	const QString id = shortId(section);
	QStringList lines{id};
	// A section without a label of its own gets none.
	if (const std::string label = readableSectionLabel(section.id, m_labels); !label.empty() && label != id.toStdString())
		lines << QString::fromStdString(label);
	lines << (section.isSwitch && marks.station ? QStringLiteral("Switch section at a station")
			: section.isSwitch					? QStringLiteral("Switch section")
			: marks.station						? QStringLiteral("Station section")
												: QStringLiteral("Block section"));
	if (marks.critical) lines << QStringLiteral("Critical block");
	if (marks.conflict) lines << QStringLiteral("Conflict");
	lines << QString("Section %1 of %2").arg(section.order).arg(m_routeSectionCount);
	lines << QString("Position %1 to %2 km").arg(section.geoStartKm, 0, 'f', 3).arg(section.geoEndKm, 0, 'f', 3);
	lines << QString("%1 to %2 km along %3").arg(section.startKm, 0, 'f', 3).arg(section.endKm, 0, 'f', 3).arg(m_route);
	return lines.join('\n');
}

QRectF BlockSectionOverlay::headerRect() const {
	const QRectF plot = m_chart->plotArea();
	return QRectF(plot.left(), plot.top() - headerHeight(), plot.width(), headerHeight());
}

// The ids that the strip shows for the distance range low to high across the plot, in `rows` rows from the top of the
// strip (placedStripLabels). `candidates` gets the number of ids that could be shown.
std::vector<BlockSectionOverlay::Label> BlockSectionOverlay::placedLabels(double low, double high, bool reverse, int rows, std::size_t* candidates) const {
	std::vector<Label> placed;
	if (candidates) *candidates = 0;
	if (!(high > low)) return placed;
	const QRectF plot = m_chart->plotArea();
	const auto xAt = [&](double km) {
		const double fraction = (km - low) / (high - low);
		return plot.left() + (reverse ? 1 - fraction : fraction) * plot.width();
	};
	const auto idWidth = [this](const BlockSectionExtent& section, bool strong) {
		return QFontMetricsF(strong ? m_strongFont : m_font).horizontalAdvance(shortId(section));
	};
	for (const StripLabel& label : placedStripLabels(m_sections, m_marks, plot, headerRect().top(), rowHeight(), rows, xAt, idWidth, candidates)) {
		const BlockSectionExtent& section = m_sections[label.section];
		placed.push_back({&section, label.rect, shortId(section), label.strong});
	}
	return placed;
}

// Painting and inspection use the same live screen-space rectangles.
std::vector<BlockSectionOverlay::Label> BlockSectionOverlay::renderedLabels() const {
	const auto* axis = distanceAxis();
	return axis ? placedLabels(axis->min(), axis->max(), axis->isReverse(), m_rows) : std::vector<Label>();
}

// One row when every id fits at the full view of the chart, else two, so that zooming keeps the rows. A new size of
// the chart chooses again.
void BlockSectionOverlay::chooseRows() {
	const auto* axis = distanceAxis();
	std::size_t candidates = 0;
	const std::size_t fitting = axis ? placedLabels(m_fullLow, m_fullHigh, axis->isReverse(), 1, &candidates).size() : 0;
	const int rows = fitting < candidates ? 2 : 1;
	m_rowsChartSize = m_chart->size();
	setProperty("stripRows", rows);
	if (rows != m_rows) {
		prepareGeometryChange();
		m_rows = rows;
		if (m_rowsChanged) m_rowsChanged();
	}
	setProperty("stripHeight", stripHeight());
	setProperty("headerHeight", headerHeight());
}

QValueAxis* BlockSectionOverlay::distanceAxis() const {
	const auto axes = m_chart->axes(Qt::Horizontal);
	return axes.isEmpty() ? nullptr : qobject_cast<QValueAxis*>(axes.first());
}

double BlockSectionOverlay::rowHeight() const {
	return QFontMetricsF(m_font).height() + 6;
}

double BlockSectionOverlay::stripHeight() const {
	return m_sections.empty() ? 0.0 : static_cast<double>(m_rows) * rowHeight() + kStripGap;
}

double BlockSectionOverlay::noticeHeight() const {
	return m_notice.isEmpty() ? 0.0 : QFontMetricsF(m_font).height() + 10;
}

double BlockSectionOverlay::pixelX(double km, const QValueAxis* axis) const {
	const QRectF plot = m_chart->plotArea();
	const double fraction = (km - axis->min()) / (axis->max() - axis->min());
	return plot.left() + (axis->isReverse() ? 1 - fraction : fraction) * plot.width();
}
