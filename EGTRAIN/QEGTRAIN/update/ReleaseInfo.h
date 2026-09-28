#ifndef EGTRAIN_RELEASE_INFO_H
#define EGTRAIN_RELEASE_INFO_H

#include "util/Version.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QUrl>
#include <optional>

struct ReleaseAsset {
	QString name;
	QUrl downloadUrl;
};

struct StableRelease {
	SemanticVersion version;
	QString tag;
	QString notes;
	QUrl releasePage;
	QList<ReleaseAsset> assets;

	const ReleaseAsset* asset(const QString& name) const;
};

struct UpdateManifest {
	QString version;
	QString platform;
	QString assetName;
	QString sha256;
	qint64 assetSize = 0;
};

QString updatePlatformKey();
QString updatePackageName(const QString& platform = updatePlatformKey());
QString updateManifestAssetName();
bool isExpectedReleaseAssetUrl(const QString& tag, const QString& name, const QUrl& url);

std::optional<StableRelease> parseLatestStableRelease(const QByteArray& json,
	QString* error = nullptr);
std::optional<UpdateManifest> parseUpdateManifest(const QByteArray& json,
	const QString& expectedTag, const QString& platform = updatePlatformKey(),
	QString* error = nullptr);
bool isUpdateAvailable(const SemanticVersion& current, const StableRelease& release);
QString formatSemanticVersion(const SemanticVersion& version);

#endif
