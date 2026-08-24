#ifndef EGTRAIN_RELEASE_INFO_H
#define EGTRAIN_RELEASE_INFO_H

#include "util/Version.h"

#include <QByteArray>
#include <QString>
#include <QUrl>
#include <optional>

struct StableRelease {
	SemanticVersion version;
	QString tag;
	QString notes;
	QUrl releasePage;
};

std::optional<StableRelease> parseLatestStableRelease(const QByteArray& json,
	QString* error = nullptr);
bool isUpdateAvailable(const SemanticVersion& current, const StableRelease& release);
QString formatSemanticVersion(const SemanticVersion& version);

#endif
