#ifndef STATIONOVERLAYITEM_H
#define STATIONOVERLAYITEM_H

#include <QFont>
#include <QGraphicsItem>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <functional>

#include "graphics/VisualPolish.h"

class StationOverlayItem : public QGraphicsItem {
public:
	enum class LabelSide { Right, Left, Above, Below };
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
	bool m_collisionBlocked = false;
	bool m_fitSymbolVisible = true;
	bool m_hovered = false;
	bool m_followed = false;
	int m_degree = 0;
	bool m_interchange = false;
	bool m_endpoint = false;
	QList<SourceIdentity> m_sourceIdentities;
	std::function<void(const QString&)> m_displacedClickHandler;
};

#endif // STATIONOVERLAYITEM_H
