#include "scene/SceneBundle.h"
#include "scene/SceneCompatibility.h"
#include "scene/SceneModel.h"

#include "recovery/RecoveryStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace {

// A record is a few hundred bytes. A larger file is not one.
constexpr qint64 kMaxRecordBytes = 65536;
constexpr int kRecordVersion = 1;
// The largest generation number that a name can hold (nine digits).
constexpr int kMaxGeneration = 999999999;
// New ids to try when the folder name of an id is taken.
constexpr int kIdAttempts = 8;
// Whole numbers up to this size are exact in a JSON double.
constexpr double kMaxExactNumber = 9007199254740992.0;

RecoveryResult okResult(const QString& bundlePath = QString()) {
	RecoveryResult result;
	result.ok = true;
	result.bundlePath = bundlePath;
	return result;
}

// The error text is a fixed sentence. A diagnostic code is a stable id such as
// scene.bundle.schema and is added only when it has that form.
RecoveryResult failure(const char* sentence, const std::string& code = std::string()) {
	RecoveryResult result;
	result.error = QString::fromLatin1(sentence);
	const bool plainCode = !code.empty() && code.size() <= 64 && std::all_of(code.begin(), code.end(), [](char character) {
		return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '.' || character == '-' || character == '_';
	});
	if (plainCode)
		result.error += QStringLiteral(" (") + QString::fromStdString(code) + QLatin1Char(')');
	return result;
}

std::string firstErrorCode(const std::vector<SceneDiagnostic>& diagnostics) {
	for (const SceneDiagnostic& diagnostic : diagnostics) {
		if (diagnostic.severity == SceneSeverity::Error)
			return diagnostic.code;
	}
	return std::string();
}

// Runs one operation of the store. No exception leaves the store.
template <typename Operation>
RecoveryResult guarded(const char* sentence, Operation operation) {
	try {
		return operation();
	} catch (...) {
		return failure(sentence);
	}
}

// An instance id is a random UUID in the form QUuid::WithoutBraces gives.
bool validInstanceId(const QString& id) {
	const QUuid uuid(id);
	return !uuid.isNull() && uuid.toString(QUuid::WithoutBraces) == id;
}

QString instanceFolder(const QString& root, const QString& id) {
	return QDir(root).filePath(id);
}

QString bundleFileName(int generation) {
	return QStringLiteral("gen-%1.egscene").arg(generation);
}

QString recordPath(const QString& folder) {
	return QDir(folder).filePath(QStringLiteral("recovery.json"));
}

// The number n of a name gen-<n>.egscene, or 0 for any other name.
int generationNumber(const QString& name) {
	const QRegularExpression pattern(QStringLiteral("\\Agen-([1-9][0-9]{0,8})\\.egscene\\z"));
	const QRegularExpressionMatch match = pattern.match(name);
	return match.hasMatch() ? match.captured(1).toInt() : 0;
}

// What saveSceneBundle leaves next to a bundle when it is interrupted.
bool isStagingName(const QString& name) {
	const QRegularExpression pattern(QStringLiteral("\\Agen-[1-9][0-9]{0,8}\\.egscene\\.staging-"));
	return pattern.match(name).hasMatch();
}

// What QSaveFile leaves next to a record when it is interrupted.
bool isRecordLeftover(const QString& name) {
	return name.startsWith(QStringLiteral("recovery.json."));
}

QFileInfoList folderEntries(const QString& folder) {
	return QDir(folder).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
}

// Removes one entry. A link is removed itself and its target is left alone.
bool removeEntry(const QFileInfo& entry) {
	if (entry.isDir() && !entry.isSymLink())
		return QDir(entry.absoluteFilePath()).removeRecursively();
	return QFile::remove(entry.absoluteFilePath());
}

// Locks the lock file of an instance folder. Null when the folder is not a real folder or the
// lock cannot be taken, which means that its owner is alive or that the folder is not writable.
std::unique_ptr<QLockFile> lockFolder(const QString& folder) {
	const QFileInfo info(folder);
	if (info.isSymLink() || !info.isDir())
		return nullptr;
	auto lock = std::make_unique<QLockFile>(QDir(folder).filePath(QStringLiteral("lock")));
	// QLockFile can take a lock file that is older than the stale time (30 seconds by default) as
	// stale. The lock of an open store lasts as long as the application runs, so the time is 0.
	lock->setStaleLockTime(0);
	if (!lock->tryLock(0))
		return nullptr;
	return lock;
}

// Removes an instance folder: everything in it except the lock file, then the lock, then the
// empty folder. The caller holds the lock of the folder. Returns false when the folder remains.
bool removeInstanceFolder(const QString& root, const QString& id, QLockFile& lock) {
	const QString folder = instanceFolder(root, id);
	const QFileInfo info(folder);
	if (!validInstanceId(id) || info.isSymLink() || !info.isDir() || !lock.isLocked())
		return false;
	for (const QFileInfo& entry : folderEntries(folder)) {
		if (entry.fileName() != QLatin1String("lock"))
			removeEntry(entry);
	}
	lock.unlock();
	return QDir().rmdir(folder);
}

// Removes the generation files other than keepGeneration, the entries that an interrupted
// bundle write left and the temporary records. Other names stay. Returns false when an entry
// could not be removed.
bool pruneFolder(const QString& folder, int keepGeneration) {
	bool clean = true;
	for (const QFileInfo& entry : folderEntries(folder)) {
		const QString name = entry.fileName();
		const int number = generationNumber(name);
		if (number > 0) {
			if (number != keepGeneration && entry.isFile() && !entry.isSymLink())
				clean &= QFile::remove(entry.absoluteFilePath());
		} else if (isStagingName(name) || isRecordLeftover(name)) {
			clean &= removeEntry(entry);
		}
	}
	return clean;
}

bool removeRecord(const QString& folder) {
	const QFileInfo info(recordPath(folder));
	if (!info.exists() && !info.isSymLink())
		return true;
	return removeEntry(info);
}

const char* kindName(RecoverySourceKind kind) {
	switch (kind) {
		case RecoverySourceKind::Bundle:
			return "bundle";
		case RecoverySourceKind::Directory:
			return "directory";
		case RecoverySourceKind::Untitled:
			break;
	}
	return "untitled";
}

bool kindFromName(const QString& name, RecoverySourceKind* kind) {
	if (name == QLatin1String("bundle"))
		*kind = RecoverySourceKind::Bundle;
	else if (name == QLatin1String("directory"))
		*kind = RecoverySourceKind::Directory;
	else if (name == QLatin1String("untitled"))
		*kind = RecoverySourceKind::Untitled;
	else
		return false;
	return true;
}

QString timeText(const QDateTime& time) {
	return time.toUTC().toString(Qt::ISODateWithMs);
}

bool parseTime(const QString& text, QDateTime* time) {
	const QDateTime parsed = QDateTime::fromString(text, Qt::ISODateWithMs);
	if (!parsed.isValid())
		return false;
	*time = parsed.toUTC();
	return true;
}

// A JSON number without a fraction.
bool wholeNumber(const QJsonValue& value, double* number) {
	if (!value.isDouble())
		return false;
	*number = value.toDouble();
	return std::isfinite(*number) && *number == std::floor(*number);
}

bool exactInteger(const QJsonValue& value, qint64* integer) {
	double number = 0;
	if (!wholeNumber(value, &number) || std::fabs(number) > kMaxExactNumber)
		return false;
	*integer = static_cast<qint64>(number);
	return true;
}

// The content of recovery.json when the file is a record this version can use.
struct Record {
	bool valid = false;
	bool newerVersion = false; // The file is a record of a layout this version does not know.
	RecoverySource source;
	QDateTime writtenAt;
};

// A file that is missing, too large, not JSON, of another instance or of the wrong shape is no
// record. That is never an error.
Record readRecord(const QString& folder, const QString& id) {
	Record record;
	const QFileInfo info(recordPath(folder));
	if (info.isSymLink() || !info.isFile() || info.size() > kMaxRecordBytes)
		return record;
	QFile file(info.absoluteFilePath());
	if (!file.open(QIODevice::ReadOnly))
		return record;
	const QByteArray bytes = file.read(kMaxRecordBytes + 1);
	if (bytes.size() > kMaxRecordBytes)
		return record;
	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject())
		return record;
	const QJsonObject object = document.object();

	double version = 0;
	if (!wholeNumber(object.value(QStringLiteral("record_version")), &version))
		return record;
	if (version > kRecordVersion) {
		record.newerVersion = true;
		return record;
	}
	if (version != kRecordVersion)
		return record;

	qint64 generation = 0;
	const QJsonValue idValue = object.value(QStringLiteral("instance_id"));
	const QJsonValue nameValue = object.value(QStringLiteral("display_name"));
	const QJsonValue writtenValue = object.value(QStringLiteral("written_at"));
	const QJsonValue sourceValue = object.value(QStringLiteral("source"));
	if (!idValue.isString() || idValue.toString() != id || !object.value(QStringLiteral("bundle_file")).isString()
		|| !exactInteger(object.value(QStringLiteral("generation")), &generation) || !nameValue.isString()
		|| !writtenValue.isString() || !parseTime(writtenValue.toString(), &record.writtenAt) || !sourceValue.isObject())
		return record;
	const QJsonObject sourceObject = sourceValue.toObject();
	const QJsonValue kindValue = sourceObject.value(QStringLiteral("kind"));
	const QJsonValue pathValue = sourceObject.value(QStringLiteral("path"));
	const QJsonValue modifiedValue = sourceObject.value(QStringLiteral("modified"));
	RecoverySource source;
	if (!kindValue.isString() || !kindFromName(kindValue.toString(), &source.kind) || !pathValue.isString()
		|| !exactInteger(sourceObject.value(QStringLiteral("size")), &source.sizeBytes) || source.sizeBytes < -1
		|| !modifiedValue.isString())
		return record;
	if (!modifiedValue.toString().isEmpty() && !parseTime(modifiedValue.toString(), &source.modified))
		return record;
	source.path = pathValue.toString();
	source.displayName = nameValue.toString();
	record.source = source;
	record.valid = true;
	return record;
}

// Replaces recovery.json as a whole. False when it cannot be written.
bool writeRecord(const QString& folder, const QString& id, int generation, const RecoverySource& source, const QDateTime& writtenAt) {
	QJsonObject sourceObject;
	sourceObject.insert(QStringLiteral("kind"), QString::fromLatin1(kindName(source.kind)));
	sourceObject.insert(QStringLiteral("path"), source.path);
	sourceObject.insert(QStringLiteral("size"), source.sizeBytes);
	sourceObject.insert(QStringLiteral("modified"), source.modified.isValid() ? timeText(source.modified) : QString());
	QJsonObject object;
	object.insert(QStringLiteral("record_version"), kRecordVersion);
	object.insert(QStringLiteral("instance_id"), id);
	object.insert(QStringLiteral("bundle_file"), bundleFileName(generation));
	object.insert(QStringLiteral("generation"), generation);
	object.insert(QStringLiteral("written_at"), timeText(writtenAt));
	object.insert(QStringLiteral("display_name"), source.displayName);
	object.insert(QStringLiteral("source"), sourceObject);
	const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
	if (bytes.size() > kMaxRecordBytes)
		return false;
	QSaveFile file(recordPath(folder));
	if (!file.open(QIODevice::WriteOnly))
		return false;
	if (file.write(bytes) != bytes.size()) {
		file.cancelWriting();
		return false;
	}
	return file.commit();
}

struct Generation {
	int number = 0;
	QString path;
};

// The generation files of an instance folder, highest number first.
std::vector<Generation> generationFiles(const QString& folder) {
	std::vector<Generation> found;
	for (const QFileInfo& entry : folderEntries(folder)) {
		const int number = generationNumber(entry.fileName());
		if (number > 0 && entry.isFile() && !entry.isSymLink())
			found.push_back({number, entry.absoluteFilePath()});
	}
	std::sort(found.begin(), found.end(), [](const Generation& left, const Generation& right) { return left.number > right.number; });
	return found;
}

bool hasDiagnosticCode(const std::vector<SceneDiagnostic>& diagnostics, const char* code) {
	return std::any_of(diagnostics.begin(), diagnostics.end(), [code](const SceneDiagnostic& diagnostic) { return diagnostic.code == code; });
}

struct Inspection {
	bool unreadable = false; // A bundle cannot be read now. The folder is left as it is.
	bool found = false;
	int generation = 0; // Number of the bundle of the candidate.
	RecoveryCandidate candidate;
};

// Finds the copy in an instance folder: the newest bundle that passes the bundle inspection.
// Changes nothing.
Inspection inspectFolder(const QString& root, const QString& id) {
	Inspection inspection;
	const QString folder = instanceFolder(root, id);
	const Record record = readRecord(folder, id);
	RecoveryCandidate& candidate = inspection.candidate;
	candidate.instanceId = id;

	SceneCompatibilityClass classification = SceneCompatibilityClass::Malformed;
	for (const Generation& generation : generationFiles(folder)) {
		const SceneCompatibilityProbeResult probe = probeSceneCompatibility(generation.path.toStdString());
		if (probe.classification != SceneCompatibilityClass::Malformed) {
			classification = probe.classification;
			inspection.generation = generation.number;
			candidate.bundlePath = generation.path;
			break;
		}
		if (hasDiagnosticCode(probe.diagnostics, "scene.bundle.file.read") && !record.newerVersion) {
			inspection.unreadable = true;
			return inspection;
		}
	}

	if (record.newerVersion) {
		candidate.state = RecoveryCandidateState::NewerRecordVersion;
		candidate.writtenAt = QFileInfo(recordPath(folder)).lastModified().toUTC();
		inspection.found = true;
		return inspection;
	}
	if (candidate.bundlePath.isEmpty())
		return inspection;
	candidate.state = classification == SceneCompatibilityClass::Current ? RecoveryCandidateState::Openable : RecoveryCandidateState::OtherSceneVersion;
	if (record.valid) {
		candidate.source = record.source;
		candidate.writtenAt = record.writtenAt;
	} else {
		candidate.writtenAt = QFileInfo(candidate.bundlePath).lastModified().toUTC();
	}
	candidate.sourceStatus = recoverySourceStatus(candidate.source, candidate.writtenAt);
	inspection.found = true;
	return inspection;
}

} // namespace

RecoverySourceStatus recoverySourceStatus(const RecoverySource& source, const QDateTime& writtenAt) {
	if (source.kind == RecoverySourceKind::Untitled || source.path.isEmpty())
		return RecoverySourceStatus::NotApplicable;
	const QString file = source.kind == RecoverySourceKind::Directory ? QDir(source.path).filePath(QStringLiteral("scene.json")) : source.path;
	const QFileInfo info(file);
	if (!info.isFile())
		return RecoverySourceStatus::Missing;
	if (source.sizeBytes < 0 || !source.modified.isValid())
		return RecoverySourceStatus::Unknown;
	const qint64 modified = info.lastModified().toMSecsSinceEpoch();
	if (writtenAt.isValid() && modified > writtenAt.toMSecsSinceEpoch())
		return RecoverySourceStatus::NewerThanCopy;
	if (info.size() != source.sizeBytes || modified != source.modified.toMSecsSinceEpoch())
		return RecoverySourceStatus::Changed;
	return RecoverySourceStatus::Unchanged;
}

RecoveryStore::RecoveryStore(const QString& root)
	: m_root(root) {}

RecoveryStore::~RecoveryStore() = default;

bool RecoveryStore::isOpen() const {
	return m_lock != nullptr;
}

QString RecoveryStore::instanceId() const {
	return m_id;
}

RecoveryResult RecoveryStore::open() {
	return guarded("Cannot create the recovery folder", [this]() -> RecoveryResult {
		if (m_lock)
			return okResult();
		if (m_root.isEmpty())
			return failure("The recovery folder is not set");
		if (!QDir().mkpath(m_root))
			return failure("Cannot create the recovery folder");
		for (int attempt = 0; attempt < kIdAttempts; ++attempt) {
			const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
			const QString folder = instanceFolder(m_root, id);
			if (!QDir().mkdir(folder)) {
				if (QFileInfo::exists(folder))
					continue;
				return failure("Cannot create the recovery folder");
			}
#ifdef Q_OS_UNIX
			// On Windows the folder follows the access rules of the profile folder.
			if (!QFile::setPermissions(folder, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) {
				QDir().rmdir(folder);
				return failure("Cannot restrict the recovery folder to its owner");
			}
#endif
			std::unique_ptr<QLockFile> lock = lockFolder(folder);
			if (!lock) {
				QDir().rmdir(folder);
				return failure("Cannot lock the recovery folder");
			}
			m_id = id;
			m_lock = std::move(lock);
			m_generation = 0;
			return okResult();
		}
		return failure("Cannot create the recovery folder");
	});
}

RecoveryResult RecoveryStore::write(const SceneModel& scene, const RecoverySource& source) {
	return guarded("Cannot write the recovery copy", [&]() -> RecoveryResult {
		if (!m_lock)
			return failure("The recovery store is not open");
		if (m_generation >= kMaxGeneration)
			return failure("Cannot write the recovery copy");
		const int generation = m_generation + 1;
		const QString folder = instanceFolder(m_root, m_id);
		const QString bundlePath = QDir(folder).filePath(bundleFileName(generation));
		const SceneSaveResult saved = saveSceneBundle(scene, bundlePath.toStdString());
		if (!saved.success()) {
			QFile::remove(bundlePath);
			return failure("Cannot write the recovery copy", firstErrorCode(saved.diagnostics));
		}
		// The previous copy stays until the record points at the new one.
		if (!writeRecord(folder, m_id, generation, source, QDateTime::currentDateTimeUtc())) {
			QFile::remove(bundlePath);
			return failure("Cannot write the recovery record");
		}
		m_generation = generation;
		pruneFolder(folder, generation);
		return okResult(bundlePath);
	});
}

RecoveryResult RecoveryStore::clear() {
	return guarded("Cannot remove the recovery copy", [this]() -> RecoveryResult {
		if (!m_lock)
			return failure("The recovery store is not open");
		// The record goes last, so that an interruption leaves a record without a bundle at worst.
		const QString folder = instanceFolder(m_root, m_id);
		const bool bundlesRemoved = pruneFolder(folder, 0);
		const bool recordRemoved = removeRecord(folder);
		return bundlesRemoved && recordRemoved ? okResult() : failure("Cannot remove the recovery copy");
	});
}

RecoveryResult RecoveryStore::close() {
	return guarded("Cannot remove the recovery folder", [this]() -> RecoveryResult {
		if (!m_lock)
			return okResult();
		const bool removed = removeInstanceFolder(m_root, m_id, *m_lock);
		m_lock.reset();
		m_id.clear();
		m_generation = 0;
		return removed ? okResult() : failure("Cannot remove the recovery folder");
	});
}

RecoveryScan RecoveryStore::scan() {
	RecoveryScan result;
	struct Held {
		RecoveryCandidate candidate;
		std::unique_ptr<QLockFile> lock;
	};
	std::vector<Held> held;
	try {
		const QDir rootDir(m_root);
		if (m_root.isEmpty() || !rootDir.exists())
			return result;
		for (const QFileInfo& entry : rootDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
			const QString id = entry.fileName();
			if (!validInstanceId(id) || id == m_id)
				continue;
			try {
				// A folder whose lock is held belongs to a running application. Nothing in it is touched.
				std::unique_ptr<QLockFile> lock = lockFolder(instanceFolder(m_root, id));
				if (!lock)
					continue;
				const Inspection inspection = inspectFolder(m_root, id);
				if (inspection.unreadable)
					continue;
				if (!inspection.found) {
					removeInstanceFolder(m_root, id, *lock);
					continue;
				}
				if (inspection.candidate.state != RecoveryCandidateState::NewerRecordVersion)
					pruneFolder(instanceFolder(m_root, id), inspection.generation);
				held.push_back({inspection.candidate, std::move(lock)});
			} catch (...) {
			}
		}
		std::sort(held.begin(), held.end(), [](const Held& left, const Held& right) {
			if (left.candidate.writtenAt != right.candidate.writtenAt)
				return left.candidate.writtenAt > right.candidate.writtenAt;
			return left.candidate.instanceId < right.candidate.instanceId;
		});
		for (std::size_t index = 0; index < held.size(); ++index) {
			Held& item = held[index];
			if (index < static_cast<std::size_t>(kMaxKeptInstances))
				result.candidates.push_back(item.candidate);
			else if (removeInstanceFolder(m_root, item.candidate.instanceId, *item.lock))
				++result.removedOverCap;
		}
	} catch (...) {
	}
	return result;
}

RecoveryResult RecoveryStore::adopt(const RecoveryCandidate& candidate) {
	return guarded("Cannot take over the recovery copy", [&]() -> RecoveryResult {
		if (!m_lock)
			return failure("The recovery store is not open");
		if (candidate.state != RecoveryCandidateState::Openable || !validInstanceId(candidate.instanceId) || candidate.instanceId == m_id)
			return failure("This recovery copy cannot be taken over");
		if (m_generation >= kMaxGeneration)
			return failure("Cannot take over the recovery copy");
		const QString folder = instanceFolder(m_root, candidate.instanceId);
		const QFileInfo info(folder);
		if (info.isSymLink() || !info.isDir())
			return failure("The recovery copy is no longer there");
		std::unique_ptr<QLockFile> lock = lockFolder(folder);
		if (!lock)
			return failure("The recovery copy is in use");
		// The candidate that the caller holds may be out of date, so the folder is inspected again.
		const Inspection inspection = inspectFolder(m_root, candidate.instanceId);
		if (!inspection.found || inspection.candidate.state != RecoveryCandidateState::Openable)
			return failure("This recovery copy cannot be taken over");

		const int generation = m_generation + 1;
		const QString ownFolder = instanceFolder(m_root, m_id);
		const QString target = QDir(ownFolder).filePath(bundleFileName(generation));
		if (!QFile::copy(inspection.candidate.bundlePath, target)) {
			QFile::remove(target);
			return failure("Cannot take over the recovery copy");
		}
		if (probeSceneCompatibility(target.toStdString()).classification != SceneCompatibilityClass::Current) {
			QFile::remove(target);
			return failure("Cannot take over the recovery copy");
		}
		if (!writeRecord(ownFolder, m_id, generation, inspection.candidate.source, inspection.candidate.writtenAt)) {
			QFile::remove(target);
			return failure("Cannot write the recovery record");
		}
		m_generation = generation;
		pruneFolder(ownFolder, generation);
		removeInstanceFolder(m_root, candidate.instanceId, *lock);
		return okResult(target);
	});
}

RecoveryResult RecoveryStore::discard(const RecoveryCandidate& candidate) {
	return guarded("Cannot remove the recovery copy", [&]() -> RecoveryResult {
		if (!validInstanceId(candidate.instanceId) || candidate.instanceId == m_id)
			return failure("This recovery copy cannot be removed");
		const QString folder = instanceFolder(m_root, candidate.instanceId);
		const QFileInfo info(folder);
		if (info.isSymLink() || !info.isDir())
			return failure("The recovery copy is no longer there");
		std::unique_ptr<QLockFile> lock = lockFolder(folder);
		if (!lock)
			return failure("The recovery copy is in use");
		if (!removeInstanceFolder(m_root, candidate.instanceId, *lock))
			return failure("Cannot remove the recovery copy");
		return okResult();
	});
}
