// The -style option and QT_STYLE_OVERRIDE choose the widget style. Without them the windows11 style that Qt gives
// Windows 11 becomes windowsvista, and any other style stays. On Windows the test runs on the windows platform, so on
// Windows 11 it sees the change.
#include "widgets/WidgetStyle.h"

#include <QApplication>
#include <QByteArray>
#include <QOperatingSystemVersion>
#include <QScopeGuard>
#include <QStringList>
#include <QStyle>
#include <QTest>

#include <vector>

// widgetStyleChosen reads only its arguments and the environment, so it gives the same answer after the application
// object exists.
static bool chosen(const QStringList& arguments) {
	std::vector<QByteArray> texts;
	for (const QString& argument : arguments)
		texts.push_back(argument.toLocal8Bit());
	std::vector<char*> argv;
	for (QByteArray& text : texts)
		argv.push_back(text.data());
	return widgetStyleChosen(static_cast<int>(argv.size()), argv.data());
}

class TestWidgetStyle : public QObject {
	Q_OBJECT

private slots:
	void commandLine_data();
	void commandLine();
	void styleOverride();
	void windows11BecomesWindowsVista();
};

void TestWidgetStyle::commandLine_data() {
	QTest::addColumn<QStringList>("arguments");
	QTest::addColumn<bool>("expected");
	QTest::newRow("QEGTRAIN") << QStringList{"QEGTRAIN"} << false;
	QTest::newRow("QEGTRAIN -style fusion") << QStringList{"QEGTRAIN", "-style", "fusion"} << true;
	QTest::newRow("QEGTRAIN --style fusion") << QStringList{"QEGTRAIN", "--style", "fusion"} << true;
	QTest::newRow("QEGTRAIN -style=fusion") << QStringList{"QEGTRAIN", "-style=fusion"} << true;
	QTest::newRow("QEGTRAIN --style=fusion") << QStringList{"QEGTRAIN", "--style=fusion"} << true;
	QTest::newRow("QEGTRAIN -stylesheet egtrain.qss") << QStringList{"QEGTRAIN", "-stylesheet", "egtrain.qss"} << false;
}

void TestWidgetStyle::commandLine() {
	QFETCH(QStringList, arguments);
	QFETCH(bool, expected);
	QCOMPARE(chosen(arguments), expected);
}

void TestWidgetStyle::styleOverride() {
	if (qEnvironmentVariableIsSet("QT_STYLE_OVERRIDE"))
		QSKIP("QT_STYLE_OVERRIDE is set for the test");
	qputenv("QT_STYLE_OVERRIDE", "fusion");
	const auto unsetOverride = qScopeGuard([] { qunsetenv("QT_STYLE_OVERRIDE"); });
	QVERIFY2(chosen({"QEGTRAIN"}), "QT_STYLE_OVERRIDE=fusion QEGTRAIN chooses no style");
}

void TestWidgetStyle::windows11BecomesWindowsVista() {
	const QString first = QApplication::style()->name();
	const bool windows11 = first.compare(QLatin1String("windows11"), Qt::CaseInsensitive) == 0;
#ifdef Q_OS_WIN
	if (!qEnvironmentVariableIsSet("QT_STYLE_OVERRIDE") && QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows11)
		QVERIFY2(windows11, qPrintable(QStringLiteral("Windows 11 gave the style %1 instead of windows11").arg(first)));
#endif
	preferWindowsVistaStyle(true);
	// The chosen style stays.
	QCOMPARE(QApplication::style()->name(), first);
	preferWindowsVistaStyle(false);
	const QString expected = windows11 ? QStringLiteral("windowsvista") : first;
	QVERIFY2(QApplication::style()->name().compare(expected, Qt::CaseInsensitive) == 0,
		qPrintable(QStringLiteral("the style %1 became %2 instead of %3").arg(first, QApplication::style()->name(), expected)));
}

QTEST_MAIN(TestWidgetStyle)
#include "test_widgetstyle.moc"
