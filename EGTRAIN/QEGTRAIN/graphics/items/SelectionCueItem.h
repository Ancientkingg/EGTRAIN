#ifndef SELECTIONCUEITEM_H
#define SELECTIONCUEITEM_H

#include <QColor>
#include <QGraphicsObject>
#include <QLineF>
#include <QPainterPath>
#include <QRectF>

// The one colour of the selection cue. No built-in track, signal or default train colour uses it; a service colour that the user chooses is not checked.
inline const QColor kSelectionCueColor(61, 214, 255);

// A ring around the selected item, drawn in screen pixels just below the item in the stacking order. The ring is a 2 pixel
// line in kSelectionCueColor with a 1 pixel dark casing on both sides. A line item gets a halo along its
// line, any other item a rounded ring of at least MinPixels around its bounds. The ring never covers
// the item, takes no mouse input and has an empty shape, so picking is not affected.
// The cue reads the target in sync() and must not outlive it: HighlightEffect owns it. The pictogram of a station
// node is read through a pointer that stays valid because the overlays are only replaced with the whole scene.
class SelectionCueItem : public QGraphicsObject {
	Q_OBJECT

public:
	static constexpr qreal MinPixels = 16.0;
	static constexpr qreal PaddingPixels = 3.0;
	static constexpr qreal RingPixels = 2.0;
	static constexpr qreal CasingPixels = 1.0;
	static constexpr qreal HaloRadiusPixels = 6.0;

	explicit SelectionCueItem(const QGraphicsItem* target);

	enum { Type = UserType + 14 };
	int type() const override { return Type; }

	// Device pixels per scene unit of the view. A value that is not positive keeps the old scale.
	void setViewScale(qreal scale);
	qreal viewScale() const { return m_scale; }
	// Follows the target: geometry, visibility and the scene it is in.
	void sync();
	// The scene rectangle the ring surrounds, or the line for a line item.
	QRectF targetRect() const { return m_rect; }
	// The ring itself in scene units, without the casing.
	QRectF ringRect() const { return m_ring.boundingRect(); }
	bool followsLine() const { return m_isLine; }
	QLineF targetLine() const { return m_line; }

	QRectF boundingRect() const override;
	QPainterPath shape() const override;
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
