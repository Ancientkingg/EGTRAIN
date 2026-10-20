#include "diagrams/DiagramWindow.h"
#include "app/DialogFolders.h"
#include "diagrams/TrainFilterButton.h"
#include "util/TimeFormat.h"
#include "widgets/PlainToolTip.h"

#include <QApplication>
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
#include <QPinchGesture>
#include <QShortcut>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QAreaSeries>
#include <algorithm>
#include <QColor>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSaveFile>
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

// Reduced-opacity pen for lines that are not the pinned train.
QPen mutedPen(const QPen& base) {
	QPen pen = base;
	QColor color = pen.color();
	color.setAlpha(60);
	pen.setColor(color);
	return pen;
}

// Takes the colour of a train for a line or area series and keeps its widths,
// dash patterns and the alpha of an area fill.
void paintSeries(QAbstractSeries* series, const QColor& color) {
	if (auto* xy = qobject_cast<QXYSeries*>(series)) {
		QPen pen = xy->pen();
		pen.setColor(color);
		xy->setPen(pen);
	} else if (auto* area = qobject_cast<QAreaSeries*>(series)) {
		QBrush brush = area->brush();
		QColor fill = color;
		fill.setAlpha(brush.color().alpha());
		brush.setColor(fill);
		area->setBrush(brush);
		QPen pen = area->pen();
		pen.setColor(color);
		area->setPen(pen);
	}
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

// Planned series are drawn dashed and have their own legend entry; both rules use
// this one test of the series name.
bool isPlannedSeries(const QAbstractSeries* series) {
	return series->name().contains("planned", Qt::CaseInsensitive);
}

// The kind of line a series draws, as the legend names it: the layer its chart builder gave it, else planned or simulated.
QString lineKind(const QAbstractSeries* series) {
	QString kind = series->property("layer").toString();
	if (kind.isEmpty()) kind = isPlannedSeries(series) ? QStringLiteral("planned") : QStringLiteral("simulated");
	kind[0] = kind[0].toUpper();
	return kind;
}

constexpr QMargins kChartMargins(12, 8, 12, 8);

// Puts the ticks of a numeric axis on round values: the multiples of 1, 2 or 5 times a power of ten that divide its range
// into at most eight steps, labelled with as many decimals as the step has.
void roundTicks(QValueAxis* axis) {
	const double span = axis->max() - axis->min();
	if (!(span > 0.0) || !std::isfinite(span)) return;
	const double power = std::pow(10.0, std::floor(std::log10(span / 8)));
	double step = 10 * power;
	for (const double factor : {1.0, 2.0, 5.0})
		if (span / (factor * power) <= 8) {
			step = factor * power;
			break;
		}
	axis->setTickType(QValueAxis::TicksDynamic);
	axis->setTickInterval(step);
	// Qt reaches each tick by adding the step to the one before, so the tick at zero can end a hair below zero and read
	// "-0.0". In a range that holds zero, an anchor a millionth of a step above zero keeps that tick above it.
	axis->setTickAnchor(axis->min() < 0 && axis->max() >= 0 ? step / 1e6 : 0.0);
	const int decimals = std::max(0, static_cast<int>(-std::floor(std::log10(step) + 1e-9)));
	axis->setLabelFormat(QStringLiteral("%.%1f").arg(decimals));
}

// A legend below the plot with an entry for each kind of line of the chart: a short sample and the name of the kind. The
// entries wrap into rows that fit the chart, and the chart widens its bottom margin to keep them clear of the axes.
class LineKindLegend : public QGraphicsObject {
public:
	struct Kind {
		QString name;
		QPen pen;	 // the sample line, or the outline of a box
		bool filled; // drawn as a box, like a filled envelope
	};
	LineKindLegend(QChart* chart, std::vector<Kind> kinds, const QFont& font, const QColor& textColor)
		: QGraphicsObject(chart), m_chart(chart), m_kinds(std::move(kinds)), m_font(font), m_textColor(textColor) {
		QObject::setParent(chart);
		setObjectName("lineKindLegend");
		setAcceptedMouseButtons(Qt::NoButton);
		QStringList names;
		for (const Kind& kind : m_kinds) names << kind.name;
		setProperty("kinds", names);
		connect(chart, &QGraphicsWidget::geometryChanged, this, [this] { reserveSpace(); });
		reserveSpace();
	}
	QRectF boundingRect() const override { return m_area; }
	void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override {
		const QFontMetricsF metrics(m_font);
		painter->save();
		painter->setFont(m_font);
		for (const auto& entry : m_placed) {
			const Kind& kind = m_kinds[entry.first];
			const QRectF sample(entry.second, QSizeF(kSample, metrics.height()));
			painter->setPen(kind.pen);
			if (kind.filled) {
				QColor fill = kind.pen.color();
				fill.setAlpha(40);
				painter->setBrush(fill);
				painter->drawRect(sample.adjusted(1, 3, -1, -3));
				painter->setBrush(Qt::NoBrush);
			} else {
				painter->drawLine(QPointF(sample.left(), sample.center().y()), QPointF(sample.right(), sample.center().y()));
			}
			painter->setPen(m_textColor);
			painter->drawText(QPointF(sample.right() + kGap, sample.top() + metrics.ascent()), kind.name);
		}
		painter->restore();
	}

private:
	static constexpr double kSample = 24, kGap = 6, kSpacing = 16;
	void reserveSpace() {
		const QFontMetricsF metrics(m_font);
		const QRectF chart = m_chart->rect();
		const double room = chart.width() - kChartMargins.left() - kChartMargins.right();
		std::vector<std::vector<std::pair<std::size_t, double>>> rows; // (kind, width) from left to right
		double used = room;
		for (std::size_t i = 0; i < m_kinds.size(); ++i) {
			const double width = kSample + kGap + metrics.horizontalAdvance(m_kinds[i].name);
			if (rows.empty() || used + kSpacing + width > room) {
				rows.emplace_back();
				used = -kSpacing;
			}
			rows.back().emplace_back(i, width);
			used += kSpacing + width;
		}
		const double height = metrics.height() * static_cast<double>(rows.size());
		prepareGeometryChange();
		m_area = QRectF(chart.left(), chart.bottom() - kChartMargins.bottom() - height, chart.width(), height);
		m_placed.clear();
		for (std::size_t row = 0; row < rows.size(); ++row) {
			double width = -kSpacing;
			for (const auto& entry : rows[row]) width += kSpacing + entry.second;
			double x = chart.center().x() - width / 2;
			for (const auto& entry : rows[row]) {
				m_placed.emplace_back(entry.first, QPointF(x, m_area.top() + metrics.height() * static_cast<double>(row)));
				x += entry.second + kSpacing;
			}
		}
		QMargins margins = m_chart->margins();
		margins.setBottom(kChartMargins.bottom() + static_cast<int>(std::ceil(height)));
		if (margins != m_chart->margins()) m_chart->setMargins(margins);
		update();
	}
	QChart* m_chart;
	std::vector<Kind> m_kinds;
	QFont m_font;
	QColor m_textColor;
	QRectF m_area;
	std::vector<std::pair<std::size_t, QPointF>> m_placed; // kind and top left of its sample
};

// One chart-owned item, outside the train series/filter identity space. Geometry is mapped
// from the live numeric distance axis; font sizes and the header stay in chart pixels.
class BlockSectionOverlay : public QGraphicsObject {
public:
	BlockSectionOverlay(QChart* chart, std::vector<BlockSectionExtent> sections,
		const QString& route, const QFont& font, const QPalette& palette)
		: QGraphicsObject(chart), m_chart(chart), m_sections(std::move(sections)),
		  m_route(route), m_font(font), m_palette(palette) {
		QObject::setParent(chart);
		setObjectName("blockSectionOverlay");
		setAcceptedMouseButtons(Qt::NoButton);
		setZValue(100);
		setProperty("sectionCount", static_cast<int>(m_sections.size()));
		m_notice = m_sections.empty() ? "Reference sections unavailable; no physical geometry shown."
									  : QString::fromStdString(blockSectionGeometryNote(m_sections)).trimmed();
		setProperty("geometryNotice", m_notice);
		connect(chart, &QChart::plotAreaChanged, this, [this] {
			prepareGeometryChange();
			m_bounds = m_chart->boundingRect();
			update();
		});
		m_bounds = chart->boundingRect();
		watchAxes();
	}
	QRectF boundingRect() const override { return m_bounds; }
	void watchAxes() {
		for (auto* axis : m_chart->axes()) {
			if (auto* value = qobject_cast<QValueAxis*>(axis)) {
				// Time-axis replacement changes chart layout as well as ranges.
				if (m_axes.contains(value)) continue;
				m_axes.insert(value);
				connect(value, &QValueAxis::rangeChanged, this, [this] { update(); });
				connect(value, &QAbstractAxis::reverseChanged, this, [this] { update(); });
				connect(value, &QObject::destroyed, this, [this, value] { m_axes.remove(value); });
			}
		}
		update();
	}
	double headerHeight() const { return stripHeight() * (m_notice.isEmpty() || m_sections.empty() ? 1 : 2); }
	bool drawsBoundaries() const {
		return std::any_of(m_sections.begin(), m_sections.end(), [](const BlockSectionExtent& section) { return section.geometryValid; });
	}
	QString inspect(const QPointF& point, bool headerOnly) const {
		const auto* axis = distanceAxis();
		const QRectF plot = m_chart->plotArea();
		const QRectF header = headerRect();
		if (!plot.contains(point) && !header.contains(point)) return {};
		const bool inHeader = !m_sections.empty() && header.contains(point) && point.y() < header.top() + stripHeight();
		const bool inNotice = header.contains(point) && !m_notice.isEmpty() && !inHeader;
		if (headerOnly && !inHeader && !inNotice) return {};
		QStringList details;
		const BlockSectionExtent* labelHit = nullptr;
		for (const auto& label : renderedLabels())
			if (label.rect.contains(point)) {
				labelHit = label.section;
				break;
			}
		if (axis)
			for (const auto& section : m_sections) {
				if (!section.geometryValid) continue;
				const double start = pixelX(section.startKm, axis), end = pixelX(section.endKm, axis);
				// A visible label wins over neighbouring intervals; coincident occurrences remain inspectable.
				const bool labelMatches = labelHit && section.lowerKm() == labelHit->lowerKm() && section.upperKm() == labelHit->upperKm();
				if ((inHeader && (labelHit ? labelMatches : point.x() >= std::min(start, end) - 3 && point.x() <= std::max(start, end) + 3))
					|| (!headerOnly && plot.contains(point) && std::min(std::abs(point.x() - start), std::abs(point.x() - end)) <= 4))
					details << QString("Physical reference section %1:%2 | Route: %3 | Directed X: %4 to %5 km")
								   .arg(section.order)
								   .arg(QString::fromStdString(section.id), m_route)
								   .arg(section.startKm, 0, 'g', 12)
								   .arg(section.endKm, 0, 'g', 12);
			}
		if (inNotice) {
			details << m_notice;
			for (const auto& section : m_sections)
				if (!section.geometryValid)
					details << QString("%1:%2 | Geometry unavailable: %3").arg(section.order).arg(QString::fromStdString(section.id), QString::fromStdString(section.issue));
		}
		return details.join('\n');
	}
	void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override {
		const QRectF plot = m_chart->plotArea();
		const auto* axis = distanceAxis();
		painter->save();
		painter->setClipRect(plot);
		painter->setFont(m_font);
		QColor stroke = m_palette.color(QPalette::Text);
		stroke.setAlpha(100);
		painter->setPen(QPen(stroke, 1, Qt::DotLine));
		if (axis)
			for (const auto& section : m_sections) {
				if (!section.geometryValid) continue;
				for (double position : {section.startKm, section.endKm}) {
					const double x = pixelX(position, axis);
					painter->drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
				}
			}
		const double height = stripHeight();
		const QRectF header = headerRect();
		painter->setClipRect(header);
		painter->fillRect(header, m_palette.brush(QPalette::Base));
		painter->setPen(QPen(m_palette.color(QPalette::Text), 1));
		if (axis)
			for (const auto& section : m_sections) {
				if (!section.geometryValid) continue;
				const double start = pixelX(section.startKm, axis), end = pixelX(section.endKm, axis);
				const double left = std::max(plot.left(), std::min(start, end));
				const double right = std::min(plot.right(), std::max(start, end));
				if (right <= left) continue;
				const double y = header.top() + height - 3;
				painter->drawLine(QPointF(left, y), QPointF(right, y));
				for (double x : {start, end}) painter->drawLine(QPointF(x, y - 3), QPointF(x, y + 2));
			}
		for (const auto& label : renderedLabels())
			painter->drawText(label.rect, Qt::AlignCenter, label.text);
		const QFontMetricsF metrics(m_font);
		if (!m_notice.isEmpty()) {
			const QRectF notice(plot.left(), header.top() + (m_sections.empty() ? 0 : height), plot.width(), height);
			painter->fillRect(notice, m_palette.brush(QPalette::Base));
			painter->drawText(notice.adjusted(4, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
				metrics.elidedText(m_notice, Qt::ElideRight, plot.width() - 8));
		}
		painter->restore();
	}
private:
	struct Label {
		const BlockSectionExtent* section;
		QRectF rect;
		QString text;
	};
	QRectF headerRect() const {
		const QRectF plot = m_chart->plotArea();
		return QRectF(plot.left(), plot.top() - headerHeight(), plot.width(), headerHeight());
	}
	// Painting and inspection use the same live screen-space rectangles, including expanded labels.
	std::vector<Label> renderedLabels() const {
		std::vector<Label> labels;
		const auto* axis = distanceAxis();
		const QRectF plot = m_chart->plotArea();
		if (!axis || plot.width() <= 8) return labels;
		for (const auto& section : m_sections) {
			if (!section.geometryValid) continue;
			const double start = pixelX(section.startKm, axis), end = pixelX(section.endKm, axis);
			const double left = std::max(plot.left(), std::min(start, end));
			const double right = std::min(plot.right(), std::max(start, end));
			if (right > left) labels.push_back({&section, QRectF(left, headerRect().top(), right - left, stripHeight() - 5),
				QString("%1:%2").arg(section.order).arg(QString::fromStdString(section.id))});
		}
		std::stable_sort(labels.begin(), labels.end(), [](const auto& a, const auto& b) { return a.rect.left() < b.rect.left(); });
		const QFontMetricsF metrics(m_font);
		double previousRight = plot.left() - 6;
		labels.erase(std::remove_if(labels.begin(), labels.end(), [&](auto& label) {
			// Neighbours that collide are thinned out below; text is shortened only when it exceeds this cap.
			const double budget = std::min(plot.width() - 8, 240.0);
			label.text = metrics.elidedText(label.text, Qt::ElideMiddle, budget);
			const double width = metrics.horizontalAdvance(label.text);
			const double left = std::max(plot.left(), std::min(plot.right() - width, label.rect.center().x() - width / 2));
			if (label.text.isEmpty() || left < previousRight + 6) return true;
			label.rect.setLeft(left);
			label.rect.setWidth(width);
			previousRight = left + width;
			return false;
		}),
			labels.end());
		return labels;
	}
	QValueAxis* distanceAxis() const {
		const auto axes = m_chart->axes(Qt::Horizontal);
		return axes.isEmpty() ? nullptr : qobject_cast<QValueAxis*>(axes.first());
	}
	double stripHeight() const { return QFontMetricsF(m_font).height() + 10; }
	double pixelX(double km, const QValueAxis* axis) const {
		const QRectF plot = m_chart->plotArea();
		const double fraction = (km - axis->min()) / (axis->max() - axis->min());
		return plot.left() + (axis->isReverse() ? 1 - fraction : fraction) * plot.width();
	}
	QChart* m_chart;
	std::vector<BlockSectionExtent> m_sections;
	QString m_route, m_notice;
	QFont m_font;
	QPalette m_palette;
	QRectF m_bounds;
	QSet<QValueAxis*> m_axes;
};

// The window header cannot name the file-local overlay type, so it keeps the item as a QGraphicsObject.
BlockSectionOverlay* sectionOverlayOf(const QPointer<QGraphicsObject>& overlay) {
	return static_cast<BlockSectionOverlay*>(overlay.data());
}

// The label each station gets above the plot, as (position, plain text). Every label is centred on its station, must lie
// within the chart and may not touch its neighbour. The chart shortens a label that is wider than the share of the plot
// width that it gives each of the labels, so a label is only used when it is narrower than that share. Of the stations
// in the range, every first, second, third and so on is tried until the labels fit. A station gets the longest
// spelling that fits.
std::vector<std::pair<double, QString>> labelledStations(const std::vector<StationTick>& stations, const QValueAxis& axis,
	const QRectF& plot, double chartWidth, const QFontMetricsF& metrics) {
	constexpr double edge = 4.0, gap = 8.0, slack = 16.0;
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
			const double room = std::min(share, 2.0 * std::min(x - edge, chartWidth - edge - x));
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
	m_view->setRenderHint(QPainter::Antialiasing);
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
	// The shortcuts belong to the chart view, so they only take keys while the chart has focus.
	for (const auto key : {Qt::Key_Plus, Qt::Key_Equal, Qt::Key_Minus, Qt::Key_0, Qt::Key_Home,
			 Qt::Key_Left, Qt::Key_Right, Qt::Key_Up, Qt::Key_Down}) {
		auto* shortcut = new QShortcut(QKeySequence(key), m_view);
		shortcut->setContext(Qt::WidgetWithChildrenShortcut);
		connect(shortcut, &QShortcut::activated, this, [this, key] {
			if (key == Qt::Key_0 || key == Qt::Key_Home) {
				resetZoom();
				return;
			}
			QPointF pan;
			if (key == Qt::Key_Left) pan.setX(40);
			if (key == Qt::Key_Right) pan.setX(-40);
			if (key == Qt::Key_Up) pan.setY(40);
			if (key == Qt::Key_Down) pan.setY(-40);
			navigate(pan.isNull() ? (key == Qt::Key_Minus ? 1 / 1.2 : 1.2) : 1,
				m_view->chart()->plotArea().center(), pan);
		});
	}
	// Drag a rectangle to zoom; filter state is unaffected by zoom or pan.
	m_view->setRubberBand(QChartView::RectangleRubberBand);

	// Top bar: train visibility dropdown, pin state, zoom reset, exports.
	m_trainsButton = new TrainFilterButton(this);
	// The text size of the push buttons beside it; macOS gives a tool button a smaller font.
	m_trainsButton->setFont(QApplication::font());
	connect(m_trainsButton, &TrainFilterButton::selectionChanged,
		this, &DiagramWindow::applyTrainVisibility);

	m_clearPinButton = new QPushButton("Clear selection", this);
	connect(m_clearPinButton, &QPushButton::clicked, this, &DiagramWindow::clearPin);
	m_pinLabel = new QLabel("", this);
	m_pinLabel->setObjectName("diagramPinLabel");
	// Shown only while a train is selected: an empty label would still take a gap in the top bar.
	m_pinLabel->hide();

	QPushButton* resetZoomBtn = new QPushButton("Reset zoom", this);
	resetZoomBtn->setToolTip("Restore full bounds; keep train filters and selection (Home or 0)");
	connect(resetZoomBtn, &QPushButton::clicked, this, &DiagramWindow::resetZoom);

	m_csvButton = new QPushButton("Export CSV...", this);
	m_csvButton->setToolTip("Write the raw data of the visible trains to a CSV file");
	m_csvButton->hide();
	connect(m_csvButton, &QPushButton::clicked, this, &DiagramWindow::exportCsv);

	QPushButton* exportPngBtn = new QPushButton("Export PNG...", this);
	exportPngBtn->setToolTip("Save the chart as an image");
	connect(exportPngBtn, &QPushButton::clicked, this, &DiagramWindow::exportPng);

	// No button of the window is a default button, so Enter presses none.
	for (QPushButton* button : findChildren<QPushButton*>(QString(), Qt::FindDirectChildrenOnly))
		button->setAutoDefault(false);

	QHBoxLayout* topBar = new QHBoxLayout();
	topBar->addWidget(m_trainsButton);
	topBar->addWidget(m_clearPinButton);
	topBar->addWidget(m_pinLabel);
	topBar->addStretch();
	topBar->addWidget(resetZoomBtn);
	topBar->addWidget(m_csvButton);
	topBar->addWidget(exportPngBtn);

	m_readout = new QLabel(this);
	m_readout->setObjectName("diagramNavigationHelp");
	m_readout->setWordWrap(true);
	m_readout->setTextFormat(Qt::PlainText);
	refreshNavigationHelp();
	m_warningLabel = new QLabel(this);
	m_warningLabel->setObjectName("diagramWarning");
	m_warningLabel->setTextFormat(Qt::PlainText);
	m_warningLabel->setWordWrap(true);
	m_warningLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	QSizePolicy warningPolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);
	warningPolicy.setHeightForWidth(true);
	m_warningLabel->setSizePolicy(warningPolicy);
	m_warningLabel->hide();
	m_contextLabel = new QLabel(this);
	m_contextLabel->setObjectName("diagramContext");
	m_contextLabel->setTextFormat(Qt::PlainText);
	m_contextLabel->setWordWrap(true);
	m_contextLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	m_contextLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_contextLabel->setMaximumHeight(fontMetrics().height() * 3);
	m_contextLabel->hide();

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->setSpacing(8);
	layout->addLayout(topBar);
	layout->addWidget(m_warningLabel);
	layout->addWidget(m_contextLabel);
	layout->addWidget(m_view, 1);
	layout->addWidget(m_readout);
}

void DiagramWindow::setPresentation(const QString& heading, const QString& context,
	const QString& warning) {
	setWindowTitle(heading);
	m_warningLabel->setText(warning);
	m_warningLabel->setVisible(!warning.isEmpty());
	m_contextLabel->setMaximumHeight(m_contextLabel->fontMetrics().height() * 3);
	m_contextLabel->setText(context);
	m_contextLabel->setToolTip(plainToolTip(context));
	m_contextLabel->setVisible(!context.isEmpty());
}

void DiagramWindow::setRollingStockSubject(bool on) {
	m_rollingStockSubject = on;
	m_trainsButton->setVisible(!on);
	m_clearPinButton->setVisible(!on);
	m_pinLabel->setVisible(!on && !m_pinnedTrainId.isEmpty());
	refreshNavigationHelp();
}

void DiagramWindow::refreshNavigationHelp() {
	if (!m_readout) return;
	const QString navigation = QStringLiteral(
		"Drag to zoom; two-finger scroll to pan; pinch or Ctrl+wheel to zoom. +/- zoom, arrows pan, Home resets.");
	m_readout->setText((m_rollingStockSubject ? QStringLiteral("Input tractive effort by speed. ")
											  : QStringLiteral("Hover to inspect; click to select. "))
		+ navigation);
}

// A chart with more than one kind of line gets a legend of the kinds. A sample takes the line style and width of the
// first series of its kind in the text colour, since the colour of a series belongs to its train.
void DiagramWindow::refreshLegend() {
	QChart* chart = m_view ? m_view->chart() : nullptr;
	if (!chart) return;
	delete m_lineLegend.data();
	chart->setMargins(kChartMargins);
	const QColor text = palette().color(QPalette::WindowText);
	std::vector<LineKindLegend::Kind> kinds;
	for (QAbstractSeries* series : chart->series()) {
		const auto* xy = qobject_cast<QXYSeries*>(series);
		if (!xy) continue; // an area series is the fill of an envelope whose outline is a line series
		const QPen pen = m_basePens.value(series, xy->pen());
		const bool filled = series->property("inspectionFilled").toBool();
		const QString name = lineKind(series);
		const auto known = std::find_if(kinds.begin(), kinds.end(), [&name](const LineKindLegend::Kind& kind) { return kind.name == name; });
		if (known == kinds.end())
			kinds.push_back({name, QPen(text, pen.widthF(), pen.style()), filled});
		else
			known->filled = known->filled || filled;
	}
	const auto* overlay = sectionOverlayOf(m_sectionOverlay);
	if (overlay && overlay->drawsBoundaries()) {
		QColor stroke = palette().color(QPalette::Text);
		stroke.setAlpha(100); // as the overlay draws the boundaries
		kinds.push_back({QStringLiteral("Physical section"), QPen(stroke, 1, Qt::DotLine), false});
	}
	if (kinds.size() > 1) m_lineLegend = new LineKindLegend(chart, std::move(kinds), font(), text);
}

void DiagramWindow::setTrainColors(const QHash<QString, QColor>& colors) {
	applyTrainColors(colors, true);
}

void DiagramWindow::setTrainListColors(const QHash<QString, QColor>& colors) {
	applyTrainColors(colors, false);
}

void DiagramWindow::applyTrainColors(const QHash<QString, QColor>& colors, bool paintSeries) {
	m_trainColors = colors;
	m_paintTrainSeries = paintSeries;
	if (!m_view || !m_view->chart())
		return;
	// The rebuild records the series as they are, so first undo a pin emphasis.
	for (QAbstractSeries* series : m_view->chart()->series()) {
		if (auto* xy = qobject_cast<QXYSeries*>(series))
			xy->setPen(m_basePens.value(series, xy->pen()));
		else if (auto* area = qobject_cast<QAreaSeries*>(series))
			area->setBrush(m_baseBrushes.value(series, area->brush()));
	}
	rebuildFilterGroups();
	applyTrainVisibility();
}

void DiagramWindow::setChart(QChart* chart) {
	if (m_view && m_view->chart() == chart) return;
	clearTooltip();
	m_sectionOverlay = nullptr;	 // the old chart owns and deletes it
	delete m_numericAxis.data(); // detached axis belongs to this window, not the old chart
	m_numericAxis = nullptr;
	m_fullBounds.clear();
	m_clockAxis = nullptr;
	m_stationAxis = nullptr;
	m_stations.clear();
	if (chart)
		applyChartStyle(chart);
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
	refreshLegend();
	if (chart)
		for (auto* axis : chart->axes())
			if (auto* value = qobject_cast<QValueAxis*>(axis)) {
				m_fullBounds.insert(axis, {value->min(), value->max()});
				// Round ticks for every numeric axis whose ticks and labels the chart builder left as Qt sets them.
				if (qobject_cast<QCategoryAxis*>(axis) || value->tickType() != QValueAxis::TicksFixed || !value->labelFormat().isEmpty())
					continue;
				roundTicks(value);
				connect(value, &QValueAxis::rangeChanged, value, [value] { roundTicks(value); });
			}
	applyTimeAxis();
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
	const auto shown = labelledStations(m_stations, *axis, chart->plotArea(), chart->size().width(), QFontMetricsF(axis->labelsFont()));
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

void DiagramWindow::setBlockSections(std::vector<BlockSectionExtent> sections, const QString& referenceRouteId) {
	clearTooltip();
	delete m_sectionOverlay.data();
	auto* chart = m_view->chart();
	auto* overlay = new BlockSectionOverlay(chart, std::move(sections), referenceRouteId, font(), palette());
	m_sectionOverlay = overlay;
	// QChart lays a top legend out between its title and the plot area, so an invisible legend as tall as the
	// strip keeps the title where it is and moves only the plot down. A larger top margin would move both.
	auto* legend = chart->legend();
	const int height = static_cast<int>(std::ceil(overlay->headerHeight()));
	legend->setAlignment(Qt::AlignTop);
	legend->setMinimumHeight(0);
	legend->setMaximumHeight(height);
	legend->setMinimumHeight(height);
	legend->setOpacity(0); // markers return whenever a train is shown again
	legend->setEnabled(false);
	legend->show();
	refreshLegend();
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

	QFont titleFont = font();
	titleFont.setPointSizeF(titleFont.pointSizeF() * 1.25);
	titleFont.setBold(true);
	chart->setTitleFont(titleFont);
	chart->setTitleBrush(palette().brush(QPalette::WindowText));

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
			if (pen.widthF() < 2.0) {
				pen.setWidthF(2.0);
				xy->setPen(pen);
			}
		}
	}
}

// Keep numeric ranges and orientation when replacing time labels.
void DiagramWindow::applyTimeAxis() {
	QChart* chart = m_view ? m_view->chart() : nullptr;
	if (!chart) return;
	if (m_clockAxis) {
		auto* old = qobject_cast<QValueAxis*>(m_clockAxis.data());
		const auto alignment = old->alignment();
		const auto bounds = m_fullBounds.take(old);
		auto* numeric = m_numericAxis.data();
		if (!numeric) return;
		numeric->setRange(old->min(), old->max());
		numeric->setReverse(old->isReverse());
		QList<QAbstractSeries*> attached;
		for (auto* series : chart->series())
			if (series->attachedAxes().contains(old)) attached.append(series);
		chart->removeAxis(old);
		old->deleteLater();
		chart->addAxis(numeric, alignment);
		for (auto* series : attached) series->attachAxis(numeric);
		m_fullBounds.insert(numeric, bounds);
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
	const double min = seconds->min();
	const double max = seconds->max();
	const double span = max - min;
	if (!(span > 0.0))
		return;


	auto* clock = new QCategoryAxis(chart);
	clock->setLabelsPosition(QCategoryAxis::AxisLabelsPositionOnValue);
	clock->setStartValue(min - 1);
	clock->setReverse(seconds->isReverse());
	clock->setMin(min);
	clock->setMax(max);
	clock->setTitleText("Time");
	clock->setGridLinePen(seconds->gridLinePen());
	clock->setGridLineVisible(seconds->isGridLineVisible());
	clock->setMinorGridLineVisible(seconds->isMinorGridLineVisible());
	clock->setLinePen(seconds->linePen());
	clock->setLabelsFont(seconds->labelsFont());
	clock->setTitleFont(seconds->titleFont());
	clock->setLabelsBrush(palette().brush(QPalette::WindowText));
	clock->setTitleBrush(palette().brush(QPalette::WindowText));


	QList<QAbstractSeries*> seriesList;
	for (auto* series : chart->series())
		if (series->attachedAxes().contains(seconds)) seriesList.append(series);
	const auto alignment = seconds->alignment();
	const auto bounds = m_fullBounds.take(seconds);
	chart->removeAxis(seconds);
	seconds->setParent(this); // keep the original numeric ticks and grid for the roundtrip
	m_numericAxis = seconds;
	chart->addAxis(clock, alignment);
	m_fullBounds.insert(clock, bounds);
	for (QAbstractSeries* series : seriesList)
		series->attachAxis(clock);
	m_clockAxis = clock;
	const auto updateLabels = [this, clock](qreal low, qreal high) {
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
		const auto append = [this, clock](double tick) {
			clock->append(QString::fromStdString(formatSimTime(static_cast<long long>(tick), m_startOffset)), tick);
		};
		// Ticks fall on round clock times. Elapsed zero, the run start, keeps a tick of its own while it is in range, and a
		// round tick less than a step from it is left out.
		const bool runStart = low <= 0 && high >= 0;
		bool startPending = runStart;
		const double offset = static_cast<double>(m_startOffset);
		// NOLINTNEXTLINE(bugprone-float-loop-counter,clang-analyzer-security.FloatLoopCounter): ticks are placed at multiples of the interval
		for (double tick = std::ceil((low + offset) / interval) * interval - offset; tick <= high; tick += interval) {
			if (startPending && tick > -interval) {
				append(0);
				startPending = false;
			}
			if (!runStart || std::abs(tick) >= interval) append(tick);
		}
		if (startPending) append(0);
	};
	connect(clock, &QValueAxis::rangeChanged, this, updateLabels);
	updateLabels(min, max);
	if (auto* overlay = sectionOverlayOf(m_sectionOverlay)) overlay->watchAxes();
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
	m_groups.clear();
	m_basePens.clear();
	m_baseBrushes.clear();
	QChart* chart = m_view ? m_view->chart() : nullptr;
	if (!chart) {
		if (m_trainsButton)
			m_trainsButton->setTrains({});
		return;
	}

	QHash<QString, int> indexByTrain;
	const auto seriesList = chart->series();
	for (QAbstractSeries* series : seriesList) {
		const QString trainId = groupIdForSeries(series);
		const QColor trainColor = m_trainColors.value(trainId);
		if (m_paintTrainSeries && trainColor.isValid())
			paintSeries(series, trainColor);
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

	if (!m_trainsButton)
		return;
	QVector<QPair<QString, QColor>> trains;
	trains.reserve(m_groups.size());
	for (const SeriesGroup& group : m_groups) {
		QColor swatch = m_trainColors.value(group.trainId);
		if (!swatch.isValid() && !group.members.isEmpty()) {
			if (auto* xy = qobject_cast<QXYSeries*>(group.members.first()))
				swatch = xy->pen().color();
		}
		trains.append({group.trainId, swatch});
	}
	m_trainsButton->setTrains(trains);
}

void DiagramWindow::applyTrainVisibility() {
	clearTooltip();
	if (!m_trainsButton)
		return;
	for (const SeriesGroup& group : m_groups) {
		const bool visible = m_trainsButton->isTrainVisible(group.trainId);
		for (QAbstractSeries* series : group.members)
			series->setVisible(visible);
	}
	refreshEmphasis();
}

void DiagramWindow::pinTrain(const QString& trainId) {
	m_pinnedTrainId = trainId;
	if (m_pinLabel) {
		m_pinLabel->setText(QString("Selected: %1").arg(trainId));
		m_pinLabel->setVisible(!m_rollingStockSubject);
	}
	refreshEmphasis();
	emit trainSelected(trainId);
}

void DiagramWindow::clearPin() {
	m_pinnedTrainId.clear();
	if (m_pinLabel) {
		m_pinLabel->clear();
		m_pinLabel->hide();
	}
	refreshEmphasis();
}

void DiagramWindow::refreshEmphasis() {
	for (const SeriesGroup& group : m_groups) {
		const bool pinnedActive = !m_pinnedTrainId.isEmpty();
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
			else
				xy->setPen(mutedPen(base));
		}
	}
}

void DiagramWindow::updateReadout(const QPointF& value, const QString& seriesName) {
	if (!m_tooltip || !m_view || !m_view->chart())
		return;

	const QChart* chart = m_view->chart();
	const auto horizontal = chart->axes(Qt::Horizontal);
	const auto vertical = chart->axes(Qt::Vertical);
	const QString xLabel = horizontal.isEmpty() || horizontal.first()->titleText().isEmpty()
		? QString("x")
		: horizontal.first()->titleText();
	const QString yLabel = vertical.isEmpty() || vertical.first()->titleText().isEmpty()
		? QString("y")
		: vertical.first()->titleText();
	const QString xText = m_timeAxis && m_timeOrientation == Qt::Horizontal
		? QString::fromStdString(formatSimTime(static_cast<long long>(value.x()), m_startOffset))
		: QString::number(value.x(), 'f', 2);
	const QString yText = m_timeAxis && m_timeOrientation == Qt::Vertical
		? QString::fromStdString(formatSimTime(static_cast<long long>(value.y()), m_startOffset))
		: QString::number(value.y(), 'f', 2);
	m_tooltip->setText(QString("%1\n%2: %3   %4: %5")
			.arg(seriesName)
			.arg(xLabel)
			.arg(xText)
			.arg(yLabel)
			.arg(yText));
}

QStringList DiagramWindow::visibleTrainIds() const {
	if (m_trainsButton)
		return m_trainsButton->visibleTrainIds();
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
	const auto operation = m_telemetryCapture ? m_telemetryCapture() : telemetry::OperationObservation();
	QString path = QFileDialog::getSaveFileName(this, "Export Diagram", dialog_folders::suggestedPath("diagram.png"), "PNG Image (*.png)");
	if (path.isEmpty())
		return; // cancelled: no file is written
	if (QFileInfo(path).suffix().compare("png", Qt::CaseInsensitive) != 0)
		path += ".png";
	QPixmap pix = m_view->grab();
	QByteArray png;
	QBuffer buffer(&png);
	if (!buffer.open(QIODevice::WriteOnly) || !pix.save(&buffer, "PNG")) {
		operation.failure(telemetry::Operation::Export, telemetry::Error::InternalFailure);
		QMessageBox::warning(this, "Export failed",
			QString("Could not write the image to:\n%1").arg(path));
		return;
	}
	const std::string bytes(png.constData(), static_cast<std::size_t>(png.size()));
	const bool written = m_provenanceWriter
		? m_provenanceWriter(path, "png", bytes)
		: writeArtifact(path, bytes);
	operation.exportFinished(telemetry::ExportKind::Png, written, true, telemetry::Error::IoFailure);
	if (!written)
		QMessageBox::warning(this, "Export failed",
			QString("Could not export the image and provenance to:\n%1").arg(path));
}

void DiagramWindow::exportCsv() {
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
	if (!written)
		QMessageBox::warning(this, "Export failed",
			QString("Could not export the data and provenance to:\n%1").arg(path));
}

void DiagramWindow::clearTooltip() {
	if (m_tooltip) m_tooltip->hide();
}

QAbstractSeries* DiagramWindow::inspectAt(const QPoint& position) {
	clearTooltip();
	auto* chart = m_view->chart();
	const QPointF cursor = chart->mapFromScene(m_view->mapToScene(position));
	if (const auto* overlay = sectionOverlayOf(m_sectionOverlay)) {
		const QString text = overlay->inspect(cursor, true);
		if (!text.isEmpty()) {
			m_tooltip->setText(text); // PlainText, so authored ids cannot inject rich text.
			showTooltip(position);
			return nullptr;
		}
	}
	if (!chart->plotArea().contains(cursor)) {
		const QString title = elidedAxisTextAt(cursor);
		if (!title.isEmpty()) {
			m_tooltip->setText(title);
			showTooltip(position);
		}
		return nullptr;
	}
	QXYSeries* selected = nullptr;
	QPointF sample;
	int sampleIndex = -1;
	double best = 10;
	QXYSeries* filled = nullptr;
	QPointF filledSample;
	int filledIndex = -1;
	double filledVertexDistance = std::numeric_limits<double>::max();
	for (auto* series : chart->series()) {
		auto* xy = qobject_cast<QXYSeries*>(series);
		if (!xy || !xy->isVisible()) continue;
		const auto points = xy->points();
		bool inside = false;
		if (xy->property("inspectionFilled").toBool() && points.size() >= 4
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
			}
			if (i == 0 || qobject_cast<QScatterSeries*>(xy)) continue;
			const QPointF previous = chart->mapToPosition(points[i - 1], xy);
			double first, last;
			if (!clippedSegment(chart->plotArea(), previous, point, first, last)) continue;
			const QPointF delta = point - previous;
			const double length2 = QPointF::dotProduct(delta, delta);
			const double t = length2 > 0 ? std::clamp(QPointF::dotProduct(cursor - previous, delta) / length2, first, last) : first;
			const double distance = QLineF(cursor, previous + t * delta).length();
			if (distance < best) {
				best = distance;
				const int nearest = t < 0.5 ? i - 1 : i;
				selected = xy;
				sample = points[nearest];
				sampleIndex = nearest;
			}
		}
	}
	if (!selected && filled) {
		selected = filled;
		sample = filledSample;
		sampleIndex = filledIndex;
	}
	if (!selected) {
		if (const auto* overlay = sectionOverlayOf(m_sectionOverlay)) {
			const QString text = overlay->inspect(cursor, false);
			if (!text.isEmpty()) {
				m_tooltip->setText(text);
				showTooltip(position);
			}
		}
		return nullptr;
	}
	QString identity = selected->name();
	if (!identity.contains(groupIdForSeries(selected))) identity.prepend(groupIdForSeries(selected) + " ");
	updateReadout(sample, identity + " (nearest sample)");
	const QStringList contexts = selected->property("inspectionPoints").toStringList();
	if (sampleIndex >= 0 && sampleIndex < contexts.size() && !contexts[sampleIndex].isEmpty())
		m_tooltip->setText(m_tooltip->text() + "\n" + contexts[sampleIndex]);
	const QString interval = selected->property("inspectionInterval").toString();
	if (!interval.isEmpty()) m_tooltip->setText(m_tooltip->text() + "\n" + interval);
	showTooltip(position);
	return selected;
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
	if (ev->type() == QEvent::MouseMove) {
		auto* mouse = static_cast<QMouseEvent*>(ev);
		if (mouse->buttons() == Qt::NoButton) inspectAt(mouse->pos());
		else clearTooltip();
	} else if (ev->type() == QEvent::MouseButtonPress) {
		m_pressPosition = static_cast<QMouseEvent*>(ev)->pos();
		clearTooltip();
	} else if (ev->type() == QEvent::MouseButtonRelease) {
		auto* mouse = static_cast<QMouseEvent*>(ev);
		if (mouse->button() == Qt::LeftButton && (mouse->pos() - m_pressPosition).manhattanLength() < QApplication::startDragDistance())
			if (auto* series = inspectAt(mouse->pos())) pinTrain(groupIdForSeries(series));
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
		wheel->accept();
		return true;
	} else if (ev->type() == QEvent::NativeGesture) {
		auto* gesture = static_cast<QNativeGestureEvent*>(ev);
		if (gesture->gestureType() == Qt::ZoomNativeGesture) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
			navigate(1 + gesture->value(), chartPosition(gesture->position().toPoint()));
#else
			navigate(1 + gesture->value(), chartPosition(gesture->localPos().toPoint()));
#endif
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
