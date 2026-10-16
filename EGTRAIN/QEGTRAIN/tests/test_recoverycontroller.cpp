#include "scene/SceneBundle.h"
#include "scene/SceneModel.h"

#include "recovery/RecoveryController.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QVector>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

static std::shared_ptr<const SceneModel> makeScene(const std::string& name) {
	SceneModel scene = makeNewSceneModel();
	scene.name = name;
	for (const char* id : {"track.a", "track.b", "track.c"})
		scene.tracks.push_back({id});
	return std::make_shared<const SceneModel>(std::move(scene));
}

static RecoverySource makeSource(const char* name) {
	RecoverySource source;
	source.displayName = QString::fromLatin1(name);
	return source;
}

// Runs the event loop until the condition holds. False after timeoutMs.
static bool waitFor(const std::function<bool()>& done, int timeoutMs = 20000) {
	if (done())
		return true;
	QEventLoop loop;
	QTimer poll;
	QTimer deadline;
	deadline.setSingleShot(true);
	QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
		if (done())
			loop.quit();
	});
	QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
	poll.start(5);
	deadline.start(timeoutMs);
	loop.exec();
	return done();
}

// Runs the event loop for a while, so that timers and queued results get their turn.
static void settle(int milliseconds) {
	QEventLoop loop;
	QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
	loop.exec();
}

static QStringList instanceFolders(const QString& root) {
	return QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
}

// The bundles and records that the instance folders under the root hold.
static QStringList copyFiles(const QString& root) {
	QStringList found;
	for (const QString& folder : instanceFolders(root))
		found += QDir(QDir(root).filePath(folder)).entryList(QStringList{"gen-*.egscene", "recovery.json"}, QDir::Files, QDir::Name);
	return found;
}

// The name of the scene in the one bundle below the root, or a note when there is not exactly one.
static QString copyName(const QString& root) {
	QStringList bundles;
	for (const QString& folder : instanceFolders(root)) {
		const QDir dir(QDir(root).filePath(folder));
		for (const QString& name : dir.entryList(QStringList{"gen-*.egscene"}, QDir::Files))
			bundles << dir.filePath(name);
	}
	if (bundles.size() != 1)
		return QStringLiteral("<%1 bundles>").arg(bundles.size());
	const SceneLoadResult loaded = loadSceneBundle(bundles.first().toStdString());
	return hasErrors(loaded.diagnostics) ? QStringLiteral("<unreadable>") : QString::fromStdString(loaded.scene.name);
}

// Counts a thread that ends. A worker thread gets one when it first calls the hook.
struct ThreadEndCounter {
	std::atomic<int>& ended;
	~ThreadEndCounter() { ++ended; }
};

// Holds the worker thread at one point of a job, and counts the points that the worker reaches and
// the worker threads that have ended.
class Gate {
public:
	std::function<void(RecoveryHookPoint)> hook() {
		return [this](RecoveryHookPoint point) {
			thread_local ThreadEndCounter counter{m_endedWorkers};
			static_cast<void>(counter);
			std::unique_lock<std::mutex> lock(m_mutex);
			++(point == RecoveryHookPoint::JobStart ? m_jobStarts : m_beforeWrites);
			m_changed.wait(lock, [&]() { return !(m_closed && point == m_point); });
		};
	}
	void closeAt(RecoveryHookPoint point) {
		const std::lock_guard<std::mutex> lock(m_mutex);
		m_closed = true;
		m_point = point;
	}
	void open() {
		{
			const std::lock_guard<std::mutex> lock(m_mutex);
			m_closed = false;
		}
		m_changed.notify_all();
	}
	int endedWorkers() const { return m_endedWorkers; }
	int jobStarts() {
		const std::lock_guard<std::mutex> lock(m_mutex);
		return m_jobStarts;
	}
	int beforeWrites() {
		const std::lock_guard<std::mutex> lock(m_mutex);
		return m_beforeWrites;
	}

private:
	std::mutex m_mutex;
	std::condition_variable m_changed;
	bool m_closed = false;
	RecoveryHookPoint m_point = RecoveryHookPoint::BeforeWrite;
	int m_jobStarts = 0;
	int m_beforeWrites = 0;
	std::atomic<int> m_endedWorkers{0};
};

// A controller on a new root, with the signals it emitted.
struct Fixture {
	QTemporaryDir temp;
	QString root;
	Gate gate;
	QVector<quint64> written;
	QStringList failures;
	std::unique_ptr<RecoveryController> controller;

	Fixture()
		: root(temp.filePath("root")) {
		controller = std::make_unique<RecoveryController>(root, nullptr, gate.hook());
		QObject::connect(controller.get(), &RecoveryController::written, [this](quint64 revision) { written.push_back(revision); });
		QObject::connect(controller.get(), &RecoveryController::writeFailed, [this](const QString& message) { failures << message; });
	}
	// A test that fails must not leave the worker held, or the destructor of the controller would wait for it.
	~Fixture() { gate.open(); }

	bool idle() const { return controller->isIdle(); }
};

static bool testInterval() {
	bool ok = true;
	const auto intervalWith = [](const char* value) {
		if (value)
			qputenv("QEGTRAIN_RECOVERY_INTERVAL_MS", value);
		else
			qunsetenv("QEGTRAIN_RECOVERY_INTERVAL_MS");
		return RecoveryController(QString()).intervalMs();
	};
	ok &= expect(intervalWith("25") == 25, "the variable sets the interval");
	ok &= expect(intervalWith(nullptr) == RecoveryController::kDefaultIntervalMs, "the default interval without the variable");
	ok &= expect(RecoveryController::kDefaultIntervalMs == 30000, "the default interval is 30 seconds");
	for (const char* bad : {"", "abc", "0", "-5", "12x"})
		ok &= expect(intervalWith(bad) == RecoveryController::kDefaultIntervalMs, "an unusable value keeps the default interval");
	intervalWith("10");
	return ok;
}

static bool testNothingWithoutEdits() {
	Fixture f;
	f.controller->noteSaved();
	f.controller->noteClean();
	f.controller->noteReplaced();
	settle(100);
	bool ok = expect(f.gate.jobStarts() == 0, "no job without an edit");
	f.controller->shutdown();
	ok &= expect(!QFile::exists(f.root), "a controller that never wrote creates nothing");
	return ok;
}

static bool testDebounce() {
	Fixture f;
	bool ok = true;
	f.controller->noteEdited(makeScene("first"), makeSource("first"), 1);
	f.controller->noteEdited(makeScene("second"), makeSource("second"), 2);
	f.controller->noteEdited(makeScene("third"), makeSource("third"), 3);
	ok &= expect(!f.idle(), "an edit waits for the timer");
	ok &= expect(!QFile::exists(f.root), "nothing is written before the timer fires");
	ok &= expect(waitFor([&]() { return !f.written.isEmpty(); }), "the timer writes the scene");
	settle(150);
	ok &= expect(f.written == QVector<quint64>{3}, "the edits of one interval give one write, of the newest scene");
	ok &= expect(f.gate.beforeWrites() == 1, "the store was written once");
	ok &= expect(copyName(f.root) == "third", "the copy holds the newest scene");
	ok &= expect(f.idle() && f.failures.isEmpty(), "nothing is left to do and nothing failed");

	f.controller->noteEdited(makeScene("third"), makeSource("third"), 3);
	settle(150);
	ok &= expect(f.written == QVector<quint64>{3}, "a revision that is on disk is not written again");

	f.controller->noteEdited(makeScene("fourth"), makeSource("fourth"), 4);
	ok &= expect(waitFor([&]() { return f.written.size() == 2; }), "a new revision is written");
	ok &= expect(f.written == (QVector<quint64>{3, 4}) && copyName(f.root) == "fourth", "the new copy replaces the old one");
	return ok;
}

static bool testSingleFlightWithCoalescing() {
	Fixture f;
	bool ok = true;
	f.gate.closeAt(RecoveryHookPoint::BeforeWrite);
	f.controller->noteEdited(makeScene("first"), makeSource("first"), 1);
	ok &= expect(waitFor([&]() { return f.gate.beforeWrites() == 1; }), "the first write starts");

	// Each of these edits arms the timer again, and the timer fires while the write is running.
	f.controller->noteEdited(makeScene("second"), makeSource("second"), 2);
	settle(100);
	f.controller->noteEdited(makeScene("third"), makeSource("third"), 3);
	settle(100);
	f.controller->noteEdited(makeScene("fourth"), makeSource("fourth"), 4);
	settle(100);
	ok &= expect(f.gate.jobStarts() == 1 && f.gate.beforeWrites() == 1, "no second job starts while one runs");
	ok &= expect(f.written.isEmpty() && !f.idle(), "the running write has not finished");

	f.gate.open();
	ok &= expect(waitFor([&]() { return f.written.size() == 2 && f.idle(); }), "the waiting edits are written");
	settle(100);
	ok &= expect(f.written == (QVector<quint64>{1, 4}), "the edits that arrived during the job give one more write, of the newest scene");
	ok &= expect(f.gate.jobStarts() == 2 && copyName(f.root) == "fourth", "two jobs ran and the copy holds the newest scene");
	return ok;
}

// A write is running when the scene is saved, replaced or back at its clean state.
static bool testCancelDuringWrite(void (RecoveryController::*cancel)(), const char* what) {
	Fixture f;
	bool ok = true;
	f.gate.closeAt(RecoveryHookPoint::BeforeWrite);
	f.controller->noteEdited(makeScene("edited"), makeSource("edited"), 1);
	ok &= expect(waitFor([&]() { return f.gate.beforeWrites() == 1; }), what);
	(f.controller.get()->*cancel)();
	f.gate.open();
	ok &= expect(waitFor([&]() { return f.idle(); }), what);
	settle(100);
	ok &= expect(copyFiles(f.root).isEmpty(), what);
	ok &= expect(f.written.isEmpty() && f.failures.isEmpty(), what);
	return ok;
}

static bool testCancelBeforeWrite() {
	bool ok = true;
	for (void (RecoveryController::*cancel)() : {&RecoveryController::noteSaved, &RecoveryController::noteClean, &RecoveryController::noteReplaced}) {
		Fixture f;
		f.controller->noteEdited(makeScene("edited"), makeSource("edited"), 1);
		(f.controller.get()->*cancel)();
		settle(150);
		ok &= expect(f.gate.jobStarts() == 0 && f.written.isEmpty(), "an edit that is cancelled before the timer fires is not written");
		ok &= expect(!QFile::exists(f.root) && f.idle(), "nothing was created");
	}
	return ok;
}

static bool testRemoveAndWriteAgain() {
	Fixture f;
	bool ok = true;
	f.controller->noteEdited(makeScene("one"), makeSource("one"), 5);
	ok &= expect(waitFor([&]() { return f.written.size() == 1; }), "the first copy is written");
	ok &= expect(copyFiles(f.root).size() == 2, "a bundle and a record are on disk");
	f.controller->noteSaved();
	ok &= expect(waitFor([&]() { return f.idle(); }), "the removal ends");
	ok &= expect(copyFiles(f.root).isEmpty(), "the save removes the copy");
	ok &= expect(f.written == QVector<quint64>{5}, "the removal is not a write");

	// The copy of this revision is gone, so it is written again.
	f.controller->noteEdited(makeScene("two"), makeSource("two"), 5);
	ok &= expect(waitFor([&]() { return f.written.size() == 2; }), "the revision is written again after a removal");
	ok &= expect(copyName(f.root) == "two", "the new copy is on disk");
	return ok;
}

static bool testShutdownWaitsAndRemoves() {
	Fixture f;
	bool ok = true;
	f.gate.closeAt(RecoveryHookPoint::BeforeWrite);
	f.controller->noteEdited(makeScene("edited"), makeSource("edited"), 1);
	ok &= expect(waitFor([&]() { return f.gate.beforeWrites() == 1; }), "the write starts");
	ok &= expect(instanceFolders(f.root).size() == 1, "the instance folder exists");

	std::atomic<bool> released(false);
	std::thread releaser([&]() {
		QThread::msleep(100);
		released = true;
		f.gate.open();
	});
	f.controller->shutdown();
	ok &= expect(released, "shutdown waits for the write that runs");
	ok &= expect(f.gate.endedWorkers() == 1, "shutdown joins the worker thread");
	releaser.join();
	ok &= expect(instanceFolders(f.root).isEmpty(), "shutdown removes the instance folder");
	settle(100);
	ok &= expect(f.written.isEmpty() && f.failures.isEmpty(), "a write that ends in shutdown reports nothing");

	f.controller->noteEdited(makeScene("late"), makeSource("late"), 2);
	settle(100);
	ok &= expect(f.gate.beforeWrites() == 1 && instanceFolders(f.root).isEmpty() && f.idle(), "nothing is written after shutdown");
	f.controller->shutdown();
	const RecoveryResult adopted = f.controller->adopt(RecoveryCandidate());
	ok &= expect(!adopted.ok && instanceFolders(f.root).isEmpty(), "adopt is refused after shutdown");
	return ok;
}

// A job that has taken the job from the queue but not started when the shutdown begins does not write.
static bool testShutdownBeforeJobStarts() {
	Fixture f;
	bool ok = true;
	f.gate.closeAt(RecoveryHookPoint::JobStart);
	f.controller->noteEdited(makeScene("edited"), makeSource("edited"), 1);
	ok &= expect(waitFor([&]() { return f.gate.jobStarts() == 1; }), "the job is taken");
	std::thread releaser([&]() {
		QThread::msleep(100);
		f.gate.open();
	});
	f.controller->shutdown();
	releaser.join();
	ok &= expect(!QFile::exists(f.root) || instanceFolders(f.root).isEmpty(), "a job that had not started leaves nothing");
	ok &= expect(f.gate.beforeWrites() == 0, "the job did not write");
	return ok;
}

// A scene that the bundle writer refuses: a coordinate that is not a number.
static std::shared_ptr<const SceneModel> makeInvalidScene() {
	SceneModel scene = *makeScene("invalid");
	SceneNode node;
	node.id = "node.a";
	node.trackId = "track.a";
	node.xKm = std::numeric_limits<double>::quiet_NaN();
	scene.nodes.push_back(node);
	return std::make_shared<const SceneModel>(std::move(scene));
}

static bool testFailureReportedOnce() {
	QTemporaryDir temp;
	const QString blocker = temp.filePath("blocker");
	QFile file(blocker);
	if (!expect(file.open(QIODevice::WriteOnly) && file.write("x") == 1, "a file blocks the root"))
		return false;
	file.close();

	bool ok = true;
	const QString root = QDir(blocker).filePath("recovery");
	RecoveryController controller(root);
	QStringList failures;
	QVector<quint64> written;
	QObject::connect(&controller, &RecoveryController::writeFailed, [&](const QString& message) { failures << message; });
	QObject::connect(&controller, &RecoveryController::written, [&](quint64 revision) { written.push_back(revision); });
	quint64 revision = 0;
	const auto attempt = [&](const std::shared_ptr<const SceneModel>& scene) {
		controller.noteEdited(scene, makeSource("edited"), ++revision);
		return waitFor([&]() { return controller.isIdle(); });
	};
	const std::shared_ptr<const SceneModel> good = makeScene("edited");
	const std::shared_ptr<const SceneModel> bad = makeInvalidScene();

	// The folder of the copies cannot be created.
	ok &= expect(attempt(good), "the first attempt ends");
	ok &= expect(failures.size() == 1 && written.isEmpty(), "the first failure is reported");
	ok &= expect(!failures.isEmpty() && !failures.first().isEmpty() && !failures.first().contains(blocker) && !failures.first().contains(QLatin1Char('/')),
		"the message is a sentence without a path");
	ok &= expect(attempt(good) && attempt(good), "the next attempts end");
	ok &= expect(failures.size() == 1, "a failure run is reported once");

	// The folder becomes usable: a write succeeds and ends the run.
	if (!expect(QFile::remove(blocker) && QDir().mkdir(blocker), "the blocker is replaced by a folder"))
		return false;
	ok &= expect(attempt(good), "the attempt after the repair ends");
	ok &= expect(written == QVector<quint64>{4} && failures.size() == 1, "the write succeeds");

	// The writer refuses the scene: a new run, reported once, and the copy on disk stays.
	ok &= expect(attempt(bad), "the first refused scene ends");
	ok &= expect(failures.size() == 2 && written.size() == 1, "a failure after a success is reported again");
	ok &= expect(attempt(bad) && failures.size() == 2, "a second refusal is not reported");
	ok &= expect(copyName(root) == "edited", "a refused write keeps the previous copy");
	ok &= expect(attempt(good) && written.size() == 2, "a valid scene is written again");
	ok &= expect(attempt(bad) && failures.size() == 3, "the next refusal is a new run");
	return ok;
}

// Scans, adopts and discards go through the controller, and an adoption cancels what waits.
static bool testStoreCalls() {
	Fixture f;
	bool ok = true;
	{
		RecoveryStore first(f.root);
		RecoveryStore second(f.root);
		ok &= expect(first.open().ok && second.open().ok, "the old stores open");
		ok &= expect(first.write(*makeScene("dead one"), makeSource("dead one")).ok, "the first old copy is written");
		QThread::msleep(20);
		ok &= expect(second.write(*makeScene("dead two"), makeSource("dead two")).ok, "the second old copy is written");
	}
	const RecoveryScan scan = f.controller->scan();
	if (!expect(scan.candidates.size() == 2, "the scan lists both old copies"))
		return false;
	const RecoveryCandidate newest = scan.candidates.at(0);
	const RecoveryCandidate older = scan.candidates.at(1);
	ok &= expect(newest.source.displayName == "dead two" && older.source.displayName == "dead one", "the scan lists the newest copy first");

	ok &= expect(f.controller->discard(newest).ok && instanceFolders(f.root).size() == 1, "discard removes a copy");

	// An edit waits when the copy is taken over: it belongs to the scene that is being replaced.
	f.controller->noteEdited(makeScene("stale"), makeSource("stale"), 1);
	const RecoveryResult adopted = f.controller->adopt(older);
	ok &= expect(adopted.ok, "adopt takes over the copy");
	settle(150);
	ok &= expect(f.written.isEmpty() && copyName(f.root) == "dead one", "the edit that waited does not replace the copy");
	ok &= expect(instanceFolders(f.root).size() == 1, "the old folder is gone and the controller has its own");

	f.controller->noteSaved();
	ok &= expect(waitFor([&]() { return f.idle(); }), "the removal ends");
	ok &= expect(copyFiles(f.root).isEmpty(), "a save removes the copy that was taken over");
	return ok;
}

// A job that has started but not locked the store when the copy is taken over does not write.
static bool testAdoptBeforeJobWrites() {
	Fixture f;
	bool ok = true;
	{
		RecoveryStore old(f.root);
		ok &= expect(old.open().ok && old.write(*makeScene("taken"), makeSource("taken")).ok, "the old copy is written");
	}
	const RecoveryScan scan = f.controller->scan();
	if (!expect(scan.candidates.size() == 1, "the scan lists the old copy"))
		return false;

	f.gate.closeAt(RecoveryHookPoint::JobStart);
	f.controller->noteEdited(makeScene("stale"), makeSource("stale"), 1);
	ok &= expect(waitFor([&]() { return f.gate.jobStarts() == 1; }), "the job is taken");
	ok &= expect(f.controller->adopt(scan.candidates.first()).ok, "adopt takes over the copy");
	f.gate.open();
	ok &= expect(waitFor([&]() { return f.idle(); }), "the job ends");
	settle(100);
	ok &= expect(f.gate.beforeWrites() == 0 && f.written.isEmpty(), "the job from before the adoption does not write");
	ok &= expect(copyName(f.root) == "taken", "the copy that was taken over is still the copy");
	return ok;
}

// A save is followed by an adoption while the write that the save cancelled still runs. The removal
// that the save asked for must not remove the copy that was taken over.
static bool testAdoptAfterSaveDuringWrite() {
	Fixture f;
	bool ok = true;
	{
		RecoveryStore old(f.root);
		ok &= expect(old.open().ok && old.write(*makeScene("taken"), makeSource("taken")).ok, "the old copy is written");
	}
	const RecoveryScan scan = f.controller->scan();
	if (!expect(scan.candidates.size() == 1, "the scan lists the old copy"))
		return false;

	f.gate.closeAt(RecoveryHookPoint::BeforeWrite);
	f.controller->noteEdited(makeScene("edited"), makeSource("edited"), 1);
	ok &= expect(waitFor([&]() { return f.gate.beforeWrites() == 1; }), "the write starts");
	f.controller->noteSaved();
	std::thread releaser([&]() {
		QThread::msleep(100);
		f.gate.open();
	});
	const RecoveryResult adopted = f.controller->adopt(scan.candidates.first());
	releaser.join();
	ok &= expect(adopted.ok, "adopt takes over the copy after the write ended");
	ok &= expect(waitFor([&]() { return f.idle(); }), "the jobs end");
	settle(100);
	ok &= expect(f.written.isEmpty() && copyName(f.root) == "taken", "the removal of the save does not remove the copy that was taken over");
	return ok;
}

int main(int argc, char** argv) {
	QCoreApplication application(argc, argv);
	bool ok = testInterval();
	qputenv("QEGTRAIN_RECOVERY_INTERVAL_MS", "10");
	ok &= testNothingWithoutEdits();
	ok &= testDebounce();
	ok &= testSingleFlightWithCoalescing();
	ok &= testCancelDuringWrite(&RecoveryController::noteSaved, "a save during a write leaves no copy");
	ok &= testCancelDuringWrite(&RecoveryController::noteClean, "back at clean during a write leaves no copy");
	ok &= testCancelDuringWrite(&RecoveryController::noteReplaced, "a replacement during a write leaves no copy");
	ok &= testCancelBeforeWrite();
	ok &= testRemoveAndWriteAgain();
	ok &= testShutdownWaitsAndRemoves();
	ok &= testShutdownBeforeJobStarts();
	ok &= testFailureReportedOnce();
	ok &= testStoreCalls();
	ok &= testAdoptBeforeJobWrites();
	ok &= testAdoptAfterSaveDuringWrite();
	return ok ? 0 : 1;
}
