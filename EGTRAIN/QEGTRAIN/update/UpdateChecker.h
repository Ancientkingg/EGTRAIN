#ifndef EGTRAIN_UPDATE_CHECKER_H
#define EGTRAIN_UPDATE_CHECKER_H

#include "update/HttpTransport.h"
#include "update/ReleaseInfo.h"

#include <QPointer>
#include <QTimer>
#include <QObject>

struct UpdateCheckResult {
	bool success = false;
	std::optional<StableRelease> release;
	QString error;
};

class UpdateChecker : public QObject {
	Q_OBJECT

public:
	explicit UpdateChecker(QObject* parent = nullptr);
	// For tests: uses a transport that the caller owns and the given timeout.
	UpdateChecker(HttpTransport* transport, int timeoutMs, QObject* parent = nullptr);

	void check();
	bool isChecking() const { return !m_reply.isNull(); }
	static QUrl releasesUrl();

signals:
	void finished(const UpdateCheckResult& result);

private:
	void connectTimeout();

	HttpTransport* m_transport;
	int m_timeoutMs;
	QPointer<HttpReply> m_reply;
	QTimer m_timeout;
};

#endif
