#include "scene/SceneModel.h"

#include "recovery/RecoveryController.h"

#include <QByteArray>
#include <QMetaObject>

#include <utility>

namespace {

int intervalFromEnvironment() {
	bool ok = false;
	const int value = qgetenv("QEGTRAIN_RECOVERY_INTERVAL_MS").toInt(&ok, 10);
	return ok && value > 0 ? value : RecoveryController::kDefaultIntervalMs;
}

RecoveryResult refusal(const char* sentence) {
	RecoveryResult result;
	result.error = QString::fromLatin1(sentence);
	return result;
}

} // namespace

RecoveryController::RecoveryController(const QString& root, QObject* parent, std::function<void(RecoveryHookPoint)> testHook)
	: QObject(parent), m_testHook(std::move(testHook)), m_store(root) {
	m_timer.setSingleShot(true);
	m_timer.setInterval(intervalFromEnvironment());
	connect(&m_timer, &QTimer::timeout, this, [this]() {
		m_due = true;
		dispatchNext();
	});
}

RecoveryController::~RecoveryController() {
	stopWorker();
}

int RecoveryController::intervalMs() const {
	return m_timer.interval();
}

bool RecoveryController::isIdle() const {
	return !m_busy && !m_clearWanted && !m_pending;
}

void RecoveryController::noteEdited(std::shared_ptr<const SceneModel> scene, const RecoverySource& source, quint64 revision) {
	if (m_closed || !scene)
		return;
	Request request;
	request.scene = std::move(scene);
	request.source = source;
	request.revision = revision;
	m_pending = std::move(request);
	if (!m_timer.isActive())
		m_timer.start();
}

void RecoveryController::noteClean() {
	cancelAndClear();
}

void RecoveryController::noteSaved() {
	cancelAndClear();
}

void RecoveryController::noteReplaced() {
	cancelAndClear();
}

void RecoveryController::cancelPending() {
	m_epoch.fetch_add(1);
	m_timer.stop();
	m_pending.reset();
	m_due = false;
	m_hasWritten = false;
}

void RecoveryController::cancelAndClear() {
	if (m_closed)
		return;
	cancelPending();
	// A job that is running may be a write whose copy appears after this call.
	if (m_busy || m_recordMayExist)
		m_clearWanted = true;
	dispatchNext();
}

void RecoveryController::shutdown() {
	if (m_closed)
		return;
	m_closed = true;
	m_timer.stop();
	m_pending.reset();
	m_busy = false;
	m_clearWanted = false;
	m_epoch.fetch_add(1);
	stopWorker();
	const std::lock_guard<std::mutex> lock(m_storeMutex);
	m_store.close();
}

RecoveryScan RecoveryController::scan() {
	const std::lock_guard<std::mutex> lock(m_storeMutex);
	return m_store.scan();
}

RecoveryResult RecoveryController::adopt(const RecoveryCandidate& candidate) {
	if (m_closed)
		return refusal("The recovery controller is shut down");
	// A write that waits for the store must not replace the copy that is taken over, and a removal
	// that waits must not remove it.
	cancelPending();
	const bool removalWanted = m_clearWanted;
	m_clearWanted = false;
	RecoveryResult result;
	{
		const std::lock_guard<std::mutex> lock(m_storeMutex);
		result = m_store.open();
		if (result.ok)
			result = m_store.adopt(candidate);
	}
	if (result.ok) {
		m_recordMayExist = true;
	} else {
		// A removal that was skipped because of the new epoch must run again, as after a replacement.
		m_clearWanted = removalWanted || m_recordMayExist;
		dispatchNext();
	}
	return result;
}

RecoveryResult RecoveryController::discard(const RecoveryCandidate& candidate) {
	const std::lock_guard<std::mutex> lock(m_storeMutex);
	return m_store.discard(candidate);
}

void RecoveryController::dispatchNext() {
	if (m_busy || m_closed)
		return;
	Job job;
	if (m_clearWanted) {
		m_clearWanted = false;
		job.kind = JobKind::Clear;
	} else if (m_due && m_pending) {
		job.kind = JobKind::Write;
		job.request = std::move(*m_pending);
		m_pending.reset();
		m_due = false;
		if (m_hasWritten && job.request.revision == m_writtenRevision)
			return;
		m_recordMayExist = true;
	} else {
		return;
	}
	job.epoch = m_epoch.load();
	m_busy = true;
	if (!submit(std::move(job))) {
		m_busy = false;
		reportFailure(QStringLiteral("Cannot start the recovery thread"));
	}
}

bool RecoveryController::submit(Job job) {
	try {
		if (!m_worker.joinable())
			m_worker = std::thread([this]() { workerLoop(); });
		{
			const std::lock_guard<std::mutex> lock(m_queueMutex);
			m_job = std::move(job);
		}
		m_wake.notify_one();
		return true;
	} catch (...) {
		return false;
	}
}

void RecoveryController::reportFailure(const QString& message) {
	if (m_failing)
		return;
	m_failing = true;
	emit writeFailed(message);
}

void RecoveryController::collectOutcome() {
	std::optional<Outcome> outcome;
	{
		const std::lock_guard<std::mutex> lock(m_queueMutex);
		outcome.swap(m_outcome);
	}
	if (outcome)
		finishJob(*outcome);
}

void RecoveryController::finishJob(const Outcome& outcome) {
	if (m_closed)
		return;
	m_busy = false;
	// The result of a job from an earlier epoch is dropped: the job did not run, or what it did is
	// removed by the job that follows.
	if (outcome.epoch == m_epoch.load()) {
		if (!outcome.ok) {
			reportFailure(outcome.error);
		} else {
			m_failing = false;
			if (outcome.kind == JobKind::Clear) {
				m_recordMayExist = false;
			} else {
				m_hasWritten = true;
				m_writtenRevision = outcome.revision;
				emit written(outcome.revision);
			}
		}
	}
	dispatchNext();
}

void RecoveryController::stopWorker() {
	{
		const std::lock_guard<std::mutex> lock(m_queueMutex);
		m_stopping = true;
		m_job.reset();
	}
	m_wake.notify_all();
	if (m_worker.joinable())
		m_worker.join();
}

void RecoveryController::workerLoop() {
	std::unique_lock<std::mutex> lock(m_queueMutex);
	for (;;) {
		m_wake.wait(lock, [this]() { return m_stopping || m_job.has_value(); });
		if (m_stopping)
			return;
		const Job job = std::move(*m_job); // NOLINT(bugprone-unchecked-optional-access): the wait returned with a job or stopping
		m_job.reset();
		lock.unlock();
		Outcome outcome;
		try {
			outcome = run(job);
		} catch (...) {
			outcome.kind = job.kind;
			outcome.epoch = job.epoch;
			outcome.revision = job.request.revision;
			outcome.error = QStringLiteral("Cannot write the recovery copy");
		}
		lock.lock();
		m_outcome = std::move(outcome);
		QMetaObject::invokeMethod(this, "collectOutcome", Qt::QueuedConnection);
	}
}

RecoveryController::Outcome RecoveryController::run(const Job& job) {
	Outcome outcome;
	outcome.kind = job.kind;
	outcome.epoch = job.epoch;
	outcome.revision = job.request.revision;
	if (m_testHook)
		m_testHook(RecoveryHookPoint::JobStart);
	const std::lock_guard<std::mutex> lock(m_storeMutex);
	RecoveryResult result;
	result.ok = true;
	// A job from an earlier epoch does nothing: the removal that follows it, or the adoption that
	// ended its epoch, has dealt with the copy.
	if (m_epoch.load() == job.epoch) {
		if (job.kind == JobKind::Clear) {
			if (m_store.isOpen())
				result = m_store.clear();
		} else {
			result = m_store.open();
			if (result.ok) {
				if (m_testHook)
					m_testHook(RecoveryHookPoint::BeforeWrite);
				result = m_store.write(*job.request.scene, job.request.source);
			}
		}
	}
	outcome.ok = result.ok;
	outcome.error = result.error;
	return outcome;
}
