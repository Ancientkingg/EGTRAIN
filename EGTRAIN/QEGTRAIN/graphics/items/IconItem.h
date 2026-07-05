#ifndef ICONITEM_H
#define ICONITEM_H

#include <QGraphicsPixmapItem>

class IconItem : public QGraphicsPixmapItem {
	// Q_OBJECT

public:
	IconItem(const QPixmap& pixmap, QGraphicsItem* parent = 0);
	~IconItem();

	// to allow cast
	enum { Type = UserType + 3 };
	int type() const {
		// Enable the use of qgraphicsitem_cast with this item.
		return Type;
	}
};

#endif // ICONITEM_H