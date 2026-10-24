#ifndef UPDATEPROMPTTEXT_H
#define UPDATEPROMPTTEXT_H

#include <QString>

/// The main texts of the two update message boxes. macOS draws a message box without its window title, so the question of
/// the first and the versions in the second are in the main text. The titles stay for Windows and Linux.
namespace update_prompt {

/// The question of the first message box, whether to check for updates automatically.
inline QString automaticChecksQuestion() {
	return QStringLiteral("Check for EGTRAIN updates automatically?");
}

/// Below the question: what the check does and where to change the answer.
inline QString automaticChecksDetail() {
	return QStringLiteral("EGTRAIN can check GitHub for a new version each time it starts. You can change this later in the "
						  "Help menu.");
}

/// The main text of the second message box: the version on offer and the installed version.
inline QString availableText(const QString& offeredVersion, const QString& installedVersion) {
	return QStringLiteral("EGTRAIN %1 is available. You are using %2.").arg(offeredVersion, installedVersion);
}

}

#endif
