#ifndef TRAINBADGEITEM_H
#define TRAINBADGEITEM_H

#include <QGraphicsItem>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWidget>

#include "graphics/VisualPolish.h"

class TrainBadgeItem : public QGraphicsItem {
public:
	TrainBadgeItem(QGraphicsItem* parent = nullptr);

	void setIdentifier(const QString& identifier);
	void setSpeedText(const QString& speedText);
	void setTrainVisual(const TrainVisual& visual);
	void setReversed(bool reversed);
	void setCompact(bool compact);

	QRectF boundingRect() const override;
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;

	enum { Type = UserType + 11 };
	int type() const override { return Type; }

private:
	QRectF badgeRect() const;

	QString m_identifier;
	QString m_speedText;
	TrainVisual m_visual;
	bool m_reversed;
	bool m_compact;
};

#endif // TRAINBADGEITEM_H
