#ifndef UPDATEPROMPTTEXT_H
#define UPDATEPROMPTTEXT_H

#include <QString>

// The main texts of the two update message boxes. macOS draws a message box without its window title, so the question of
// the first and the versions in the second are in the main text. The titles stay for Windows and Linux.
namespace update_prompt {

inline QString automaticChecksQuestion() {
	return QStringLiteral("Check for EGTRAIN updates automatically?");
}

// Below the question: what the check does and where to change the answer.
inline QString automaticChecksDetail() {
	return QStringLiteral("EGTRAIN can check GitHub Releases when the application starts and notify you when a newer "
						  "stable version is available. You can change this later from Help.");
}

inline QString availableText(const QString& offeredVersion, const QString& installedVersion) {
	return QStringLiteral("EGTRAIN %1 is available. You are using %2.").arg(offeredVersion, installedVersion);
}

}

#endif
