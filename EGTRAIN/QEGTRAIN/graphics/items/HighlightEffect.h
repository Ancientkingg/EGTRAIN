#ifndef HIGHLIGHTEFFECT_H
#define HIGHLIGHTEFFECT_H

#include <QGraphicsDropShadowEffect>

class HighlightEffect : public QGraphicsDropShadowEffect {
	Q_OBJECT

public:
	HighlightEffect(QColor color, qreal strength, QObject* parent = 0);
	~HighlightEffect();
};

#endif // HIGHLIGHTEFFECT_H
