#ifndef EGTRAIN_SELF_UPDATER_H
#define EGTRAIN_SELF_UPDATER_H

#include "update/HttpTransport.h"
#include "update/ReleaseInfo.h"
#include "update/UpdatePreparation.h"

#include <QByteArray>
#include <QFile>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QObject>

struct SelfUpdateCapability {
	bool supported = false;
	QString reason;
	// The entry of update-manifest.json that describes the package of this copy.
	QString manifestKey;
	// True when the update runs the installer of the release instead of replacing the files of the copy.
	bool usesInstaller = false;
	QString currentPath;
	QString launchPath;
	QString helperPath;
};

class SelfUpdater : public QObject {
	Q_OBJECT

public:
	explicit SelfUpdater(QObject* parent = nullptr);
	~SelfUpdater() override;

	static SelfUpdateCapability capability();
	// Removes staging folders that an earlier update left next to the installation.
	static void cleanupStaleStaging();
	bool canSelfUpdate(const StableRelease& release) const;
	bool isBusy() const { return m_busy; }
	bool isPreparing() const { return m_preparationThread != nullptr; }
	void start(const StableRelease& release);
	void cancel();
	bool restart();

#ifdef EGTRAIN_UPDATE_TEST_HOOK
	// Makes capability() return the replacement and accepts the package of a release only at
	// releaseBase/<tag>/<name>, so that a test can update from a server on the loopback address into
	// a folder of its own. An empty releaseBase ends the replacement.
	static void setTestHook(const SelfUpdateCapability& replacement, const QString& releaseBase);
#endif

signals:
	void progress(qint64 received, qint64 total);
	void preparing(const QString& version);
	void finished(bool success, const QString& error);

private slots:
	void handlePreparationFinished(const UpdatePreparationResult& result);

private:
	void requestManifest();
	void requestPackage(const UpdateManifest& manifest);
	void handleReply(HttpReply* reply, bool manifestReply);
	void fail(const QString& error);
	void startPreparation();
	void clearStaging();
	bool startHelper();

	HttpTransport* m_transport;
	QPointer<HttpReply> m_reply;
	QTimer m_timeout;
	QFile m_packageFile;
	QByteArray m_manifestData;
	StableRelease m_release;
	UpdateManifest m_manifest;
	QString m_manifestKey;
	QString m_stagingRoot;
	QString m_stagedPath;
	QString m_currentPath;
	QString m_launchPath;
	QString m_helperPath;
	QThread* m_preparationThread = nullptr;
	qint64 m_packageBytes = 0;
	bool m_usesInstaller = false;
	bool m_busy = false;
	bool m_cancelRequested = false;
	bool m_ready = false;
};

#endif
