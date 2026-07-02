#include "myQGraphicsColorizeEffect.h"

myQGraphicsColorizeEffect::myQGraphicsColorizeEffect(QColor color, qreal strength, QObject* parent)
	: QGraphicsColorizeEffect(parent) {
	setColor(color);
	setStrength(strength);
}

myQGraphicsColorizeEffect::~myQGraphicsColorizeEffect() {
}
