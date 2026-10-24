#ifndef STATIONOVERLAYITEM_H
#define STATIONOVERLAYITEM_H

#include <QFont>
#include <QGraphicsItem>
#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsTextItem>
#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QTransform>

#include <functional>

#include "graphics/VisualPolish.h"

/// The scene-space mark of a station. In the far look the transparent pixmap only gives the size and anchor
/// that StationOverlayItem enlarges, and paint draws the pin of the station class over it. In the near look
/// the mark draws nothing and the station squares show.
class StationMarkItem : public QGraphicsPixmapItem {
public:
	/// The z value of the station square that holds the mark: over the other squares (z 1), which the pin can
	/// cover, and under platform bars and signals (z 2), trains and names.
	static constexpr qreal HostZValue = 1.05;

	/// A mark of class cls in a transparent pixmap of size by size pixels, one scene unit per pixel before the
	/// scale of the item.
	StationMarkItem(StationClass cls, int size, QGraphicsItem* parent = nullptr);

	/// The pixmap and the pin that reaches past it in the far look; empty in the near look.
	QRectF boundingRect() const override;
	/// The pin with its edge, the click target, in the far look (stationMarkShape()); empty in the near look.
	QPainterPath shape() const override;
	/// Paints the pin of the class over the pixmap with a halo of 2 logical pixels; nothing in the near look.
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;
	/// The class of the station.
	StationClass stationClass() const { return m_class; }
	/// The centres of the station squares at the place of the station in scene coordinates, in the order of its
	/// nodes, or none; StationOverlayItem::setNodeCentres() sets them.
	QList<QPointF> nodeCentres() const { return m_nodeCentres; }
	/// Sets the centres of the station squares.
	void setNodeCentres(const QList<QPointF>& centres) { m_nodeCentres = centres; }
	/// Draws nothing in the near look, and the pin of the class over the pixmap otherwise.
	void setNearLook(bool nearLook);
	/// The mark draws nothing.
	bool isNearLook() const { return m_nearLook; }
	/// The pixmap and the pin that reaches past it, in item coordinates: boundingRect() in the far look.
	QRectF farBoundingRect() const { return m_farBounds; }

private:
	StationClass m_class;
	QList<QPointF> m_nodeCentres;
	bool m_nearLook = false;
	QRectF m_farBounds;
	QPainterPath m_farShape;
};

/// The name of a station on the network canvas: white text in a halo of HaloPixels logical pixels in canvasColor(), a
/// child item stacked behind it, so that a track, a pin or a train under the name does not cross its letters. It is
/// stacked over everything else on the canvas except the boxes that a click opens, and it takes no click. The text
/// and the font stay as constructed.
class StationNameItem : public QGraphicsTextItem {
public:
	/// Over the trains (z 4), their speed (TrainBadgeItem) and the notices of a virtual coupling (z 4.4), and under the
	/// callouts of a train and the details of a passenger (z 5).
	static constexpr qreal NameZValue = 4.5;
	/// The width of the halo around the letters on screen, in logical pixels.
	static constexpr qreal HaloPixels = 2.0;

	/// The name text in font, white, at NameZValue, with its halo.
	StationNameItem(const QString& text, const QFont& font, QGraphicsItem* parent = nullptr);
	/// The halo, a child item behind the name, drawn on every paint without a cached picture. While Qt draws the letters
	/// from its glyph cache, it is the letters drawn sixteen times around their place, in eight directions at half and
	/// at all of HaloPixels on screen; for larger letters it is a stroke along their outlines that reaches as far past
	/// them. It is at most as wide as at the smallest size of the name on screen, StationOverlayItem::MinNamePixels.
	QGraphicsItem* haloItem() const { return m_halo; }

private:
	QGraphicsItem* m_halo = nullptr;
};

/// The overlay of a station at its anchor in the scene. It ignores the transformation of the view, so its own
/// geometry is in logical pixels around the anchor. On the network canvas it is a scene decoration: it draws
/// nothing itself and keeps the scene-space picture and name of the station readable (attachArtwork()).
/// Otherwise it draws its own mark and label, which can be placed in the viewport.
class StationOverlayItem : public QGraphicsItem {
public:
	/// The side of the mark on which the label sits.
	enum class LabelSide { Right, ///< To the right of the mark.
		Left,					  ///< To the left of the mark.
		Above,					  ///< Above the mark.
		Below /**< Below the mark. */ };
	enum { Type = UserType + 13 /**< The item type for qgraphicsitem_cast. */ };
	/// Where the mark and the label go for one label side, in logical pixels of the viewport.
	struct ViewportPlacement {
		LabelSide side = LabelSide::Right; ///< The side of the label.
		QPointF offset;					   ///< The move that keeps the mark and the label in the inset; zero when the anchor is too far out.
		QRectF symbolRect;				   ///< The mark after the move.
		QRectF labelRect;				   ///< The label after the move.
		QRectF combinedRect;			   ///< The mark and the label after the move.
		qreal overflow = 0.0;			   ///< How far the mark and the label reach outside the inset before the move, summed over the four sides.
		bool fitsBeforeClamp = false;	   ///< The mark and the label fit in the inset without the move.
		bool fits = false;				   ///< The mark and the label fit in the inset after the move.
	};
	/// A node of the run that the station stands for.
	struct SourceIdentity {
		double nodeId = 0.0; ///< The ID of the node.
		int track = -1;		 ///< The index of the track line of the node; -1 without one.
	};

	/// An overlay at stableAnchor in scene coordinates with a mark of 16 logical pixels and the name to its right
	/// in a font of 11 pixels. degree is the number of connections at the station.
	StationOverlayItem(const QString& stationName, const QPointF& stableAnchor,
		const StationVisual& visual, int degree = 0, QGraphicsItem* parent = nullptr);
	~StationOverlayItem() override;

	/// Returns Type.
	int type() const override { return Type; }
	/// The mark and the label after their moves, in logical pixels.
	QRectF boundingRect() const override;
	/// The visible mark and label; empty for a scene decoration.
	QPainterPath shape() const override;
	/// Draws the mark and the label; a scene decoration draws nothing.
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;

	/// The name of the station as given.
	QString stationName() const { return m_stationName; }
	/// The name as it is shown, with spaces between its words.
	QString displayName() const;
	/// A station name with a space before each capital that follows a lower-case letter, and before the last
	/// capital of a run of two or more that a lower-case letter follows.
	static QString displayName(const QString& stationName);
	/// The same for a std::string.
	static QString displayName(const std::string& stationName);
	/// The factor of the label font, from 1 to 3.
	qreal labelScale() const;
	/// Sets the factor of the label font, kept from 1 to 3. The font is 11 pixels times both scales.
	void setLabelScale(qreal scale);
	/// The factor of the mark, the label and the gap between them, from 0.1 to 1.
	qreal visualScale() const { return m_visualScale; }
	/// Sets the factor of the mark, the label and the gap, kept from 0.1 to 1.
	void setVisualScale(qreal scale);

	/// The position of the station in scene coordinates.
	QPointF stableAnchor() const { return m_stableAnchor; }
	/// The move of the mark and the label in logical pixels.
	QPointF viewportOffset() const { return m_viewportOffset; }
	/// Sets the move of the mark and the label in logical pixels.
	void setViewportOffset(const QPointF& offset);
	/// The further move of the mark alone in logical pixels, which keeps it clear of other marks at Fit.
	QPointF fitCollisionOffset() const { return m_fitCollisionOffset; }
	/// Sets that move. While it is not zero, a click on the moved mark calls the displaced click handler.
	void setFitCollisionOffset(const QPointF& offset);
	/// The mark is drawn.
	bool isFitSymbolVisible() const { return m_fitSymbolVisible; }
	/// Shows or hides the mark.
	void setFitSymbolVisible(bool visible);
	/// Sets the function that a click on the moved mark calls with the station name; empty for none.
	void setDisplacedClickHandler(std::function<void(const QString&)> handler);
	/// Sets the nodes of the run that the station stands for.
	void setSourceIdentities(const QList<SourceIdentity>& identities);
	/// Forgets the nodes of the run.
	void clearSourceIdentities();
	/// The station stands for a node of the run.
	bool hasSourceIdentity() const { return !m_sourceIdentities.isEmpty(); }
	/// The number of nodes of the run that the station stands for.
	int sourceIdentityCount() const { return static_cast<int>(m_sourceIdentities.size()); }
	/// The station stands for the node with this ID on this track line.
	bool matchesSourceIdentity(double nodeId, int track) const;
	/// The ID of the first node; 0 without one.
	double sourceNodeId() const;
	/// The track line of the first node; -1 without one.
	int sourceTrack() const;
	/// The first move of symbolRect, from none to 40 logical pixels along the axes and the diagonals, that keeps
	/// it in viewportInset and clear of occupiedSymbols and blockedRects. found tells whether there is one;
	/// without one the move is zero.
	static QPointF firstFitCollisionOffset(const QRectF& symbolRect, const QRectF& viewportInset,
		const QList<QRectF>& occupiedSymbols, const QList<QRectF>& blockedRects, bool* found);

	/// The side of the mark on which the label sits.
	LabelSide labelSide() const { return m_labelSide; }
	/// Puts the label on this side of the mark.
	void setLabelSide(LabelSide side);
	/// The placement for a label side with the anchor at deviceAnchor in the viewport, inside viewportInset.
	ViewportPlacement placementForSide(LabelSide side, const QPointF& deviceAnchor,
		const QRectF& viewportInset) const;
	/// The placement on the right when it fits before the move, else on the left when that fits, else on the
	/// side that overflows less, the right on a tie.
	ViewportPlacement preferredViewportPlacement(const QPointF& deviceAnchor,
		const QRectF& viewportInset) const;
	/// The mark around the anchor, in logical pixels, before its moves.
	QRectF symbolRect() const { return m_symbolRect; }
	/// The label, in logical pixels, before its move.
	QRectF labelRect() const { return m_labelRect; }
	/// The label and the visible mark after their moves.
	QRectF combinedRect() const;
	/// The mark after its moves.
	QRectF deviceSymbolRect() const { return translatedSymbol(m_symbolRect); }
	/// The label after its move.
	QRectF deviceLabelRect() const { return translated(m_labelRect); }
	/// The same as combinedRect().
	QRectF deviceCombinedRect() const { return combinedRect(); }

	/// The layout shows the label. Hovering and selecting show it anyway.
	void setLayoutVisible(bool visible);
	/// The name may show at all.
	void setNameVisible(bool visible);
	/// A scene decoration draws nothing and takes no click; otherwise the overlay takes left clicks.
	void setSceneDecoration(bool sceneDecoration) {
		m_sceneDecoration = sceneDecoration;
		setAcceptedMouseButtons(sceneDecoration ? Qt::NoButton : Qt::LeftButton);
		update();
	}
	/// Hides the label because it collides with another, unless it is hovered or selected.
	void setCollisionBlocked(bool blocked);
	/// The layout shows the label.
	bool isLayoutVisible() const { return m_layoutVisible; }
	/// The label is hidden because it collides.
	bool isCollisionBlocked() const { return m_collisionBlocked; }
	/// The same as isLayoutVisible().
	bool isLabelLayoutVisible() const { return m_layoutVisible; }
	/// The label is drawn.
	bool isLabelVisible() const;
	/// The pointer is over the overlay.
	bool isHovered() const { return m_hovered; }
	/// Marks the station as followed; the window marks the station nearest to the followed train.
	void setFollowed(bool followed);
	/// The station is marked as followed.
	bool isFollowed() const { return m_followed; }
	/// The number of connections at the station.
	int degree() const { return m_degree; }
	/// Sets the number of connections; three or more make an interchange and one an endpoint.
	void setDegree(int degree);
	/// Sets the number of connections, and marks an interchange or an endpoint also when the degree does not.
	void setNetworkDegree(int degree, bool interchange, bool endpoint);
	/// The colours and the class of the station.
	const StationVisual& visual() const { return m_visual; }
	/// The station is an interchange.
	bool isInterchange() const;
	/// The station is an endpoint.
	bool isEndpoint() const;

	/// Whether left ranks before right when names collide: selected, followed, more platforms, interchange,
	/// endpoint, nearer to viewportCenter in scene coordinates, then by name and anchor.
	static bool priorityLess(const StationOverlayItem& left, const StationOverlayItem& right,
		const QPointF& viewportCenter);

	/// Smallest height on screen, in logical pixels, of the scene-space pictogram.
	static constexpr qreal MinPictogramPixels = 24.0;
	/// Smallest font size on screen, in logical pixels, of the scene-space name.
	static constexpr qreal MinNamePixels = 12.0;
	/// Smallest spacing on screen, in logical pixels, of the tracks at the stations for the near look, in which
	/// the marks draw nothing and the platform bars show.
	static constexpr qreal NearTrackSpacingPixels = 12.0;
	/// Width on screen, in logical pixels, by which the area of a station reaches past its node centres on every side,
	/// and in the near look past its platform bars.
	static constexpr qreal AreaMarginPixels = 6.0;
	/// The z value of the area of a station: under the tracks and connections (z 0).
	static constexpr qreal AreaZValue = -1.0;

	/// Scale of an item of nativeSizePx that is at least minPx logical pixels on screen at viewScale,
	/// in logical pixels per scene unit, and keeps presentationScale as soon as that is larger.
	static qreal readableItemScale(qreal nativeSizePx, qreal presentationScale,
		qreal viewScale, qreal minPx);
	/// Links the scene-space pictogram and name of this station. The items stay owned
	/// by the scene; both are enlarged about their anchor (bottom centre of the pictogram,
	/// top centre of the name) through their item transform, which stays the identity at
	/// their scene size. For a station mark the transform also moves these anchors to the
	/// station nodes (applyViewScale()). Attach the items to their parents before the first
	/// applyViewScale: the transform moves scenePos() of an enlarged item. More platforms
	/// rank a station higher when names collide.
	/// For a StationMarkItem that is in a scene it also adds the area of the station to the scene, a path item at
	/// AreaZValue in stationAreaColor() that takes no mouse input.
	void attachArtwork(QGraphicsPixmapItem* picture, QGraphicsTextItem* name, int platformCount);
	/// The attached pictogram, or null.
	QGraphicsPixmapItem* pictureItem() const { return m_picture; }
	/// The attached name, or null.
	QGraphicsTextItem* nameItem() const { return m_name; }
	/// The area of the station, or null for a plain picture.
	QGraphicsPathItem* areaItem() const { return m_area; }
	/// The number of platforms of the station.
	int platformCount() const { return m_platformCount; }
	/// Gives the attached station mark the centres in scene coordinates of the station squares that are at the place
	/// of the anchor, those at most 32 mark frames at scene size from it, or none without one. A station with platform
	/// nodes on another line has its mark, area and name at the place of its anchor only.
	void setNodeCentres(const QList<QPointF>& centres);
	/// Gives the area of the attached station mark the platform bars of the station, each in scene coordinates, of
	/// which it keeps those whose centre is at the place of the anchor, as setNodeCentres() does.
	void setPlatformBars(const QList<QPolygonF>& bars);
	/// Scales the attached items for the view scale (logical pixels per scene unit). A station mark stands with the
	/// bottom centre of its frame on the top edge of the convex hull of its node centres, or on its anchor without them,
	/// at the x of the centre of the footprint of its squares (stationFootprintRect()): a station or major station on the
	/// top centre of their box for a single centre, a row or a stack, a halt on its first square, and a diagonal station
	/// on its own area. It stands there in both looks and draws nothing in the near look, which a mark without node
	/// centres does not take: it keeps its pin as the only thing to see and click, and there the pin and the name keep
	/// their smallest size on screen, MinPictogramPixels and MinNamePixels, at every zoom. Its name sits under the
	/// footprint in both looks, so that mark and name share their x. The area of the station is shown while the mark is
	/// visible: in the far look the hull of its node centres widened by AreaMarginPixels on screen, and in the near look
	/// the hull of its node centres and platform bars widened by as much. A plain picture keeps the far look and, like its
	/// name, its artwork point.
	void applyViewScale(qreal viewScale, bool nearLook = false);
	/// resolveNameCollisions() hid the attached name.
	bool isNameHiddenByCollision() const { return m_nameHiddenByCollision; }
	/// The area in scene coordinates of the pin of a station mark at the applied view scale, its halo included, and of
	/// the footprint of its squares, the same in either look; the scene bounds of a plain picture.
	QRectF pictogramSceneRect() const;
	/// Places the names of station marks and hides the attached names that overlap, on screen, the name of a
	/// higher-priority station or that cover part of the drawn pictogram of any other station: a visible plain picture,
	/// the pictogram rect of a visible mark that draws its pin, and the footprint of the squares of a visible mark in the
	/// near look. A name goes under the footprint of the squares of its station, or, where its text there crosses a
	/// visible track line or the pictogram of another station, shown or hidden, to the first of these spots that crosses
	/// neither: above the pin, right of the footprint, left of it, and above a frame on the top of the box of the node
	/// centres at the x of the footprint; without one it stays under the footprint. A mark that draws nothing in the near
	/// look has no pin and no frame there, so its name goes above the footprint instead of above the pin and has no last
	/// spot. A name is placed again only after applyViewScale() moved it. Selected and followed stations keep their name.
	/// Pictograms stay visible. sceneToViewport is the viewport transform of the view.
	static void resolveNameCollisions(const QList<StationOverlayItem*>& stations,
		const QTransform& sceneToViewport);

protected:
	/// Shows the label while the pointer is over the overlay.
	void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
	/// Ends the hover.
	void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
	/// A left click on the moved mark calls the displaced click handler; any other press passes on.
	void mousePressEvent(QGraphicsSceneMouseEvent* event) override;

private:
	void rebuildGeometry();
	void placeArtwork(qreal viewScale, bool nearLook, bool keptPin);
	void chooseNameSpot(const QList<QPair<const StationOverlayItem*, QRectF>>& pictograms,
		const QList<QPair<const QGraphicsItem*, QRectF>>& tracks);
	QRectF labelRectForSide(LabelSide side) const;
	QRectF translated(const QRectF& rect) const;
	QRectF translatedSymbol(const QRectF& rect) const;

	QString m_stationName;
	QString m_displayName;
	QPointF m_stableAnchor;
	QPointF m_viewportOffset;
	QPointF m_fitCollisionOffset;
	StationVisual m_visual;
	QFont m_labelFont;
	qreal m_labelScale = 1.0;
	qreal m_visualScale = 1.0;
	QRectF m_symbolRect;
	QRectF m_labelRect;
	LabelSide m_labelSide = LabelSide::Right;
	bool m_layoutVisible = true;
	bool m_nameVisible = true;
	bool m_sceneDecoration = false;
	bool m_collisionBlocked = false;
	bool m_fitSymbolVisible = true;
	bool m_hovered = false;
	bool m_followed = false;
	int m_degree = 0;
	bool m_interchange = false;
	bool m_endpoint = false;
	QList<SourceIdentity> m_sourceIdentities;
	QGraphicsPixmapItem* m_picture = nullptr;
	QGraphicsTextItem* m_name = nullptr;
	QGraphicsPathItem* m_area = nullptr;
	qreal m_pictureSceneScale = 1.0;
	qreal m_nameSceneScale = 1.0;
	qreal m_pictureNativePixels = 0.0;
	qreal m_nameNativePixels = 0.0;
	QPointF m_pictureAnchor;
	QPointF m_nameAnchor;
	qreal m_appliedViewScale = 0.0;
	bool m_appliedNearLook = false;
	bool m_appliedKeptPin = false;
	// The pin of the far look and the footprint of the squares in scene coordinates, in either look; null for a plain
	// picture. The footprint alone, its halo included; null without node centres.
	QRectF m_markSceneRect;
	QRectF m_squaresSceneRect;
	// The corners in scene coordinates of the platform bars at the place of the station, which the area covers in the
	// near look.
	QList<QPointF> m_barCorners;
	// The places in scene coordinates for the top centre of the name, the first under the footprint, the size of its
	// text without the document margin in scene units, and its enlargement about m_nameAnchor.
	QList<QPointF> m_nameSpots;
	QRectF m_nameTextRect;
	qreal m_nameFactor = 1.0;
	bool m_nameSpotChosen = true;
	int m_platformCount = 0;
	bool m_nameHiddenByCollision = false;
	std::function<void(const QString&)> m_displacedClickHandler;
};

#endif // STATIONOVERLAYITEM_H
