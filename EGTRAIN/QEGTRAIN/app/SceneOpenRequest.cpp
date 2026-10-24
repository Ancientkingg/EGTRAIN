#include "app/SceneOpenRequest.h"

#include <QFileInfo>

SceneOpenChoice classifyOpenPaths(const QStringList& paths) {
	SceneOpenChoice choice;
	if (paths.isEmpty()) {
		choice.rejection = QStringLiteral("no file was given");
		return choice;
	}
	if (paths.size() != 1) {
		choice.rejection = QStringLiteral("more than one file was given");
		return choice;
	}
	const QFileInfo info(paths.front());
	if (!info.exists()) {
		choice.rejection = QStringLiteral("the file does not exist");
		return choice;
	}
	if (!info.isFile()) {
		choice.rejection = QStringLiteral("it is a folder, not an .egscene file");
		return choice;
	}
	if (info.suffix().compare(QStringLiteral("egscene"), Qt::CaseInsensitive) != 0) {
		choice.rejection = QStringLiteral("it is not an .egscene file");
		return choice;
	}
	choice.path = info.absoluteFilePath();
	return choice;
}

QStringList positionalArguments(const QStringList& arguments, const QStringList& optionsWithValue) {
	QStringList positional;
	bool skipValue = false;
	for (int i = 1; i < arguments.size(); ++i) {
		const QString& argument = arguments.at(i);
		if (skipValue) {
			skipValue = false;
		} else if (argument.startsWith(QLatin1Char('-'))) {
			skipValue = optionsWithValue.contains(argument);
		} else {
			positional.append(argument);
		}
	}
	return positional;
}

QString optionValue(const QStringList& arguments, const QString& option) {
	const auto index = arguments.indexOf(option);
	if (index < 0 || index + 1 >= arguments.size())
		return QString();
	return arguments.at(index + 1);
}
