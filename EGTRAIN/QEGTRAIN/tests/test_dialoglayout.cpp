#include "widgets/DialogLayout.h"

#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QToolBar>
#include <QToolButton>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QImage>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QProxyStyle>
#include <QPushButton>
#include <QScrollBar>
#include <QStyleFactory>
#include <QTableWidget>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>

namespace {
bool check(bool condition, const char* message) {
	if (!condition)
		std::cerr << message << '\n';
	return condition;
}

// Counts pixels in the leftmost 40 px of an item row that differ clearly from the row background,
// and reports the largest difference. Items have no text, so only the indicator is in that strip.
struct Indicator {
	int strongPixels = 0;
	int maxDifference = 0;
};

Indicator measureIndicator(const QImage& image, const QRect& row) {
	const int background = qGray(image.pixel(row.left() + 90, row.center().y()));
	Indicator result;
	for (int y = row.top(); y <= row.bottom(); ++y) {
		for (int x = row.left(); x < row.left() + 40; ++x) {
			const int difference = std::abs(qGray(image.pixel(x, y)) - background);
			result.maxDifference = std::max(result.maxDifference, difference);
			result.strongPixels += difference >= 90 ? 1 : 0;
		}
	}
	return result;
}

bool checkIndicators(const QImage& image, const QRect& unchecked, const QRect& alternate,
	const QRect& selected, const QRect& checked, const char* surface) {
	const Indicator plain = measureIndicator(image, unchecked);
	const Indicator striped = measureIndicator(image, alternate);
	const Indicator highlighted = measureIndicator(image, selected);
	const Indicator ticked = measureIndicator(image, checked);
	bool ok = check(plain.maxDifference >= 90, surface);
	ok &= check(striped.maxDifference >= 90, surface);
	ok &= check(highlighted.maxDifference >= 90, surface);
	ok &= check(ticked.strongPixels >= 2 * plain.strongPixels && plain.strongPixels > 0, surface);
	return ok;
}

bool exerciseItemViewIndicators() {
	const Qt::CheckState states[] = {Qt::Unchecked, Qt::Unchecked, Qt::Checked, Qt::Unchecked};
	QListWidget list;
	list.setAlternatingRowColors(true);
	list.resize(200, 160);
	QTableWidget table(4, 1);
	table.setAlternatingRowColors(true);
	table.setColumnWidth(0, 120);
	table.horizontalHeader()->hide();
	table.verticalHeader()->hide();
	table.resize(200, 160);
	for (int row = 0; row < 4; ++row) {
		auto* listItem = new QListWidgetItem(&list);
		listItem->setFlags(listItem->flags() | Qt::ItemIsUserCheckable);
		listItem->setCheckState(states[row]);
		auto* tableItem = new QTableWidgetItem;
		tableItem->setFlags(tableItem->flags() | Qt::ItemIsUserCheckable);
		tableItem->setCheckState(states[row]);
		table.setItem(row, 0, tableItem);
	}
	list.setCurrentRow(3);
	table.setCurrentCell(3, 0);
	list.show();
	table.show();
	QApplication::processEvents();

	const QImage listImage = list.viewport()->grab().toImage();
	const QImage tableImage = table.viewport()->grab().toImage();
	auto listRow = [&](int row) { return list.visualItemRect(list.item(row)); };
	auto tableRow = [&](int row) { return table.visualItemRect(table.item(row, 0)); };
	bool ok = checkIndicators(listImage, listRow(0), listRow(1), listRow(3), listRow(2),
		"list item indicators are not clearly visible");
	ok &= checkIndicators(tableImage, tableRow(0), tableRow(1), tableRow(3), tableRow(2),
		"table item indicators are not clearly visible");
	return ok;
}

bool exercise(const QRect& screen, qreal scale) {
	QDialog dialog;
	QFont font = dialog.font();
	font.setPointSizeF(12 * scale);
	dialog.setFont(font);
	auto* body = new QWidget;
	auto* fields = new QVBoxLayout(body);
	for (int i = 0; i < 80; ++i) {
		auto* label = new QLabel(QStringLiteral("Service %1: Copenhagen to a long destination with a long platform name")
									 .arg(i),
			body);
		label->setWordWrap(true);
		fields->addWidget(label);
	}
	auto* lastField = new QLineEdit(body);
	lastField->setPlaceholderText(QStringLiteral("Last field"));
	fields->addWidget(lastField);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	buttons->button(QDialogButtonBox::Ok)->setDefault(true);
	const QString longName(2500, QLatin1Char('X'));
	auto* scroll = DialogLayout::install(dialog, QStringLiteral("Review <simulation> & ") + longName.left(120),
		QStringLiteral("Case <identifier>: ") + longName,
		body, buttons, screen);
	dialog.show();
	QApplication::processEvents();
	bool ok = check(dialog.width() <= screen.width() * 9 / 10 && dialog.height() <= screen.height() * 4 / 5,
		"dialog exceeds available screen budget");
	ok &= check(scroll->verticalScrollBar()->maximum() > 0,
		"long content does not scroll");
	scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
	ok &= check(buttons->isVisible() && buttons->geometry().bottom() <= dialog.height() && scroll->geometry().bottom() <= buttons->geometry().top(),
		"footer must remain reachable outside the scrolling body");
	auto* context = dialog.findChild<QLabel*>(QStringLiteral("dialogContext"));
	ok &= check(dialog.windowTitle() == QStringLiteral("Review <simulation> & ") + longName.left(120)
			&& !dialog.findChild<QLabel*>(QStringLiteral("dialogHeading")),
		"the heading is the window title and is not repeated in the dialog");
	ok &= check(context && context->textFormat() == Qt::PlainText && context->parentWidget() == scroll->widget(),
		"the context remains plain text and scrolls");
	buttons->button(QDialogButtonBox::Ok)->setFocus();
	scroll->verticalScrollBar()->setValue(0);
	QKeyEvent backtab(QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
	QApplication::sendEvent(buttons->button(QDialogButtonBox::Ok), &backtab);
	QApplication::processEvents();
	const QRect fieldRect(lastField->mapTo(scroll->viewport(), QPoint()), lastField->size());
	ok &= check(lastField->hasFocus() && scroll->viewport()->rect().contains(fieldRect),
		"Backtab from footer must reveal the focused body field");
	QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
	QApplication::sendEvent(lastField, &tab);
	ok &= check(buttons->button(QDialogButtonBox::Ok)->hasFocus() || buttons->button(QDialogButtonBox::Cancel)->hasFocus(),
		"Tab from last body field reaches fixed footer");
	buttons->button(QDialogButtonBox::Ok)->setFocus();
	QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
	QApplication::sendEvent(buttons->button(QDialogButtonBox::Ok), &enter);
	ok &= check(dialog.result() == QDialog::Accepted, "default Enter must accept");

	QDialog cancelDialog;
	auto* cancelBody = new QWidget;
	cancelBody->setLayout(new QVBoxLayout);
	auto* cancelButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	QObject::connect(cancelButtons, &QDialogButtonBox::accepted, &cancelDialog, &QDialog::accept);
	QObject::connect(cancelButtons, &QDialogButtonBox::rejected, &cancelDialog, &QDialog::reject);
	DialogLayout::install(cancelDialog, QStringLiteral("Choose case"), QString(),
		cancelBody, cancelButtons, screen);
	cancelDialog.show();
	QApplication::processEvents();
	QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
	QApplication::sendEvent(&cancelDialog, &escape);
	ok &= check(cancelDialog.result() == QDialog::Rejected && !cancelDialog.isVisible(),
		"Escape must reject without accepting");
	return ok;
}

// Without a field in the body the default button has the focus. Tab moves it to the next footer button and Space presses
// that button.
bool exerciseFooterKeys() {
	QDialog dialog;
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	DialogLayout::install(dialog, QStringLiteral("Choose case"), QString(), new QWidget, buttons, QRect(0, 0, 1280, 800));
	dialog.show();
	dialog.activateWindow();
	QApplication::processEvents();
	QPushButton* okButton = buttons->button(QDialogButtonBox::Ok);
	QPushButton* cancelButton = buttons->button(QDialogButtonBox::Cancel);
	bool ok = check(okButton->hasFocus() && okButton->isDefault() && !cancelButton->isDefault(), "OK does not have the focus and the default");
	QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
	QApplication::sendEvent(okButton, &tab);
	ok &= check(cancelButton->hasFocus(), "Tab does not move from OK to Cancel");
	QKeyEvent spacePress(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "));
	QKeyEvent spaceRelease(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "));
	QApplication::sendEvent(cancelButton, &spacePress);
	QApplication::sendEvent(cancelButton, &spaceRelease);
	ok &= check(dialog.result() == QDialog::Rejected && !dialog.isVisible(), "Space does not press the focused Cancel");
	return ok;
}

// An installed dialog whose body is a plain widget of a given height.
struct Opened {
	QDialog dialog;
	QDialogButtonBox* buttons = nullptr;
	QScrollArea* scroll = nullptr;
	int limit = 0;
};

void openDialog(Opened& opened, const QRect& screen, qreal scale, int bodyHeight, const QString& context) {
	QFont font = opened.dialog.font();
	font.setPointSizeF(12 * scale);
	opened.dialog.setFont(font);
	auto* body = new QWidget;
	auto* fields = new QVBoxLayout(body);
	auto* filler = new QWidget(body);
	filler->setFixedHeight(bodyHeight);
	fields->addWidget(filler);
	opened.buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	opened.scroll = DialogLayout::install(opened.dialog, QStringLiteral("Edit"), context, body, opened.buttons, screen);
	opened.limit = screen.height() * 4 / 5;
	opened.dialog.show();
	QApplication::processEvents();
}

// A dialog opens at the height its content needs, up to the height limit, and scrolls only beyond it.
bool exerciseContentHeight(qreal scale) {
	const QRect screen(0, 0, 1920, 1080);
	bool ok = true;
	for (const int bodyHeight : {20, 500}) {
		Opened fits;
		openDialog(fits, screen, scale, bodyHeight, QStringLiteral("Context"));
		ok &= check(fits.scroll->verticalScrollBar()->maximum() == 0 && !fits.scroll->verticalScrollBar()->isVisible(),
			"a body shorter than the height limit opens with a vertical scroll bar");
		ok &= check(fits.dialog.height() < fits.limit, "a body shorter than the height limit opens at the height limit");
		if (bodyHeight == 20)
			ok &= check(fits.dialog.height() < 16 * QFontMetrics(fits.dialog.font()).height(),
				"a dialog with a short body opens taller than its content needs");
		ok &= check(fits.buttons->isVisible() && fits.buttons->geometry().bottom() <= fits.dialog.height(),
			"the footer of a dialog that fits is not visible");
	}
	Opened tall;
	openDialog(tall, screen, scale, 3000, QStringLiteral("Context"));
	ok &= check(tall.scroll->verticalScrollBar()->maximum() > 0, "a body taller than the height limit does not scroll");
	ok &= check(tall.dialog.height() == tall.limit, "a body taller than the height limit does not open at the height limit");
	ok &= check(tall.buttons->isVisible() && tall.scroll->geometry().bottom() <= tall.buttons->geometry().top()
			&& tall.buttons->geometry().bottom() <= tall.dialog.height(),
		"the footer of a dialog that scrolls is not visible below the body");
	return ok;
}

// A message shown in a shown dialog makes the dialog taller, up to the height limit, instead of scrolling the body.
bool exerciseStatusAfterOpen(qreal scale) {
	const QRect screen(0, 0, 1920, 1080);
	const QString longText = QStringLiteral("The planned time is before the previous stop, so the stop would be out of order. ").repeated(3);
	bool ok = true;
	for (const int bodyHeight : {400, 3000}) {
		QDialog dialog;
		QFont font = dialog.font();
		font.setPointSizeF(12 * scale);
		dialog.setFont(font);
		auto* body = new QWidget;
		auto* fields = new QVBoxLayout(body);
		auto* filler = new QWidget(body);
		filler->setFixedHeight(bodyHeight);
		fields->addWidget(filler);
		auto* message = new QLabel(&dialog);
		fields->addWidget(message);
		DialogLayout::setStatus(message, DialogLayout::Severity::Error, QString());
		auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
		QScrollArea* scroll = DialogLayout::install(dialog, QStringLiteral("Edit"), QString(), body, buttons, screen);
		dialog.show();
		QApplication::processEvents();
		const int before = dialog.height();
		DialogLayout::setStatus(message, DialogLayout::Severity::Error, longText);
		QApplication::processEvents();
		const int limit = screen.height() * 4 / 5;
		if (bodyHeight == 400) {
			ok &= check(dialog.height() > before, "a message shown in a shown dialog does not make it taller");
			ok &= check(scroll->verticalScrollBar()->maximum() == 0 && message->isVisible(),
				"a message shown in a shown dialog that has room for it scrolls the body");
		} else {
			ok &= check(dialog.height() == limit && before == limit, "a message in a dialog at the height limit changes its height");
		}
		ok &= check(buttons->geometry().bottom() <= dialog.height(), "the footer is not visible after a message is shown");
	}
	return ok;
}

// fitWidthToContent makes a shown dialog taller for a body that has grown, up to the height limit, and never shorter.
bool exerciseFitHeight() {
	const QRect screen(0, 0, 1920, 1080);
	QDialog dialog;
	auto* body = new QWidget;
	auto* fields = new QVBoxLayout(body);
	auto* filler = new QWidget(body);
	filler->setFixedHeight(100);
	fields->addWidget(filler);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	QScrollArea* scroll = DialogLayout::install(dialog, QStringLiteral("Fit"), QString(), body, buttons, screen);
	dialog.show();
	QApplication::processEvents();
	const int limit = screen.height() * 4 / 5;
	const int initial = dialog.height();
	filler->setFixedHeight(600);
	DialogLayout::fitWidthToContent(dialog);
	QApplication::processEvents();
	const int grown = dialog.height();
	bool ok = check(grown > initial && grown <= limit, "fitWidthToContent does not make the dialog taller for a taller body");
	ok &= check(scroll->verticalScrollBar()->maximum() == 0, "a dialog made taller by fitWidthToContent still scrolls its body");
	filler->setFixedHeight(100);
	DialogLayout::fitWidthToContent(dialog);
	QApplication::processEvents();
	ok &= check(dialog.height() == grown, "fitWidthToContent shortens the dialog");
	filler->setFixedHeight(3000);
	DialogLayout::fitWidthToContent(dialog);
	QApplication::processEvents();
	ok &= check(dialog.height() == limit, "fitWidthToContent grows the dialog beyond the height limit");
	return ok;
}

// The context line is as tall as its text, also when the dialog is taller than its content.
bool exerciseContextHeight(qreal scale) {
	const QString longContext = QStringLiteral("Netherlands / baseline with a long context that wraps onto more than one line. ").repeated(6);
	bool ok = true;
	for (const QString& context : {QStringLiteral("Netherlands / baseline"), longContext}) {
		Opened opened;
		openDialog(opened, QRect(0, 0, 1920, 1080), scale, 20, context);
		auto* label = opened.dialog.findChild<QLabel*>(QStringLiteral("dialogContext"));
		ok &= check(label != nullptr, "the dialog has no context line");
		if (!label)
			continue;
		opened.dialog.resize(opened.dialog.width(), opened.dialog.height() + 200);
		QApplication::processEvents();
		ok &= check(label->height() == label->heightForWidth(label->width()),
			"the context line is taller than its text: the dialog gives it space it does not use");
	}
	return ok;
}

// The background of a widget as the pixel 6 px inside its right edge at half its height. The text
// of the labels that use it is short and left aligned, so that point is away from text, border and
// rounded corners. The whole value is returned, alpha included: a transparent background may grab as
// transparent.
QRgb sampleBackground(QWidget* widget) {
	const QImage image = widget->grab().toImage();
	return image.pixel(image.width() - 1 - 6, image.height() / 2);
}

bool exerciseInlineMessages() {
	using DialogLayout::Severity;
	QDialog dialog;
	auto* body = new QWidget;
	auto* fields = new QVBoxLayout(body);
	auto addLabel = [&](const QString& text) {
		auto* label = new QLabel(text, body);
		fields->addWidget(label);
		return label;
	};
	auto* neutral = addLabel(QString());
	DialogLayout::setStatus(neutral, Severity::Neutral, QStringLiteral("Neutral reference"));
	auto* warning = addLabel(QString());
	DialogLayout::setStatus(warning, Severity::Warning, QStringLiteral("Warning reference"));
	auto* error = addLabel(QString());
	DialogLayout::setStatus(error, Severity::Error, QStringLiteral("Error reference"));
	auto* help = addLabel(QString());
	DialogLayout::setHelp(help, QStringLiteral("Help reference"));
	auto* plain = addLabel(QStringLiteral("Plain label"));
	auto* subject = addLabel(QStringLiteral("Subject label"));
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	DialogLayout::install(dialog, QStringLiteral("Inline messages"), QString(), body, buttons, QRect(0, 0, 1280, 800));
	dialog.show();
	QApplication::processEvents();

	auto setStatus = [&](Severity severity, const QString& text) {
		DialogLayout::setStatus(subject, severity, text);
		QApplication::processEvents();
	};
	auto setHelp = [&](const QString& text) {
		DialogLayout::setHelp(subject, text);
		QApplication::processEvents();
	};
	auto hasStatusOnly = [&](const char* value) {
		return subject->property("dialogStatus").toString() == QLatin1String(value) && !subject->property("dialogHelp").isValid();
	};
	auto hasHelpOnly = [&]() { return subject->property("dialogHelp").toBool() && !subject->property("dialogStatus").isValid(); };

	const QRgb plainBackground = sampleBackground(plain);
	const QRgb neutralBackground = sampleBackground(neutral);
	const QRgb warningBackground = sampleBackground(warning);
	const QRgb errorBackground = sampleBackground(error);
	const QRgb helpBackground = sampleBackground(help);
	bool ok = check(neutralBackground != warningBackground && neutralBackground != errorBackground && warningBackground != errorBackground
			&& plainBackground != neutralBackground && plainBackground != warningBackground && plainBackground != errorBackground,
		"reference backgrounds are not pairwise different: the stylesheet is not loaded or a role does not change the look of a label");

	// The subject was shown as a plain label, so every call below changes a polished label.
	setStatus(Severity::Error, QStringLiteral("Subject error"));
	ok &= check(sampleBackground(subject) == errorBackground, "setStatus(Error) after show does not give the error callout background");
	ok &= check(sampleBackground(subject) != neutralBackground, "setStatus(Error) after show still looks like the neutral callout");
	setStatus(Severity::Neutral, QStringLiteral("Subject neutral"));
	ok &= check(sampleBackground(subject) == neutralBackground, "setStatus(Neutral) after the error callout does not give the neutral background");
	setStatus(Severity::Warning, QStringLiteral("Subject warning"));
	ok &= check(sampleBackground(subject) == warningBackground, "setStatus(Warning) after show does not give the warning callout background");

	// A label has one role: help, status, help, status replace each other.
	setHelp(QStringLiteral("Subject help"));
	ok &= check(hasHelpOnly(), "setHelp does not set dialogHelp and remove dialogStatus");
	ok &= check(sampleBackground(subject) == helpBackground && helpBackground != neutralBackground && helpBackground != warningBackground
			&& helpBackground != errorBackground,
		"setHelp after a status does not give the help background");
	const QColor helpColor = help->palette().color(QPalette::WindowText);
	ok &= check(subject->palette().color(QPalette::WindowText) == helpColor && helpColor != plain->palette().color(QPalette::WindowText),
		"setHelp does not give the text colour of the help rule");
	setStatus(Severity::Error, QStringLiteral("Subject error"));
	ok &= check(hasStatusOnly("error"), "setStatus(Error) does not set dialogStatus to error and remove dialogHelp");
	ok &= check(sampleBackground(subject) == errorBackground, "setStatus(Error) after help does not give the error callout background");
	setHelp(QStringLiteral("Subject help"));
	ok &= check(hasHelpOnly(), "setHelp after a status does not remove dialogStatus");
	ok &= check(sampleBackground(subject) == helpBackground, "setHelp after the error callout does not give the help background");
	setStatus(Severity::Warning, QStringLiteral("Subject warning"));
	ok &= check(hasStatusOnly("warning"), "setStatus(Warning) does not set dialogStatus to warning and remove dialogHelp");
	ok &= check(sampleBackground(subject) == warningBackground, "setStatus(Warning) after help does not give the warning callout background");
	setStatus(Severity::Neutral, QStringLiteral("Subject neutral"));
	ok &= check(hasStatusOnly("neutral"), "setStatus(Neutral) does not set dialogStatus to neutral and remove dialogHelp");

	// An empty text hides the label, a text shows it, and markup stays literal.
	setStatus(Severity::Error, QString());
	ok &= check(subject->isHidden() && !subject->isVisible(), "setStatus with an empty text does not hide the label");
	setStatus(Severity::Warning, QStringLiteral("<b>x</b>"));
	ok &= check(subject->isVisible(), "setStatus with a text does not show a hidden label");
	ok &= check(subject->text() == QStringLiteral("<b>x</b>"), "setStatus does not show the text it was given");
	ok &= check(subject->textFormat() == Qt::PlainText, "setStatus does not show the text as plain text");
	setHelp(QString());
	ok &= check(subject->isHidden() && !subject->isVisible(), "setHelp with an empty text does not hide the label");
	setHelp(QStringLiteral("<b>x</b>"));
	ok &= check(subject->isVisible(), "setHelp with a text does not show a hidden label");
	ok &= check(subject->text() == QStringLiteral("<b>x</b>"), "setHelp does not show the text it was given");
	ok &= check(subject->textFormat() == Qt::PlainText, "setHelp does not show the text as plain text");
	return ok;
}

// An editor panel gives its message labels their role while it is hidden. Labels set before the panel is
// shown look the same as labels set afterwards, and the text colour of every role differs from a plain label.
bool exercisePanelMessages() {
	using DialogLayout::Severity;
	QWidget panel;
	auto* fields = new QVBoxLayout(&panel);
	auto addLabel = [&]() {
		auto* label = new QLabel(&panel);
		fields->addWidget(label);
		return label;
	};
	QLabel* plain = addLabel();
	plain->setText(QStringLiteral("Plain label"));
	QLabel* early[3] = {addLabel(), addLabel(), addLabel()};
	QLabel* late[3] = {addLabel(), addLabel(), addLabel()};
	DialogLayout::setStatus(early[0], Severity::Error, QStringLiteral("Early error"));
	DialogLayout::setStatus(early[1], Severity::Warning, QStringLiteral("Early warning"));
	DialogLayout::setHelp(early[2], QStringLiteral("Early help"));
	panel.resize(400, 400);
	panel.show();
	QApplication::processEvents();
	DialogLayout::setStatus(late[0], Severity::Error, QStringLiteral("Late error"));
	DialogLayout::setStatus(late[1], Severity::Warning, QStringLiteral("Late warning"));
	DialogLayout::setHelp(late[2], QStringLiteral("Late help"));
	QApplication::processEvents();

	bool ok = true;
	for (int role = 0; role < 3; ++role) {
		ok &= check(early[role]->isVisible() && late[role]->isVisible(), "a message label of a panel is not shown with its panel");
		ok &= check(sampleBackground(early[role]) == sampleBackground(late[role])
				&& early[role]->palette().color(QPalette::WindowText) == late[role]->palette().color(QPalette::WindowText),
			"a role given before the panel is shown does not look as the same role given after");
		ok &= check(early[role]->palette().color(QPalette::WindowText) != plain->palette().color(QPalette::WindowText),
			"a role of a message label does not change the text colour of a plain label");
	}
	ok &= check(early[0]->palette().color(QPalette::WindowText) != early[1]->palette().color(QPalette::WindowText)
			&& early[0]->palette().color(QPalette::WindowText) != early[2]->palette().color(QPalette::WindowText)
			&& early[1]->palette().color(QPalette::WindowText) != early[2]->palette().color(QPalette::WindowText),
		"the error, warning and help roles do not have three text colours");
	return ok;
}

// A plain widget, not a dialog, as an editor panel is. Its labels hold a word wider than the widget
// and a text that needs many lines.
bool exerciseNarrowBody() {
	QWidget body;
	auto* fields = new QVBoxLayout(&body);
	auto* longWord = new QLabel(&body);
	fields->addWidget(longWord);
	auto* manyWords = new QLabel(&body);
	fields->addWidget(manyWords);
	fields->addStretch();
	const QString word(400, QLatin1Char('W'));
	const QString words = QStringLiteral("word ").repeated(60);
	bool ok = check(longWord->fontMetrics().horizontalAdvance(word) > 320, "the long word is not wider than the body, so the test proves nothing");
	ok &= check(manyWords->fontMetrics().horizontalAdvance(words) > 960, "the long text does not need more than three lines, so the test proves nothing");
	DialogLayout::setStatus(longWord, DialogLayout::Severity::Error, word);
	DialogLayout::setStatus(manyWords, DialogLayout::Severity::Warning, words);
	// The body is tall enough for the wrapped text with any font, and the stretch keeps the labels at their own height.
	body.resize(320, 1000);
	body.show();
	QApplication::processEvents();

	ok &= check(body.width() == 320 && longWord->width() <= 320, "a label with a word wider than the body widens the body");
	ok &= check(manyWords->width() <= 320, "a label with a long text is wider than the body");
	ok &= check(manyWords->height() >= 3 * manyWords->fontMetrics().lineSpacing(), "a label with a long text does not wrap");
	ok &= check(manyWords->height() >= manyWords->heightForWidth(manyWords->width()), "a label with a long text is not as tall as its wrapped text");
	return ok;
}
// A shown dialog whose body gets a wider choice: fitWidthToContent widens the dialog for it at once, not only after the
// queued layout requests have run.
bool exerciseFitAfterChange() {
	QDialog dialog;
	auto* body = new QWidget;
	auto* form = new QFormLayout(body);
	auto* choice = new QComboBox(&dialog);
	choice->setSizeAdjustPolicy(QComboBox::AdjustToContents);
	choice->addItem(QStringLiteral("short"));
	form->addRow(QStringLiteral("Choice:"), choice);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	DialogLayout::install(dialog, QStringLiteral("Fit"), QString(), body, buttons, QRect(0, 0, 1280, 800));
	DialogLayout::fitWidthToContent(dialog);
	dialog.show();
	QApplication::processEvents();

	const int before = dialog.width();
	const QString wide(60, QLatin1Char('W'));
	const int needed = choice->fontMetrics().horizontalAdvance(wide);
	bool ok = check(needed > before && needed < dialog.maximumWidth(), "the wide choice does not need a wider dialog within its limit, so the test proves nothing");
	choice->addItem(wide);
	choice->setCurrentIndex(1);
	DialogLayout::fitWidthToContent(dialog);
	ok &= check(dialog.width() >= needed, "a choice added to a shown dialog widens it");
	return ok;
}

struct ControlLook {
	QRgb face;
	QRgb ink;
};

int colorDifference(QRgb left, QRgb right) {
	return std::abs(qRed(left) - qRed(right)) + std::abs(qGreen(left) - qGreen(right)) + std::abs(qBlue(left) - qBlue(right));
}

// The common interior colour is the face; the strongest contrast in the text strip is the ink.
// The strip excludes borders, the combo arrow and the check box indicator, including at high DPI.
ControlLook controlLook(QWidget* widget) {
	const QPixmap grab = widget->grab();
	const QImage image = grab.toImage();
	const qreal ratio = grab.devicePixelRatio();
	auto pixels = [ratio](const QRect& rect) {
		return QRect(qRound(rect.x() * ratio), qRound(rect.y() * ratio), qRound(rect.width() * ratio), qRound(rect.height() * ratio));
	};
	const QRect interior = pixels(widget->rect().adjusted(3, 3, -3, -3));
	std::map<QRgb, int> counts;
	for (int y = interior.top(); y <= interior.bottom(); ++y)
		for (int x = interior.left(); x <= interior.right(); ++x)
			++counts[image.pixel(x, y)];
	const QRgb face = std::max_element(counts.begin(), counts.end(), [](const auto& left, const auto& right) {
		return left.second < right.second;
	})->first;
	const QRect text = pixels(QRect(32, 8, widget->width() - 62, widget->height() - 16));
	QRgb ink = face;
	for (int y = text.top(); y <= text.bottom(); ++y)
		for (int x = text.left(); x <= text.right(); ++x)
			if (colorDifference(image.pixel(x, y), face) > colorDifference(ink, face))
				ink = image.pixel(x, y);
	return {face, ink};
}

bool sameLook(const ControlLook& left, const ControlLook& right) {
	return left.face == right.face && left.ink == right.ink;
}

bool checkLooks(bool condition, const std::string& message, const ControlLook& left, const ControlLook& right) {
	auto describe = [](const ControlLook& look) {
		return QStringLiteral("face=%1 ink=%2").arg(look.face, 8, 16, QLatin1Char('0')).arg(look.ink, 8, 16, QLatin1Char('0')).toStdString();
	};
	return check(condition, (message + ": " + describe(left) + " versus " + describe(right)).c_str());
}

enum class ControlKind { PushButton,
	ComboBox,
	LineEdit,
	ReadOnly,
	ToolButton,
	CheckedToolButton,
	CheckBox };

std::pair<ControlLook, ControlLook> controlLooks(ControlKind kind, bool presentation, bool inherited) {
	QWidget panel;
	QDialog dialog;
	auto* body = new QWidget;
	auto* fields = new QVBoxLayout(body);
	const QString text = QStringLiteral("MMMM MMMM");
	QWidget* control = nullptr;
	QAction* action = nullptr;
	if (kind == ControlKind::PushButton) {
		auto* button = new QPushButton(text, body);
		button->setAutoDefault(false);
		control = button;
	} else if (kind == ControlKind::ComboBox) {
		auto* combo = new QComboBox(body);
		combo->addItem(text);
		control = combo;
	} else if (kind == ControlKind::LineEdit || kind == ControlKind::ReadOnly) {
		auto* edit = new QLineEdit(text, body);
		edit->setReadOnly(kind == ControlKind::ReadOnly);
		control = edit;
	} else if (kind == ControlKind::CheckBox) {
		control = new QCheckBox(text, body);
	} else {
		auto* toolbar = new QToolBar(body);
		fields->addWidget(toolbar);
		action = new QAction(text, toolbar);
		action->setCheckable(kind == ControlKind::CheckedToolButton);
		action->setChecked(kind == ControlKind::CheckedToolButton);
		toolbar->addAction(action);
		auto* button = qobject_cast<QToolButton*>(toolbar->widgetForAction(action));
		button->setAutoRaise(true);
		button->setToolButtonStyle(Qt::ToolButtonTextOnly);
		control = button;
	}
	control->setFont(qApp->font());
	control->setFocusPolicy(Qt::NoFocus);
	control->setFixedSize(150, 48);
	if (!action)
		fields->addWidget(control);
	if (presentation) {
		auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
		DialogLayout::install(dialog, QStringLiteral("Control states"), QString(), body, buttons, QRect(0, 0, 1920, 1080));
		dialog.show();
	} else {
		auto* layout = new QVBoxLayout(&panel);
		layout->addWidget(body);
		panel.show();
	}
	QApplication::processEvents();
	control->setFixedSize(150, 48);
	QApplication::processEvents();
	const ControlLook enabled = controlLook(control);
	if (inherited)
		body->setEnabled(false);
	else if (action)
		action->setEnabled(false);
	else
		control->setEnabled(false);
	QApplication::processEvents();
	return {enabled, controlLook(control)};
}

// The look of the default button of a dialog footer, enabled and then disabled.
std::pair<ControlLook, ControlLook> dialogDefaultLooks() {
	QDialog dialog;
	auto* body = new QWidget;
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	auto* primary = buttons->button(QDialogButtonBox::Ok);
	primary->setText(QStringLiteral("MMMM MMMM"));
	primary->setFocusPolicy(Qt::NoFocus);
	primary->setFont(qApp->font());
	buttons->button(QDialogButtonBox::Cancel)->setAutoDefault(false);
	buttons->button(QDialogButtonBox::Cancel)->setFocusPolicy(Qt::NoFocus);
	DialogLayout::install(dialog, QStringLiteral("Default action"), QString(), body, buttons, QRect(0, 0, 1920, 1080));
	dialog.show();
	QApplication::processEvents();
	primary->setFixedSize(150, 48);
	primary->setDefault(true);
	QApplication::processEvents();
	const ControlLook enabledDefault = controlLook(primary);
	primary->setEnabled(false);
	QApplication::processEvents();
	return {enabledDefault, controlLook(primary)};
}

// The look of one row of a shown menu: the common colour of the row is the face, the strongest contrast in it the ink.
ControlLook menuRowLook(QMenu& menu, QAction* action) {
	const QPixmap grab = menu.grab();
	const QImage image = grab.toImage();
	const qreal ratio = grab.devicePixelRatio();
	const QRect row = menu.actionGeometry(action).adjusted(4, 2, -4, -2);
	const int left = qRound(row.left() * ratio);
	const int right = qRound(row.right() * ratio);
	const int top = qRound(row.top() * ratio);
	const int bottom = qRound(row.bottom() * ratio);
	std::map<QRgb, int> counts;
	for (int y = top; y <= bottom; ++y)
		for (int x = left; x <= right; ++x)
			++counts[image.pixel(x, y)];
	const QRgb face = std::max_element(counts.begin(), counts.end(), [](const auto& a, const auto& b) { return a.second < b.second; })->first;
	QRgb ink = face;
	for (int y = top; y <= bottom; ++y)
		for (int x = left; x <= right; ++x)
			if (colorDifference(image.pixel(x, y), face) > colorDifference(ink, face))
				ink = image.pixel(x, y);
	return {face, ink};
}

// A menu with an enabled and a disabled entry: the disabled text is lighter.
bool exerciseDisabledMenuItems() {
	QMenu menu;
	QAction* live = menu.addAction(QStringLiteral("MMMM MMMM"));
	QAction* gone = menu.addAction(QStringLiteral("MMMM MMMM"));
	gone->setEnabled(false);
	menu.show();
	QApplication::processEvents();
	const ControlLook enabled = menuRowLook(menu, live);
	const ControlLook disabled = menuRowLook(menu, gone);
	menu.hide();
	bool ok = checkLooks(colorDifference(enabled.face, enabled.ink) >= 90, "an enabled menu entry draws text distinct from its face", enabled, disabled);
	ok &= checkLooks(qGray(disabled.ink) >= qGray(enabled.ink) + 30, "a disabled menu entry has lighter text", enabled, disabled);
	return ok;
}

bool exerciseDisabledControls() {
	bool ok = true;
	const std::pair<ControlKind, const char*> controls[] = {
		{ControlKind::PushButton, "push button"}, {ControlKind::ComboBox, "combo box"},
		{ControlKind::LineEdit, "line edit"}, {ControlKind::ToolButton, "tool button"}, {ControlKind::CheckBox, "check box"}};
	for (const auto& entry : controls) {
		const auto plain = controlLooks(entry.first, false, false);
		const auto dialog = controlLooks(entry.first, true, false);
		const auto parent = controlLooks(entry.first, false, true);
		const std::string name = entry.second;
		ok &= checkLooks(colorDifference(plain.first.face, plain.first.ink) >= 90,
			"an enabled " + name + " draws text distinct from its face", plain.first, plain.second);
		ok &= checkLooks(qGray(plain.second.ink) >= qGray(plain.first.ink) + 30,
			"a disabled " + name + " has lighter text", plain.first, plain.second);
		const bool box = entry.first == ControlKind::PushButton || entry.first == ControlKind::ComboBox || entry.first == ControlKind::LineEdit;
		if (box) {
			ok &= checkLooks(plain.first.face != plain.second.face,
				"a disabled " + name + " has a different face", plain.first, plain.second);
			ok &= checkLooks(sameLook(plain.second, dialog.second) && qGray(plain.second.ink) >= qGray(plain.first.ink) + 30,
				"a disabled " + name + " looks the same inside and outside a dialog", plain.second, dialog.second);
		}
		ok &= checkLooks(sameLook(parent.second, plain.second) && qGray(parent.second.ink) >= qGray(parent.first.ink) + 30,
			"a " + name + " disabled only because its parent is disabled looks like a disabled one", parent.second, plain.second);
	}
	const auto readOnly = controlLooks(ControlKind::ReadOnly, false, false);
	ok &= checkLooks(sameLook(readOnly.first, readOnly.second), "a disabled read-only line edit keeps its read-only look", readOnly.first, readOnly.second);
	const auto checked = controlLooks(ControlKind::CheckedToolButton, false, false);
	ok &= checkLooks(sameLook(checked.first, checked.second), "a disabled checked tool button keeps its checked look", checked.first, checked.second);

	const auto defaults = dialogDefaultLooks();
	ok &= checkLooks(qGray(defaults.second.face) >= qGray(defaults.first.face) + 30,
		"a disabled default button keeps its lighter face", defaults.first, defaults.second);
	ok &= exerciseDisabledMenuItems();
	return ok;
}

// A question box with Yes and No, No the default, built and styled without running its event loop. Its default button
// looks like the default button of a dialog footer and the other button keeps the plain look.
bool exerciseMessageBoxDefault() {
	QMessageBox box(QMessageBox::Question, QStringLiteral("Delete"), QStringLiteral("Delete the item?"),
		QMessageBox::Yes | QMessageBox::No);
	box.setDefaultButton(QMessageBox::No);
	auto* defaultButton = qobject_cast<QPushButton*>(box.button(QMessageBox::No));
	auto* otherButton = qobject_cast<QPushButton*>(box.button(QMessageBox::Yes));
	for (QPushButton* button : {defaultButton, otherButton}) {
		button->setText(QStringLiteral("MMMM MMMM"));
		button->setFocusPolicy(Qt::NoFocus);
		button->setFont(qApp->font());
		button->setFixedSize(150, 48);
	}
	box.ensurePolished();
	bool ok = check(defaultButton->isDefault() && !otherButton->isDefault(), "the message box does not make No the default button");
	const auto dialogDefault = dialogDefaultLooks();
	const ControlLook plain = controlLooks(ControlKind::PushButton, true, false).first;
	const ControlLook defaultLook = controlLook(defaultButton);
	ok &= checkLooks(sameLook(defaultLook, dialogDefault.first),
		"the default button of a message box does not look like the default button of a dialog", defaultLook, dialogDefault.first);
	ok &= checkLooks(sameLook(controlLook(otherButton), plain),
		"the other button of a message box does not keep the plain look", controlLook(otherButton), plain);
	defaultButton->setEnabled(false);
	const ControlLook disabledLook = controlLook(defaultButton);
	ok &= checkLooks(sameLook(disabledLook, dialogDefault.second),
		"a disabled default button of a message box does not look like a disabled default button of a dialog", disabledLook, dialogDefault.second);
	return ok;
}

// The width of the drawn text of a button, from the pixels that differ strongly from its face. The button is widened first,
// so that no text is clipped, and the grab is taken at its natural position within it. The rows within 6 px of the top
// and the bottom are left out, because the dialog shows through the rounded corners there.
int drawnTextWidth(QPushButton* button) {
	button->setFixedWidth(button->width() + 300);
	const QPixmap grab = button->grab();
	const QImage image = grab.toImage();
	const QRgb face = controlLook(button).face;
	const qreal ratio = grab.devicePixelRatio();
	int left = image.width();
	int right = -1;
	for (int y = qRound(6 * ratio); y < image.height() - qRound(6 * ratio); ++y)
		for (int x = qRound(3 * ratio); x < image.width() - qRound(3 * ratio); ++x)
			if (colorDifference(image.pixel(x, y), face) >= 200) {
				left = std::min(left, x);
				right = std::max(right, x);
			}
	return right < left ? 0 : qRound((right - left + 1) / ratio);
}

// A button shows its whole text when its width holds the text as it is drawn plus the border and the padding that the style
// sheet gives a button in a dialog (1 px and 14 px on each side). The drawn text can be bolder than the font of the widget.
bool buttonShowsText(QPushButton* button, const char* where, qreal scale) {
	const int width = button->width();
	const int needed = drawnTextWidth(button) + 2 * (1 + 14);
	const bool ok = width >= needed;
	if (!ok)
		std::cerr << where << ": the button '" << button->text().toStdString() << "' is " << width << " px wide at " << scale
				  << " times the font but its text needs " << needed << " px\n";
	return ok;
}

// The default button of an update prompt and of a dialog footer with long texts is not narrower than its text.
bool exerciseDefaultButtonWidth(qreal scale) {
	const QFont appFont = qApp->font();
	QFont font = appFont;
	font.setPointSizeF(12 * scale);
	QApplication::setFont(font);
	QMessageBox box(QMessageBox::Information, QStringLiteral("EGTRAIN 1.0.2 Is Available"), QStringLiteral("EGTRAIN 1.0.2 is available."));
	auto* update = box.addButton(QStringLiteral("Update and Restart"), QMessageBox::AcceptRole);
	box.addButton(QStringLiteral("Open Release Page"), QMessageBox::AcceptRole);
	box.addButton(QStringLiteral("Stop Checking"), QMessageBox::DestructiveRole);
	box.addButton(QStringLiteral("Later"), QMessageBox::RejectRole);
	box.setDefaultButton(update);
	for (QAbstractButton* button : box.buttons())
		button->setFocusPolicy(Qt::NoFocus);
	box.show();
	QApplication::processEvents();
	bool ok = check(update->isDefault(), "the update prompt does not make Update and Restart the default button");
	// QMessageBox caps its width from the screen width, so on a narrow screen with a wide font every button of a long row is
	// squeezed, the default one and the others alike. That cap is not what this check is about: it compares only when the
	// button row got the width it asks for.
	auto* row = box.findChild<QDialogButtonBox*>();
	if (row && row->width() < row->sizeHint().width()) {
		std::cerr << "note: at " << scale << " times the font the message box is narrower than its buttons ("
				  << row->width() << " of " << row->sizeHint().width() << " px); the update prompt is not compared\n";
	} else {
		for (QAbstractButton* button : box.buttons())
			ok &= buttonShowsText(qobject_cast<QPushButton*>(button), "update prompt", scale);
	}

	QDialog dialog;
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
	auto* primary = buttons->addButton(QStringLiteral("Run the whole simulation again"), QDialogButtonBox::AcceptRole);
	primary->setDefault(true);
	for (QAbstractButton* button : buttons->buttons())
		button->setFocusPolicy(Qt::NoFocus);
	DialogLayout::install(dialog, QStringLiteral("Default action"), QString(), new QWidget, buttons, QRect(0, 0, 1920, 1080));
	dialog.show();
	QApplication::processEvents();
	ok &= check(primary->isDefault(), "the dialog footer does not make its accept button the default button");
	ok &= buttonShowsText(primary, "dialog footer", scale);
	QApplication::setFont(appFont);
	return ok;
}

// Fusion with the icons that the GNOME and KDE themes give the standard buttons of a dialog footer.
class FooterIconStyle : public QProxyStyle {
public:
	FooterIconStyle() : QProxyStyle(QStringLiteral("Fusion")) {}
	int styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget, QStyleHintReturn* returnData) const override {
		return hint == SH_DialogButtonBox_ButtonsHaveIcons ? 1 : QProxyStyle::styleHint(hint, option, widget, returnData);
	}
};

// OK, Cancel and Close of a dialog footer and of a message box show no icon, also under a theme that gives them one.
bool exerciseFooterWithoutIcons() {
	FooterIconStyle style;
	QDialog dialog;
	dialog.setStyle(&style);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Close, &dialog);
	DialogLayout::install(dialog, QStringLiteral("Icons"), QString(), new QWidget, buttons, QRect(0, 0, 1280, 800));
	QList<QAbstractButton*> shown = buttons->buttons();
	QMessageBox box;
	box.setStyle(&style);
	box.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel | QMessageBox::Close);
	shown += box.buttons();
	bool ok = check(shown.size() >= 3, "the footers have no standard buttons");
	for (const QAbstractButton* button : shown)
		ok &= check(button->icon().isNull(), "a standard button of a dialog footer or a message box shows an icon");
	return ok;
}

// Fusion with the footer layout of one platform.
class FooterLayoutStyle : public QProxyStyle {
public:
	explicit FooterLayoutStyle(QDialogButtonBox::ButtonLayout layout) : QProxyStyle(QStringLiteral("Fusion")), m_layout(layout) {}
	int styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget, QStyleHintReturn* returnData) const override {
		return hint == SH_DialogButtonLayout ? m_layout : QProxyStyle::styleHint(hint, option, widget, returnData);
	}

private:
	QDialogButtonBox::ButtonLayout m_layout;
};

// Under the footer layout of each platform, Tab and Shift+Tab from the field of the body visit each footer button once, in
// the order the footer shows them, and then the field again. The buttons are added in another order than the footer shows
// them, as the case chooser and the run review add theirs. No stop is the body scroll area or another widget without a
// focus look.
bool exerciseFooterTabOrder() {
	bool ok = true;
	for (const auto layout : {QDialogButtonBox::WinLayout, QDialogButtonBox::MacLayout, QDialogButtonBox::KdeLayout, QDialogButtonBox::GnomeLayout}) {
		FooterLayoutStyle style(layout);
		QDialog dialog;
		dialog.setStyle(&style);
		auto* body = new QWidget;
		auto* field = new QLineEdit(body);
		(new QVBoxLayout(body))->addWidget(field);
		auto* buttons = new QDialogButtonBox(&dialog);
		QPushButton* cancel = buttons->addButton(QDialogButtonBox::Cancel);
		buttons->addButton(QDialogButtonBox::RestoreDefaults);
		QPushButton* accept = buttons->addButton(QDialogButtonBox::Ok);
		accept->setDefault(true);
		DialogLayout::install(dialog, QStringLiteral("Tab order"), QString(), body, buttons, QRect(0, 0, 1280, 800));
		dialog.show();
		dialog.activateWindow();
		QApplication::processEvents();
		QList<QWidget*> footer;
		for (int i = 0; i < buttons->layout()->count(); ++i)
			if (QWidget* button = buttons->layout()->itemAt(i)->widget())
				footer.append(button);
		const bool acceptFirst = layout == QDialogButtonBox::WinLayout || layout == QDialogButtonBox::KdeLayout;
		ok &= check(footer.size() == 3 && (footer.indexOf(accept) < footer.indexOf(cancel)) == acceptFirst,
			"the footer does not have the three buttons in the order of its layout");
		ok &= check(field->hasFocus(), "the field of the body does not have the first focus");
		for (const bool forward : {true, false}) {
			QList<QWidget*> expected = footer;
			if (!forward)
				std::reverse(expected.begin(), expected.end());
			expected.append(field);
			field->setFocus();
			QList<QWidget*> stops;
			while (stops.size() < expected.size() && dialog.focusWidget()) {
				QKeyEvent key(QEvent::KeyPress, forward ? Qt::Key_Tab : Qt::Key_Backtab, forward ? Qt::NoModifier : Qt::ShiftModifier);
				QApplication::sendEvent(dialog.focusWidget(), &key);
				stops.append(dialog.focusWidget());
			}
			if (stops != expected) {
				std::cerr << "footer layout " << static_cast<int>(layout) << (forward ? ": Tab" : ": Shift+Tab") << " from the field stops at";
				for (const QWidget* stop : stops) {
					const auto* button = qobject_cast<const QAbstractButton*>(stop);
					std::cerr << ' ' << (button ? button->text().toStdString() : std::string(stop ? stop->metaObject()->className() : "nothing"));
				}
				std::cerr << '\n';
				ok = false;
			}
		}
	}
	return ok;
}

// A spin box outside a dialog, such as one in a cell of the traction table of Rolling Stock Units, gets the height it
// asks for in a table cell, so that its text is not cut. It asks for 6 px more width and 2 px more height than a Fusion
// spin box at the offscreen font whatever its padding, so the columns of the traction table are a little wider than
// with Fusion; test_editor_smoke checks that the Rolling stock dock still holds its buttons. The spin box uses Fusion,
// so the widget style of the platform does not matter.
bool exerciseCellSpinBoxSize() {
	QProxyStyle fusion(QStringLiteral("Fusion"));
	QTableWidget table(1, 1);
	auto* spinBox = new QDoubleSpinBox;
	spinBox->setStyle(&fusion);
	table.setCellWidget(0, 0, spinBox);
	table.show();
	QApplication::processEvents();
	const int height = spinBox->sizeHint().height();
	const bool ok = height <= spinBox->height();
	if (!ok)
		std::cerr << "a spin box in a table asks for " << height << " px, its cell gives it " << spinBox->height() << " px\n";
	return ok;
}

// A button that opens a menu, such as Trains and Open result view..., keeps room beside its text for the arrow. Open result
// view... is as tall as the push buttons beside it in Run Results.
bool exerciseMenuButtonRoom() {
	QToolButton plain;
	plain.setText(QStringLiteral("Trains (2/2)"));
	bool ok = true;
	for (const char* name : {"", "runResultsViewsButton"}) {
		QToolButton menuButton;
		menuButton.setObjectName(QString::fromLatin1(name));
		menuButton.setText(plain.text());
		menuButton.setPopupMode(QToolButton::InstantPopup);
		menuButton.setMenu(new QMenu(&menuButton));
		ok &= check(menuButton.sizeHint().width() >= plain.sizeHint().width() + 12, "a menu button has no room for its arrow");
	}
	QToolButton views;
	views.setObjectName(QStringLiteral("runResultsViewsButton"));
	views.setText(QStringLiteral("Open result view..."));
	views.setPopupMode(QToolButton::InstantPopup);
	views.setMenu(new QMenu(&views));
	ok &= check(views.sizeHint().height() == QPushButton(QStringLiteral("Export CSV...")).sizeHint().height(),
		"Open result view... is not as tall as the push buttons beside it");
	return ok;
}

// The footer of the Run simulation review is built as the review builds it: the buttons exist and are named before the
// dialog is installed, the accept button is the default. Its face and text look like the default
// button of a dialog footer, whichever widget style the platform supplies.
bool exerciseRunReviewDefault() {
	QDialog review;
	review.setObjectName(QStringLiteral("runReviewDialog"));
	auto* buttons = new QDialogButtonBox(&review);
	buttons->setObjectName(QStringLiteral("runReviewButtons"));
	buttons->addButton(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("runReviewCancelButton"));
	auto* runButton = buttons->addButton(QStringLiteral("MMMM MMMM"), QDialogButtonBox::AcceptRole);
	runButton->setObjectName(QStringLiteral("runReviewRunButton"));
	runButton->setDefault(true);
	DialogLayout::install(review, QStringLiteral("Run simulation"), QString(), new QWidget, buttons, QRect(0, 0, 1920, 1080));
	review.show();
	QApplication::processEvents();
	runButton->setFixedSize(150, 48);
	QApplication::processEvents();
	const ControlLook reviewLook = controlLook(runButton);
	const ControlLook dialogDefault = dialogDefaultLooks().first;
	bool ok = check(runButton->isDefault(), "the Run simulation button is not the default button");
	ok &= checkLooks(sameLook(reviewLook, dialogDefault),
		"the Run simulation button does not look like the default button of a dialog", reviewLook, dialogDefault);
	// The run button shows its focus on its fill, as the dialog draws it. Cancel becomes the default while it has the focus,
	// but keeps the face of a plain button, so the footer never shows two default buttons.
	auto* cancel = buttons->button(QDialogButtonBox::Cancel);
	cancel->setFixedSize(150, 48);
	review.activateWindow();
	QApplication::processEvents();
	if (QWidget* focused = QApplication::focusWidget())
		focused->clearFocus();
	QApplication::processEvents();
	const auto drawnRunButton = [&review, runButton] {
		return review.grab(QRect(runButton->mapTo(&review, QPoint()), runButton->size())).toImage();
	};
	const QImage unfocused = drawnRunButton();
	runButton->setFocus();
	QApplication::processEvents();
	ok &= check(runButton->hasFocus() && drawnRunButton() != unfocused, "the focused Run simulation button looks the same as without focus");
	cancel->setFocus();
	QApplication::processEvents();
	ok &= check(cancel->hasFocus() && cancel->isDefault(), "Cancel does not take the focus and the default");
	const ControlLook cancelLook = controlLook(cancel);
	const ControlLook plainLook = controlLooks(ControlKind::PushButton, true, false).first;
	ok &= checkLooks(cancelLook.face == plainLook.face, "the focused Cancel button looks like a default button", cancelLook, plainLook);
	QMessageBox box(QMessageBox::Question, QStringLiteral("Delete"), QStringLiteral("Delete the item?"),
		QMessageBox::Yes | QMessageBox::No);
	box.setDefaultButton(QMessageBox::No);
	auto* boxDefault = qobject_cast<QPushButton*>(box.button(QMessageBox::No));
	boxDefault->setText(QStringLiteral("MMMM MMMM"));
	boxDefault->setFocusPolicy(Qt::NoFocus);
	boxDefault->setFont(qApp->font());
	boxDefault->setFixedSize(150, 48);
	box.ensurePolished();
	const ControlLook boxLook = controlLook(boxDefault);
	ok &= checkLooks(reviewLook.face == boxLook.face,
		"the Run simulation button does not have the face of the default button of a message box", reviewLook, boxLook);
	return ok;
}

// The width in pixels and the colour of the left border of widget, half way down.
std::pair<int, QRgb> leftBorder(QWidget* widget) {
	const QImage image = widget->grab().toImage();
	const int y = image.height() / 2;
	const QRgb color = image.pixel(0, y);
	int width = 0;
	while (width < image.width() && image.pixel(width, y) == color)
		++width;
	return {qRound(width / image.devicePixelRatio()), color};
}

// The Run details toggle of the run review, built as its dialog builds it, shows keyboard focus with a border as wide and in
// the same colour as a push button of the dialog, such as Loaded Data beside it.
bool exerciseDetailsToggleFocus(const QString& name, const QString& text) {
	QDialog review;
	auto* body = new QWidget;
	auto* layout = new QVBoxLayout(body);
	auto* toggle = new QToolButton(body);
	toggle->setText(text);
	toggle->setObjectName(name);
	toggle->setCheckable(true);
	toggle->setArrowType(Qt::RightArrow);
	toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	auto* button = new QPushButton(QStringLiteral("Loaded Data"), body);
	layout->addWidget(toggle);
	layout->addWidget(button);
	DialogLayout::install(review, QStringLiteral("Run simulation"), QString(), body, new QDialogButtonBox(QDialogButtonBox::Cancel),
		QRect(0, 0, 1280, 800));
	review.show();
	review.activateWindow();
	QApplication::processEvents();
	toggle->setFocus();
	QApplication::processEvents();
	const bool toggleFocused = toggle->hasFocus();
	const std::pair<int, QRgb> toggleBorder = leftBorder(toggle);
	button->setFocus();
	QApplication::processEvents();
	const std::pair<int, QRgb> buttonBorder = leftBorder(button);
	const bool ok = toggleFocused && button->hasFocus() && toggleBorder == buttonBorder && buttonBorder.first == 2;
	if (!ok)
		std::cerr << "the focused " << text.toStdString() << " toggle has a " << toggleBorder.first << " px border of " << std::hex << toggleBorder.second
				  << ", a focused dialog button a " << std::dec << buttonBorder.first << " px border of " << std::hex << buttonBorder.second
				  << std::dec << '\n';
	return ok;
}

// The Windows styles fill the page areas of a scroll bar, above and below the handle, with a dither of two colours unless
// the style sheet styles them. Under the classic Windows style, which every platform has and which draws that dither, the
// track of a vertical scroll bar is one plain colour, the band colour, on both sides of the handle. The native Windows
// styles are left out: on the offscreen platform of Windows the check crashed under the Windows 11 style.
bool exerciseScrollBarTrack() {
	std::unique_ptr<QStyle> style(QStyleFactory::create(QStringLiteral("Windows")));
	QScrollBar bar(Qt::Vertical);
	bar.setStyle(style.get());
	bar.setRange(0, 100);
	bar.setPageStep(20);
	bar.setValue(50);
	bar.resize(bar.sizeHint().width(), 200);
	bar.show();
	QApplication::processEvents();
	const QImage image = bar.grab().toImage();
	std::set<QRgb> track;
	for (int y = 0; y < image.height(); ++y)
		if (y < image.height() / 4 || y >= image.height() * 3 / 4)
			for (int x = 0; x < image.width(); ++x)
				track.insert(image.pixel(x, y));
	const QRgb handle = image.pixel(image.width() / 2, image.height() / 2);
	const bool ok = handle == qRgb(0x9a, 0xa5, 0xab) && track.size() == 1 && *track.begin() == qRgb(0xe3, 0xea, 0xee);
	if (!ok) {
		std::cerr << "the scroll bar under " << style->objectName().toStdString() << " has a handle of " << std::hex << handle
				  << " and a track of";
		for (const QRgb color : track)
			std::cerr << ' ' << color;
		std::cerr << std::dec << '\n';
	}
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
	const bool small = exercise(QRect(0, 0, 1280, 800), 1.0);
	const bool scaledSmall = exercise(QRect(0, 0, 1280, 800), 1.5);
	const bool large = exercise(QRect(0, 0, 1920, 1080), 1.5);
	const bool footerKeys = exerciseFooterKeys();
	const bool footerTabOrder = exerciseFooterTabOrder();
	const bool contentHeight = exerciseContentHeight(1.0) && exerciseContentHeight(1.5);
	const bool contextHeight = exerciseContextHeight(1.0) && exerciseContextHeight(1.5);
	const bool statusAfterOpen = exerciseStatusAfterOpen(1.0) && exerciseStatusAfterOpen(1.5);
	const bool fitHeight = exerciseFitHeight();
	const bool indicators = exerciseItemViewIndicators();
	const bool messages = exerciseInlineMessages();
	const bool panelMessages = exercisePanelMessages();
	const bool narrow = exerciseNarrowBody();
	const bool fit = exerciseFitAfterChange();
	const bool disabled = exerciseDisabledControls();
	const bool messageBox = exerciseMessageBoxDefault();
	const bool runReview = exerciseRunReviewDefault();
	const bool detailsToggle = exerciseDetailsToggleFocus(QStringLiteral("runReviewDetailsToggle"), QStringLiteral("Run details"));
	const bool menuButtons = exerciseMenuButtonRoom();
	const bool defaultWidth = exerciseDefaultButtonWidth(1.0);
	const bool scaledDefaultWidth = exerciseDefaultButtonWidth(1.5);
	const bool footerIcons = exerciseFooterWithoutIcons();
	const bool cellSpinBox = exerciseCellSpinBoxSize();
	const bool scrollBarTrack = exerciseScrollBarTrack();
	return small && scaledSmall && large && footerKeys && footerTabOrder && contentHeight && contextHeight && statusAfterOpen && fitHeight && indicators && messages && panelMessages && narrow && fit && disabled && messageBox && runReview && detailsToggle && menuButtons && defaultWidth
			&& scaledDefaultWidth && footerIcons && cellSpinBox && scrollBarTrack
		? 0
		: 1;
}
