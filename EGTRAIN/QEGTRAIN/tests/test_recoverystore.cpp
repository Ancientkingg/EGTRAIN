#include "io/third_party/miniz/miniz.h"
#include "scene/SceneBundle.h"
#include "scene/SceneCompatibility.h"
#include "scene/SceneModel.h"

#include "recovery/RecoveryStore.h"

#include <QCoreApplication>
#include <QDateTime>
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
#include <QTest>
#include <QUuid>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

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

// A record file that the scan must not use, and the old message that describes it.
struct Variant {
	const char* message;
	std::function<QByteArray(const QString&)> makeRecord;
};

static std::function<QByteArray(const QString&)> withField(const char* key, const QJsonValue& value) {
	return [key, value](const QString& id) {
		QJsonObject record = validRecord(id);
		record.insert(key, value);
		return toBytes(record);
	};
}

static std::function<QByteArray(const QString&)> withSourceField(const char* key, const QJsonValue& value) {
	return [key, value](const QString& id) {
		QJsonObject record = validRecord(id);
		QJsonObject source = record.value("source").toObject();
		source.insert(key, value);
		record.insert("source", source);
		return toBytes(record);
	};
}

static const std::vector<Variant>& recordVariants() {
	static const std::vector<Variant> variants = {
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
	return variants;
}

class TestRecoveryStore : public QObject {
	Q_OBJECT

private slots:
	void opensInstanceFolder();
	void writesCopyAndRecord();
	void keepsCopyWhenWriteFails();
	void keepsEmptyNames();
	void listsCopyLeftBehind();
	void skipsOpenStores();
	void listsNewestCompleteBundle();
	void fallsBackFromTruncatedBundle();
	void dropsUnusableBundle_data();
	void dropsUnusableBundle();
	void readsValidRecord();
	void ignoresBadRecord_data();
	void ignoresBadRecord();
	void listsNewerRecordUnchanged();
	void listsOtherSceneVersionUnchanged();
	void prunesOnlyWhatIsUnusable();
	void keepsNewestCandidates();
	void clearsAndCloses();
	void adoptsCandidate();
	void discardsCandidate();
	void judgesSource();
	void skipsUnreadableBundle();
};

void TestRecoveryStore::opensInstanceFolder() {
	QTemporaryDir temp;
	QVERIFY2(temp.isValid(), "open: temporary folder");
	const QString root = temp.filePath("root");
	{
		RecoveryStore store(root);
		// A new store is closed.
		QVERIFY(!store.isOpen());
		QVERIFY(store.instanceId().isEmpty());
		const RecoveryResult opened = store.open();
		// Open succeeds.
		QVERIFY(opened.ok);
		QVERIFY(opened.error.isEmpty());
		QVERIFY(store.isOpen());
		const QString id = store.instanceId();
		const QString folder = at(root, id);
		QVERIFY2(isValidId(id), "open: the instance id is a UUID");
		// The root holds only the instance folder.
		QCOMPARE(listNames(root), QStringList{id});
		// The folder holds the lock file.
		QVERIFY(QFileInfo(folder).isDir());
		QVERIFY(QFileInfo(at(folder, "lock")).isFile());
#ifdef Q_OS_UNIX
		const QFile::Permissions others = QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup
			| QFile::ReadOther | QFile::WriteOther | QFile::ExeOther;
		QVERIFY2(!(QFile::permissions(folder) & others), "open: the folder is restricted to its owner");
#endif
		// Opening twice changes nothing.
		QVERIFY(store.open().ok);
		QCOMPARE(store.instanceId(), id);
		QCOMPARE(listNames(root).size(), 1);
	}

	const QString lockedRoot = temp.filePath("locked");
	QDir().mkpath(lockedRoot);
	PermissionGuard guard(lockedRoot);
	QFile::setPermissions(lockedRoot, QFile::ReadOwner | QFile::ExeOwner);
	if (refusesNewFiles(lockedRoot, "open: a read-only root")) {
		RecoveryStore store(lockedRoot);
		const RecoveryResult refused = store.open();
		QVERIFY2(cleanError(refused, lockedRoot), "open: a read-only root gives an error without a path");
		// A refused open leaves no folder.
		QVERIFY(!store.isOpen());
		QVERIFY(listNames(lockedRoot).isEmpty());
		guard.restore();
		// The same store opens once the root is writable.
		QVERIFY(store.open().ok);
		QVERIFY(store.isOpen());
	}
}

void TestRecoveryStore::writesCopyAndRecord() {
	QTemporaryDir temp;
	const QString root = temp.filePath("root");
	const QString sourceFolder = temp.filePath("source");
	const RecoverySource source = makeBundleSource(sourceFolder, "source");
	const QByteArray sourceBytes = readBytes(source.path);
	const QMap<QString, QByteArray> sourceBefore = snapshot(sourceFolder);
	QVERIFY2(!sourceBytes.isEmpty(), "write: the source bundle is written");

	RecoveryStore store(root);
	QVERIFY2(store.open().ok, "write: open");
	const QString folder = at(root, store.instanceId());
	const RecoveryResult first = store.write(makeScene("first"), source);
	// The first write returns its bundle.
	QVERIFY(first.ok);
	QVERIFY(first.error.isEmpty());
	QCOMPARE(first.bundlePath, at(folder, "gen-1.egscene"));
	// The folder holds the lock, the bundle and the record.
	QCOMPARE(listNames(folder), QStringList({"gen-1.egscene", "lock", "recovery.json"}));
	QVERIFY2(bundleLoadsAs(first.bundlePath, "first"), "write: the copy is an ordinary bundle that loads");

	const QJsonObject record = readRecordObject(folder);
	const QJsonObject recordSource = record.value("source").toObject();
	// The record names the version and the instance.
	QCOMPARE(record.value("record_version").toInt(), 1);
	QCOMPARE(record.value("instance_id").toString(), store.instanceId());
	// The record names the generation.
	QCOMPARE(record.value("bundle_file").toString(), "gen-1.egscene");
	QCOMPARE(record.value("generation").toInt(), 1);
	QVERIFY2(nearNow(QDateTime::fromString(record.value("written_at").toString(), Qt::ISODateWithMs)),
		"write: the record holds the time of the write");
	// The record holds the display name.
	QCOMPARE(record.value("display_name").toString(), source.displayName);
	// The record holds the source kind and path.
	QCOMPARE(recordSource.value("kind").toString(), "bundle");
	QCOMPARE(recordSource.value("path").toString(), source.path);
	// The record holds the source size.
	QCOMPARE(static_cast<qint64>(recordSource.value("size").toDouble()), source.sizeBytes);
	// The record holds the source time.
	QCOMPARE(recordSource.value("modified").toString(), source.modified.toString(Qt::ISODateWithMs));

	const RecoveryResult second = store.write(makeScene("second"), source);
	// The second write returns the next generation.
	QVERIFY(second.ok);
	QCOMPARE(second.bundlePath, at(folder, "gen-2.egscene"));
	// The previous generation is removed.
	QCOMPARE(listNames(folder), QStringList({"gen-2.egscene", "lock", "recovery.json"}));
	QVERIFY2(bundleLoadsAs(second.bundlePath, "second"), "write: the new copy loads");
	// The record names the new generation.
	QCOMPARE(readRecordObject(folder).value("generation").toInt(), 2);

	// The source is not touched.
	QCOMPARE(readBytes(source.path), sourceBytes);
	QVERIFY(snapshot(sourceFolder) == sourceBefore);
}

void TestRecoveryStore::keepsCopyWhenWriteFails() {
	QTemporaryDir temp;
	const QString root = temp.filePath("root");
	const RecoverySource source = makeBundleSource(temp.filePath("source"), "source");
	RecoveryStore store(root);
	QVERIFY2(store.open().ok, "failed write: open");
	const QString folder = at(root, store.instanceId());
	QVERIFY2(store.write(makeScene("first"), source).ok, "failed write: first write");
	const QMap<QString, QByteArray> before = snapshot(folder);

	SceneModel other = makeScene("other");
	other.schemaVersion = kCurrentSceneSchemaVersion + 1;
	const RecoveryResult refused = store.write(other, source);
	// A scene of another version gives an error without a path.
	QVERIFY(cleanError(refused, root));
	QVERIFY(refused.bundlePath.isEmpty());
	QVERIFY2(!refused.error.contains("scene."), "failed write: the error holds no diagnostic code");

	// An incomplete incident cannot be written: the error says which entry, in words and without the code.
	SceneModel incomplete = makeScene("incomplete");
	SceneIncident incident;
	incident.id = "first";
	incident.type = "signal_failure";
	incident.target = "sig.a";
	defaultScenarioIncidents(incomplete).push_back(incident);
	incident.id = "second";
	incident.target.clear();
	defaultScenarioIncidents(incomplete).push_back(incident);
	const RecoveryResult unfinished = store.write(incomplete, source);
	// An incomplete incident is named in the error.
	QVERIFY(cleanError(unfinished, root));
	QCOMPARE(unfinished.error, "Cannot write the recovery copy because incident 2 is incomplete");
	SceneModel newService = makeScene("new service");
	SceneService service;
	service.id = "service.a";
	newService.services.push_back(service);
	const RecoveryResult unfinishedService = store.write(newService, source);
	// A new service without a composition is named in the error.
	QVERIFY(cleanError(unfinishedService, root));
	QVERIFY(unfinishedService.error.endsWith(" because service 1 is incomplete"));
	QVERIFY2(snapshot(folder) == before, "failed write: the folder and the record are unchanged");
	QVERIFY2(bundleLoadsAs(at(folder, "gen-1.egscene"), "first"), "failed write: the previous bundle still loads");

#ifdef Q_OS_UNIX
	// A record that cannot be replaced: the new bundle goes again and the previous copy stays.
	const QString record = at(folder, "recovery.json");
	// A folder with a file in it takes the place of the record.
	QVERIFY(QFile::remove(record));
	QVERIFY(QDir().mkpath(at(record, "obstacle")));
	QVERIFY(writeBytes(at(record, "obstacle/keep"), "k"));
	const RecoveryResult blocked = store.write(makeScene("blocked"), source);
	QVERIFY2(cleanError(blocked, root), "failed write: an unwritable record gives an error without a path");
	// No new bundle is left behind.
	QCOMPARE(listNames(folder), QStringList({"gen-1.egscene", "lock", "recovery.json"}));
	QVERIFY2(bundleLoadsAs(at(folder, "gen-1.egscene"), "first"), "failed write: the previous bundle is still there after a record failure");
	// The record takes the place of the folder again.
	QVERIFY(QDir(record).removeRecursively());
	QVERIFY(writeBytes(record, before.value("recovery.json")));
	const RecoveryResult retried = store.write(makeScene("second"), source);
	// The generation did not advance.
	QVERIFY(retried.ok);
	QCOMPARE(retried.bundlePath, at(folder, "gen-2.egscene"));

	{
		PermissionGuard guard(folder);
		QFile::setPermissions(folder, QFile::ReadOwner | QFile::ExeOwner);
		if (refusesNewFiles(folder, "failed write: a read-only instance folder")) {
			const RecoveryResult locked = store.write(makeScene("third"), source);
			QVERIFY2(cleanError(locked, root), "failed write: a read-only folder gives an error without a path");
			QVERIFY2(bundleLoadsAs(at(folder, "gen-2.egscene"), "second"),
				"failed write: the previous bundle is still there in a read-only folder");
		}
	}
	// The folder is as before.
	QCOMPARE(listNames(folder), QStringList({"gen-2.egscene", "lock", "recovery.json"}));
#endif
}

// The editors allow an empty case, station and scenario name, and the copy keeps them.
void TestRecoveryStore::keepsEmptyNames() {
	QTemporaryDir temp;
	const RecoverySource source = makeBundleSource(temp.filePath("source"), "source");
	RecoveryStore store(temp.filePath("root"));
	QVERIFY2(store.open().ok, "empty names: open");
	SceneModel scene = makeScene("");
	SceneStation station;
	station.id = "station.a";
	scene.stations.push_back(station);
	scene.scenarios.front().name.clear();
	const RecoveryResult written = store.write(scene, source);
	// The copy is written.
	QVERIFY(written.ok);
	QVERIFY(written.error.isEmpty());
	const SceneLoadResult loaded = loadSceneBundle(written.bundlePath.toStdString());
	// The copy loads with the empty names.
	QVERIFY(!hasErrors(loaded.diagnostics));
	QVERIFY(loaded.scene.name.empty());
	QCOMPARE(loaded.scene.stations.size(), std::size_t(1));
	QCOMPARE(loaded.scene.stations[0].id, "station.a");
	QVERIFY(loaded.scene.stations[0].name.empty());
	QCOMPARE(loaded.scene.scenarios.size(), std::size_t(1));
	QVERIFY(loaded.scene.scenarios[0].name.empty());
}

void TestRecoveryStore::listsCopyLeftBehind() {
	QTemporaryDir temp;
	const QString root = temp.filePath("root");
	const RecoverySource source = makeBundleSource(temp.filePath("source"), "source");
	const QString id = leftBehind(root, "left behind", source);
	QVERIFY2(!id.isEmpty(), "left behind: fixture");

	RecoveryStore scanner(root);
	const RecoveryScan scan = scanner.scan();
	// One candidate.
	QCOMPARE(scan.candidates.size(), 1);
	QCOMPARE(scan.removedOverCap, 0);
	const RecoveryCandidate& candidate = scan.candidates.first();
	// The candidate is openable.
	QCOMPARE(candidate.instanceId, id);
	QVERIFY(candidate.state == RecoveryCandidateState::Openable);
	// The source kind and path are as written.
	QVERIFY(candidate.source.kind == RecoverySourceKind::Bundle);
	QCOMPARE(candidate.source.path, source.path);
	// The display name is as written.
	QCOMPARE(candidate.source.displayName, source.displayName);
	// The source size and time are as written.
	QCOMPARE(candidate.source.sizeBytes, source.sizeBytes);
	QCOMPARE(candidate.source.modified, source.modified);
	QVERIFY2(nearNow(candidate.writtenAt), "left behind: the written time is the time of the write");
	QVERIFY2(candidate.sourceStatus == RecoverySourceStatus::Unchanged, "left behind: the source is unchanged");
	// The bundle loads.
	QVERIFY(QFileInfo(candidate.bundlePath).isFile());
	QVERIFY(bundleLoadsAs(candidate.bundlePath, "left behind"));
}

// An open store is not a candidate. In one process the lock file is the proof of its owner.
void TestRecoveryStore::skipsOpenStores() {
	QTemporaryDir temp;
	const RecoverySource source = makeBundleSource(temp.filePath("source"), "source");
	const QString liveRoot = temp.filePath("live");
	RecoveryStore live(liveRoot);
	// The open store holds a copy.
	QVERIFY(live.open().ok);
	QVERIFY(live.write(makeScene("live"), source).ok);
	const QString liveFolder = at(liveRoot, live.instanceId());
	const QMap<QString, QByteArray> liveBefore = snapshot(liveFolder);
	// The folder holds the lock and the bundle.
	QVERIFY(liveBefore.contains("lock"));
	QVERIFY(liveBefore.contains("gen-1.egscene"));
	RecoveryStore other(liveRoot);
	// Another store does not list or touch an open store.
	QVERIFY(other.scan().candidates.isEmpty());
	QVERIFY(snapshot(liveFolder) == liveBefore);
	// A store skips its own folder.
	QVERIFY(live.scan().candidates.isEmpty());
	QVERIFY(snapshot(liveFolder) == liveBefore);
}

void TestRecoveryStore::listsNewestCompleteBundle() {
	QTemporaryDir temp;
	const QString root = temp.filePath("newest");
	const QString folder = makeDeadFolder(root);
	// Two generations are written.
	QVERIFY(saveGeneration(folder, 1, "A"));
	QVERIFY(saveGeneration(folder, 2, "B"));
	const RecoveryScan scan = RecoveryStore(root).scan();
	QVERIFY2(listsOnly(scan, at(folder, "gen-2.egscene"), "B"), "newest: the newest complete bundle is the candidate");
	// The older generation is removed.
	QCOMPARE(listNames(folder), QStringList({"gen-2.egscene"}));
}

void TestRecoveryStore::fallsBackFromTruncatedBundle() {
	QTemporaryDir temp;
	const QString root = temp.filePath("truncated");
	const QString folder = makeDeadFolder(root);
	// Two generations are written.
	QVERIFY(saveGeneration(folder, 1, "A"));
	QVERIFY(saveGeneration(folder, 2, "B"));
	const QByteArray whole = readBytes(at(folder, "gen-2.egscene"));
	QVERIFY2(writeBytes(at(folder, "gen-2.egscene"), whole.left(whole.size() / 2)), "truncated: fixture is cut");
	const RecoveryScan scan = RecoveryStore(root).scan();
	QVERIFY2(listsOnly(scan, at(folder, "gen-1.egscene"), "A"), "truncated: an incomplete newest bundle gives way to the previous one");
	// The incomplete bundle is removed.
	QCOMPARE(listNames(folder), QStringList({"gen-1.egscene"}));
}

void TestRecoveryStore::dropsUnusableBundle_data() {
	QTest::addColumn<bool>("halfLength");
	QTest::newRow("empty") << false;
	QTest::newRow("half length") << true;
}

void TestRecoveryStore::dropsUnusableBundle() {
	QFETCH(bool, halfLength);
	QTemporaryDir temp;
	const QString root = temp.filePath(halfLength ? "half" : "empty");
	const QString folder = makeDeadFolder(root);
	const QString bundle = at(folder, "gen-1.egscene");
	QVERIFY2(saveGeneration(folder, 1, "A"), "unusable: fixture");
	const QByteArray whole = readBytes(bundle);
	QVERIFY2(writeBytes(bundle, halfLength ? whole.left(whole.size() / 2) : QByteArray()), "unusable: fixture is cut");
	const SceneCompatibilityProbeResult probe = probeSceneCompatibility(bundle.toStdString());
	// The bundle inspection rejects the file and does not report an unreadable file.
	QVERIFY(probe.classification == SceneCompatibilityClass::Malformed);
	QVERIFY(!probe.diagnostics.empty());
	QVERIFY(!hasCode(probe, "scene.bundle.file.read"));
	const RecoveryScan scan = RecoveryStore(root).scan();
	// The bundle is not listed and its folder goes.
	QVERIFY(scan.candidates.isEmpty());
	QVERIFY(!QFileInfo::exists(folder));
}

void TestRecoveryStore::readsValidRecord() {
	const OneFolderScan valid = scanWithRecord([](const QString& id) { return toBytes(validRecord(id)); });
	// A valid record gives a candidate.
	QCOMPARE(valid.scan.candidates.size(), 1);
	const OneFolderScan atLimit = scanWithRecord([](const QString& id) { return recordOfSize(id, 65536); });
	// A record at the size limit is read.
	QCOMPARE(atLimit.scan.candidates.size(), 1);
	QVERIFY(atLimit.scan.candidates.first().source.kind == RecoverySourceKind::Directory);
	const RecoveryCandidate& candidate = valid.scan.candidates.first();
	const QDateTime recordedModified = QDateTime::fromString("2026-01-02T03:04:05.678Z", Qt::ISODateWithMs);
	// A valid record gives the recorded source.
	QVERIFY(candidate.source.kind == RecoverySourceKind::Directory);
	QCOMPARE(candidate.source.path, "/some/where");
	QCOMPARE(candidate.source.displayName, "Recorded");
	QCOMPARE(candidate.source.sizeBytes, qint64(1234));
	QCOMPARE(candidate.source.modified, recordedModified);
	// A valid record gives the recorded time.
	QCOMPARE(candidate.writtenAt, QDateTime::fromString("2026-01-02T03:04:06.789Z", Qt::ISODateWithMs));
	QVERIFY2(candidate.sourceStatus == RecoverySourceStatus::Missing, "record: the recorded source is judged");
}

void TestRecoveryStore::ignoresBadRecord_data() {
	QTest::addColumn<int>("variant");
	const std::vector<Variant>& variants = recordVariants();
	for (std::size_t index = 0; index < variants.size(); ++index)
		QTest::newRow(variants[index].message) << static_cast<int>(index);
}

// The row name is the old message of the variant.
void TestRecoveryStore::ignoresBadRecord() {
	QFETCH(int, variant);
	QVERIFY(hasNoRecordValues(scanWithRecord(recordVariants().at(static_cast<std::size_t>(variant)).makeRecord)));
}

// A record of a newer layout: the folder is listed and nothing in it changes.
void TestRecoveryStore::listsNewerRecordUnchanged() {
	QTemporaryDir temp;
	const QString root = temp.filePath("newer");
	const QString folder = makeDeadFolder(root);
	const QString id = QFileInfo(folder).fileName();
	QJsonObject record = validRecord(id);
	record.insert("record_version", 2);
	record.insert("future_field", "x");
	// Two generations, the record and the stray files of an interrupted write are written.
	QVERIFY(saveGeneration(folder, 1, "A"));
	QVERIFY(saveGeneration(folder, 2, "B"));
	QVERIFY(writeBytes(at(folder, "recovery.json"), toBytes(record)));
	QVERIFY(writeBytes(at(folder, "recovery.json.x"), "temporary"));
	QVERIFY(QDir().mkpath(at(folder, "gen-2.egscene.staging-1-0")));
	const QMap<QString, QByteArray> before = snapshot(folder);
	RecoveryStore store(root);
	const RecoveryScan scan = store.scan();
	// The folder is listed as such.
	QCOMPARE(scan.candidates.size(), 1);
	QVERIFY(scan.candidates.first().state == RecoveryCandidateState::NewerRecordVersion);
	const RecoveryCandidate& candidate = scan.candidates.first();
	// The newest valid bundle is named.
	QCOMPARE(candidate.bundlePath, at(folder, "gen-2.egscene"));
	// The source is not read.
	QVERIFY(candidate.source.path.isEmpty());
	QVERIFY(candidate.source.displayName.isEmpty());
	QVERIFY(candidate.sourceStatus == RecoverySourceStatus::NotApplicable);
	QVERIFY2(nearNow(candidate.writtenAt), "newer record: the time is the time of the record file");
	QVERIFY2(snapshot(folder) == before, "newer record: no file of the folder changes");

	// A newer record without a bundle is listed too. The bundles are removed.
	QVERIFY(QFile::remove(at(folder, "gen-1.egscene")));
	QVERIFY(QFile::remove(at(folder, "gen-2.egscene")));
	const QMap<QString, QByteArray> bare = snapshot(folder);
	const RecoveryScan bareScan = store.scan();
	// A folder without a bundle is listed and kept.
	QCOMPARE(bareScan.candidates.size(), 1);
	QVERIFY(bareScan.candidates.first().bundlePath.isEmpty());
	QVERIFY(snapshot(folder) == bare);

	// Neither state can be adopted, and both can be discarded.
	RecoveryStore adopter(root);
	// Adopter opens.
	QVERIFY(adopter.open().ok);
	QCOMPARE(bareScan.candidates.size(), 1);
	const QMap<QString, QByteArray> adopterBefore = snapshot(at(root, adopter.instanceId()));
	// Adopt refuses and changes nothing.
	QVERIFY(cleanError(adopter.adopt(bareScan.candidates.first()), root));
	QVERIFY(snapshot(folder) == bare);
	QVERIFY2(snapshot(at(root, adopter.instanceId())) == adopterBefore, "newer record: the adopter folder is unchanged");
	// Discard removes the folder.
	QVERIFY(adopter.discard(bareScan.candidates.first()).ok);
	QVERIFY(!QFileInfo::exists(folder));
}

void TestRecoveryStore::listsOtherSceneVersionUnchanged() {
	QTemporaryDir temp;
	const QString root = temp.filePath("other");
	const QString folder = makeDeadFolder(root);
	QVERIFY2(writeNewerBundle(at(folder, "gen-1.egscene")), "other version: fixture");
	const QMap<QString, QByteArray> before = snapshot(folder);
	RecoveryStore store(root);
	const RecoveryScan scan = store.scan();
	// A newer bundle is listed as such.
	QCOMPARE(scan.candidates.size(), 1);
	QVERIFY(scan.candidates.first().state == RecoveryCandidateState::OtherSceneVersion);
	QVERIFY2(snapshot(folder) == before, "other version: the bundle is kept");
	RecoveryStore adopter(root);
	// Adopter opens.
	QVERIFY(adopter.open().ok);
	QCOMPARE(scan.candidates.size(), 1);
	// Adopt refuses and changes nothing.
	QVERIFY(cleanError(adopter.adopt(scan.candidates.first()), root));
	QVERIFY(snapshot(folder) == before);
	// The adopter folder is unchanged.
	QCOMPARE(listNames(at(root, adopter.instanceId())), QStringList({"lock"}));
	// Discard removes the folder.
	QVERIFY(adopter.discard(scan.candidates.first()).ok);
	QVERIFY(!QFileInfo::exists(folder));
}

void TestRecoveryStore::prunesOnlyWhatIsUnusable() {
	QTemporaryDir temp;
	const QString root = temp.filePath("root");
	const QString outside = temp.filePath("outside");
	QDir().mkpath(outside);
	const QString folder = makeDeadFolder(root);
	// The folder holds a bundle, and a folder outside the root holds a file and a bundle.
	QVERIFY(saveGeneration(folder, 1, "A"));
	QVERIFY(writeBytes(at(outside, "inside.txt"), "inside"));
	QVERIFY(saveGeneration(outside, 1, "outside"));
	const QString staging = at(folder, "gen-1.egscene.staging-1-0");
	// The stray entries of an interrupted write.
	QVERIFY(QDir().mkpath(staging));
	QVERIFY(writeBytes(at(staging, "bundle.egscene"), "partial"));
	QVERIFY(writeBytes(at(folder, "recovery.json.x1"), "temporary"));
	// Names that are not generations hold valid scenes, so that a rule that took them for one would show.
	const QByteArray bundleBytes = readBytes(at(folder, "gen-1.egscene"));
	const QString sceneFolder = at(folder, "gen-5.egscene");
	const QStringList unknownFiles = {"notes.txt", "gen-0.egscene", "gen-01.egscene", "gen-02.egscene", "gen-1234567890.egscene", "gen-01.egscene.staging-1"};
	bool unknownWritten = saveScene(makeScene("directory"), sceneFolder.toStdString()).success();
	for (const QString& name : unknownFiles)
		unknownWritten &= writeBytes(at(folder, name), name == "notes.txt" ? QByteArray("notes") : bundleBytes);
	QVERIFY2(unknownWritten, "prune: unknown names");
	const QMap<QString, QByteArray> sceneFolderBefore = snapshot(sceneFolder);
	const QString rootFile = at(root, "plain.txt");
	const QString otherFolder = at(root, "not-an-id");
	// A file and a folder with another name in the root.
	QVERIFY(writeBytes(rootFile, "plain"));
	QVERIFY(QDir().mkpath(otherFolder));
	QVERIFY(saveGeneration(otherFolder, 1, "B"));
	const QMap<QString, QByteArray> otherBefore = snapshot(otherFolder);
#ifdef Q_OS_UNIX
	const QString linkedId = newId();
	// Links with the name of an instance and of a generation.
	QVERIFY(QFile::link(outside, at(root, linkedId)));
	QVERIFY(QFile::link(at(outside, "gen-1.egscene"), at(folder, "gen-9.egscene")));
#endif
	const QMap<QString, QByteArray> outsideBefore = snapshot(outside);

	RecoveryStore store(root);
	const RecoveryScan scan = store.scan();
	// Only the folder of the ended instance is listed.
	QCOMPARE(scan.candidates.size(), 1);
	QCOMPARE(scan.candidates.first().bundlePath, at(folder, "gen-1.egscene"));
	QVERIFY2(!QFileInfo::exists(at(folder, "gen-1.egscene.staging-1-0")), "prune: a staging folder of an interrupted write is removed");
	QVERIFY2(!QFileInfo::exists(at(folder, "recovery.json.x1")), "prune: a temporary record is removed");
	for (const QString& name : unknownFiles) {
		// An unknown name is kept.
		QVERIFY(QFileInfo(at(folder, name)).isFile());
		QCOMPARE(readBytes(at(folder, name)), name == "notes.txt" ? QByteArray("notes") : bundleBytes);
	}
	// A folder named like a generation is kept.
	QVERIFY(snapshot(sceneFolder) == sceneFolderBefore);
	QVERIFY(QFileInfo(sceneFolder).isDir());
	QVERIFY2(snapshot(otherFolder) == otherBefore, "prune: a folder with another name is kept");
	// A file in the root is kept.
	QCOMPARE(readBytes(rootFile), QByteArray("plain"));
	QVERIFY2(snapshot(outside) == outsideBefore, "prune: the files outside the root are kept");
#ifdef Q_OS_UNIX
	QVERIFY2(QFileInfo(at(root, linkedId)).isSymLink(), "prune: a link with the name of an instance is kept");
	QVERIFY2(QFileInfo(at(folder, "gen-9.egscene")).isSymLink(), "prune: a link with the name of a generation is kept");
#endif
}

void TestRecoveryStore::keepsNewestCandidates() {
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
		// The next copy is written in a later millisecond, so that the scan can order the copies.
		const QDateTime written = QDateTime::currentDateTimeUtc();
		QTRY_VERIFY_WITH_TIMEOUT(QDateTime::currentDateTimeUtc() > written, 20);
	}
	QVERIFY2(!ids.contains(QString()), "cap: fixture");
	RecoveryStore store(root);
	const RecoveryScan scan = store.scan();
	// The oldest candidate beyond the cap is removed and counted.
	QCOMPARE(scan.candidates.size(), RecoveryStore::kMaxKeptInstances);
	QCOMPARE(scan.removedOverCap, 1);
	// The list is newest first.
	QCOMPARE(scan.candidates.first().source.displayName, names.last());
	QCOMPARE(scan.candidates.last().source.displayName, names.at(1));
	QVERIFY2(!QFileInfo::exists(at(root, ids.first())), "cap: the folder of the oldest candidate is gone");
	// The root holds the kept folders.
	QCOMPARE(listNames(root).size(), RecoveryStore::kMaxKeptInstances);
}

void TestRecoveryStore::clearsAndCloses() {
	QTemporaryDir temp;
	const QString root = temp.filePath("root");
	QString folder;
	{
		RecoveryStore store(root);
		QVERIFY2(!store.clear().ok, "clear: a closed store has nothing to clear");
		// The store holds a copy.
		QVERIFY(store.open().ok);
		QVERIFY(store.write(makeScene("A"), RecoverySource()).ok);
		folder = at(root, store.instanceId());
		const RecoveryResult cleared = store.clear();
		// Only the lock stays.
		QVERIFY(cleared.ok);
		QCOMPARE(listNames(folder), QStringList({"lock"}));
	}
	// A store that ended after a clear leaves no candidate.
	QVERIFY(RecoveryStore(root).scan().candidates.isEmpty());
	QVERIFY(!QFileInfo::exists(folder));

	RecoveryStore store(root);
	// The store wrote a copy and cleared it.
	QVERIFY(store.open().ok);
	QVERIFY(store.write(makeScene("A"), RecoverySource()).ok);
	QVERIFY(store.clear().ok);
	folder = at(root, store.instanceId());
	const RecoveryResult again = store.write(makeScene("B"), RecoverySource());
	// The generation number is not reused.
	QVERIFY(again.ok);
	QCOMPARE(again.bundlePath, at(folder, "gen-2.egscene"));
	// Close removes the folder.
	QVERIFY(store.close().ok);
	QVERIFY(!store.isOpen());
	QVERIFY(!QFileInfo::exists(folder));
	QVERIFY2(listNames(root).isEmpty(), "close: the root is empty");
	QVERIFY2(store.close().ok, "close: closing twice is ok");
	QVERIFY2(cleanError(store.write(makeScene("C"), RecoverySource()), root), "close: a write after close is an error");
}

void TestRecoveryStore::adoptsCandidate() {
	QTemporaryDir temp;
	const QString root = temp.filePath("root");
	const RecoverySource source = makeBundleSource(temp.filePath("source"), "source");
	const QString id = leftBehind(root, "adopted", source);
	RecoveryStore adopter(root);
	const RecoveryScan scan = adopter.scan();
	// A copy is left behind.
	QVERIFY(!id.isEmpty());
	QCOMPARE(scan.candidates.size(), 1);
	const RecoveryCandidate candidate = scan.candidates.first();
	const QString candidateFolder = at(root, id);

	RecoveryStore closed(root);
	// A closed store cannot adopt.
	QVERIFY(cleanError(closed.adopt(candidate), root));
	QVERIFY(QFileInfo::exists(candidateFolder));

	QString ownFolder;
	{
		RecoveryStore owner(root);
		QVERIFY2(owner.open().ok, "adopt: open");
		ownFolder = at(root, owner.instanceId());
		const RecoveryResult adopted = owner.adopt(candidate);
		// Adopt returns the new bundle.
		QVERIFY(adopted.ok);
		QCOMPARE(adopted.bundlePath, at(ownFolder, "gen-1.egscene"));
		// The adopting folder holds the copy and the record.
		QCOMPARE(listNames(ownFolder), QStringList({"gen-1.egscene", "lock", "recovery.json"}));
		QVERIFY2(!QFileInfo::exists(candidateFolder), "adopt: the candidate folder is gone");
		QVERIFY2(bundleLoadsAs(adopted.bundlePath, "adopted"), "adopt: the adopted bundle loads");
		const QJsonObject record = readRecordObject(ownFolder);
		// The record keeps the source and the display name.
		QCOMPARE(record.value("instance_id").toString(), owner.instanceId());
		QCOMPARE(record.value("display_name").toString(), source.displayName);
		QCOMPARE(record.value("source").toObject().value("path").toString(), source.path);
		// The record keeps the time.
		QCOMPARE(record.value("written_at").toString(), candidate.writtenAt.toString(Qt::ISODateWithMs));
	}
	const RecoveryScan again = RecoveryStore(root).scan();
	// The adopted copy is a candidate again after the adopter ended.
	QCOMPARE(again.candidates.size(), 1);
	const RecoveryCandidate& next = again.candidates.first();
	// The fields survive the adoption.
	QVERIFY(next.source.kind == candidate.source.kind);
	QCOMPARE(next.source.path, candidate.source.path);
	QCOMPARE(next.source.displayName, candidate.source.displayName);
	QCOMPARE(next.source.sizeBytes, candidate.source.sizeBytes);
	QCOMPARE(next.source.modified, candidate.source.modified);
	QCOMPARE(next.writtenAt, candidate.writtenAt);
	QVERIFY(next.sourceStatus == candidate.sourceStatus);

	// A candidate whose lock is held belongs to someone else.
	const QString busyId = leftBehind(root, "busy", source);
	const QString busyFolder = at(root, busyId);
	RecoveryCandidate busy;
	busy.instanceId = busyId;
	busy.bundlePath = at(busyFolder, "gen-1.egscene");
	RecoveryStore other(root);
	// A second copy is left behind and another store is open.
	QVERIFY(!busyId.isEmpty());
	QVERIFY(other.open().ok);
	QLockFile hold(at(busyFolder, "lock"));
	hold.setStaleLockTime(0);
	QVERIFY2(hold.tryLock(0), "adopt: the test takes the lock");
	const QMap<QString, QByteArray> busyBefore = snapshot(busyFolder);
	// A candidate in use is refused and untouched.
	QVERIFY(cleanError(other.adopt(busy), root));
	QVERIFY(snapshot(busyFolder) == busyBefore);
	// Nothing is copied for a candidate in use.
	QCOMPARE(listNames(at(root, other.instanceId())), QStringList({"lock"}));
	hold.unlock();

#ifdef Q_OS_UNIX
	// A record that cannot be written: the candidate stays whole, the copy that the adopting store
	// holds stays and no new copy remains.
	const QString ownFolderOfOther = at(root, other.instanceId());
	const QString ownRecord = at(ownFolderOfOther, "recovery.json");
	QVERIFY2(other.write(makeScene("own"), RecoverySource()).ok, "adopt: the adopting store holds a copy");
	// A folder with a file in it takes the place of the record.
	QVERIFY(QFile::remove(ownRecord));
	QVERIFY(QDir().mkpath(at(ownRecord, "obstacle")));
	QVERIFY(writeBytes(at(ownRecord, "obstacle/keep"), "k"));
	const RecoveryScan listed = other.scan();
	const RecoveryCandidate* pending = findCandidate(listed, busyId);
	if (pending == nullptr)
		QFAIL("adopt: the busy candidate is listed once its lock is released");
	const QMap<QString, QByteArray> pendingBefore = snapshot(busyFolder);
	QVERIFY2(cleanError(other.adopt(*pending), root), "adopt: an unwritable record gives an error");
	// The candidate is still listed and whole.
	QVERIFY(findCandidate(other.scan(), busyId) != nullptr);
	QVERIFY(snapshot(busyFolder) == pendingBefore);
	// No new copy is left in the adopting folder.
	QCOMPARE(listNames(ownFolderOfOther), QStringList({"gen-1.egscene", "lock", "recovery.json"}));
	QVERIFY2(bundleLoadsAs(at(ownFolderOfOther, "gen-1.egscene"), "own"), "adopt: the previous copy of the adopting folder stays");
#endif
}

void TestRecoveryStore::discardsCandidate() {
	QTemporaryDir temp;
	const QString root = temp.filePath("root");
	const QString id = leftBehind(root, "discarded", RecoverySource());
	RecoveryStore store(root);
	const RecoveryScan scan = store.scan();
	// A copy is left behind.
	QVERIFY(!id.isEmpty());
	QCOMPARE(scan.candidates.size(), 1);
	// The folder is removed.
	QVERIFY(store.discard(scan.candidates.first()).ok);
	QVERIFY(!QFileInfo::exists(at(root, id)));
	QVERIFY2(!store.discard(scan.candidates.first()).ok, "discard: a folder that is gone gives an error");

	RecoveryStore live(root);
	// An open store holds a copy.
	QVERIFY(live.open().ok);
	QVERIFY(live.write(makeScene("live"), RecoverySource()).ok);
	const QString liveFolder = at(root, live.instanceId());
	const QMap<QString, QByteArray> liveBefore = snapshot(liveFolder);
	RecoveryCandidate liveCandidate;
	liveCandidate.instanceId = live.instanceId();
	// A folder whose lock is held is refused.
	QVERIFY(!store.discard(liveCandidate).ok);
	QVERIFY(snapshot(liveFolder) == liveBefore);
	// A store does not discard its own folder.
	QVERIFY(!live.discard(liveCandidate).ok);
	QVERIFY(snapshot(liveFolder) == liveBefore);

	// Ids that are no instance ids never reach the file system.
	const QString nextToRoot = temp.filePath("outside");
	QDir().mkpath(nextToRoot);
	// A file next to the root and a file in the temporary folder.
	QVERIFY(writeBytes(at(nextToRoot, "keep"), "k"));
	QVERIFY(writeBytes(temp.filePath("marker"), "m"));
	for (const QString& bad : {QString(), QString("x"), QString(".."), QString("../outside"), QString("{") + newId() + "}", newId().toUpper()}) {
		RecoveryCandidate candidate;
		candidate.instanceId = bad;
		// An invalid id is refused.
		QVERIFY(!store.discard(candidate).ok);
		QVERIFY(!live.adopt(candidate).ok);
	}
	// Nothing outside the root is touched.
	QCOMPARE(readBytes(at(nextToRoot, "keep")), QByteArray("k"));
	QCOMPARE(readBytes(temp.filePath("marker")), QByteArray("m"));
	QVERIFY(snapshot(liveFolder) == liveBefore);
}

void TestRecoveryStore::judgesSource() {
	QTemporaryDir temp;
	RecoverySource bundle = makeBundleSource(temp.filePath("source"), "status");
	const QDateTime written = bundle.modified.addSecs(60);
	QVERIFY2(bundle.sizeBytes > 0, "status: fixture");
	QVERIFY2(recoverySourceStatus(RecoverySource(), written) == RecoverySourceStatus::NotApplicable, "status: an untitled scene has no source");
	RecoverySource noPath = bundle;
	noPath.path.clear();
	QVERIFY2(recoverySourceStatus(noPath, written) == RecoverySourceStatus::NotApplicable, "status: a source without a path has no source");
	RecoverySource untitledWithPath = bundle;
	untitledWithPath.kind = RecoverySourceKind::Untitled;
	QVERIFY2(recoverySourceStatus(untitledWithPath, written) == RecoverySourceStatus::NotApplicable,
		"status: an untitled scene has no source even when a path is set");
	QVERIFY2(recoverySourceStatus(bundle, written) == RecoverySourceStatus::Unchanged, "status: an unchanged bundle");
	RecoverySource sized = bundle;
	sized.sizeBytes += 1;
	QVERIFY2(recoverySourceStatus(sized, written) == RecoverySourceStatus::Changed, "status: a different size");
	RecoverySource noSize = bundle;
	noSize.sizeBytes = -1;
	QVERIFY2(recoverySourceStatus(noSize, written) == RecoverySourceStatus::Unknown, "status: an unknown size");
	RecoverySource noTime = bundle;
	noTime.modified = QDateTime();
	QVERIFY2(recoverySourceStatus(noTime, written) == RecoverySourceStatus::Unknown, "status: an unknown time");
	QVERIFY2(setModified(bundle.path, bundle.modified.addSecs(-30)), "status: the bundle can be dated");
	QVERIFY2(recoverySourceStatus(bundle, written) == RecoverySourceStatus::Changed, "status: a different time");
	QVERIFY2(setModified(bundle.path, written.addSecs(30)), "status: the bundle can be dated later");
	QVERIFY2(recoverySourceStatus(bundle, written) == RecoverySourceStatus::NewerThanCopy, "status: a source modified after the copy");
	// A deleted bundle.
	QVERIFY(QFile::remove(bundle.path));
	QVERIFY(recoverySourceStatus(bundle, written) == RecoverySourceStatus::Missing);

	const QString sceneFolder = temp.filePath("scene");
	QDir().mkpath(sceneFolder);
	QVERIFY2(writeBytes(at(sceneFolder, "scene.json"), "{}"), "status: directory fixture");
	RecoverySource directory;
	directory.kind = RecoverySourceKind::Directory;
	directory.path = sceneFolder;
	directory.sizeBytes = QFileInfo(at(sceneFolder, "scene.json")).size();
	directory.modified = QFileInfo(at(sceneFolder, "scene.json")).lastModified().toUTC();
	const QDateTime directoryWritten = directory.modified.addSecs(60);
	QVERIFY2(recoverySourceStatus(directory, directoryWritten) == RecoverySourceStatus::Unchanged,
		"status: an unchanged directory is judged by scene.json");
	// Other files of a directory do not count.
	QVERIFY(writeBytes(at(sceneFolder, "other.json"), "{}"));
	QVERIFY(recoverySourceStatus(directory, directoryWritten) == RecoverySourceStatus::Unchanged);
	// A directory without scene.json.
	QVERIFY(QFile::remove(at(sceneFolder, "scene.json")));
	QVERIFY(recoverySourceStatus(directory, directoryWritten) == RecoverySourceStatus::Missing);
}

void TestRecoveryStore::skipsUnreadableBundle() {
#ifndef Q_OS_UNIX
	QSKIP("unreadable: a bundle without read permission (permission bits do not restrict reading here)");
#else
	QTemporaryDir temp;
	const QString root = temp.filePath("root");
	const QString folder = makeDeadFolder(root);
	const QString bundle = at(folder, "gen-1.egscene");
	QVERIFY2(saveGeneration(folder, 1, "A"), "unreadable: fixture");
	const QMap<QString, QByteArray> before = snapshot(folder);
	RecoveryStore store(root);
	{
		PermissionGuard guard(bundle);
		QFile::setPermissions(bundle, QFile::WriteOwner);
		if (refusesReading(bundle, "unreadable: a bundle without read permission")) {
			QVERIFY2(store.scan().candidates.isEmpty(), "unreadable: a bundle that cannot be read is not listed");
			// Nothing is removed.
			QCOMPARE(listNames(folder), QStringList({"gen-1.egscene"}));
		}
	}
	const RecoveryScan scan = store.scan();
	// The bundle is listed once it can be read.
	QCOMPARE(scan.candidates.size(), 1);
	QVERIFY(snapshot(folder) == before);
#endif
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
	// The process cases start copies of the program and run without Qt Test.
	if (arguments.size() == 2 && arguments.at(1) == "--processes") {
		bool ok = testHeldByChild();
		ok &= testChildEndsWithoutClose();
		return ok ? 0 : 1;
	}
	TestRecoveryStore test;
	return QTest::qExec(&test, argc, argv);
}

#include "test_recoverystore.moc"
