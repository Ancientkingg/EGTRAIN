#ifndef RELEASENOTESTEXT_H
#define RELEASENOTESTEXT_H

#include <QString>
#include <QStringList>

// Release notes as the Markdown that a message box shows. Markdown joins the lines of a paragraph, so a line that is
// followed by another line gets a hard break, except inside a fenced block. GitHub gives the notes with CRLF line ends.
inline QString releaseNotesMarkdown(QString notes) {
	notes.replace(QLatin1String("\r\n"), QLatin1String("\n"));
	notes.replace('\r', '\n');
	QStringList lines = notes.split('\n');
	bool fenced = false;
	for (int i = 0; i < lines.size(); ++i) {
		const QString trimmed = lines[i].trimmed();
		if (trimmed.startsWith(QLatin1String("```")) || trimmed.startsWith(QLatin1String("~~~"))) {
			fenced = !fenced;
			continue;
		}
		const bool followed = i + 1 < lines.size() && !lines[i + 1].trimmed().isEmpty();
		if (!fenced && followed && !trimmed.isEmpty() && !lines[i].endsWith(QLatin1String("  ")))
			lines[i] += QLatin1String("  ");
	}
	return lines.join('\n');
}

#endif
