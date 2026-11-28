#ifndef STATIONCALLOUTITEM_H
#define STATIONCALLOUTITEM_H

#include <QColor>
#include <QGraphicsItem>
#include <QList>
#include <QRectF>
#include <QString>
#include <functional>
#include <tuple>

/// What the card of a station shows, as text and counts that StationCalloutItem lays out.
struct StationCalloutContent {
	/// One destination of the waiting passengers and how many of them go there.
	struct Destination {
		QString name;  ///< The station, as it is shown.
		int count = 0; ///< The passengers waiting for it.
		/// True when both name the same station and count.
		bool operator==(const Destination& other) const { return std::tie(name, count) == std::tie(other.name, other.count); }
	};
	/// One call of a train, as the cells of a table row.
	struct Call {
		QString time;		  ///< The arrival, hh:mm:ss.
		QString train;		  ///< The operating code of the train.
		QString boarded;	  ///< Passengers who boarded, or a dash.
		QString alighted;	  ///< Passengers who alighted, or a dash.
		QString leftBehind;	  ///< Passengers left behind, or a dash.
		QString load;		  ///< The load at departure, or a dash.
		bool anyLeft = false; ///< Some passengers were left behind, which the card draws in red.
		/// True when every cell and the flag are the same.
		bool operator==(const Call& other) const {
			return std::tie(time, train, boarded, alighted, leftBehind, load, anyLeft)
				== std::tie(other.time, other.train, other.boarded, other.alighted, other.leftBehind, other.load, other.anyLeft);
		}
	};
	QString title;					 ///< The name of the station.
	QString subtitle;				 ///< The passengers waiting and the capacity of the platforms.
	QString held;					 ///< Passengers held at the station entrance, empty when none are held.
	bool nobodyWaiting = false;		 ///< No passenger waits: the card says so instead of listing destinations.
	bool destinationHeader = false;	 ///< The card has the section of destinations, under the heading "Waiting, by destination".
	QList<Destination> destinations; ///< The destinations of the waiting passengers, the largest first.
	QList<Call> calls;				 ///< The latest calls, newest first.
	/// True when every field is the same. A field added above has to be added here too.
	bool operator==(const StationCalloutContent& other) const {
		return std::tie(title, subtitle, held, nobodyWaiting, destinationHeader, destinations, calls)
			== std::tie(other.title, other.subtitle, other.held, other.nobodyWaiting, other.destinationHeader, other.destinations, other.calls);
	}
};

/// The card that a click on a station opens on the canvas: a dark box with the passengers waiting at the station by
/// destination and its latest calls, joined to the station by a short pointer. It keeps its size on the screen whatever
/// the zoom: not through ItemIgnoresTransformations, which would leave the scene index with the card at its size in scene
/// units and so unfound over most of what it covers, but through setPixelScale(), which makes its scene extent the real one. The origin of the item is the point of the pointer; the card stands above it, or below it when setBelow()
/// says so. The × at the top right closes the card through onClose().
class StationCalloutItem : public QGraphicsItem {
public:
	enum { Type = UserType + 14 /**< The item type for qgraphicsitem_cast. */ };
	/// An empty card, stacked at z 5 like the callouts of the trains and the passengers.
	explicit StationCalloutItem(QGraphicsItem* parent = nullptr);
	/// Returns Type.
	int type() const override { return Type; }
	/// The card with its pointer, in logical pixels around the origin.
	QRectF boundingRect() const override;
	/// The card without the pointer, which is what a click lands on.
	QPainterPath shape() const override;
	/// Draws the card.
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;

	/// Shows the content, laying the card out again when it differs from the last one.
	void setContent(const StationCalloutContent& content);
	/// The content shown.
	const StationCalloutContent& content() const { return m_content; }
	/// Sets the scale of the view, so that one unit of the item is one pixel on the screen. The scene then holds the card
	/// at the area it covers on the screen. A scale of 0 or less is taken as 1.
	void setPixelScale(qreal scale);
	/// The scale of the view that the card was last given.
	qreal pixelScale() const { return m_pixelScale; }
	/// Moves the card sideways by shift pixels, keeping the pointer at the origin, which then meets the card off its centre.
	/// The shift is limited so that the pointer stays under the card.
	void setShift(qreal shift);
	/// The sideways move of the card in pixels.
	qreal shift() const { return m_shift; }
	/// Puts the card below the origin instead of above it.
	void setBelow(bool below);
	/// Whether the card stands below the origin.
	bool below() const { return m_below; }
	/// Limits the card height in logical pixels. When the destination list would make the card taller, the destination
	/// section shrinks and scrolls while the header and the calls stay in place. A value of 0 or less removes the limit.
	void setMaxHeight(qreal maxHeight);
	/// The height limit in logical pixels, or 0 when unlimited.
	qreal maxHeight() const { return m_maxHeight; }
	/// The height in logical pixels that the card takes without a height limit.
	qreal fullHeight() const;
	/// Sets the vertical scroll position of the destination list in logical pixels, clamped to maxScrollOffset().
	void setScrollOffset(qreal offset);
	/// The vertical scroll position of the destination list in logical pixels.
	qreal scrollOffset() const { return m_scrollOffset; }
	/// The largest scroll offset of the destination list in logical pixels, or 0 when every destination fits.
	qreal maxScrollOffset() const { return m_maxScrollOffset; }
	/// True when the destination list is taller than its visible area and scrolls.
	bool canScroll() const { return m_maxScrollOffset > 0.0; }
	/// The card without the pointer, in item coordinates.
	QRectF cardRect() const;
	/// The visible area of the destination rows in item coordinates, or an empty rectangle when no list is shown.
	QRectF destinationsRect() const;
	/// The track of the scroll bar in item coordinates, or an empty rectangle when the list does not scroll.
	QRectF scrollBarRect() const;
	/// The thumb of the scroll bar in item coordinates, or an empty rectangle when the list does not scroll.
	QRectF scrollThumbRect() const;
	/// The area of the × in item coordinates, which closes the card.
	QRectF closeRect() const;
	/// The subtitle as the card draws it: whole when it fits in the width of the card, and else shortened with an ellipsis.
	QString shownSubtitle() const;
	/// The call that a click on the × makes.
	std::function<void()> onClose;

	/// The distance in pixels between the origin and the edge of the card.
	static constexpr qreal Gap = 8.0;
	/// The width of the card in pixels.
	static constexpr qreal CardWidth = 340.0;
	/// The z value of the card: over the names of the stations (z 4.5), like the callouts of a click.
	static constexpr qreal CardZValue = 5.0;
	/// The colour of the card.
	static QColor cardColour() { return QColor(0x18, 0x23, 0x2e); }
	/// The colour of the edge of the card.
	static QColor edgeColour() { return QColor(0x2e, 0x3d, 0x4c); }
	/// The red of a count of passengers left behind.
	static QColor leftBehindColour() { return QColor(0xe5, 0x53, 0x4b); }

protected:
	/// A press on the × calls onClose(); a press on the scroll bar starts dragging it; a press elsewhere on the card is
	/// taken so that it does not reach the canvas.
	void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
	/// Updates the scroll position while the scroll bar is dragged.
	void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
	/// Ends a scroll bar drag.
	void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
	/// Scrolls the destination list when the pointer is over it and the list overflows, leaving the wheel to the canvas
	/// elsewhere.
	void wheelEvent(QGraphicsSceneWheelEvent* event) override;

private:
	qreal fixedHeight() const;
	qreal fullDestinationsHeight() const;
	qreal layoutDestinationsHeight() const;
	QRectF scrollBarHitRect() const;
	void scrollToThumbTop(qreal thumbTop);
	void layout();
	StationCalloutContent m_content;
	bool m_below = false;
	qreal m_shift = 0.0;
	qreal m_pixelScale = 1.0;
	qreal m_maxHeight = 0.0;
	qreal m_height = 0.0;
	qreal m_destinationsHeight = 0.0;
	qreal m_scrollOffset = 0.0;
	qreal m_maxScrollOffset = 0.0;
	bool m_draggingScrollBar = false;
	qreal m_dragThumbOffset = 0.0;
};

#endif // STATIONCALLOUTITEM_H
