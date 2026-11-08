// Minimal tests for time formatting utility.
#include "util/TimeFormat.h"
#include "util/timeutil.hpp"

#include <QTest>

#include <ctime>
#include <limits>
#include <optional>
#include <string>

class TestTimeFormat : public QObject {
	Q_OBJECT

private slots:
	void formatsSimTime_data();
	void formatsSimTime();
	void roundtripsPlannedClockText_data();
	void roundtripsPlannedClockText();
	void unchangedPlannedTextKeepsPrecision_data();
	void unchangedPlannedTextKeepsPrecision();
	void blankPlannedTimeIsAbsent();
	void zeroBaseMidnightIsPresentZero();
	void rejectsInvalidClockText_data();
	void rejectsInvalidClockText();
	void rejectsInvalidElapsedText_data();
	void rejectsInvalidElapsedText();
	void keepsNegativeContextButRejectsNegativeEdits();
	void negativeMidnightHasNoSignedZero();
	void nonFiniteValueFailsUnchangedText();
	void formatsDateTimeWithSingleDigits();
};

void TestTimeFormat::formatsSimTime_data() {
	QTest::addColumn<qlonglong>("simSeconds");
	QTest::addColumn<qlonglong>("baseOffset");
	QTest::addColumn<QString>("expected");
	// Seconds since start, no offset.
	QTest::newRow("zero") << 0LL << 0LL << "00:00:00";
	QTest::newRow("minute second") << 61LL << 0LL << "00:01:01";
	QTest::newRow("hour minute second") << 3661LL << 0LL << "01:01:01";
	// Offset (08:00 = 28800s) plus 90s sim time.
	QTest::newRow("offset") << 90LL << 28800LL << "08:01:30";
	QTest::newRow("wrap") << 3600LL << 23LL * 3600 << "00:00:00";
	QTest::newRow("negative clamp") << -5LL << 0LL << "00:00:00";
}

void TestTimeFormat::formatsSimTime() {
	QFETCH(qlonglong, simSeconds);
	QFETCH(qlonglong, baseOffset);
	QFETCH(QString, expected);
	QCOMPARE(QString::fromStdString(formatSimTime(simSeconds, baseOffset)), expected);
}

void TestTimeFormat::roundtripsPlannedClockText_data() {
	QTest::addColumn<double>("seconds");
	QTest::addColumn<QString>("text");
	QTest::newRow("08:00:00") << 0.0 << "08:00:00";
	QTest::newRow("08:01:30") << 90.0 << "08:01:30";
	QTest::newRow("08:01:30.125") << 90.125 << "08:01:30.125";
	QTest::newRow("+1d 00:00:00") << 57600.0 << "+1d 00:00:00";
	QTest::newRow("+2d 00:00:01") << 144001.0 << "+2d 00:00:01";
}

void TestTimeFormat::roundtripsPlannedClockText() {
	QFETCH(double, seconds);
	QFETCH(QString, text);
	QCOMPARE(QString::fromStdString(formatPlannedTime(seconds, true, 28800)), text);
	std::optional<double> value;
	QVERIFY(parsePlannedTime(text.toStdString(), true, 28800, value));
	QVERIFY(value == seconds);
}

void TestTimeFormat::unchangedPlannedTextKeepsPrecision_data() {
	QTest::addColumn<bool>("clock");
	QTest::newRow("elapsed") << false;
	QTest::newRow("clock") << true;
}

void TestTimeFormat::unchangedPlannedTextKeepsPrecision() {
	QFETCH(bool, clock);
	std::optional<double> value = 90.1234567890123;
	const double original = *value;
	QVERIFY(parsePlannedTime(formatPlannedTime(value, clock, 28800), clock, 28800, value));
	QVERIFY(value == original);
}

void TestTimeFormat::blankPlannedTimeIsAbsent() {
	std::optional<double> value = 90.1234567890123;
	QVERIFY(parsePlannedTime("", true, 28800, value));
	QVERIFY(!value);
	QVERIFY(formatPlannedTime(value, false, 0).empty());
}

void TestTimeFormat::zeroBaseMidnightIsPresentZero() {
	std::optional<double> value;
	QVERIFY(parsePlannedTime("00:00:00", true, 0, value));
	QVERIFY(value == 0.0);
}

// Invalid clock input leaves the value unchanged; there is no implicit tomorrow.
void TestTimeFormat::rejectsInvalidClockText_data() {
	QTest::addColumn<QString>("text");
	for (const char* invalid : {"07:59:59", "24:00:00", "08:60:00", "08:00:60", "8:00:00", "08:00", "08:00:00junk", "+1d", "nan",
			 "-1d 08:00:00"})
		QTest::newRow(invalid) << invalid;
}

void TestTimeFormat::rejectsInvalidClockText() {
	QFETCH(QString, text);
	std::optional<double> value = 0.0;
	QVERIFY(!parsePlannedTime(text.toStdString(), true, 28800, value));
	QVERIFY(value == 0.0);
}

// Invalid elapsed input leaves the value unchanged.
void TestTimeFormat::rejectsInvalidElapsedText_data() {
	QTest::addColumn<QString>("text");
	for (const char* invalid : {"-1", "nan", "inf", "1e309", "12junk", " 12"})
		QTest::newRow(invalid) << invalid;
}

void TestTimeFormat::rejectsInvalidElapsedText() {
	QFETCH(QString, text);
	std::optional<double> value = 0.0;
	QVERIFY(!parsePlannedTime(text.toStdString(), false, 0, value));
	QVERIFY(value == 0.0);
}

void TestTimeFormat::keepsNegativeContextButRejectsNegativeEdits() {
	std::optional<double> value = -28801.5;
	QCOMPARE(formatPlannedTime(value, true, 28800), "-1d 23:59:58.5");
	QVERIFY(parsePlannedTime("-1d 23:59:58.5", true, 28800, value));
	QVERIFY(value == -28801.5);
	QVERIFY(!parsePlannedTime("-1d 23:59:58", true, 28800, value));
}

void TestTimeFormat::negativeMidnightHasNoSignedZero() {
	QCOMPARE(formatPlannedTime(-115200.0, true, 28800), "-1d 00:00:00");
}

void TestTimeFormat::nonFiniteValueFailsUnchangedText() {
	std::optional<double> value = std::numeric_limits<double>::infinity();
	QVERIFY2(!parsePlannedTime(formatPlannedTime(value, false, 0), false, 0, value),
		"non-finite stored values cannot pass unchanged-text validation");
}

void TestTimeFormat::formatsDateTimeWithSingleDigits() {
	std::tm timestamp{};
	timestamp.tm_year = 124;
	timestamp.tm_mon = 3;
	timestamp.tm_mday = 2;
	timestamp.tm_hour = 4;
	timestamp.tm_min = 5;
	timestamp.tm_sec = 6;
	timestamp.tm_wday = 2;
	QCOMPARE(formatDateTime(timestamp), "Tue 02.04.2024 4:05:06");
	QCOMPARE(formatDateTimeFilename(timestamp), "2024_04_02_4_05_06");
}

QTEST_APPLESS_MAIN(TestTimeFormat)
#include "test_timeformat.moc"
