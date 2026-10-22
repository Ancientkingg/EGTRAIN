#pragma once

// RecoveryStore.h includes no Qt header that defines the macro "signals", because SceneModel has a
// member of that name. This header includes QObject, so a file that uses the members of SceneModel
// includes scene/SceneModel.h before it.
#include "recovery/RecoveryStore.h"

#include <QObject>
#include <QString>
#include <QTimer>
#include <QtGlobal>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

struct SceneModel;

/// A point of a job where a test can stop the worker thread. Nothing else uses a hook.
enum class RecoveryHookPoint {
	JobStart,	 ///< The worker took the job. It has not locked the store or checked for cancellation.
	BeforeWrite, ///< The job is current and the store is open and locked. The scene is not written yet.
};

/// Writes a recovery copy of a scene while the scene has unsaved edits, through a RecoveryStore.
///
/// The first edit after a write arms a single-shot timer. When it fires, the newest scene is
/// written, unless that revision is already on disk. Later edits do not move the timer, so a copy is
/// never more than one interval behind the editing. The interval is 30 seconds, or the value in
/// QEGTRAIN_RECOVERY_INTERVAL_MS when that is a positive whole number of milliseconds.
///
/// The write runs on one worker thread, because saving a bundle takes tens of milliseconds for the
/// largest committed scene and longer when the disk or a virus scanner is slow. One job runs at a
/// time. Edits that arrive while a job runs are merged: when it ends, the newest scene is written
/// once. The scene handed over is never modified, so the editing thread needs no lock.
///
/// Saving, replacing the scene and returning to the clean state each bump an epoch and remove the
/// copy. A write that is under way cannot be stopped, but its result is dropped and the removal runs
/// after it, so a late write never brings back a copy that a save removed. A write that has not
/// started when the epoch changes does not run.
///
/// A failure is reported with writeFailed and never stops the editing: nothing here blocks the
/// caller except scan, adopt, discard and shutdown, which wait for a job that is running. No
/// function throws, prints or logs.
///
/// All functions belong to the thread that owns the object, which runs the event loop. Signals are
/// emitted there. The controller holds the store alone: the store is used by one thread at a time
/// and has no static state.
class RecoveryController : public QObject {
	Q_OBJECT

public:
	/// Interval of the write timer in milliseconds when QEGTRAIN_RECOVERY_INTERVAL_MS does not set one.
	static constexpr int kDefaultIntervalMs = 30000;

	/// root is the folder of the store. Nothing is created until the first copy is written. The
	/// destructor stops the worker and keeps the copy on disk, as a crash would. testHook is called on
	/// the worker thread at the points of RecoveryHookPoint, and may be null.
	explicit RecoveryController(const QString& root, QObject* parent = nullptr, std::function<void(RecoveryHookPoint)> testHook = nullptr);
	~RecoveryController() override;
	RecoveryController(const RecoveryController&) = delete;
	RecoveryController& operator=(const RecoveryController&) = delete;

	/// The scene has unsaved edits. scene must not be changed afterwards; a null scene is ignored. revision
	/// identifies the state of the scene, and a revision that is already on disk is not written again. The
	/// newest call replaces the ones that still wait. Ignored after shutdown.
	void noteEdited(std::shared_ptr<const SceneModel> scene, const RecoverySource& source, quint64 revision);
	/// The scene is back at its saved state (Undo to the clean point). Removes the copy.
	void noteClean();
	/// The scene was saved. Removes the copy.
	void noteSaved();
	/// Another scene took its place, or the scene was discarded. Removes the copy.
	void noteReplaced();
	/// Stops the worker after the job that is running, removes the instance folder and releases the
	/// lock. Nothing is written afterwards. Ok to call twice.
	void shutdown();

	/// RecoveryStore::scan, serialised against the worker. Waits for a job that is running.
	RecoveryScan scan();
	/// Takes over the copy of a candidate as the copy of the scene that is now open. It cancels the
	/// edits and the removal that wait, like a replacement, but keeps the copy it took over, so a
	/// replacement of the previous scene belongs before it. When the store refuses the copy, the copy
	/// of the previous scene is still removed, as after a replacement. Refused after shutdown.
	RecoveryResult adopt(const RecoveryCandidate& candidate);
	/// RecoveryStore::discard, serialised against the worker. Waits for a job that is running.
	RecoveryResult discard(const RecoveryCandidate& candidate);

	/// True when no edit waits for the timer and no job is running or waiting.
	bool isIdle() const;
	/// Interval of the write timer in milliseconds.
	int intervalMs() const;

signals:
	/// A copy of the scene at this revision is on disk. Not emitted for a write that was cancelled.
	void written(quint64 revision);
	/// A write or a removal failed. The text is a fixed sentence without a path. It is emitted for
	/// the first failure of a run; any success ends the run.
	void writeFailed(const QString& message);

private slots:
	// Takes the result of the job that ended on the worker thread. It runs on the owning thread.
	void collectOutcome();

private:
	struct Request {
		std::shared_ptr<const SceneModel> scene;
		RecoverySource source;
		quint64 revision = 0;
	};
	enum class JobKind {
		Write,
		Clear,
	};
	struct Job {
		JobKind kind = JobKind::Write;
		Request request;
		quint64 epoch = 0;
	};
	struct Outcome {
		JobKind kind = JobKind::Write;
		quint64 epoch = 0;
		quint64 revision = 0;
		bool ok = false;
		QString error;
	};

	// Drops the edits that wait and starts a new epoch.
	void cancelPending();
	// cancelPending, and removes the copy.
	void cancelAndClear();
	void dispatchNext();
	bool submit(Job job);
	void reportFailure(const QString& message);
	void finishJob(const Outcome& outcome);
	void stopWorker();
	void workerLoop();
	Outcome run(const Job& job);

	// Used by the owning thread only.
	QTimer m_timer;
	std::optional<Request> m_pending; // The newest edit that is not handed over.
	bool m_due = false;				  // The timer fired, so m_pending is to be written.
	bool m_busy = false;			  // A job is with the worker.
	bool m_clearWanted = false;
	bool m_recordMayExist = false;
	bool m_hasWritten = false; // A copy of m_writtenRevision is on disk.
	quint64 m_writtenRevision = 0;
	bool m_failing = false;
	bool m_closed = false;

	std::atomic<quint64> m_epoch{0};
	const std::function<void(RecoveryHookPoint)> m_testHook;

	std::mutex m_storeMutex;
	RecoveryStore m_store; // Used with m_storeMutex held.

	std::mutex m_queueMutex;
	std::condition_variable m_wake;
	std::optional<Job> m_job;		  // The job the worker takes next. Used with m_queueMutex held.
	std::optional<Outcome> m_outcome; // The result the owning thread has not taken yet. Used with m_queueMutex held.
	bool m_stopping = false;		  // Used with m_queueMutex held.
	std::thread m_worker;
};
