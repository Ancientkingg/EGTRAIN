#include "update/ReleaseInfo.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

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

} // namespace

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
		if (!releasePage.isValid() || releasePage.scheme() != QStringLiteral("https")
			|| releasePage.host().compare(QStringLiteral("github.com"), Qt::CaseInsensitive) != 0
			|| !releasePage.path().startsWith(QStringLiteral("/Ancientkingg/EGTRAIN/releases/tag/")))
			releasePage = QUrl(QStringLiteral("https://github.com/Ancientkingg/EGTRAIN/releases/tag/") + tag);
		latest = StableRelease{*version, tag, releaseNotes(object), releasePage};
	}

	if (!latest)
		setError(error, QStringLiteral("No stable vX.Y.Z GitHub release was found."));
	return latest;
}

bool isUpdateAvailable(const SemanticVersion& current, const StableRelease& release) {
	return current < release.version;
}

QString formatSemanticVersion(const SemanticVersion& version) {
	return QStringLiteral("%1.%2.%3")
		.arg(version.major).arg(version.minor).arg(version.patch);
}
