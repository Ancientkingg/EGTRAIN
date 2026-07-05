#ifndef HIGHLIGHTEFFECT_H
#define HIGHLIGHTEFFECT_H

#include <QGraphicsColorizeEffect>

class HighlightEffect : public QGraphicsColorizeEffect {
	Q_OBJECT

public:
	HighlightEffect(QColor color, qreal strength, QObject* parent = 0);
	~HighlightEffect();
};

#endif // HIGHLIGHTEFFECT_H