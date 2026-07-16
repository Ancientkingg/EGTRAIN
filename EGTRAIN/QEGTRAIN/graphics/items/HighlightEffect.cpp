#include "graphics/items/HighlightEffect.h"

HighlightEffect::HighlightEffect(QColor color, qreal strength, QObject* parent)
	: QGraphicsDropShadowEffect(parent) {
	setOffset(0.0, 0.0);
	setBlurRadius(qBound<qreal>(8.0, 8.0 + strength * 4.0, 16.0));
	color.setAlphaF(qBound<qreal>(0.25, strength, 1.0) * 0.55);
	setColor(color);
}

HighlightEffect::~HighlightEffect() {
}
