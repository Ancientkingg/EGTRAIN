#include "update/UpdateRecord.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <iostream>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

static bool writeFile(const QString& path, const QByteArray& contents) {
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

static const QByteArray kValidRecord("outcome=rolled-back\nversion=9.8.7\ntime=2026-10-09T12:00:00Z\n");

// A valid record followed by an unknown key that makes the whole file the given size.
static QByteArray recordOfSize(int size) {
	QByteArray contents = kValidRecord + "padding=";
	contents.append(QByteArray(size - contents.size(), 'x'));
	return contents;
}

static bool testParse() {
	bool ok = true;
	const std::optional<UpdateRecord> record = parseUpdateRecord(kValidRecord);
	ok &= expect(record && record->version == SemanticVersion{9, 8, 7}, "a valid record gives its version");
	ok &= expect(record && record->time == QDateTime(QDate(2026, 10, 9), QTime(12, 0, 0), Qt::UTC),
		"a valid record gives its time");
	ok &= expect(record && record->time.timeSpec() == Qt::UTC, "the time is UTC");

	const std::optional<UpdateRecord> crlf =
		parseUpdateRecord("outcome=rolled-back\r\nversion=9.8.7\r\ntime=2026-10-09T12:00:00Z\r\n");
	ok &= expect(crlf && crlf->version == SemanticVersion{9, 8, 7}
			&& crlf->time == QDateTime(QDate(2026, 10, 9), QTime(12, 0, 0), Qt::UTC),
		"a record with CRLF line ends is read");
	const std::optional<UpdateRecord> reordered =
		parseUpdateRecord("time=2026-10-09T12:00:00Z\nversion=9.8.7\noutcome=rolled-back");
	ok &= expect(reordered && reordered->version == SemanticVersion{9, 8, 7},
		"the keys may come in any order and the last line needs no line feed");
	const std::optional<UpdateRecord> extra = parseUpdateRecord(kValidRecord + "note=later\n");
	ok &= expect(extra && extra->version == SemanticVersion{9, 8, 7}, "an unknown key is ignored");
	const std::optional<UpdateRecord> repeated = parseUpdateRecord(kValidRecord + "version=1.0.0\n");
	ok &= expect(repeated && repeated->version == SemanticVersion{9, 8, 7}, "the first occurrence of a key counts");

	const struct {
		const char* label;
		QByteArray contents;
	} rejected[] = {
		{"an empty record", ""},
		{"a record without an outcome", "version=9.8.7\ntime=2026-10-09T12:00:00Z\n"},
		{"a record without a version", "outcome=rolled-back\ntime=2026-10-09T12:00:00Z\n"},
		{"a record without a time", "outcome=rolled-back\nversion=9.8.7\n"},
		{"another outcome", "outcome=not-applied\nversion=9.8.7\ntime=2026-10-09T12:00:00Z\n"},
		{"a version of two parts", "outcome=rolled-back\nversion=1.2\ntime=2026-10-09T12:00:00Z\n"},
		{"a version with a prefix", "outcome=rolled-back\nversion=v1.2.3\ntime=2026-10-09T12:00:00Z\n"},
		{"a version with a leading zero", "outcome=rolled-back\nversion=01.2.3\ntime=2026-10-09T12:00:00Z\n"},
		{"a prerelease version", "outcome=rolled-back\nversion=1.2.3-beta\ntime=2026-10-09T12:00:00Z\n"},
		{"a time without the Z", "outcome=rolled-back\nversion=9.8.7\ntime=2026-10-09T12:00:00\n"},
		{"a time that is not a date", "outcome=rolled-back\nversion=9.8.7\ntime=2026-13-45T25:61:61Z\n"}};
	for (const auto& entry : rejected)
		ok &= expect(!parseUpdateRecord(entry.contents), entry.label);
	return ok;
}

static bool testTake(const QString& directory) {
	bool ok = true;
	const SemanticVersion installed{9, 8, 6};
	const QString path = QDir(directory).filePath("record.txt");

	ok &= expect(!takeUpdateRecord(path, installed), "no file gives no record");

	ok &= expect(writeFile(path, kValidRecord), "record fixture is writable");
	const std::optional<UpdateRecord> taken = takeUpdateRecord(path, installed);
	ok &= expect(taken && taken->version == SemanticVersion{9, 8, 7}, "a record for a newer version is returned");
	ok &= expect(!QFileInfo::exists(path), "a returned record is removed");

	ok &= expect(writeFile(path, kValidRecord), "record fixture is writable again");
	ok &= expect(!takeUpdateRecord(path, SemanticVersion{9, 8, 7}), "a record for the installed version is stale");
	ok &= expect(!QFileInfo::exists(path), "a record for the installed version is removed");

	ok &= expect(writeFile(path, kValidRecord), "record fixture is writable once more");
	ok &= expect(!takeUpdateRecord(path, SemanticVersion{9, 9, 0}), "a record for an older version is stale");
	ok &= expect(!QFileInfo::exists(path), "a record for an older version is removed");

	ok &= expect(writeFile(path, "outcome=rolled-back\nversion=9.8.7\n"), "malformed fixture is writable");
	ok &= expect(!takeUpdateRecord(path, installed), "a malformed record gives no record");
	ok &= expect(!QFileInfo::exists(path), "a malformed record is removed");

	ok &= expect(writeFile(path, recordOfSize(4096)), "fixture of 4096 bytes is writable");
	ok &= expect(QFileInfo(path).size() == 4096, "the fixture has 4096 bytes");
	ok &= expect(takeUpdateRecord(path, installed).has_value(), "a record of 4096 bytes is read");
	ok &= expect(!QFileInfo::exists(path), "a record of 4096 bytes is removed");

	ok &= expect(writeFile(path, recordOfSize(4097)), "fixture of 4097 bytes is writable");
	ok &= expect(!takeUpdateRecord(path, installed), "a file of more than 4096 bytes is not a record");
	ok &= expect(!QFileInfo::exists(path), "a file of more than 4096 bytes is removed");

	const QString folder = QDir(directory).filePath("folder");
	ok &= expect(QDir().mkpath(folder), "folder fixture is writable");
	ok &= expect(!takeUpdateRecord(folder, installed), "a folder at the record path gives no record");
	ok &= expect(QFileInfo(folder).isDir(), "a folder at the record path is left alone");
	return ok;
}

static const QByteArray kInstallerSucceeded("outcome=installer-succeeded\nversion=9.8.7\ntime=2026-10-09T12:00:00Z\n");
static const QByteArray kInstallerFailed("outcome=installer-failed\nversion=9.8.7\ntime=2026-10-09T12:00:00Z\nexit-code=5\n");

static bool testParseInstaller() {
	bool ok = true;
	const std::optional<InstallerRecord> succeeded = parseInstallerRecord(kInstallerSucceeded);
	ok &= expect(succeeded && succeeded->succeeded && succeeded->version == SemanticVersion{9, 8, 7}
			&& !succeeded->exitCode,
		"a record of an installer that finished gives its version and no exit code");
	const std::optional<InstallerRecord> failed = parseInstallerRecord(kInstallerFailed);
	ok &= expect(failed && !failed->succeeded && failed->version == SemanticVersion{9, 8, 7}
			&& failed->exitCode == 5,
		"a record of a failed installer gives its version and exit code");
	const std::optional<InstallerRecord> notStarted =
		parseInstallerRecord("outcome=installer-failed\nversion=9.8.7\ntime=2026-10-09T12:00:00Z\n");
	ok &= expect(notStarted && !notStarted->succeeded && !notStarted->exitCode,
		"a failed installer without an exit code gives none");
	const std::optional<InstallerRecord> large =
		parseInstallerRecord("outcome=installer-failed\r\nversion=9.8.7\r\nexit-code=3221225477\r\n");
	ok &= expect(large && large->exitCode == 3221225477ULL, "an exit code above the range of int is read, with CRLF line ends");
	const std::optional<InstallerRecord> unreadable =
		parseInstallerRecord("outcome=installer-failed\nversion=9.8.7\nexit-code=abc\n");
	ok &= expect(unreadable && !unreadable->exitCode, "an exit code that is no number is ignored");

	const struct {
		const char* label;
		QByteArray contents;
	} rejected[] = {
		{"an empty installer record", ""},
		{"a rolled-back record", kValidRecord},
		{"another outcome", "outcome=installer-unknown\nversion=9.8.7\n"},
		{"an installer record without a version", "outcome=installer-succeeded\ntime=2026-10-09T12:00:00Z\n"},
		{"an installer record with a version of two parts", "outcome=installer-succeeded\nversion=1.2\n"},
		{"an installer record with a prerelease version", "outcome=installer-failed\nversion=1.2.3-beta\n"}};
	for (const auto& entry : rejected)
		ok &= expect(!parseInstallerRecord(entry.contents), entry.label);
	ok &= expect(!parseUpdateRecord(kInstallerSucceeded) && !parseUpdateRecord(kInstallerFailed),
		"the reader of a rolled-back update does not take an installer record");
	return ok;
}

static bool testCheckInstaller() {
	bool ok = true;
	const InstallerRecord succeeded{SemanticVersion{9, 8, 7}, true, std::nullopt};
	const InstallerRecord failed{SemanticVersion{9, 8, 7}, false, 5};
	ok &= expect(checkInstallerRecord(succeeded, SemanticVersion{9, 8, 7}) == InstallerCheck::Updated,
		"the version that the installer was to install runs");
	ok &= expect(checkInstallerRecord(succeeded, SemanticVersion{9, 8, 6}) == InstallerCheck::NotUpdated,
		"an older version runs although the installer finished");
	ok &= expect(checkInstallerRecord(succeeded, SemanticVersion{9, 9, 0}) == InstallerCheck::Stale,
		"a newer version runs than the record names");
	ok &= expect(checkInstallerRecord(failed, SemanticVersion{9, 8, 6}) == InstallerCheck::Failed,
		"an older version runs after a failed installer");
	ok &= expect(checkInstallerRecord(failed, SemanticVersion{9, 8, 7}) == InstallerCheck::Failed,
		"a failed installer is reported although the expected version runs");
	ok &= expect(checkInstallerRecord(failed, SemanticVersion{10, 0, 0}) == InstallerCheck::Stale,
		"a failed installer of an older version says nothing");
	return ok;
}

static bool testTakeInstaller(const QString& directory) {
	bool ok = true;
	const QString path = QDir(directory).filePath("installer-record.txt");

	ok &= expect(!takeInstallerRecord(path), "no file gives no installer record");

	ok &= expect(writeFile(path, kInstallerSucceeded), "installer record fixture is writable");
	const std::optional<InstallerRecord> taken = takeInstallerRecord(path);
	ok &= expect(taken && taken->succeeded && taken->version == SemanticVersion{9, 8, 7}, "an installer record is returned");
	ok &= expect(!QFileInfo::exists(path), "a returned installer record is removed");

	// The record of a rolled-back update stays for takeUpdateRecord, which is called after this reader.
	ok &= expect(writeFile(path, kValidRecord), "rolled-back fixture is writable");
	ok &= expect(!takeInstallerRecord(path), "a rolled-back record is no installer record");
	ok &= expect(QFileInfo::exists(path), "a rolled-back record is left for its own reader");
	const std::optional<UpdateRecord> rolledBack = takeUpdateRecord(path, SemanticVersion{9, 8, 6});
	ok &= expect(rolledBack && rolledBack->version == SemanticVersion{9, 8, 7}, "the rolled-back reader still gets it");

	// takeUpdateRecord removes any record it finds, so an installer record that it reads first is lost unseen.
	ok &= expect(writeFile(path, kInstallerSucceeded), "installer fixture is writable again");
	ok &= expect(!takeUpdateRecord(path, SemanticVersion{9, 8, 6}) && !QFileInfo::exists(path),
		"the rolled-back reader removes an installer record without returning it");

	ok &= expect(writeFile(path, "outcome=installer-failed\n"), "malformed fixture is writable");
	ok &= expect(!takeInstallerRecord(path) && QFileInfo::exists(path), "a malformed record is left for the other reader");
	ok &= expect(!takeUpdateRecord(path, SemanticVersion{9, 8, 6}) && !QFileInfo::exists(path),
		"which removes it");

	QByteArray tooLong = kInstallerSucceeded + "padding=";
	tooLong.append(QByteArray(4097 - tooLong.size(), 'x'));
	ok &= expect(writeFile(path, tooLong), "fixture of 4097 bytes is writable");
	ok &= expect(!takeInstallerRecord(path) && QFileInfo::exists(path), "a file of more than 4096 bytes is not an installer record");
	ok &= expect(QFile::remove(path), "the long fixture is removed");

	const QString folder = QDir(directory).filePath("installer-folder");
	ok &= expect(QDir().mkpath(folder), "folder fixture is writable");
	ok &= expect(!takeInstallerRecord(folder) && QFileInfo(folder).isDir(), "a folder at the record path gives no record");
	return ok;
}

static bool testPathAndArguments() {
	bool ok = true;
	const QString path = updateRecordPath();
	ok &= expect(path.isEmpty() || path.endsWith(QStringLiteral("/update-record.txt")),
		"the record is named update-record.txt");
	ok &= expect(updateRecordArguments(SemanticVersion{1, 2, 3}, QStringLiteral("/tmp/record.txt"))
			== QStringList({"--version", "1.2.3", "--record", "/tmp/record.txt"}),
		"the helper options name the version and the path");
	ok &= expect(updateRecordArguments(SemanticVersion{1, 2, 3}, QString()).isEmpty(),
		"no path gives no helper options");
	return ok;
}

int main(int argc, char** argv) {
	QCoreApplication application(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid())
		return 1;
	// Nothing here reads or writes updateRecordPath(): the record folder of a real installation is not touched.
	bool ok = true;
	ok &= testParse();
	ok &= testTake(temp.path());
	ok &= testParseInstaller();
	ok &= testCheckInstaller();
	ok &= testTakeInstaller(temp.path());
	ok &= testPathAndArguments();
	return ok ? 0 : 1;
}
