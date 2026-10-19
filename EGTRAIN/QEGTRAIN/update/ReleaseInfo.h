#ifndef EGTRAIN_RELEASE_INFO_H
#define EGTRAIN_RELEASE_INFO_H

#include "util/Version.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <optional>

/// A file published with a release.
struct ReleaseAsset {
	QString name;	  ///< The file name, such as "update-manifest.json".
	QUrl downloadUrl; ///< `https://github.com/Ancientkingg/EGTRAIN/releases/download/<tag>/<name>`.
};

/// The newest stable release on GitHub, as parseLatestStableRelease() reads it.
struct StableRelease {
	SemanticVersion version; ///< The version of the tag.
	QString tag;			 ///< The tag, "vX.Y.Z".
	/// The release notes with LF line ends, without trailing spaces and with at most one blank line in a
	/// row. Notes longer than 360 characters are cut and end with a line that points to the release page.
	QString notes;
	/// The page of the release on github.com. When GitHub names another page, the page of the tag.
	QUrl releasePage;
	/// The update manifest and the known package files of the release. A file at any other address is left out.
	QList<ReleaseAsset> assets;

	/// The asset with this name, or nullptr when the release has none. The pointer points into assets.
	const ReleaseAsset* asset(const QString& name) const;
};

/// The entry of update-manifest.json that describes the package of one distribution key.
struct UpdateManifest {
	QString version;	  ///< The release version, "X.Y.Z" without the v of the tag.
	QString distribution; ///< The distribution key of the entry, such as "windows-x64".
	QString assetName;	  ///< The file name of the package.
	QString sha256;		  ///< The SHA-256 of the package as 64 lowercase hexadecimal digits.
	qint64 assetSize = 0; ///< The size of the package in bytes, from 1 byte to 2 GiB.
	/// The files the package contains, as relative paths with forward slashes. Empty when the entry has
	/// no list; a list always names QEGTRAIN.exe.
	QStringList files;
};

/// The distribution key of this platform and processor: "windows-x64", "macos-arm64" or "linux-x86_64".
/// Empty on any other platform or processor. An installed Windows copy uses the installer entry
/// instead (updatePackageFor()).
QString updateDistributionKey();
/// The file name of the update manifest of a release: "update-manifest.json".
QString updateManifestAssetName();
/// True when name is a package name that the distribution key allows for the given release version,
/// "X.Y.Z". False when version is empty.
bool isUpdateAssetName(const QString& distribution, const QString& name, const QString& version);
/// True when the release lists a package that the distribution key allows.
bool releaseHasUpdatePackage(const StableRelease& release, const QString& distribution);
/// True when url is `https://github.com/Ancientkingg/EGTRAIN/releases/download/<tag>/<name>`, without
/// user, port, query or fragment.
bool isExpectedReleaseAssetUrl(const QString& tag, const QString& name, const QUrl& url);

/// Reads the release list of the GitHub API and returns the release with the highest "vX.Y.Z" tag that
/// is neither a draft nor a prerelease. Returns nothing, and sets error when it is given, when there is none.
std::optional<StableRelease> parseLatestStableRelease(const QByteArray& json,
	QString* error = nullptr);
/// Reads update-manifest.json of the release with expectedTag ("vX.Y.Z") and returns its entry for
/// distribution. Returns nothing, and sets error when it is given, when the version differs from the
/// tag or the entry has no valid package name, SHA-256, size or file list.
std::optional<UpdateManifest> parseUpdateManifest(const QByteArray& json,
	const QString& expectedTag, const QString& distribution = updateDistributionKey(),
	QString* error = nullptr);
/// True when the release is newer than current.
bool isUpdateAvailable(const SemanticVersion& current, const StableRelease& release);
/// The version as "X.Y.Z".
QString formatSemanticVersion(const SemanticVersion& version);

#endif
