#include "update/UpdatePreparation.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <chrono>
#include <functional>
#include <iostream>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

static bool writeFile(const QString& path, const QByteArray& contents) {
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

struct PreparationOutcome {
	UpdatePreparationResult result;
	QThread* stagerThread = nullptr;
	QThread* deliveryThread = nullptr;
	bool stagerRan = false;
	int heartbeatTicks = 0;
};

static PreparationOutcome runPreparation(const UpdatePreparationInput& input,
	const std::function<QString(const UpdatePreparationInput&, QString*)>& stager) {
	PreparationOutcome outcome;
	QEventLoop loop;
	auto* thread = new QThread;
	auto* worker = new UpdatePreparationWorker;
	worker->setPlatformStager([&outcome, stager](const UpdatePreparationInput& stagerInput,
								  QString* error) {
		outcome.stagerRan = true;
		outcome.stagerThread = QThread::currentThread();
		QThread::msleep(150);
		return stager ? stager(stagerInput, error) : QString();
	});
	worker->moveToThread(thread);
	QObject::connect(thread, &QThread::started, worker,
		[worker, input]() { worker->prepare(input); });
	QObject::connect(worker, &UpdatePreparationWorker::finished, &loop,
		[&outcome, &loop](const UpdatePreparationResult& result) {
			outcome.result = result;
			outcome.deliveryThread = QThread::currentThread();
			loop.quit();
		});
	QObject::connect(thread, &QThread::finished, worker, &QObject::deleteLater);

	QTimer heartbeat;
	heartbeat.setInterval(10);
	QObject::connect(&heartbeat, &QTimer::timeout, &heartbeat,
		[&outcome]() { ++outcome.heartbeatTicks; });
	QTimer watchdog;
	watchdog.setSingleShot(true);
	QObject::connect(&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);

	heartbeat.start();
	watchdog.start(15000);
	thread->start();
	loop.exec();
	heartbeat.stop();
	thread->quit();
	if (!thread->wait(5000))
		std::cerr << "failed: preparation thread stopped\n";
	delete thread;
	return outcome;
}

static bool setModificationTime(const QString& path, const QDateTime& time) {
	QFile file(path);
	return file.open(QIODevice::ReadWrite | QIODevice::ExistingOnly) && file.setFileTime(time, QFileDevice::FileModificationTime);
}

static bool testStaleStagingSweep(const QString& base) {
	const QDir parent(QDir(base).filePath("sweep"));
	const QString stale = parent.filePath(".qegtrain-update-stale");
	const QString empty = parent.filePath(".qegtrain-update-empty");
	const QString other = parent.filePath("other");
	const QString file = parent.filePath(".qegtrain-update-file");
	const QString target = parent.filePath("target");
	const QString link = parent.filePath(".qegtrain-update-link");
	bool ok = expect(QDir().mkpath(stale + "/extract") && QDir().mkpath(empty) && QDir().mkpath(other)
			&& QDir().mkpath(target) && writeFile(stale + "/package.zip", "p") && writeFile(stale + "/extract/file", "f")
			&& writeFile(other + "/file", "o") && writeFile(file, "f") && writeFile(target + "/keep", "k")
			&& QFile::link(target, link),
		"sweep fixtures are writable");

	removeStaleUpdateStaging(parent.path(), std::chrono::minutes(60));
	ok &= expect(QFileInfo::exists(stale) && QFileInfo::exists(empty),
		"a staging folder that changed within the minimum age is kept");

	QThread::msleep(20);
	removeStaleUpdateStaging(parent.path(), std::chrono::minutes(0));
	ok &= expect(!QFileInfo::exists(stale), "an old staging folder is removed with its content");
	ok &= expect(!QFileInfo::exists(empty), "an old empty staging folder is removed");
	ok &= expect(QFileInfo::exists(other + "/file"), "a folder with another name is kept");
	ok &= expect(QFileInfo::exists(file), "a file with the name of a staging folder is kept");
	ok &= expect(QFileInfo::exists(target + "/keep"), "the target of a symbolic link is kept");
// QFile::link makes a shortcut file on Windows, so a symbolic link is only covered elsewhere.
#if !defined(Q_OS_WIN)
	ok &= expect(QFileInfo(link).isSymLink(), "a symbolic link with the name of a staging folder is kept");
#endif

	// The folder time is now. Only the age of the entries counts.
	const QString oldEntries = parent.filePath(".qegtrain-update-old-entries");
	const QString recentInside = parent.filePath(".qegtrain-update-recent-inside");
	const QDateTime twoHoursAgo = QDateTime::currentDateTime().addSecs(-2 * 3600);
	ok &= expect(QDir().mkpath(oldEntries) && writeFile(oldEntries + "/package.zip", "p")
			&& QDir().mkpath(recentInside + "/extract") && writeFile(recentInside + "/package.zip", "p")
			&& writeFile(recentInside + "/extract/file", "f"),
		"age fixtures are writable");
	ok &= expect(setModificationTime(oldEntries + "/package.zip", twoHoursAgo) && setModificationTime(recentInside + "/package.zip", twoHoursAgo),
		"age fixtures can be dated");
	removeStaleUpdateStaging(parent.path(), std::chrono::minutes(60));
	ok &= expect(!QFileInfo::exists(oldEntries), "a folder whose entries are old is removed although the folder itself changed");
	ok &= expect(QFileInfo::exists(recentInside), "a recent entry in a subfolder keeps the folder");

	removeStaleUpdateStaging(QDir(base).filePath("missing"), std::chrono::minutes(0));
	ok &= expect(!QFileInfo::exists(QDir(base).filePath("missing")), "a missing parent is left alone");
	return ok;
}

int main(int argc, char** argv) {
	QCoreApplication application(argc, argv);
	qRegisterMetaType<UpdatePreparationInput>("UpdatePreparationInput");
	qRegisterMetaType<UpdatePreparationResult>("UpdatePreparationResult");
	QTemporaryDir temp;
	if (!temp.isValid())
		return 1;

	bool ok = true;
#if defined(Q_OS_LINUX)
	UpdatePreparationInput linuxInput;
	linuxInput.stagingRoot = temp.path();
	linuxInput.packagePath = QDir(temp.path()).filePath("QEGTRAIN-linux-x86_64.AppImage");
	linuxInput.currentPath = QDir(temp.path()).filePath("current.AppImage");
	const QByteArray elfContents = QByteArray::fromHex("7f454c46") + "test AppImage";
	ok &= expect(writeFile(linuxInput.packagePath, elfContents)
			&& writeFile(linuxInput.currentPath, elfContents),
		"AppImage fixtures are writable");
	QString stageError;
	const QString linuxStage = stageUpdatePackage(linuxInput, &stageError);
	ok &= expect(!linuxStage.isEmpty() && stageError.isEmpty(),
		"real Linux preparation accepts the production download path");
	ok &= expect(linuxStage != linuxInput.packagePath && QFile::exists(linuxInput.packagePath),
		"AppImage preparation keeps download and staged installation separate");
	ok &= expect(QFileInfo(linuxStage).isExecutable(), "staged AppImage is executable");
#endif
	QByteArray packageContents(3 * 1024 * 1024, 'p');
	for (int offset = 0; offset < packageContents.size(); offset += 4096)
		packageContents[offset] = static_cast<char>(offset % 251);
	const QString packagePath = QDir(temp.path()).filePath(QStringLiteral("package.zip"));
	ok &= expect(writeFile(packagePath, packageContents), "package fixture is writable");
	const QString expectedSha = QString::fromLatin1(
		QCryptographicHash::hash(packageContents, QCryptographicHash::Sha256).toHex());
	const QThread* mainThread = QCoreApplication::instance()->thread();

	const auto baseInput = [&](const QString& sha256) {
		UpdatePreparationInput input;
		input.packagePath = packagePath;
		input.stagingRoot = QDir(temp.path()).filePath(QStringLiteral("staging"));
		input.currentPath = QDir(temp.path()).filePath(QStringLiteral("current"));
		input.manifest.version = QStringLiteral("1.2.3");
		input.manifest.assetName = QStringLiteral("package.zip");
		input.manifest.sha256 = sha256;
		input.manifest.assetSize = packageContents.size();
		return input;
	};

	PreparationOutcome failedHash = runPreparation(
		baseInput(QString(64, '0')), [](const UpdatePreparationInput&, QString*) {
			return QStringLiteral("should-not-stage");
		});
	ok &= expect(!failedHash.result.success && !failedHash.stagerRan,
		"a failed hash never proceeds to staging");
	ok &= expect(failedHash.result.error
			== QStringLiteral("The downloaded update failed its SHA-256 check."),
		"hash failure reports the SHA-256 mismatch");
	ok &= expect(failedHash.result.stagedPath.isEmpty(),
		"a failed hash publishes no staged path");

	const QString stagedPath = QDir(temp.path()).filePath(QStringLiteral("staged"));
	PreparationOutcome prepared = runPreparation(baseInput(expectedSha),
		[stagedPath](const UpdatePreparationInput&, QString*) { return stagedPath; });
	ok &= expect(prepared.result.success && prepared.result.error.isEmpty(),
		"successful preparation completes without an error");
	ok &= expect(prepared.result.stagedPath == stagedPath,
		"successful preparation publishes only the resulting staged path");
	ok &= expect(prepared.stagerRan && prepared.stagerThread
			&& prepared.stagerThread != mainThread,
		"preparation work runs off the application thread");
	ok &= expect(prepared.deliveryThread == mainThread,
		"preparation completion is delivered on the application thread");
	ok &= expect(prepared.heartbeatTicks >= 4,
		"the event loop keeps processing events while preparation runs");

	ok &= testStaleStagingSweep(temp.path());

	return ok ? 0 : 1;
}
