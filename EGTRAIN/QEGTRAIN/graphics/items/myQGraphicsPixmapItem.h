#ifndef MYQGRAPHICSPIXMAPITEM_H
#define MYQGRAPHICSPIXMAPITEM_H

#include <QGraphicsPixmapItem>

class myQGraphicsPixmapItem : public QGraphicsPixmapItem {
	// Q_OBJECT

public:
	myQGraphicsPixmapItem(const QPixmap& pixmap, QGraphicsItem* parent = 0);
	~myQGraphicsPixmapItem();

	// to allow cast
	enum { Type = UserType + 3 };
	int type() const {
		// Enable the use of qgraphicsitem_cast with this item.
		return Type;
	}
};

#endif // MYQGRAPHICSPIXMAPITEM_H