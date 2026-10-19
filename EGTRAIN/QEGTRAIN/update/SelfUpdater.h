#ifndef EGTRAIN_SELF_UPDATER_H
#define EGTRAIN_SELF_UPDATER_H

#include "update/Distribution.h"
#include "update/HttpTransport.h"
#include "update/ReleaseInfo.h"
#include "update/UpdatePreparation.h"

#include <QByteArray>
#include <QFile>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QObject>

/// Whether the running copy can update itself, and the paths an update uses.
struct SelfUpdateCapability {
	bool supported = false; ///< True when the running copy can update itself.
	QString reason;			///< Why the copy cannot update itself, for the user. Empty when supported.
	/// The entry of update-manifest.json that describes the package of this copy.
	QString manifestKey;
	/// True when the update runs the installer of the release instead of replacing the files of the copy.
	bool usesInstaller = false;
	/// What an update replaces: the QEGTRAIN.app bundle on macOS, the folder of QEGTRAIN.exe on Windows and
	/// the AppImage file on Linux. The staging folders are made in its parent folder.
	QString currentPath;
	QString launchPath; ///< The program that the update helper starts after the update.
	QString helperPath; ///< The update helper next to the running executable.
};

/// The package that an update of a kind of copy downloads.
struct UpdatePackage {
	/// The entry of update-manifest.json that describes the package.
	QString manifestKey;
	/// True when the update runs the installer of the release instead of replacing the files of the copy.
	bool usesInstaller = false;
};

/// An installed Windows copy runs the installer of the release. Every other copy replaces its own files
/// with the package of its platform, whose manifest entry is platformKey.
UpdatePackage updatePackageFor(Distribution distribution, const QString& platformKey);

/// Downloads the update manifest and the package of a release, prepares the package on a worker thread
/// and starts the update helper that installs it.
class SelfUpdater : public QObject {
	Q_OBJECT

public:
	/// Makes an updater that uses the transport of the application.
	explicit SelfUpdater(QObject* parent = nullptr);
	/// Waits for a preparation that is running to end.
	~SelfUpdater() override;

	/// Checks whether this is a packaged release build that runs from a complete, writable installation
	/// with its update helper.
	static SelfUpdateCapability capability();
	/// Removes staging folders that an earlier update left next to the installation.
	static void cleanupStaleStaging();
	/// True when this copy can update itself and the release lists the manifest and a package for this copy.
	bool canSelfUpdate(const StableRelease& release) const;
	/// True from start() until the download or the preparation ends.
	bool isBusy() const { return m_busy; }
	/// True while the downloaded package is checked and unpacked on the worker thread.
	bool isPreparing() const { return m_preparationThread != nullptr; }
	/// Downloads and prepares the update to release. Does nothing while busy; otherwise finished() is
	/// emitted once when the update is ready or has failed.
	void start(const StableRelease& release);
	/// Stops a download. Does nothing when no update runs or while the package is prepared.
	void cancel();
	/// Starts the update helper of an update that is ready. The helper waits for this process to end,
	/// installs the update and starts the application again. Returns false, and removes the staged
	/// update, when no update is ready or the helper could not be started.
	bool restart();

#ifdef EGTRAIN_UPDATE_TEST_HOOK
	/// Makes capability() return the replacement and accepts the package of a release only at
	/// `releaseBase/<tag>/<name>`, so that a test can update from a server on the loopback address into
	/// a folder of its own. An empty releaseBase ends the replacement.
	static void setTestHook(const SelfUpdateCapability& replacement, const QString& releaseBase);
#endif

signals:
	/// The bytes of the package received so far and the total in bytes, or -1 when the total is not known.
	void progress(qint64 received, qint64 total);
	/// The package is downloaded and its preparation for version "X.Y.Z" starts.
	void preparing(const QString& version);
	/// The update started by start() is ready for restart(), or failed with error. error is empty on success.
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
