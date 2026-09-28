#ifndef EGTRAIN_UPDATE_CHECKER_H
#define EGTRAIN_UPDATE_CHECKER_H

#include "update/ReleaseInfo.h"

#include <QNetworkAccessManager>
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

	void check();
	bool isChecking() const { return !m_reply.isNull(); }
	static QUrl releasesUrl();

signals:
	void finished(const UpdateCheckResult& result);

private:
	QNetworkAccessManager m_network;
	QPointer<QNetworkReply> m_reply;
	QTimer m_timeout;
};

#endif
