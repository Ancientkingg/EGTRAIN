#ifndef EGTRAIN_UPDATE_PREPARATION_H
#define EGTRAIN_UPDATE_PREPARATION_H

#include "update/ReleaseInfo.h"

#include <QMetaType>
#include <QObject>
#include <QString>
#include <functional>

// Inputs for preparing a downloaded update package. Everything is copied so
// the preparation worker never touches SelfUpdater or GUI-thread state.
struct UpdatePreparationInput {
	QString packagePath;
	QString stagingRoot;
	QString currentPath;
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
	void prepare(UpdatePreparationInput input);

signals:
	void finished(const UpdatePreparationResult& result);

private:
	PlatformStager m_platformStager;
};

#endif
