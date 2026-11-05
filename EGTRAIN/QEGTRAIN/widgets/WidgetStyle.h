#pragma once

#include <QApplication>
#include <QLatin1String>
#include <QString>
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

/// The application style sheet sheet as EGTRAIN applies it under the widget style named styleName (QStyle::name()).
/// Under the macOS style and Fusion it ends with a rule that has the style sheet draw each item of a list, table or tree
/// whole; under any other style, windowsvista and windows11 among them, it is sheet.
inline QString applicationStyleSheet(const QString& sheet, const QString& styleName) {
	// The style sheet draws an item whole only when a rule for the item draws something. For any other item it draws only
	// the check box and leaves the rest of the item to the widget style, which draws its own check box as well, and that
	// box shows wherever it is larger than the styled one, as under the macOS style. An empty image is such a rule and
	// paints nothing itself; a background would also become the colour behind the focus frame of the item. Other styles
	// keep drawing their items: windowsvista and windows11, for example, draw a hover highlight on an item, and the style
	// sheet draws none.
	if (styleName.compare(QLatin1String("macos"), Qt::CaseInsensitive) != 0
		&& styleName.compare(QLatin1String("fusion"), Qt::CaseInsensitive) != 0)
		return sheet;
	return sheet + QStringLiteral("\nQListWidget::item, QTableView::item, QTreeView::item {\n    image: none;\n}\n");
}
