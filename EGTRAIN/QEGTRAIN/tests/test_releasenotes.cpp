#include "app/ReleaseNotesText.h"

#include <QApplication>
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
	return 0;
}
