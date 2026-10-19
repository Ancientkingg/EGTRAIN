#pragma once

#include <QString>
#include <QStringList>

// What a command line asks the application to open. The functions use only Qt Core and read
// no file, so the application and a test share them.

/// The answer for a list of paths: exactly one of the two members is not empty.
struct SceneOpenChoice {
	QString path;	   ///< The absolute path of the one .egscene file that may be opened.
	QString rejection; ///< Why the paths cannot be opened, in words for the person who gave them.
};

/// Accepts exactly one path that names an existing file with the extension .egscene, in any case.
/// The checks run in this order: the count, the existence, the kind (a file, not a directory) and
/// the extension.
SceneOpenChoice classifyOpenPaths(const QStringList& paths);

/// The arguments that are positional: not the program name (the first entry), not an argument
/// that starts with "-", and not the one argument after an option of optionsWithValue. An option
/// that is not in optionsWithValue is skipped without its value.
QStringList positionalArguments(const QStringList& arguments, const QStringList& optionsWithValue);

/// The argument after the first occurrence of option, or an empty string when option is absent or
/// the last entry.
QString optionValue(const QStringList& arguments, const QString& option);
