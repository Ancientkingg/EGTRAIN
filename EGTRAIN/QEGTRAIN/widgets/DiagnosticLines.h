#pragma once

#include "scene/SceneDiagnostic.h"
#include <QString>
#include <QStringList>

/// The messages of the diagnostics that one callout of a passenger panel lists: the errors, and the notes and warnings
/// that follow them with their severity in front.
struct DiagnosticLines {
	/// The most lines of one callout. The rest is counted, and the tooltip of the callout lists all of them.
	static constexpr int MaxLines = 5;

	QStringList errors;		 ///< The messages of the errors.
	QStringList notes;		 ///< The messages of the warnings and the infos, each with "Warning: " or "Note: " in front.
	bool hasWarning = false; ///< True when `notes` holds a warning.

	/// Adds a message to the list of its severity.
	void add(SceneSeverity severity, const QString& message) {
		if (severity == SceneSeverity::Error) {
			errors << message;
			return;
		}
		const bool warning = severity == SceneSeverity::Warning;
		notes << (warning ? QStringLiteral("Warning: ") : QStringLiteral("Note: ")) + message;
		hasWarning = hasWarning || warning;
	}

	/// True when there is neither an error nor a note.
	bool isEmpty() const { return errors.isEmpty() && notes.isEmpty(); }

	/// The first MaxLines of lines, one per line, and "and N more" when there are others.
	static QString capped(const QStringList& lines) {
		if (lines.size() <= MaxLines)
			return lines.join(QLatin1Char('\n'));
		return lines.mid(0, MaxLines).join(QLatin1Char('\n')) + QStringLiteral("\nand %1 more").arg(lines.size() - MaxLines);
	}
};
