#include "telemetry/TelemetryQueue.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <climits>
#include <algorithm>

namespace telemetry {
namespace {
constexpr qint64 kCategoryBytes = 192 * 1024;
constexpr qint64 kControlBytes = 64 * 1024;
constexpr qint64 kDiskBytes = 1024 * 1024;
constexpr qint64 kRecordBytes = 1024;
constexpr qint64 kLifetimeMs = 7LL * 24 * 60 * 60 * 1000;
QString filename(Category category) { return category == Category::Usage ? QStringLiteral("usage.json") : QStringLiteral("diagnostics.json"); }
bool utc(const QDateTime& time) { return validUtc(time); }
bool keys(const QJsonObject& obj, std::initializer_list<const char*> expected) {
    if (obj.size() != static_cast<int>(expected.size())) return false;
    for (const char* key : expected) if (!obj.contains(QLatin1String(key))) return false;
    return true;
}
bool validRetry(const RetryState& state) {
    return state.failures >= 0 && state.failures <= 31 &&
        state.usageBatchLimit >= 1 && state.usageBatchLimit <= 50 &&
        state.diagnosticsBatchLimit >= 1 && state.diagnosticsBatchLimit <= 50 &&
        (!state.nextEligibleUtc.isValid() || utc(state.nextEligibleUtc));
}
}
TelemetryQueue::~TelemetryQueue() { close(); }
QVector<TelemetryQueue::Stored>& TelemetryQueue::records(Category category) {
    return category == Category::Usage ? m_usage : m_diagnostics;
}
const QVector<TelemetryQueue::Stored>& TelemetryQueue::records(Category category) const {
    return category == Category::Usage ? m_usage : m_diagnostics;
}
bool TelemetryQueue::checkSpace(qint64 incomingBytes) const {
    QDirIterator entries(m_dir, QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    if (incomingBytes < 0 || incomingBytes > kDiskBytes) return false;
    qint64 size = incomingBytes;
    int count = 0;
    while (entries.hasNext()) {
        entries.next();
        const QFileInfo entry = entries.fileInfo();
        if (++count > 32 || entry.isSymLink() || !entry.isFile() || entry.size() < 0 || entry.size() > kDiskBytes - size) return false;
        size += entry.size();
    }
    return true;
}
bool TelemetryQueue::fail() {
    m_failed = true;
    m_usage.clear(); m_diagnostics.clear();
    // Best effort marker. A storage error must never turn into an upload attempt.
    if (m_owner && !m_dir.isEmpty() && !QFile::exists(m_dir + QStringLiteral("/disabled")) && checkSpace(9)) {
        QSaveFile marker(m_dir + QStringLiteral("/disabled"));
        marker.setDirectWriteFallback(false);
        if (marker.open(QIODevice::WriteOnly)) { marker.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner); marker.write("disabled\n"); marker.commit(); }
    }
    return false;
}
bool TelemetryQueue::replace(const QString& file, const QByteArray& data, qint64 limit) {
    // QSaveFile's temporary payload coexists with the old file and any orphan leftovers.
    if (!m_owner || m_failed || data.size() > limit || !checkSpace(data.size())) return fail();
#ifdef EGTRAIN_TELEMETRY_TEST_HOOK
    if (m_failNextWrite) { m_failNextWrite = false; return fail(); }
#endif
    QSaveFile output(m_dir + QLatin1Char('/') + file);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly) || !output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
        output.write(data) != data.size() || !output.commit() || !checkSpace()) return fail();
    return true;
}
bool TelemetryQueue::loadControl() {
    QFile file(m_dir + QStringLiteral("/control.json"));
    if (!file.exists()) return true;
    if (file.size() > kControlBytes || !file.open(QIODevice::ReadOnly)) return false;
    const QJsonDocument doc = QJsonDocument::fromJson(file.read(kControlBytes + 1));
    if (!doc.isObject()) return false;
    const auto obj = doc.object();
    if (!keys(obj, {"format", "retired", "last_seen", "retry", "endpoint", "terms"}) ||
        obj.value(QStringLiteral("format")).toInt(-1) != 1 || !obj.value(QStringLiteral("retired")).isArray() ||
        !obj.value(QStringLiteral("retry")).isObject() || !obj.value(QStringLiteral("endpoint")).isString() ||
        !obj.value(QStringLiteral("terms")).isString() || !obj.value(QStringLiteral("last_seen")).isString()) return false;
    for (const auto& value : obj.value(QStringLiteral("retired")).toArray()) {
        if (!value.isString() || value.toString().toUtf8().size() > 2048 || m_retired.contains(value.toString()) || m_retired.size() >= 16) return false;
        m_retired.append(value.toString());
    }
    const QString last = obj.value(QStringLiteral("last_seen")).toString();
    if (!last.isEmpty()) { m_lastSeen = QDateTime::fromString(last, Qt::ISODateWithMs); if (!utc(m_lastSeen) || !last.endsWith(QLatin1Char('Z'))) return false; }
    m_lastCheckpoint = m_lastSeen;
    const auto retry = obj.value(QStringLiteral("retry")).toObject();
    if (!keys(retry, {"failures", "next", "usage_limit", "diagnostics_limit"}) ||
        !retry.value(QStringLiteral("failures")).isDouble() || !retry.value(QStringLiteral("usage_limit")).isDouble() ||
        !retry.value(QStringLiteral("diagnostics_limit")).isDouble() || !retry.value(QStringLiteral("next")).isString()) return false;
    m_retry.failures = retry.value(QStringLiteral("failures")).toInt(-1);
    m_retry.usageBatchLimit = retry.value(QStringLiteral("usage_limit")).toInt(-1);
    m_retry.diagnosticsBatchLimit = retry.value(QStringLiteral("diagnostics_limit")).toInt(-1);
    const QString next = retry.value(QStringLiteral("next")).toString();
    if (!next.isEmpty()) { m_retry.nextEligibleUtc = QDateTime::fromString(next, Qt::ISODateWithMs); if (!utc(m_retry.nextEligibleUtc) || !next.endsWith(QLatin1Char('Z'))) return false; }
    if (!validRetry(m_retry)) return false;
    // A changed receiver discards retry state but never the retirement ledger.
    if (obj.value(QStringLiteral("endpoint")).toString() != m_endpoint || obj.value(QStringLiteral("terms")).toString() != m_terms) m_retry = {};
    return true;
}
bool TelemetryQueue::saveControl() {
    QJsonArray retired;
    for (const auto& endpoint : m_retired) retired.append(endpoint);
    QJsonObject retry{{QStringLiteral("failures"), m_retry.failures},
                      {QStringLiteral("next"), m_retry.nextEligibleUtc.isValid() ? m_retry.nextEligibleUtc.toUTC().toString(Qt::ISODateWithMs) : QString()},
                      {QStringLiteral("usage_limit"), m_retry.usageBatchLimit}, {QStringLiteral("diagnostics_limit"), m_retry.diagnosticsBatchLimit}};
    QJsonObject obj{{QStringLiteral("format"), 1}, {QStringLiteral("endpoint"), m_endpoint}, {QStringLiteral("terms"), m_terms},
                    {QStringLiteral("retired"), retired}, {QStringLiteral("retry"), retry},
                    {QStringLiteral("last_seen"), m_lastSeen.isValid() ? m_lastSeen.toUTC().toString(Qt::ISODateWithMs) : QString()}};
    if (!replace(QStringLiteral("control.json"), QJsonDocument(obj).toJson(QJsonDocument::Compact), kControlBytes)) return false;
    m_lastCheckpoint = m_lastSeen;
    return true;
}
bool TelemetryQueue::loadCategory(Category category) {
    QFile file(m_dir + QLatin1Char('/') + filename(category));
    if (!file.exists()) return true;
    if (file.size() > kCategoryBytes || !file.open(QIODevice::ReadOnly)) return false;
    const auto doc = QJsonDocument::fromJson(file.read(kCategoryBytes + 1));
    if (!doc.isObject()) return false;
    const auto obj = doc.object();
    if (!keys(obj, {"format", "endpoint", "terms", "events"}) || obj.value(QStringLiteral("format")).toInt(-1) != 1 ||
        !obj.value(QStringLiteral("endpoint")).isString() || !obj.value(QStringLiteral("terms")).isString() ||
        !obj.value(QStringLiteral("events")).isArray()) return false;
    if (obj.value(QStringLiteral("endpoint")).toString() != m_endpoint || obj.value(QStringLiteral("terms")).toString() != m_terms)
        return saveCategory(category);
    const auto array = obj.value(QStringLiteral("events")).toArray();
    if (array.size() > 512) return false;
    QSet<QString> ids;
    for (const auto& value : array) {
        if (!value.isObject()) return false;
        const auto item = value.toObject();
        if (!keys(item, {"stamp", "event"}) || !item.value(QStringLiteral("stamp")).isString() || !item.value(QStringLiteral("event")).isObject() ||
            QJsonDocument(item).toJson(QJsonDocument::Compact).size() > kRecordBytes) return false;
        Stored stored;
        stored.stamp = item.value(QStringLiteral("stamp")).toString();
        if (category == Category::Usage ? !validUuid(stored.stamp) : stored.stamp.isEmpty()) return false;
        if (category == Category::Diagnostics) {
            bool ok = false; const int generation = stored.stamp.toInt(&ok);
            if (!ok || generation < 0 || generation == INT_MAX || QString::number(generation) != stored.stamp) return false;
        }
        if (!readEvent(item.value(QStringLiteral("event")).toObject(), category, &stored.event) || ids.contains(stored.event.id)) return false;
        ids.insert(stored.event.id); records(category).append(stored);
    }
    return true;
}
bool TelemetryQueue::saveCategory(Category category) {
    QJsonArray array;
    for (const auto& stored : records(category)) array.append(QJsonObject{{QStringLiteral("stamp"), stored.stamp},
        {QStringLiteral("event"), eventObject(stored.event, category)}});
    const QJsonObject obj{{QStringLiteral("format"), 1}, {QStringLiteral("endpoint"), m_endpoint},
                          {QStringLiteral("terms"), m_terms}, {QStringLiteral("events"), array}};
    return replace(filename(category), QJsonDocument(obj).toJson(QJsonDocument::Compact), kCategoryBytes);
}
TelemetryQueue::OpenResult TelemetryQueue::open(const QString& privateDirectory, const QString& exactEndpoint,
                                                   const QString& terms, const QDateTime& now) {
    close(); m_failed = false; m_retired.clear(); m_retry = {}; m_lastSeen = {}; m_lastCheckpoint = {};
    if (!utc(now) || exactEndpoint.toUtf8().size() > 2048 || exactEndpoint.isEmpty() || terms.isEmpty() || terms.toUtf8().size() > 64 ||
        privateDirectory.isEmpty() || QFileInfo(privateDirectory).isSymLink()) return OpenResult::Disabled;
    m_dir = privateDirectory; m_endpoint = exactEndpoint; m_terms = terms;
    if (!QDir().mkpath(m_dir) || !QFile::setPermissions(m_dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner)) return OpenResult::Disabled;
    m_lock.reset(new QLockFile(m_dir + QStringLiteral("/owner.lock")));
    m_lock->setStaleLockTime(0);
    if (!m_lock->tryLock(0)) { m_lock.reset(); return OpenResult::Follower; }
    m_owner = true;
    m_ownershipId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    // Never interpret deleted control state as an empty retirement ledger if
    // category data already exists in this directory.
    const bool missingControlWithData = !QFile::exists(m_dir + QStringLiteral("/control.json")) &&
        (QFile::exists(m_dir + QStringLiteral("/usage.json")) || QFile::exists(m_dir + QStringLiteral("/diagnostics.json")));
    if (QFile::exists(m_dir + QStringLiteral("/disabled"))) {
        // Recovery only discards payloads. Never erase the disabled marker or retirement ledger.
        QFile::remove(m_dir + QStringLiteral("/usage.json"));
        QFile::remove(m_dir + QStringLiteral("/diagnostics.json"));
        fail(); return OpenResult::Disabled;
    }
    if (!checkSpace() || missingControlWithData || !loadControl()) { fail(); return OpenResult::Disabled; }
    if (isRetired(m_endpoint)) {
        // Retirement was committed before category replacement. Complete that
        // interrupted transaction on every reopen while holding ownership.
        if (!purgeEndpointData()) return OpenResult::Disabled;
        return OpenResult::Retired;
    }
    for (Category category : {Category::Usage, Category::Diagnostics}) {
        if (!loadCategory(category)) { records(category).clear(); if (!saveCategory(category)) return OpenResult::Disabled; }
    }
    if (!prune(now) || !saveControl()) return OpenResult::Disabled;
    return OpenResult::Owner;
}
bool TelemetryQueue::prune(const QDateTime& now) {
    if (!healthy() || !utc(now)) return fail();
    const bool rollback = m_lastSeen.isValid() && now < m_lastSeen;
    for (Category category : {Category::Usage, Category::Diagnostics}) {
        auto& list = records(category);
        const auto before = list.size();
        for (int i = list.size() - 1; i >= 0; --i)
            if (rollback || list[i].event.occurredAt > now || list[i].event.occurredAt.msecsTo(now) >= kLifetimeMs) list.removeAt(i);
        if (before != list.size() && !saveCategory(category)) return false;
    }
    if (rollback) {
        // Preserve a conservative cooldown after UTC rollback; sender additionally
        // maintains its monotonic deadline while this process is running.
        m_retry.nextEligibleUtc = now.addSecs(24 * 60 * 60);
    }
    if (!m_lastSeen.isValid() || now > m_lastSeen || rollback) m_lastSeen = now;
    // Persist a last-seen checkpoint at most once a minute during quiet polls.
    // Writes by commitRetry/open/retirement also checkpoint the current time.
    return (rollback || !m_lastCheckpoint.isValid() || m_lastCheckpoint.msecsTo(now) >= 60000)
        ? saveControl() : true;
}
QDateTime TelemetryQueue::nextExpiryUtc() const {
    if (!healthy()) return {};
    QDateTime earliest;
    for (Category category : {Category::Usage, Category::Diagnostics})
        for (const auto& item : records(category))
            if (!earliest.isValid() || item.event.occurredAt < earliest) earliest = item.event.occurredAt;
    return earliest.isValid() ? earliest.addMSecs(kLifetimeMs) : QDateTime();
}
bool TelemetryQueue::enqueueUsage(const UsageRecord& record, const QDateTime& now) {
    if (!healthy() || !validUuid(record.installationId) || eventObject(record.event, Category::Usage).isEmpty() ||
        !utc(now) || record.event.occurredAt > now || record.event.occurredAt.msecsTo(now) >= kLifetimeMs) return false;
    if (!prune(now)) return false;
    auto& list = m_usage;
    for (const auto& item : list) if (item.event.id == record.event.id) return false;
    Stored item{record.event, record.installationId};
    if (QJsonDocument(QJsonObject{{QStringLiteral("stamp"), item.stamp}, {QStringLiteral("event"), eventObject(item.event, Category::Usage)}}).toJson(QJsonDocument::Compact).size() > kRecordBytes) return false;
    list.append(item);
    std::stable_sort(list.begin(), list.end(), [](const Stored& left, const Stored& right) {
        return left.event.occurredAt < right.event.occurredAt;
    });
    while (list.size() > 512) list.removeFirst();
    while (!list.isEmpty()) {
        QJsonArray array;
        for (const auto& entry : list) array.append(QJsonObject{{QStringLiteral("stamp"), entry.stamp},
            {QStringLiteral("event"), eventObject(entry.event, Category::Usage)}});
        if (QJsonDocument(QJsonObject{{QStringLiteral("format"), 1}, {QStringLiteral("endpoint"), m_endpoint},
            {QStringLiteral("terms"), m_terms}, {QStringLiteral("events"), array}}).toJson(QJsonDocument::Compact).size() <= kCategoryBytes) break;
        list.removeFirst();
    }
    return saveCategory(Category::Usage);
}
bool TelemetryQueue::enqueueDiagnostic(const DiagnosticRecord& record, const QDateTime& now) {
    if (!healthy() || record.generation < 0 || record.generation == INT_MAX || eventObject(record.event, Category::Diagnostics).isEmpty() ||
        !utc(now) || record.event.occurredAt > now || record.event.occurredAt.msecsTo(now) >= kLifetimeMs) return false;
    if (!prune(now)) return false;
    auto& list = m_diagnostics;
    for (const auto& item : list) if (item.event.id == record.event.id) return false;
    Stored item{record.event, QString::number(record.generation)};
    if (QJsonDocument(QJsonObject{{QStringLiteral("stamp"), item.stamp}, {QStringLiteral("event"), eventObject(item.event, Category::Diagnostics)}}).toJson(QJsonDocument::Compact).size() > kRecordBytes) return false;
    list.append(item);
    std::stable_sort(list.begin(), list.end(), [](const Stored& left, const Stored& right) {
        return left.event.occurredAt < right.event.occurredAt;
    });
    while (list.size() > 512) list.removeFirst();
    while (!list.isEmpty()) {
        QJsonArray array;
        for (const auto& entry : list) array.append(QJsonObject{{QStringLiteral("stamp"), entry.stamp},
            {QStringLiteral("event"), eventObject(entry.event, Category::Diagnostics)}});
        if (QJsonDocument(QJsonObject{{QStringLiteral("format"), 1}, {QStringLiteral("endpoint"), m_endpoint},
            {QStringLiteral("terms"), m_terms}, {QStringLiteral("events"), array}}).toJson(QJsonDocument::Compact).size() <= kCategoryBytes) break;
        list.removeFirst();
    }
    return saveCategory(Category::Diagnostics);
}
bool TelemetryQueue::purgeCategory(Category category) {
    if (!healthy()) return false;
    records(category).clear(); return saveCategory(category);
}
bool TelemetryQueue::purgeOtherStamps(Category category, const QString& currentStamp) {
    if (!healthy()) return false;
    if (category == Category::Usage) {
        if (!validUuid(currentStamp)) return false;
    } else if (category == Category::Diagnostics) {
        bool ok = false;
        const int generation = currentStamp.toInt(&ok);
        if (!ok || generation < 0 || generation == INT_MAX
            || QString::number(generation) != currentStamp) return false;
    } else return false;
    auto& list = records(category);
    const int oldSize = list.size();
    for (int i = list.size() - 1; i >= 0; --i)
        if (list[i].stamp != currentStamp) list.removeAt(i);
    return list.size() == oldSize || saveCategory(category);
}
bool TelemetryQueue::purgeEndpointData() {
    return purgeCategory(Category::Usage) && purgeCategory(Category::Diagnostics);
}
PreparedBatch TelemetryQueue::prepareBatch(Category category, const QString& stamp, const Application& app,
                                            const QDateTime& now, int maxEvents) const {
    PreparedBatch result;
    if (!healthy() || !utc(now) || maxEvents < 1 || !validApplication(app) ||
        (category == Category::Usage ? !validUuid(stamp) : stamp.isEmpty())) return result;
    result.token.category = category; result.token.stamp = stamp; result.token.ownershipId = m_ownershipId;
    QVector<TelemetryEvent> selected;
    const int cap = qMin(50, qMin(maxEvents, category == Category::Usage ? m_retry.usageBatchLimit : m_retry.diagnosticsBatchLimit));
    for (const auto& item : records(category)) {
        if (item.stamp != stamp || item.event.occurredAt > now || item.event.occurredAt.msecsTo(now) >= kLifetimeMs) continue;
        if (selected.size() >= cap) break;
        selected.append(item.event);
        const auto bytes = batch(selected, category, category == Category::Usage ? stamp : QString(), app, now);
        if (bytes.isEmpty()) { selected.removeLast(); break; }
        result.bytes = bytes; result.token.ids.append(item.event.id);
    }
    return result;
}
bool TelemetryQueue::discardBatch(const BatchToken& token) {
    if (!healthy() || token.ownershipId != m_ownershipId || token.ids.isEmpty() || token.ids.size() > 50) return false;
    auto& list = records(token.category);
    for (int i = list.size() - 1; i >= 0; --i)
        if (list[i].stamp == token.stamp && token.ids.contains(list[i].event.id)) list.removeAt(i);
    return saveCategory(token.category);
}
bool TelemetryQueue::commitRetry(const RetryState& state, const QDateTime& now) {
    if (!healthy() || !validRetry(state) || !utc(now)) return false;
    m_retry = state;
    if (!m_lastSeen.isValid() || now > m_lastSeen) m_lastSeen = now;
    return saveControl();
}
bool TelemetryQueue::isRetired(const QString& endpoint) const { return m_retired.contains(endpoint); }
bool TelemetryQueue::retireExactEndpoint(const QString& endpoint) {
    if (!healthy() || endpoint != m_endpoint || endpoint.toUtf8().size() > 2048) return false;
    if (!isRetired(endpoint)) {
        if (m_retired.size() >= 16) return fail();
        m_retired.append(endpoint);
    }
    if (!saveControl()) return false;
    return purgeEndpointData();
}
void TelemetryQueue::close() {
    m_owner = false; m_ownershipId.clear(); m_usage.clear(); m_diagnostics.clear();
    if (m_lock) { if (m_lock->isLocked()) m_lock->unlock(); m_lock.reset(); }
}
}
