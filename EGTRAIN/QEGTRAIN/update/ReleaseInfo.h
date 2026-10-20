#ifndef EGTRAIN_RELEASE_INFO_H
#define EGTRAIN_RELEASE_INFO_H

#include "util/Version.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
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
	QString distribution;
	QString assetName;
	QString sha256;
	qint64 assetSize = 0;
	QStringList files;
};

// Names the kind of copy that is running and with it the manifest entry that describes its package.
QString updateDistributionKey();
QString updateManifestAssetName();
// True when name is a package name that the distribution key allows for the given release version.
bool isUpdateAssetName(const QString& distribution, const QString& name, const QString& version);
// True when the release lists a package that the distribution key allows.
bool releaseHasUpdatePackage(const StableRelease& release, const QString& distribution);
bool isExpectedReleaseAssetUrl(const QString& tag, const QString& name, const QUrl& url);

std::optional<StableRelease> parseLatestStableRelease(const QByteArray& json,
	QString* error = nullptr);
std::optional<UpdateManifest> parseUpdateManifest(const QByteArray& json,
	const QString& expectedTag, const QString& distribution = updateDistributionKey(),
	QString* error = nullptr);
bool isUpdateAvailable(const SemanticVersion& current, const StableRelease& release);
QString formatSemanticVersion(const SemanticVersion& version);

#endif
