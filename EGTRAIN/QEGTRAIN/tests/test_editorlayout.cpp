#include "widgets/DialogLayout.h"
#include "widgets/EditorLayout.h"

#include <QApplication>
#include <QCheckBox>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QRect>
#include <QScrollArea>
#include <QScrollBar>
#include <QTest>
#include <QVBoxLayout>

#include <initializer_list>

namespace {
constexpr int kPanelWidth = 320;
constexpr int kScrollWidth = 300;
const QString kLongWord(400, QLatin1Char('W'));
const QString kSentence = QStringLiteral("word ").repeated(60);

// A plain widget, not a dialog, as an editor panel is: the labels go into a vertical layout that ends
// in a stretch, so that each label keeps the height of its own text.
void arrange(QWidget& panel, std::initializer_list<QLabel*> labels) {
	auto* layout = new QVBoxLayout(&panel);
	for (QLabel* label : labels)
		layout->addWidget(label);
	layout->addStretch();
}

// The panel is tall enough for the wrapped text with any font.
void showPanel(QWidget& panel) {
	panel.resize(kPanelWidth, 1000);
	panel.show();
	QApplication::processEvents();
}

// A scroll area as the editor docks have it, narrower than the panel width of the other tests.
void showInScrollArea(QScrollArea& scroll, QWidget* panel) {
	scroll.setWidgetResizable(true);
	scroll.setWidget(panel);
	scroll.resize(kScrollWidth, 400);
	scroll.show();
	QApplication::processEvents();
}

QLabel* plainWrappedLabel(const QString& text, QWidget* parent) {
	auto* label = new QLabel(text, parent);
	label->setWordWrap(true);
	return label;
}

// The width that the text of the label needs when a line may break at a space or at a zero-width space only.
int wrappedTextWidth(const QLabel* label, int width) {
	return label->fontMetrics().boundingRect(QRect(0, 0, width, 100000), Qt::TextWordWrap, label->text()).width();
}
} // namespace

class TestEditorLayout : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void wrappedLabelHasPlainWrappedText();
	void wrappedLabelDoesNotWidenPanel();
	void wrappedLabelGivesNoHorizontalScrollBar();
	void wrappedLabelWrapsText();
	void emptyStateLabelShowsOnlyMessage();
	void sectionHeadingIsLargerAndBold();
	void breakableNameBreaksAfterUnderscores();
	void verticalScrollAreaFitsPanelWidth();
};

void TestEditorLayout::initTestCase() {
	QFile qss(QStringLiteral(EGTRAIN_DIALOG_QSS));
	QVERIFY2(qss.open(QIODevice::ReadOnly), "cannot load application QSS");
	// The test binary has no Qt resources, so point the stylesheet at the icon files.
	QString styleSheet = QString::fromUtf8(qss.readAll());
	styleSheet.replace(QStringLiteral(":/icons/"),
		QFileInfo(qss).absolutePath() + QStringLiteral("/../icons/"));
	qApp->setStyleSheet(styleSheet);
}

void TestEditorLayout::wrappedLabelHasPlainWrappedText() {
	QWidget panel;
	const QString markup = QStringLiteral("<b>x</b> & <i>y</i>");
	QLabel* label = EditorLayout::wrappedLabel(markup, &panel);
	QLabel* empty = EditorLayout::wrappedLabel(QString(), &panel);
	// A layout reparents its widgets, so the parent is read before the labels go into one.
	// wrappedLabel uses the parent it was given.
	QCOMPARE(label->parentWidget(), &panel);
	arrange(panel, {label, empty});
	// wrappedLabel shows the text it was given.
	QCOMPARE(label->text(), markup);
	QVERIFY2(!label->property("dialogHelp").isValid(), "wrappedLabel has a help or status role");
	QVERIFY2(!label->property("dialogStatus").isValid(), "wrappedLabel has a help or status role");
	// wrappedLabel shows the text as plain text.
	QCOMPARE(label->textFormat(), Qt::PlainText);
	QVERIFY2(label->wordWrap(), "wrappedLabel does not wrap its text");
	// wrappedLabel makes layouts ignore its width hints.
	QCOMPARE(label->sizePolicy().horizontalPolicy(), QSizePolicy::Ignored);
	QVERIFY2(label->sizePolicy().hasHeightForWidth(), "wrappedLabel loses the height for width of its wrapped text");
	showPanel(panel);
	QVERIFY2(empty->isVisible(), "wrappedLabel with an empty text hides itself");
}

// A word wider than the panel gives a plain wrapped label, and so its panel, the width of that
// word. The control shows this, so that the checks below prove something.
void TestEditorLayout::wrappedLabelDoesNotWidenPanel() {
	QWidget controlPanel;
	QLabel* control = plainWrappedLabel(kLongWord, &controlPanel);
	arrange(controlPanel, {control});
	// The long word is wider than twice the panel, or the test proves nothing.
	QCOMPARE_GT(control->fontMetrics().horizontalAdvance(kLongWord), 2 * kPanelWidth);
	// Control: a plain wrapped label with a long word widens its panel, or the test proves nothing.
	QCOMPARE_GT(controlPanel.minimumSizeHint().width(), kPanelWidth);

	QWidget panel;
	QLabel* label = EditorLayout::wrappedLabel(kLongWord, &panel);
	arrange(panel, {label});
	// A wrappedLabel with a long word does not raise the minimum width of its panel.
	QCOMPARE_LE(panel.minimumSizeHint().width(), kPanelWidth);
	showPanel(panel);
	// A wrappedLabel with a long word does not widen its panel.
	QCOMPARE(panel.width(), kPanelWidth);
	// A wrappedLabel with a long word is not wider than its panel.
	QCOMPARE_LE(label->width(), kPanelWidth);
}

void TestEditorLayout::wrappedLabelGivesNoHorizontalScrollBar() {
	QScrollArea controlScroll;
	auto* controlPanel = new QWidget;
	arrange(*controlPanel, {plainWrappedLabel(kLongWord, controlPanel)});
	showInScrollArea(controlScroll, controlPanel);
	// Control: a plain wrapped label with a long word gives the scroll area a horizontal scroll bar, or the test proves
	// nothing.
	QCOMPARE_GT(controlScroll.horizontalScrollBar()->maximum(), 0);

	QScrollArea scroll;
	auto* panel = new QWidget;
	QLabel* label = EditorLayout::wrappedLabel(kLongWord, panel);
	arrange(*panel, {label});
	showInScrollArea(scroll, panel);
	// A wrappedLabel with a long word gives the scroll area no horizontal scroll bar.
	QCOMPARE(scroll.horizontalScrollBar()->maximum(), 0);
	// A wrappedLabel with a long word is not wider than the scroll area.
	QCOMPARE_LE(label->width(), scroll.viewport()->width());
}

void TestEditorLayout::wrappedLabelWrapsText() {
	QWidget panel;
	QLabel* label = EditorLayout::wrappedLabel(kSentence, &panel);
	arrange(panel, {label});
	// The sentence is wider than three panels, or the test proves nothing.
	QCOMPARE_GT(label->fontMetrics().horizontalAdvance(kSentence), 3 * kPanelWidth);
	showPanel(panel);
	// A wrappedLabel with a long sentence is not wider than its panel.
	QCOMPARE_LE(label->width(), kPanelWidth);
	// A wrappedLabel with a long sentence wraps.
	QCOMPARE_GE(label->height(), 3 * label->fontMetrics().lineSpacing());
	// A wrappedLabel with a long sentence is as tall as its wrapped text.
	QCOMPARE_GE(label->height(), label->heightForWidth(label->width()));

	// A panel makes its labels without a text and sets it on every refresh, also after the panel is shown.
	QWidget refreshedPanel;
	QLabel* refreshed = EditorLayout::wrappedLabel(QString(), &refreshedPanel);
	arrange(refreshedPanel, {refreshed});
	showPanel(refreshedPanel);
	refreshed->setText(kSentence);
	QApplication::processEvents();
	// A wrappedLabel that gets its text after the panel is shown is not wider than its panel.
	QCOMPARE_LE(refreshed->width(), kPanelWidth);
	// A wrappedLabel that gets its text after the panel is shown wraps.
	QCOMPARE_GE(refreshed->height(), 3 * refreshed->fontMetrics().lineSpacing());
	// A wrappedLabel that gets its text after the panel is shown is as tall as its wrapped text.
	QCOMPARE_GE(refreshed->height(), refreshed->heightForWidth(refreshed->width()));
}

void TestEditorLayout::emptyStateLabelShowsOnlyMessage() {
	QWidget panel;
	auto* reference = new QLabel(QStringLiteral("Plain reference"), &panel);
	QLabel* label = EditorLayout::emptyStateLabel(&panel);
	// emptyStateLabel uses the parent it was given.
	QCOMPARE(label->parentWidget(), &panel);
	arrange(panel, {reference, label});
	showPanel(panel);
	QVERIFY2(label->isHidden(), "emptyStateLabel is not hidden until it has a message");
	QVERIFY2(!label->isVisible(), "emptyStateLabel is not hidden until it has a message");
	QVERIFY2(label->property("dialogHelp").toBool(), "emptyStateLabel does not have the help role");
	QVERIFY2(!label->property("dialogStatus").isValid(), "emptyStateLabel has a status role");
	QVERIFY2(label->text().isEmpty(), "emptyStateLabel has a text before it has a message");

	const QString message = QStringLiteral("Select a unit or use Add.");
	DialogLayout::setHelp(label, message);
	QApplication::processEvents();
	QVERIFY2(label->isVisible(), "the message of emptyStateLabel is not shown after the panel is shown");
	// emptyStateLabel shows the message it was given.
	QCOMPARE(label->text(), message);
	// emptyStateLabel has the text colour of the help rule.
	QCOMPARE_NE(label->palette().color(QPalette::WindowText), reference->palette().color(QPalette::WindowText));

	DialogLayout::setHelp(label, QString());
	QApplication::processEvents();
	QVERIFY2(label->isHidden(), "emptyStateLabel is not hidden again by an empty message");
	QVERIFY2(!label->isVisible(), "emptyStateLabel is not hidden again by an empty message");
}

void TestEditorLayout::sectionHeadingIsLargerAndBold() {
	const QString markup = QStringLiteral("<b>x</b>");
	QWidget parent;
	QFont font = parent.font();
	font.setPointSizeF(12);
	parent.setFont(font);
	auto* reference = new QLabel(QStringLiteral("Reference"), &parent);
	QLabel* heading = EditorLayout::sectionHeading(markup, &parent);
	// sectionHeading shows the text it was given.
	QCOMPARE(heading->text(), markup);
	// sectionHeading shows the text as plain text.
	QCOMPARE(heading->textFormat(), Qt::PlainText);
	QVERIFY2(heading->font().bold(), "sectionHeading is not bold");
	QVERIFY2(!reference->font().bold(), "sectionHeading is not bold");
	// sectionHeading is larger than the font of its parent.
	QCOMPARE_GT(heading->font().pointSizeF(), reference->font().pointSizeF());
	QVERIFY2(heading->wordWrap(), "sectionHeading does not wrap its text");
	// sectionHeading makes layouts ignore its width hints.
	QCOMPARE(heading->sizePolicy().horizontalPolicy(), QSizePolicy::Ignored);
	// sectionHeading uses the parent it was given.
	QCOMPARE(heading->parentWidget(), &parent);

	// A font in pixels has no size in points.
	QWidget pixelParent;
	QFont pixelFont = pixelParent.font();
	pixelFont.setPixelSize(14);
	pixelParent.setFont(pixelFont);
	auto* pixelReference = new QLabel(QStringLiteral("Reference"), &pixelParent);
	QLabel* pixelHeading = EditorLayout::sectionHeading(QStringLiteral("Heading"), &pixelParent);
	// The font of the second parent is given in pixels, or the test proves nothing.
	QCOMPARE(pixelReference->font().pixelSize(), 14);
	// sectionHeading is larger than a font in pixels.
	QCOMPARE_GT(pixelHeading->font().pixelSize(), pixelReference->font().pixelSize());
	QVERIFY2(pixelHeading->font().bold(), "sectionHeading is not bold with a font in pixels");

	QWidget panel;
	QLabel* longHeading = EditorLayout::sectionHeading(kLongWord, &panel);
	arrange(panel, {longHeading});
	// A sectionHeading with a long word does not raise the minimum width of its panel.
	QCOMPARE_LE(panel.minimumSizeHint().width(), kPanelWidth);
}

void TestEditorLayout::breakableNameBreaksAfterUnderscores() {
	const QString name = QStringLiteral("Amsterdam_Hilversum_Student");
	const QString broken = EditorLayout::breakableName(name);
	// breakableName changes nothing but what it adds.
	QCOMPARE(QString(broken).remove(QChar(0x200B)), name);
	// breakableName adds a break after each underscore.
	QCOMPARE(broken.count(QChar(0x200B)), qsizetype(2));
	// breakableName leaves a name without an underscore as it is.
	QCOMPARE(EditorLayout::breakableName(QStringLiteral("Netherlands")), QStringLiteral("Netherlands"));

	// A panel a little wider than the longest part of the name and much narrower than the whole name.
	QWidget controlPanel;
	QLabel* control = EditorLayout::wrappedLabel(name, &controlPanel);
	const int width = control->fontMetrics().horizontalAdvance(QStringLiteral("Amsterdam_Hilversum_")) + 8;
	// The name is wider than the panel, or the test proves nothing.
	QCOMPARE_GT(control->fontMetrics().horizontalAdvance(name), width + 20);
	// Control: a name with underscores does not fit the panel without a break, or the test proves nothing.
	QCOMPARE_GT(wrappedTextWidth(control, width), width);

	QWidget panel;
	QLabel* label = EditorLayout::wrappedLabel(broken, &panel);
	auto* layout = new QVBoxLayout(&panel);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(label);
	layout->addStretch();
	panel.resize(width, 1000);
	panel.show();
	QApplication::processEvents();
	// The label has the width of the panel, or the test proves nothing.
	QCOMPARE(label->width(), width);
	// A wrapped name is not cut at the edge of its panel.
	QCOMPARE_LE(wrappedTextWidth(label, label->width()), label->width());
	// A wrapped name breaks between its parts.
	QCOMPARE_GE(label->height(), 2 * label->fontMetrics().lineSpacing());
}

// A scroll area for a panel of one check box with a text wider than the area, as high as the area allows.
// The check box is the widest thing that the panel has, as in a rail of toggles.
void TestEditorLayout::verticalScrollAreaFitsPanelWidth() {
	const QString text = QStringLiteral("A check box with a text that is much wider than the scroll area is made narrow");
	const auto fill = [&text](QWidget* panel) {
		auto* box = new QCheckBox(text, panel);
		auto* layout = new QVBoxLayout(panel);
		layout->addWidget(box);
		layout->addSpacing(400);
		return box;
	};

	auto* controlPanel = new QWidget;
	fill(controlPanel);
	QScrollArea control;
	control.setWidgetResizable(true);
	control.setWidget(controlPanel);
	// The width that the panel asks for, without room for the vertical scroll bar that its height needs.
	control.resize(control.widget()->sizeHint().width(), 100);
	control.show();
	QApplication::processEvents();
	QVERIFY2(control.verticalScrollBar()->isVisible(),
		"control: a plain scroll area as wide as its panel has no scroll bars, so the test proves nothing");
	QVERIFY2(control.horizontalScrollBar()->isVisible(),
		"control: a plain scroll area as wide as its panel has no scroll bars, so the test proves nothing");

	auto* panel = new QWidget;
	QCheckBox* box = fill(panel);
	EditorLayout::VerticalScrollArea scroll;
	scroll.setWidget(panel);
	// A VerticalScrollArea never shows a horizontal scroll bar.
	QCOMPARE(scroll.horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
	// A VerticalScrollArea has no frame and gives its panel the size of the area.
	QVERIFY2(scroll.widgetResizable(), "a VerticalScrollArea has a frame or a panel of its own size");
	QCOMPARE(scroll.frameShape(), QFrame::NoFrame);
	// The minimum width of a VerticalScrollArea has room for the panel and the vertical scroll bar.
	QCOMPARE_GE(scroll.minimumSizeHint().width(), scroll.widget()->minimumSizeHint().width() + scroll.verticalScrollBar()->sizeHint().width());
	scroll.resize(scroll.minimumSizeHint().width(), 100);
	scroll.show();
	QApplication::processEvents();
	QVERIFY2(scroll.verticalScrollBar()->isVisible(), "a VerticalScrollArea shorter than its panel has no vertical scroll bar");
	QVERIFY2(!scroll.horizontalScrollBar()->isVisible(), "a VerticalScrollArea shows a horizontal scroll bar");
	// A VerticalScrollArea at its minimum width does not cut its panel at the right.
	QCOMPARE_GE(box->width(), box->sizeHint().width());
	QCOMPARE_LT(box->geometry().right(), scroll.viewport()->width());
}

QTEST_MAIN(TestEditorLayout)
#include "test_editorlayout.moc"
