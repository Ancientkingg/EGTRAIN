#pragma once

#include "widgets/UserPrompts.h"

#include <QFileInfo>
#include <QLatin1Char>
#include <QString>
#include <QWidget>

/// The path that a save dialog returned, with `.suffix` appended when its name does not end in it, ignoring case. A save
/// dialog asks before it returns the name of a file that exists, but it does not see the name with the suffix, so when
/// the suffix is appended and that file exists, `prompts` asks under `title` whether to replace it, with No as the
/// default. Returns an empty string when the answer is No.
inline QString savePathWithSuffix(QWidget* parent, UserPrompts& prompts, const QString& title, const QString& path,
	const QString& suffix) {
	const QString dotted = QLatin1Char('.') + suffix;
	if (path.endsWith(dotted, Qt::CaseInsensitive))
		return path;
	QString withSuffix = path + dotted;
	if (QFileInfo::exists(withSuffix)
		&& !prompts.confirm(parent, title, QString("%1 already exists. Replace it?").arg(QFileInfo(withSuffix).fileName())))
		return QString();
	return withSuffix;
}
