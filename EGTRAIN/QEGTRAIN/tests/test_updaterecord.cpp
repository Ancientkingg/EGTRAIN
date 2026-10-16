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
	ok &= testPathAndArguments();
	return ok ? 0 : 1;
}
