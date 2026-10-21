#pragma once

#include <QColor>
#include <QPalette>

/// The light standard palette of the Fusion style in Qt 6. From Qt 6.5 Fusion gives a dark standard palette while the
/// desktop uses a dark colour scheme, and EGTRAIN has one light look, so the application takes this palette then.
inline QPalette lightFusionPalette() {
	const QColor window(239, 239, 239);
	const QColor dark = window.darker(150);
	const QColor mid = window.darker(130);
	const QColor shadow = dark.darker(135);
	const QColor highlight(48, 140, 198);
	const QColor disabledText(190, 190, 190);
	const QColor disabledHighlight(145, 145, 145);
	QPalette palette(Qt::black, window, window.lighter(150), dark, mid, Qt::black, Qt::white);
	palette.setBrush(QPalette::Midlight, mid.lighter(110));
	palette.setBrush(QPalette::Button, window);
	palette.setBrush(QPalette::Shadow, shadow);
	palette.setBrush(QPalette::HighlightedText, QColor(Qt::white));
	for (const QPalette::ColorRole role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
		palette.setBrush(QPalette::Disabled, role, disabledText);
	palette.setBrush(QPalette::Disabled, QPalette::Base, window);
	palette.setBrush(QPalette::Disabled, QPalette::Dark, QColor(209, 209, 209).darker(110));
	palette.setBrush(QPalette::Disabled, QPalette::Shadow, shadow.lighter(150));
	for (const QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive}) {
		palette.setBrush(group, QPalette::Highlight, highlight);
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
		palette.setBrush(group, QPalette::Accent, highlight);
#endif
	}
	palette.setBrush(QPalette::Disabled, QPalette::Highlight, disabledHighlight);
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
	palette.setBrush(QPalette::Disabled, QPalette::Accent, disabledHighlight);
#endif
	QColor placeholder(Qt::black);
	placeholder.setAlpha(128);
	palette.setBrush(QPalette::PlaceholderText, placeholder);
	return palette;
}
