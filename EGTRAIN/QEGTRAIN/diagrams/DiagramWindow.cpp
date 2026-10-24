#include "diagrams/DiagramWindow.h"
#include "app/DialogFolders.h"
#include "diagrams/ResultFrame.h"
#include "diagrams/TrainColors.h"
#include "diagrams/TrainKeyList.h"
#include "scene/SectionLabels.h"
#include "util/TimeFormat.h"
#include "widgets/DialogLayout.h"
#include "widgets/PlainToolTip.h"

#include <QApplication>
#include <QClipboard>
#include <QGraphicsLayout>
#include <QGraphicsObject>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QTextDocument>
#include <QSet>
#include <QFontMetricsF>
#include <QBuffer>
#include <QWheelEvent>
#include <QNativeGestureEvent>
#include <QGestureEvent>
#include <QKeyEvent>
#include <QPinchGesture>
#include <QShortcut>
#include <QStatusBar>
#include <QToolButton>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QAreaSeries>
#include <algorithm>
#include <QColor>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QTreeWidget>
#include <QScreen>
#include <QVariant>
#include <QVBoxLayout>
#include <QtCharts/QAbstractSeries>
#include <QtCharts/QCategoryAxis>
#include <QtCharts/QLegend>
#include <QtCharts/QValueAxis>
#include <QtCharts/QXYSeries>

#include <cmath>
#include <limits>
namespace {

// A colour of something that is not the pinned train: mixed with the background as an alpha of 60 would show it, and
// opaque, because a chart draws a solid line in short pieces, and on a dense line their overlaps would add a
// transparent colour up to nearly the full colour.
QColor mutedColour(const QColor& color, const QColor& background) {
	const auto mix = [](int line, int back) { return (line * 60 + back * 195 + 127) / 255; };
	return QColor(mix(color.red(), background.red()), mix(color.green(), background.green()), mix(color.blue(), background.blue()));
}

// Pen for lines that are not the pinned train.
QPen mutedPen(const QPen& base, const QColor& background) {
	QPen pen = base;
	pen.setColor(mutedColour(base.color(), background));
	return pen;
}

bool writeArtifact(const QString& path, const std::string& bytes) {
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly))
		return false;
	const QByteArray data = QByteArray::fromStdString(bytes);
	return file.write(data) == data.size() && file.commit();
}

// Clip a stroke to the visible plot before hit-testing. The returned fractions
// still refer to its original endpoints, so inspection keeps the sample index.
bool clippedSegment(const QRectF& plot, const QPointF& a, const QPointF& b,
	double& first, double& last) {
	first = 0;
	last = 1;
	const QPointF d = b - a;
	const auto edge = [&](double p, double q) {
		if (p == 0) return q >= 0;
		const double t = q / p;
		if (p < 0) first = std::max(first, t);
		else last = std::min(last, t);
		return first <= last;
	};
	return edge(-d.x(), a.x() - plot.left()) && edge(d.x(), plot.right() - a.x())
		&& edge(-d.y(), a.y() - plot.top()) && edge(d.y(), plot.bottom() - a.y());
}

// Emphasised pen for the pinned train.
QPen emphasisedPen(const QPen& base) {
	QPen pen = base;
	QColor color = pen.color();
	color.setAlpha(255);
	pen.setColor(color);
	pen.setWidthF(base.widthF() + 1.5);
	return pen;
}

// Planned series are drawn dashed and have their own Key entry; both rules use this one test: the "planned" property of
// the series, or its name.
bool isPlannedSeries(const QAbstractSeries* series) {
	return series->property("planned").toBool() || series->name().contains("planned", Qt::CaseInsensitive);
}

// The planned points of a series are open circles, as wide as this, in the colour of its pen on white.
constexpr double kPlannedMarkerSize = 7;

// The open circle that a planned series draws at its points: a ring of 1.4 px in the colour of `pen` around `fill`, both
// with the alpha of the pen. At two pixels per pixel, for a screen of two.
QImage plannedMarker(const QPen& pen, const QColor& fill = Qt::white) {
	constexpr qreal ratio = 2;
	QImage image(QSize(qRound(kPlannedMarkerSize * ratio), qRound(kPlannedMarkerSize * ratio)), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio);
	image.fill(Qt::transparent);
	QPainter painter(&image);
	painter.setRenderHint(QPainter::Antialiasing);
	QColor inside = fill;
	inside.setAlpha(pen.color().alpha());
	painter.setPen(QPen(pen.color(), 1.4));
	painter.setBrush(inside);
	painter.drawEllipse(QRectF(0.7, 0.7, kPlannedMarkerSize - 1.4, kPlannedMarkerSize - 1.4));
	return image;
}

constexpr QMargins kChartMargins(12, 8, 12, 8);

// Inside a box a click selects a line rather than the box only within this many pixels of the line: the half width of an
// emphasised line of 3.5 px and room for the pointer. Further from the line the click is on the box.
constexpr double kLineOverBox = 4;

// Puts the ticks of a numeric axis on round values: the multiples of 1, 2 or 5 times a power of ten that divide its range
// into at most eight steps, labelled with as many decimals as the step has.
void roundTicks(QValueAxis* axis) {
	const double low = axis->min();
	const double high = axis->max();
	const double span = high - low;
	if (!(span > 0.0) || !std::isfinite(span)) return;
	const double power = std::pow(10.0, std::floor(std::log10(span / 8)));
	double step = power;
	for (const double factor : {2.0, 5.0, 10.0})
		if (span / step > 8) step = factor * power;
	axis->setTickType(QValueAxis::TicksDynamic);
	// Qt reaches each tick by adding the interval to the one before. The interval is the step cut to 31 significant bits,
	// a hair shorter, so the sums from an anchor at zero are exact and zero reads "0.0", not "-0.0", and each tick lies a
	// hair nearer the anchor than its round value, so an end of the range on a round value keeps its tick. The anchor is
	// zero when the range holds it, else a round value inside the range.
	const int exponent = std::ilogb(step) - 30;
	axis->setTickInterval(std::ldexp(std::floor(std::ldexp(step, -exponent)), exponent));
	axis->setTickAnchor(low <= 0 && high >= 0 ? 0.0 : std::round((low + high) / 2 / step) * step);
	const int decimals = std::max(0, static_cast<int>(-std::floor(std::log10(step) + 1e-9)));
	axis->setLabelFormat(QStringLiteral("%.%1f").arg(decimals));
}

// Widens the range of a numeric axis to round ticks past its ends, so that both ends carry a label and a line at an end
// keeps clear of the plot edge. Each end moves out to the first tick beyond it; an end within a thousandth of the range of
// a tick counts as on it. A range that starts at zero keeps zero as its start, also when it starts a little below zero, as
// a chart builder's margin of 0.01 km does: up to a thousandth of the range or a tenth of the step. The step is the
// smallest one that keeps the widened range to eight steps, which is the step that roundTicks gives the widened range.
void widenToRoundTicks(QValueAxis* axis) {
	const double low = axis->min();
	const double high = axis->max();
	const double span = high - low;
	if (!(span > 0.0) || !std::isfinite(span)) return;
	const double hair = span / 1000;
	const double power = std::pow(10.0, std::floor(std::log10(span / 8)));
	for (const double factor : {1.0, 2.0, 5.0, 10.0, 20.0}) {
		const double step = factor * power;
		double start = std::floor((low - hair) / step) * step;
		if (low >= -std::max(hair, step / 10)) start = std::max(start, 0.0);
		const double end = std::ceil((high + hair) / step) * step;
		if (std::lround((end - start) / step) <= 8) {
			axis->setRange(start, end);
			return;
		}
	}
}

// The look of the section strip and of the break marks of the position axis.
const QColor kStripFill(238, 242, 245);
const QColor kStripRule(201, 211, 219);
const QColor kStripRowRule(221, 228, 233);
const QColor kStripText(63, 77, 86);
const QColor kStripStrongText(36, 49, 58);
const QColor kStationBar(91, 104, 113);
const QColor kSwitchHatch(160, 172, 180);
const QColor kCriticalBar(227, 162, 26);
const QColor kConflictBar(158, 27, 27);
constexpr double kStripLabelGap = 6;   // between two ids in a row of the strip
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

// One chart-owned item, outside the train series/filter identity space. Geometry is mapped from the live distance axis,
// which may be the position axis; font sizes and the strip stay in chart pixels. It draws the strip of the sections above
// the plot and the break marks of the position axis on the bottom edge of the plot over the series, and the boundaries of
// the sections in the plot under the series, in a layer of its own.
class BlockSectionOverlay : public QGraphicsObject {
public:
	BlockSectionOverlay(QChart* chart, std::vector<BlockSectionExtent> sections, std::vector<BlockSectionMarks> marks,
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
	QRectF boundingRect() const override { return m_bounds; }
	void watchAxes() {
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
	void refresh() {
		update();
		m_boundaries->update();
	}
	// The range of the distance axis at the full view of the chart, which decides the rows of the strip.
	void setFullRange(double low, double high) {
		m_fullLow = low;
		m_fullHigh = high;
		chooseRows();
		refresh();
	}
	// The jumps of the position axis, each drawn as a break mark; none without a position axis.
	void setJumps(std::vector<PositionScale::Jump> jumps) {
		m_jumps = std::move(jumps);
		update();
	}
	// Called when the strip takes another number of rows, which changes the height that it needs above the plot.
	void setRowsChanged(std::function<void()> changed) { m_rowsChanged = std::move(changed); }
	// The height above the plot: the strip, then the notice.
	double headerHeight() const { return stripHeight() + noticeHeight(); }
	bool drawsBoundaries() const {
		return std::any_of(m_sections.begin(), m_sections.end(), [](const BlockSectionExtent& section) { return section.geometryValid; });
	}
	// The rows of the Key for what the overlay draws: a cell with the bar of a station section, a hatched cell of a switch
	// section and the dotted line of a boundary, each while the strip draws one.
	QVector<DiagramKeyEntry> keyEntries() const {
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
	QString inspect(const QPointF& point, bool headerOnly) const {
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
	void paintBoundaries(QPainter* painter) const {
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
	void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override {
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
private:
	struct Label {
		const BlockSectionExtent* section;
		QRectF rect;
		QString text;
		bool strong; // a critical or conflict section, drawn in bold
	};
	// The dotted line of a section boundary in the plot.
	QPen boundaryPen() const {
		QColor stroke = m_palette.color(QPalette::Text);
		stroke.setAlpha(100);
		return QPen(stroke, 1, Qt::DotLine);
	}
	// The short id that the strip shows.
	static QString shortId(const BlockSectionExtent& section) {
		return QString::fromStdString(shortBlockId(section.id));
	}
	// The hover of a section, one fact per line: the id, the label, the kind, the marks, its place in the route, the geo
	// extent and the route distance.
	QString sectionDetails(std::size_t index) const {
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
	QRectF headerRect() const {
		const QRectF plot = m_chart->plotArea();
		return QRectF(plot.left(), plot.top() - headerHeight(), plot.width(), headerHeight());
	}
	// The ids that the strip shows for the distance range low to high across the plot, in `rows` rows from the top of
	// the strip. Each id is centred in the part of its cell that lies in the plot and kept inside the plot, and goes in the
	// first row where it keeps clear of the ids placed there. The ids are placed in route order when that names every
	// section. Otherwise the ids of critical and conflict sections are placed first, then those of the first and last
	// section, of switch sections and of station sections, then the others in route order, and an id that keeps clear in
	// no row is left out. `candidates` gets the number of ids that could be shown.
	std::vector<Label> placedLabels(double low, double high, bool reverse, int rows, std::size_t* candidates = nullptr) const {
		std::vector<Label> placed;
		if (candidates) *candidates = 0;
		const QRectF plot = m_chart->plotArea();
		if (plot.width() <= 8 || !(high > low)) return placed;
		const auto xAt = [&](double km) {
			const double fraction = (km - low) / (high - low);
			return plot.left() + (reverse ? 1 - fraction : fraction) * plot.width();
		};
		struct Candidate {
			int priority;
			std::size_t index;
			double centre;
			double width;
		};
		std::vector<Candidate> found;
		std::size_t first = m_sections.size(), last = 0;
		for (std::size_t i = 0; i < m_sections.size(); ++i)
			if (m_sections[i].geometryValid) {
				first = std::min(first, i);
				last = i;
			}
		for (std::size_t i = 0; i < m_sections.size(); ++i) {
			const BlockSectionExtent& section = m_sections[i];
			if (!section.geometryValid) continue;
			const double left = std::max(plot.left(), std::min(xAt(section.startKm), xAt(section.endKm)));
			const double right = std::min(plot.right(), std::max(xAt(section.startKm), xAt(section.endKm)));
			if (right <= left) continue;
			const bool strong = m_marks[i].critical || m_marks[i].conflict;
			const double width = QFontMetricsF(strong ? m_strongFont : m_font).horizontalAdvance(shortId(section));
			if (width > plot.width()) continue;
			int priority = 4;
			if (strong) priority = 0;
			else if (i == first || i == last) priority = 1;
			else if (section.isSwitch) priority = 2;
			else if (m_marks[i].station) priority = 3;
			found.push_back({priority, i, std::clamp((left + right) / 2, plot.left() + width / 2, plot.right() - width / 2), width});
		}
		if (candidates) *candidates = found.size();
		const double top = headerRect().top();
		const auto place = [&] {
			placed.clear();
			std::vector<std::vector<std::pair<double, double>>> taken(static_cast<std::size_t>(std::max(1, rows)));
			for (const Candidate& candidate : found) {
				const double from = candidate.centre - candidate.width / 2, to = candidate.centre + candidate.width / 2;
				for (std::size_t row = 0; row < taken.size(); ++row) {
					const bool clear = std::all_of(taken[row].begin(), taken[row].end(), [&](const std::pair<double, double>& other) {
						return to + kStripLabelGap <= other.first || from - kStripLabelGap >= other.second;
					});
					if (!clear) continue;
					taken[row].emplace_back(from, to);
					const BlockSectionExtent& section = m_sections[candidate.index];
					placed.push_back({&section, QRectF(from, top + static_cast<double>(row) * rowHeight(), candidate.width, rowHeight()),
						shortId(section), m_marks[candidate.index].critical || m_marks[candidate.index].conflict});
					break;
				}
			}
		};
		place();
		if (placed.size() == found.size()) return placed;
		std::stable_sort(found.begin(), found.end(), [](const Candidate& a, const Candidate& b) { return a.priority < b.priority; });
		place();
		return placed;
	}
	// Painting and inspection use the same live screen-space rectangles.
	std::vector<Label> renderedLabels() const {
		const auto* axis = distanceAxis();
		return axis ? placedLabels(axis->min(), axis->max(), axis->isReverse(), m_rows) : std::vector<Label>();
	}
	// One row when every id fits at the full view of the chart, else two, so that zooming keeps the rows. A new size of
	// the chart chooses again.
	void chooseRows() {
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
	QValueAxis* distanceAxis() const {
		const auto axes = m_chart->axes(Qt::Horizontal);
		return axes.isEmpty() ? nullptr : qobject_cast<QValueAxis*>(axes.first());
	}
	double rowHeight() const { return QFontMetricsF(m_font).height() + 6; }
	double stripHeight() const { return m_sections.empty() ? 0.0 : static_cast<double>(m_rows) * rowHeight() + kStripGap; }
	double noticeHeight() const { return m_notice.isEmpty() ? 0.0 : QFontMetricsF(m_font).height() + 10; }
	double pixelX(double km, const QValueAxis* axis) const {
		const QRectF plot = m_chart->plotArea();
		const double fraction = (km - axis->min()) / (axis->max() - axis->min());
		return plot.left() + (axis->isReverse() ? 1 - fraction : fraction) * plot.width();
	}
	QChart* m_chart;
	std::vector<BlockSectionExtent> m_sections;
	std::vector<BlockSectionMarks> m_marks; ///< One per section
	int m_routeSectionCount = 0;
	SceneSectionLabels m_labels; ///< Of the sections of m_sections; empty without labels
	QString m_route, m_notice;
	QFont m_font, m_strongFont;
	QPalette m_palette;
	QRectF m_bounds;
	QSet<QValueAxis*> m_axes;
	double m_fullLow = 0.0, m_fullHigh = 0.0;
	int m_rows = 1;
	QSizeF m_rowsChartSize; ///< The size of the chart when the rows were chosen
	std::vector<PositionScale::Jump> m_jumps;
	std::function<void()> m_rowsChanged;
	UnderSeriesLayer* m_boundaries = nullptr; ///< A child of the overlay
};

// The window header cannot name the file-local overlay type, so it keeps the item as a QGraphicsObject.
BlockSectionOverlay* sectionOverlayOf(const QPointer<QGraphicsObject>& overlay) {
	return static_cast<BlockSectionOverlay*>(overlay.data());
}

// A dark outline around the selected blocking time, 1.5 px outside its box as the chart maps the box at the moment, and
// cut at the edges of the plot. The rectangle it draws is in the dynamic property "outline", for the tests.
class SelectionOutline : public QGraphicsObject {
public:
	SelectionOutline(QChart* chart, QXYSeries* series)
		: QGraphicsObject(chart), m_chart(chart), m_series(series) {
		QObject::setParent(chart);
		setObjectName("diagramSelectionOutline");
		setAcceptedMouseButtons(Qt::NoButton);
		setZValue(90); // over the series, under the section strip
		connect(chart, &QChart::plotAreaChanged, this, [this] {
			prepareGeometryChange();
			update();
		});
		for (QAbstractAxis* axis : chart->axes())
			if (auto* value = qobject_cast<QValueAxis*>(axis)) connect(value, &QValueAxis::rangeChanged, this, [this] { update(); });
	}
	QRectF boundingRect() const override { return m_chart->plotArea(); }
	void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override {
		if (!m_series) return;
		QPolygonF corners;
		for (const QPointF& point : m_series->points()) corners << m_chart->mapToPosition(point, m_series);
		const QRectF outline = corners.boundingRect().adjusted(-1.5, -1.5, 1.5, 1.5);
		setProperty("outline", outline);
		painter->save();
		painter->setClipRect(m_chart->plotArea());
		painter->setRenderHint(QPainter::Antialiasing);
		painter->setPen(QPen(kStripStrongText, 2));
		painter->setBrush(Qt::NoBrush);
		painter->drawRoundedRect(outline, 1, 1);
		painter->restore();
	}

private:
	QChart* m_chart;
	QPointer<QXYSeries> m_series;
};

// The line that a chart without series shows in the middle of its plot, such as "No arrival delays at stations of route8". Its text
// is in the dynamic property "text", for the tests.
class EmptyChartNote : public QGraphicsObject {
public:
	EmptyChartNote(QChart* chart, const QString& text, const QFont& font, const QColor& colour)
		: QGraphicsObject(chart), m_chart(chart), m_text(text), m_font(font), m_colour(colour) {
		QObject::setParent(chart);
		setObjectName("diagramEmptyNote");
		setProperty("text", text);
		setAcceptedMouseButtons(Qt::NoButton);
		setZValue(100);
		connect(chart, &QChart::plotAreaChanged, this, [this] {
			prepareGeometryChange();
			update();
		});
	}
	QRectF boundingRect() const override { return m_chart->plotArea(); }
	void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override {
		painter->save();
		painter->setFont(m_font);
		painter->setPen(m_colour);
		painter->drawText(m_chart->plotArea(), Qt::AlignCenter, m_text);
		painter->restore();
	}

private:
	QChart* m_chart;
	QString m_text;
	QFont m_font;
	QColor m_colour;
};

// The order of the kinds in the Key; a kind that is not listed comes after these, in the order of the series.
const QStringList kKeyOrder{QStringLiteral("Blocking time"), QStringLiteral("Critical block"), QStringLiteral("Conflict"),
	QStringLiteral("Planned"), QStringLiteral("Simulated"), QStringLiteral("Stop"), QStringLiteral("Dwell"),
	QStringLiteral("Station section"), QStringLiteral("Switch section"), QStringLiteral("Block boundary")};

// The label each station gets above the plot, as (position, plain text). Every label is centred on its station, must lie
// within the chart and may not touch its neighbour. The chart hides a label whose text starts left of `labelsLeft`, the
// left edge of its axes. The chart shortens a label that is wider than the share of the plot
// width that it gives each of the labels, so a label is only used when it is narrower than that share. Of the stations
// in the range, every first, second, third and so on is tried until the labels fit. A station gets the longest
// spelling that fits.
std::vector<std::pair<double, QString>> labelledStations(const std::vector<StationTick>& stations, const QValueAxis& axis,
	const QRectF& plot, double labelsLeft, double chartWidth, const QFontMetricsF& metrics) {
	constexpr double edge = 4.0, gap = 8.0, slack = 16.0;
	// A pixel for the rounding of the label position.
	const double leftEdge = std::max(edge, labelsLeft + 1.0);
	struct Candidate {
		double x;
		const StationTick* station;
	};
	const double span = axis.max() - axis.min();
	if (!(span > 0.0) || plot.width() <= 0.0) return {};
	std::vector<Candidate> inRange;
	for (const StationTick& station : stations) {
		if (station.spellings.isEmpty() || station.position < axis.min() || station.position > axis.max()) continue;
		const double fraction = (station.position - axis.min()) / span;
		inRange.push_back({plot.left() + (axis.isReverse() ? 1.0 - fraction : fraction) * plot.width(), &station});
	}
	std::sort(inRange.begin(), inRange.end(), [](const Candidate& a, const Candidate& b) { return a.x < b.x; });
	struct Placed {
		double x;
		double width;
		std::pair<double, QString> label;
	};
	for (std::size_t stride = 1; stride <= inRange.size(); ++stride) {
		const std::size_t named = (inRange.size() + stride - 1) / stride;
		const double share = plot.width() / static_cast<double>(named + 1) - slack;
		std::vector<Placed> placed;
		bool fits = true;
		for (std::size_t i = 0; i < inRange.size() && fits; i += stride) {
			const double x = inRange[i].x;
			const double room = std::min(share, 2.0 * std::min(x - leftEdge, chartWidth - edge - x));
			const QStringList& spellings = inRange[i].station->spellings;
			const auto spelling = std::find_if(spellings.begin(), spellings.end(),
				[&](const QString& text) { return metrics.horizontalAdvance(text) <= room; });
			if (spelling != spellings.end()) {
				placed.push_back({x, metrics.horizontalAdvance(*spelling), {inRange[i].station->position, *spelling}});
				continue;
			}
			// A station near the edge of the chart may stay unlabelled, one that is too wide for its share may not.
			fits = metrics.horizontalAdvance(spellings.last()) <= share;
		}
		bool apart = fits && !placed.empty();
		for (std::size_t i = 1; i < placed.size(); ++i)
			apart = apart && placed[i].x - placed[i - 1].x - (placed[i].width + placed[i - 1].width) / 2.0 >= gap;
		if (!apart) continue;
		std::vector<std::pair<double, QString>> labels;
		for (const Placed& one : placed) labels.push_back(one.label);
		return labels;
	}
	// No label fits whole: the middle station keeps the start of its shortest spelling.
	if (inRange.empty()) return {};
	const StationTick& middle = *inRange[inRange.size() / 2].station;
	return {{middle.position, metrics.elidedText(middle.spellings.last(), Qt::ElideRight, plot.width() / 2.0 - slack)}};
}

// Removes `axis` from `chart` with what it drew. QChart hides the items of a removed axis only when it deletes them, on
// the next turn of the event loop, and until then they follow the visibility of the axis. So the axis hides first, and
// its old items stop following it: a picture taken before that turn shows one axis on that side, and showing the axis
// again in a chart shows only its new items.
void removeAxisNow(QChart* chart, QAbstractAxis* axis) {
	axis->hide();
	chart->removeAxis(axis);
	QObject::disconnect(axis, &QAbstractAxis::visibleChanged, nullptr, nullptr);
}

// A category axis keeps one category for each text and drops a later one with the same text, so the k-th repeat of a
// text gets k zero-width spaces, which draw nothing.
QStringList uniqueCategoryTexts(const QStringList& texts) {
	QHash<QString, int> seen;
	QStringList unique;
	for (const QString& text : texts) {
		const int repeat = seen[text]++;
		unique << text + QString(repeat, QChar(0x200B));
	}
	return unique;
}

// A category axis that takes the place of `numeric` along its edge, with its range, direction and look, and with the
// series of `numeric` attached in its place. `numeric` leaves the chart and becomes a child of `owner`, which keeps it
// for the way back; its full bounds go to the new axis.
QCategoryAxis* replaceWithCategoryAxis(QChart* chart, QValueAxis* numeric, const QString& title, QObject* owner,
	QHash<QAbstractAxis*, QPair<double, double>>& fullBounds, const QBrush& textBrush) {
	auto* category = new QCategoryAxis(chart);
	category->setLabelsPosition(QCategoryAxis::AxisLabelsPositionOnValue);
	category->setStartValue(numeric->min() - 1);
	category->setReverse(numeric->isReverse());
	category->setMin(numeric->min());
	category->setMax(numeric->max());
	category->setTitleText(title);
	category->setGridLinePen(numeric->gridLinePen());
	category->setGridLineVisible(numeric->isGridLineVisible());
	category->setMinorGridLineVisible(numeric->isMinorGridLineVisible());
	category->setLinePen(numeric->linePen());
	category->setLabelsFont(numeric->labelsFont());
	category->setTitleFont(numeric->titleFont());
	category->setLabelsBrush(textBrush);
	category->setTitleBrush(textBrush);
	QList<QAbstractSeries*> attached;
	for (auto* series : chart->series())
		if (series->attachedAxes().contains(numeric)) attached.append(series);
	const auto alignment = numeric->alignment();
	const auto bounds = fullBounds.take(numeric);
	removeAxisNow(chart, numeric);
	numeric->setParent(owner); // keep the original numeric ticks and grid for the roundtrip
	chart->addAxis(category, alignment);
	fullBounds.insert(category, bounds);
	for (QAbstractSeries* series : attached) series->attachAxis(category);
	return category;
}

// Puts `numeric` back in the place of the category axis `category`, with the range and direction the category axis has
// then, and deletes the category axis.
void restoreNumericAxis(QChart* chart, QCategoryAxis* category, QValueAxis* numeric,
	QHash<QAbstractAxis*, QPair<double, double>>& fullBounds) {
	const auto alignment = category->alignment();
	const auto bounds = fullBounds.take(category);
	numeric->setRange(category->min(), category->max());
	numeric->setReverse(category->isReverse());
	QList<QAbstractSeries*> attached;
	for (auto* series : chart->series())
		if (series->attachedAxes().contains(category)) attached.append(series);
	removeAxisNow(chart, category);
	category->deleteLater();
	numeric->show();
	chart->addAxis(numeric, alignment);
	for (auto* series : attached) series->attachAxis(numeric);
	fullBounds.insert(numeric, bounds);
}

} // namespace

DiagramWindow::DiagramWindow(const QString& title, QWidget* parent)
	: QDialog(parent) {
	setModal(false);
	setProperty("dialogPresentation", true);
	setWindowTitle(title);
	const QRect available = screen() ? screen()->availableGeometry() : QRect(0, 0, 1280, 800);
	setMaximumSize(available.width() * 9 / 10, available.height() * 4 / 5);
	resize(qMin(1200, maximumWidth()), qMin(720, maximumHeight()));

	m_view = new QChartView(this);
	// The hatches are textures at twice the pixels, which a screen of one pixel per pixel scales down smoothly.
	m_view->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
	m_view->setMouseTracking(true);
	m_view->viewport()->setMouseTracking(true);
	m_view->viewport()->installEventFilter(this);
	m_view->viewport()->grabGesture(Qt::PinchGesture);
	m_view->setFocusPolicy(Qt::StrongFocus);
	m_tooltip = new QLabel(this);
	m_tooltip->setObjectName("diagramTooltip");
	m_tooltip->setTextFormat(Qt::PlainText);
	m_tooltip->setForegroundRole(QPalette::ToolTipText);
	m_tooltip->setBackgroundRole(QPalette::ToolTipBase);
	m_tooltip->setAutoFillBackground(true);
	m_tooltip->setMargin(6);
	m_tooltip->setFrameShape(QFrame::StyledPanel);
	m_tooltip->setAttribute(Qt::WA_TransparentForMouseEvents);
	m_tooltip->hide();
	installEventFilter(this);
	m_view->setFrameShape(QFrame::NoFrame);
	const auto zoomAtCentre = [this](double factor) { navigate(factor, m_view->chart()->plotArea().center()); };
	// The shortcuts belong to the chart view, so they only take keys while the chart has focus.
	for (const auto key : {Qt::Key_Plus, Qt::Key_Equal, Qt::Key_Minus, Qt::Key_0, Qt::Key_Home,
			 Qt::Key_Left, Qt::Key_Right, Qt::Key_Up, Qt::Key_Down}) {
		auto* shortcut = new QShortcut(QKeySequence(key), m_view);
		shortcut->setContext(Qt::WidgetWithChildrenShortcut);
		connect(shortcut, &QShortcut::activated, this, [this, key, zoomAtCentre] {
			if (key == Qt::Key_0 || key == Qt::Key_Home) {
				resetZoom();
				return;
			}
			QPointF pan;
			if (key == Qt::Key_Left) pan.setX(40);
			if (key == Qt::Key_Right) pan.setX(-40);
			if (key == Qt::Key_Up) pan.setY(40);
			if (key == Qt::Key_Down) pan.setY(-40);
			if (pan.isNull())
				zoomAtCentre(key == Qt::Key_Minus ? 1 / 1.2 : 1.2);
			else
				navigate(1, m_view->chart()->plotArea().center(), pan);
		});
	}
	// Copy takes the figure while the chart has the focus; a text field with the focus keeps Copy for its text.
	auto* copy = new QShortcut(QKeySequence::Copy, m_view);
	copy->setContext(Qt::WidgetWithChildrenShortcut);
	connect(copy, &QShortcut::activated, this, &DiagramWindow::copyFigure);
	// Drag a rectangle to zoom; filter state is unaffected by zoom or pan.
	m_view->setRubberBand(QChartView::RectangleRubberBand);

	m_frame = new ResultFrame(m_view, "diagram", this);
	m_contextLabel = m_frame->contextLabel();
	// The header buttons. The wheel zooms with Control or Command held; the tooltip names the key that the platform uses.
#ifdef Q_OS_MACOS
	const QString wheelZoom = QStringLiteral("Pinch or Command-scroll to zoom at the pointer");
#else
	const QString wheelZoom = QStringLiteral("Pinch or Ctrl-scroll to zoom at the pointer");
#endif
	QToolButton* zoomOut = m_frame->addHeaderButton("diagramZoomOut", "Zoom Out", ":/icons/zoom-out.svg", "Zoom out (-)");
	connect(zoomOut, &QToolButton::clicked, this, [zoomAtCentre] { zoomAtCentre(1 / 1.2); });
	QToolButton* zoomIn = m_frame->addHeaderButton("diagramZoomIn", "Zoom In", ":/icons/zoom-in.svg",
		"Zoom in (+)\nDrag a rectangle to zoom into it\n" + wheelZoom);
	connect(zoomIn, &QToolButton::clicked, this, [zoomAtCentre] { zoomAtCentre(1.2); });
	QToolButton* fit = m_frame->addHeaderButton("diagramFit", "Fit", ":/icons/fit.svg",
		"Show the whole chart (Home or 0)\nScroll or the arrow keys to pan");
	connect(fit, &QToolButton::clicked, this, &DiagramWindow::resetZoom);

	// A warning, such as a curve cut off at 0 kN, has the look of a warning of the table windows, inside the margins of
	// the main area.
	m_warningArea = new QWidget(this);
	m_warningArea->setObjectName("diagramWarningArea");
	auto* warningLayout = new QVBoxLayout(m_warningArea);
	warningLayout->setContentsMargins(16, 8, 12, 0);
	m_warningLabel = new QLabel(m_warningArea);
	m_warningLabel->setObjectName("diagramWarning");
	m_warningLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	warningLayout->addWidget(m_warningLabel);
	DialogLayout::setStatus(m_warningLabel, DialogLayout::Severity::Warning, QString());
	QSizePolicy warningPolicy = m_warningLabel->sizePolicy();
	warningPolicy.setVerticalPolicy(QSizePolicy::Minimum);
	warningPolicy.setHeightForWidth(true);
	m_warningLabel->setSizePolicy(warningPolicy);
	m_warningArea->hide();
	m_frame->addAboveMain(m_warningArea);

	// The panel: the trains, the key, the selected item, the exports and the readout.
	ResultPanel* panel = m_frame->panel();
	m_trainList = new TrainKeyList(this);
	connect(m_trainList, &TrainKeyList::visibilityChanged, this, &DiagramWindow::applyTrainVisibility);
	m_trainsSection = panel->addSection("Trains", m_trainList, m_trainList->countLabel());

	// Shown while the chart draws a kind of mark.
	m_key = new DiagramKey(this);
	m_keySection = panel->addSection("Key", m_key);
	panel->setSectionVisible(m_keySection, false);

	m_clearPinButton = new QPushButton("Clear", this);
	m_clearPinButton->setObjectName("diagramClearSelection");
	m_clearPinButton->setAccessibleName("Clear selection");
	m_clearPinButton->setToolTip("Clear the selection (Escape)");
	connect(m_clearPinButton, &QPushButton::clicked, this, &DiagramWindow::clearPin);
	auto* selected = new QWidget(this);
	auto* selectedColumn = new QVBoxLayout(selected);
	selectedColumn->setContentsMargins(0, 0, 0, 0);
	selectedColumn->setSpacing(2);
	m_pinTitle = new QLabel(selected);
	m_pinTitle->setObjectName("diagramSelectedTitle");
	m_pinLabel = new QLabel(selected);
	m_pinLabel->setObjectName("diagramPinLabel");
	for (QLabel* label : {m_pinTitle.data(), m_pinLabel.data()}) {
		label->setTextFormat(Qt::PlainText);
		label->setWordWrap(true);
		label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		selectedColumn->addWidget(label);
	}
	QFont titleFont = m_pinTitle->font();
	titleFont.setBold(true);
	m_pinTitle->setFont(titleFont);
	// Shown only while something is selected.
	m_selectedSection = panel->addSection("Selected", selected, m_clearPinButton);
	panel->setSectionVisible(m_selectedSection, false);

	auto* exports = new QWidget(this);
	auto* exportRow = new QHBoxLayout(exports);
	exportRow->setContentsMargins(0, 0, 0, 0);
	exportRow->setSpacing(8);
	auto* pngButton = new QPushButton("PNG...", exports);
	pngButton->setObjectName("diagramExportPng");
	pngButton->setAccessibleName("Export PNG");
	pngButton->setToolTip("Save the chart as an image");
	connect(pngButton, &QPushButton::clicked, this, &DiagramWindow::exportPng);
	m_csvButton = new QPushButton("CSV...", exports);
	m_csvButton->setObjectName("diagramExportCsv");
	m_csvButton->setAccessibleName("Export CSV");
	m_csvButton->setToolTip("Save the data of the visible trains as CSV");
	// Without a CSV export the PNG button keeps its half of the row.
	QSizePolicy csvPolicy = m_csvButton->sizePolicy();
	csvPolicy.setRetainSizeWhenHidden(true);
	m_csvButton->setSizePolicy(csvPolicy);
	m_csvButton->hide();
	connect(m_csvButton, &QPushButton::clicked, this, &DiagramWindow::exportCsv);
	exportRow->addWidget(pngButton, 1);
	exportRow->addWidget(m_csvButton, 1);
	panel->addSection("Export", exports);
	m_readout = panel->readout();

	// No button of the window is a default button, so Enter presses none. The buttons of the panel lie deeper than the
	// direct children of the window, so the search takes every descendant.
	for (QPushButton* button : findChildren<QPushButton*>())
		button->setAutoDefault(false);
	// Tab goes through the header, the chart, then the panel from top to bottom.
	QList<QWidget*> order{zoomOut, zoomIn, fit, m_frame->panelButton(), m_view};
	order << m_trainList->focusChain() << m_clearPinButton << pngButton << m_csvButton;
	for (int i = 1; i < order.size(); ++i)
		QWidget::setTabOrder(order[i - 1], order[i]);
	// The window opens with the chart in focus, so that its keys work at once.
	m_view->setFocus();

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(m_frame);
}

void DiagramWindow::setPresentation(const QString& heading, const QString& context,
	const QString& warning, const QString& contextToolTip) {
	setWindowTitle(heading);
	DialogLayout::setStatus(m_warningLabel, DialogLayout::Severity::Warning, warning);
	m_warningArea->setVisible(!warning.isEmpty());
	m_frame->setContext(context, contextToolTip);
}

void DiagramWindow::setEmptyText(const QString& text) {
	m_emptyText = text;
	refreshEmptyNote();
}

// The note of an empty chart is shown while the chart has no series.
void DiagramWindow::refreshEmptyNote() {
	delete m_emptyNote.data();
	QChart* chart = m_view ? m_view->chart() : nullptr;
	if (!chart || !chart->series().isEmpty() || m_emptyText.isEmpty()) return;
	QColor colour = palette().color(QPalette::WindowText);
	colour.setAlpha(170);
	m_emptyNote = new EmptyChartNote(chart, m_emptyText, font(), colour);
}

void DiagramWindow::setFigureFileName(const QString& fileName) {
	m_figureFileName = fileName;
}

void DiagramWindow::setRollingStockSubject(bool on) {
	m_rollingStockSubject = on;
	refreshTrainsSection();
	refreshSelected();
	refreshKey();
}

// The Trains section is shown while the chart has trains, and never for rolling stock input.
void DiagramWindow::refreshTrainsSection() {
	if (m_trainsSection) m_frame->panel()->setSectionVisible(m_trainsSection, !m_rollingStockSubject && !m_groups.isEmpty());
}

// The kinds of mark that the shown series draw, as the class comment describes them, and those of the section strip.
QVector<DiagramKeyEntry> DiagramWindow::keyEntries() const {
	QVector<DiagramKeyEntry> entries;
	QChart* chart = m_view ? m_view->chart() : nullptr;
	if (!chart) return entries;
	const auto entryNamed = [&entries](const QString& name) {
		return std::find_if(entries.begin(), entries.end(), [&name](const DiagramKeyEntry& entry) { return entry.name == name; });
	};
	const auto series = chart->series();
	if (m_rollingStockSubject) {
		for (QAbstractSeries* one : series) {
			const auto* xy = qobject_cast<QXYSeries*>(one);
			if (xy && xy->isVisible() && !one->name().isEmpty() && entryNamed(one->name()) == entries.end())
				entries.push_back({one->name(), DiagramKeyEntry::Shape::Line, m_basePens.value(one, xy->pen()), QBrush()});
		}
		return entries;
	}
	// The fill of a box is the brush of the first shown area series of its kind.
	QHash<QString, QBrush> fills;
	for (QAbstractSeries* one : series)
		if (const auto* area = qobject_cast<QAreaSeries*>(one); area && area->isVisible()) {
			const QString kind = one->property("keyKind").toString();
			if (!kind.isEmpty() && !fills.contains(kind)) fills.insert(kind, m_baseBrushes.value(one, area->brush()));
		}
	const QColor text = palette().color(QPalette::WindowText);
	for (QAbstractSeries* one : series) {
		const auto* xy = qobject_cast<QXYSeries*>(one);
		if (!xy || !xy->isVisible()) continue;
		const QVariant keyKind = one->property("keyKind");
		QString name = keyKind.isValid() ? keyKind.toString() : one->property("layer").toString();
		if (!keyKind.isValid() && name.isEmpty()) name = isPlannedSeries(one) ? QStringLiteral("planned") : QStringLiteral("simulated");
		// The points of a planned series are open circles, as the chart draws them, and those of another series dots.
		const bool planned = isPlannedSeries(one);
		const QString marker = xy->pointsVisible() ? one->property("keyMarker").toString() : QString();
		if (!marker.isEmpty()) {
			const auto known = entryNamed(marker);
			if (known == entries.end())
				entries.push_back({marker, DiagramKeyEntry::Shape::Marker, QPen(text, 1.5), planned ? QBrush(Qt::white) : QBrush(text)});
			else if (!planned)
				known->brush = QBrush(text); // a kind with dots draws as a dot, also when its first series is planned
		}
		if (name.isEmpty()) continue;
		name[0] = name[0].toUpper();
		const QPen pen = m_basePens.value(one, xy->pen());
		DiagramKeyEntry entry;
		entry.name = name;
		if (one->property("inspectionFilled").toBool()) {
			// The "keyPen" of the outline, when it has one, draws the kind alike for every train.
			const QVariant keyPen = one->property("keyPen");
			entry.shape = DiagramKeyEntry::Shape::Box;
			entry.pen = keyPen.canConvert<QPen>() ? keyPen.value<QPen>() : pen;
			entry.brush = fills.value(name, QBrush(Qt::NoBrush));
		} else {
			// The colour of a line belongs to its train, so the sample takes the text colour.
			entry.shape = one->property("keyUpright").toBool() ? DiagramKeyEntry::Shape::Tick : DiagramKeyEntry::Shape::Line;
			entry.pen = QPen(text, pen.widthF(), pen.style());
			// A planned line shows its points as open circles, also those that the Key names as a kind of their own.
			entry.marker = xy->pointsVisible() && (marker.isEmpty() || planned);
			if (entry.marker && !planned) entry.brush = QBrush(text);
		}
		const auto known = entryNamed(name);
		if (known == entries.end())
			entries.push_back(entry);
		else if (entry.shape == DiagramKeyEntry::Shape::Box && known->shape != DiagramKeyEntry::Shape::Box)
			*known = entry; // a kind with a box draws as a box, also when its first series is a line of no width
	}
	// Every line is simulated unless another kind of line says otherwise.
	const auto lines = std::count_if(entries.begin(), entries.end(), [](const DiagramKeyEntry& entry) {
		return entry.shape == DiagramKeyEntry::Shape::Line;
	});
	if (lines == 1) {
		const auto simulated = entryNamed(QStringLiteral("Simulated"));
		if (simulated != entries.end() && simulated->shape == DiagramKeyEntry::Shape::Line) entries.erase(simulated);
	}
	if (const auto* overlay = sectionOverlayOf(m_sectionOverlay)) entries << overlay->keyEntries();
	std::stable_sort(entries.begin(), entries.end(), [](const DiagramKeyEntry& a, const DiagramKeyEntry& b) {
		const auto rank = [](const QString& name) {
			const auto index = kKeyOrder.indexOf(name);
			return index < 0 ? kKeyOrder.size() : index;
		};
		return rank(a.name) < rank(b.name);
	});
	return entries;
}

void DiagramWindow::refreshKey() {
	if (!m_key) return;
	QVector<DiagramKeyEntry> entries = keyEntries();
	const bool any = !entries.isEmpty();
	m_key->setEntries(std::move(entries));
	m_frame->panel()->setSectionVisible(m_keySection, any);
}

void DiagramWindow::setTrainColors(const QHash<QString, QColor>& colors) {
	m_serviceColours = colors;
	applyColourPlan();
}

void DiagramWindow::setTrainServices(const QHash<QString, QString>& services) {
	m_trainServices = services;
	refreshTrainList();
	applyColourPlan();
}

void DiagramWindow::setTrainCopies(const QHash<QString, QString>& copyOf) {
	m_trainCopies = copyOf;
	applyColourPlan();
}

// The colour rule needs the trains that the list shows, so the list keeps its check states and takes the colours of the
// rule in its line samples only.
void DiagramWindow::applyColourPlan() {
	if (m_trainList && !m_rollingStockSubject) {
		QVector<TrainColourInput> trains;
		trains.reserve(m_groups.size());
		for (const SeriesGroup& group : m_groups)
			trains.append({group.trainId, m_trainServices.value(group.trainId), m_trainList->isTrainVisible(group.trainId),
				m_trainCopies.value(group.trainId)});
		m_planColours = trainColourPlan(trains, m_serviceColours, m_pinnedTrainId).colours;
		// The base look of a series takes the colour of its train and keeps its widths, dash patterns and the alpha of an
		// area fill.
		for (const SeriesGroup& group : m_groups) {
			const QColor colour = m_planColours.value(group.trainId);
			for (QAbstractSeries* series : group.members) {
				if (!colour.isValid() || series->property("ownColour").toBool()) continue;
				if (qobject_cast<QXYSeries*>(series) && m_basePens.contains(series)) {
					m_basePens[series].setColor(colour);
				} else if (auto* area = qobject_cast<QAreaSeries*>(series); area && m_baseBrushes.contains(series)) {
					QBrush& brush = m_baseBrushes[series];
					QColor fill = colour;
					fill.setAlpha(brush.color().alpha());
					brush.setColor(fill);
					QPen pen = area->pen();
					pen.setColor(colour);
					area->setPen(pen);
				}
			}
		}
		m_trainList->setColours(m_planColours);
	}
	refreshEmphasis();
}

void DiagramWindow::setChart(QChart* chart) {
	if (m_view && m_view->chart() == chart) return;
	clearTooltip();
	m_sectionOverlay = nullptr;	 // the old chart owns and deletes it
	delete m_numericAxis.data(); // detached axis belongs to this window, not the old chart
	m_numericAxis = nullptr;
	delete m_positionNumericAxis.data();
	m_positionNumericAxis = nullptr;
	m_positionAxis = nullptr;
	m_positionScale = PositionScale();
	m_fullBounds.clear();
	m_builderBounds.clear();
	m_clockAxis = nullptr;
	m_stationAxis = nullptr;
	m_stations.clear();
	// A new chart ends the selection; the old chart deletes the outline and the note of an empty chart.
	m_selectionOutline = nullptr;
	m_emptyNote = nullptr;
	m_selectedBox = nullptr;
	m_pinnedTrainId.clear();
	m_selectedLines.clear();
	if (m_trainList) m_trainList->setSelectedTrain(QString());
	refreshSelected();
	// The base look of the old series is of no use, and a new series can take the address of an old one.
	m_basePens.clear();
	m_baseBrushes.clear();
	if (m_readout) m_readout->clear();
	if (chart) {
		// The window title and the export name the chart, so the plot has no title of its own.
		chart->setTitle(QString());
		applyChartStyle(chart);
	}
	QChart* previous = m_view->chart();
	if (previous != chart)
		m_view->setChart(chart); // QChartView takes ownership of the new chart
	if (previous && previous != chart)
		delete previous; // setChart releases, rather than deletes, the old chart
	// The train dropdown replaces the built-in legend, which collapses to "..."
	// once many trains are present.
	if (chart && chart->legend())
		chart->legend()->hide();
	// Existing input-traction callers tag their windows before setting the chart.
	// Keep the explicit API for future callers and avoid a train-only filter here.
	if (property("inputTrainUnitId").isValid()) setRollingStockSubject(true);
	rebuildFilterGroups();
	// The trains of the old chart that were hidden stay hidden, and the Key follows.
	applyTrainVisibility();
	if (chart) {
		for (auto* axis : chart->axes())
			if (auto* value = qobject_cast<QValueAxis*>(axis)) {
				// Round ticks for every numeric axis whose ticks the chart builder left as Qt sets them, within the range of the
				// builder where it asks for that range exactly.
				if (qobject_cast<QCategoryAxis*>(axis) || value->tickType() != QValueAxis::TicksFixed) continue;
				m_builderBounds.insert(axis, {value->min(), value->max()});
				if (!value->property("exactRange").toBool()) widenToRoundTicks(value);
				roundTicks(value);
				connect(value, &QValueAxis::rangeChanged, value, [value] { roundTicks(value); });
			}
		// Taken once every axis is widened, since an axis that shares a series with a widened one follows its range.
		for (auto* axis : chart->axes())
			if (auto* value = qobject_cast<QValueAxis*>(axis)) m_fullBounds.insert(axis, {value->min(), value->max()});
	}
	applyTimeAxis();
	refreshEmptyNote();
}

void DiagramWindow::setTimeAxisX(bool on, long long startOffsetSeconds) {
	clearTooltip();
	m_timeOrientation = Qt::Horizontal;
	m_timeAxis = on;
	m_startOffset = startOffsetSeconds;
	applyTimeAxis();
}

void DiagramWindow::setTimeAxisY(bool on, long long startOffsetSeconds) {
	clearTooltip();
	m_timeOrientation = Qt::Vertical;
	m_timeAxis = on;
	m_startOffset = startOffsetSeconds;
	applyTimeAxis();
}

void DiagramWindow::setCsvProvider(std::function<std::string(const QStringList&)> provider,
	const QString& suggestedFileName, const QStringList& trainIds) {
	m_csvProvider = std::move(provider);
	const QStringList ids = m_csvProvider ? trainIds : QStringList();
	if (ids != m_csvTrainIds) {
		m_csvTrainIds = ids;
		rebuildFilterGroups();
		applyTrainVisibility();
	}
	m_csvSuggestedName = suggestedFileName;
	if (m_csvButton)
		m_csvButton->setVisible(static_cast<bool>(m_csvProvider));
}

void DiagramWindow::setStationAxis(QCategoryAxis* axis, std::vector<StationTick> stations) {
	QChart* chart = m_view ? m_view->chart() : nullptr;
	if (!axis || !chart) return;
	m_stationAxis = axis;
	m_stations = std::move(stations);
	connect(axis, &QValueAxis::rangeChanged, this, [this] { scheduleStationLayout(); });
	connect(chart, &QChart::plotAreaChanged, this, [this] { scheduleStationLayout(); });
	layoutStationAxis();
}

// The chart is laid out when it is shown and whenever its plot area changes, and an axis must not change in the
// middle of that, so the labels are chosen once the layout is done.
void DiagramWindow::scheduleStationLayout() {
	if (m_stationLayoutPending) return;
	m_stationLayoutPending = true;
	QMetaObject::invokeMethod(this, [this] { layoutStationAxis(); }, Qt::QueuedConnection);
}

void DiagramWindow::layoutStationAxis() {
	m_stationLayoutPending = false;
	QChart* chart = m_view ? m_view->chart() : nullptr;
	if (!m_stationAxis || !chart) return;
	QCategoryAxis* axis = m_stationAxis;
	// The axes start inside the margins of the chart and of its layout, which depend on the style.
	qreal contentsLeft = 0, contentsTop = 0, contentsRight = 0, contentsBottom = 0;
	if (chart->layout()) chart->layout()->getContentsMargins(&contentsLeft, &contentsTop, &contentsRight, &contentsBottom);
	const auto shown = labelledStations(m_stations, *axis, chart->plotArea(), contentsLeft + chart->margins().left(), chart->size().width(),
		QFontMetricsF(axis->labelsFont()));
	QStringList labels;
	for (const auto& station : shown) labels << station.second.toHtmlEscaped();
	if (labels == axis->categoriesLabels()) return;
	const QStringList old = axis->categoriesLabels();
	for (const QString& label : old) axis->remove(label);
	axis->setStartValue(axis->min() - 1);
	for (const auto& station : shown) axis->append(station.second.toHtmlEscaped(), station.first);
}

// When the pointer is on an axis text that the chart has shortened, the whole axis title; empty otherwise.
QString DiagramWindow::elidedAxisTextAt(const QPointF& chartPosition) const {
	QChart* chart = m_view->chart();
	const QPointF scenePosition = chart->mapToScene(chartPosition);
	for (QGraphicsItem* item : m_view->scene()->items(scenePosition)) {
		const auto* text = qgraphicsitem_cast<QGraphicsTextItem*>(item);
		if (!text || !text->toPlainText().endsWith(QLatin1String("..."))) continue;
		const QString shown = text->toPlainText().chopped(3);
		for (QAbstractAxis* axis : chart->axes()) {
			QTextDocument title;
			title.setHtml(axis->titleText());
			if (title.toPlainText().size() > shown.size() && title.toPlainText().startsWith(shown)) return title.toPlainText();
		}
	}
	return {};
}

void DiagramWindow::setBlockSections(std::vector<BlockSectionExtent> sections, const QString& referenceRouteId,
	const SceneSectionLabels* labels, std::vector<BlockSectionMarks> marks, int routeSectionCount) {
	clearTooltip();
	delete m_sectionOverlay.data();
	auto* chart = m_view->chart();
	auto* overlay = new BlockSectionOverlay(chart, std::move(sections), std::move(marks), routeSectionCount, referenceRouteId,
		labels, font(), palette());
	m_sectionOverlay = overlay;
	// The strip chooses its rows at the full view of the chart.
	const auto horizontal = chart->axes(Qt::Horizontal);
	if (auto* distance = horizontal.isEmpty() ? nullptr : qobject_cast<QValueAxis*>(horizontal.first())) {
		const auto full = m_fullBounds.value(distance, {distance->min(), distance->max()});
		overlay->setFullRange(full.first, full.second);
	}
	if (m_positionAxis) overlay->setJumps(m_positionScale.jumps());
	// A new row count changes the room the strip needs, which the chart may not take while it lays itself out.
	overlay->setRowsChanged([this] { QMetaObject::invokeMethod(this, [this] { reserveStripRoom(); }, Qt::QueuedConnection); });
	// QChart lays a top legend out above the plot area, so an invisible legend as tall as the strip moves the plot
	// down and leaves the strip its room.
	auto* legend = chart->legend();
	legend->setAlignment(Qt::AlignTop);
	legend->setOpacity(0); // markers return whenever a train is shown again
	legend->setEnabled(false);
	legend->show();
	reserveStripRoom();
	refreshKey();
}

// The invisible top legend takes the height of the strip and its notice.
void DiagramWindow::reserveStripRoom() {
	const auto* overlay = sectionOverlayOf(m_sectionOverlay);
	QChart* chart = m_view ? m_view->chart() : nullptr;
	if (!overlay || !chart) return;
	auto* legend = chart->legend();
	const int height = static_cast<int>(std::ceil(overlay->headerHeight()));
	if (legend->minimumHeight() == height && legend->maximumHeight() == height) return;
	legend->setMinimumHeight(0);
	legend->setMaximumHeight(height);
	legend->setMinimumHeight(height);
}

std::string DiagramWindow::csvContent() const {
	return m_csvProvider ? m_csvProvider(visibleTrainIds()) : std::string();
}

void DiagramWindow::setProvenanceWriter(
	std::function<bool(const QString&, const char*, const std::string&)> writer) {
	m_provenanceWriter = std::move(writer);
}

// Shared look for every diagram: quiet grid, readable title, line weight that
// survives PNG export.
void DiagramWindow::applyChartStyle(QChart* chart) {
	chart->setBackgroundRoundness(0);
	chart->setBackgroundBrush(palette().brush(QPalette::Base));
	chart->setMargins(kChartMargins);

	QColor gridColor = palette().color(QPalette::WindowText);
	gridColor.setAlpha(30);
	const QPen gridPen(gridColor);
	const QBrush labelBrush = palette().brush(QPalette::WindowText);
	const auto axes = chart->axes();
	for (QAbstractAxis* axis : axes) {
		axis->setGridLinePen(gridPen);
		axis->setLabelsBrush(labelBrush);
		axis->setTitleBrush(labelBrush);
		QColor lineColor = palette().color(QPalette::WindowText);
		lineColor.setAlpha(90);
		axis->setLinePen(QPen(lineColor));
	}

	const auto seriesList = chart->series();
	for (QAbstractSeries* series : seriesList) {
		if (auto* xy = qobject_cast<QXYSeries*>(series)) {
			QPen pen = xy->pen();
			if (isPlannedSeries(series))
				pen.setStyle(Qt::DashLine);
			xy->setPen(pen);
			// A line is at least 2 px wide; the outline of a box keeps its width.
			if (pen.widthF() < 2.0 && !series->property("inspectionFilled").toBool()) {
				pen.setWidthF(2.0);
				xy->setPen(pen);
			}
			// The points of a planned series are open circles in the colour of its pen, which the colour rule and the
			// emphasis change.
			if (isPlannedSeries(series) && xy->pointsVisible()) {
				xy->setMarkerSize(kPlannedMarkerSize);
				xy->setLightMarker(plannedMarker(pen));
				connect(xy, &QXYSeries::penChanged, xy, [xy](const QPen& changed) { xy->setLightMarker(plannedMarker(changed)); });
			}
		}
	}
}

// Keep numeric ranges and orientation when replacing time labels.
void DiagramWindow::applyTimeAxis() {
	QChart* chart = m_view ? m_view->chart() : nullptr;
	if (!chart) return;
	if (m_clockAxis) {
		auto* old = qobject_cast<QCategoryAxis*>(m_clockAxis.data());
		if (!m_numericAxis) return;
		restoreNumericAxis(chart, old, m_numericAxis, m_fullBounds);
		m_numericAxis = nullptr;
		m_clockAxis = nullptr;
	}
	if (auto* overlay = sectionOverlayOf(m_sectionOverlay)) overlay->watchAxes();
	if (!m_timeAxis) return;
	const auto timeAxes = chart->axes(m_timeOrientation);
	if (timeAxes.isEmpty())
		return;
	auto* seconds = qobject_cast<QValueAxis*>(timeAxes.first());
	if (!seconds)
		return;
	// A clock axis keeps the range the chart builder gave it, since round seconds are no round clock times.
	if (m_builderBounds.contains(seconds)) {
		const auto range = m_builderBounds.value(seconds);
		seconds->setRange(range.first, range.second);
		m_fullBounds.insert(seconds, range);
	}
	const double min = seconds->min();
	const double max = seconds->max();
	const double span = max - min;
	if (!(span > 0.0))
		return;

	auto* clock = replaceWithCategoryAxis(chart, seconds, QStringLiteral("Time (hh:mm)"), this, m_fullBounds,
		palette().brush(QPalette::WindowText));
	m_numericAxis = seconds;
	m_clockAxis = clock;
	const bool vertical = m_timeOrientation == Qt::Vertical;
	// A queued update can come after a chart replacement has deleted the chart and before the axis goes.
	const QPointer<QChart> owner(chart);
	const auto updateLabels = [this, clock, owner, vertical](qreal low, qreal high) {
		for (const QString& label : clock->categoriesLabels()) clock->remove(label);
		const double raw = (high - low) / 6;
		const double power = std::pow(10.0, std::floor(std::log10(std::max(1.0, raw))));
		double interval = std::max(1.0, std::ceil(raw / power) * power);
		for (double candidate : {1., 5., 10., 30., 60., 120., 300., 600., 900., 1800., 3600., 7200., 14400., 21600., 43200.})
			if ((high - low) / candidate <= 8) {
				interval = candidate;
				break;
			}
		clock->setStartValue(low - interval);
		// A tick on a whole minute reads HH:MM while the ticks are a minute apart or more, so that no two ticks read the
		// same minute; every other tick keeps its seconds.
		const bool minutes = interval >= 60;
		const auto clockLabel = [this, minutes](double tick) {
			const auto elapsed = static_cast<long long>(tick);
			QString text = QString::fromStdString(formatSimTime(elapsed, m_startOffset));
			if (minutes && (elapsed + m_startOffset) % 60 == 0) text.truncate(5);
			return text;
		};
		// Ticks fall on round clock times. Elapsed zero, the run start, keeps a tick of its own while it is in range, and a
		// round tick less than a step from it is left out where its label would overlap the label of the run start in the
		// plot as it is laid out, or while the plot has no size.
		const bool runStart = low <= 0 && high >= 0;
		const double offset = static_cast<double>(m_startOffset);
		const QFontMetricsF metrics(clock->labelsFont());
		const QRectF plot = owner ? owner->plotArea() : QRectF();
		const double pixelsPerSecond = (vertical ? plot.height() : plot.width()) / (high - low);
		std::vector<std::pair<double, QString>> ticks;
		if (runStart) ticks.emplace_back(0, clockLabel(0));
		// NOLINTNEXTLINE(bugprone-float-loop-counter,clang-analyzer-security.FloatLoopCounter): ticks are placed at multiples of the interval
		for (double tick = std::ceil((low + offset) / interval) * interval - offset; tick <= high; tick += interval) {
			const QString label = clockLabel(tick);
			const double room = vertical ? metrics.height() : metrics.horizontalAdvance(label);
			if (!runStart || std::abs(tick) >= interval || std::abs(tick) * pixelsPerSecond >= room) ticks.emplace_back(tick, label);
		}
		std::sort(ticks.begin(), ticks.end());
		QStringList texts;
		for (const auto& tick : ticks) texts << tick.second;
		// A day later the clock reads the same again.
		texts = uniqueCategoryTexts(texts);
		for (std::size_t i = 0; i < ticks.size(); ++i) clock->append(texts[static_cast<int>(i)], ticks[i].first);
	};
	connect(clock, &QValueAxis::rangeChanged, this, updateLabels);
	// The plot is laid out when it is shown and whenever it changes size, and an axis must not change in the middle of that.
	connect(chart, &QChart::plotAreaChanged, clock, [clock, updateLabels] { updateLabels(clock->min(), clock->max()); }, Qt::QueuedConnection);
	updateLabels(min, max);
	if (auto* overlay = sectionOverlayOf(m_sectionOverlay)) overlay->watchAxes();
}

void DiagramWindow::setPositionAxis(PositionScale scale) {
	clearTooltip();
	m_positionScale = std::move(scale);
	applyPositionAxis();
}

// As applyTimeAxis, for the distance along the bottom with the labels of the position scale: the numeric axis leaves the
// chart while the position axis takes its place, and comes back with its range when the scale is empty.
void DiagramWindow::applyPositionAxis() {
	QChart* chart = m_view ? m_view->chart() : nullptr;
	if (!chart) return;
	auto* overlay = sectionOverlayOf(m_sectionOverlay);
	if (m_positionAxis && m_positionNumericAxis) restoreNumericAxis(chart, m_positionAxis, m_positionNumericAxis, m_fullBounds);
	m_positionAxis = nullptr;
	m_positionNumericAxis = nullptr;
	if (overlay) overlay->setJumps({});
	QValueAxis* distance = nullptr;
	for (QAbstractAxis* axis : chart->axes(Qt::Horizontal))
		if (axis->alignment() == Qt::AlignBottom && !qobject_cast<QCategoryAxis*>(axis)) {
			distance = qobject_cast<QValueAxis*>(axis);
			if (distance) break;
		}
	if (m_positionScale.empty() || !distance || !(distance->max() > distance->min())) {
		if (overlay) overlay->watchAxes();
		return;
	}
	auto* position = replaceWithCategoryAxis(chart, distance, QStringLiteral("Position (km)"), this, m_fullBounds,
		palette().brush(QPalette::WindowText));
	m_positionNumericAxis = distance;
	m_positionAxis = position;
	connect(position, &QValueAxis::rangeChanged, this, [this] { schedulePositionLayout(); });
	connect(chart, &QChart::plotAreaChanged, position, [this] { schedulePositionLayout(); });
	layoutPositionAxis();
	if (overlay) {
		overlay->setJumps(m_positionScale.jumps());
		overlay->watchAxes();
	}
}

// One relayout for a burst of range and plot changes, once the chart is laid out, as for the station axis.
void DiagramWindow::schedulePositionLayout() {
	if (m_positionLayoutPending) return;
	m_positionLayoutPending = true;
	QMetaObject::invokeMethod(this, [this] { layoutPositionAxis(); }, Qt::QueuedConnection);
}

// The labels of the position axis for the range in view: the round geo X values of the scale at their route distance,
// with the decimals of the step.
void DiagramWindow::layoutPositionAxis() {
	m_positionLayoutPending = false;
	QChart* chart = m_view ? m_view->chart() : nullptr;
	QCategoryAxis* axis = m_positionAxis;
	if (!chart || !axis) return;
	const double width = chart->plotArea().width();
	const auto labels = m_positionScale.roundLabels(axis->min(), axis->max(), width);
	const double step = m_positionScale.labelStepKm(axis->min(), axis->max(), width);
	const int decimals = step > 0 ? std::max(0, static_cast<int>(-std::floor(std::log10(step) + 1e-9))) : 0;
	QStringList texts;
	for (const PositionScale::Label& label : labels)
		texts << QString::number(std::abs(label.geoKm) < 0.5 * std::pow(10.0, -decimals) ? 0.0 : label.geoKm, 'f', decimals);
	texts = uniqueCategoryTexts(texts);
	bool same = texts == axis->categoriesLabels();
	for (int i = 0; same && i < texts.size(); ++i) same = axis->endValue(texts[i]) == labels[static_cast<std::size_t>(i)].routeKm;
	if (same) return;
	for (const QString& label : axis->categoriesLabels()) axis->remove(label);
	axis->setStartValue(axis->min() - 1);
	for (int i = 0; i < texts.size(); ++i) axis->append(texts[i], labels[static_cast<std::size_t>(i)].routeKm);
}

QString DiagramWindow::groupIdForSeries(QAbstractSeries* series) const {
	if (!series)
		return QString();
	const QVariant trainId = series->property("trainId");
	if (trainId.isValid() && !trainId.toString().isEmpty())
		return trainId.toString();
	return series->name();
}

void DiagramWindow::rebuildFilterGroups() {
	QChart* chart = m_view ? m_view->chart() : nullptr;
	// The rebuild records the series as they are, so first the emphasis of a selected train is undone.
	if (chart)
		for (QAbstractSeries* series : chart->series()) {
			if (auto* xy = qobject_cast<QXYSeries*>(series))
				xy->setPen(m_basePens.value(series, xy->pen()));
			else if (auto* area = qobject_cast<QAreaSeries*>(series))
				area->setBrush(m_baseBrushes.value(series, area->brush()));
		}
	m_groups.clear();
	m_basePens.clear();
	m_baseBrushes.clear();
	if (!chart) {
		refreshTrainList();
		return;
	}

	QHash<QString, int> indexByTrain;
	const auto seriesList = chart->series();
	for (QAbstractSeries* series : seriesList) {
		const QString trainId = groupIdForSeries(series);
		if (auto* xy = qobject_cast<QXYSeries*>(series))
			m_basePens.insert(series, xy->pen());
		if (auto* area = qobject_cast<QAreaSeries*>(series))
			m_baseBrushes.insert(series, area->brush());

		int groupIndex;
		auto it = indexByTrain.find(trainId);
		if (it == indexByTrain.end()) {
			SeriesGroup group;
			group.trainId = trainId;
			m_groups.append(group);
			groupIndex = static_cast<int>(m_groups.size()) - 1;
			indexByTrain.insert(trainId, groupIndex);
		} else {
			groupIndex = it.value();
		}
		m_groups[groupIndex].members.append(series);
	}

	for (const QString& trainId : m_csvTrainIds)
		if (!trainId.isEmpty() && !indexByTrain.contains(trainId)) {
			indexByTrain.insert(trainId, static_cast<int>(m_groups.size()));
			m_groups.append({trainId, {}});
		}
	refreshTrainList();
}

// The line sample of a train has the colour that the colour rule gave it last; applyColourPlan gives the new colours.
void DiagramWindow::refreshTrainList() {
	if (!m_trainList) return;
	QVector<TrainKeyList::Train> trains;
	trains.reserve(m_groups.size());
	for (const SeriesGroup& group : m_groups)
		trains.append({group.trainId, m_planColours.value(group.trainId), m_trainServices.value(group.trainId)});
	m_trainList->setTrains(trains);
	refreshTrainsSection();
}

void DiagramWindow::applyTrainVisibility() {
	clearTooltip();
	if (!m_trainList)
		return;
	for (const SeriesGroup& group : m_groups) {
		const bool visible = m_trainList->isTrainVisible(group.trainId);
		for (QAbstractSeries* series : group.members)
			series->setVisible(visible);
	}
	// Hiding the selected train ends the selection. The trains that are shown give the colours.
	if (!m_pinnedTrainId.isEmpty() && !m_trainList->isTrainVisible(m_pinnedTrainId))
		clearPin();
	else
		applyColourPlan();
	refreshKey();
}

// A click on a line selects its train, which the chart emphasises; a click inside a blocking time selects that box,
// which an outline marks, and its train.
void DiagramWindow::select(const ChartHit& hit) {
	QChart* chart = m_view ? m_view->chart() : nullptr;
	if (!hit.series || !chart) return;
	const QString trainId = groupIdForSeries(hit.series);
	m_pinnedTrainId = trainId;
	m_selectedLines = hit.lines;
	m_selectedBox = hit.inside ? hit.series : nullptr;
	delete m_selectionOutline.data();
	if (m_selectedBox) m_selectionOutline = new SelectionOutline(chart, m_selectedBox);
	if (m_trainList) m_trainList->setSelectedTrain(trainId);
	raiseTrain(trainId);
	refreshSelected();
	// Among many trains in grey the selected train takes a colour.
	applyColourPlan();
	emit trainSelected(trainId);
}

// QChart draws its series in the order it has them, so the series of the train go to the end, with the axes they had.
void DiagramWindow::raiseTrain(const QString& trainId) {
	QChart* chart = m_view ? m_view->chart() : nullptr;
	const auto group = std::find_if(m_groups.cbegin(), m_groups.cend(), [&trainId](const SeriesGroup& one) { return one.trainId == trainId; });
	if (!chart || group == m_groups.cend()) return;
	const QList<QAbstractSeries*> series = chart->series();
	if (series.size() >= group->members.size()
		&& std::equal(group->members.cbegin(), group->members.cend(), series.cend() - group->members.size()))
		return;
	for (QAbstractSeries* member : group->members) {
		if (!series.contains(member)) continue;
		const QList<QAbstractAxis*> axes = member->attachedAxes();
		chart->removeSeries(member);
		chart->addSeries(member);
		for (QAbstractAxis* axis : axes) member->attachAxis(axis);
	}
}

void DiagramWindow::clearPin() {
	m_pinnedTrainId.clear();
	m_selectedLines.clear();
	m_selectedBox = nullptr;
	delete m_selectionOutline.data();
	if (m_trainList) m_trainList->setSelectedTrain(QString());
	refreshSelected();
	applyColourPlan();
}

// Selected shows the lines of the click, the first in bold, while something is selected in a chart with trains.
void DiagramWindow::refreshSelected() {
	if (!m_pinTitle || !m_pinLabel) return;
	m_pinTitle->setText(withBreakOpportunities(m_selectedLines.value(0)));
	m_pinLabel->setText(withBreakOpportunities(m_selectedLines.mid(1).join('\n')));
	m_pinLabel->setVisible(m_selectedLines.size() > 1);
	m_frame->panel()->setSectionVisible(m_selectedSection, !m_rollingStockSubject && !m_selectedLines.isEmpty());
}

// The selected train is drawn wider and the others faint. A selected blocking time has its outline instead, so the
// chart keeps its look.
void DiagramWindow::refreshEmphasis() {
	const bool pinnedActive = !m_pinnedTrainId.isEmpty() && !m_selectedBox;
	for (const SeriesGroup& group : m_groups) {
		const bool isPinned = pinnedActive && group.trainId == m_pinnedTrainId;
		for (QAbstractSeries* series : group.members) {
			auto* xy = qobject_cast<QXYSeries*>(series);
			if (!xy) {
				if (auto* area = qobject_cast<QAreaSeries*>(series)) {
					QBrush brush = m_baseBrushes.value(series, area->brush());
					if (pinnedActive && !isPinned) {
						QColor color = brush.color();
						color.setAlpha(std::min(color.alpha(), 18));
						brush.setColor(color);
					}
					area->setBrush(brush);
				}
				continue;
			}
			const QPen base = m_basePens.value(series, xy->pen());
			if (!pinnedActive)
				xy->setPen(base);
			else if (isPinned)
				xy->setPen(emphasisedPen(base));
			else {
				const QColor background = m_view && m_view->chart() ? m_view->chart()->backgroundBrush().color() : palette().color(QPalette::Base);
				xy->setPen(mutedPen(base, background));
				// The circles of a muted planned line fade with it, their white too.
				if (isPlannedSeries(series) && xy->pointsVisible())
					xy->setLightMarker(plannedMarker(xy->pen(), mutedColour(Qt::white, background)));
			}
		}
	}
}

// The lines of the tooltip of a plotted sample: the identity of its series, and each of its values with the title of
// its axis.
QStringList DiagramWindow::sampleLines(const QPointF& value, const QString& seriesName) const {
	if (!m_view || !m_view->chart())
		return {};

	const QChart* chart = m_view->chart();
	const auto horizontal = chart->axes(Qt::Horizontal);
	const auto vertical = chart->axes(Qt::Vertical);
	// A clock axis is titled "Time (hh:mm)", but the tooltip gives the seconds too, so it says "Time".
	const QString xLabel = m_timeAxis && m_timeOrientation == Qt::Horizontal ? QStringLiteral("Time")
		: horizontal.isEmpty() || horizontal.first()->titleText().isEmpty()	 ? QString("x")
																			 : horizontal.first()->titleText();
	const QString yLabel = m_timeAxis && m_timeOrientation == Qt::Vertical ? QStringLiteral("Time")
		: vertical.isEmpty() || vertical.first()->titleText().isEmpty()	   ? QString("y")
																		   : vertical.first()->titleText();
	const bool positionX = m_positionAxis && !horizontal.isEmpty() && horizontal.first() == m_positionAxis.data();
	const QString xText = m_timeAxis && m_timeOrientation == Qt::Horizontal
		? QString::fromStdString(formatSimTime(static_cast<long long>(value.x()), m_startOffset))
		: positionX ? QString::number(m_positionScale.geoAt(value.x()), 'f', 3)
					: QString::number(value.x(), 'f', 2);
	const QString yText = m_timeAxis && m_timeOrientation == Qt::Vertical
		? QString::fromStdString(formatSimTime(static_cast<long long>(value.y()), m_startOffset))
		: QString::number(value.y(), 'f', 2);
	return {seriesName, QString("%1: %2").arg(xLabel, xText), QString("%1: %2").arg(yLabel, yText)};
}

namespace {

// The shown plot axis along an edge of the chart, or null: the numeric axis or the clock axis in its place.
QValueAxis* edgeAxis(const QChart& chart, Qt::Alignment edge) {
	for (QAbstractAxis* axis : chart.axes())
		if (axis->alignment() == edge && axis->isVisible())
			if (auto* value = qobject_cast<QValueAxis*>(axis)) return value;
	return nullptr;
}

// The unit of an axis: the text in the last round brackets of its title, such as "km" for "Distance (km)".
QString axisUnit(const QAbstractAxis& axis) {
	QTextDocument title;
	title.setHtml(axis.titleText());
	const QString text = title.toPlainText();
	const auto open = text.lastIndexOf(QLatin1Char('('));
	const auto close = open < 0 ? -1 : text.indexOf(QLatin1Char(')'), open);
	return close < 0 ? QString() : text.mid(open + 1, close - open - 1);
}

} // namespace

// The value under the pointer in the units of the axes, to the precision of one pixel of the plot, as the value of the
// left axis at the value of the bottom axis: "07:14:32 at 9.860 km", "100.0 km/h at 07:14:32". A position axis gives the
// geo X. An axis with the dynamic property "readoutStation" reads the station of the station axis at the nearest value
// along it that a shown series has a sample at, "2.5 min at Treviglio", and nothing without one. Empty outside the plot.
void DiagramWindow::updateCursorReadout(const QPointF& chartPosition) {
	if (!m_readout) return;
	const QChart* chart = m_view ? m_view->chart() : nullptr;
	const QRectF plot = chart ? chart->plotArea() : QRectF();
	if (!chart || plot.isEmpty() || !plot.contains(chartPosition)) {
		m_readout->clear();
		return;
	}
	QStringList values; // the value of the left axis, then that of the bottom axis
	for (const Qt::Alignment edge : {Qt::AlignLeft, Qt::AlignBottom}) {
		QValueAxis* const axis = edgeAxis(*chart, edge);
		if (!axis) continue;
		const bool horizontal = edge == Qt::AlignBottom;
		double fraction = horizontal ? (chartPosition.x() - plot.left()) / plot.width() : (plot.bottom() - chartPosition.y()) / plot.height();
		if (axis->isReverse()) fraction = 1 - fraction;
		const double span = axis->max() - axis->min();
		double value = axis->min() + fraction * span;
		if (axis == m_clockAxis.data()) {
			values << QString::fromStdString(formatSimTime(static_cast<long long>(std::floor(value)), m_startOffset));
			continue;
		}
		if (axis->property("readoutStation").toBool()) {
			double nearest = std::numeric_limits<double>::quiet_NaN();
			for (QAbstractSeries* series : chart->series()) {
				const auto* xy = qobject_cast<const QXYSeries*>(series);
				if (!xy || !xy->isVisible() || !series->attachedAxes().contains(axis)) continue;
				for (const QPointF& point : xy->points()) {
					const double along = horizontal ? point.x() : point.y();
					if (std::isnan(nearest) || std::abs(along - value) < std::abs(nearest - value)) nearest = along;
				}
			}
			const auto station = std::find_if(m_stations.begin(), m_stations.end(),
				[nearest](const StationTick& tick) { return tick.position == nearest && !tick.spellings.isEmpty(); });
			if (station != m_stations.end()) values << station->spellings.last();
			continue;
		}
		// The position axis reads the geo X at the route distance.
		if (axis == m_positionAxis.data()) value = m_positionScale.geoAt(value);
		const double perPixel = span / (horizontal ? plot.width() : plot.height());
		const int decimals = perPixel > 0 ? std::clamp(static_cast<int>(std::ceil(-std::log10(perPixel))), 0, 6) : 0;
		// A value that rounds to zero reads "0.00", not "-0.00".
		const double shown = std::abs(value) < 0.5 * std::pow(10.0, -decimals) ? 0.0 : value;
		const QString unit = axisUnit(*axis);
		values << QString::number(shown, 'f', decimals) + (unit.isEmpty() ? QString() : QStringLiteral(" ") + unit);
	}
	m_readout->setText(values.join(QStringLiteral(" at ")));
}

QImage DiagramWindow::figureImage(const FigureColumn& column) const {
	constexpr qreal ratio = 2;
	constexpr int margin = 16;
	const QSize chartSize = m_view->viewport()->size();
	const int width = chartSize.width() + static_cast<int>(std::ceil(std::max(0.0, column.width)));
	// The heading in the bold font a chart title had, 1.25 times the window font, and the context in the window font.
	QFont headingFont = font();
	if (headingFont.pointSizeF() > 0)
		headingFont.setPointSizeF(headingFont.pointSizeF() * 1.25);
	else
		headingFont.setPixelSize(qRound(headingFont.pixelSize() * 1.25));
	headingFont.setBold(true);
	const QFontMetrics headingMetrics(headingFont);
	// The context wraps at its spaces, and a long identifier also after every 32 characters.
	const QString context = m_contextLabel && !m_contextLabel->isHidden() ? withBreakOpportunities(m_contextLabel->text()) : QString();
	const QRect textArea(margin, margin, std::max(1, width - 2 * margin), 100000);
	const QRect contextArea = context.isEmpty()
		? QRect()
		: fontMetrics().boundingRect(textArea.adjusted(0, headingMetrics.height() + 4, 0, 0), Qt::TextWordWrap, context);
	const int contextBottom = contextArea.isEmpty() ? margin + headingMetrics.height() : contextArea.bottom() + 1;
	// The warning of the window, such as a curve cut off at 0 kN, goes with the figure under the context, with its look.
	const bool warning = !m_warningArea->isHidden() && !m_warningLabel->text().isEmpty();
	const QRect warningArea = warning ? QRect(QPoint(margin, contextBottom + 8), m_warningLabel->size()) : QRect();
	const int top = (warningArea.isEmpty() ? contextBottom : warningArea.bottom() + 1) + margin / 2;

	QImage image(QSize(width, top + chartSize.height()) * ratio, QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio);
	// One surface with the chart.
	image.fill(m_view->chart() ? m_view->chart()->backgroundBrush().color() : palette().color(QPalette::Base));
	QPainter painter(&image);
	painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
	painter.setFont(headingFont);
	painter.setPen(palette().color(QPalette::WindowText));
	painter.drawText(QRect(textArea.topLeft(), QSize(textArea.width(), headingMetrics.height())), Qt::AlignLeft | Qt::AlignVCenter,
		headingMetrics.elidedText(windowTitle(), Qt::ElideRight, textArea.width()));
	if (!context.isEmpty()) {
		painter.setFont(font());
		painter.setPen(m_contextLabel->palette().color(m_contextLabel->foregroundRole()));
		painter.drawText(contextArea, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, context);
	}
	if (warning) m_warningLabel->render(&painter, warningArea.topLeft(), QRegion(), QWidget::DrawWindowBackground | QWidget::DrawChildren);
	// The scene of the chart, without the widgets over it, the rubber band and the tooltip, and without the outline of a
	// selected blocking time.
	const bool outline = m_selectionOutline && m_selectionOutline->isVisible();
	if (outline) m_selectionOutline->setVisible(false);
	m_view->render(&painter, QRectF(QPointF(0, top), chartSize), m_view->viewport()->rect());
	if (outline) m_selectionOutline->setVisible(true);
	if (column.width > 0 && column.paint)
		column.paint(painter, QRectF(chartSize.width(), top, column.width, chartSize.height()));
	painter.end();
	// Twice the resolution of the screen, so that a viewer shows the image at the size of the window. It is set once the
	// figure is drawn, as text drawn on an image takes its size from the resolution of the image.
	image.setDotsPerMeterX(qRound(ratio * logicalDpiX() / 0.0254));
	image.setDotsPerMeterY(qRound(ratio * logicalDpiY() / 0.0254));
	return image;
}

namespace {
constexpr double kColumnPadding = 16;	 // around the column of an exported figure
constexpr double kColumnHeadingGap = 6;	 // below a heading of the column
constexpr double kColumnSectionGap = 14; // between the Trains and the Key of the column
} // namespace

// The column of an exported figure: a rule, then the shown trains under "Trains", each with its line sample, or each
// service with a shown train while the list groups them, and the Key under "Key". Rows that do not fit end in "N more".
FigureColumn DiagramWindow::panelColumn() const {
	QVector<DiagramKeyEntry> trains;
	if (m_trainList && !m_rollingStockSubject)
		for (const auto& row : m_trainList->shownRows())
			trains.push_back({row.first, DiagramKeyEntry::Shape::Line,
				row.second.isValid() ? QPen(row.second, 2.5, Qt::SolidLine, Qt::RoundCap) : QPen(Qt::NoPen), QBrush()});
	const QVector<DiagramKeyEntry> key = m_key ? m_key->entries() : QVector<DiagramKeyEntry>();
	if (trains.isEmpty() && key.isEmpty()) return {};
	QFont headingFont = font();
	headingFont.setBold(true);
	const QFontMetricsF headingMetrics(headingFont);
	const double rowHeight = diagramKeyRowHeight(font());
	const double content = std::max({diagramKeyWidth(trains, font()), diagramKeyWidth(key, font()), headingMetrics.horizontalAdvance("Trains"),
		headingMetrics.horizontalAdvance("Key")});
	FigureColumn column;
	column.width = std::clamp(std::ceil(content + 2 * kColumnPadding), 140.0, 280.0);
	const QColor text = palette().color(QPalette::WindowText);
	const QFont rowFont = font();
	column.paint = [trains, key, headingFont, rowFont, rowHeight, text](QPainter& painter, const QRectF& area) {
		const QFontMetricsF metrics(headingFont);
		const QRectF inner = area.adjusted(kColumnPadding, kColumnPadding, -kColumnPadding, -kColumnPadding);
		painter.save();
		painter.setPen(QPen(kStripRule, 1));
		painter.drawLine(QPointF(area.left() + 0.5, inner.top()), QPointF(area.left() + 0.5, inner.bottom()));
		double y = inner.top();
		const auto heading = [&](const QString& title) {
			painter.setFont(headingFont);
			painter.setPen(text);
			painter.drawText(QRectF(inner.left(), y, inner.width(), metrics.height()), Qt::AlignLeft | Qt::AlignVCenter, title);
			y += metrics.height() + kColumnHeadingGap;
		};
		const double keyHeight =
			key.isEmpty() ? 0.0 : kColumnSectionGap + metrics.height() + kColumnHeadingGap + rowHeight * static_cast<double>(key.size());
		if (!trains.isEmpty()) {
			heading(QStringLiteral("Trains"));
			const int room = std::max(1, static_cast<int>((inner.bottom() - keyHeight - y) / rowHeight));
			const int count = static_cast<int>(trains.size());
			const int drawn = count <= room ? count : std::max(0, room - 1);
			for (int i = 0; i < drawn; ++i)
				y += paintKeyRow(painter, QRectF(inner.left(), y, inner.width(), rowHeight), trains[i], rowFont, text);
			if (drawn < count) {
				painter.setFont(rowFont);
				painter.setPen(text);
				painter.drawText(QRectF(inner.left() + kKeySampleWidth + kKeySampleGap, y, inner.width(), rowHeight), Qt::AlignLeft | Qt::AlignVCenter,
					QStringLiteral("%1 more").arg(count - drawn));
				y += rowHeight;
			}
			y += kColumnSectionGap;
		}
		if (!key.isEmpty()) {
			heading(QStringLiteral("Key"));
			for (const DiagramKeyEntry& entry : key)
				y += paintKeyRow(painter, QRectF(inner.left(), y, inner.width(), rowHeight), entry, rowFont, text);
		}
		painter.restore();
	};
	return column;
}

QImage DiagramWindow::exportedFigure() const {
	return figureImage(panelColumn());
}

void DiagramWindow::copyFigure() {
	if (QClipboard* clipboard = QGuiApplication::clipboard())
		clipboard->setImage(exportedFigure());
}

QStringList DiagramWindow::visibleTrainIds() const {
	if (m_trainList)
		return m_trainList->visibleTrainIds();
	return QStringList();
}

void DiagramWindow::resetZoom() {
	clearTooltip();
	if (m_view && m_view->chart()) {
		m_view->chart()->zoomReset();
		for (auto it = m_fullBounds.cbegin(); it != m_fullBounds.cend(); ++it)
			if (auto* axis = qobject_cast<QValueAxis*>(it.key()))
				axis->setRange(it.value().first, it.value().second);
	}
}

void DiagramWindow::exportPng() {
	// The window comes back in front once the save dialog and its messages close.
	const KeepInFront front(this);
	const auto operation = m_telemetryCapture ? m_telemetryCapture() : telemetry::OperationObservation();
	const QString suggested = !m_figureFileName.isEmpty() ? m_figureFileName
		: m_csvSuggestedName.isEmpty()					  ? QString("diagram.png")
														  : QFileInfo(m_csvSuggestedName).completeBaseName() + ".png";
	QString path = QFileDialog::getSaveFileName(this, "Export Diagram", dialog_folders::suggestedPath(suggested), "PNG Image (*.png)");
	if (path.isEmpty())
		return; // cancelled: no file is written
	if (QFileInfo(path).suffix().compare("png", Qt::CaseInsensitive) != 0)
		path += ".png";
	const QImage figure = exportedFigure();
	QByteArray png;
	QBuffer buffer(&png);
	if (!buffer.open(QIODevice::WriteOnly) || !figure.save(&buffer, "PNG")) {
		operation.failure(telemetry::Operation::Export, telemetry::Error::InternalFailure);
		QMessageBox::warning(this, "Export failed",
			QString("Could not save the image to:\n%1").arg(path));
		return;
	}
	const std::string bytes(png.constData(), static_cast<std::size_t>(png.size()));
	const bool written = m_provenanceWriter
		? m_provenanceWriter(path, "png", bytes)
		: writeArtifact(path, bytes);
	operation.exportFinished(telemetry::ExportKind::Png, written, true, telemetry::Error::IoFailure);
	if (!written)
		QMessageBox::warning(this, "Export failed",
			QString("Could not save the image to:\n%1").arg(path));
}

void DiagramWindow::exportCsv() {
	// The window comes back in front once the save dialog and its messages close.
	const KeepInFront front(this);
	const auto operation = m_telemetryCapture ? m_telemetryCapture() : telemetry::OperationObservation();
	if (!m_csvProvider)
		return;
	const std::string content = csvContent();
	if (content.empty()) {
		QMessageBox::information(this, "Nothing to export",
			"There is no data to export for the visible trains.");
		return;
	}
	const QString suggested = m_csvSuggestedName.isEmpty() ? QString("export.csv") : m_csvSuggestedName;
	QString path = QFileDialog::getSaveFileName(this, "Export Data", dialog_folders::suggestedPath(suggested), "CSV File (*.csv)");
	if (path.isEmpty())
		return; // cancelled: no file is written
	if (QFileInfo(path).suffix().compare("csv", Qt::CaseInsensitive) != 0)
		path += ".csv";
	const bool written = m_provenanceWriter
		? m_provenanceWriter(path, "csv", content)
		: writeArtifact(path, content);
	operation.exportFinished(telemetry::ExportKind::Csv, written, true, telemetry::Error::IoFailure);
	if (!written) {
		QMessageBox::warning(this, "Export failed",
			QString("Could not save the data to:\n%1").arg(path));
		return;
	}
	// The window has no status line of its own, so the main window names the saved file.
	if (auto* mainWindow = qobject_cast<QMainWindow*>(parentWidget()))
		mainWindow->statusBar()->showMessage(QStringLiteral("Saved %1").arg(QDir::toNativeSeparators(path)), 8000);
}

void DiagramWindow::clearTooltip() {
	if (m_tooltip) m_tooltip->hide();
}

// What the chart shows at a point, without showing it: the hover of the section strip, the whole text of a shortened
// axis title, inside a box the nearest sample of a shown line within kLineOverBox px, else the box, outside the boxes the
// nearest sample of a shown line within 10 px, else the hover of a section boundary. A sample gives its values, a box its
// title, or else its name, and its context lines only.
DiagramWindow::ChartHit DiagramWindow::hitAt(const QPoint& position) const {
	ChartHit hit;
	QChart* chart = m_view ? m_view->chart() : nullptr;
	if (!chart) return hit;
	const QPointF cursor = chart->mapFromScene(m_view->mapToScene(position));
	if (const auto* overlay = sectionOverlayOf(m_sectionOverlay)) {
		const QString text = overlay->inspect(cursor, true);
		if (!text.isEmpty()) {
			hit.lines = text.split('\n');
			return hit;
		}
	}
	if (!chart->plotArea().contains(cursor)) {
		const QString title = elidedAxisTextAt(cursor);
		if (!title.isEmpty()) hit.lines << title;
		return hit;
	}
	QXYSeries* selected = nullptr;
	QPointF sample;
	int sampleIndex = -1;
	double best = 10;
	// The nearest sample of a line that is not the outline of a box, which inside a box wins only when it is this near.
	QXYSeries* line = nullptr;
	QPointF lineSample;
	int lineIndex = -1;
	double lineBest = kLineOverBox;
	QXYSeries* filled = nullptr;
	QPointF filledSample;
	int filledIndex = -1;
	double filledVertexDistance = std::numeric_limits<double>::max();
	for (auto* series : chart->series()) {
		auto* xy = qobject_cast<QXYSeries*>(series);
		if (!xy || !xy->isVisible()) continue;
		const auto points = xy->points();
		const bool outline = xy->property("inspectionFilled").toBool();
		bool inside = false;
		if (outline && points.size() >= 4
			&& points.first() == points.last() && !qobject_cast<QScatterSeries*>(xy)) {
			QPolygonF polygon;
			for (const auto& point : points) polygon.append(chart->mapToPosition(point, xy));
			inside = polygon.containsPoint(cursor, Qt::OddEvenFill);
		}
		for (int i = 0; i < points.size(); ++i) {
			const QPointF point = chart->mapToPosition(points[i], xy);
			if (inside) {
				const double vertexDistance = QLineF(cursor, point).length();
				if (vertexDistance < filledVertexDistance) {
					filledVertexDistance = vertexDistance;
					filled = xy;
					filledSample = points[i];
					filledIndex = i;
				}
				continue;
			}
			if (chart->plotArea().contains(point)) {
				const double distance = QLineF(cursor, point).length();
				if (distance < best) {
					best = distance;
					selected = xy;
					sample = points[i];
					sampleIndex = i;
				}
				if (!outline && distance < lineBest) {
					lineBest = distance;
					line = xy;
					lineSample = points[i];
					lineIndex = i;
				}
			}
			if (i == 0 || qobject_cast<QScatterSeries*>(xy)) continue;
			const QPointF previous = chart->mapToPosition(points[i - 1], xy);
			double first, last;
			if (!clippedSegment(chart->plotArea(), previous, point, first, last)) continue;
			const QPointF delta = point - previous;
			const double length2 = QPointF::dotProduct(delta, delta);
			const double t = length2 > 0 ? std::clamp(QPointF::dotProduct(cursor - previous, delta) / length2, first, last) : first;
			const double distance = QLineF(cursor, previous + t * delta).length();
			const int nearest = t < 0.5 ? i - 1 : i;
			if (distance < best) {
				best = distance;
				selected = xy;
				sample = points[nearest];
				sampleIndex = nearest;
			}
			if (!outline && distance < lineBest) {
				lineBest = distance;
				line = xy;
				lineSample = points[nearest];
				lineIndex = nearest;
			}
		}
	}
	if (filled) {
		hit.inside = !line;
		selected = line ? line : filled;
		sample = line ? lineSample : filledSample;
		sampleIndex = line ? lineIndex : filledIndex;
	}
	if (!selected) {
		if (const auto* overlay = sectionOverlayOf(m_sectionOverlay)) {
			const QString text = overlay->inspect(cursor, false);
			if (!text.isEmpty()) hit.lines = text.split('\n');
		}
		return hit;
	}
	hit.series = selected;
	QString identity = selected->name();
	if (!identity.contains(groupIdForSeries(selected))) identity.prepend(groupIdForSeries(selected) + " ");
	// A box can name itself, such as "IC1723-2 on 12-B0", also near its outline.
	const QString title = selected->property("inspectionTitle").toString();
	if (!title.isEmpty()) identity = title;
	// The corner of a box nearest the pointer says nothing about the box, so a box gives only its own lines.
	hit.lines = hit.inside || !title.isEmpty() ? QStringList{identity} : sampleLines(sample, identity);
	const QStringList contexts = selected->property("inspectionPoints").toStringList();
	if (sampleIndex >= 0 && sampleIndex < contexts.size() && !contexts[sampleIndex].isEmpty())
		hit.lines << contexts[sampleIndex].split('\n');
	const QString interval = selected->property("inspectionInterval").toString();
	if (!interval.isEmpty()) hit.lines << interval.split('\n');
	return hit;
}

// Shows the tooltip of what the chart shows at a point, one fact per line, and gives it back.
DiagramWindow::ChartHit DiagramWindow::inspectAt(const QPoint& position) {
	clearTooltip();
	ChartHit hit = hitAt(position);
	if (!hit.lines.isEmpty()) {
		m_tooltip->setText(hit.lines.join('\n')); // PlainText, so authored ids cannot inject rich text.
		showTooltip(position);
	}
	return hit;
}

void DiagramWindow::showTooltip(const QPoint& position) {
	m_tooltip->setWordWrap(true);
	m_tooltip->setMaximumWidth(std::max(100, std::min(480, width() - 24)));
	m_tooltip->adjustSize();
	QPoint local = m_view->viewport()->mapTo(this, position) + QPoint(16, 20);
	if (local.x() + m_tooltip->width() > width()) local.setX(local.x() - m_tooltip->width() - 32);
	if (local.y() + m_tooltip->height() > height()) local.setY(local.y() - m_tooltip->height() - 40);
	local.setX(std::clamp(local.x(), 0, std::max(0, width() - m_tooltip->width())));
	local.setY(std::clamp(local.y(), 0, std::max(0, height() - m_tooltip->height())));
	m_tooltip->move(local);
	m_tooltip->show();
	m_tooltip->raise();
}

void DiagramWindow::navigate(double factor, const QPointF& position, const QPointF& pan) {
	clearTooltip();
	if (!std::isfinite(factor) || factor <= 0) return;
	const QRectF plot = m_view->chart()->plotArea();
	if (plot.width() <= 0 || plot.height() <= 0) return;
	QVector<QPair<QValueAxis*, QPair<double, double>>> destinations;
	for (auto it = m_fullBounds.cbegin(); it != m_fullBounds.cend(); ++it) {
		auto* axis = qobject_cast<QValueAxis*>(it.key());
		if (!axis) continue;
		const double full = it.value().second - it.value().first;
		if (!(full > 0)) continue;
		const bool horizontal = axis->orientation() == Qt::Horizontal;
		double fraction = horizontal ? (position.x() - plot.left()) / plot.width() : (plot.bottom() - position.y()) / plot.height();
		fraction = std::clamp(fraction, 0.0, 1.0);
		if (axis->isReverse()) fraction = 1 - fraction;
		const double span = axis->max() - axis->min();
		const double next = std::clamp(span / factor, full / 1000000, full);
		double shift = horizontal ? -pan.x() / plot.width() : pan.y() / plot.height();
		if (axis->isReverse()) shift = -shift;
		const double low = std::clamp(axis->min() + fraction * (span - next) + shift * span,
			it.value().first, it.value().second - next);
		destinations.append({axis, {low, low + next}});
	}
	// Attached axes can synchronize when any range changes. Capture all ranges
	// before the first mutation so each receives exactly one navigation step.
	for (const auto& destination : destinations)
		destination.first->setRange(destination.second.first, destination.second.second);
}

bool DiagramWindow::eventFilter(QObject* obj, QEvent* ev) {
	if (ev->type() == QEvent::Hide || ev->type() == QEvent::Close || ev->type() == QEvent::Leave)
		clearTooltip();
	if (!m_view || obj != m_view->viewport() || !m_view->chart())
		return QDialog::eventFilter(obj, ev);
	const auto chartPosition = [this](const QPoint& p) { return m_view->chart()->mapFromScene(m_view->mapToScene(p)); };
	if ((ev->type() == QEvent::Leave || ev->type() == QEvent::Resize) && m_readout) {
		// A new size of the chart, as when the panel shows or hides, moves the plot from under the pointer.
		clearTooltip();
		m_readout->clear();
	} else if (ev->type() == QEvent::Enter) {
		// The pointer that comes into the plot reads its value at once, before it moves there.
		updateCursorReadout(chartPosition(static_cast<QEnterEvent*>(ev)->position().toPoint()));
	} else if (ev->type() == QEvent::MouseMove) {
		auto* mouse = static_cast<QMouseEvent*>(ev);
		updateCursorReadout(chartPosition(mouse->pos()));
		if (mouse->buttons() == Qt::NoButton) inspectAt(mouse->pos());
		else clearTooltip();
	} else if (ev->type() == QEvent::MouseButtonPress) {
		m_pressPosition = static_cast<QMouseEvent*>(ev)->pos();
		clearTooltip();
	} else if (ev->type() == QEvent::MouseButtonRelease) {
		auto* mouse = static_cast<QMouseEvent*>(ev);
		if (mouse->button() == Qt::LeftButton && (mouse->pos() - m_pressPosition).manhattanLength() < QApplication::startDragDistance()) {
			if (const ChartHit hit = inspectAt(mouse->pos()); hit.series) select(hit);
		} else if (mouse->button() == Qt::LeftButton) {
			// The chart view zooms into the dragged rectangle once it has the release, which changes the value under the pointer.
			QMetaObject::invokeMethod(
				this,
				[this, position = mouse->pos()] {
					if (m_view && m_view->chart()) updateCursorReadout(m_view->chart()->mapFromScene(m_view->mapToScene(position)));
				},
				Qt::QueuedConnection);
		}
	} else if (ev->type() == QEvent::Wheel) {
		auto* wheel = static_cast<QWheelEvent*>(ev);
		const QPoint pixels = wheel->pixelDelta();
		const QPoint angles = wheel->angleDelta();
		if (pixels.isNull() && angles.isNull()) {
			wheel->accept();
			return true;
		}
		if (wheel->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)) {
			const double delta = pixels.isNull()
				? (angles.y() != 0 ? angles.y() : angles.x()) / 120.0
				: (pixels.y() != 0 ? pixels.y() : pixels.x()) / 100.0;
			navigate(std::exp(std::clamp(delta * 0.2, -2.0, 2.0)), chartPosition(wheel->position().toPoint()));
		} else {
			QPointF pan = pixels.isNull() ? QPointF(angles) / 3 : QPointF(pixels);
			if (wheel->modifiers() & Qt::ShiftModifier) pan = QPointF(pan.y(), pan.x());
			navigate(1, chartPosition(wheel->position().toPoint()), pan);
		}
		// The value under the pointer changes with the axes.
		updateCursorReadout(chartPosition(wheel->position().toPoint()));
		wheel->accept();
		return true;
	} else if (ev->type() == QEvent::NativeGesture) {
		auto* gesture = static_cast<QNativeGestureEvent*>(ev);
		if (gesture->gestureType() == Qt::ZoomNativeGesture) {
			navigate(1 + gesture->value(), chartPosition(gesture->position().toPoint()));
			gesture->accept();
			return true;
		}
	} else if (ev->type() == QEvent::Gesture) {
		auto* event = static_cast<QGestureEvent*>(ev);
		if (auto* pinch = static_cast<QPinchGesture*>(event->gesture(Qt::PinchGesture))) {
			if (pinch->changeFlags() & QPinchGesture::ScaleFactorChanged)
				navigate(pinch->scaleFactor(), chartPosition(pinch->centerPoint().toPoint()));
			event->accept(pinch);
			return true;
		}
	}
	return QDialog::eventFilter(obj, ev);
}

void DiagramWindow::keyPressEvent(QKeyEvent* event) {
	if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier && !m_selectedLines.isEmpty()) {
		clearPin();
		event->accept();
		return;
	}
	QDialog::keyPressEvent(event);
}
