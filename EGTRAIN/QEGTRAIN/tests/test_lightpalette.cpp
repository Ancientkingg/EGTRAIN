// The light palette that EGTRAIN takes while the desktop is dark is light, and in Qt 6 it is the light standard palette of
// Fusion in every group and role. The offscreen platform has no colour scheme, so Fusion gives its light palette here.
#include "widgets/LightPalette.h"

#include <QApplication>
#include <QStyle>
#include <QStyleFactory>

#include <iostream>
#include <memory>

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	const QPalette light = lightFusionPalette();
	bool ok = light.color(QPalette::Window).lightness() > light.color(QPalette::WindowText).lightness()
		&& light.color(QPalette::Base) == QColor(Qt::white) && light.color(QPalette::Text) == QColor(Qt::black);
	if (!ok)
		std::cerr << "the light palette is not light\n";
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
	const QPalette fusion = std::unique_ptr<QStyle>(QStyleFactory::create(QStringLiteral("Fusion")))->standardPalette();
	for (int group = 0; group < QPalette::NColorGroups; ++group) {
		for (int role = 0; role < QPalette::NColorRoles; ++role) {
			const auto colorGroup = static_cast<QPalette::ColorGroup>(group);
			const auto colorRole = static_cast<QPalette::ColorRole>(role);
			if (light.brush(colorGroup, colorRole) == fusion.brush(colorGroup, colorRole))
				continue;
			std::cerr << "group " << group << " role " << role << " is " << qPrintable(light.color(colorGroup, colorRole).name(QColor::HexArgb))
					  << " instead of " << qPrintable(fusion.color(colorGroup, colorRole).name(QColor::HexArgb)) << '\n';
			ok = false;
		}
	}
#endif
	return ok ? 0 : 1;
}
