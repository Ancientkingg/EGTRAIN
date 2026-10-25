#include "app/ReleaseNotesText.h"
#include "update/ReleaseInfo.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextList>

#include <optional>

namespace {
QString visible(const QTextDocument& document) {
	QString text = document.toPlainText();
	text.replace(QChar::LineSeparator, '\n').replace(QChar::ParagraphSeparator, '\n');
	return text;
}

// The notes that parseLatestStableRelease reads from a release whose body is the given text, or nothing when the release
// does not parse.
std::optional<QString> notesOfReleaseBody(const QString& body) {
	QJsonObject release;
	release.insert(QStringLiteral("tag_name"), QStringLiteral("v1.0.2"));
	release.insert(QStringLiteral("draft"), false);
	release.insert(QStringLiteral("prerelease"), false);
	release.insert(QStringLiteral("html_url"), QStringLiteral("https://github.com/Ancientkingg/EGTRAIN/releases/tag/v1.0.2"));
	release.insert(QStringLiteral("body"), body);
	const std::optional<StableRelease> parsed = parseLatestStableRelease(QJsonDocument(QJsonArray{release}).toJson());
	if (!parsed)
		return std::nullopt;
	return parsed->notes;
}

const QString kReleaseSentence = QStringLiteral("The full notes are on the release page.");
// The end of notes that were cut.
const QString kTail = QStringLiteral("\n\n") + kReleaseSentence;

// Long notes end at a line or a word and point to the release page.
const QString kFiller = QStringLiteral("Words go here and there. ").repeated(14).trimmed();
}

class TestReleaseNotes : public QObject {
	Q_OBJECT

private slots:
	void formatsMarkdown();
	void breaksLinesOnlyWithPreparation();
	void showsInformativeTextAsMarkdown();
	void showsReleaseBodyInDialog();
	void keepsShortNotes();
	void cutsLongNotesAtWord();
	void keepsCodeSpanWhole();
	void keepsLinkWhole();
	void shortNotesDoNotPointToReleasePage();
};

// Back ticks, lists and line breaks show as formatted text. Only the line break, the code lines and the control
// below depend on releaseNotesMarkdown; the rest is what Qt renders for Markdown.
static const QString kNotes = QStringLiteral(
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

void TestReleaseNotes::formatsMarkdown() {
	QTextDocument document;
	document.setMarkdown(releaseNotesMarkdown(kNotes));
	const QString text = visible(document);
	QVERIFY2(!text.contains('`'), "back ticks are visible");
	QVERIFY2(!text.contains("##"), "heading marks are visible");
	QVERIFY2(!text.contains('\r'), "carriage return is visible");
	QVERIFY2(text.contains("First line\nSecond line"), "line break between two lines is lost");
	QVERIFY2(text.contains("code a\ncode b"), "code lines are joined");
	QVERIFY2(!text.contains("code a  "), "spaces were added to a code line");
	int listItems = 0;
	for (QTextBlock block = document.begin(); block.isValid(); block = block.next())
		if (block.textList())
			++listItems;
	// The lists are formatted as lists.
	QCOMPARE(listItems, 4);
}

void TestReleaseNotes::breaksLinesOnlyWithPreparation() {
	QTextDocument raw;
	raw.setMarkdown(kNotes);
	QVERIFY2(!visible(raw).contains("First line\nSecond line"), "the lines already break without preparation");
}

// The informative text of a message box shows Markdown; setTextFormat of the box does not reach it.
void TestReleaseNotes::showsInformativeTextAsMarkdown() {
	QMessageBox box;
	box.setInformativeText(QStringLiteral("- one\n- two"));
	const QLabel* label = box.findChild<QLabel*>(QStringLiteral("qt_msgbox_informativelabel"));
	if (label == nullptr)
		QFAIL("the message box has no informative label");
	// The control label does not show Markdown yet.
	QCOMPARE_NE(label->textFormat(), Qt::MarkdownText);
	setMarkdownInformativeText(box, QStringLiteral("- one\n- two"));
	// The informative text is shown as Markdown.
	QCOMPARE(label->textFormat(), Qt::MarkdownText);
}

// The body of release v1.0.2 as GitHub sends it, with CRLF line ends, goes through the parser and into the label of
// the update dialog as separate paragraphs and a two item list.
void TestReleaseNotes::showsReleaseBodyInDialog() {
	const QString body = QStringLiteral(
		"Automated EGTRAIN build for `EGTRAIN v1.0.2`.\r\n"
		"\r\n"
		"- Commit: 13cb88561425b64b79624efd6eadf8d609357158\r\n"
		"- Workflow run: https://github.com/Ancientkingg/EGTRAIN/actions/runs/36513873983\r\n"
		"\r\n"
		"Download the package for your platform. Case studies are also available as `.egscene` files.\r\n");
	const std::optional<QString> notes = notesOfReleaseBody(body);
	QVERIFY2(notes.has_value(), "the release does not parse");
	QMessageBox dialog;
	setMarkdownInformativeText(dialog, releaseNotesMarkdown(*notes));
	const QLabel* notesLabel = dialog.findChild<QLabel*>(QStringLiteral("qt_msgbox_informativelabel"));
	// The notes label is Markdown.
	if (notesLabel == nullptr)
		QFAIL("the notes label is not Markdown");
	QCOMPARE(notesLabel->textFormat(), Qt::MarkdownText);
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
	// The dialog notes show a list of two items.
	QCOMPARE(items, 2);
	// The dialog notes show two separate paragraphs.
	QCOMPARE(paragraphs, 2);
	QVERIFY2(!visible(rendered).contains('`'), "back ticks of the dialog notes are visible");
}

// Short notes are kept as they are, apart from line ends, trailing spaces and runs of blank lines.
void TestReleaseNotes::keepsShortNotes() {
	const std::optional<QString> notes = notesOfReleaseBody(QStringLiteral("One  \r\n\r\n\r\n\r\nTwo\rThree\t\n"));
	QVERIFY2(notes.has_value(), "the release does not parse");
	// The line structure of short notes stays.
	QCOMPARE(*notes, QStringLiteral("One\n\nTwo\nThree"));
	const std::optional<QString> blank = notesOfReleaseBody(QStringLiteral("  \r\n"));
	QVERIFY2(blank.has_value(), "the release does not parse");
	QVERIFY2(blank->isEmpty(), "blank notes are not empty");
}

void TestReleaseNotes::cutsLongNotesAtWord() {
	const QString plainBody = kFiller + QStringLiteral("\n") + kFiller;
	const std::optional<QString> notes = notesOfReleaseBody(plainBody);
	QVERIFY2(notes.has_value(), "the release does not parse");
	QVERIFY2(notes->endsWith(kTail), "cut notes do not point to the release page");
	const QString plain = notes->left(notes->size() - kTail.size());
	// The notes are cut at a word.
	QCOMPARE_LE(plain.size(), 360);
	QVERIFY(plainBody.startsWith(plain));
	QVERIFY(plainBody.at(plain.size()).isSpace());
}

// A code span is not cut.
void TestReleaseNotes::keepsCodeSpanWhole() {
	const QString spanBody = kFiller.left(340) + QStringLiteral(" `a code span that is long enough` and more ") + kFiller;
	const std::optional<QString> notes = notesOfReleaseBody(spanBody);
	QVERIFY2(notes.has_value(), "the release does not parse");
	QVERIFY2(notes->endsWith(kTail), "cut notes do not point to the release page");
	const QString spanned = notes->left(notes->size() - kTail.size());
	// The code span is not cut.
	QCOMPARE(spanned, kFiller.left(340));
	QVERIFY(!spanned.contains('`'));
}

// A link is not cut.
void TestReleaseNotes::keepsLinkWhole() {
	const QString linkBody = kFiller.left(340) + QStringLiteral(" [link text here](https://example.com/x) ") + kFiller;
	const std::optional<QString> notes = notesOfReleaseBody(linkBody);
	QVERIFY2(notes.has_value(), "the release does not parse");
	QVERIFY2(notes->endsWith(kTail), "cut notes do not point to the release page");
	// The link is not cut.
	QCOMPARE(notes->left(notes->size() - kTail.size()), kFiller.left(340));
}

void TestReleaseNotes::shortNotesDoNotPointToReleasePage() {
	const std::optional<QString> notes = notesOfReleaseBody(kFiller);
	QVERIFY2(notes.has_value(), "the release does not parse");
	QVERIFY2(!notes->contains(kReleaseSentence), "short notes mention the release page");
}

QTEST_MAIN(TestReleaseNotes)
#include "test_releasenotes.moc"
