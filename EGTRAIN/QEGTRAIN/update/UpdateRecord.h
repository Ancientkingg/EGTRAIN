#ifndef EGTRAIN_UPDATE_RECORD_H
#define EGTRAIN_UPDATE_RECORD_H

#include "util/Version.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <optional>

// The record that the update helper leaves when the new version fails to start and the previous
// installation is put back. The helper writes it in update/UpdateHelper.cpp, which holds the same
// format. A text file in updateRecordPath() of three lines, each ended by a line feed:
//   outcome=rolled-back
//   version=X.Y.Z
//   time=YYYY-MM-DDTHH:MM:SSZ
// The version is the one that failed to start and the time is the UTC time of the write.
struct UpdateRecord {
	SemanticVersion version;
	QDateTime time;
};

// Reads the text of a record. The lines end in LF or CRLF, the keys outcome, version and time must
// each occur (the first occurrence counts) and other keys are ignored. The outcome must be
// rolled-back, the version a stable version and the time a UTC time ending in Z.
std::optional<UpdateRecord> parseUpdateRecord(const QByteArray& contents);

// Where the helper leaves the record: the local application data folder of this installation on
// this machine. Empty when Qt has no such folder.
QString updateRecordPath();

// Reads the record at path and removes the file, whether or not it could be read. Returns the record
// only when it is valid and its version is newer than the installed one; any other record is stale.
// Nothing is done unless path is a regular file, and a file of more than 4096 bytes is not a record.
// A file that cannot be removed does not stop the record from being returned.
std::optional<UpdateRecord> takeUpdateRecord(const QString& path, const SemanticVersion& installed);

// The options that make the helper write the record of an update to version. Empty without a path.
QStringList updateRecordArguments(const SemanticVersion& version, const QString& recordPath);

#endif
