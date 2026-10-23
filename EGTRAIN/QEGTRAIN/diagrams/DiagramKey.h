#ifndef DIAGRAMKEY_H
#define DIAGRAMKEY_H

#include <QBrush>
#include <QColor>
#include <QPen>
#include <QString>
#include <QVector>
#include <QWidget>

class QFont;
class QPainter;
class QPaintEvent;

/// One kind of mark in the key of a chart, such as "Blocking time" or "Planned", with the pen and brush that draw it.
struct DiagramKeyEntry {
	/// How the sample of the kind is drawn.
	enum class Shape {
		Line,	///< A short horizontal line in the pen, with a dot in its middle when `marker` is set
		Box,	///< A box filled with the brush and outlined with the pen
		Marker, ///< A dot filled with the brush and outlined with the pen
		Tick,	///< A short vertical line in the pen, such as a boundary or a dwell
		Cell	///< A cell of the section strip: `background` outlined a shade darker, then the brush, and a bar in the pen
				///< colour along its top when the pen draws
	};
	QString name;			   ///< In sentence case, such as "Station section"
	Shape shape = Shape::Line; ///< How the sample is drawn
	QPen pen;				   ///< The line, outline or bar
	QBrush brush;			   ///< The fill of a box, a dot or a cell
	QColor background{};	   ///< The fill of a cell under its brush
	bool marker = false;	   ///< A line sample with a dot in its middle, for a series that shows its points
};

/// The width of the sample of a key row or a train row, in pixels.
constexpr double kKeySampleWidth = 22;
/// The height of the sample of a key row or a train row, in pixels.
constexpr double kKeySampleHeight = 14;
/// The room between a sample and its name, in pixels.
constexpr double kKeySampleGap = 8;

/// The height of one row of a key in `font`: its text or its sample, whichever is taller, and room above and below.
double diagramKeyRowHeight(const QFont& font);
/// Draws the sample of `entry` centred in `area`.
void paintKeySample(QPainter& painter, const QRectF& area, const DiagramKeyEntry& entry);
/// Draws one row of a key from the top left of `area`: the sample and the name in `textColour`, elided at the right
/// edge of `area`. Returns the height of the row.
double paintKeyRow(QPainter& painter, const QRectF& area, const DiagramKeyEntry& entry, const QFont& font, const QColor& textColour);
/// The width of the widest row of `entries` in `font`, sample and name.
double diagramKeyWidth(const QVector<DiagramKeyEntry>& entries, const QFont& font);

/// The Key section of a diagram window: a row for each kind of mark in view, its sample and its name. The names are in
/// the dynamic property "kinds", for the tests, and in the accessible description.
class DiagramKey : public QWidget {
	Q_OBJECT
public:
	/// An empty key.
	explicit DiagramKey(QWidget* parent = nullptr);
	/// Shows `entries`, one per row, in the order given.
	void setEntries(QVector<DiagramKeyEntry> entries);
	/// The rows of the key.
	const QVector<DiagramKeyEntry>& entries() const { return m_entries; }
	/// As high as the rows, as wide as the widest row.
	QSize sizeHint() const override;
	/// As high as the rows, so that the panel does not cut a row, and no width, so that a long name is elided rather than
	/// widening the panel.
	QSize minimumSizeHint() const override;

protected:
	/// Draws the rows.
	void paintEvent(QPaintEvent* event) override;

private:
	QVector<DiagramKeyEntry> m_entries;
};

#endif // DIAGRAMKEY_H
