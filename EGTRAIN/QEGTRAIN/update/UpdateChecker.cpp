#include "update/UpdateChecker.h"

#include <QCoreApplication>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace {
constexpr int kUpdateCheckTimeoutMs = 10000;
}

UpdateChecker::UpdateChecker(QObject* parent)
	: QObject(parent), m_network(this), m_timeout(this) {
	m_timeout.setSingleShot(true);
	connect(&m_timeout, &QTimer::timeout, this, [this]() {
		QNetworkReply* reply = m_reply;
		if (!reply)
			return;
		m_reply = nullptr;
		reply->abort();
		reply->deleteLater();
		emit finished(UpdateCheckResult{false, std::nullopt,
			QStringLiteral("The update check timed out.")});
	});
}

QUrl UpdateChecker::releasesUrl() {
	// Keep startup traffic bounded to one request; paginate if stable tags can fall behind 100 prereleases.
	return QUrl(QStringLiteral("https://api.github.com/repos/Ancientkingg/EGTRAIN/releases?per_page=100"));
}

void UpdateChecker::check() {
	if (m_reply) {
		QNetworkReply* previous = m_reply;
		m_reply = nullptr;
		m_timeout.stop();
		previous->abort();
		previous->deleteLater();
	}

	QNetworkRequest request(releasesUrl());
	request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("EGTRAIN/%1")
		.arg(QCoreApplication::applicationVersion()));
	request.setRawHeader("Accept", "application/vnd.github+json");
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	QNetworkReply* reply = m_network.get(request);
	m_reply = reply;
	m_timeout.start(kUpdateCheckTimeoutMs);
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		if (reply != m_reply)
			return;
		m_reply = nullptr;
		m_timeout.stop();

		if (reply->error() != QNetworkReply::NoError) {
			const QString error = reply->errorString();
			reply->deleteLater();
			emit finished(UpdateCheckResult{false, std::nullopt,
				error.isEmpty() ? QStringLiteral("GitHub update check failed.") : error});
			return;
		}
		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (status < 200 || status >= 300) {
			reply->deleteLater();
			emit finished(UpdateCheckResult{false, std::nullopt,
				QStringLiteral("GitHub returned HTTP %1.").arg(status)});
			return;
		}

		QString parseError;
		const std::optional<StableRelease> release = parseLatestStableRelease(reply->readAll(), &parseError);
		reply->deleteLater();
		if (!release) {
			emit finished(UpdateCheckResult{false, std::nullopt,
				parseError.isEmpty() ? QStringLiteral("No stable release was found.") : parseError});
			return;
		}
		emit finished(UpdateCheckResult{true, release, QString()});
	});
}
