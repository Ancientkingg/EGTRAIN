#pragma once

#include <QChar>
#include <QDir>
#include <QString>

// A path as the person types and reads it on this system, with backslashes on Windows. It does what
// QDir::toNativeSeparators does. Show every path in a tooltip or a text with it and keep the stored path unchanged.
namespace display_path {

// The separator that format writes. Only a test sets another one, to check the Windows form on any system.
inline QChar& separator() {
	static QChar value = QDir::separator();
	return value;
}

inline QString format(QString path) {
	return path.replace(QLatin1Char('/'), separator());
}

} // namespace display_path
