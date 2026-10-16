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
// In installer mode the helper runs the installer of an installer-managed copy instead and leaves
// outcome=installer-succeeded (the installer exited with 0) or outcome=installer-failed, with the same
// version (the one to install) and time. A failed installer that ended with an exit code adds a fourth
// line, exit-code=N. Only the first record is an UpdateRecord: parseUpdateRecord does not accept the
// others, so they never produce the message about a rolled-back update. The others are an InstallerRecord.
struct UpdateRecord {
	SemanticVersion version;
	QDateTime time;
};

// The record of an installer-mode update.
struct InstallerRecord {
	// The version that the installer was to install.
	SemanticVersion version;
	bool succeeded = false;
	// The exit code that a failed installer ended with, when the record names one.
	std::optional<qulonglong> exitCode;
};

// What the version that runs after an installer-mode update says about its record.
enum class InstallerCheck {
	// The installer finished and the version it was to install runs.
	Updated,
	// The installer finished but an older version runs.
	NotUpdated,
	// The installer did not finish, whichever version runs.
	Failed,
	// A newer version than the record names runs, so the record says nothing.
	Stale
};

// Reads the text of a record. The lines end in LF or CRLF, the keys outcome, version and time must
// each occur (the first occurrence counts) and other keys are ignored. The outcome must be
// rolled-back, the version a stable version and the time a UTC time ending in Z.
std::optional<UpdateRecord> parseUpdateRecord(const QByteArray& contents);

// Reads the text of an installer record. The lines are read as in parseUpdateRecord, the outcome must be
// installer-succeeded or installer-failed and the version a stable version. The time is not read.
std::optional<InstallerRecord> parseInstallerRecord(const QByteArray& contents);

// Compares the installed version with the version that the record names.
InstallerCheck checkInstallerRecord(const InstallerRecord& record, const SemanticVersion& installed);

// Where the helper leaves the record: the local application data folder of this installation on
// this machine. Empty when Qt has no such folder.
QString updateRecordPath();

// Reads the record at path and removes the file, whether or not it could be read. Returns the record
// only when it is valid and its version is newer than the installed one; any other record is stale.
// Nothing is done unless path is a regular file, and a file of more than 4096 bytes is not a record.
// A file that cannot be removed does not stop the record from being returned.
std::optional<UpdateRecord> takeUpdateRecord(const QString& path, const SemanticVersion& installed);

// Reads the installer record at path and removes the file. A file that is no installer record is left
// as it is, for takeUpdateRecord, and so is anything that is no regular file or is longer than 4096 bytes.
std::optional<InstallerRecord> takeInstallerRecord(const QString& path);

// The options that make the helper write the record of an update to version. Empty without a path.
QStringList updateRecordArguments(const SemanticVersion& version, const QString& recordPath);

#endif
