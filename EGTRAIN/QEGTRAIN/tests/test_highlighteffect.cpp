#include "graphics/items/HighlightEffect.h"

#include <QApplication>
#include <QBrush>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsRectItem>
#include <QPen>

#include <iostream>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

int main(int argc, char* argv[]) {
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv);

	HighlightEffect* effect = new HighlightEffect(Qt::blue, 1.0);
	QGraphicsDropShadowEffect* halo = dynamic_cast<QGraphicsDropShadowEffect*>(effect);
	bool ok = expect(halo != nullptr, "highlight effect is a drop shadow");
	if (halo) {
		ok &= expect(halo->offset() == QPointF(0.0, 0.0), "highlight effect has zero offset");
		ok &= expect(halo->blurRadius() > 0.0, "highlight effect has visible blur");
		ok &= expect(halo->color().alpha() > 0, "highlight effect color is visible");
	}

	QGraphicsRectItem item(QRectF(0.0, 0.0, 10.0, 10.0));
	const QPen sourcePen(Qt::darkGray);
	const QBrush sourceBrush(Qt::yellow);
	item.setPen(sourcePen);
	item.setBrush(sourceBrush);
	item.setGraphicsEffect(effect);
	ok &= expect(item.pen() == sourcePen, "highlight effect preserves source pen");
	ok &= expect(item.brush() == sourceBrush, "highlight effect preserves source brush");

	if (!ok)
		return 1;

	std::cout << "all HighlightEffect tests passed\n";
	return 0;
}
