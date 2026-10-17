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
#include <QVBoxLayout>

#include <initializer_list>
#include <iostream>

namespace {
bool check(bool condition, const char* message) {
	if (!condition)
		std::cerr << message << '\n';
	return condition;
}

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

bool exerciseWrappedLabelProperties() {
	QWidget panel;
	const QString markup = QStringLiteral("<b>x</b> & <i>y</i>");
	QLabel* label = EditorLayout::wrappedLabel(markup, &panel);
	QLabel* empty = EditorLayout::wrappedLabel(QString(), &panel);
	// A layout reparents its widgets, so the parent is read before the labels go into one.
	bool ok = check(label->parentWidget() == &panel, "wrappedLabel does not use the parent it was given");
	arrange(panel, {label, empty});
	ok &= check(label->text() == markup, "wrappedLabel does not show the text it was given");
	ok &= check(!label->property("dialogHelp").isValid() && !label->property("dialogStatus").isValid(),
		"wrappedLabel has a help or status role");
	ok &= check(label->textFormat() == Qt::PlainText, "wrappedLabel does not show the text as plain text");
	ok &= check(label->wordWrap(), "wrappedLabel does not wrap its text");
	ok &= check(label->sizePolicy().horizontalPolicy() == QSizePolicy::Ignored,
		"wrappedLabel does not make layouts ignore its width hints");
	ok &= check(label->sizePolicy().hasHeightForWidth(), "wrappedLabel loses the height for width of its wrapped text");
	showPanel(panel);
	ok &= check(empty->isVisible(), "wrappedLabel with an empty text hides itself");
	return ok;
}

// A word wider than the panel gives a plain wrapped label, and so its panel, the width of that
// word. The control shows this, so that the checks below prove something.
bool exerciseNoWidening() {
	QWidget controlPanel;
	QLabel* control = plainWrappedLabel(kLongWord, &controlPanel);
	arrange(controlPanel, {control});
	bool ok = check(control->fontMetrics().horizontalAdvance(kLongWord) > 2 * kPanelWidth,
		"the long word is not wider than twice the panel, so the test proves nothing");
	ok &= check(controlPanel.minimumSizeHint().width() > kPanelWidth,
		"control: a plain wrapped label with a long word does not widen its panel, so the test proves nothing");

	QWidget panel;
	QLabel* label = EditorLayout::wrappedLabel(kLongWord, &panel);
	arrange(panel, {label});
	ok &= check(panel.minimumSizeHint().width() <= kPanelWidth, "a wrappedLabel with a long word raises the minimum width of its panel");
	showPanel(panel);
	ok &= check(panel.width() == kPanelWidth, "a wrappedLabel with a long word widens its panel");
	ok &= check(label->width() <= kPanelWidth, "a wrappedLabel with a long word is wider than its panel");
	return ok;
}

bool exerciseNoHorizontalScrollBar() {
	QScrollArea controlScroll;
	auto* controlPanel = new QWidget;
	arrange(*controlPanel, {plainWrappedLabel(kLongWord, controlPanel)});
	showInScrollArea(controlScroll, controlPanel);
	bool ok = check(controlScroll.horizontalScrollBar()->maximum() > 0,
		"control: a plain wrapped label with a long word gives the scroll area no horizontal scroll bar, so the test proves nothing");

	QScrollArea scroll;
	auto* panel = new QWidget;
	QLabel* label = EditorLayout::wrappedLabel(kLongWord, panel);
	arrange(*panel, {label});
	showInScrollArea(scroll, panel);
	ok &= check(scroll.horizontalScrollBar()->maximum() == 0,
		"a wrappedLabel with a long word gives the scroll area a horizontal scroll bar");
	ok &= check(label->width() <= scroll.viewport()->width(), "a wrappedLabel with a long word is wider than the scroll area");
	return ok;
}

bool exerciseWrapping() {
	QWidget panel;
	QLabel* label = EditorLayout::wrappedLabel(kSentence, &panel);
	arrange(panel, {label});
	bool ok = check(label->fontMetrics().horizontalAdvance(kSentence) > 3 * kPanelWidth,
		"the sentence is not wider than three panels, so the test proves nothing");
	showPanel(panel);
	ok &= check(label->width() <= kPanelWidth, "a wrappedLabel with a long sentence is wider than its panel");
	ok &= check(label->height() >= 3 * label->fontMetrics().lineSpacing(), "a wrappedLabel with a long sentence does not wrap");
	ok &= check(label->height() >= label->heightForWidth(label->width()),
		"a wrappedLabel with a long sentence is not as tall as its wrapped text");

	// A panel makes its labels without a text and sets it on every refresh, also after the panel is shown.
	QWidget refreshedPanel;
	QLabel* refreshed = EditorLayout::wrappedLabel(QString(), &refreshedPanel);
	arrange(refreshedPanel, {refreshed});
	showPanel(refreshedPanel);
	refreshed->setText(kSentence);
	QApplication::processEvents();
	ok &= check(refreshed->width() <= kPanelWidth, "a wrappedLabel that gets its text after the panel is shown is wider than its panel");
	ok &= check(refreshed->height() >= 3 * refreshed->fontMetrics().lineSpacing(),
		"a wrappedLabel that gets its text after the panel is shown does not wrap");
	ok &= check(refreshed->height() >= refreshed->heightForWidth(refreshed->width()),
		"a wrappedLabel that gets its text after the panel is shown is not as tall as its wrapped text");
	return ok;
}

bool exerciseEmptyStateLabel() {
	QWidget panel;
	auto* reference = new QLabel(QStringLiteral("Plain reference"), &panel);
	QLabel* label = EditorLayout::emptyStateLabel(&panel);
	bool ok = check(label->parentWidget() == &panel, "emptyStateLabel does not use the parent it was given");
	arrange(panel, {reference, label});
	showPanel(panel);
	ok &= check(label->isHidden() && !label->isVisible(), "emptyStateLabel is not hidden until it has a message");
	ok &= check(label->property("dialogHelp").toBool(), "emptyStateLabel does not have the help role");
	ok &= check(!label->property("dialogStatus").isValid(), "emptyStateLabel has a status role");
	ok &= check(label->text().isEmpty(), "emptyStateLabel has a text before it has a message");

	const QString message = QStringLiteral("Select a unit or use Add.");
	DialogLayout::setHelp(label, message);
	QApplication::processEvents();
	ok &= check(label->isVisible(), "the message of emptyStateLabel is not shown after the panel is shown");
	ok &= check(label->text() == message, "emptyStateLabel does not show the message it was given");
	ok &= check(label->palette().color(QPalette::WindowText) != reference->palette().color(QPalette::WindowText),
		"emptyStateLabel does not have the text colour of the help rule");

	DialogLayout::setHelp(label, QString());
	QApplication::processEvents();
	ok &= check(label->isHidden() && !label->isVisible(), "emptyStateLabel is not hidden again by an empty message");
	return ok;
}

bool exerciseSectionHeading() {
	const QString markup = QStringLiteral("<b>x</b>");
	QWidget parent;
	QFont font = parent.font();
	font.setPointSizeF(12);
	parent.setFont(font);
	auto* reference = new QLabel(QStringLiteral("Reference"), &parent);
	QLabel* heading = EditorLayout::sectionHeading(markup, &parent);
	bool ok = check(heading->text() == markup, "sectionHeading does not show the text it was given");
	ok &= check(heading->textFormat() == Qt::PlainText, "sectionHeading does not show the text as plain text");
	ok &= check(heading->font().bold() && !reference->font().bold(), "sectionHeading is not bold");
	ok &= check(heading->font().pointSizeF() > reference->font().pointSizeF(), "sectionHeading is not larger than the font of its parent");
	ok &= check(heading->wordWrap(), "sectionHeading does not wrap its text");
	ok &= check(heading->sizePolicy().horizontalPolicy() == QSizePolicy::Ignored,
		"sectionHeading does not make layouts ignore its width hints");
	ok &= check(heading->parentWidget() == &parent, "sectionHeading does not use the parent it was given");

	// A font in pixels has no size in points.
	QWidget pixelParent;
	QFont pixelFont = pixelParent.font();
	pixelFont.setPixelSize(14);
	pixelParent.setFont(pixelFont);
	auto* pixelReference = new QLabel(QStringLiteral("Reference"), &pixelParent);
	QLabel* pixelHeading = EditorLayout::sectionHeading(QStringLiteral("Heading"), &pixelParent);
	ok &= check(pixelReference->font().pixelSize() == 14,
		"the font of the second parent is not given in pixels, so the test proves nothing");
	ok &= check(pixelHeading->font().pixelSize() > pixelReference->font().pixelSize(),
		"sectionHeading is not larger than a font in pixels");
	ok &= check(pixelHeading->font().bold(), "sectionHeading is not bold with a font in pixels");

	QWidget panel;
	QLabel* longHeading = EditorLayout::sectionHeading(kLongWord, &panel);
	arrange(panel, {longHeading});
	ok &= check(panel.minimumSizeHint().width() <= kPanelWidth, "a sectionHeading with a long word raises the minimum width of its panel");
	return ok;
}

// The width that the text of the label needs when a line may break at a space or at a zero-width space only.
int wrappedTextWidth(const QLabel* label, int width) {
	return label->fontMetrics().boundingRect(QRect(0, 0, width, 100000), Qt::TextWordWrap, label->text()).width();
}

bool exerciseBreakableName() {
	const QString name = QStringLiteral("Amsterdam_Hilversum_Student");
	const QString broken = EditorLayout::breakableName(name);
	bool ok = check(QString(broken).remove(QChar(0x200B)) == name, "breakableName changes more than it adds");
	ok &= check(broken.count(QChar(0x200B)) == 2, "breakableName does not add a break after each underscore");
	ok &= check(EditorLayout::breakableName(QStringLiteral("Netherlands")) == QStringLiteral("Netherlands"),
		"breakableName changes a name without an underscore");

	// A panel a little wider than the longest part of the name and much narrower than the whole name.
	QWidget controlPanel;
	QLabel* control = EditorLayout::wrappedLabel(name, &controlPanel);
	const int width = control->fontMetrics().horizontalAdvance(QStringLiteral("Amsterdam_Hilversum_")) + 8;
	ok &= check(control->fontMetrics().horizontalAdvance(name) > width + 20, "the name is not wider than the panel, so the test proves nothing");
	ok &= check(wrappedTextWidth(control, width) > width,
		"control: a name with underscores fits the panel without a break, so the test proves nothing");

	QWidget panel;
	QLabel* label = EditorLayout::wrappedLabel(broken, &panel);
	auto* layout = new QVBoxLayout(&panel);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(label);
	layout->addStretch();
	panel.resize(width, 1000);
	panel.show();
	QApplication::processEvents();
	ok &= check(label->width() == width, "the label does not have the width of the panel, so the test proves nothing");
	ok &= check(wrappedTextWidth(label, label->width()) <= label->width(), "a wrapped name is cut at the edge of its panel");
	ok &= check(label->height() >= 2 * label->fontMetrics().lineSpacing(), "a wrapped name does not break between its parts");
	return ok;
}

// A scroll area for a panel of one check box with a text wider than the area, as high as the area allows.
// The check box is the widest thing that the panel has, as in a rail of toggles.
bool exerciseVerticalScrollArea() {
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
	bool ok = check(control.verticalScrollBar()->isVisible() && control.horizontalScrollBar()->isVisible(),
		"control: a plain scroll area as wide as its panel has no scroll bars, so the test proves nothing");

	auto* panel = new QWidget;
	QCheckBox* box = fill(panel);
	EditorLayout::VerticalScrollArea scroll;
	scroll.setWidget(panel);
	ok &= check(scroll.horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff, "a VerticalScrollArea can show a horizontal scroll bar");
	ok &= check(scroll.widgetResizable() && scroll.frameShape() == QFrame::NoFrame, "a VerticalScrollArea has a frame or a panel of its own size");
	ok &= check(scroll.minimumSizeHint().width() >= scroll.widget()->minimumSizeHint().width() + scroll.verticalScrollBar()->sizeHint().width(),
		"the minimum width of a VerticalScrollArea has no room for the panel and the vertical scroll bar");
	scroll.resize(scroll.minimumSizeHint().width(), 100);
	scroll.show();
	QApplication::processEvents();
	ok &= check(scroll.verticalScrollBar()->isVisible(), "a VerticalScrollArea shorter than its panel has no vertical scroll bar");
	ok &= check(!scroll.horizontalScrollBar()->isVisible(), "a VerticalScrollArea shows a horizontal scroll bar");
	ok &= check(box->width() >= box->sizeHint().width() && box->geometry().right() < scroll.viewport()->width(),
		"a VerticalScrollArea at its minimum width cuts its panel at the right");
	return ok;
}
} // namespace

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	QFile qss(QStringLiteral(EGTRAIN_DIALOG_QSS));
	if (!qss.open(QIODevice::ReadOnly)) {
		std::cerr << "cannot load application QSS\n";
		return 1;
	}
	// The test binary has no Qt resources, so point the stylesheet at the icon files.
	QString styleSheet = QString::fromUtf8(qss.readAll());
	styleSheet.replace(QStringLiteral(":/icons/"),
		QFileInfo(qss).absolutePath() + QStringLiteral("/../icons/"));
	app.setStyleSheet(styleSheet);
	const bool properties = exerciseWrappedLabelProperties();
	const bool widening = exerciseNoWidening();
	const bool scrollBar = exerciseNoHorizontalScrollBar();
	const bool wrapping = exerciseWrapping();
	const bool emptyState = exerciseEmptyStateLabel();
	const bool heading = exerciseSectionHeading();
	const bool name = exerciseBreakableName();
	const bool verticalScroll = exerciseVerticalScrollArea();
	return properties && widening && scrollBar && wrapping && emptyState && heading && name && verticalScroll ? 0 : 1;
}
