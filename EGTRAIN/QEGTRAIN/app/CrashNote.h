#pragma once

#include <QString>

/// Makes a fatal signal or an uncaught exception write a crash note before the process ends as it would without the
/// handler, so that the system still reports the crash. The note is a new file crash-<UTC start time>-<pid>.txt in
/// folder and names the version, the cause and, on macOS and Linux, the stack. Does nothing for an empty folder or one
/// that cannot be made. Call it once, after the application version is set.
void installCrashHandler(const QString& folder);

/// Marks the notes in folder that no start has reported yet as reported, by renaming crash-*.txt to
/// reported-crash-*.txt, and returns the new path of the newest, or an empty string when there is none. A note that
/// another instance took first is left out. Only the newest 10 reported notes are kept.
QString takeCrashNote(const QString& folder);
