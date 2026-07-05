#ifndef BASENETWORKITEM_H
#define BASENETWORKITEM_H

#include <QGraphicsItem>

class BaseNetworkItem : public QGraphicsItem {
	// Q_OBJECT

public:
	BaseNetworkItem(QGraphicsItem* parent = 0);
	~BaseNetworkItem();
};

#endif // BASENETWORKITEM_H