#include "scene/SceneBundle.h"
#include "scene/SceneModel.h"

#include "recovery/RecoveryController.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <QVector>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

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

// Sets the interval of the controllers that are made while it is in scope, then restores the one that the other cases use.
struct IntervalScope {
	explicit IntervalScope(const char* milliseconds) { qputenv("QEGTRAIN_RECOVERY_INTERVAL_MS", milliseconds); }
	~IntervalScope() { qputenv("QEGTRAIN_RECOVERY_INTERVAL_MS", "10"); }
};

// Takes the right to add and remove files away from a folder while it is in scope.
class ReadOnlyFolder {
public:
	explicit ReadOnlyFolder(const QString& path)
		: m_path(path), m_changed(QFile::setPermissions(path, QFile::ReadOwner | QFile::ExeOwner)) {}
	~ReadOnlyFolder() {
		if (m_changed)
			QFile::setPermissions(m_path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
	}
	ReadOnlyFolder(const ReadOnlyFolder&) = delete;
	ReadOnlyFolder& operator=(const ReadOnlyFolder&) = delete;
	// False when the folder still accepts a new file, as it does for a superuser.
	bool effective() const {
		QFile probe(QDir(m_path).filePath(QStringLiteral("probe")));
		if (!m_changed || probe.open(QIODevice::WriteOnly)) {
			probe.close();
			probe.remove();
			return false;
		}
		return true;
	}

private:
	QString m_path;
	bool m_changed;
};

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

// The interval of a new controller when the variable has the value, or is unset for null.
static int intervalWith(const char* value) {
	if (value)
		qputenv("QEGTRAIN_RECOVERY_INTERVAL_MS", value);
	else
		qunsetenv("QEGTRAIN_RECOVERY_INTERVAL_MS");
	return RecoveryController(QString()).intervalMs();
}

// The calls that remove the copy: a save, the return to the clean state and a replacement.
static void (RecoveryController::* const kCancels[])() = {&RecoveryController::noteSaved, &RecoveryController::noteClean,
	&RecoveryController::noteReplaced};

class TestRecoveryController : public QObject {
	Q_OBJECT

private slots:
	void init();
	void readsInterval();
	void keepsDefaultForUnusableInterval_data();
	void keepsDefaultForUnusableInterval();
	void writesNothingWithoutEdits();
	void writesNewestSceneOfInterval();
	void laterEditsDoNotMoveTimer();
	void runsOneJobAtATime();
	void cancelDuringWrite_data();
	void cancelDuringWrite();
	void cancelBeforeWrite_data();
	void cancelBeforeWrite();
	void writesAgainAfterRemoval();
	void shutdownWaitsAndRemoves();
	void shutdownBeforeJobStarts();
	void destructorKeepsCopy();
	void reportsFailureOnce();
	void reportsFailedRemovalOnce();
	void passesStoreCalls();
	void adoptBeforeJobWrites();
	void adoptAfterSaveDuringWrite();
	void failedAdoptKeepsRemoval();
};

// The cases other than the interval checks write after 10 ms.
void TestRecoveryController::init() {
	qputenv("QEGTRAIN_RECOVERY_INTERVAL_MS", "10");
}

void TestRecoveryController::readsInterval() {
	// The variable sets the interval.
	QCOMPARE(intervalWith("25"), 25);
	// The default interval without the variable.
	QCOMPARE(intervalWith(nullptr), RecoveryController::kDefaultIntervalMs);
	// The default interval is 30 seconds.
	QCOMPARE(RecoveryController::kDefaultIntervalMs, 30000);
	// The number is decimal, so a leading zero is not octal.
	QCOMPARE(intervalWith("010"), 10);
}

void TestRecoveryController::keepsDefaultForUnusableInterval_data() {
	QTest::addColumn<QByteArray>("value");
	for (const char* bad : {"", "abc", "0", "-5", "12x", "0x10"})
		QTest::newRow(*bad ? bad : "empty") << QByteArray(bad);
}

void TestRecoveryController::keepsDefaultForUnusableInterval() {
	QFETCH(QByteArray, value);
	// An unusable value keeps the default interval.
	QCOMPARE(intervalWith(value.constData()), RecoveryController::kDefaultIntervalMs);
}

void TestRecoveryController::writesNothingWithoutEdits() {
	Fixture f;
	f.controller->noteSaved();
	f.controller->noteClean();
	f.controller->noteReplaced();
	settle(100);
	// No job without an edit.
	QCOMPARE(f.gate.jobStarts(), 0);
	f.controller->shutdown();
	QVERIFY2(!QFile::exists(f.root), "a controller that never wrote creates nothing");
}

void TestRecoveryController::writesNewestSceneOfInterval() {
	Fixture f;
	f.controller->noteEdited(makeScene("first"), makeSource("first"), 1);
	f.controller->noteEdited(makeScene("second"), makeSource("second"), 2);
	f.controller->noteEdited(makeScene("third"), makeSource("third"), 3);
	QVERIFY2(!f.idle(), "an edit waits for the timer");
	QVERIFY2(!QFile::exists(f.root), "nothing is written before the timer fires");
	QVERIFY2(waitFor([&]() { return !f.written.isEmpty(); }), "the timer writes the scene");
	settle(150);
	// The edits of one interval give one write, of the newest scene.
	QCOMPARE(f.written, QVector<quint64>{3});
	// The store was written once.
	QCOMPARE(f.gate.beforeWrites(), 1);
	// The copy holds the newest scene.
	QCOMPARE(copyName(f.root), "third");
	// Nothing is left to do and nothing failed.
	QVERIFY(f.idle());
	QVERIFY(f.failures.isEmpty());

	f.controller->noteEdited(makeScene("third"), makeSource("third"), 3);
	settle(150);
	// A revision that is on disk is not written again.
	QCOMPARE(f.written, QVector<quint64>{3});

	f.controller->noteEdited(makeScene("fourth"), makeSource("fourth"), 4);
	QVERIFY2(waitFor([&]() { return f.written.size() == 2; }), "a new revision is written");
	// The new copy replaces the old one.
	QCOMPARE(f.written, (QVector<quint64>{3, 4}));
	QCOMPARE(copyName(f.root), "fourth");
}

// The first edit arms the timer and the later ones leave it alone, so editing without a pause cannot keep the write away.
// The write is due 2000 ms after the first edit. A timer that restarts on the second edit would write 2000 ms after
// that edit, which is more than 1700 ms later, so the margins on both sides are several hundred milliseconds.
void TestRecoveryController::laterEditsDoNotMoveTimer() {
	const IntervalScope interval("2000");
	Fixture f;
	// The case uses a long interval.
	QCOMPARE(f.controller->intervalMs(), 2000);
	f.controller->noteEdited(makeScene("first"), makeSource("first"), 1);
	settle(800);
	f.controller->noteEdited(makeScene("second"), makeSource("second"), 2);
	// A timer that the second edit started again would write 2000 ms after it, a timer that keeps
	// running about 1200 ms after it, however long the wait above took.
	QElapsedTimer sinceSecondEdit;
	sinceSecondEdit.start();
	QVERIFY2(waitFor([&]() { return !f.written.isEmpty(); }, 4000), "the copy is written");
	// The write comes one interval after the first edit.
	QCOMPARE_LT(sinceSecondEdit.elapsed(), qint64(1700));
	// The write holds the newest scene.
	QCOMPARE(f.written, QVector<quint64>{2});
	QCOMPARE(copyName(f.root), "second");
}

void TestRecoveryController::runsOneJobAtATime() {
	Fixture f;
	f.gate.closeAt(RecoveryHookPoint::BeforeWrite);
	f.controller->noteEdited(makeScene("first"), makeSource("first"), 1);
	QVERIFY2(waitFor([&]() { return f.gate.beforeWrites() == 1; }), "the first write starts");

	// Each of these edits arms the timer again, and the timer fires while the write is running.
	f.controller->noteEdited(makeScene("second"), makeSource("second"), 2);
	settle(100);
	f.controller->noteEdited(makeScene("third"), makeSource("third"), 3);
	settle(100);
	f.controller->noteEdited(makeScene("fourth"), makeSource("fourth"), 4);
	settle(100);
	// The running write has not finished.
	QVERIFY(f.written.isEmpty());
	QVERIFY(!f.idle());

	// The event loop does not run for a while, so the owning thread has not taken the result of the write
	// and cannot hand over a job. A worker that finds a second job by itself got it from the timer while
	// the first job was running. The sleep stays fixed: it checks that no second job starts.
	f.gate.open();
	QThread::msleep(100);
	// The edits that arrived during a job wait for it to end.
	QCOMPARE(f.gate.jobStarts(), 1);
	QVERIFY2(waitFor([&]() { return f.written.size() == 2 && f.idle(); }), "the waiting edits are written");
	settle(100);
	// The edits that arrived during the job give one more write, of the newest scene.
	QCOMPARE(f.written, (QVector<quint64>{1, 4}));
	// Two jobs ran and the copy holds the newest scene.
	QCOMPARE(f.gate.jobStarts(), 2);
	QCOMPARE(copyName(f.root), "fourth");
}

void TestRecoveryController::cancelDuringWrite_data() {
	QTest::addColumn<int>("cancel");
	QTest::newRow("a save during a write leaves no copy") << 0;
	QTest::newRow("back at clean during a write leaves no copy") << 1;
	QTest::newRow("a replacement during a write leaves no copy") << 2;
}

// A write is running when the scene is saved, replaced or back at its clean state.
void TestRecoveryController::cancelDuringWrite() {
	QFETCH(int, cancel);
	const char* what = QTest::currentDataTag();
	Fixture f;
	f.gate.closeAt(RecoveryHookPoint::BeforeWrite);
	f.controller->noteEdited(makeScene("edited"), makeSource("edited"), 1);
	QVERIFY2(waitFor([&]() { return f.gate.beforeWrites() == 1; }), what);
	(f.controller.get()->*kCancels[cancel])();
	f.gate.open();
	QVERIFY2(waitFor([&]() { return f.idle(); }), what);
	settle(100);
	QVERIFY2(copyFiles(f.root).isEmpty(), what);
	QVERIFY2(f.written.isEmpty(), what);
	QVERIFY2(f.failures.isEmpty(), what);
}

void TestRecoveryController::cancelBeforeWrite_data() {
	QTest::addColumn<int>("cancel");
	QTest::newRow("save") << 0;
	QTest::newRow("back at clean") << 1;
	QTest::newRow("replacement") << 2;
}

void TestRecoveryController::cancelBeforeWrite() {
	QFETCH(int, cancel);
	Fixture f;
	f.controller->noteEdited(makeScene("edited"), makeSource("edited"), 1);
	(f.controller.get()->*kCancels[cancel])();
	settle(150);
	// An edit that is cancelled before the timer fires is not written.
	QCOMPARE(f.gate.jobStarts(), 0);
	QVERIFY(f.written.isEmpty());
	// Nothing was created.
	QVERIFY(!QFile::exists(f.root));
	QVERIFY(f.idle());
}

void TestRecoveryController::writesAgainAfterRemoval() {
	Fixture f;
	f.controller->noteEdited(makeScene("one"), makeSource("one"), 5);
	QVERIFY2(waitFor([&]() { return f.written.size() == 1; }), "the first copy is written");
	// A bundle and a record are on disk.
	QCOMPARE(copyFiles(f.root).size(), 2);
	f.controller->noteSaved();
	QVERIFY2(waitFor([&]() { return f.idle(); }), "the removal ends");
	QVERIFY2(copyFiles(f.root).isEmpty(), "the save removes the copy");
	// The removal is not a write.
	QCOMPARE(f.written, QVector<quint64>{5});

	// The copy of this revision is gone, so it is written again.
	f.controller->noteEdited(makeScene("two"), makeSource("two"), 5);
	QVERIFY2(waitFor([&]() { return f.written.size() == 2; }), "the revision is written again after a removal");
	// The new copy is on disk.
	QCOMPARE(copyName(f.root), "two");
}

void TestRecoveryController::shutdownWaitsAndRemoves() {
	Fixture f;
	f.gate.closeAt(RecoveryHookPoint::BeforeWrite);
	f.controller->noteEdited(makeScene("edited"), makeSource("edited"), 1);
	QVERIFY2(waitFor([&]() { return f.gate.beforeWrites() == 1; }), "the write starts");
	// The instance folder exists.
	QCOMPARE(instanceFolders(f.root).size(), 1);

	// The write goes on 100 ms after the shutdown began. The sleep stays fixed: a shutdown that returned
	// within it would not have waited for the write.
	std::atomic<bool> released(false);
	std::thread releaser([&]() {
		QThread::msleep(100);
		released = true;
		f.gate.open();
	});
	f.controller->shutdown();
	const bool waited = released;
	const int endedWorkers = f.gate.endedWorkers();
	releaser.join();
	QVERIFY2(waited, "shutdown waits for the write that runs");
	// Shutdown joins the worker thread.
	QCOMPARE(endedWorkers, 1);
	QVERIFY2(instanceFolders(f.root).isEmpty(), "shutdown removes the instance folder");
	settle(100);
	// A write that ends in shutdown reports nothing.
	QVERIFY(f.written.isEmpty());
	QVERIFY(f.failures.isEmpty());

	f.controller->noteEdited(makeScene("late"), makeSource("late"), 2);
	settle(100);
	// Nothing is written after shutdown.
	QCOMPARE(f.gate.beforeWrites(), 1);
	QVERIFY(instanceFolders(f.root).isEmpty());
	QVERIFY(f.idle());
	f.controller->shutdown();
	const RecoveryResult adopted = f.controller->adopt(RecoveryCandidate());
	// Adopt is refused after shutdown.
	QVERIFY(!adopted.ok);
	QVERIFY(instanceFolders(f.root).isEmpty());
}

// A job that has taken the job from the queue but not started when the shutdown begins does not write.
void TestRecoveryController::shutdownBeforeJobStarts() {
	Fixture f;
	f.gate.closeAt(RecoveryHookPoint::JobStart);
	f.controller->noteEdited(makeScene("edited"), makeSource("edited"), 1);
	QVERIFY2(waitFor([&]() { return f.gate.jobStarts() == 1; }), "the job is taken");
	// The sleep gives the shutdown time to start a new epoch before the job goes on. No signal marks that
	// moment, so it stays fixed.
	std::thread releaser([&]() {
		QThread::msleep(100);
		f.gate.open();
	});
	f.controller->shutdown();
	releaser.join();
	QVERIFY2(!QFile::exists(f.root) || instanceFolders(f.root).isEmpty(), "a job that had not started leaves nothing");
	// The job did not write.
	QCOMPARE(f.gate.beforeWrites(), 0);
}

// A controller that is destroyed without a shutdown ends its thread and leaves the copy for the next start.
void TestRecoveryController::destructorKeepsCopy() {
	Fixture f;
	f.controller->noteEdited(makeScene("edited"), makeSource("edited"), 1);
	QVERIFY2(waitFor([&]() { return f.written.size() == 1 && f.idle(); }), "the copy is written");
	f.controller.reset();
	// The destructor joins the worker thread.
	QCOMPARE(f.gate.endedWorkers(), 1);
	// The copy stays on disk.
	QCOMPARE(copyFiles(f.root).size(), 2);
	QCOMPARE(copyName(f.root), "edited");
	RecoveryStore next(f.root);
	const RecoveryScan scan = next.scan();
	// The lock is released, so the next start finds the copy.
	QCOMPARE(scan.candidates.size(), 1);
	QCOMPARE(scan.candidates.first().source.displayName, "edited");
}

void TestRecoveryController::reportsFailureOnce() {
	QTemporaryDir temp;
	const QString blocker = temp.filePath("blocker");
	QFile file(blocker);
	// A file blocks the root.
	QVERIFY(file.open(QIODevice::WriteOnly));
	QCOMPARE(file.write("x"), qint64(1));
	file.close();

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
	QVERIFY2(attempt(good), "the first attempt ends");
	// The first failure is reported.
	QCOMPARE(failures.size(), 1);
	QVERIFY(written.isEmpty());
	// The message is a sentence without a path.
	QVERIFY(!failures.isEmpty());
	QVERIFY(!failures.first().isEmpty());
	QVERIFY(!failures.first().contains(blocker));
	QVERIFY(!failures.first().contains(QLatin1Char('/')));
	// The next attempts end.
	QVERIFY(attempt(good));
	QVERIFY(attempt(good));
	// A failure run is reported once.
	QCOMPARE(failures.size(), 1);

	// The folder becomes usable: a write succeeds and ends the run.
	// The blocker is replaced by a folder.
	QVERIFY(QFile::remove(blocker));
	QVERIFY(QDir().mkdir(blocker));
	QVERIFY2(attempt(good), "the attempt after the repair ends");
	// The write succeeds.
	QCOMPARE(written, QVector<quint64>{4});
	QCOMPARE(failures.size(), 1);

	// The writer refuses the scene: a new run, reported once, and the copy on disk stays.
	QVERIFY2(attempt(bad), "the first refused scene ends");
	// A failure after a success is reported again.
	QCOMPARE(failures.size(), 2);
	QCOMPARE(written.size(), 1);
	// A second refusal is not reported.
	QVERIFY(attempt(bad));
	QCOMPARE(failures.size(), 2);
	// A refused write keeps the previous copy.
	QCOMPARE(copyName(root), "edited");
	// A valid scene is written again.
	QVERIFY(attempt(good));
	QCOMPARE(written.size(), 2);
	// The next refusal is a new run.
	QVERIFY(attempt(bad));
	QCOMPARE(failures.size(), 3);
}

// A removal that fails is reported like a write that fails, once per run, and the copy stays.
void TestRecoveryController::reportsFailedRemovalOnce() {
#ifndef Q_OS_UNIX
	QSKIP("the permissions of a folder do not stop a removal here");
#else
	Fixture f;
	f.controller->noteEdited(makeScene("edited"), makeSource("edited"), 1);
	QVERIFY2(waitFor([&]() { return f.written.size() == 1 && f.idle(); }), "the copy is written");
	const QStringList folders = instanceFolders(f.root);
	// There is one instance folder.
	QCOMPARE(folders.size(), 1);
	const QString folder = QDir(f.root).filePath(folders.first());
	if (!ReadOnlyFolder(folder).effective())
		QSKIP("the permissions of a folder do not stop this user");
	const auto saveWithoutPermission = [&]() {
		const ReadOnlyFolder locked(folder);
		f.controller->noteSaved();
		return waitFor([&]() { return f.idle(); });
	};

	QVERIFY2(saveWithoutPermission(), "the first removal ends");
	// A removal that fails is reported and the copy stays.
	QCOMPARE(f.failures.size(), 1);
	QCOMPARE(copyFiles(f.root).size(), 2);
	// The message is a sentence without a path.
	QVERIFY(!f.failures.isEmpty());
	QVERIFY(!f.failures.first().isEmpty());
	QVERIFY(!f.failures.first().contains(QLatin1Char('/')));
	// A second failing removal is not reported.
	QVERIFY(saveWithoutPermission());
	QCOMPARE(f.failures.size(), 1);
	QCOMPARE(copyFiles(f.root).size(), 2);

	f.controller->noteSaved();
	// The removal runs again and succeeds.
	QVERIFY(waitFor([&]() { return f.idle(); }));
	QVERIFY(copyFiles(f.root).isEmpty());
	// The success reports nothing.
	QCOMPARE(f.failures.size(), 1);
	QCOMPARE(f.written, QVector<quint64>{1});

	// The success ended the run: the next failing removal is reported again.
	f.controller->noteEdited(makeScene("again"), makeSource("again"), 2);
	QVERIFY2(waitFor([&]() { return f.written.size() == 2 && f.idle(); }), "a new copy is written");
	// A failing removal after a success is a new run.
	QVERIFY(saveWithoutPermission());
	QCOMPARE(f.failures.size(), 2);
#endif
}

// Scans, adopts and discards go through the controller, and an adoption cancels what waits.
void TestRecoveryController::passesStoreCalls() {
	Fixture f;
	{
		RecoveryStore first(f.root);
		RecoveryStore second(f.root);
		// The old stores open.
		QVERIFY(first.open().ok);
		QVERIFY(second.open().ok);
		QVERIFY2(first.write(*makeScene("dead one"), makeSource("dead one")).ok, "the first old copy is written");
		// The second copy is written in a later millisecond, so that the scan can order the two.
		const QDateTime firstWritten = QDateTime::currentDateTimeUtc();
		QTRY_VERIFY_WITH_TIMEOUT(QDateTime::currentDateTimeUtc() > firstWritten, 20);
		QVERIFY2(second.write(*makeScene("dead two"), makeSource("dead two")).ok, "the second old copy is written");
	}
	const RecoveryScan scan = f.controller->scan();
	// The scan lists both old copies.
	QCOMPARE(scan.candidates.size(), 2);
	const RecoveryCandidate newest = scan.candidates.at(0);
	const RecoveryCandidate older = scan.candidates.at(1);
	// The scan lists the newest copy first.
	QCOMPARE(newest.source.displayName, "dead two");
	QCOMPARE(older.source.displayName, "dead one");

	// Discard removes a copy.
	QVERIFY(f.controller->discard(newest).ok);
	QCOMPARE(instanceFolders(f.root).size(), 1);

	// An edit waits when the copy is taken over: it belongs to the scene that is being replaced.
	f.controller->noteEdited(makeScene("stale"), makeSource("stale"), 1);
	const RecoveryResult adopted = f.controller->adopt(older);
	QVERIFY2(adopted.ok, "adopt takes over the copy");
	settle(150);
	// The edit that waited does not replace the copy.
	QVERIFY(f.written.isEmpty());
	QCOMPARE(copyName(f.root), "dead one");
	// The old folder is gone and the controller has its own.
	QCOMPARE(instanceFolders(f.root).size(), 1);

	f.controller->noteSaved();
	QVERIFY2(waitFor([&]() { return f.idle(); }), "the removal ends");
	QVERIFY2(copyFiles(f.root).isEmpty(), "a save removes the copy that was taken over");
}

// A job that has started but not locked the store when the copy is taken over does not write.
void TestRecoveryController::adoptBeforeJobWrites() {
	Fixture f;
	{
		RecoveryStore old(f.root);
		// The old copy is written.
		QVERIFY(old.open().ok);
		QVERIFY(old.write(*makeScene("taken"), makeSource("taken")).ok);
	}
	const RecoveryScan scan = f.controller->scan();
	// The scan lists the old copy.
	QCOMPARE(scan.candidates.size(), 1);

	f.gate.closeAt(RecoveryHookPoint::JobStart);
	f.controller->noteEdited(makeScene("stale"), makeSource("stale"), 1);
	QVERIFY2(waitFor([&]() { return f.gate.jobStarts() == 1; }), "the job is taken");
	QVERIFY2(f.controller->adopt(scan.candidates.first()).ok, "adopt takes over the copy");
	f.gate.open();
	QVERIFY2(waitFor([&]() { return f.idle(); }), "the job ends");
	settle(100);
	// The job from before the adoption does not write.
	QCOMPARE(f.gate.beforeWrites(), 0);
	QVERIFY(f.written.isEmpty());
	// The copy that was taken over is still the copy.
	QCOMPARE(copyName(f.root), "taken");
}

// A save is followed by an adoption while the write that the save cancelled still runs. The removal
// that the save asked for must not remove the copy that was taken over.
void TestRecoveryController::adoptAfterSaveDuringWrite() {
	Fixture f;
	{
		RecoveryStore old(f.root);
		// The old copy is written.
		QVERIFY(old.open().ok);
		QVERIFY(old.write(*makeScene("taken"), makeSource("taken")).ok);
	}
	const RecoveryScan scan = f.controller->scan();
	// The scan lists the old copy.
	QCOMPARE(scan.candidates.size(), 1);

	f.gate.closeAt(RecoveryHookPoint::BeforeWrite);
	f.controller->noteEdited(makeScene("edited"), makeSource("edited"), 1);
	QVERIFY2(waitFor([&]() { return f.gate.beforeWrites() == 1; }), "the write starts");
	f.controller->noteSaved();
	// The sleep gives adopt time to wait for the store that the write holds. No signal marks that moment,
	// so it stays fixed.
	std::thread releaser([&]() {
		QThread::msleep(100);
		f.gate.open();
	});
	const RecoveryResult adopted = f.controller->adopt(scan.candidates.first());
	releaser.join();
	QVERIFY2(adopted.ok, "adopt takes over the copy after the write ended");
	QVERIFY2(waitFor([&]() { return f.idle(); }), "the jobs end");
	settle(100);
	// The removal of the save does not remove the copy that was taken over.
	QVERIFY(f.written.isEmpty());
	QCOMPARE(copyName(f.root), "taken");
}

// The removal of the replaced scene's copy has been handed to the worker when the adoption starts, and the
// adoption bumps the epoch, so the worker skips the removal. When the store then refuses the adoption, the
// removal has to run again.
void TestRecoveryController::failedAdoptKeepsRemoval() {
	Fixture f;
	{
		RecoveryStore old(f.root);
		// The old copy is written.
		QVERIFY(old.open().ok);
		QVERIFY(old.write(*makeScene("gone"), makeSource("gone")).ok);
	}
	const RecoveryScan scan = f.controller->scan();
	// The scan lists the old copy.
	QCOMPARE(scan.candidates.size(), 1);
	const RecoveryCandidate gone = scan.candidates.first();
	QVERIFY2(f.controller->discard(gone).ok, "the old copy is discarded, so the candidate is out of date");

	f.controller->noteEdited(makeScene("edited"), makeSource("edited"), 1);
	QVERIFY2(waitFor([&]() { return f.written.size() == 1 && f.idle(); }), "the copy of the scene is written");
	// The copy is on disk.
	QCOMPARE(copyFiles(f.root).size(), 2);

	f.gate.closeAt(RecoveryHookPoint::JobStart);
	f.controller->noteReplaced();
	QVERIFY2(waitFor([&]() { return f.gate.jobStarts() == 2; }), "the worker took the removal");
	QVERIFY2(!f.controller->adopt(gone).ok, "a copy that is gone cannot be taken over");
	f.gate.open();
	QVERIFY2(waitFor([&]() { return f.idle(); }), "the jobs end");
	settle(100);
	QVERIFY2(copyFiles(f.root).isEmpty(), "the copy of the replaced scene is removed although the adoption failed");
	// Nothing else was written or reported.
	QCOMPARE(f.written, QVector<quint64>{1});
	QVERIFY(f.failures.isEmpty());
}

QTEST_GUILESS_MAIN(TestRecoveryController)
#include "test_recoverycontroller.moc"
