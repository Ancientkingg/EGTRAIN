#include "io/third_party/miniz/miniz.h"
#include "scene/SceneBundle.h"
#include "scene/SceneCompatibility.h"
#include "scene/SceneModel.h"

#include "recovery/RecoveryStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QMap>
#include <QProcess>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

// A check of a process case. The first run on a platform shows nothing but this message, so it
// names the stage, the platform and what was seen before.
static bool expectStage(bool condition, const char* stage, const QString& note) {
	if (!condition)
		std::cerr << "failed: " << stage << " on " << QSysInfo::kernelType().toStdString() << " (" << note.toStdString() << ")\n";
	return condition;
}

static SceneModel makeScene(const std::string& name) {
	SceneModel scene = makeNewSceneModel();
	scene.name = name;
	for (const char* id : {"track.a", "track.b", "track.c"})
		scene.tracks.push_back({id});
	return scene;
}

static QString at(const QString& folder, const QString& name) {
	return QDir(folder).filePath(name);
}

static QByteArray readBytes(const QString& path) {
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

static bool writeBytes(const QString& path, const QByteArray& contents) {
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

static QStringList listNames(const QString& folder) {
	return QDir(folder).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::Name);
}

// Every entry below a folder with its bytes, so that "unchanged" covers names and content.
static QMap<QString, QByteArray> snapshot(const QString& folder) {
	QMap<QString, QByteArray> entries;
	QDirIterator iterator(folder, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
	while (iterator.hasNext()) {
		iterator.next();
		const QFileInfo info = iterator.fileInfo();
		const QString name = QDir(folder).relativeFilePath(iterator.filePath());
		if (info.isSymLink())
			entries.insert(name, "link:" + info.symLinkTarget().toUtf8());
		else
			entries.insert(name, info.isFile() ? readBytes(iterator.filePath()) : QByteArray("folder"));
	}
	return entries;
}

static QString newId() {
	return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

static bool isValidId(const QString& id) {
	const QUuid uuid(id);
	return !uuid.isNull() && uuid.toString(QUuid::WithoutBraces) == id;
}

static bool bundleLoadsAs(const QString& bundlePath, const char* name) {
	const SceneLoadResult loaded = loadSceneBundle(bundlePath.toStdString());
	return !hasErrors(loaded.diagnostics) && loaded.scene.name == name;
}

static bool hasCode(const SceneCompatibilityProbeResult& probe, const char* code) {
	for (const SceneDiagnostic& diagnostic : probe.diagnostics) {
		if (diagnostic.code == code)
			return true;
	}
	return false;
}

// An error is a fixed sentence: it holds no path.
static bool cleanError(const RecoveryResult& result, const QString& root) {
	return !result.ok && !result.error.isEmpty() && !result.error.contains(root) && !result.error.contains(QDir::toNativeSeparators(root))
		&& !result.error.contains(QLatin1Char('/')) && !result.error.contains(QLatin1Char('\\'));
}

// The scan lists one candidate, and its bundle is the given file and holds the named scene.
static bool listsOnly(const RecoveryScan& scan, const QString& bundlePath, const char* sceneName) {
	return scan.candidates.size() == 1 && scan.candidates.first().bundlePath == bundlePath && bundleLoadsAs(bundlePath, sceneName);
}

static bool nearNow(const QDateTime& time) {
	return time.isValid() && qAbs(time.secsTo(QDateTime::currentDateTimeUtc())) < 60;
}

// A bundle file in the folder and the source that describes it.
static RecoverySource makeBundleSource(const QString& folder, const char* sceneName) {
	RecoverySource source;
	QDir().mkpath(folder);
	source.kind = RecoverySourceKind::Bundle;
	source.path = at(folder, "source.egscene");
	source.displayName = "Source scene";
	if (saveSceneBundle(makeScene(sceneName), source.path.toStdString()).success()) {
		const QFileInfo info(source.path);
		source.sizeBytes = info.size();
		source.modified = info.lastModified().toUTC();
	}
	return source;
}

// Opens a store, writes a scene and ends the store without closing it. Returns the instance id.
static QString leftBehind(const QString& root, const char* sceneName, const RecoverySource& source) {
	RecoveryStore store(root);
	if (!store.open().ok || !store.write(makeScene(sceneName), source).ok)
		return QString();
	return store.instanceId();
}

// The folder of an instance that has ended: no lock file.
static QString makeDeadFolder(const QString& root) {
	const QString folder = at(root, newId());
	QDir().mkpath(folder);
	return folder;
}

static bool saveGeneration(const QString& folder, int number, const char* sceneName) {
	const QString path = at(folder, QStringLiteral("gen-%1.egscene").arg(number));
	return saveSceneBundle(makeScene(sceneName), path.toStdString()).success();
}

#ifdef Q_OS_UNIX
static const RecoveryCandidate* findCandidate(const RecoveryScan& scan, const QString& id) {
	for (const RecoveryCandidate& candidate : scan.candidates) {
		if (candidate.instanceId == id)
			return &candidate;
	}
	return nullptr;
}
#endif

static QJsonObject readRecordObject(const QString& folder) {
	return QJsonDocument::fromJson(readBytes(at(folder, "recovery.json"))).object();
}

static bool setModified(const QString& path, const QDateTime& time) {
	QFile file(path);
	return file.open(QIODevice::ReadWrite | QIODevice::ExistingOnly) && file.setFileTime(time, QFileDevice::FileModificationTime);
}

// Gives the permissions of a path back when it goes out of scope, so that a failed check cannot
// leave a folder that the temporary folder cannot remove.
class PermissionGuard {
public:
	explicit PermissionGuard(const QString& path)
		: m_path(path), m_original(QFile::permissions(path)) {}
	~PermissionGuard() { restore(); }
	PermissionGuard(const PermissionGuard&) = delete;
	PermissionGuard& operator=(const PermissionGuard&) = delete;
	void restore() { QFile::setPermissions(m_path, m_original); }

private:
	QString m_path;
	QFile::Permissions m_original;
};

// True when a folder that the caller made read-only refuses a new file. A process with root
// rights writes anyway, and Windows does not restrict folders with permission bits. The case
// then skips itself.
static bool refusesNewFiles(const QString& folder, const char* what) {
#ifdef Q_OS_UNIX
	if (access(QFile::encodeName(folder).constData(), W_OK) != 0)
		return true;
	std::cerr << "skipped: " << what << " (the process may write into a read-only folder)\n";
#else
	Q_UNUSED(folder);
	std::cerr << "skipped: " << what << " (permission bits do not restrict folders here)\n";
#endif
	return false;
}

#ifdef Q_OS_UNIX
// True when a file whose read permission the caller removed cannot be opened.
static bool refusesReading(const QString& path, const char* what) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return true;
	std::cerr << "skipped: " << what << " (the process may read a file without permission)\n";
	return false;
}
#endif

static bool testOpenAndLayout() {
	bool ok = true;
	QTemporaryDir temp;
	ok &= expect(temp.isValid(), "open: temporary folder");
	const QString root = temp.filePath("root");
	{
		RecoveryStore store(root);
		ok &= expect(!store.isOpen() && store.instanceId().isEmpty(), "open: a new store is closed");
		const RecoveryResult opened = store.open();
		ok &= expect(opened.ok && opened.error.isEmpty() && store.isOpen(), "open: open succeeds");
		const QString id = store.instanceId();
		const QString folder = at(root, id);
		ok &= expect(isValidId(id), "open: the instance id is a UUID");
		ok &= expect(listNames(root) == QStringList{id}, "open: the root holds only the instance folder");
		ok &= expect(QFileInfo(folder).isDir() && QFileInfo(at(folder, "lock")).isFile(), "open: the folder holds the lock file");
#ifdef Q_OS_UNIX
		const QFile::Permissions others = QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup
			| QFile::ReadOther | QFile::WriteOther | QFile::ExeOther;
		ok &= expect(!(QFile::permissions(folder) & others), "open: the folder is restricted to its owner");
#endif
		ok &= expect(store.open().ok && store.instanceId() == id && listNames(root).size() == 1, "open: opening twice changes nothing");
	}

	const QString lockedRoot = temp.filePath("locked");
	QDir().mkpath(lockedRoot);
	PermissionGuard guard(lockedRoot);
	QFile::setPermissions(lockedRoot, QFile::ReadOwner | QFile::ExeOwner);
	if (refusesNewFiles(lockedRoot, "open: a read-only root")) {
		RecoveryStore store(lockedRoot);
		const RecoveryResult refused = store.open();
		ok &= expect(cleanError(refused, lockedRoot), "open: a read-only root gives an error without a path");
		ok &= expect(!store.isOpen() && listNames(lockedRoot).isEmpty(), "open: a refused open leaves no folder");
		guard.restore();
		ok &= expect(store.open().ok && store.isOpen(), "open: the same store opens once the root is writable");
	}
	return ok;
}

static bool testWrite() {
	bool ok = true;
	QTemporaryDir temp;
	const QString root = temp.filePath("root");
	const QString sourceFolder = temp.filePath("source");
	const RecoverySource source = makeBundleSource(sourceFolder, "source");
	const QByteArray sourceBytes = readBytes(source.path);
	const QMap<QString, QByteArray> sourceBefore = snapshot(sourceFolder);
	ok &= expect(!sourceBytes.isEmpty(), "write: the source bundle is written");

	RecoveryStore store(root);
	ok &= expect(store.open().ok, "write: open");
	const QString folder = at(root, store.instanceId());
	const RecoveryResult first = store.write(makeScene("first"), source);
	ok &= expect(first.ok && first.error.isEmpty() && first.bundlePath == at(folder, "gen-1.egscene"), "write: the first write returns its bundle");
	ok &= expect(listNames(folder) == QStringList({"gen-1.egscene", "lock", "recovery.json"}),
		"write: the folder holds the lock, the bundle and the record");
	ok &= expect(bundleLoadsAs(first.bundlePath, "first"), "write: the copy is an ordinary bundle that loads");

	const QJsonObject record = readRecordObject(folder);
	const QJsonObject recordSource = record.value("source").toObject();
	ok &= expect(record.value("record_version").toInt() == 1 && record.value("instance_id").toString() == store.instanceId(),
		"write: the record names the version and the instance");
	ok &= expect(record.value("bundle_file").toString() == "gen-1.egscene" && record.value("generation").toInt() == 1,
		"write: the record names the generation");
	ok &= expect(nearNow(QDateTime::fromString(record.value("written_at").toString(), Qt::ISODateWithMs)),
		"write: the record holds the time of the write");
	ok &= expect(record.value("display_name").toString() == source.displayName, "write: the record holds the display name");
	ok &= expect(recordSource.value("kind").toString() == "bundle" && recordSource.value("path").toString() == source.path,
		"write: the record holds the source kind and path");
	ok &= expect(static_cast<qint64>(recordSource.value("size").toDouble()) == source.sizeBytes, "write: the record holds the source size");
	ok &= expect(recordSource.value("modified").toString() == source.modified.toString(Qt::ISODateWithMs), "write: the record holds the source time");

	const RecoveryResult second = store.write(makeScene("second"), source);
	ok &= expect(second.ok && second.bundlePath == at(folder, "gen-2.egscene"), "write: the second write returns the next generation");
	ok &= expect(listNames(folder) == QStringList({"gen-2.egscene", "lock", "recovery.json"}), "write: the previous generation is removed");
	ok &= expect(bundleLoadsAs(second.bundlePath, "second"), "write: the new copy loads");
	ok &= expect(readRecordObject(folder).value("generation").toInt() == 2, "write: the record names the new generation");

	ok &= expect(readBytes(source.path) == sourceBytes && snapshot(sourceFolder) == sourceBefore, "write: the source is not touched");
	return ok;
}

static bool testFailedWrite() {
	bool ok = true;
	QTemporaryDir temp;
	const QString root = temp.filePath("root");
	const RecoverySource source = makeBundleSource(temp.filePath("source"), "source");
	RecoveryStore store(root);
	ok &= expect(store.open().ok, "failed write: open");
	const QString folder = at(root, store.instanceId());
	ok &= expect(store.write(makeScene("first"), source).ok, "failed write: first write");
	const QMap<QString, QByteArray> before = snapshot(folder);

	SceneModel other = makeScene("other");
	other.schemaVersion = kCurrentSceneSchemaVersion + 1;
	const RecoveryResult refused = store.write(other, source);
	ok &= expect(cleanError(refused, root) && refused.bundlePath.isEmpty(), "failed write: a scene of another version gives an error without a path");
	ok &= expect(refused.error.endsWith("(scene.bundle.schema)"), "failed write: the error names the diagnostic code");
	ok &= expect(snapshot(folder) == before, "failed write: the folder and the record are unchanged");
	ok &= expect(bundleLoadsAs(at(folder, "gen-1.egscene"), "first"), "failed write: the previous bundle still loads");

#ifdef Q_OS_UNIX
	// A record that cannot be replaced: the new bundle goes again and the previous copy stays.
	const QString record = at(folder, "recovery.json");
	ok &= expect(QFile::remove(record) && QDir().mkpath(at(record, "obstacle")) && writeBytes(at(record, "obstacle/keep"), "k"),
		"failed write: obstacle fixture");
	const RecoveryResult blocked = store.write(makeScene("blocked"), source);
	ok &= expect(cleanError(blocked, root), "failed write: an unwritable record gives an error without a path");
	ok &= expect(listNames(folder) == QStringList({"gen-1.egscene", "lock", "recovery.json"}), "failed write: no new bundle is left behind");
	ok &= expect(bundleLoadsAs(at(folder, "gen-1.egscene"), "first"), "failed write: the previous bundle is still there after a record failure");
	ok &= expect(QDir(record).removeRecursively() && writeBytes(record, before.value("recovery.json")), "failed write: obstacle removal");
	const RecoveryResult retried = store.write(makeScene("second"), source);
	ok &= expect(retried.ok && retried.bundlePath == at(folder, "gen-2.egscene"), "failed write: the generation did not advance");

	{
		PermissionGuard guard(folder);
		QFile::setPermissions(folder, QFile::ReadOwner | QFile::ExeOwner);
		if (refusesNewFiles(folder, "failed write: a read-only instance folder")) {
			const RecoveryResult locked = store.write(makeScene("third"), source);
			ok &= expect(cleanError(locked, root), "failed write: a read-only folder gives an error without a path");
			ok &= expect(bundleLoadsAs(at(folder, "gen-2.egscene"), "second"),
				"failed write: the previous bundle is still there in a read-only folder");
		}
	}
	ok &= expect(listNames(folder) == QStringList({"gen-2.egscene", "lock", "recovery.json"}), "failed write: the folder is as before");
#endif
	return ok;
}

static bool testLeftBehind() {
	bool ok = true;
	QTemporaryDir temp;
	const QString root = temp.filePath("root");
	const RecoverySource source = makeBundleSource(temp.filePath("source"), "source");
	const QString id = leftBehind(root, "left behind", source);
	ok &= expect(!id.isEmpty(), "left behind: fixture");

	RecoveryStore scanner(root);
	const RecoveryScan scan = scanner.scan();
	ok &= expect(scan.candidates.size() == 1 && scan.removedOverCap == 0, "left behind: one candidate");
	if (scan.candidates.size() == 1) {
		const RecoveryCandidate& candidate = scan.candidates.first();
		ok &= expect(candidate.instanceId == id && candidate.state == RecoveryCandidateState::Openable, "left behind: the candidate is openable");
		ok &= expect(candidate.source.kind == RecoverySourceKind::Bundle && candidate.source.path == source.path,
			"left behind: the source kind and path are as written");
		ok &= expect(candidate.source.displayName == source.displayName, "left behind: the display name is as written");
		ok &= expect(candidate.source.sizeBytes == source.sizeBytes && candidate.source.modified == source.modified,
			"left behind: the source size and time are as written");
		ok &= expect(nearNow(candidate.writtenAt), "left behind: the written time is the time of the write");
		ok &= expect(candidate.sourceStatus == RecoverySourceStatus::Unchanged, "left behind: the source is unchanged");
		ok &= expect(QFileInfo(candidate.bundlePath).isFile() && bundleLoadsAs(candidate.bundlePath, "left behind"), "left behind: the bundle loads");
	}

	// An open store is not a candidate. In one process the lock file is the proof of its owner.
	const QString liveRoot = temp.filePath("live");
	RecoveryStore live(liveRoot);
	ok &= expect(live.open().ok && live.write(makeScene("live"), source).ok, "live: fixture");
	const QString liveFolder = at(liveRoot, live.instanceId());
	const QMap<QString, QByteArray> liveBefore = snapshot(liveFolder);
	ok &= expect(liveBefore.contains("lock") && liveBefore.contains("gen-1.egscene"), "live: the folder holds the lock and the bundle");
	RecoveryStore other(liveRoot);
	ok &= expect(other.scan().candidates.isEmpty() && snapshot(liveFolder) == liveBefore, "live: another store does not list or touch an open store");
	ok &= expect(live.scan().candidates.isEmpty() && snapshot(liveFolder) == liveBefore, "live: a store skips its own folder");
	return ok;
}

static bool testNewestCompleteBundle() {
	bool ok = true;
	QTemporaryDir temp;
	{
		const QString root = temp.filePath("newest");
		const QString folder = makeDeadFolder(root);
		ok &= expect(saveGeneration(folder, 1, "A") && saveGeneration(folder, 2, "B"), "newest: fixture");
		const RecoveryScan scan = RecoveryStore(root).scan();
		ok &= expect(listsOnly(scan, at(folder, "gen-2.egscene"), "B"), "newest: the newest complete bundle is the candidate");
		ok &= expect(listNames(folder) == QStringList({"gen-2.egscene"}), "newest: the older generation is removed");
	}
	{
		const QString root = temp.filePath("truncated");
		const QString folder = makeDeadFolder(root);
		ok &= expect(saveGeneration(folder, 1, "A") && saveGeneration(folder, 2, "B"), "truncated: fixture");
		const QByteArray whole = readBytes(at(folder, "gen-2.egscene"));
		ok &= expect(writeBytes(at(folder, "gen-2.egscene"), whole.left(whole.size() / 2)), "truncated: fixture is cut");
		const RecoveryScan scan = RecoveryStore(root).scan();
		ok &= expect(listsOnly(scan, at(folder, "gen-1.egscene"), "A"), "truncated: an incomplete newest bundle gives way to the previous one");
		ok &= expect(listNames(folder) == QStringList({"gen-1.egscene"}), "truncated: the incomplete bundle is removed");
	}
	for (const bool halfLength : {false, true}) {
		const QString root = temp.filePath(halfLength ? "half" : "empty");
		const QString folder = makeDeadFolder(root);
		const QString bundle = at(folder, "gen-1.egscene");
		ok &= expect(saveGeneration(folder, 1, "A"), "unusable: fixture");
		const QByteArray whole = readBytes(bundle);
		ok &= expect(writeBytes(bundle, halfLength ? whole.left(whole.size() / 2) : QByteArray()), "unusable: fixture is cut");
		const SceneCompatibilityProbeResult probe = probeSceneCompatibility(bundle.toStdString());
		const bool rejected = probe.classification == SceneCompatibilityClass::Malformed && !probe.diagnostics.empty();
		ok &= expect(rejected && !hasCode(probe, "scene.bundle.file.read"),
			"unusable: the bundle inspection rejects the file and does not report an unreadable file");
		const RecoveryScan scan = RecoveryStore(root).scan();
		const char* message = halfLength ? "unusable: a half-length bundle is not listed and its folder goes"
										 : "unusable: an empty bundle is not listed and its folder goes";
		ok &= expect(scan.candidates.isEmpty() && !QFileInfo::exists(folder), message);
	}
	return ok;
}

// The record of a folder with every field set.
static QJsonObject validRecord(const QString& id) {
	QJsonObject source;
	source.insert("kind", "directory");
	source.insert("path", "/some/where");
	source.insert("size", 1234);
	source.insert("modified", "2026-01-02T03:04:05.678Z");
	QJsonObject record;
	record.insert("record_version", 1);
	record.insert("instance_id", id);
	record.insert("bundle_file", "gen-1.egscene");
	record.insert("generation", 1);
	record.insert("written_at", "2026-01-02T03:04:06.789Z");
	record.insert("display_name", "Recorded");
	record.insert("source", source);
	return record;
}

static QByteArray toBytes(const QJsonObject& object) {
	return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

// The valid record, with the display name padded so that the file has exactly the given size.
static QByteArray recordOfSize(const QString& id, int size) {
	QJsonObject record = validRecord(id);
	record.insert("display_name", "");
	record.insert("display_name", QString(size - toBytes(record).size(), 'x'));
	return toBytes(record);
}

struct OneFolderScan {
	RecoveryScan scan;
	QDateTime bundleTime;
};

// Scans a root with one dead folder that holds a valid bundle and the record that the function makes.
static OneFolderScan scanWithRecord(const std::function<QByteArray(const QString&)>& makeRecord) {
	QTemporaryDir temp;
	const QString root = temp.filePath("root");
	const QString folder = makeDeadFolder(root);
	const QString id = QFileInfo(folder).fileName();
	OneFolderScan result;
	if (!saveGeneration(folder, 1, "A") || !writeBytes(at(folder, "recovery.json"), makeRecord(id)))
		return result;
	result.bundleTime = QFileInfo(at(folder, "gen-1.egscene")).lastModified().toUTC();
	result.scan = RecoveryStore(root).scan();
	return result;
}

static bool hasNoRecordValues(const OneFolderScan& scanned) {
	if (scanned.scan.candidates.size() != 1)
		return false;
	const RecoveryCandidate& candidate = scanned.scan.candidates.first();
	return candidate.state == RecoveryCandidateState::Openable && candidate.source.kind == RecoverySourceKind::Untitled
		&& candidate.source.path.isEmpty() && candidate.source.displayName.isEmpty() && candidate.source.sizeBytes == -1
		&& !candidate.source.modified.isValid() && candidate.sourceStatus == RecoverySourceStatus::NotApplicable
		&& qAbs(candidate.writtenAt.secsTo(scanned.bundleTime)) < 2;
}

// Writes a bundle with a newer bundle_version, as a later release would.
static bool writeNewerBundle(const QString& path) {
	mz_zip_archive writer{};
	if (!mz_zip_writer_init_file(&writer, path.toStdString().c_str(), 0))
		return false;
	const std::string manifest = "{\"format\":\"egscene\",\"bundle_version\":" + std::to_string(kCurrentSceneBundleVersion + 1)
		+ ",\"schema_version\":" + std::to_string(kCurrentSceneSchemaVersion) + "}";
	bool ok = mz_zip_writer_add_mem(&writer, "scene.json", manifest.data(), manifest.size(), MZ_BEST_COMPRESSION)
		&& mz_zip_writer_add_mem(&writer, "future-layout.json", "future", 6, MZ_BEST_COMPRESSION) && mz_zip_writer_finalize_archive(&writer);
	ok = mz_zip_writer_end(&writer) && ok;
	return ok;
}

static bool testRecordProblems() {
	bool ok = true;

	const OneFolderScan valid = scanWithRecord([](const QString& id) { return toBytes(validRecord(id)); });
	ok &= expect(valid.scan.candidates.size() == 1, "record: a valid record gives a candidate");
	const OneFolderScan atLimit = scanWithRecord([](const QString& id) { return recordOfSize(id, 65536); });
	ok &= expect(atLimit.scan.candidates.size() == 1 && atLimit.scan.candidates.first().source.kind == RecoverySourceKind::Directory,
		"record: a record at the size limit is read");
	if (valid.scan.candidates.size() == 1) {
		const RecoveryCandidate& candidate = valid.scan.candidates.first();
		const QDateTime recordedModified = QDateTime::fromString("2026-01-02T03:04:05.678Z", Qt::ISODateWithMs);
		ok &= expect(candidate.source.kind == RecoverySourceKind::Directory && candidate.source.path == "/some/where"
				&& candidate.source.displayName == "Recorded" && candidate.source.sizeBytes == 1234 && candidate.source.modified == recordedModified,
			"record: a valid record gives the recorded source");
		ok &= expect(candidate.writtenAt == QDateTime::fromString("2026-01-02T03:04:06.789Z", Qt::ISODateWithMs),
			"record: a valid record gives the recorded time");
		ok &= expect(candidate.sourceStatus == RecoverySourceStatus::Missing, "record: the recorded source is judged");
	}

	struct Variant {
		const char* message;
		std::function<QByteArray(const QString&)> makeRecord;
	};
	const auto withField = [](const char* key, const QJsonValue& value) {
		return [key, value](const QString& id) {
			QJsonObject record = validRecord(id);
			record.insert(key, value);
			return toBytes(record);
		};
	};
	const auto withSourceField = [](const char* key, const QJsonValue& value) {
		return [key, value](const QString& id) {
			QJsonObject record = validRecord(id);
			QJsonObject source = record.value("source").toObject();
			source.insert(key, value);
			record.insert("source", source);
			return toBytes(record);
		};
	};
	const std::vector<Variant> variants = {
		{"record: a record cut in half is no record",
			[](const QString& id) {
				const QByteArray whole = toBytes(validRecord(id));
				return whole.left(whole.size() / 2);
			}},
		{"record: an empty file is no record", [](const QString&) { return QByteArray(); }},
		{"record: a JSON array is no record", [](const QString&) { return QByteArray("[]"); }},
		{"record: a record above the size limit is no record", [](const QString& id) { return recordOfSize(id, 65537); }},
		{"record: a record far above the size limit is no record", withField("display_name", QString(70000, 'x'))},
		{"record: a record of another instance is no record", [](const QString&) { return toBytes(validRecord(newId())); }},
		{"record: a version as a string is no record", withField("record_version", "1")},
		{"record: a version with a fraction is no record", withField("record_version", 1.5)},
		{"record: a version of zero is no record", withField("record_version", 0)},
		{"record: a source as a string is no record", withField("source", "bundle")},
		{"record: a size as a string is no record", withSourceField("size", "1234")},
		{"record: a size with a fraction is no record", withSourceField("size", 12.5)},
		{"record: a time that is not a date is no record", withField("written_at", "yesterday")},
		{"record: a source time that is not a date is no record", withSourceField("modified", "yesterday")},
		{"record: an unknown source kind is no record", withSourceField("kind", "cloud")},
		{"record: a display name as a number is no record", withField("display_name", 7)},
		{"record: an instance id as a number is no record", withField("instance_id", 7)},
	};
	for (const Variant& variant : variants)
		ok &= expect(hasNoRecordValues(scanWithRecord(variant.makeRecord)), variant.message);

	QTemporaryDir temp;
	{
		// A record of a newer layout: the folder is listed and nothing in it changes.
		const QString root = temp.filePath("newer");
		const QString folder = makeDeadFolder(root);
		const QString id = QFileInfo(folder).fileName();
		QJsonObject record = validRecord(id);
		record.insert("record_version", 2);
		record.insert("future_field", "x");
		ok &= expect(saveGeneration(folder, 1, "A") && saveGeneration(folder, 2, "B") && writeBytes(at(folder, "recovery.json"), toBytes(record))
				&& writeBytes(at(folder, "recovery.json.x"), "temporary") && QDir().mkpath(at(folder, "gen-2.egscene.staging-1-0")),
			"newer record: fixture");
		const QMap<QString, QByteArray> before = snapshot(folder);
		RecoveryStore store(root);
		const RecoveryScan scan = store.scan();
		ok &= expect(scan.candidates.size() == 1 && scan.candidates.first().state == RecoveryCandidateState::NewerRecordVersion,
			"newer record: the folder is listed as such");
		if (scan.candidates.size() == 1) {
			const RecoveryCandidate& candidate = scan.candidates.first();
			ok &= expect(candidate.bundlePath == at(folder, "gen-2.egscene"), "newer record: the newest valid bundle is named");
			ok &= expect(candidate.source.path.isEmpty() && candidate.source.displayName.isEmpty()
					&& candidate.sourceStatus == RecoverySourceStatus::NotApplicable,
				"newer record: the source is not read");
			ok &= expect(nearNow(candidate.writtenAt), "newer record: the time is the time of the record file");
		}
		ok &= expect(snapshot(folder) == before, "newer record: no file of the folder changes");

		// A newer record without a bundle is listed too.
		ok &= expect(QFile::remove(at(folder, "gen-1.egscene")) && QFile::remove(at(folder, "gen-2.egscene")), "newer record: bundles removed");
		const QMap<QString, QByteArray> bare = snapshot(folder);
		const RecoveryScan bareScan = store.scan();
		ok &= expect(bareScan.candidates.size() == 1 && bareScan.candidates.first().bundlePath.isEmpty() && snapshot(folder) == bare,
			"newer record: a folder without a bundle is listed and kept");

		// Neither state can be adopted, and both can be discarded.
		RecoveryStore adopter(root);
		ok &= expect(adopter.open().ok && bareScan.candidates.size() == 1, "newer record: adopter opens");
		if (bareScan.candidates.size() == 1) {
			const QMap<QString, QByteArray> adopterBefore = snapshot(at(root, adopter.instanceId()));
			ok &= expect(cleanError(adopter.adopt(bareScan.candidates.first()), root) && snapshot(folder) == bare,
				"newer record: adopt refuses and changes nothing");
			ok &= expect(snapshot(at(root, adopter.instanceId())) == adopterBefore, "newer record: the adopter folder is unchanged");
			ok &= expect(adopter.discard(bareScan.candidates.first()).ok && !QFileInfo::exists(folder), "newer record: discard removes the folder");
		}
	}
	{
		const QString root = temp.filePath("other");
		const QString folder = makeDeadFolder(root);
		ok &= expect(writeNewerBundle(at(folder, "gen-1.egscene")), "other version: fixture");
		const QMap<QString, QByteArray> before = snapshot(folder);
		RecoveryStore store(root);
		const RecoveryScan scan = store.scan();
		ok &= expect(scan.candidates.size() == 1 && scan.candidates.first().state == RecoveryCandidateState::OtherSceneVersion,
			"other version: a newer bundle is listed as such");
		ok &= expect(snapshot(folder) == before, "other version: the bundle is kept");
		RecoveryStore adopter(root);
		ok &= expect(adopter.open().ok && scan.candidates.size() == 1, "other version: adopter opens");
		if (scan.candidates.size() == 1) {
			ok &= expect(cleanError(adopter.adopt(scan.candidates.first()), root) && snapshot(folder) == before,
				"other version: adopt refuses and changes nothing");
			ok &= expect(listNames(at(root, adopter.instanceId())) == QStringList({"lock"}), "other version: the adopter folder is unchanged");
			ok &= expect(adopter.discard(scan.candidates.first()).ok && !QFileInfo::exists(folder), "other version: discard removes the folder");
		}
	}
	return ok;
}

static bool testPruning() {
	bool ok = true;
	QTemporaryDir temp;
	const QString root = temp.filePath("root");
	const QString outside = temp.filePath("outside");
	QDir().mkpath(outside);
	const QString folder = makeDeadFolder(root);
	ok &= expect(saveGeneration(folder, 1, "A") && writeBytes(at(outside, "inside.txt"), "inside") && saveGeneration(outside, 1, "outside"),
		"prune: fixture");
	const QString staging = at(folder, "gen-1.egscene.staging-1-0");
	ok &= expect(QDir().mkpath(staging) && writeBytes(at(staging, "bundle.egscene"), "partial") && writeBytes(at(folder, "recovery.json.x1"), "temporary"),
		"prune: stray entries");
	// Names that are not generations hold valid scenes, so that a rule that took them for one would show.
	const QByteArray bundleBytes = readBytes(at(folder, "gen-1.egscene"));
	const QString sceneFolder = at(folder, "gen-5.egscene");
	const QStringList unknownFiles = {"notes.txt", "gen-0.egscene", "gen-01.egscene", "gen-02.egscene", "gen-1234567890.egscene", "gen-01.egscene.staging-1"};
	bool unknownWritten = saveScene(makeScene("directory"), sceneFolder.toStdString()).success();
	for (const QString& name : unknownFiles)
		unknownWritten &= writeBytes(at(folder, name), name == "notes.txt" ? QByteArray("notes") : bundleBytes);
	ok &= expect(unknownWritten, "prune: unknown names");
	const QMap<QString, QByteArray> sceneFolderBefore = snapshot(sceneFolder);
	const QString rootFile = at(root, "plain.txt");
	const QString otherFolder = at(root, "not-an-id");
	ok &= expect(writeBytes(rootFile, "plain") && QDir().mkpath(otherFolder) && saveGeneration(otherFolder, 1, "B"), "prune: root entries");
	const QMap<QString, QByteArray> otherBefore = snapshot(otherFolder);
#ifdef Q_OS_UNIX
	const QString linkedId = newId();
	ok &= expect(QFile::link(outside, at(root, linkedId)) && QFile::link(at(outside, "gen-1.egscene"), at(folder, "gen-9.egscene")), "prune: links");
#endif
	const QMap<QString, QByteArray> outsideBefore = snapshot(outside);

	RecoveryStore store(root);
	const RecoveryScan scan = store.scan();
	ok &= expect(scan.candidates.size() == 1 && scan.candidates.first().bundlePath == at(folder, "gen-1.egscene"),
		"prune: only the folder of the ended instance is listed");
	ok &= expect(!QFileInfo::exists(at(folder, "gen-1.egscene.staging-1-0")), "prune: a staging folder of an interrupted write is removed");
	ok &= expect(!QFileInfo::exists(at(folder, "recovery.json.x1")), "prune: a temporary record is removed");
	for (const QString& name : unknownFiles)
		ok &= expect(QFileInfo(at(folder, name)).isFile() && readBytes(at(folder, name)) == (name == "notes.txt" ? QByteArray("notes") : bundleBytes),
			"prune: an unknown name is kept");
	ok &= expect(snapshot(sceneFolder) == sceneFolderBefore && QFileInfo(sceneFolder).isDir(), "prune: a folder named like a generation is kept");
	ok &= expect(snapshot(otherFolder) == otherBefore, "prune: a folder with another name is kept");
	ok &= expect(readBytes(rootFile) == "plain", "prune: a file in the root is kept");
	ok &= expect(snapshot(outside) == outsideBefore, "prune: the files outside the root are kept");
#ifdef Q_OS_UNIX
	ok &= expect(QFileInfo(at(root, linkedId)).isSymLink(), "prune: a link with the name of an instance is kept");
	ok &= expect(QFileInfo(at(folder, "gen-9.egscene")).isSymLink(), "prune: a link with the name of a generation is kept");
#endif
	return ok;
}

static bool testCap() {
	bool ok = true;
	QTemporaryDir temp;
	const QString root = temp.filePath("root");
	const RecoverySource source;
	QStringList ids;
	QStringList names;
	for (int index = 0; index <= RecoveryStore::kMaxKeptInstances; ++index) {
		RecoverySource named = source;
		named.displayName = QStringLiteral("n%1").arg(index);
		names.append(named.displayName);
		ids.append(leftBehind(root, "scene", named));
		QThread::msleep(20);
	}
	ok &= expect(!ids.contains(QString()), "cap: fixture");
	RecoveryStore store(root);
	const RecoveryScan scan = store.scan();
	ok &= expect(scan.candidates.size() == RecoveryStore::kMaxKeptInstances && scan.removedOverCap == 1,
		"cap: the oldest candidate beyond the cap is removed and counted");
	if (scan.candidates.size() == RecoveryStore::kMaxKeptInstances) {
		ok &= expect(scan.candidates.first().source.displayName == names.last() && scan.candidates.last().source.displayName == names.at(1),
			"cap: the list is newest first");
		ok &= expect(!QFileInfo::exists(at(root, ids.first())), "cap: the folder of the oldest candidate is gone");
	}
	ok &= expect(listNames(root).size() == RecoveryStore::kMaxKeptInstances, "cap: the root holds the kept folders");
	return ok;
}

static bool testClearAndClose() {
	bool ok = true;
	QTemporaryDir temp;
	const QString root = temp.filePath("root");
	QString folder;
	{
		RecoveryStore store(root);
		ok &= expect(!store.clear().ok, "clear: a closed store has nothing to clear");
		ok &= expect(store.open().ok && store.write(makeScene("A"), RecoverySource()).ok, "clear: fixture");
		folder = at(root, store.instanceId());
		const RecoveryResult cleared = store.clear();
		ok &= expect(cleared.ok && listNames(folder) == QStringList({"lock"}), "clear: only the lock stays");
	}
	ok &= expect(RecoveryStore(root).scan().candidates.isEmpty() && !QFileInfo::exists(folder),
		"clear: a store that ended after a clear leaves no candidate");

	RecoveryStore store(root);
	ok &= expect(store.open().ok && store.write(makeScene("A"), RecoverySource()).ok && store.clear().ok, "clear: second fixture");
	folder = at(root, store.instanceId());
	const RecoveryResult again = store.write(makeScene("B"), RecoverySource());
	ok &= expect(again.ok && again.bundlePath == at(folder, "gen-2.egscene"), "clear: the generation number is not reused");
	ok &= expect(store.close().ok && !store.isOpen() && !QFileInfo::exists(folder), "close: the folder is removed");
	ok &= expect(listNames(root).isEmpty(), "close: the root is empty");
	ok &= expect(store.close().ok, "close: closing twice is ok");
	ok &= expect(cleanError(store.write(makeScene("C"), RecoverySource()), root), "close: a write after close is an error");
	return ok;
}

static bool testAdopt() {
	bool ok = true;
	QTemporaryDir temp;
	const QString root = temp.filePath("root");
	const RecoverySource source = makeBundleSource(temp.filePath("source"), "source");
	const QString id = leftBehind(root, "adopted", source);
	RecoveryStore adopter(root);
	const RecoveryScan scan = adopter.scan();
	ok &= expect(!id.isEmpty() && scan.candidates.size() == 1, "adopt: fixture");
	if (scan.candidates.size() != 1)
		return false;
	const RecoveryCandidate candidate = scan.candidates.first();
	const QString candidateFolder = at(root, id);

	RecoveryStore closed(root);
	ok &= expect(cleanError(closed.adopt(candidate), root) && QFileInfo::exists(candidateFolder), "adopt: a closed store cannot adopt");

	QString ownFolder;
	{
		RecoveryStore owner(root);
		ok &= expect(owner.open().ok, "adopt: open");
		ownFolder = at(root, owner.instanceId());
		const RecoveryResult adopted = owner.adopt(candidate);
		ok &= expect(adopted.ok && adopted.bundlePath == at(ownFolder, "gen-1.egscene"), "adopt: adopt returns the new bundle");
		ok &= expect(listNames(ownFolder) == QStringList({"gen-1.egscene", "lock", "recovery.json"}),
			"adopt: the adopting folder holds the copy and the record");
		ok &= expect(!QFileInfo::exists(candidateFolder), "adopt: the candidate folder is gone");
		ok &= expect(bundleLoadsAs(adopted.bundlePath, "adopted"), "adopt: the adopted bundle loads");
		const QJsonObject record = readRecordObject(ownFolder);
		ok &= expect(record.value("instance_id").toString() == owner.instanceId() && record.value("display_name").toString() == source.displayName
				&& record.value("source").toObject().value("path").toString() == source.path,
			"adopt: the record keeps the source and the display name");
		ok &= expect(record.value("written_at").toString() == candidate.writtenAt.toString(Qt::ISODateWithMs), "adopt: the record keeps the time");
	}
	const RecoveryScan again = RecoveryStore(root).scan();
	ok &= expect(again.candidates.size() == 1, "adopt: the adopted copy is a candidate again after the adopter ended");
	if (again.candidates.size() == 1) {
		const RecoveryCandidate& next = again.candidates.first();
		ok &= expect(next.source.kind == candidate.source.kind && next.source.path == candidate.source.path
				&& next.source.displayName == candidate.source.displayName && next.source.sizeBytes == candidate.source.sizeBytes
				&& next.source.modified == candidate.source.modified && next.writtenAt == candidate.writtenAt
				&& next.sourceStatus == candidate.sourceStatus,
			"adopt: the fields survive the adoption");
	}

	// A candidate whose lock is held belongs to someone else.
	const QString busyId = leftBehind(root, "busy", source);
	const QString busyFolder = at(root, busyId);
	RecoveryCandidate busy;
	busy.instanceId = busyId;
	busy.bundlePath = at(busyFolder, "gen-1.egscene");
	RecoveryStore other(root);
	ok &= expect(!busyId.isEmpty() && other.open().ok, "adopt: busy fixture");
	QLockFile hold(at(busyFolder, "lock"));
	hold.setStaleLockTime(0);
	ok &= expect(hold.tryLock(0), "adopt: the test takes the lock");
	const QMap<QString, QByteArray> busyBefore = snapshot(busyFolder);
	ok &= expect(cleanError(other.adopt(busy), root) && snapshot(busyFolder) == busyBefore, "adopt: a candidate in use is refused and untouched");
	ok &= expect(listNames(at(root, other.instanceId())) == QStringList({"lock"}), "adopt: nothing is copied for a candidate in use");
	hold.unlock();

#ifdef Q_OS_UNIX
	// A record that cannot be written: the candidate stays whole, the copy that the adopting store
	// holds stays and no new copy remains.
	const QString ownFolderOfOther = at(root, other.instanceId());
	const QString ownRecord = at(ownFolderOfOther, "recovery.json");
	ok &= expect(other.write(makeScene("own"), RecoverySource()).ok, "adopt: the adopting store holds a copy");
	ok &= expect(QFile::remove(ownRecord) && QDir().mkpath(at(ownRecord, "obstacle")) && writeBytes(at(ownRecord, "obstacle/keep"), "k"),
		"adopt: obstacle fixture");
	const RecoveryScan listed = other.scan();
	const RecoveryCandidate* pending = findCandidate(listed, busyId);
	ok &= expect(pending != nullptr, "adopt: the busy candidate is listed once its lock is released");
	if (pending) {
		const QMap<QString, QByteArray> pendingBefore = snapshot(busyFolder);
		ok &= expect(cleanError(other.adopt(*pending), root), "adopt: an unwritable record gives an error");
		ok &= expect(findCandidate(other.scan(), busyId) != nullptr && snapshot(busyFolder) == pendingBefore,
			"adopt: the candidate is still listed and whole");
		ok &= expect(listNames(ownFolderOfOther) == QStringList({"gen-1.egscene", "lock", "recovery.json"}),
			"adopt: no new copy is left in the adopting folder");
		ok &= expect(bundleLoadsAs(at(ownFolderOfOther, "gen-1.egscene"), "own"), "adopt: the previous copy of the adopting folder stays");
	}
#endif
	return ok;
}

static bool testDiscard() {
	bool ok = true;
	QTemporaryDir temp;
	const QString root = temp.filePath("root");
	const QString id = leftBehind(root, "discarded", RecoverySource());
	RecoveryStore store(root);
	const RecoveryScan scan = store.scan();
	ok &= expect(!id.isEmpty() && scan.candidates.size() == 1, "discard: fixture");
	if (scan.candidates.size() == 1) {
		ok &= expect(store.discard(scan.candidates.first()).ok && !QFileInfo::exists(at(root, id)), "discard: the folder is removed");
		ok &= expect(!store.discard(scan.candidates.first()).ok, "discard: a folder that is gone gives an error");
	}

	RecoveryStore live(root);
	ok &= expect(live.open().ok && live.write(makeScene("live"), RecoverySource()).ok, "discard: live fixture");
	const QString liveFolder = at(root, live.instanceId());
	const QMap<QString, QByteArray> liveBefore = snapshot(liveFolder);
	RecoveryCandidate liveCandidate;
	liveCandidate.instanceId = live.instanceId();
	ok &= expect(!store.discard(liveCandidate).ok && snapshot(liveFolder) == liveBefore, "discard: a folder whose lock is held is refused");
	ok &= expect(!live.discard(liveCandidate).ok && snapshot(liveFolder) == liveBefore, "discard: a store does not discard its own folder");

	// Ids that are no instance ids never reach the file system.
	const QString nextToRoot = temp.filePath("outside");
	QDir().mkpath(nextToRoot);
	ok &= expect(writeBytes(at(nextToRoot, "keep"), "k") && writeBytes(temp.filePath("marker"), "m"), "discard: outside fixture");
	for (const QString& bad : {QString(), QString("x"), QString(".."), QString("../outside"), QString("{") + newId() + "}", newId().toUpper()}) {
		RecoveryCandidate candidate;
		candidate.instanceId = bad;
		ok &= expect(!store.discard(candidate).ok && !live.adopt(candidate).ok, "discard: an invalid id is refused");
	}
	ok &= expect(readBytes(at(nextToRoot, "keep")) == "k" && readBytes(temp.filePath("marker")) == "m" && snapshot(liveFolder) == liveBefore,
		"discard: nothing outside the root is touched");
	return ok;
}

static bool testSourceStatus() {
	bool ok = true;
	QTemporaryDir temp;
	RecoverySource bundle = makeBundleSource(temp.filePath("source"), "status");
	const QDateTime written = bundle.modified.addSecs(60);
	ok &= expect(bundle.sizeBytes > 0, "status: fixture");
	ok &= expect(recoverySourceStatus(RecoverySource(), written) == RecoverySourceStatus::NotApplicable, "status: an untitled scene has no source");
	RecoverySource noPath = bundle;
	noPath.path.clear();
	ok &= expect(recoverySourceStatus(noPath, written) == RecoverySourceStatus::NotApplicable, "status: a source without a path has no source");
	RecoverySource untitledWithPath = bundle;
	untitledWithPath.kind = RecoverySourceKind::Untitled;
	ok &= expect(recoverySourceStatus(untitledWithPath, written) == RecoverySourceStatus::NotApplicable,
		"status: an untitled scene has no source even when a path is set");
	ok &= expect(recoverySourceStatus(bundle, written) == RecoverySourceStatus::Unchanged, "status: an unchanged bundle");
	RecoverySource sized = bundle;
	sized.sizeBytes += 1;
	ok &= expect(recoverySourceStatus(sized, written) == RecoverySourceStatus::Changed, "status: a different size");
	RecoverySource noSize = bundle;
	noSize.sizeBytes = -1;
	ok &= expect(recoverySourceStatus(noSize, written) == RecoverySourceStatus::Unknown, "status: an unknown size");
	RecoverySource noTime = bundle;
	noTime.modified = QDateTime();
	ok &= expect(recoverySourceStatus(noTime, written) == RecoverySourceStatus::Unknown, "status: an unknown time");
	ok &= expect(setModified(bundle.path, bundle.modified.addSecs(-30)), "status: the bundle can be dated");
	ok &= expect(recoverySourceStatus(bundle, written) == RecoverySourceStatus::Changed, "status: a different time");
	ok &= expect(setModified(bundle.path, written.addSecs(30)), "status: the bundle can be dated later");
	ok &= expect(recoverySourceStatus(bundle, written) == RecoverySourceStatus::NewerThanCopy, "status: a source modified after the copy");
	ok &= expect(QFile::remove(bundle.path) && recoverySourceStatus(bundle, written) == RecoverySourceStatus::Missing, "status: a deleted bundle");

	const QString sceneFolder = temp.filePath("scene");
	QDir().mkpath(sceneFolder);
	ok &= expect(writeBytes(at(sceneFolder, "scene.json"), "{}"), "status: directory fixture");
	RecoverySource directory;
	directory.kind = RecoverySourceKind::Directory;
	directory.path = sceneFolder;
	directory.sizeBytes = QFileInfo(at(sceneFolder, "scene.json")).size();
	directory.modified = QFileInfo(at(sceneFolder, "scene.json")).lastModified().toUTC();
	const QDateTime directoryWritten = directory.modified.addSecs(60);
	ok &= expect(recoverySourceStatus(directory, directoryWritten) == RecoverySourceStatus::Unchanged,
		"status: an unchanged directory is judged by scene.json");
	ok &= expect(writeBytes(at(sceneFolder, "other.json"), "{}") && recoverySourceStatus(directory, directoryWritten) == RecoverySourceStatus::Unchanged,
		"status: other files of a directory do not count");
	ok &= expect(QFile::remove(at(sceneFolder, "scene.json")) && recoverySourceStatus(directory, directoryWritten) == RecoverySourceStatus::Missing,
		"status: a directory without scene.json");
	return ok;
}

static bool testUnreadableBundle() {
	bool ok = true;
#ifdef Q_OS_UNIX
	QTemporaryDir temp;
	const QString root = temp.filePath("root");
	const QString folder = makeDeadFolder(root);
	const QString bundle = at(folder, "gen-1.egscene");
	ok &= expect(saveGeneration(folder, 1, "A"), "unreadable: fixture");
	const QMap<QString, QByteArray> before = snapshot(folder);
	RecoveryStore store(root);
	{
		PermissionGuard guard(bundle);
		QFile::setPermissions(bundle, QFile::WriteOwner);
		if (refusesReading(bundle, "unreadable: a bundle without read permission")) {
			ok &= expect(store.scan().candidates.isEmpty(), "unreadable: a bundle that cannot be read is not listed");
			ok &= expect(listNames(folder) == QStringList({"gen-1.egscene"}), "unreadable: nothing is removed");
		}
	}
	const RecoveryScan scan = store.scan();
	ok &= expect(scan.candidates.size() == 1 && snapshot(folder) == before, "unreadable: the bundle is listed once it can be read");
#else
	std::cerr << "skipped: unreadable: a bundle without read permission (permission bits do not restrict reading here)\n";
#endif
	return ok;
}

// The program starts itself again as the child of the process cases.
static int runChildHold(const QString& root) {
	RecoveryStore store(root);
	RecoverySource source;
	source.displayName = "child";
	if (!store.open().ok || !store.write(makeScene("child"), source).ok)
		return 2;
	std::cout << "ready" << std::endl;
	// Blocks until the parent kills the process.
	while (std::cin.get() != std::char_traits<char>::eof()) {
	}
	return 0;
}

static int runChildCrash(const QString& root) {
	RecoveryStore store(root);
	RecoverySource source;
	source.displayName = "crashed";
	if (!store.open().ok || !store.write(makeScene("crashed"), source).ok)
		return 2;
	std::_Exit(3);
}

// Kills the child on every way out of a case.
class ChildGuard {
public:
	explicit ChildGuard(QProcess& child)
		: m_child(child) {}
	~ChildGuard() {
		if (m_child.state() != QProcess::NotRunning) {
			m_child.kill();
			m_child.waitForFinished(10000);
		}
	}
	ChildGuard(const ChildGuard&) = delete;
	ChildGuard& operator=(const ChildGuard&) = delete;

private:
	QProcess& m_child;
};

static bool startChild(QProcess& child, const QString& mode, const QString& root) {
	child.setProcessChannelMode(QProcess::ForwardedErrorChannel);
	child.start(QCoreApplication::applicationFilePath(), QStringList{mode, root});
	return child.waitForStarted(60000);
}

static bool waitForReady(QProcess& child) {
	QByteArray output;
	QElapsedTimer timer;
	timer.start();
	while (timer.elapsed() < 60000) {
		output += child.readAllStandardOutput();
		if (output.contains("ready"))
			return true;
		if (child.state() == QProcess::NotRunning)
			break;
		child.waitForReadyRead(200);
	}
	return false;
}

static bool testHeldByChild() {
	bool ok = true;
	QTemporaryDir temp;
	const QString root = temp.filePath("root");
	QProcess child;
	ChildGuard guard(child);
	if (!expectStage(startChild(child, "--child-hold", root), "child start", QString()))
		return false;
	if (!expectStage(waitForReady(child), "child ready", QString()))
		return false;

	RecoveryStore parent(root);
	const QStringList folders = listNames(root);
	if (!expectStage(folders.size() == 1, "live child folder", QString::number(folders.size()) + " folders"))
		return false;
	const QString folder = at(root, folders.first());
	const QMap<QString, QByteArray> before = snapshot(folder);
	const bool written = before.contains("gen-1.egscene") && before.contains("recovery.json");
	ok &= expectStage(written && parent.scan().candidates.isEmpty() && snapshot(folder) == before, "live child hidden",
		QStringLiteral("the folder of a running child is listed or changed"));

	child.kill();
	const bool ended = child.waitForFinished(60000);
	const QString note = QStringLiteral("lock file after kill: ") + (QFileInfo::exists(at(folder, "lock")) ? "present" : "absent");
	ok &= expectStage(ended, "child end after kill", note);
	const RecoveryScan first = parent.scan();
	const bool listed = first.candidates.size() == 1 && first.candidates.first().state == RecoveryCandidateState::Openable
		&& first.candidates.first().source.displayName == "child";
	ok &= expectStage(listed, "killed child listed", note + ", candidates: " + QString::number(first.candidates.size()));
	const RecoveryScan second = parent.scan();
	ok &= expectStage(second.candidates.size() == 1 && second.candidates.first().source.displayName == "child", "killed child listed again", note);
	return ok;
}

static bool testChildEndsWithoutClose() {
	bool ok = true;
	QTemporaryDir temp;
	const QString root = temp.filePath("root");
	QProcess child;
	ChildGuard guard(child);
	if (!expectStage(startChild(child, "--child-crash", root), "child start", QString()))
		return false;
	const bool ended = child.waitForFinished(60000);
	const bool exitedThree = ended && child.exitStatus() == QProcess::NormalExit && child.exitCode() == 3;
	ok &= expectStage(exitedThree, "child exit code", QString::number(child.exitCode()));

	RecoveryStore parent(root);
	const RecoveryScan scan = parent.scan();
	const QString note = QStringLiteral("candidates: ") + QString::number(scan.candidates.size());
	const bool listed = scan.candidates.size() == 1 && scan.candidates.first().state == RecoveryCandidateState::Openable;
	ok &= expectStage(listed, "exit without close listed", note);
	if (scan.candidates.size() != 1)
		return false;
	const QString candidateFolder = at(root, scan.candidates.first().instanceId);
	RecoveryStore adopter(root);
	const RecoveryResult adopted = adopter.open().ok ? adopter.adopt(scan.candidates.first()) : RecoveryResult();
	const bool taken = adopted.ok && bundleLoadsAs(adopted.bundlePath, "crashed") && !QFileInfo::exists(candidateFolder);
	ok &= expectStage(taken, "adopt after crash", adopted.error);
	return ok;
}

int main(int argc, char** argv) {
	QCoreApplication application(argc, argv);
	const QStringList arguments = QCoreApplication::arguments();
	if (arguments.size() == 3 && arguments.at(1) == "--child-hold")
		return runChildHold(arguments.at(2));
	if (arguments.size() == 3 && arguments.at(1) == "--child-crash")
		return runChildCrash(arguments.at(2));

	bool ok = true;
	if (arguments.size() == 2 && arguments.at(1) == "--processes") {
		ok &= testHeldByChild();
		ok &= testChildEndsWithoutClose();
	} else if (arguments.size() == 1) {
		ok &= testOpenAndLayout();
		ok &= testWrite();
		ok &= testFailedWrite();
		ok &= testLeftBehind();
		ok &= testNewestCompleteBundle();
		ok &= testRecordProblems();
		ok &= testPruning();
		ok &= testCap();
		ok &= testClearAndClose();
		ok &= testAdopt();
		ok &= testDiscard();
		ok &= testSourceStatus();
		ok &= testUnreadableBundle();
	} else {
		std::cerr << "usage: test_recoverystore [--processes]\n";
		return 2;
	}
	return ok ? 0 : 1;
}
