#ifndef MYQGRAPHICSCOLORIZEEFFECT_H
#define MYQGRAPHICSCOLORIZEEFFECT_H

#include <QGraphicsColorizeEffect>

class myQGraphicsColorizeEffect : public QGraphicsColorizeEffect {
	Q_OBJECT

public:
	myQGraphicsColorizeEffect(QColor color, qreal strength, QObject* parent = 0);
	~myQGraphicsColorizeEffect();
};

#endif // MYQGRAPHICSCOLORIZEEFFECT_H