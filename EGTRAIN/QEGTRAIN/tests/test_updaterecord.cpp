#include "update/UpdateRecord.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

static bool writeFile(const QString& path, const QByteArray& contents) {
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

static const QByteArray kValidRecord("outcome=rolled-back\nversion=9.8.7\ntime=2026-10-09T12:00:00Z\n");
static const QByteArray kNotAppliedRecord("outcome=not-applied\nversion=9.8.7\ntime=2026-10-09T12:00:00Z\n");
static const QByteArray kAppliedRecord("outcome=applied\nversion=9.8.7\ntime=2026-10-09T12:00:00Z\n");
static const QByteArray kInstallerSucceeded("outcome=installer-succeeded\nversion=9.8.7\ntime=2026-10-09T12:00:00Z\n");
static const QByteArray kInstallerFailed("outcome=installer-failed\nversion=9.8.7\ntime=2026-10-09T12:00:00Z\nexit-code=5\n");

// The version of the records, and the version that takeUpdateRecord is told is installed, which is older.
static const SemanticVersion kRecordVersion{9, 8, 7};
static const SemanticVersion kInstalled{9, 8, 6};

// A valid record followed by an unknown key that makes the whole file the given size.
static QByteArray recordOfSize(int size) {
	QByteArray contents = kValidRecord + "padding=";
	contents.append(QByteArray(size - contents.size(), 'x'));
	return contents;
}

static QDateTime recordTime() {
	return QDateTime(QDate(2026, 10, 9), QTime(12, 0, 0), QTimeZone::utc());
}

// Nothing here reads or writes updateRecordPath(): the record folder of a real installation is not touched.
class TestUpdateRecord : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void parsesRolledBackRecord();
	void parsesNotAppliedRecord();
	void parsesAppliedRecord();
	void readsRecordInAnyLayout();
	void rejectsRecord_data();
	void rejectsRecord();

	void givesNoRecordWithoutFile();
	void takesRecordOfNewerVersion();
	void takesNotAppliedRecord();
	void dropsRecordOfInstalledVersion();
	void dropsRecordOfOlderVersion();
	void takesAppliedRecordOnlyForItsVersion();
	void dropsMalformedRecord();
	void readsRecordOf4096Bytes();
	void refusesFileOfMoreThan4096Bytes();
	void leavesFolderAtRecordPath();

	void parsesSucceededInstallerRecord();
	void parsesFailedInstallerRecord();
	void readsInstallerExitCode();
	void rejectsInstallerRecord_data();
	void rejectsInstallerRecord();
	void updateReaderRefusesInstallerRecord();
	void checksInstallerRecord();

	void givesNoInstallerRecordWithoutFile();
	void takesInstallerRecord();
	void leavesUpdateRecordsForTheirReader();
	void updateReaderRemovesInstallerRecord();
	void leavesMalformedInstallerRecord();
	void refusesLongInstallerRecord();
	void leavesFolderAtInstallerRecordPath();

	void describesOutcomes();
	void namesRecordAndHelperOptions();

private:
	QString path(const char* name) const { return QDir(m_temp.path()).filePath(QString::fromLatin1(name)); }

	QTemporaryDir m_temp;
};

void TestUpdateRecord::initTestCase() {
	QVERIFY(m_temp.isValid());
}

void TestUpdateRecord::parsesRolledBackRecord() {
	const std::optional<UpdateRecord> record = parseUpdateRecord(kValidRecord);
	// A valid record gives its version.
	QVERIFY(record.has_value());
	QVERIFY(record->version == kRecordVersion);
	// A valid record gives its time.
	QCOMPARE(record->time, recordTime());
	// The time is UTC.
	QCOMPARE(record->time.timeSpec(), Qt::UTC);
	// A rolled-back record gives that outcome.
	QCOMPARE(record->outcome, UpdateOutcome::RolledBack);
}

void TestUpdateRecord::parsesNotAppliedRecord() {
	const std::optional<UpdateRecord> notApplied = parseUpdateRecord(kNotAppliedRecord);
	// A not-applied record gives that outcome, its version and its time.
	QVERIFY(notApplied.has_value());
	QCOMPARE(notApplied->outcome, UpdateOutcome::NotApplied);
	QVERIFY(notApplied->version == kRecordVersion);
	QCOMPARE(notApplied->time, recordTime());
}

void TestUpdateRecord::parsesAppliedRecord() {
	const std::optional<UpdateRecord> applied = parseUpdateRecord(kAppliedRecord);
	// An applied record gives that outcome, its version and its time.
	QVERIFY(applied.has_value());
	QCOMPARE(applied->outcome, UpdateOutcome::Applied);
	QVERIFY(applied->version == kRecordVersion);
	QCOMPARE(applied->time, recordTime());
}

void TestUpdateRecord::readsRecordInAnyLayout() {
	const std::optional<UpdateRecord> crlf =
		parseUpdateRecord("outcome=rolled-back\r\nversion=9.8.7\r\ntime=2026-10-09T12:00:00Z\r\n");
	// A record with CRLF line ends is read.
	QVERIFY(crlf.has_value());
	QVERIFY(crlf->version == kRecordVersion);
	QCOMPARE(crlf->time, recordTime());
	const std::optional<UpdateRecord> reordered =
		parseUpdateRecord("time=2026-10-09T12:00:00Z\nversion=9.8.7\noutcome=rolled-back");
	// The keys may come in any order and the last line needs no line feed.
	QVERIFY(reordered.has_value());
	QVERIFY(reordered->version == kRecordVersion);
	const std::optional<UpdateRecord> extra = parseUpdateRecord(kValidRecord + "note=later\n");
	// An unknown key is ignored.
	QVERIFY(extra.has_value());
	QVERIFY(extra->version == kRecordVersion);
	const std::optional<UpdateRecord> repeated = parseUpdateRecord(kValidRecord + "version=1.0.0\n");
	// The first occurrence of a key counts.
	QVERIFY(repeated.has_value());
	QVERIFY(repeated->version == kRecordVersion);
}

void TestUpdateRecord::rejectsRecord_data() {
	QTest::addColumn<QByteArray>("contents");
	QTest::newRow("an empty record") << QByteArray("");
	QTest::newRow("a record without an outcome") << QByteArray("version=9.8.7\ntime=2026-10-09T12:00:00Z\n");
	QTest::newRow("a record without a version") << QByteArray("outcome=rolled-back\ntime=2026-10-09T12:00:00Z\n");
	QTest::newRow("a record without a time") << QByteArray("outcome=rolled-back\nversion=9.8.7\n");
	QTest::newRow("another outcome") << QByteArray("outcome=unknown\nversion=9.8.7\ntime=2026-10-09T12:00:00Z\n");
	QTest::newRow("a version of two parts") << QByteArray("outcome=rolled-back\nversion=1.2\ntime=2026-10-09T12:00:00Z\n");
	QTest::newRow("a version with a prefix") << QByteArray("outcome=rolled-back\nversion=v1.2.3\ntime=2026-10-09T12:00:00Z\n");
	QTest::newRow("a version with a leading zero")
		<< QByteArray("outcome=rolled-back\nversion=01.2.3\ntime=2026-10-09T12:00:00Z\n");
	QTest::newRow("a prerelease version")
		<< QByteArray("outcome=rolled-back\nversion=1.2.3-beta\ntime=2026-10-09T12:00:00Z\n");
	QTest::newRow("a time without the Z") << QByteArray("outcome=rolled-back\nversion=9.8.7\ntime=2026-10-09T12:00:00\n");
	QTest::newRow("a time that is not a date")
		<< QByteArray("outcome=rolled-back\nversion=9.8.7\ntime=2026-13-45T25:61:61Z\n");
}

void TestUpdateRecord::rejectsRecord() {
	QFETCH(QByteArray, contents);
	QVERIFY(!parseUpdateRecord(contents));
}

void TestUpdateRecord::givesNoRecordWithoutFile() {
	QVERIFY2(!takeUpdateRecord(path("missing-record.txt"), kInstalled), "no file gives no record");
}

void TestUpdateRecord::takesRecordOfNewerVersion() {
	const QString record = path("newer-record.txt");
	QVERIFY2(writeFile(record, kValidRecord), "record fixture is writable");
	const std::optional<UpdateRecord> taken = takeUpdateRecord(record, kInstalled);
	// A record for a newer version is returned.
	QVERIFY(taken.has_value());
	QVERIFY(taken->version == kRecordVersion);
	QVERIFY2(!QFileInfo::exists(record), "a returned record is removed");
}

void TestUpdateRecord::takesNotAppliedRecord() {
	const QString record = path("not-applied-record.txt");
	QVERIFY2(writeFile(record, kNotAppliedRecord), "not-applied fixture is writable");
	const std::optional<UpdateRecord> notApplied = takeUpdateRecord(record, kInstalled);
	// A not-applied record for a newer version is returned.
	QVERIFY(notApplied.has_value());
	QCOMPARE(notApplied->outcome, UpdateOutcome::NotApplied);
	QVERIFY2(!QFileInfo::exists(record), "a returned not-applied record is removed");
}

void TestUpdateRecord::dropsRecordOfInstalledVersion() {
	const QString record = path("installed-record.txt");
	QVERIFY2(writeFile(record, kValidRecord), "record fixture is writable again");
	QVERIFY2(!takeUpdateRecord(record, SemanticVersion{9, 8, 7}), "a record for the installed version is stale");
	QVERIFY2(!QFileInfo::exists(record), "a record for the installed version is removed");
}

void TestUpdateRecord::dropsRecordOfOlderVersion() {
	const QString record = path("older-record.txt");
	QVERIFY2(writeFile(record, kValidRecord), "record fixture is writable once more");
	QVERIFY2(!takeUpdateRecord(record, SemanticVersion{9, 9, 0}), "a record for an older version is stale");
	QVERIFY2(!QFileInfo::exists(record), "a record for an older version is removed");
}

// An applied record is reported by the version that it names, and only by that version.
void TestUpdateRecord::takesAppliedRecordOnlyForItsVersion() {
	const QString record = path("applied-record.txt");
	QVERIFY2(writeFile(record, kAppliedRecord), "applied fixture is writable");
	const std::optional<UpdateRecord> applied = takeUpdateRecord(record, SemanticVersion{9, 8, 7});
	// An applied record for the installed version is returned.
	QVERIFY(applied.has_value());
	QCOMPARE(applied->outcome, UpdateOutcome::Applied);
	QVERIFY(applied->version == kRecordVersion);
	QVERIFY2(!QFileInfo::exists(record), "a returned applied record is removed");
	for (const SemanticVersion& other : {kInstalled, SemanticVersion{9, 9, 0}}) {
		QVERIFY2(writeFile(record, kAppliedRecord), "applied fixture is writable again");
		QVERIFY2(!takeUpdateRecord(record, other), "an applied record for another version gives no notice");
		QVERIFY2(!QFileInfo::exists(record), "an applied record for another version is removed");
	}
}

void TestUpdateRecord::dropsMalformedRecord() {
	const QString record = path("malformed-record.txt");
	QVERIFY2(writeFile(record, "outcome=rolled-back\nversion=9.8.7\n"), "malformed fixture is writable");
	QVERIFY2(!takeUpdateRecord(record, kInstalled), "a malformed record gives no record");
	QVERIFY2(!QFileInfo::exists(record), "a malformed record is removed");
}

void TestUpdateRecord::readsRecordOf4096Bytes() {
	const QString record = path("record-4096.txt");
	QVERIFY2(writeFile(record, recordOfSize(4096)), "fixture of 4096 bytes is writable");
	// The fixture has 4096 bytes.
	QCOMPARE(QFileInfo(record).size(), 4096);
	QVERIFY2(takeUpdateRecord(record, kInstalled).has_value(), "a record of 4096 bytes is read");
	QVERIFY2(!QFileInfo::exists(record), "a record of 4096 bytes is removed");
}

void TestUpdateRecord::refusesFileOfMoreThan4096Bytes() {
	const QString record = path("record-4097.txt");
	QVERIFY2(writeFile(record, recordOfSize(4097)), "fixture of 4097 bytes is writable");
	QVERIFY2(!takeUpdateRecord(record, kInstalled), "a file of more than 4096 bytes is not a record");
	QVERIFY2(!QFileInfo::exists(record), "a file of more than 4096 bytes is removed");
}

void TestUpdateRecord::leavesFolderAtRecordPath() {
	const QString folder = path("folder");
	QVERIFY2(QDir().mkpath(folder), "folder fixture is writable");
	QVERIFY2(!takeUpdateRecord(folder, kInstalled), "a folder at the record path gives no record");
	QVERIFY2(QFileInfo(folder).isDir(), "a folder at the record path is left alone");
}

void TestUpdateRecord::parsesSucceededInstallerRecord() {
	const std::optional<InstallerRecord> succeeded = parseInstallerRecord(kInstallerSucceeded);
	// A record of an installer that finished gives its version and no exit code.
	QVERIFY(succeeded.has_value());
	QVERIFY(succeeded->succeeded);
	QVERIFY(succeeded->version == kRecordVersion);
	QVERIFY(!succeeded->exitCode);
}

void TestUpdateRecord::parsesFailedInstallerRecord() {
	const std::optional<InstallerRecord> failed = parseInstallerRecord(kInstallerFailed);
	// A record of a failed installer gives its version and exit code.
	QVERIFY(failed.has_value());
	QVERIFY(!failed->succeeded);
	QVERIFY(failed->version == kRecordVersion);
	QVERIFY(failed->exitCode == 5);
}

void TestUpdateRecord::readsInstallerExitCode() {
	const std::optional<InstallerRecord> notStarted =
		parseInstallerRecord("outcome=installer-failed\nversion=9.8.7\ntime=2026-10-09T12:00:00Z\n");
	// A failed installer without an exit code gives none.
	QVERIFY(notStarted.has_value());
	QVERIFY(!notStarted->succeeded);
	QVERIFY(!notStarted->exitCode);
	const std::optional<InstallerRecord> large =
		parseInstallerRecord("outcome=installer-failed\r\nversion=9.8.7\r\nexit-code=3221225477\r\n");
	// An exit code above the range of int is read, with CRLF line ends.
	QVERIFY(large.has_value());
	QVERIFY(large->exitCode == 3221225477ULL);
	const std::optional<InstallerRecord> unreadable =
		parseInstallerRecord("outcome=installer-failed\nversion=9.8.7\nexit-code=abc\n");
	// An exit code that is no number is ignored.
	QVERIFY(unreadable.has_value());
	QVERIFY(!unreadable->exitCode);
}

void TestUpdateRecord::rejectsInstallerRecord_data() {
	QTest::addColumn<QByteArray>("contents");
	QTest::newRow("an empty installer record") << QByteArray("");
	QTest::newRow("a rolled-back record") << kValidRecord;
	QTest::newRow("a not-applied record") << kNotAppliedRecord;
	QTest::newRow("an applied record") << kAppliedRecord;
	QTest::newRow("another outcome") << QByteArray("outcome=installer-unknown\nversion=9.8.7\n");
	QTest::newRow("an installer record without a version")
		<< QByteArray("outcome=installer-succeeded\ntime=2026-10-09T12:00:00Z\n");
	QTest::newRow("an installer record with a version of two parts") << QByteArray("outcome=installer-succeeded\nversion=1.2\n");
	QTest::newRow("an installer record with a prerelease version")
		<< QByteArray("outcome=installer-failed\nversion=1.2.3-beta\n");
}

void TestUpdateRecord::rejectsInstallerRecord() {
	QFETCH(QByteArray, contents);
	QVERIFY(!parseInstallerRecord(contents));
}

void TestUpdateRecord::updateReaderRefusesInstallerRecord() {
	// The reader of a rolled-back update does not take an installer record.
	QVERIFY(!parseUpdateRecord(kInstallerSucceeded));
	QVERIFY(!parseUpdateRecord(kInstallerFailed));
}

void TestUpdateRecord::checksInstallerRecord() {
	const InstallerRecord succeeded{SemanticVersion{9, 8, 7}, true, std::nullopt};
	const InstallerRecord failed{SemanticVersion{9, 8, 7}, false, 5};
	// The version that the installer was to install runs.
	QCOMPARE(checkInstallerRecord(succeeded, SemanticVersion{9, 8, 7}), InstallerCheck::Updated);
	// An older version runs although the installer finished.
	QCOMPARE(checkInstallerRecord(succeeded, SemanticVersion{9, 8, 6}), InstallerCheck::NotUpdated);
	// A newer version runs than the record names.
	QCOMPARE(checkInstallerRecord(succeeded, SemanticVersion{9, 9, 0}), InstallerCheck::Stale);
	// An older version runs after a failed installer.
	QCOMPARE(checkInstallerRecord(failed, SemanticVersion{9, 8, 6}), InstallerCheck::Failed);
	// A failed installer is reported although the expected version runs.
	QCOMPARE(checkInstallerRecord(failed, SemanticVersion{9, 8, 7}), InstallerCheck::Failed);
	// A failed installer of an older version says nothing.
	QCOMPARE(checkInstallerRecord(failed, SemanticVersion{10, 0, 0}), InstallerCheck::Stale);
}

void TestUpdateRecord::givesNoInstallerRecordWithoutFile() {
	QVERIFY2(!takeInstallerRecord(path("missing-installer-record.txt")), "no file gives no installer record");
}

void TestUpdateRecord::takesInstallerRecord() {
	const QString record = path("installer-record.txt");
	QVERIFY2(writeFile(record, kInstallerSucceeded), "installer record fixture is writable");
	const std::optional<InstallerRecord> taken = takeInstallerRecord(record);
	// An installer record is returned.
	QVERIFY(taken.has_value());
	QVERIFY(taken->succeeded);
	QVERIFY(taken->version == kRecordVersion);
	QVERIFY2(!QFileInfo::exists(record), "a returned installer record is removed");
}

// The record of a rolled-back update stays for takeUpdateRecord, which is called after this reader.
void TestUpdateRecord::leavesUpdateRecordsForTheirReader() {
	const QString record = path("rolled-back-installer-record.txt");
	QVERIFY2(writeFile(record, kValidRecord), "rolled-back fixture is writable");
	QVERIFY2(!takeInstallerRecord(record), "a rolled-back record is no installer record");
	QVERIFY2(QFileInfo::exists(record), "a rolled-back record is left for its own reader");
	const std::optional<UpdateRecord> rolledBack = takeUpdateRecord(record, kInstalled);
	// The rolled-back reader still gets it.
	QVERIFY(rolledBack.has_value());
	QVERIFY(rolledBack->version == kRecordVersion);
	// An applied record is no installer record and is left for its own reader.
	QVERIFY(writeFile(record, kAppliedRecord));
	QVERIFY(!takeInstallerRecord(record));
	QVERIFY(QFileInfo::exists(record));
	QVERIFY2(takeUpdateRecord(record, SemanticVersion{9, 8, 7}).has_value(), "the reader of an update in place gets it");
}

// takeUpdateRecord removes any record it finds, so an installer record that it reads first is lost unseen.
void TestUpdateRecord::updateReaderRemovesInstallerRecord() {
	const QString record = path("installer-record-read-first.txt");
	QVERIFY2(writeFile(record, kInstallerSucceeded), "installer fixture is writable again");
	// The rolled-back reader removes an installer record without returning it.
	QVERIFY(!takeUpdateRecord(record, kInstalled));
	QVERIFY(!QFileInfo::exists(record));
}

void TestUpdateRecord::leavesMalformedInstallerRecord() {
	const QString record = path("malformed-installer-record.txt");
	QVERIFY2(writeFile(record, "outcome=installer-failed\n"), "malformed fixture is writable");
	// A malformed record is left for the other reader.
	QVERIFY(!takeInstallerRecord(record));
	QVERIFY(QFileInfo::exists(record));
	// Which removes it.
	QVERIFY(!takeUpdateRecord(record, kInstalled));
	QVERIFY(!QFileInfo::exists(record));
}

void TestUpdateRecord::refusesLongInstallerRecord() {
	const QString record = path("long-installer-record.txt");
	QByteArray tooLong = kInstallerSucceeded + "padding=";
	tooLong.append(QByteArray(4097 - tooLong.size(), 'x'));
	QVERIFY2(writeFile(record, tooLong), "fixture of 4097 bytes is writable");
	// A file of more than 4096 bytes is not an installer record.
	QVERIFY(!takeInstallerRecord(record));
	QVERIFY(QFileInfo::exists(record));
	QVERIFY2(QFile::remove(record), "the long fixture is removed");
}

void TestUpdateRecord::leavesFolderAtInstallerRecordPath() {
	const QString folder = path("installer-folder");
	QVERIFY2(QDir().mkpath(folder), "folder fixture is writable");
	// A folder at the record path gives no record.
	QVERIFY(!takeInstallerRecord(folder));
	QVERIFY(QFileInfo(folder).isDir());
}

void TestUpdateRecord::describesOutcomes() {
	const QDateTime time = recordTime();
	const UpdateRecordText rolledBack =
		describeUpdateRecord(UpdateRecord{SemanticVersion{9, 8, 7}, time, UpdateOutcome::RolledBack});
	const UpdateRecordText notApplied =
		describeUpdateRecord(UpdateRecord{SemanticVersion{9, 8, 7}, time, UpdateOutcome::NotApplied});
	const UpdateRecordText applied =
		describeUpdateRecord(UpdateRecord{SemanticVersion{9, 8, 7}, time, UpdateOutcome::Applied});
	// A rolled-back update says that it did not start and that the previous version was restored.
	QVERIFY(rolledBack.text.contains("9.8.7"));
	QVERIFY(rolledBack.text.contains("could not be started"));
	QVERIFY(rolledBack.text.contains("restored"));
	// A not-applied update says that it failed and that the previous version is still installed.
	QVERIFY(notApplied.text.contains("9.8.7"));
	QVERIFY(notApplied.text.contains("could not be applied"));
	QVERIFY(notApplied.text.contains("previous version is still installed"));
	// The advice differs for the two outcomes.
	QVERIFY(!notApplied.advice.isEmpty());
	QVERIFY(notApplied.advice != rolledBack.advice);
	// An applied update says which version EGTRAIN was updated to, as after an installer update.
	QCOMPARE(applied.text, QStringLiteral("EGTRAIN was updated to version 9.8.7."));
	QVERIFY(applied.advice.isEmpty());
}

void TestUpdateRecord::namesRecordAndHelperOptions() {
	const QString recordPath = updateRecordPath();
	QVERIFY2(recordPath.isEmpty() || recordPath.endsWith(QStringLiteral("/update-record.txt")),
		"the record is named update-record.txt");
	// The helper options name the version and the path.
	QCOMPARE(updateRecordArguments(SemanticVersion{1, 2, 3}, QStringLiteral("/tmp/record.txt")),
		QStringList({"--version", "1.2.3", "--record", "/tmp/record.txt"}));
	QVERIFY2(updateRecordArguments(SemanticVersion{1, 2, 3}, QString()).isEmpty(),
		"no path gives no helper options");
}

QTEST_GUILESS_MAIN(TestUpdateRecord)
#include "test_updaterecord.moc"
