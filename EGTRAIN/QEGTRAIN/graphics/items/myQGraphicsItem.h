#ifndef MYQGRAPHICSITEM_H
#define MYQGRAPHICSITEM_H

#include <QGraphicsItem>

class myQGraphicsItem : public QGraphicsItem {
	// Q_OBJECT

public:
	myQGraphicsItem(QGraphicsItem* parent = 0);
	~myQGraphicsItem();
};

#endif // MYQGRAPHICSITEM_H