#pragma once

#include <QString>
#include <QStringList>

/// The folder for crash notes, or an empty string when this run keeps none. QEGTRAIN_CRASH_DIR names a folder for any
/// run. Otherwise a scripted run, which has no user to read a notice, and a run on the offscreen or minimal platform keep
/// none, so that they never meet the notes of a user. A normal run keeps them in AppLocalDataLocation/Crashes. Call it
/// once the application object exists.
QString crashNoteFolder(bool scripted);

/// Makes a fatal signal or an uncaught exception write a crash note before the process ends as it would without the
/// handler, so that the system still reports the crash. The note is a new file crash-<UTC start time>-<pid>.txt in
/// folder and names the version, the cause and, on macOS and Linux, the stack. Does nothing for an empty folder or one
/// that cannot be made. Call it once, after the application version is set.
void installCrashHandler(const QString& folder);

/// Marks the notes in folder that no start has reported yet as reported, by renaming crash-*.txt to
/// reported-crash-*.txt, and returns their new paths, oldest first. A note that another instance took first is
/// left out. Only the newest 10 reported notes are kept.
QStringList takeCrashNotes(const QString& folder);
