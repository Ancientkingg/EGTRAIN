#pragma once

#include "telemetry/TelemetryEvent.h"
#include <QLockFile>
#include <memory>

namespace telemetry {
struct RetryState {
	int failures = 0;
	QDateTime nextEligibleUtc;
	int usageBatchLimit = 50;
	int diagnosticsBatchLimit = 50;
};
struct BatchToken {
	Category category = Category::Usage;
	QString stamp; // Usage UUID, or decimal diagnostics generation; never transmitted for diagnostics.
	QVector<QString> ids;
	QString ownershipId; // Changes on every successful ownership acquisition.
};
struct PreparedBatch {
	QByteArray bytes;
	BatchToken token;
};

// One worker owns this object and all methods. An unsuccessful storage operation
// fails closed; the caller must close ingress and abandon outstanding RAM work.
class TelemetryQueue {
public:
	enum class OpenResult { Owner,
		Follower,
		Retired,
		Disabled };
	TelemetryQueue() = default;
	~TelemetryQueue();
	OpenResult open(const QString& privateDirectory, const QString& exactEndpoint, const QString& terms,
		const QDateTime& now);
	bool enqueueUsage(const UsageRecord& record, const QDateTime& now);
	bool enqueueDiagnostic(const DiagnosticRecord& record, const QDateTime& now);
	bool purgeCategory(Category category);
	bool purgeOtherStamps(Category category, const QString& currentStamp);
	bool purgeEndpointData();
	bool prune(const QDateTime& now);
	QDateTime nextExpiryUtc() const; // Invalid if neither category has records.
	PreparedBatch prepareBatch(Category category, const QString& stamp, const Application& application,
		const QDateTime& now, int maxEvents = 50) const;
	bool discardBatch(const BatchToken& token);
	RetryState retryState() const { return m_retry; }
	bool commitRetry(const RetryState& state, const QDateTime& now);
	bool retireExactEndpoint(const QString& endpoint);
	bool isRetired(const QString& endpoint) const;
	bool healthy() const { return m_owner && !m_failed; }
#ifdef EGTRAIN_TELEMETRY_TEST_HOOK
	void failNextWriteForTesting() { m_failNextWrite = true; }
#endif
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
