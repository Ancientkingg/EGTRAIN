// The light palette that EGTRAIN takes while the desktop is dark is light, and in Qt 6 it is the light standard palette of
// Fusion in every group and role. The offscreen platform has no colour scheme, so Fusion gives its light palette here.
#include "widgets/LightPalette.h"

#include <QStringList>
#include <QStyle>
#include <QStyleFactory>
#include <QTest>

#include <memory>

class TestLightPalette : public QObject {
	Q_OBJECT

private slots:
	void isLight();
	void isFusionStandardPalette();
};

void TestLightPalette::isLight() {
	const QPalette light = lightFusionPalette();
	// The light palette is light.
	QCOMPARE_GT(light.color(QPalette::Window).lightness(), light.color(QPalette::WindowText).lightness());
	QCOMPARE(light.color(QPalette::Base), QColor(Qt::white));
	QCOMPARE(light.color(QPalette::Text), QColor(Qt::black));
}

void TestLightPalette::isFusionStandardPalette() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
	const QPalette light = lightFusionPalette();
	const QPalette fusion = std::unique_ptr<QStyle>(QStyleFactory::create(QStringLiteral("Fusion")))->standardPalette();
	QStringList differences;
	for (int group = 0; group < QPalette::NColorGroups; ++group) {
		for (int role = 0; role < QPalette::NColorRoles; ++role) {
			const auto colorGroup = static_cast<QPalette::ColorGroup>(group);
			const auto colorRole = static_cast<QPalette::ColorRole>(role);
			if (light.brush(colorGroup, colorRole) == fusion.brush(colorGroup, colorRole))
				continue;
			differences << QStringLiteral("group %1 role %2 is %3 instead of %4")
							   .arg(group)
							   .arg(role)
							   .arg(light.color(colorGroup, colorRole).name(QColor::HexArgb), fusion.color(colorGroup, colorRole).name(QColor::HexArgb));
		}
	}
	QVERIFY2(differences.isEmpty(), qPrintable(differences.join(QLatin1Char('\n'))));
#else
	QSKIP("before Qt 6.5 Fusion has one palette, so there is nothing to compare");
#endif
}

QTEST_MAIN(TestLightPalette)
#include "test_lightpalette.moc"
