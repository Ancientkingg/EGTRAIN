#include "update/ReleaseInfo.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSysInfo>
#include <QStringList>
#include <cmath>

namespace {

void setError(QString* error, const QString& message) {
	if (error)
		*error = message;
}

QString releaseNotes(const QJsonObject& object) {
	QString notes = object.value(QStringLiteral("body")).toString().simplified();
	if (notes.size() > 360)
		notes = notes.left(357) + QStringLiteral("...");
	return notes;
}

QString expectedAssetName(const QString& platform) {
	if (platform == QStringLiteral("macos-arm64"))
		return QStringLiteral("QEGTRAIN-macos-arm64.zip");
	if (platform == QStringLiteral("windows-x64"))
		return QStringLiteral("QEGTRAIN-windows-x64.zip");
	if (platform == QStringLiteral("linux-x86_64"))
		return QStringLiteral("QEGTRAIN-linux-x86_64.AppImage");
	return {};
}

bool validHttpsGitHubUrl(const QUrl& url) {
	return url.isValid() && url.scheme() == QStringLiteral("https")
		&& url.host().compare(QStringLiteral("github.com"), Qt::CaseInsensitive) == 0
		&& url.userInfo().isEmpty() && url.port(-1) == -1
		&& url.query().isEmpty() && url.fragment().isEmpty();
}

void setManifestError(QString* error, const QString& message) {
	if (error)
		*error = message;
}

} // namespace

const ReleaseAsset* StableRelease::asset(const QString& name) const {
	for (const ReleaseAsset& candidate : assets)
		if (candidate.name == name)
			return &candidate;
	return nullptr;
}

QString updatePlatformKey() {
#if defined(Q_OS_MACOS)
	const QString architecture = QSysInfo::currentCpuArchitecture().toLower();
	return architecture == QStringLiteral("arm64") || architecture == QStringLiteral("aarch64")
		? QStringLiteral("macos-arm64") : QString();
#elif defined(Q_OS_WIN)
	const QString architecture = QSysInfo::currentCpuArchitecture().toLower();
	return architecture == QStringLiteral("x86_64") || architecture == QStringLiteral("amd64")
		? QStringLiteral("windows-x64") : QString();
#elif defined(Q_OS_LINUX)
	const QString architecture = QSysInfo::currentCpuArchitecture().toLower();
	return architecture == QStringLiteral("x86_64") || architecture == QStringLiteral("amd64")
		? QStringLiteral("linux-x86_64") : QString();
#else
	return {};
#endif
}

QString updatePackageName(const QString& platform) {
	return expectedAssetName(platform);
}

QString updateManifestAssetName() {
	return QStringLiteral("update-manifest.json");
}

bool isExpectedReleaseAssetUrl(const QString& tag, const QString& name, const QUrl& url) {
	return validHttpsGitHubUrl(url)
		&& url.path() == QStringLiteral("/Ancientkingg/EGTRAIN/releases/download/%1/%2")
			.arg(tag, name);
}

std::optional<StableRelease> parseLatestStableRelease(const QByteArray& json, QString* error) {
	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
	if (parseError.error != QJsonParseError::NoError) {
		setError(error, QStringLiteral("Invalid GitHub release JSON: %1").arg(parseError.errorString()));
		return std::nullopt;
	}

	if (!document.isArray()) {
		setError(error, QStringLiteral("GitHub returned an invalid release list."));
		return std::nullopt;
	}
	const QJsonArray releases = document.array();
	if (releases.isEmpty()) {
		if (error && error->isEmpty())
			*error = QStringLiteral("GitHub returned no releases.");
		return std::nullopt;
	}

	std::optional<StableRelease> latest;
	for (const QJsonValue& value : releases) {
		if (!value.isObject())
			continue;
		const QJsonObject object = value.toObject();
		if (!object.value(QStringLiteral("draft")).isBool()
			|| !object.value(QStringLiteral("prerelease")).isBool())
			continue;
		if (object.value(QStringLiteral("draft")).toBool()
			|| object.value(QStringLiteral("prerelease")).toBool())
			continue;
		const QString tag = object.value(QStringLiteral("tag_name")).toString();
		const std::optional<SemanticVersion> version = parseStableTag(tag.toStdString());
		if (!version || (latest && !((*latest).version < *version)))
			continue;

		QUrl releasePage(object.value(QStringLiteral("html_url")).toString());
		const QUrl expectedPage(QStringLiteral("https://github.com/Ancientkingg/EGTRAIN/releases/tag/") + tag);
		if (!validHttpsGitHubUrl(releasePage) || releasePage.path() != expectedPage.path())
			releasePage = expectedPage;

		QList<ReleaseAsset> assets;
		const QJsonValue rawAssets = object.value(QStringLiteral("assets"));
		if (rawAssets.isArray()) {
			for (const QJsonValue& rawAsset : rawAssets.toArray()) {
				if (!rawAsset.isObject())
					continue;
				const QJsonObject asset = rawAsset.toObject();
				const QString name = asset.value(QStringLiteral("name")).toString();
				const QStringList allowedNames = {
					updateManifestAssetName(),
					updatePackageName(QStringLiteral("macos-arm64")),
					updatePackageName(QStringLiteral("windows-x64")),
					updatePackageName(QStringLiteral("linux-x86_64"))};
				if (!allowedNames.contains(name))
					continue;
				const QUrl downloadUrl(asset.value(QStringLiteral("browser_download_url")).toString());
				if (isExpectedReleaseAssetUrl(tag, name, downloadUrl))
					assets.append({name, downloadUrl});
			}
		}
		latest = StableRelease{*version, tag, releaseNotes(object), releasePage, assets};
	}

	if (!latest)
		setError(error, QStringLiteral("No stable vX.Y.Z GitHub release was found."));
	return latest;
}

std::optional<UpdateManifest> parseUpdateManifest(const QByteArray& json,
	const QString& expectedTag, const QString& platform, QString* error) {
	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
		setManifestError(error, QStringLiteral("Invalid update manifest JSON."));
		return std::nullopt;
	}
	if (!parseStableTag(expectedTag.toStdString())) {
		setManifestError(error, QStringLiteral("Update manifest release tag is invalid."));
		return std::nullopt;
	}
	const QJsonObject root = document.object();
	const QString expectedVersion = expectedTag.mid(1);
	if (root.value(QStringLiteral("version")).toString() != expectedVersion
		|| !parseStableVersion(root.value(QStringLiteral("version")).toString().toStdString())) {
		setManifestError(error, QStringLiteral("Update manifest version does not match the release tag."));
		return std::nullopt;
	}
	const QJsonObject assets = root.value(QStringLiteral("assets")).toObject();
	const QJsonObject entry = assets.value(platform).toObject();
	const QString expectedName = expectedAssetName(platform);
	const QString asset = entry.value(QStringLiteral("name")).toString();
	const QString sha256 = entry.value(QStringLiteral("sha256")).toString();
	const double rawSize = entry.value(QStringLiteral("size")).toDouble(-1.0);
	if (expectedName.isEmpty() || asset != expectedName) {
		setManifestError(error, QStringLiteral("Update manifest has no valid package for this platform."));
		return std::nullopt;
	}
	static const QRegularExpression hashPattern(QStringLiteral("^[0-9a-f]{64}$"));
	if (!hashPattern.match(sha256).hasMatch()) {
		setManifestError(error, QStringLiteral("Update manifest has an invalid SHA-256."));
		return std::nullopt;
	}
	constexpr qint64 kMaxPackageBytes = 2LL * 1024 * 1024 * 1024;
	if (!std::isfinite(rawSize) || rawSize < 1.0 || rawSize > kMaxPackageBytes
		|| std::floor(rawSize) != rawSize) {
		setManifestError(error, QStringLiteral("Update manifest has an invalid package size."));
		return std::nullopt;
	}
	return UpdateManifest{expectedVersion, platform, asset, sha256, static_cast<qint64>(rawSize)};
}

bool isUpdateAvailable(const SemanticVersion& current, const StableRelease& release) {
	return current < release.version;
}

QString formatSemanticVersion(const SemanticVersion& version) {
	return QStringLiteral("%1.%2.%3")
		.arg(version.major).arg(version.minor).arg(version.patch);
}
