#ifndef NETWORKLEGENDITEM_H
#define NETWORKLEGENDITEM_H

#include <QGraphicsItem>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWidget>

class NetworkLegendItem : public QGraphicsItem {
public:
	NetworkLegendItem(QGraphicsItem* parent = nullptr);

	QRectF boundingRect() const override;
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;

	enum { Type = UserType + 12 };
	int type() const override { return Type; }
};

#endif // NETWORKLEGENDITEM_H
