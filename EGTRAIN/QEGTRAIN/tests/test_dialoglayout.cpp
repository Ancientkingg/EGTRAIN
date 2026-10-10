#include "widgets/DialogLayout.h"

#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QToolBar>
#include <QToolButton>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QImage>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollBar>
#include <QTableWidget>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
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
	auto* heading = dialog.findChild<QLabel*>(QStringLiteral("dialogHeading"));
	auto* context = dialog.findChild<QLabel*>(QStringLiteral("dialogContext"));
	ok &= check(
		heading && heading->font().pointSizeF() > font.pointSizeF() && heading->textFormat() == Qt::PlainText && context
			&& context->textFormat() == Qt::PlainText && context->parentWidget() == scroll->widget(),
		"heading scales, authored labels remain plain and context scrolls");
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
	const ControlLook disabledDefault = controlLook(primary);
	ok &= checkLooks(qGray(disabledDefault.face) >= qGray(enabledDefault.face) + 30,
		"a disabled default button keeps its lighter face", enabledDefault, disabledDefault);
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
	const bool indicators = exerciseItemViewIndicators();
	const bool messages = exerciseInlineMessages();
	const bool narrow = exerciseNarrowBody();
	const bool disabled = exerciseDisabledControls();
	return small && scaledSmall && large && indicators && messages && narrow && disabled ? 0 : 1;
}
