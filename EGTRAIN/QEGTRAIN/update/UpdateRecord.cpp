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

} // namespace

std::optional<UpdateRecord> parseUpdateRecord(const QByteArray& contents) {
	const QList<QByteArray> lines = contents.split('\n');
	const QByteArray outcome = firstValue(lines, "outcome");
	const QByteArray version = firstValue(lines, "version");
	const QByteArray time = firstValue(lines, "time");
	if (outcome != "rolled-back" || !time.endsWith('Z'))
		return std::nullopt;
	const std::optional<SemanticVersion> parsedVersion = parseStableVersion(version.toStdString());
	const QDateTime parsedTime = QDateTime::fromString(QString::fromLatin1(time), Qt::ISODate);
	if (!parsedVersion || !parsedTime.isValid())
		return std::nullopt;
	return UpdateRecord{*parsedVersion, parsedTime.toUTC()};
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
	QByteArray contents;
	bool complete = false;
	{
		QFile file(path);
		if (file.open(QIODevice::ReadOnly)) {
			contents = file.read(kMaxRecordBytes);
			complete = file.atEnd();
		}
	}
	QFile::remove(path);
	if (!complete)
		return std::nullopt;
	const std::optional<UpdateRecord> record = parseUpdateRecord(contents);
	if (!record || !(installed < record->version))
		return std::nullopt;
	return record;
}

QStringList updateRecordArguments(const SemanticVersion& version, const QString& recordPath) {
	if (recordPath.isEmpty())
		return {};
	return {QStringLiteral("--version"), formatSemanticVersion(version), QStringLiteral("--record"), recordPath};
}
