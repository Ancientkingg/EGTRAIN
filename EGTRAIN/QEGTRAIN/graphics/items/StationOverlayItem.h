#ifndef STATIONOVERLAYITEM_H
#define STATIONOVERLAYITEM_H

#include <QFont>
#include <QGraphicsItem>
#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <QString>

#include "graphics/VisualPolish.h"

class StationOverlayItem : public QGraphicsItem {
public:
	enum class LabelSide { Right, Left };
	enum { Type = UserType + 13 };

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

	QPointF stableAnchor() const { return m_stableAnchor; }
	void setStableAnchor(const QPointF& anchor);
	QPointF viewportOffset() const { return m_viewportOffset; }
	void setViewportOffset(const QPointF& offset);

	LabelSide labelSide() const { return m_labelSide; }
	void setLabelSide(LabelSide side);
	QRectF symbolRect() const { return m_symbolRect; }
	QRectF labelRect() const { return m_labelRect; }
	QRectF combinedRect() const;
	QRectF deviceSymbolRect() const { return symbolRect(); }
	QRectF deviceLabelRect() const { return labelRect(); }
	QRectF deviceCombinedRect() const { return combinedRect(); }

	void setLayoutVisible(bool visible);
	bool isLayoutVisible() const { return m_layoutVisible; }
	bool isLabelLayoutVisible() const { return m_layoutVisible; }
	bool isLabelVisible() const;
	bool isHovered() const { return m_hovered; }
	void setFollowed(bool followed);
	bool isFollowed() const { return m_followed; }
	int degree() const { return m_degree; }
	void setDegree(int degree);
	StationVisualKind visualKind() const { return m_visual.kind; }
	const StationVisual& visual() const { return m_visual; }
	bool isInterchange() const;
	bool isEndpoint() const;

	static bool priorityLess(const StationOverlayItem& left, const StationOverlayItem& right,
		const QPointF& viewportCenter);

protected:
	void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
	void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
	void rebuildGeometry();
	QRectF translated(const QRectF& rect) const;

	QString m_stationName;
	QString m_displayName;
	QPointF m_stableAnchor;
	QPointF m_viewportOffset;
	StationVisual m_visual;
	QPixmap m_symbol;
	QFont m_labelFont;
	QRectF m_symbolRect;
	QRectF m_labelRect;
	LabelSide m_labelSide = LabelSide::Right;
	bool m_layoutVisible = true;
	bool m_hovered = false;
	bool m_followed = false;
	int m_degree = 0;
};

#endif // STATIONOVERLAYITEM_H
