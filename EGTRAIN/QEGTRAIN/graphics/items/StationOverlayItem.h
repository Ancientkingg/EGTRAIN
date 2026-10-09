#ifndef STATIONOVERLAYITEM_H
#define STATIONOVERLAYITEM_H

#include <QFont>
#include <QGraphicsItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsTextItem>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QTransform>

#include <functional>

#include "graphics/VisualPolish.h"

class StationOverlayItem : public QGraphicsItem {
public:
	enum class LabelSide { Right,
		Left,
		Above,
		Below };
	enum { Type = UserType + 13 };
	struct ViewportPlacement {
		LabelSide side = LabelSide::Right;
		QPointF offset;
		QRectF symbolRect;
		QRectF labelRect;
		QRectF combinedRect;
		qreal overflow = 0.0;
		bool fitsBeforeClamp = false;
		bool fits = false;
	};
	struct SourceIdentity {
		double nodeId = 0.0;
		int track = -1;
	};

	StationOverlayItem(const QString& stationName, const QPointF& stableAnchor,
		const StationVisual& visual, int degree = 0, QGraphicsItem* parent = nullptr);
	~StationOverlayItem() override;

	int type() const override { return Type; }
	QRectF boundingRect() const override;
	QPainterPath shape() const override;
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;

	QString stationName() const { return m_stationName; }
	QString displayName() const;
	static QString displayName(const QString& stationName);
	static QString displayName(const std::string& stationName);
	qreal labelScale() const;
	void setLabelScale(qreal scale);
	qreal visualScale() const { return m_visualScale; }
	void setVisualScale(qreal scale);

	QPointF stableAnchor() const { return m_stableAnchor; }
	QPointF viewportOffset() const { return m_viewportOffset; }
	void setViewportOffset(const QPointF& offset);
	QPointF fitCollisionOffset() const { return m_fitCollisionOffset; }
	void setFitCollisionOffset(const QPointF& offset);
	bool isFitSymbolVisible() const { return m_fitSymbolVisible; }
	void setFitSymbolVisible(bool visible);
	void setDisplacedClickHandler(std::function<void(const QString&)> handler);
	void setSourceIdentities(const QList<SourceIdentity>& identities);
	void clearSourceIdentities();
	bool hasSourceIdentity() const { return !m_sourceIdentities.isEmpty(); }
	int sourceIdentityCount() const { return m_sourceIdentities.size(); }
	bool matchesSourceIdentity(double nodeId, int track) const;
	double sourceNodeId() const;
	int sourceTrack() const;
	static QPointF firstFitCollisionOffset(const QRectF& symbolRect, const QRectF& viewportInset,
		const QList<QRectF>& occupiedSymbols, const QList<QRectF>& blockedRects, bool* found);

	LabelSide labelSide() const { return m_labelSide; }
	void setLabelSide(LabelSide side);
	ViewportPlacement placementForSide(LabelSide side, const QPointF& deviceAnchor,
		const QRectF& viewportInset) const;
	ViewportPlacement preferredViewportPlacement(const QPointF& deviceAnchor,
		const QRectF& viewportInset) const;
	QRectF symbolRect() const { return m_symbolRect; }
	QRectF labelRect() const { return m_labelRect; }
	QRectF combinedRect() const;
	QRectF deviceSymbolRect() const { return translatedSymbol(m_symbolRect); }
	QRectF deviceLabelRect() const { return translated(m_labelRect); }
	QRectF deviceCombinedRect() const { return combinedRect(); }

	void setLayoutVisible(bool visible);
	void setNameVisible(bool visible);
	void setSceneDecoration(bool sceneDecoration) {
		m_sceneDecoration = sceneDecoration;
		setAcceptedMouseButtons(sceneDecoration ? Qt::NoButton : Qt::LeftButton);
		update();
	}
	void setCollisionBlocked(bool blocked);
	bool isLayoutVisible() const { return m_layoutVisible; }
	bool isCollisionBlocked() const { return m_collisionBlocked; }
	bool isLabelLayoutVisible() const { return m_layoutVisible; }
	bool isLabelVisible() const;
	bool isHovered() const { return m_hovered; }
	void setFollowed(bool followed);
	bool isFollowed() const { return m_followed; }
	int degree() const { return m_degree; }
	void setDegree(int degree);
	void setNetworkDegree(int degree, bool interchange, bool endpoint);
	const StationVisual& visual() const { return m_visual; }
	bool isInterchange() const;
	bool isEndpoint() const;

	static bool priorityLess(const StationOverlayItem& left, const StationOverlayItem& right,
		const QPointF& viewportCenter);

	// Smallest size on screen, in logical pixels, of the scene-space artwork.
	static constexpr qreal MinPictogramPixels = 24.0;
	static constexpr qreal MinNamePixels = 12.0;

	// Scale of an item of nativeSizePx that is at least minPx on screen at viewScale
	// and keeps presentationScale as soon as that is larger.
	static qreal readableItemScale(qreal nativeSizePx, qreal presentationScale,
		qreal viewScale, qreal minPx);
	// Links the scene-space pictogram and name of this station. The items stay owned
	// by the scene; both are enlarged about their anchor (bottom centre of the pictogram,
	// top centre of the name) through their item transform, which stays the identity at
	// their scene size. Attach the items to their parents before the first applyViewScale:
	// the transform moves scenePos() of an enlarged item. More platforms rank a station
	// higher when names collide.
	void attachArtwork(QGraphicsPixmapItem* picture, QGraphicsTextItem* name, int platformCount);
	QGraphicsPixmapItem* pictureItem() const { return m_picture; }
	QGraphicsTextItem* nameItem() const { return m_name; }
	int platformCount() const { return m_platformCount; }
	// Scales the attached items for the view scale (device pixels per scene unit).
	void applyViewScale(qreal viewScale);
	bool isNameHiddenByCollision() const { return m_nameHiddenByCollision; }
	// Hides the attached names that overlap, on screen, the name of a higher-priority
	// station or that cover part of the visible pictogram of any other station. Selected
	// and followed stations keep their name. Pictograms stay visible.
	static void resolveNameCollisions(const QList<StationOverlayItem*>& stations,
		const QTransform& sceneToViewport);

protected:
	void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
	void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
	void mousePressEvent(QGraphicsSceneMouseEvent* event) override;

private:
	void rebuildGeometry();
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
	qreal m_pictureSceneScale = 1.0;
	qreal m_nameSceneScale = 1.0;
	qreal m_pictureNativePixels = 0.0;
	qreal m_nameNativePixels = 0.0;
	QPointF m_pictureAnchor;
	QPointF m_nameAnchor;
	qreal m_appliedViewScale = 0.0;
	int m_platformCount = 0;
	bool m_nameHiddenByCollision = false;
	std::function<void(const QString&)> m_displacedClickHandler;
};

#endif // STATIONOVERLAYITEM_H
