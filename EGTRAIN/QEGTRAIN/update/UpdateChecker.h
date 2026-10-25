#ifndef EGTRAIN_UPDATE_CHECKER_H
#define EGTRAIN_UPDATE_CHECKER_H

#include "update/HttpTransport.h"
#include "update/ReleaseInfo.h"

#include <QPointer>
#include <QTimer>
#include <QObject>

/// The outcome of one update check.
struct UpdateCheckResult {
	bool success = false;				  ///< True when a stable release was found.
	std::optional<StableRelease> release; ///< The newest stable release. Set only on success.
	QString error;						  ///< Why the check failed, for the user. Empty on success.
};

/// Asks GitHub for the newest stable release.
class UpdateChecker : public QObject {
	Q_OBJECT

public:
	/// Uses the transport of the application and a timeout of 10 seconds.
	explicit UpdateChecker(QObject* parent = nullptr);
	/// For tests: uses a transport that the caller owns and a timeout of timeoutMs milliseconds.
	UpdateChecker(HttpTransport* transport, int timeoutMs, QObject* parent = nullptr);

	/// Fetches releasesUrl() and emits finished() once, also when the request fails or times out or the release
	/// list is larger than 8 MiB. A check that is still running is cancelled without a result.
	void check();
	/// True from check() until finished() is emitted.
	bool isChecking() const { return !m_reply.isNull(); }
	/// The release list of the GitHub API, at most 100 releases.
	static QUrl releasesUrl();

signals:
	/// The check started by check() ended.
	void finished(const UpdateCheckResult& result);

private:
	void connectTimeout();

	HttpTransport* m_transport;
	int m_timeoutMs;
	QPointer<HttpReply> m_reply;
	QByteArray m_body;
	QTimer m_timeout;
};

#endif
