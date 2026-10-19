#ifndef SELECTIONCUEITEM_H
#define SELECTIONCUEITEM_H

#include <QColor>
#include <QGraphicsObject>
#include <QLineF>
#include <QPainterPath>
#include <QRectF>

/// The one colour of the selection cue. No built-in track, signal or default train colour uses it; a service colour that the user chooses is not checked.
inline const QColor kSelectionCueColor(61, 214, 255);

/// A ring around the selected item, sized in pixels on screen and stacked just below the item. The ring is a 2 pixel
/// line in kSelectionCueColor with a 1 pixel dark casing on both sides. A line item gets a halo along its
/// line, any other item a rounded ring of at least MinPixels around its bounds. The ring never covers
/// the item, takes no mouse input and has an empty shape, so picking is not affected. Its path is in scene
/// units: the sizes in logical pixels are divided by the view scale.
/// The cue reads the target in sync() and must not outlive it: HighlightEffect owns it. The pictogram of a station
/// node is read through a pointer that stays valid because the overlays are only replaced with the whole scene.
class SelectionCueItem : public QGraphicsObject {
	Q_OBJECT

public:
	/// The smallest width and height of a ring in logical pixels.
	static constexpr qreal MinPixels = 16.0;
	/// The gap in logical pixels between the bounds of the item and the ring.
	static constexpr qreal PaddingPixels = 3.0;
	/// The width in pixels of the cosmetic pen of the ring.
	static constexpr qreal RingPixels = 2.0;
	/// The width in pixels of the dark casing on each side of the ring.
	static constexpr qreal CasingPixels = 1.0;
	/// The distance in logical pixels from a line to the halo around it.
	static constexpr qreal HaloRadiusPixels = 6.0;

	/// A cue for target, at the scale of the first view of its scene.
	explicit SelectionCueItem(const QGraphicsItem* target);

	enum { Type = UserType + 14 /**< The item type for qgraphicsitem_cast. */ };
	/// Returns Type.
	int type() const override { return Type; }

	/// Logical pixels per scene unit of the view. A value that is not positive keeps the old scale.
	void setViewScale(qreal scale);
	/// Logical pixels per scene unit of the view.
	qreal viewScale() const { return m_scale; }
	/// Follows the target: geometry, visibility and the scene it is in.
	void sync();
	/// The scene rectangle the ring surrounds; for a line item, see targetLine().
	QRectF targetRect() const { return m_rect; }
	/// The ring itself in scene units, without the casing.
	QRectF ringRect() const { return m_ring.boundingRect(); }
	/// The target is a line item, which gets a halo along its line.
	bool followsLine() const { return m_isLine; }
	/// The line of a line item in scene coordinates.
	QLineF targetLine() const { return m_line; }

	/// The ring with its casing and a margin of one logical pixel.
	QRectF boundingRect() const override;
	/// An empty shape, so the cue is never picked.
	QPainterPath shape() const override;
	/// Draws the casing and then the ring with cosmetic pens.
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;

private:
	void readTarget(bool* isLine, QLineF* line, QRectF* rect) const;
	void rebuild();
	void stackBehindTarget();

	const QGraphicsItem* m_target;
	// The pictogram of a station node, drawn apart from the node.
	const QGraphicsItem* m_pictogram = nullptr;
	bool m_isLine = false;
	QLineF m_line;
	QRectF m_rect;
	qreal m_scale = 1.0;
	QPainterPath m_ring;
	QRectF m_bounds;
};

#endif // SELECTIONCUEITEM_H
