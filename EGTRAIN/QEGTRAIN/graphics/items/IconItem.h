#ifndef ICONITEM_H
#define ICONITEM_H

#include <QGraphicsPixmapItem>

class IconItem : public QGraphicsPixmapItem {
	// Q_OBJECT

public:
	IconItem(const QPixmap& pixmap, QGraphicsItem* parent = nullptr);
	~IconItem() override;

	// to allow cast
	enum { Type = UserType + 3 };
	int type() const override {
		// Enable the use of qgraphicsitem_cast with this item.
		return Type;
	}
};

#endif // ICONITEM_H
