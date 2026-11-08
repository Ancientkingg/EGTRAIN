#include "update/UpdateChecker.h"

#include <QCoreApplication>

#include <utility>

namespace {
constexpr int kUpdateCheckTimeoutMs = 10000;
// The feed lists every release with its notes and files. The limit leaves room for many releases with long notes.
constexpr qint64 kMaxReleaseListBytes = 8 * 1024 * 1024;

UpdateCheckResult releaseListTooLarge() {
	return UpdateCheckResult{false, std::nullopt, QStringLiteral("The release list from the EGTRAIN website is too large.")};
}
}

UpdateChecker::UpdateChecker(QObject* parent)
	: QObject(parent), m_transport(createHttpTransport(this)), m_timeoutMs(kUpdateCheckTimeoutMs), m_timeout(this) {
	connectTimeout();
}

UpdateChecker::UpdateChecker(HttpTransport* transport, int timeoutMs, QObject* parent)
	: QObject(parent), m_transport(transport), m_timeoutMs(timeoutMs), m_timeout(this) {
	connectTimeout();
}

void UpdateChecker::connectTimeout() {
	m_timeout.setSingleShot(true);
	connect(&m_timeout, &QTimer::timeout, this, [this]() {
		HttpReply* reply = m_reply;
		if (!reply)
			return;
		m_reply = nullptr;
		m_body.clear();
		reply->abort();
		reply->deleteLater();
		emit finished(UpdateCheckResult{false, std::nullopt,
			QStringLiteral("The update check timed out.")});
	});
}

QUrl UpdateChecker::releasesUrl() {
	return QUrl(QStringLiteral("https://egtrain.samuelbruin.com/releases/feed.json"));
}

void UpdateChecker::check() {
	if (m_reply) {
		HttpReply* previous = m_reply;
		m_reply = nullptr;
		m_timeout.stop();
		previous->abort();
		previous->deleteLater();
	}
	m_body.clear();

	HttpRequest request;
	request.url = releasesUrl();
	request.userAgent = QStringLiteral("EGTRAIN/%1").arg(QCoreApplication::applicationVersion()).toUtf8();
	request.accept = QByteArrayLiteral("application/json");
	HttpReply* reply = m_transport->get(request);
	m_reply = reply;
	// The limit is the time without data, so a slow link that keeps sending finishes the check.
	m_timeout.start(m_timeoutMs);
	connect(reply, &HttpReply::readyRead, this, [this, reply]() {
		if (reply != m_reply)
			return;
		m_timeout.start(m_timeoutMs);
		m_body += reply->readAll();
		if (m_body.size() <= kMaxReleaseListBytes)
			return;
		m_reply = nullptr;
		m_timeout.stop();
		m_body.clear();
		reply->abort();
		reply->deleteLater();
		emit finished(releaseListTooLarge());
	});
	connect(reply, &HttpReply::finished, this, [this, reply]() {
		if (reply != m_reply)
			return;
		m_reply = nullptr;
		m_timeout.stop();
		QByteArray body = std::exchange(m_body, QByteArray());

		if (reply->failed()) {
			const QString error = reply->errorString();
			reply->deleteLater();
			emit finished(UpdateCheckResult{false, std::nullopt,
				error.isEmpty() ? QStringLiteral("The update check with the EGTRAIN website failed.") : error});
			return;
		}
		const int status = reply->statusCode();
		if (status < 200 || status >= 300) {
			reply->deleteLater();
			emit finished(UpdateCheckResult{false, std::nullopt,
				QStringLiteral("The EGTRAIN website returned HTTP %1.").arg(status)});
			return;
		}

		body += reply->readAll();
		reply->deleteLater();
		if (body.size() > kMaxReleaseListBytes) {
			emit finished(releaseListTooLarge());
			return;
		}
		QString parseError;
		const std::optional<StableRelease> release = parseLatestStableRelease(body, &parseError);
		if (!release) {
			emit finished(UpdateCheckResult{false, std::nullopt,
				parseError.isEmpty() ? QStringLiteral("No stable release was found.") : parseError});
			return;
		}
		emit finished(UpdateCheckResult{true, release, QString()});
	});
}
