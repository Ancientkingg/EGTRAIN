#ifndef BLOCKSECTIONOVERLAY_H
#define BLOCKSECTIONOVERLAY_H

#include "diagrams/BlockingTimeDiagram.h"
#include "diagrams/DiagramKey.h"
#include "diagrams/PositionScale.h"

#include <QColor>
#include <QFont>
#include <QGraphicsObject>
#include <QPalette>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QVector>

#include <cstddef>
#include <vector>

QT_BEGIN_NAMESPACE
class QChart;
class QValueAxis;
QT_END_NAMESPACE

/// The rule between the cells of the section strip, and between a chart and the column of an exported figure.
inline const QColor kStripRule(201, 211, 219);

/// One chart-owned item, outside the train series/filter identity space. Geometry is mapped from the live distance axis,
/// which may be the position axis; the height of the strip stays in chart pixels. It draws the strip of the sections above
/// the plot and the break marks of the position axis on the bottom edge of the plot over the series, and the boundaries
/// of the sections in the plot under the series, in a layer of its own. The strip is one row of cells without text, one
/// cell for each section with a usable extent.
class BlockSectionOverlay : public QGraphicsObject {
	Q_OBJECT
public:
	/// The overlay of `sections` on `chart`, which owns it. `marks` has one entry per section or none. The strip is one
	/// line of `font` high, with a little room.
	BlockSectionOverlay(QChart* chart, std::vector<BlockSectionExtent> sections, std::vector<BlockSectionMarks> marks,
		const QFont& font, const QPalette& palette);
	/// The whole chart.
	QRectF boundingRect() const override { return m_bounds; }
	/// Follows the numeric axes that the chart has now, which replacing a time or position axis changes.
	void watchAxes();
	/// The jumps of the position axis, each drawn as a break mark; none without a position axis.
	void setJumps(std::vector<PositionScale::Jump> jumps);
	/// The height above the plot that the strip takes; 0 without sections.
	double headerHeight() const { return m_headerHeight; }
	/// The rows of the Key for what the overlay draws: a cell with the bar of a station section and a hatched cell of a
	/// switch section, each while the strip draws one.
	QVector<DiagramKeyEntry> keyEntries() const;
	/// The hover at `point` in chart pixels: a break mark, the section of the cell of the strip under the pointer, or the
	/// nearest within 3 px, and without `headerOnly` the sections of the boundaries in the plot. A section is two lines:
	/// its short id (shortBlockId) and its geo extent, such as "Position 21.010 to 21.450 km". Coincident sections are
	/// all listed. Empty where the overlay shows nothing.
	QString inspect(const QPointF& point, bool headerOnly) const;
	/// Draws the strip and the break marks.
	void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override;

private:
	void refresh();
	void paintBoundaries(QPainter* painter) const;
	QPen boundaryPen() const;
	QString sectionDetails(std::size_t index) const;
	QRectF headerRect() const;
	QValueAxis* distanceAxis() const;
	double pixelX(double km, const QValueAxis* axis) const;

	QChart* m_chart;
	std::vector<BlockSectionExtent> m_sections;
	std::vector<BlockSectionMarks> m_marks; ///< One per section
	double m_headerHeight = 0;
	QPalette m_palette;
	QRectF m_bounds;
	QSet<QValueAxis*> m_axes;
	std::vector<PositionScale::Jump> m_jumps;
	QGraphicsObject* m_boundaries = nullptr; ///< A child of the overlay, under the series
};

#endif // BLOCKSECTIONOVERLAY_H
