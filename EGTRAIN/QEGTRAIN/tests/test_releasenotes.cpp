#include "app/ReleaseNotesText.h"
#include "update/ReleaseInfo.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextList>

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
	if (!condition) {
		std::cerr << "failed: " << message << '\n';
		std::exit(1);
	}
}

QString visible(const QTextDocument& document) {
	QString text = document.toPlainText();
	text.replace(QChar::LineSeparator, '\n').replace(QChar::ParagraphSeparator, '\n');
	return text;
}

// The notes that parseLatestStableRelease reads from a release whose body is the given text.
QString notesOfReleaseBody(const QString& body) {
	QJsonObject release;
	release.insert(QStringLiteral("tag_name"), QStringLiteral("v1.0.2"));
	release.insert(QStringLiteral("draft"), false);
	release.insert(QStringLiteral("prerelease"), false);
	release.insert(QStringLiteral("html_url"), QStringLiteral("https://github.com/Ancientkingg/EGTRAIN/releases/tag/v1.0.2"));
	release.insert(QStringLiteral("body"), body);
	const std::optional<StableRelease> parsed = parseLatestStableRelease(QJsonDocument(QJsonArray{release}).toJson());
	require(parsed.has_value(), "the release does not parse");
	return parsed->notes;
}

const QString kReleaseSentence = QStringLiteral("The full notes are on the release page.");
}

int main(int argc, char** argv) {
	QApplication app(argc, argv);

	// Back ticks, lists and line breaks show as formatted text. Only the line break, the code lines and the control
	// below depend on releaseNotesMarkdown; the rest is what Qt renders for Markdown.
	const QString notes = QStringLiteral(
		"## What changed\r\n"
		"First line\r\n"
		"Second line\r\n"
		"\r\n"
		"- Export `Data` starts in Documents\r\n"
		"- Second item\r\n"
		"\r\n"
		"1. One\r\n"
		"2. Two\r\n"
		"\r\n"
		"```\r\n"
		"code a\r\n"
		"code b\r\n"
		"```\r\n");
	QTextDocument document;
	document.setMarkdown(releaseNotesMarkdown(notes));
	const QString text = visible(document);
	require(!text.contains('`'), "back ticks are visible");
	require(!text.contains("##"), "heading marks are visible");
	require(!text.contains('\r'), "carriage return is visible");
	require(text.contains("First line\nSecond line"), "line break between two lines is lost");
	require(text.contains("code a\ncode b"), "code lines are joined");
	require(!text.contains("code a  "), "spaces were added to a code line");
	int listItems = 0;
	for (QTextBlock block = document.begin(); block.isValid(); block = block.next())
		if (block.textList())
			++listItems;
	require(listItems == 4, "lists are not formatted as lists");

	QTextDocument raw;
	raw.setMarkdown(notes);
	require(!visible(raw).contains("First line\nSecond line"), "the lines already break without preparation");

	// The informative text of a message box shows Markdown; setTextFormat of the box does not reach it.
	QMessageBox box;
	box.setInformativeText(QStringLiteral("- one\n- two"));
	const QLabel* label = box.findChild<QLabel*>(QStringLiteral("qt_msgbox_informativelabel"));
	require(label != nullptr, "the message box has no informative label");
	require(label->textFormat() != Qt::MarkdownText, "the control label already shows Markdown");
	setMarkdownInformativeText(box, QStringLiteral("- one\n- two"));
	require(label->textFormat() == Qt::MarkdownText, "the informative text is not shown as Markdown");

	// The body of release v1.0.2 as GitHub sends it, with CRLF line ends, goes through the parser and into the label of
	// the update dialog as separate paragraphs and a two item list.
	const QString body = QStringLiteral(
		"Automated EGTRAIN build for `EGTRAIN v1.0.2`.\r\n"
		"\r\n"
		"- Commit: 13cb88561425b64b79624efd6eadf8d609357158\r\n"
		"- Workflow run: https://github.com/Ancientkingg/EGTRAIN/actions/runs/36513873983\r\n"
		"\r\n"
		"Download the package for your platform. Case studies are also available as `.egscene` files.\r\n");
	QMessageBox dialog;
	setMarkdownInformativeText(dialog, releaseNotesMarkdown(notesOfReleaseBody(body)));
	const QLabel* notesLabel = dialog.findChild<QLabel*>(QStringLiteral("qt_msgbox_informativelabel"));
	require(notesLabel != nullptr && notesLabel->textFormat() == Qt::MarkdownText, "the notes label is not Markdown");
	QTextDocument rendered;
	rendered.setMarkdown(notesLabel->text());
	int items = 0;
	int paragraphs = 0;
	for (QTextBlock block = rendered.begin(); block.isValid(); block = block.next()) {
		if (block.textList())
			++items;
		else if (!block.text().isEmpty())
			++paragraphs;
	}
	require(items == 2, "the dialog notes do not show a list of two items");
	require(paragraphs == 2, "the dialog notes do not show two separate paragraphs");
	require(!visible(rendered).contains('`'), "back ticks of the dialog notes are visible");

	// Short notes are kept as they are, apart from line ends, trailing spaces and runs of blank lines.
	require(notesOfReleaseBody(QStringLiteral("One  \r\n\r\n\r\n\r\nTwo\rThree\t\n")) == QStringLiteral("One\n\nTwo\nThree"),
		"line structure of short notes changed");
	require(notesOfReleaseBody(QStringLiteral("  \r\n")).isEmpty(), "blank notes are not empty");

	// Long notes end at a line or a word and point to the release page; a code span or a link is not cut.
	const QString filler = QStringLiteral("Words go here and there. ").repeated(14).trimmed();
	const QString tail = QStringLiteral("\n\n") + kReleaseSentence;
	const auto keptOf = [&](const QString& cutNotes) {
		require(cutNotes.endsWith(tail), "cut notes do not point to the release page");
		return cutNotes.left(cutNotes.size() - tail.size());
	};
	const QString plainBody = filler + QStringLiteral("\n") + filler;
	const QString plain = keptOf(notesOfReleaseBody(plainBody));
	require(plain.size() <= 360 && plainBody.startsWith(plain) && plainBody.at(plain.size()).isSpace(), "notes are not cut at a word");
	const QString spanBody = filler.left(340) + QStringLiteral(" `a code span that is long enough` and more ") + filler;
	const QString spanned = keptOf(notesOfReleaseBody(spanBody));
	require(spanned == filler.left(340) && !spanned.contains('`'), "a code span is cut");
	const QString linkBody = filler.left(340) + QStringLiteral(" [link text here](https://example.com/x) ") + filler;
	require(keptOf(notesOfReleaseBody(linkBody)) == filler.left(340), "a link is cut");
	require(!notesOfReleaseBody(filler).contains(kReleaseSentence), "short notes mention the release page");
	return 0;
}
