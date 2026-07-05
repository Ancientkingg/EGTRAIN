#include "graphics/items/HighlightEffect.h"

HighlightEffect::HighlightEffect(QColor color, qreal strength, QObject* parent)
	: QGraphicsColorizeEffect(parent) {
	setColor(color);
	setStrength(strength);
}

HighlightEffect::~HighlightEffect() {
}
