#ifndef EGTRAIN_UPDATE_PREPARATION_H
#define EGTRAIN_UPDATE_PREPARATION_H

#include "update/ReleaseInfo.h"

#include <QMetaType>
#include <QObject>
#include <QString>
#include <chrono>
#include <functional>

/// Inputs for preparing a downloaded update package. Everything is copied so
/// the preparation worker never touches SelfUpdater or GUI-thread state.
struct UpdatePreparationInput {
	QString packagePath; ///< The downloaded package file.
	QString stagingRoot; ///< The staging folder that holds the package, where it is unpacked.
	/// The installation that the update replaces (SelfUpdateCapability::currentPath). On Linux the staged
	/// AppImage gets its permissions.
	QString currentPath;
	/// True when the package is the installer of an installed Windows copy, which runs from where it
	/// was downloaded. Nothing is extracted and the staged path is the package.
	bool installer = false;
	UpdateManifest manifest; ///< The manifest entry that the package is checked against.
};

/// The outcome of preparing a package.
struct UpdatePreparationResult {
	bool success = false; ///< True when the package was verified and staged.
	QString error;		  ///< Why the preparation failed, for the user. Empty on success.
	QString stagedPath;	  ///< What the update helper installs. Empty on failure.
};

Q_DECLARE_METATYPE(UpdatePreparationInput)
Q_DECLARE_METATYPE(UpdatePreparationResult)

/// Verifies the downloaded package against the manifest SHA-256 using chunked
/// reads. Returns false, and sets error when it is given, when the file cannot be read or does not match.
bool verifyDownloadedPackageHash(const QString& packagePath,
	const QString& expectedSha256, QString* error = nullptr);

/// Stages a package after download verification has succeeded, and returns the staged path: the
/// QEGTRAIN.app bundle on macOS, the QEGTRAIN folder on Windows and ready.AppImage on Linux, in
/// stagingRoot, or the package itself for an installer. Returns an empty path, and sets error when it
/// is given, on failure.
QString stageUpdatePackage(const UpdatePreparationInput& input, QString* error = nullptr);

/// The text shown when the downloaded package could not be unpacked: what failed, that the
/// installation is unchanged and how to try again, followed by the technical detail when there is one.
QString unpackFailureMessage(const QString& detail);

/// True when a file can be created in directory and removed again. It tries the folder because Qt
/// reads no NTFS access list unless it is told to, so QFileInfo::isWritable can say yes for a folder
/// that the user cannot write, such as one below Program Files.
bool canCreateFilesIn(const QString& directory);

/// True when path is on a volume that is mounted read-only or inside an App Translocation
/// location. An app that runs from a disk image or straight after a download is there.
bool isReadOnlyLocation(const QString& path);

/// Removes the staging folders ".qegtrain-update-*" in installationParent that an update left
/// behind: directories that are not symbolic links and in which no entry changed within
/// minimumAge. Failures are ignored.
void removeStaleUpdateStaging(const QString& installationParent, std::chrono::minutes minimumAge);

/// Verifies and stages a downloaded package. SelfUpdater runs it in a thread of its own.
class UpdatePreparationWorker : public QObject {
	Q_OBJECT

public:
	/// Platform-specific structural preparation of the staged installation.
	/// Defaults to the real host implementation; injectable so the asynchronous
	/// handoff can be exercised on any platform in tests.
	using PlatformStager = std::function<QString(
		const UpdatePreparationInput&, QString*)>;
	/// Stages with stager in place of stageUpdatePackage(). An empty stager restores the default.
	void setPlatformStager(PlatformStager stager);

public slots:
	/// Runs in the worker thread. Emits exactly one finished() result and must
	/// not mutate any object owned by another thread.
	void prepare(const UpdatePreparationInput& input);

signals:
	/// The preparation started by prepare() ended.
	void finished(const UpdatePreparationResult& result);

private:
	PlatformStager m_platformStager;
};

#endif
