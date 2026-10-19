#pragma once

#include "telemetry/TelemetryEvent.h"
#include <QLockFile>
#include <memory>

namespace telemetry {
/// Retry state shared by both categories, stored in control.json.
struct RetryState {
	int failures = 0;				///< Consecutive retryable failures, counting the attempt in progress; 0 to 31.
	QDateTime nextEligibleUtc;		///< No batch is sent before this UTC time; invalid when there is no wait.
	int usageBatchLimit = 50;		///< Most events in one usage batch, 1 to 50. A 413 response lowers it.
	int diagnosticsBatchLimit = 50; ///< Most events in one diagnostics batch, 1 to 50. A 413 response lowers it.
};
/// The events of a prepared batch, for discardBatch() once the receiver gave a final answer.
struct BatchToken {
	Category category = Category::Usage; ///< Category of the batch.
	QString stamp;						 ///< Usage UUID, or decimal diagnostics generation; never transmitted for diagnostics.
	QVector<QString> ids;				 ///< Event IDs in the batch.
	QString ownershipId;				 ///< Changes on every successful ownership acquisition.
};
/// A batch ready to post.
struct PreparedBatch {
	QByteArray bytes; ///< Wire body; empty when there is nothing to send.
	BatchToken token; ///< The events in bytes.
};

/// The persistent queue of usage and diagnostic events for one exact receiver endpoint and terms
/// version, in a private directory (usage.json, diagnostics.json, control.json and owner.lock).
/// Only the process that holds owner.lock uses the files. Each category holds at most 512 events and
/// 192 KiB, one stored event at most 1 KiB, and the directory at most 1 MiB in 32 files. The oldest
/// events give way first when a category is full; going over the directory limit is a storage
/// failure. An event expires seven days after it occurred. All times are UTC.
///
/// One worker owns this object and all methods. An unsuccessful storage operation fails closed; the
/// caller must close ingress and abandon outstanding RAM work. It also tries to leave a disabled
/// marker file, after which every later open() of the directory discards the queued events and
/// returns Disabled.
class TelemetryQueue {
public:
	/// Result of open().
	enum class OpenResult { Owner, ///< This object holds owner.lock and has loaded the queue.
		Follower,				   ///< Another process holds owner.lock; nothing is stored here.
		Retired,				   ///< The endpoint was retired after a 410 response; its events are purged.
		Disabled				   ///< Invalid arguments, an unusable directory, a disabled marker or a storage failure.
	};
	TelemetryQueue() = default;
	/// Calls close().
	~TelemetryQueue();
	/// Closes any earlier state, creates privateDirectory (owner-only on macOS and Linux; on Windows it
	/// keeps the permissions of its parent folder) and takes owner.lock without waiting. exactEndpoint
	/// may have at most 2048 UTF-8 bytes and terms at most 64. Stored events of another endpoint or
	/// terms, and their retry state, are dropped; the list of retired endpoints is kept. Expired events
	/// are pruned at now.
	OpenResult open(const QString& privateDirectory, const QString& exactEndpoint, const QString& terms,
		const QDateTime& now);
	/// Stores a usage event under its installation ID. Returns false for an invalid or duplicate event,
	/// one dated after now or seven days or more before it, one over 1 KiB, or a failed write; only
	/// healthy() tells a storage failure apart.
	bool enqueueUsage(const UsageRecord& record, const QDateTime& now);
	/// Stores a diagnostic event under its generation, as enqueueUsage() does.
	bool enqueueDiagnostic(const DiagnosticRecord& record, const QDateTime& now);
	/// Removes every event of category.
	bool purgeCategory(Category category);
	/// Removes the events of category stored under another stamp: an installation UUID for usage, a
	/// decimal generation for diagnostics.
	bool purgeOtherStamps(Category category, const QString& currentStamp);
	/// Removes the events of both categories.
	bool purgeEndpointData();
	/// Removes expired events and those dated after now. When now is before the last time seen, the
	/// clock moved back: every event is removed and no batch is sent for 24 hours. Saves the last time
	/// seen when a minute has passed since it was saved. Fails closed when not healthy or now is not UTC.
	bool prune(const QDateTime& now);
	/// When the oldest queued event expires; invalid when there is none or the queue is not healthy.
	QDateTime nextExpiryUtc() const;
	/// The wire body of the oldest unexpired events of category stored under stamp, prepared at now.
	/// It holds at most maxEvents, 50 and the batch limit of the retry state, and at most 65,536 bytes.
	/// For usage stamp is the installation ID sent with the batch. Nothing is removed.
	PreparedBatch prepareBatch(Category category, const QString& stamp, const Application& application,
		const QDateTime& now, int maxEvents = 50) const;
	/// Removes the events of a batch. Refused for a token from an earlier ownership, or with no or more
	/// than 50 IDs.
	bool discardBatch(const BatchToken& token);
	/// The current retry state.
	RetryState retryState() const { return m_retry; }
	/// Validates and stores state, and moves the last time seen forward to now.
	bool commitRetry(const RetryState& state, const QDateTime& now);
	/// Adds endpoint, which must be the open one, to the persistent list of retired endpoints (at most
	/// 16; a full list fails closed) and purges its events.
	bool retireExactEndpoint(const QString& endpoint);
	/// True when endpoint is on the list of retired endpoints.
	bool isRetired(const QString& endpoint) const;
	/// True while this object holds owner.lock and no storage operation has failed.
	bool healthy() const { return m_owner && !m_failed; }
#ifdef EGTRAIN_TELEMETRY_TEST_HOOK
	/// Test hook: the next file write fails as a storage failure would.
	void failNextWriteForTesting() { m_failNextWrite = true; }
#endif
	/// Releases owner.lock and drops the events held in memory. The files stay.
	void close();
private:
	struct Stored {
		TelemetryEvent event;
		QString stamp;
	};
	bool saveCategory(Category category);
	bool saveControl();
	bool replace(const QString& file, const QByteArray& bytes, qint64 limit);
	bool fail();
	bool checkSpace(qint64 incomingBytes = 0) const;
	bool loadCategory(Category category);
	bool loadControl();
	QVector<Stored>& records(Category category);
	const QVector<Stored>& records(Category category) const;
	QString m_dir, m_endpoint, m_terms, m_ownershipId;
	std::unique_ptr<QLockFile> m_lock;
	QVector<Stored> m_usage, m_diagnostics;
	QVector<QString> m_retired;
	RetryState m_retry;
	QDateTime m_lastSeen, m_lastCheckpoint;
	bool m_owner = false, m_failed = false;
#ifdef EGTRAIN_TELEMETRY_TEST_HOOK
	bool m_failNextWrite = false;
#endif
};
}
