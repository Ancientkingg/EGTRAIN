#ifndef BLOCKSECTIONOVERLAY_H
#define BLOCKSECTIONOVERLAY_H

#include "diagrams/BlockingTimeDiagram.h"
#include "diagrams/DiagramKey.h"
#include "diagrams/PositionScale.h"
#include "scene/SectionLabels.h"

#include <QColor>
#include <QFont>
#include <QGraphicsObject>
#include <QPalette>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

QT_BEGIN_NAMESPACE
class QChart;
class QValueAxis;
QT_END_NAMESPACE

/// The rule between the cells of the section strip, and between a chart and the column of an exported figure.
inline const QColor kStripRule(201, 211, 219);
/// The id of a critical or conflict section in the strip, and the outline of a selected blocking time.
inline const QColor kStripStrongText(36, 49, 58);

/// One chart-owned item, outside the train series/filter identity space. Geometry is mapped from the live distance axis,
/// which may be the position axis; font sizes and the strip stay in chart pixels. It draws the strip of the sections above
/// the plot and the break marks of the position axis on the bottom edge of the plot over the series, and the boundaries of
/// the sections in the plot under the series, in a layer of its own.
class BlockSectionOverlay : public QGraphicsObject {
	Q_OBJECT
	/// The ids that the strip draws as the chart is now, read by the tests.
	Q_PROPERTY(QStringList stripLabels READ stripLabels)
public:
	/// The overlay of `sections` on `chart`, which owns it. `marks` has one entry per section or none, `routeSectionCount`
	/// is the M of "Section N of M" (the number of sections when 0), `route` is the route that the hover names, and
	/// `labels` may be null.
	BlockSectionOverlay(QChart* chart, std::vector<BlockSectionExtent> sections, std::vector<BlockSectionMarks> marks,
		int routeSectionCount, const QString& route, const SceneSectionLabels* labels, const QFont& font, const QPalette& palette);
	/// The whole chart.
	QRectF boundingRect() const override { return m_bounds; }
	/// Follows the numeric axes that the chart has now, which replacing a time or position axis changes.
	void watchAxes();
	/// The range of the distance axis at the full view of the chart, which decides the rows of the strip.
	void setFullRange(double low, double high);
	/// The jumps of the position axis, each drawn as a break mark; none without a position axis.
	void setJumps(std::vector<PositionScale::Jump> jumps);
	/// Called when the strip takes another number of rows, which changes the height that it needs above the plot.
	void setRowsChanged(std::function<void()> changed) { m_rowsChanged = std::move(changed); }
	/// The height above the plot: the strip, then the notice.
	double headerHeight() const { return stripHeight() + noticeHeight(); }
	/// The rows of the Key for what the overlay draws: a cell with the bar of a station section, a hatched cell of a switch
	/// section and the dotted line of a boundary, each while the strip draws one.
	QVector<DiagramKeyEntry> keyEntries() const;
	/// The ids that the strip draws as the chart is now, in the order that it draws them.
	QStringList stripLabels() const;
	/// The hover at `point` in chart pixels, one fact per line: a break mark, the sections and the notice in the header
	/// and, without `headerOnly`, the boundaries in the plot. Empty where the overlay shows nothing.
	QString inspect(const QPointF& point, bool headerOnly) const;
	/// Draws the strip, the notice and the break marks.
	void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override;

private:
	struct Label {
		const BlockSectionExtent* section;
		QRectF rect;
		QString text;
		bool strong; // a critical or conflict section, drawn in bold
	};
	void refresh();
	bool drawsBoundaries() const;
	void paintBoundaries(QPainter* painter) const;
	QPen boundaryPen() const;
	static QString shortId(const BlockSectionExtent& section);
	QString sectionDetails(std::size_t index) const;
	QRectF headerRect() const;
	std::vector<Label> placedLabels(double low, double high, bool reverse, int rows, std::size_t* candidates = nullptr) const;
	std::vector<Label> renderedLabels() const;
	void chooseRows();
	QValueAxis* distanceAxis() const;
	double rowHeight() const;
	double stripHeight() const;
	double noticeHeight() const;
	double pixelX(double km, const QValueAxis* axis) const;

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
	QGraphicsObject* m_boundaries = nullptr; ///< A child of the overlay, under the series
};

#endif // BLOCKSECTIONOVERLAY_H
