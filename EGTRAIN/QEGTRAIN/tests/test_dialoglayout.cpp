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
#include <QScopeGuard>
#include <QScrollBar>
#include <QStyleFactory>
#include <QTableWidget>
#include <QTest>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <utility>

namespace {
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

void checkIndicators(const QImage& image, const QRect& unchecked, const QRect& alternate,
	const QRect& selected, const QRect& checked, const char* surface) {
	const Indicator plain = measureIndicator(image, unchecked);
	const Indicator striped = measureIndicator(image, alternate);
	const Indicator highlighted = measureIndicator(image, selected);
	const Indicator ticked = measureIndicator(image, checked);
	QVERIFY2(plain.maxDifference >= 90, surface);
	QVERIFY2(striped.maxDifference >= 90, surface);
	QVERIFY2(highlighted.maxDifference >= 90, surface);
	QVERIFY2(ticked.strongPixels >= 2 * plain.strongPixels, surface);
	QVERIFY2(plain.strongPixels > 0, surface);
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

// The background of a widget as the pixel 6 px inside its right edge at half its height. The text
// of the labels that use it is short and left aligned, so that point is away from text, border and
// rounded corners. The whole value is returned, alpha included: a transparent background may grab as
// transparent.
QRgb sampleBackground(QWidget* widget) {
	const QImage image = widget->grab().toImage();
	return image.pixel(image.width() - 1 - 6, image.height() / 2);
}

// An editor panel with a plain label and three message labels that get the error, warning and help role before the panel
// is shown, and three that get them after.
struct PanelMessages {
	QWidget panel;
	QLabel* plain = nullptr;
	QLabel* early[3] = {};
	QLabel* late[3] = {};

	PanelMessages() {
		using DialogLayout::Severity;
		auto* fields = new QVBoxLayout(&panel);
		auto addLabel = [&]() {
			auto* label = new QLabel(&panel);
			fields->addWidget(label);
			return label;
		};
		plain = addLabel();
		plain->setText(QStringLiteral("Plain label"));
		for (QLabel*& label : early)
			label = addLabel();
		for (QLabel*& label : late)
			label = addLabel();
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
	}
};

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

// The message of a check that compares two looks: what failed, then the face and ink of both.
QString looksMessage(const QString& message, const ControlLook& left, const ControlLook& right) {
	auto describe = [](const ControlLook& look) {
		return QStringLiteral("face=%1 ink=%2").arg(look.face, 8, 16, QLatin1Char('0')).arg(look.ink, 8, 16, QLatin1Char('0'));
	};
	return message + QStringLiteral(": ") + describe(left) + QStringLiteral(" versus ") + describe(right);
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
// The result says why a button does not show its whole text, and is empty when it does.
QString clippedText(QPushButton* button, const char* where, qreal scale) {
	const int width = button->width();
	const int needed = drawnTextWidth(button) + 2 * (1 + 14);
	if (width >= needed)
		return QString();
	return QStringLiteral("%1: the button '%2' is %3 px wide at %4 times the font but its text needs %5 px")
		.arg(QLatin1String(where), button->text())
		.arg(width)
		.arg(scale)
		.arg(needed);
}

// Fusion with the icons that the GNOME and KDE themes give the standard buttons of a dialog footer.
class FooterIconStyle : public QProxyStyle {
public:
	FooterIconStyle() : QProxyStyle(QStringLiteral("Fusion")) {}
	int styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget, QStyleHintReturn* returnData) const override {
		return hint == SH_DialogButtonBox_ButtonsHaveIcons ? 1 : QProxyStyle::styleHint(hint, option, widget, returnData);
	}
};

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
} // namespace

// The common dialog layout keeps a dialog on the screen with its footer reachable, and its message labels, controls and
// footer buttons look the same in every dialog. The application style sheet is loaded for every check.
class TestDialogLayout : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void longDialogStaysOnScreen_data();
	void longDialogStaysOnScreen();
	void footerKeysPressFooterButtons();
	void footerTabOrder_data();
	void footerTabOrder();
	void shortBodyOpensAtContentHeight_data();
	void shortBodyOpensAtContentHeight();
	void tallBodyOpensAtHeightLimit_data();
	void tallBodyOpensAtHeightLimit();
	void contextLineIsAsTallAsText_data();
	void contextLineIsAsTallAsText();
	void messageAfterOpen_data();
	void messageAfterOpen();
	void fitWidthToContentGrowsHeight();
	void itemViewIndicatorsAreVisible();
	void inlineMessagesTakeTheirRole();
	void panelMessagesLookAsSetAfterShow_data();
	void panelMessagesLookAsSetAfterShow();
	void panelMessageRolesHaveThreeColours();
	void narrowBodyWrapsMessages();
	void fitWidthAfterChange();
	void disabledControlLooks_data();
	void disabledControlLooks();
	void disabledReadOnlyLineEditKeepsLook();
	void disabledCheckedToolButtonKeepsLook();
	void disabledDefaultButtonKeepsLighterFace();
	void disabledMenuEntryHasLighterText();
	void messageBoxDefaultLooksLikeDialogDefault();
	void runReviewDefaultLooksLikeDialogDefault();
	void detailsToggleShowsFocusLikeButton();
	void menuButtonsHaveRoomForArrow();
	void defaultButtonShowsText_data();
	void defaultButtonShowsText();
	void footerButtonsShowNoIcons();
	void cellSpinBoxGetsItsHeight();
	void scrollBarTrackIsPlain();
};

void TestDialogLayout::initTestCase() {
	QFile qss(QStringLiteral(EGTRAIN_DIALOG_QSS));
	QVERIFY2(qss.open(QIODevice::ReadOnly), "cannot load application QSS");
	// The test binary has no Qt resources, so point the stylesheet at the icon files.
	QString styleSheet = QString::fromUtf8(qss.readAll());
	styleSheet.replace(QStringLiteral(":/icons/"),
		QFileInfo(qss).absolutePath() + QStringLiteral("/../icons/"));
	qApp->setStyleSheet(styleSheet);
}

void TestDialogLayout::longDialogStaysOnScreen_data() {
	QTest::addColumn<QRect>("screen");
	QTest::addColumn<qreal>("scale");
	QTest::newRow("1280x800 at 1.0") << QRect(0, 0, 1280, 800) << qreal(1.0);
	QTest::newRow("1280x800 at 1.5") << QRect(0, 0, 1280, 800) << qreal(1.5);
	QTest::newRow("1920x1080 at 1.5") << QRect(0, 0, 1920, 1080) << qreal(1.5);
}

// A dialog with a long heading, a long context and a long body stays within the screen budget, scrolls its body and keeps
// its footer reachable by keyboard.
void TestDialogLayout::longDialogStaysOnScreen() {
	QFETCH(QRect, screen);
	QFETCH(qreal, scale);
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
	// The dialog stays within the available screen budget.
	QCOMPARE_LE(dialog.width(), screen.width() * 9 / 10);
	QCOMPARE_LE(dialog.height(), screen.height() * 4 / 5);
	// Long content scrolls.
	QCOMPARE_GT(scroll->verticalScrollBar()->maximum(), 0);
	scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
	// The footer remains reachable outside the scrolling body.
	QVERIFY(buttons->isVisible());
	QCOMPARE_LE(buttons->geometry().bottom(), dialog.height());
	QCOMPARE_LE(scroll->geometry().bottom(), buttons->geometry().top());
	auto* context = dialog.findChild<QLabel*>(QStringLiteral("dialogContext"));
	// The heading is the window title and is not repeated in the dialog.
	QCOMPARE(dialog.windowTitle(), QStringLiteral("Review <simulation> & ") + longName.left(120));
	QVERIFY(!dialog.findChild<QLabel*>(QStringLiteral("dialogHeading")));
	// The context remains plain text and scrolls.
	if (context == nullptr)
		QFAIL("the context remains plain text and scrolls");
	QCOMPARE(context->textFormat(), Qt::PlainText);
	QCOMPARE(context->parentWidget(), scroll->widget());
	buttons->button(QDialogButtonBox::Ok)->setFocus();
	scroll->verticalScrollBar()->setValue(0);
	QKeyEvent backtab(QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
	QApplication::sendEvent(buttons->button(QDialogButtonBox::Ok), &backtab);
	QApplication::processEvents();
	const QRect fieldRect(lastField->mapTo(scroll->viewport(), QPoint()), lastField->size());
	// Backtab from the footer reveals the focused body field.
	QVERIFY(lastField->hasFocus());
	QVERIFY(scroll->viewport()->rect().contains(fieldRect));
	QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
	QApplication::sendEvent(lastField, &tab);
	QVERIFY2(buttons->button(QDialogButtonBox::Ok)->hasFocus() || buttons->button(QDialogButtonBox::Cancel)->hasFocus(),
		"Tab from last body field reaches fixed footer");
	buttons->button(QDialogButtonBox::Ok)->setFocus();
	QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
	QApplication::sendEvent(buttons->button(QDialogButtonBox::Ok), &enter);
	// Enter on the default button accepts.
	QCOMPARE(dialog.result(), int(QDialog::Accepted));

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
	// Escape rejects without accepting.
	QCOMPARE(cancelDialog.result(), int(QDialog::Rejected));
	QVERIFY(!cancelDialog.isVisible());
}

// Without a field in the body the default button has the focus. Tab moves it to the next footer button and Space presses
// that button.
void TestDialogLayout::footerKeysPressFooterButtons() {
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
	// OK has the focus and the default.
	QVERIFY(okButton->hasFocus());
	QVERIFY(okButton->isDefault());
	QVERIFY(!cancelButton->isDefault());
	QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
	QApplication::sendEvent(okButton, &tab);
	QVERIFY2(cancelButton->hasFocus(), "Tab does not move from OK to Cancel");
	QKeyEvent spacePress(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "));
	QKeyEvent spaceRelease(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "));
	QApplication::sendEvent(cancelButton, &spacePress);
	QApplication::sendEvent(cancelButton, &spaceRelease);
	// Space presses the focused Cancel.
	QCOMPARE(dialog.result(), int(QDialog::Rejected));
	QVERIFY(!dialog.isVisible());
}

void TestDialogLayout::footerTabOrder_data() {
	QTest::addColumn<int>("layout");
	QTest::newRow("Windows") << int(QDialogButtonBox::WinLayout);
	QTest::newRow("macOS") << int(QDialogButtonBox::MacLayout);
	QTest::newRow("KDE") << int(QDialogButtonBox::KdeLayout);
	QTest::newRow("GNOME") << int(QDialogButtonBox::GnomeLayout);
}

// Under the footer layout of each platform, Tab and Shift+Tab from the field of the body visit each footer button once, in
// the order the footer shows them, and then the field again. The buttons are added in another order than the footer shows
// them, as the case chooser and the run review add theirs. No stop is the body scroll area or another widget without a
// focus look.
void TestDialogLayout::footerTabOrder() {
	QFETCH(int, layout);
	FooterLayoutStyle style(static_cast<QDialogButtonBox::ButtonLayout>(layout));
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
	// The footer has the three buttons in the order of its layout.
	QCOMPARE(footer.size(), 3);
	QCOMPARE(footer.indexOf(accept) < footer.indexOf(cancel), acceptFirst);
	QVERIFY2(field->hasFocus(), "the field of the body does not have the first focus");
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
		QString message = QStringLiteral("footer layout %1: %2 from the field stops at").arg(layout).arg(forward ? QStringLiteral("Tab") : QStringLiteral("Shift+Tab"));
		for (const QWidget* stop : stops) {
			const auto* button = qobject_cast<const QAbstractButton*>(stop);
			message += QLatin1Char(' ') + (button ? button->text() : QString::fromLatin1(stop ? stop->metaObject()->className() : "nothing"));
		}
		QVERIFY2(stops == expected, qPrintable(message));
	}
}

void TestDialogLayout::shortBodyOpensAtContentHeight_data() {
	QTest::addColumn<qreal>("scale");
	QTest::addColumn<int>("bodyHeight");
	for (const qreal scale : {1.0, 1.5})
		for (const int bodyHeight : {20, 500})
			QTest::addRow("%d px at %.1f", bodyHeight, scale) << scale << bodyHeight;
}

// A dialog opens at the height its content needs, up to the height limit, and scrolls only beyond it.
void TestDialogLayout::shortBodyOpensAtContentHeight() {
	QFETCH(qreal, scale);
	QFETCH(int, bodyHeight);
	const QRect screen(0, 0, 1920, 1080);
	Opened fits;
	openDialog(fits, screen, scale, bodyHeight, QStringLiteral("Context"));
	// A body shorter than the height limit opens without a vertical scroll bar.
	QCOMPARE(fits.scroll->verticalScrollBar()->maximum(), 0);
	QVERIFY(!fits.scroll->verticalScrollBar()->isVisible());
	// A body shorter than the height limit opens below the height limit.
	QCOMPARE_LT(fits.dialog.height(), fits.limit);
	// A dialog with a short body opens no taller than its content needs.
	if (bodyHeight == 20)
		QCOMPARE_LT(fits.dialog.height(), 16 * QFontMetrics(fits.dialog.font()).height());
	// The footer of a dialog that fits is visible.
	QVERIFY(fits.buttons->isVisible());
	QCOMPARE_LE(fits.buttons->geometry().bottom(), fits.dialog.height());
}

void TestDialogLayout::tallBodyOpensAtHeightLimit_data() {
	QTest::addColumn<qreal>("scale");
	QTest::newRow("1.0") << qreal(1.0);
	QTest::newRow("1.5") << qreal(1.5);
}

void TestDialogLayout::tallBodyOpensAtHeightLimit() {
	QFETCH(qreal, scale);
	Opened tall;
	openDialog(tall, QRect(0, 0, 1920, 1080), scale, 3000, QStringLiteral("Context"));
	// A body taller than the height limit scrolls.
	QCOMPARE_GT(tall.scroll->verticalScrollBar()->maximum(), 0);
	// A body taller than the height limit opens at the height limit.
	QCOMPARE(tall.dialog.height(), tall.limit);
	// The footer of a dialog that scrolls is visible below the body.
	QVERIFY(tall.buttons->isVisible());
	QCOMPARE_LE(tall.scroll->geometry().bottom(), tall.buttons->geometry().top());
	QCOMPARE_LE(tall.buttons->geometry().bottom(), tall.dialog.height());
}

void TestDialogLayout::contextLineIsAsTallAsText_data() {
	QTest::addColumn<qreal>("scale");
	QTest::addColumn<QString>("context");
	const QString longContext = QStringLiteral("Netherlands / baseline with a long context that wraps onto more than one line. ").repeated(6);
	for (const qreal scale : {1.0, 1.5}) {
		QTest::addRow("short at %.1f", scale) << scale << QStringLiteral("Netherlands / baseline");
		QTest::addRow("long at %.1f", scale) << scale << longContext;
	}
}

// The context line is as tall as its text, also when the dialog is taller than its content.
void TestDialogLayout::contextLineIsAsTallAsText() {
	QFETCH(qreal, scale);
	QFETCH(QString, context);
	Opened opened;
	openDialog(opened, QRect(0, 0, 1920, 1080), scale, 20, context);
	auto* label = opened.dialog.findChild<QLabel*>(QStringLiteral("dialogContext"));
	if (label == nullptr)
		QFAIL("the dialog has no context line");
	opened.dialog.resize(opened.dialog.width(), opened.dialog.height() + 200);
	QApplication::processEvents();
	// The context line is no taller than its text: the dialog gives it no space it does not use.
	QCOMPARE(label->height(), label->heightForWidth(label->width()));
}

void TestDialogLayout::messageAfterOpen_data() {
	QTest::addColumn<qreal>("scale");
	QTest::addColumn<int>("bodyHeight");
	for (const qreal scale : {1.0, 1.5})
		for (const int bodyHeight : {400, 3000})
			QTest::addRow("%d px at %.1f", bodyHeight, scale) << scale << bodyHeight;
}

// A message shown in a shown dialog makes the dialog taller, up to the height limit, instead of scrolling the body.
void TestDialogLayout::messageAfterOpen() {
	QFETCH(qreal, scale);
	QFETCH(int, bodyHeight);
	const QRect screen(0, 0, 1920, 1080);
	const QString longText = QStringLiteral("The planned time is before the previous stop, so the stop would be out of order. ").repeated(3);
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
		// A message shown in a shown dialog makes it taller.
		QCOMPARE_GT(dialog.height(), before);
		// A message shown in a shown dialog that has room for it does not scroll the body.
		QCOMPARE(scroll->verticalScrollBar()->maximum(), 0);
		QVERIFY(message->isVisible());
	} else {
		// A message in a dialog at the height limit does not change its height.
		QCOMPARE(dialog.height(), limit);
		QCOMPARE(before, limit);
	}
	// The footer is visible after a message is shown.
	QCOMPARE_LE(buttons->geometry().bottom(), dialog.height());
}

// fitWidthToContent makes a shown dialog taller for a body that has grown, up to the height limit, and never shorter.
void TestDialogLayout::fitWidthToContentGrowsHeight() {
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
	// fitWidthToContent makes the dialog taller for a taller body.
	QCOMPARE_GT(grown, initial);
	QCOMPARE_LE(grown, limit);
	// A dialog made taller by fitWidthToContent does not scroll its body.
	QCOMPARE(scroll->verticalScrollBar()->maximum(), 0);
	filler->setFixedHeight(100);
	DialogLayout::fitWidthToContent(dialog);
	QApplication::processEvents();
	// fitWidthToContent does not shorten the dialog.
	QCOMPARE(dialog.height(), grown);
	filler->setFixedHeight(3000);
	DialogLayout::fitWidthToContent(dialog);
	QApplication::processEvents();
	// fitWidthToContent does not grow the dialog beyond the height limit.
	QCOMPARE(dialog.height(), limit);
}

void TestDialogLayout::itemViewIndicatorsAreVisible() {
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
	checkIndicators(listImage, listRow(0), listRow(1), listRow(3), listRow(2), "list item indicators are not clearly visible");
	if (QTest::currentTestFailed())
		return;
	checkIndicators(tableImage, tableRow(0), tableRow(1), tableRow(3), tableRow(2), "table item indicators are not clearly visible");
}

void TestDialogLayout::inlineMessagesTakeTheirRole() {
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
	// The reference backgrounds are pairwise different, or the stylesheet is not loaded or a role does not change the look of
	// a label.
	QCOMPARE_NE(neutralBackground, warningBackground);
	QCOMPARE_NE(neutralBackground, errorBackground);
	QCOMPARE_NE(warningBackground, errorBackground);
	QCOMPARE_NE(plainBackground, neutralBackground);
	QCOMPARE_NE(plainBackground, warningBackground);
	QCOMPARE_NE(plainBackground, errorBackground);

	// The subject was shown as a plain label, so every call below changes a polished label.
	setStatus(Severity::Error, QStringLiteral("Subject error"));
	// setStatus(Error) after show gives the error callout background.
	QCOMPARE(sampleBackground(subject), errorBackground);
	// setStatus(Error) after show no longer looks like the neutral callout.
	QCOMPARE_NE(sampleBackground(subject), neutralBackground);
	setStatus(Severity::Neutral, QStringLiteral("Subject neutral"));
	// setStatus(Neutral) after the error callout gives the neutral background.
	QCOMPARE(sampleBackground(subject), neutralBackground);
	setStatus(Severity::Warning, QStringLiteral("Subject warning"));
	// setStatus(Warning) after show gives the warning callout background.
	QCOMPARE(sampleBackground(subject), warningBackground);

	// A label has one role: help, status, help, status replace each other.
	setHelp(QStringLiteral("Subject help"));
	QVERIFY2(hasHelpOnly(), "setHelp does not set dialogHelp and remove dialogStatus");
	// setHelp after a status gives the help background.
	QCOMPARE(sampleBackground(subject), helpBackground);
	QCOMPARE_NE(helpBackground, neutralBackground);
	QCOMPARE_NE(helpBackground, warningBackground);
	QCOMPARE_NE(helpBackground, errorBackground);
	const QColor helpColor = help->palette().color(QPalette::WindowText);
	// setHelp gives the text colour of the help rule.
	QCOMPARE(subject->palette().color(QPalette::WindowText), helpColor);
	QCOMPARE_NE(helpColor, plain->palette().color(QPalette::WindowText));
	setStatus(Severity::Error, QStringLiteral("Subject error"));
	QVERIFY2(hasStatusOnly("error"), "setStatus(Error) does not set dialogStatus to error and remove dialogHelp");
	// setStatus(Error) after help gives the error callout background.
	QCOMPARE(sampleBackground(subject), errorBackground);
	setHelp(QStringLiteral("Subject help"));
	QVERIFY2(hasHelpOnly(), "setHelp after a status does not remove dialogStatus");
	// setHelp after the error callout gives the help background.
	QCOMPARE(sampleBackground(subject), helpBackground);
	setStatus(Severity::Warning, QStringLiteral("Subject warning"));
	QVERIFY2(hasStatusOnly("warning"), "setStatus(Warning) does not set dialogStatus to warning and remove dialogHelp");
	// setStatus(Warning) after help gives the warning callout background.
	QCOMPARE(sampleBackground(subject), warningBackground);
	setStatus(Severity::Neutral, QStringLiteral("Subject neutral"));
	QVERIFY2(hasStatusOnly("neutral"), "setStatus(Neutral) does not set dialogStatus to neutral and remove dialogHelp");

	// An empty text hides the label, a text shows it, and markup stays literal.
	setStatus(Severity::Error, QString());
	// setStatus with an empty text hides the label.
	QVERIFY(subject->isHidden());
	QVERIFY(!subject->isVisible());
	setStatus(Severity::Warning, QStringLiteral("<b>x</b>"));
	QVERIFY2(subject->isVisible(), "setStatus with a text does not show a hidden label");
	// setStatus shows the text it was given, as plain text.
	QCOMPARE(subject->text(), QStringLiteral("<b>x</b>"));
	QCOMPARE(subject->textFormat(), Qt::PlainText);
	setHelp(QString());
	// setHelp with an empty text hides the label.
	QVERIFY(subject->isHidden());
	QVERIFY(!subject->isVisible());
	setHelp(QStringLiteral("<b>x</b>"));
	QVERIFY2(subject->isVisible(), "setHelp with a text does not show a hidden label");
	// setHelp shows the text it was given, as plain text.
	QCOMPARE(subject->text(), QStringLiteral("<b>x</b>"));
	QCOMPARE(subject->textFormat(), Qt::PlainText);
}

void TestDialogLayout::panelMessagesLookAsSetAfterShow_data() {
	QTest::addColumn<int>("role");
	QTest::newRow("error") << 0;
	QTest::newRow("warning") << 1;
	QTest::newRow("help") << 2;
}

// An editor panel gives its message labels their role while it is hidden. Labels set before the panel is
// shown look the same as labels set afterwards, and the text colour of every role differs from a plain label.
void TestDialogLayout::panelMessagesLookAsSetAfterShow() {
	QFETCH(int, role);
	const PanelMessages messages;
	// A message label of a panel is shown with its panel.
	QVERIFY(messages.early[role]->isVisible());
	QVERIFY(messages.late[role]->isVisible());
	// A role given before the panel is shown looks as the same role given after.
	QCOMPARE(sampleBackground(messages.early[role]), sampleBackground(messages.late[role]));
	QCOMPARE(messages.early[role]->palette().color(QPalette::WindowText), messages.late[role]->palette().color(QPalette::WindowText));
	// A role of a message label changes the text colour of a plain label.
	QCOMPARE_NE(messages.early[role]->palette().color(QPalette::WindowText), messages.plain->palette().color(QPalette::WindowText));
}

void TestDialogLayout::panelMessageRolesHaveThreeColours() {
	const PanelMessages messages;
	const QColor error = messages.early[0]->palette().color(QPalette::WindowText);
	const QColor warning = messages.early[1]->palette().color(QPalette::WindowText);
	const QColor help = messages.early[2]->palette().color(QPalette::WindowText);
	// The error, warning and help roles have three text colours.
	QCOMPARE_NE(error, warning);
	QCOMPARE_NE(error, help);
	QCOMPARE_NE(warning, help);
}

// A plain widget, not a dialog, as an editor panel is. Its labels hold a word wider than the widget
// and a text that needs many lines.
void TestDialogLayout::narrowBodyWrapsMessages() {
	QWidget body;
	auto* fields = new QVBoxLayout(&body);
	auto* longWord = new QLabel(&body);
	fields->addWidget(longWord);
	auto* manyWords = new QLabel(&body);
	fields->addWidget(manyWords);
	fields->addStretch();
	const QString word(400, QLatin1Char('W'));
	const QString words = QStringLiteral("word ").repeated(60);
	// The long word is wider than the body, or the test proves nothing.
	QCOMPARE_GT(longWord->fontMetrics().horizontalAdvance(word), 320);
	// The long text needs more than three lines, or the test proves nothing.
	QCOMPARE_GT(manyWords->fontMetrics().horizontalAdvance(words), 960);
	DialogLayout::setStatus(longWord, DialogLayout::Severity::Error, word);
	DialogLayout::setStatus(manyWords, DialogLayout::Severity::Warning, words);
	// The body is tall enough for the wrapped text with any font, and the stretch keeps the labels at their own height.
	body.resize(320, 1000);
	body.show();
	QApplication::processEvents();

	// A label with a word wider than the body does not widen the body.
	QCOMPARE(body.width(), 320);
	QCOMPARE_LE(longWord->width(), 320);
	// A label with a long text is not wider than the body.
	QCOMPARE_LE(manyWords->width(), 320);
	// A label with a long text wraps.
	QCOMPARE_GE(manyWords->height(), 3 * manyWords->fontMetrics().lineSpacing());
	// A label with a long text is as tall as its wrapped text.
	QCOMPARE_GE(manyWords->height(), manyWords->heightForWidth(manyWords->width()));
}

// A shown dialog whose body gets a wider choice: fitWidthToContent widens the dialog for it at once, not only after the
// queued layout requests have run.
void TestDialogLayout::fitWidthAfterChange() {
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
	// The wide choice needs a wider dialog within its limit, or the test proves nothing.
	QCOMPARE_GT(needed, before);
	QCOMPARE_LT(needed, dialog.maximumWidth());
	choice->addItem(wide);
	choice->setCurrentIndex(1);
	DialogLayout::fitWidthToContent(dialog);
	// A choice added to a shown dialog widens it.
	QCOMPARE_GE(dialog.width(), needed);
}

void TestDialogLayout::disabledControlLooks_data() {
	QTest::addColumn<int>("kind");
	QTest::newRow("push button") << int(ControlKind::PushButton);
	QTest::newRow("combo box") << int(ControlKind::ComboBox);
	QTest::newRow("line edit") << int(ControlKind::LineEdit);
	QTest::newRow("tool button") << int(ControlKind::ToolButton);
	QTest::newRow("check box") << int(ControlKind::CheckBox);
}

// A disabled control has lighter text, and looks the same outside a dialog, inside one and when only its parent is disabled.
void TestDialogLayout::disabledControlLooks() {
	QFETCH(int, kind);
	const auto control = static_cast<ControlKind>(kind);
	const auto plain = controlLooks(control, false, false);
	const auto dialog = controlLooks(control, true, false);
	const auto parent = controlLooks(control, false, true);
	const QString name = QString::fromLatin1(QTest::currentDataTag());
	QVERIFY2(colorDifference(plain.first.face, plain.first.ink) >= 90,
		qPrintable(looksMessage("an enabled " + name + " draws text distinct from its face", plain.first, plain.second)));
	QVERIFY2(qGray(plain.second.ink) >= qGray(plain.first.ink) + 30,
		qPrintable(looksMessage("a disabled " + name + " has lighter text", plain.first, plain.second)));
	const bool box = control == ControlKind::PushButton || control == ControlKind::ComboBox || control == ControlKind::LineEdit;
	if (box) {
		QVERIFY2(plain.first.face != plain.second.face,
			qPrintable(looksMessage("a disabled " + name + " has a different face", plain.first, plain.second)));
		const QString sameInDialog = looksMessage("a disabled " + name + " looks the same inside and outside a dialog", plain.second, dialog.second);
		QVERIFY2(sameLook(plain.second, dialog.second), qPrintable(sameInDialog));
		QVERIFY2(qGray(plain.second.ink) >= qGray(plain.first.ink) + 30, qPrintable(sameInDialog));
	}
	const QString sameAsDisabled = looksMessage("a " + name + " disabled only because its parent is disabled looks like a disabled one",
		parent.second, plain.second);
	QVERIFY2(sameLook(parent.second, plain.second), qPrintable(sameAsDisabled));
	QVERIFY2(qGray(parent.second.ink) >= qGray(parent.first.ink) + 30, qPrintable(sameAsDisabled));
}

void TestDialogLayout::disabledReadOnlyLineEditKeepsLook() {
	const auto readOnly = controlLooks(ControlKind::ReadOnly, false, false);
	QVERIFY2(sameLook(readOnly.first, readOnly.second),
		qPrintable(looksMessage("a disabled read-only line edit keeps its read-only look", readOnly.first, readOnly.second)));
}

void TestDialogLayout::disabledCheckedToolButtonKeepsLook() {
	const auto checked = controlLooks(ControlKind::CheckedToolButton, false, false);
	QVERIFY2(sameLook(checked.first, checked.second),
		qPrintable(looksMessage("a disabled checked tool button keeps its checked look", checked.first, checked.second)));
}

void TestDialogLayout::disabledDefaultButtonKeepsLighterFace() {
	const auto defaults = dialogDefaultLooks();
	QVERIFY2(qGray(defaults.second.face) >= qGray(defaults.first.face) + 30,
		qPrintable(looksMessage("a disabled default button keeps its lighter face", defaults.first, defaults.second)));
}

// A menu with an enabled and a disabled entry: the disabled text is lighter.
void TestDialogLayout::disabledMenuEntryHasLighterText() {
	QMenu menu;
	QAction* live = menu.addAction(QStringLiteral("MMMM MMMM"));
	QAction* gone = menu.addAction(QStringLiteral("MMMM MMMM"));
	gone->setEnabled(false);
	menu.show();
	QApplication::processEvents();
	const ControlLook enabled = menuRowLook(menu, live);
	const ControlLook disabled = menuRowLook(menu, gone);
	menu.hide();
	QVERIFY2(colorDifference(enabled.face, enabled.ink) >= 90,
		qPrintable(looksMessage("an enabled menu entry draws text distinct from its face", enabled, disabled)));
	QVERIFY2(qGray(disabled.ink) >= qGray(enabled.ink) + 30, qPrintable(looksMessage("a disabled menu entry has lighter text", enabled, disabled)));
}

// A question box with Yes and No, No the default, built and styled without running its event loop. Its default button
// looks like the default button of a dialog footer and the other button keeps the plain look.
void TestDialogLayout::messageBoxDefaultLooksLikeDialogDefault() {
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
	// The message box makes No the default button.
	QVERIFY(defaultButton->isDefault());
	QVERIFY(!otherButton->isDefault());
	const auto dialogDefault = dialogDefaultLooks();
	const ControlLook plain = controlLooks(ControlKind::PushButton, true, false).first;
	const ControlLook defaultLook = controlLook(defaultButton);
	QVERIFY2(sameLook(defaultLook, dialogDefault.first),
		qPrintable(looksMessage("the default button of a message box does not look like the default button of a dialog", defaultLook,
			dialogDefault.first)));
	const ControlLook otherLook = controlLook(otherButton);
	QVERIFY2(sameLook(otherLook, plain),
		qPrintable(looksMessage("the other button of a message box does not keep the plain look", otherLook, plain)));
	defaultButton->setEnabled(false);
	const ControlLook disabledLook = controlLook(defaultButton);
	QVERIFY2(sameLook(disabledLook, dialogDefault.second),
		qPrintable(looksMessage("a disabled default button of a message box does not look like a disabled default button of a dialog",
			disabledLook, dialogDefault.second)));
}

// The footer of the Run simulation review is built as the review builds it: the buttons exist and are named before the
// dialog is installed, the accept button is the default. Its face and text look like the default
// button of a dialog footer, whichever widget style the platform supplies.
void TestDialogLayout::runReviewDefaultLooksLikeDialogDefault() {
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
	QVERIFY2(runButton->isDefault(), "the Run simulation button is not the default button");
	QVERIFY2(sameLook(reviewLook, dialogDefault),
		qPrintable(looksMessage("the Run simulation button does not look like the default button of a dialog", reviewLook, dialogDefault)));
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
	// The focused Run simulation button looks different from the one without focus.
	QVERIFY(runButton->hasFocus());
	QVERIFY(drawnRunButton() != unfocused);
	cancel->setFocus();
	QApplication::processEvents();
	// Cancel takes the focus and the default.
	QVERIFY(cancel->hasFocus());
	QVERIFY(cancel->isDefault());
	const ControlLook cancelLook = controlLook(cancel);
	const ControlLook plainLook = controlLooks(ControlKind::PushButton, true, false).first;
	QVERIFY2(cancelLook.face == plainLook.face, qPrintable(looksMessage("the focused Cancel button looks like a default button", cancelLook, plainLook)));
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
	QVERIFY2(reviewLook.face == boxLook.face,
		qPrintable(looksMessage("the Run simulation button does not have the face of the default button of a message box", reviewLook, boxLook)));
}

// The Run details toggle of the run review, built as its dialog builds it, shows keyboard focus with a border as wide and in
// the same colour as a push button of the dialog, such as Loaded Data beside it.
void TestDialogLayout::detailsToggleShowsFocusLikeButton() {
	const QString text = QStringLiteral("Run details");
	QDialog review;
	auto* body = new QWidget;
	auto* layout = new QVBoxLayout(body);
	auto* toggle = new QToolButton(body);
	toggle->setText(text);
	toggle->setObjectName(QStringLiteral("runReviewDetailsToggle"));
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
	const QString message = QStringLiteral("the focused %1 toggle has a %2 px border of %3, a focused dialog button a %4 px border of %5")
								.arg(text)
								.arg(toggleBorder.first)
								.arg(toggleBorder.second, 0, 16)
								.arg(buttonBorder.first)
								.arg(buttonBorder.second, 0, 16);
	QVERIFY2(toggleFocused, qPrintable(message));
	QVERIFY2(button->hasFocus(), qPrintable(message));
	QVERIFY2(toggleBorder == buttonBorder, qPrintable(message));
	QVERIFY2(buttonBorder.first == 2, qPrintable(message));
}

// A button that opens a menu, such as Trains and Open result view..., keeps room beside its text for the arrow. Open result
// view... is as tall as the push buttons beside it in Run Results.
void TestDialogLayout::menuButtonsHaveRoomForArrow() {
	QToolButton plain;
	plain.setText(QStringLiteral("Trains (2/2)"));
	for (const char* name : {"", "runResultsViewsButton"}) {
		QToolButton menuButton;
		menuButton.setObjectName(QString::fromLatin1(name));
		menuButton.setText(plain.text());
		menuButton.setPopupMode(QToolButton::InstantPopup);
		menuButton.setMenu(new QMenu(&menuButton));
		// A menu button has room for its arrow.
		QCOMPARE_GE(menuButton.sizeHint().width(), plain.sizeHint().width() + 12);
	}
	QToolButton views;
	views.setObjectName(QStringLiteral("runResultsViewsButton"));
	views.setText(QStringLiteral("Open result view..."));
	views.setPopupMode(QToolButton::InstantPopup);
	views.setMenu(new QMenu(&views));
	// Open result view... is as tall as the push buttons beside it.
	QCOMPARE(views.sizeHint().height(), QPushButton(QStringLiteral("Export CSV...")).sizeHint().height());
}

void TestDialogLayout::defaultButtonShowsText_data() {
	QTest::addColumn<qreal>("scale");
	QTest::newRow("1.0") << qreal(1.0);
	QTest::newRow("1.5") << qreal(1.5);
}

// The default button of an update prompt and of a dialog footer with long texts is not narrower than its text.
void TestDialogLayout::defaultButtonShowsText() {
	QFETCH(qreal, scale);
	const QFont appFont = qApp->font();
	const auto restoreFont = qScopeGuard([appFont] { QApplication::setFont(appFont); });
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
	QVERIFY2(update->isDefault(), "the update prompt does not make Update and Restart the default button");
	// QMessageBox caps its width from the screen width, so on a narrow screen with a wide font every button of a long row is
	// squeezed, the default one and the others alike. That cap is not what this check is about: it compares only when the
	// button row got the width it asks for.
	auto* row = box.findChild<QDialogButtonBox*>();
	if (row && row->width() < row->sizeHint().width()) {
		qInfo("at %g times the font the message box is narrower than its buttons (%d of %d px); the update prompt is not compared",
			scale, row->width(), row->sizeHint().width());
	} else {
		for (QAbstractButton* button : box.buttons()) {
			const QString problem = clippedText(qobject_cast<QPushButton*>(button), "update prompt", scale);
			QVERIFY2(problem.isEmpty(), qPrintable(problem));
		}
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
	QVERIFY2(primary->isDefault(), "the dialog footer does not make its accept button the default button");
	const QString problem = clippedText(primary, "dialog footer", scale);
	QVERIFY2(problem.isEmpty(), qPrintable(problem));
}

// OK, Cancel and Close of a dialog footer and of a message box show no icon, also under a theme that gives them one.
void TestDialogLayout::footerButtonsShowNoIcons() {
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
	// The footers have standard buttons.
	QCOMPARE_GE(shown.size(), 3);
	for (const QAbstractButton* button : shown)
		QVERIFY2(button->icon().isNull(), "a standard button of a dialog footer or a message box shows an icon");
}

// A spin box outside a dialog, such as one in a cell of the traction table of Rolling Stock Units, gets the height it
// asks for in a table cell, so that its text is not cut. It asks for 6 px more width and 2 px more height than a Fusion
// spin box at the offscreen font whatever its padding, so the columns of the traction table are a little wider than
// with Fusion; test_editor_smoke checks that the Rolling stock dock still holds its buttons. The spin box uses Fusion,
// so the widget style of the platform does not matter.
void TestDialogLayout::cellSpinBoxGetsItsHeight() {
	QProxyStyle fusion(QStringLiteral("Fusion"));
	QTableWidget table(1, 1);
	auto* spinBox = new QDoubleSpinBox;
	spinBox->setStyle(&fusion);
	table.setCellWidget(0, 0, spinBox);
	table.show();
	QApplication::processEvents();
	// The height a spin box in a table asks for fits the height its cell gives it.
	QCOMPARE_LE(spinBox->sizeHint().height(), spinBox->height());
}

// The Windows styles fill the page areas of a scroll bar, above and below the handle, with a dither of two colours unless
// the style sheet styles them. Under the classic Windows style, which every platform has and which draws that dither, the
// track of a vertical scroll bar is one plain colour, the band colour, on both sides of the handle. The native Windows
// styles are left out: on the offscreen platform of Windows the check crashed under the Windows 11 style.
void TestDialogLayout::scrollBarTrackIsPlain() {
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
	QString message = QStringLiteral("the scroll bar under %1 has a handle of %2 and a track of").arg(style->objectName()).arg(handle, 0, 16);
	for (const QRgb color : track)
		message += QLatin1Char(' ') + QString::number(color, 16);
	QVERIFY2(handle == qRgb(0x9a, 0xa5, 0xab), qPrintable(message));
	QVERIFY2(track.size() == 1, qPrintable(message));
	QVERIFY2(*track.begin() == qRgb(0xe3, 0xea, 0xee), qPrintable(message));
}

QTEST_MAIN(TestDialogLayout)
#include "test_dialoglayout.moc"
