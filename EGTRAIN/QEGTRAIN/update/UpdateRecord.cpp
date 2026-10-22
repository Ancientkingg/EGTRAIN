#include "update/UpdateRecord.h"
#include "update/ReleaseInfo.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace {

constexpr qint64 kMaxRecordBytes = 4096;

// The value of the first line "key=value" with this key, or an empty array when there is none.
QByteArray firstValue(const QList<QByteArray>& lines, const QByteArray& key) {
	const QByteArray prefix = key + '=';
	for (const QByteArray& line : lines) {
		if (!line.startsWith(prefix))
			continue;
		QByteArray value = line.mid(prefix.size());
		if (value.endsWith('\r'))
			value.chop(1);
		return value;
	}
	return {};
}

// The content of the file at path, or nothing when it cannot be read or is longer than a record can be.
std::optional<QByteArray> readRecordFile(const QString& path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return std::nullopt;
	QByteArray contents = file.read(kMaxRecordBytes);
	if (!file.atEnd())
		return std::nullopt;
	return contents;
}

} // namespace

std::optional<UpdateRecord> parseUpdateRecord(const QByteArray& contents) {
	const QList<QByteArray> lines = contents.split('\n');
	const QByteArray outcome = firstValue(lines, "outcome");
	const QByteArray version = firstValue(lines, "version");
	const QByteArray time = firstValue(lines, "time");
	if ((outcome != "applied" && outcome != "rolled-back" && outcome != "not-applied") || !time.endsWith('Z'))
		return std::nullopt;
	const std::optional<SemanticVersion> parsedVersion = parseStableVersion(version.toStdString());
	const QDateTime parsedTime = QDateTime::fromString(QString::fromLatin1(time), Qt::ISODate);
	if (!parsedVersion || !parsedTime.isValid())
		return std::nullopt;
	UpdateOutcome parsedOutcome = UpdateOutcome::RolledBack;
	if (outcome == "applied")
		parsedOutcome = UpdateOutcome::Applied;
	else if (outcome == "not-applied")
		parsedOutcome = UpdateOutcome::NotApplied;
	return UpdateRecord{*parsedVersion, parsedTime.toUTC(), parsedOutcome};
}

UpdateRecordText describeUpdateRecord(const UpdateRecord& record) {
	const QString version = formatSemanticVersion(record.version);
	if (record.outcome == UpdateOutcome::Applied)
		return {QStringLiteral("EGTRAIN was updated to version %1.").arg(version), QString()};
	if (record.outcome == UpdateOutcome::NotApplied)
		return {QStringLiteral("The update to EGTRAIN %1 could not be applied. The previous version is still installed.")
					.arg(version),
			QStringLiteral("Nothing was changed. Choose Check for Updates in the Help menu to try again.")};
	return {QStringLiteral("The update to EGTRAIN %1 could not be started, so the previous version was restored.")
				.arg(version),
		QStringLiteral("Automatic update checks will not offer %1 again. A newer version is offered as usual, "
					   "and Check for Updates in the Help menu still offers %1.")
			.arg(version)};
}

std::optional<InstallerRecord> parseInstallerRecord(const QByteArray& contents) {
	const QList<QByteArray> lines = contents.split('\n');
	const QByteArray outcome = firstValue(lines, "outcome");
	const std::optional<SemanticVersion> version = parseStableVersion(firstValue(lines, "version").toStdString());
	if ((outcome != "installer-succeeded" && outcome != "installer-failed") || !version)
		return std::nullopt;
	InstallerRecord record;
	record.version = *version;
	record.succeeded = outcome == "installer-succeeded";
	bool isNumber = false;
	const qulonglong exitCode = firstValue(lines, "exit-code").toULongLong(&isNumber);
	if (isNumber)
		record.exitCode = exitCode;
	return record;
}

InstallerCheck checkInstallerRecord(const InstallerRecord& record, const SemanticVersion& installed) {
	if (record.version < installed)
		return InstallerCheck::Stale;
	if (!record.succeeded)
		return InstallerCheck::Failed;
	return installed == record.version ? InstallerCheck::Updated : InstallerCheck::NotUpdated;
}

QString updateRecordPath() {
	const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
	if (directory.isEmpty())
		return {};
	return QDir(directory).filePath(QStringLiteral("update-record.txt"));
}

std::optional<UpdateRecord> takeUpdateRecord(const QString& path, const SemanticVersion& installed) {
	if (!QFileInfo(path).isFile())
		return std::nullopt;
	const std::optional<QByteArray> contents = readRecordFile(path);
	QFile::remove(path);
	if (!contents)
		return std::nullopt;
	std::optional<UpdateRecord> record = parseUpdateRecord(*contents);
	if (!record)
		return std::nullopt;
	const bool current =
		record->outcome == UpdateOutcome::Applied ? record->version == installed : installed < record->version;
	if (!current)
		return std::nullopt;
	return record;
}

std::optional<InstallerRecord> takeInstallerRecord(const QString& path) {
	if (!QFileInfo(path).isFile())
		return std::nullopt;
	const std::optional<QByteArray> contents = readRecordFile(path);
	const std::optional<InstallerRecord> record = contents ? parseInstallerRecord(*contents) : std::nullopt;
	if (record)
		QFile::remove(path);
	return record;
}

QStringList updateRecordArguments(const SemanticVersion& version, const QString& recordPath) {
	if (recordPath.isEmpty())
		return {};
	return {QStringLiteral("--version"), formatSemanticVersion(version), QStringLiteral("--record"), recordPath};
}
