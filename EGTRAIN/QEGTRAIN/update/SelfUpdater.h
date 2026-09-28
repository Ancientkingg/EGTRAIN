#ifndef EGTRAIN_SELF_UPDATER_H
#define EGTRAIN_SELF_UPDATER_H

#include "update/ReleaseInfo.h"
#include "update/UpdatePreparation.h"

#include <QByteArray>
#include <QFile>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QObject>

class QNetworkReply;

struct SelfUpdateCapability {
	bool supported = false;
	QString reason;
	QString currentPath;
	QString launchPath;
	QString helperPath;
};

class SelfUpdater : public QObject {
	Q_OBJECT

public:
	explicit SelfUpdater(QObject* parent = nullptr);
	~SelfUpdater() override;

	SelfUpdateCapability capability() const;
	bool canSelfUpdate(const StableRelease& release) const;
	bool isBusy() const { return m_busy; }
	bool isPreparing() const { return m_preparationThread != nullptr; }
	void start(const StableRelease& release);
	void cancel();
	bool restart();

signals:
	void progress(qint64 received, qint64 total);
	void preparing(const QString& version);
	void finished(bool success, const QString& error);

private slots:
	void handlePreparationFinished(const UpdatePreparationResult& result);

private:
	void requestManifest();
	void requestPackage(const UpdateManifest& manifest);
	void handleReply(QNetworkReply* reply, bool manifestReply);
	void fail(const QString& error);
	void startPreparation();
	void clearStaging();

	QNetworkAccessManager m_network;
	QPointer<QNetworkReply> m_reply;
	QTimer m_timeout;
	QFile m_packageFile;
	QByteArray m_manifestData;
	StableRelease m_release;
	UpdateManifest m_manifest;
	QString m_stagingRoot;
	QString m_stagedPath;
	QString m_currentPath;
	QString m_launchPath;
	QString m_helperPath;
	QThread* m_preparationThread = nullptr;
	qint64 m_packageBytes = 0;
	bool m_busy = false;
	bool m_cancelRequested = false;
	bool m_ready = false;
};

#endif
