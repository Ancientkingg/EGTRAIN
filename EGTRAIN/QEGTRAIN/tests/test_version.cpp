#include "util/Version.h"

#include <QString>
#include <QTest>

class TestVersion : public QObject {
	Q_OBJECT

private slots:
	void ordersNumerically();
	void rejectsMalformedVersion_data();
	void rejectsMalformedVersion();
	void rejectsPrereleaseAndBuild_data();
	void rejectsPrereleaseAndBuild();
	void rejectsNonStableTags();
};

void TestVersion::ordersNumerically() {
	const auto older = parseStableVersion("1.9.0");
	const auto newer = parseStableVersion("1.10.0");
	// Numeric semantic-version ordering.
	QVERIFY(older.has_value());
	QVERIFY(newer.has_value());
	QVERIFY(*older < *newer);
	QVERIFY2(*newer == *newer, "equal versions compare equal");
	QVERIFY2(parseStableTag("v1.10.0") == newer, "stable v tag parses");
}

void TestVersion::rejectsMalformedVersion_data() {
	QTest::addColumn<QString>("text");
	for (const char* malformed : {"", "1.0", "1.0.0.0", "1.a.0", "01.0.0", "1.0.00"})
		QTest::newRow(*malformed ? malformed : "empty") << QString::fromLatin1(malformed);
}

void TestVersion::rejectsMalformedVersion() {
	QFETCH(QString, text);
	QVERIFY2(!parseStableVersion(text.toStdString()), "malformed stable version rejected");
}

void TestVersion::rejectsPrereleaseAndBuild_data() {
	QTest::addColumn<QString>("text");
	for (const char* nonStable : {"1.0.0-alpha", "1.0.0+build", "1.0.0-rc.1"})
		QTest::newRow(nonStable) << QString::fromLatin1(nonStable);
}

void TestVersion::rejectsPrereleaseAndBuild() {
	QFETCH(QString, text);
	QVERIFY2(!parseStableVersion(text.toStdString()), "prerelease/build metadata rejected");
}

void TestVersion::rejectsNonStableTags() {
	// Non-stable tags rejected.
	QVERIFY(!parseStableTag("1.0.0"));
	QVERIFY(!parseStableTag("v1.0.0-alpha"));
}

QTEST_APPLESS_MAIN(TestVersion)
#include "test_version.moc"
