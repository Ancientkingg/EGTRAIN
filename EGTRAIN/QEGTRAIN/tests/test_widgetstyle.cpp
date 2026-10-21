// The -style option and QT_STYLE_OVERRIDE choose the widget style. Without them the windows11 style that Qt gives
// Windows 11 becomes windowsvista, and any other style stays. On Windows the test runs on the windows platform, so on
// Windows 11 it sees the change.
#include "widgets/WidgetStyle.h"

#include <QApplication>
#include <QOperatingSystemVersion>
#include <QStyle>

#include <initializer_list>
#include <iostream>
#include <vector>

static bool chosen(std::initializer_list<const char*> arguments) {
	std::vector<char*> argv;
	for (const char* argument : arguments)
		argv.push_back(const_cast<char*>(argument));
	return widgetStyleChosen(static_cast<int>(argv.size()), argv.data());
}

int main(int argc, char** argv) {
	bool ok = true;
	const auto expect = [&ok](bool actual, bool expected, const char* arguments) {
		if (actual == expected)
			return;
		std::cerr << arguments << (expected ? " chooses no style\n" : " chooses a style\n");
		ok = false;
	};
	expect(chosen({"QEGTRAIN"}), false, "QEGTRAIN");
	expect(chosen({"QEGTRAIN", "-style", "fusion"}), true, "QEGTRAIN -style fusion");
	expect(chosen({"QEGTRAIN", "--style", "fusion"}), true, "QEGTRAIN --style fusion");
	expect(chosen({"QEGTRAIN", "-style=fusion"}), true, "QEGTRAIN -style=fusion");
	expect(chosen({"QEGTRAIN", "--style=fusion"}), true, "QEGTRAIN --style=fusion");
	expect(chosen({"QEGTRAIN", "-stylesheet", "egtrain.qss"}), false, "QEGTRAIN -stylesheet egtrain.qss");
	const bool overridden = qEnvironmentVariableIsSet("QT_STYLE_OVERRIDE");
	if (!overridden) {
		qputenv("QT_STYLE_OVERRIDE", "fusion");
		expect(chosen({"QEGTRAIN"}), true, "QT_STYLE_OVERRIDE=fusion QEGTRAIN");
		qunsetenv("QT_STYLE_OVERRIDE");
	}

	QApplication app(argc, argv);
	const QString first = QApplication::style()->name();
	const bool windows11 = first.compare(QLatin1String("windows11"), Qt::CaseInsensitive) == 0;
#ifdef Q_OS_WIN
	if (!overridden && QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows11 && !windows11) {
		std::cerr << "Windows 11 gave the style " << qPrintable(first) << " instead of windows11\n";
		ok = false;
	}
#endif
	preferWindowsVistaStyle(true);
	if (QApplication::style()->name() != first) {
		std::cerr << "the chosen style " << qPrintable(first) << " became " << qPrintable(QApplication::style()->name()) << '\n';
		ok = false;
	}
	preferWindowsVistaStyle(false);
	const QString expected = windows11 ? QStringLiteral("windowsvista") : first;
	if (QApplication::style()->name().compare(expected, Qt::CaseInsensitive) != 0) {
		std::cerr << "the style " << qPrintable(first) << " became " << qPrintable(QApplication::style()->name()) << " instead of "
				  << qPrintable(expected) << '\n';
		ok = false;
	}
	return ok ? 0 : 1;
}
