#include "update/UpdatePreparation.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

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
	const QThread* mainThread = QCoreApplication::instance()->thread();
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
		[&outcome, &loop, mainThread](const UpdatePreparationResult& result) {
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

int main(int argc, char** argv) {
	QCoreApplication application(argc, argv);
	qRegisterMetaType<UpdatePreparationInput>("UpdatePreparationInput");
	qRegisterMetaType<UpdatePreparationResult>("UpdatePreparationResult");
	QTemporaryDir temp;
	if (!temp.isValid())
		return 1;

	bool ok = true;
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

	return ok ? 0 : 1;
}
