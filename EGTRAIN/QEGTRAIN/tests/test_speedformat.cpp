#include "util/SpeedFormat.h"

#include <QTest>

class TestSpeedFormat : public QObject {
	Q_OBJECT

private slots:
	void labelsZeroSpeed();
	void roundsToWholeKilometresPerHour();
	void clampsNegativeSpeedToZero();
};

void TestSpeedFormat::labelsZeroSpeed() {
	QCOMPARE(formatSpeedLabel(0.0), "0 km/h");
}

void TestSpeedFormat::roundsToWholeKilometresPerHour() {
	QCOMPARE(formatSpeedLabel(82.6), "83 km/h");
	QCOMPARE(formatSpeedLabel(82.4), "82 km/h");
}

void TestSpeedFormat::clampsNegativeSpeedToZero() {
	QCOMPARE(formatSpeedLabel(-3.0), "0 km/h");
}

QTEST_APPLESS_MAIN(TestSpeedFormat)
#include "test_speedformat.moc"
