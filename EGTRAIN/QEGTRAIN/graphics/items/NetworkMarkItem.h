#ifndef NETWORKMARKITEM_H
#define NETWORKMARKITEM_H

#include <QColor>
#include <QGraphicsItem>
#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>

/// The colour of the mark of the row that the Infrastructure table selects, a violet that no track, signal, station,
/// default train or selection cue uses.
inline const QColor kNetworkMarkColor(196, 120, 255);
/// The second colour of the mark, for the part of the marked item that is chosen on its own, such as the section chosen
/// under Route sections.
inline const QColor kNetworkMarkEmphasisColor(255, 220, 140);

/// The mark of an item of the network on the canvas, a look of its own beside the selection cue of a click
/// (SelectionCueItem), so that both can show at once. It draws a band in kNetworkMarkColor along each stretch of track it
/// is given, a wider band in kNetworkMarkEmphasisColor along each stretch of emphasis, a disc at the start of the first
/// stretch and an arrowhead at the end of the last one when it is directed, a ring around each ring target and a halo
/// around the outline of each halo target. Each part has a dark casing. The sizes are logical pixels on screen, so the
/// mark reads the same at every zoom: the stretches are drawn with cosmetic pens, and the rings, the disc and the
/// arrowhead are sized by the view scale. It is stacked at ZValue, under the tracks, the nodes, the platform bars and the
/// signals, so that it never covers them, takes no mouse input and has an empty shape.
/// The mark reads its targets in sync() and paints only those that are shown. It must not outlive them: the presenter
/// that places it deletes it before the targets, which are deleted only with the whole scene.
class NetworkMarkItem : public QGraphicsItem {
public:
	/// The z value of the mark: over the area of a station (z -1), under the tracks and the connections (z 0).
	static constexpr qreal ZValue = -0.5;
	/// The width in logical pixels of the band along a stretch.
	static constexpr qreal BandPixels = 8.0;
	/// The width in logical pixels of the band along a stretch of emphasis.
	static constexpr qreal EmphasisPixels = 12.0;
	/// The width in logical pixels of the dark casing on each side of a band, a ring and a halo.
	static constexpr qreal CasingPixels = 1.0;
	/// The width in logical pixels of the line of a ring.
	static constexpr qreal RingPixels = 3.0;
	/// The smallest radius in logical pixels of a ring.
	static constexpr qreal RingRadiusPixels = 9.0;
	/// The gap in logical pixels between the corners of a ring target and its ring.
	static constexpr qreal RingPaddingPixels = 4.0;
	/// The distance in logical pixels from the outline of a halo target to the edge of its halo.
	static constexpr qreal HaloPixels = 4.0;
	/// The radius in logical pixels of the disc at the start of a directed mark.
	static constexpr qreal StartPixels = 6.0;
	/// The length in logical pixels of the arrowhead at the end of a directed mark; it is as wide as it is long.
	static constexpr qreal ArrowPixels = 16.0;

	/// An empty mark at scale 1.
	NetworkMarkItem();

	enum { Type = UserType + 16 /**< The item type for qgraphicsitem_cast. */ };
	/// Returns Type.
	int type() const override { return Type; }

	/// Sets the stretches, polylines in scene coordinates. A polyline of one point is drawn as a dot.
	void setStretches(const QList<QPolygonF>& stretches);
	/// The stretches.
	const QList<QPolygonF>& stretches() const { return m_stretches; }
	/// Sets the stretches of emphasis, drawn over the stretches.
	void setEmphasis(const QList<QPolygonF>& emphasis);
	/// The stretches of emphasis.
	const QList<QPolygonF>& emphasis() const { return m_emphasis; }
	/// Shows the direction of the stretches: they are in travel order, each from its start to its end.
	void setDirected(bool directed);
	/// The direction of the stretches shows.
	bool directed() const { return m_directed; }
	/// Sets the items that get a ring around their bounds in the scene.
	void setRingTargets(const QList<const QGraphicsItem*>& targets);
	/// The items that get a ring.
	const QList<const QGraphicsItem*>& ringTargets() const { return m_ringTargets; }
	/// Sets the items that get a halo around their outline: the rectangle of a rectangle item, else the bounds.
	void setHaloTargets(const QList<const QGraphicsItem*>& targets);
	/// The items that get a halo.
	const QList<const QGraphicsItem*>& haloTargets() const { return m_haloTargets; }
	/// The mark has no stretch and no target.
	bool isEmpty() const;

	/// Logical pixels per scene unit of the view. A value that is not positive keeps the old scale.
	void setViewScale(qreal scale);
	/// Logical pixels per scene unit of the view.
	qreal viewScale() const { return m_scale; }
	/// Reads the targets again: which of them are shown and where.
	void sync();

	/// The rings that are drawn, the squares around their circles in scene coordinates, one per ring target that is shown.
	const QList<QRectF>& rings() const { return m_rings; }
	/// The outlines of the halo targets that are shown, in scene coordinates.
	const QList<QPolygonF>& halos() const { return m_halos; }
	/// Part of the mark lies in the rectangle: a point of a stretch, a segment between two of them, a ring or a halo.
	bool intersects(const QRectF& rect) const;
	/// The point of the mark nearest to `point`: on a stretch, or the centre of a ring or a halo. False for an empty mark.
	bool nearestPoint(const QPointF& point, QPointF* nearest) const;

	/// Everything the mark draws, with its casing and a margin of one logical pixel.
	QRectF boundingRect() const override;
	/// An empty shape, so the mark is never picked.
	QPainterPath shape() const override;
	/// Draws the casings and then the bands, the direction, the halos and the rings.
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;

private:
	void rebuild();

	QList<QPolygonF> m_stretches;
	QList<QPolygonF> m_emphasis;
	bool m_directed = false;
	QList<const QGraphicsItem*> m_ringTargets;
	QList<const QGraphicsItem*> m_haloTargets;
	qreal m_scale = 1.0;
	QList<QRectF> m_rings;
	QList<QPolygonF> m_halos;
	// The disc at the start and the arrowhead at the end of a directed mark.
	QPainterPath m_direction;
	QRectF m_bounds;
};

#endif // NETWORKMARKITEM_H
