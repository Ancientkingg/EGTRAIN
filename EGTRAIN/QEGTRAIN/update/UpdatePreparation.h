#ifndef EGTRAIN_UPDATE_PREPARATION_H
#define EGTRAIN_UPDATE_PREPARATION_H

#include "update/ReleaseInfo.h"

#include <QMetaType>
#include <QObject>
#include <QString>
#include <chrono>
#include <functional>

// Inputs for preparing a downloaded update package. Everything is copied so
// the preparation worker never touches SelfUpdater or GUI-thread state.
struct UpdatePreparationInput {
	QString packagePath;
	QString stagingRoot;
	QString currentPath;
	// True when the package is the installer of an installed Windows copy, which runs from where it
	// was downloaded. Nothing is extracted and the staged path is the package.
	bool installer = false;
	UpdateManifest manifest;
};

struct UpdatePreparationResult {
	bool success = false;
	QString error;
	QString stagedPath;
};

Q_DECLARE_METATYPE(UpdatePreparationInput)
Q_DECLARE_METATYPE(UpdatePreparationResult)

// Verifies the downloaded package against the manifest SHA-256 using chunked
// reads. A failed hash must never proceed to staging.
bool verifyDownloadedPackageHash(const QString& packagePath,
	const QString& expectedSha256, QString* error = nullptr);

// Stage a package after download verification has succeeded.
QString stageUpdatePackage(const UpdatePreparationInput& input, QString* error = nullptr);

// The text shown when the downloaded package could not be unpacked: what failed, that the
// installation is unchanged and how to try again, followed by the technical detail when there is one.
QString unpackFailureMessage(const QString& detail);

// True when a file can be created in directory and removed again. It tries the folder because Qt
// reads no NTFS access list unless it is told to, so QFileInfo::isWritable can say yes for a folder
// that the user cannot write, such as one below Program Files.
bool canCreateFilesIn(const QString& directory);

// True when path is on a volume that is mounted read-only or inside an App Translocation
// location. An app that runs from a disk image or straight after a download is there.
bool isReadOnlyLocation(const QString& path);

// Removes the staging folders ".qegtrain-update-*" in installationParent that an update left
// behind: directories that are not symbolic links and in which no entry changed within
// minimumAge. Failures are ignored.
void removeStaleUpdateStaging(const QString& installationParent, std::chrono::minutes minimumAge);

class UpdatePreparationWorker : public QObject {
	Q_OBJECT

public:
	// Platform-specific structural preparation of the staged installation.
	// Defaults to the real host implementation; injectable so the asynchronous
	// handoff can be exercised on any platform in tests.
	using PlatformStager = std::function<QString(
		const UpdatePreparationInput&, QString*)>;
	void setPlatformStager(PlatformStager stager);

	// Runs in the worker thread. Emits exactly one finished() result and must
	// not mutate any object owned by another thread.
public slots:
	void prepare(const UpdatePreparationInput& input);

signals:
	void finished(const UpdatePreparationResult& result);

private:
	PlatformStager m_platformStager;
};

#endif
