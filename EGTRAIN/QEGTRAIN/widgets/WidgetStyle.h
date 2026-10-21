#pragma once

#include <QApplication>
#include <QLatin1String>
#include <QStyle>
#include <QStyleFactory>

#include <cstring>

/// Whether the command line or QT_STYLE_OVERRIDE names the widget style. QApplication takes -style out of the arguments,
/// so this looks at them before the application object exists.
inline bool widgetStyleChosen(int argc, char** argv) {
	if (qEnvironmentVariableIsSet("QT_STYLE_OVERRIDE"))
		return true;
	for (int i = 1; i < argc; ++i) {
		const char* argument = argv[i];
		if (std::strncmp(argument, "--", 2) == 0)
			++argument;
		if (std::strcmp(argument, "-style") == 0 || std::strncmp(argument, "-style=", 7) == 0)
			return true;
	}
	return false;
}

/// Qt 6.7 and later give Windows 11 the windows11 style. Its menu bar and spin boxes are taller than those of
/// windowsvista, the style of Qt 5 that the windows were laid out in, so that Case and Layers needs a scroll bar with
/// Paimpol open on a 1024 x 768 screen. Unless the user chose a style, EGTRAIN keeps windowsvista.
inline void preferWindowsVistaStyle(bool styleChosen) {
	if (styleChosen || QApplication::style()->name().compare(QLatin1String("windows11"), Qt::CaseInsensitive) != 0)
		return;
	if (QStyle* vista = QStyleFactory::create(QStringLiteral("windowsvista")))
		QApplication::setStyle(vista);
}
