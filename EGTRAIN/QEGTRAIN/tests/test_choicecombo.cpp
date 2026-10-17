#include "widgets/ChoiceComboBox.h"
#include "widgets/CompactDoubleSpinBox.h"
#include "widgets/DialogLayout.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QWheelEvent>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <iostream>

namespace {
bool check(bool condition, const char* message) {
	if (!condition)
		std::cerr << message << '\n';
	return condition;
}

QString choiceName(int length) {
	return QStringLiteral("Stop_abcdefghijklmnopqrstuvwxyz_0123456789_abcdefghijklmnopqrstuvwxyz_0123456789_"
						  "abcdefghijklmnopqrstuvwxyz")
		.left(length);
}

const QString kExplanation = QStringLiteral("Invalid: platform is not reachable on this route");

struct StopForm {
	QDialog dialog;
	ChoiceComboBox* station = nullptr;
	ChoiceComboBox* platform = nullptr;
	ChoiceComboBox* mode = nullptr;
	QScrollArea* scroll = nullptr;

	// Fills the combos the way the stop editor does: the platform list is rebuilt
	// with signals blocked, and an unknown platform becomes an "Invalid" item.
	StopForm(const QRect& screen, qreal scale, const QString& platformId) {
		QFont font = dialog.font();
		font.setPointSizeF(12 * scale);
		dialog.setFont(font);
		auto* body = new QWidget;
		auto* form = new QFormLayout(body);
		station = new ChoiceComboBox(&dialog);
		station->setObjectName(QStringLiteral("station"));
		platform = new ChoiceComboBox(&dialog);
		platform->setObjectName(QStringLiteral("platform"));
		mode = new ChoiceComboBox(&dialog);
		mode->setObjectName(QStringLiteral("mode"));
		form->addRow(QStringLiteral("Station"), station);
		form->addRow(QStringLiteral("Compatible platform"), platform);
		form->addRow(QStringLiteral("Planned time display"), mode);
		form->addRow(QStringLiteral("Arrival"), new QLineEdit(&dialog));

		station->addItem(choiceName(31), QStringLiteral("short"));
		station->addItem(choiceName(95), QStringLiteral("long"));
		station->setCurrentIndex(1);
		mode->addItem(QStringLiteral("Elapsed offsets (s)"), false);
		mode->addItem(QStringLiteral("Clock time"), true);
		{
			const QSignalBlocker blocker(platform);
			platform->clear();
			platform->addItem(QStringLiteral("(no platform)"), QString());
			platform->addItem(choiceName(31), QStringLiteral("p31"));
			platform->addItem(choiceName(95), QStringLiteral("p95"));
			if (platform->findData(platformId) < 0 && !platformId.isEmpty()) {
				platform->addItem(QStringLiteral("Invalid: %1").arg(platformId), platformId);
				platform->setItemData(platform->count() - 1, kExplanation, Qt::ToolTipRole);
			}
			platform->setCurrentIndex(std::max(0, platform->findData(platformId)));
		}

		auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
		scroll = DialogLayout::install(dialog, QStringLiteral("Edit timetable stop"),
			QStringLiteral("Set the station, platform and times."),
			body, buttons, screen);
		DialogLayout::fitWidthToContent(dialog);
		dialog.show();
		QApplication::processEvents();
	}

	QList<ChoiceComboBox*> combos() const { return {station, platform, mode}; }
};

// The text of the tip that the combo shows when the pointer rests on it.
QString shownTip(QComboBox* combo) {
	QHelpEvent help(QEvent::ToolTip, QPoint(4, 4), combo->mapToGlobal(QPoint(4, 4)));
	QApplication::sendEvent(combo, &help);
	const QString text = QToolTip::text();
	QToolTip::hideText();
	return text;
}

bool tooltipMatches(QComboBox* combo) {
	return shownTip(combo) == combo->currentText();
}

void press(QComboBox* combo, int key) {
	QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
	QApplication::sendEvent(combo, &event);
}

bool exerciseLayout(const QRect& screen, qreal scale) {
	StopForm f(screen, scale, choiceName(95));
	bool ok = check(f.dialog.width() <= screen.width() * 9 / 10
			&& f.dialog.height() <= screen.height() * 4 / 5,
		"dialog exceeds the size budget");
	ok &= check(f.platform->currentText().startsWith(QStringLiteral("Invalid: ")),
		"layout form does not hold the invalid platform item");
	ok &= check(!f.scroll->horizontalScrollBar()->isVisible()
			&& f.scroll->horizontalScrollBar()->maximum() == 0,
		"form needs a horizontal scroll bar");
	const QRect viewport = f.scroll->viewport()->rect();
	for (ChoiceComboBox* combo : f.combos()) {
		const QRect rect(combo->mapTo(f.scroll->viewport(), QPoint()), combo->size());
		ok &= check(viewport.contains(rect), "combo lies outside the scroll viewport");
		ok &= check(combo->width() >= combo->sizeHint().width()
				|| f.dialog.width() == f.dialog.maximumWidth(),
			"dialog stays narrower than its content although the budget allows more");
	}
	ok &= check(f.platform->minimumSizeHint().width() < f.platform->sizeHint().width() / 2,
		"long choice combo cannot shrink");

	const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
	for (ChoiceComboBox* combo : f.combos()) {
		int widest = 0;
		for (int i = 0; i < combo->count(); ++i)
			widest = std::max(widest, combo->view()->fontMetrics().horizontalAdvance(combo->itemText(i)));
		combo->showPopup();
		QApplication::processEvents();
		ok &= check(combo->view()->isVisible(), "popup did not open");
		ok &= check(combo->view()->width() >= std::min(widest, available.width() * 9 / 10),
			"popup is narrower than its widest item within the screen budget");
		ok &= check(available.contains(combo->view()->window()->frameGeometry()),
			"popup lies outside the available screen area");
		combo->hidePopup();
		QApplication::processEvents();
	}
	return ok;
}

bool exerciseSelection() {
	const QRect screen(0, 0, 1280, 800);
	StopForm f(screen, 1.0, QStringLiteral("p95"));
	bool ok = true;
	for (ChoiceComboBox* combo : f.combos())
		ok &= check(tooltipMatches(combo), "tooltip is not the current text");
	ok &= check(f.platform->currentText() == choiceName(95) && shownTip(f.platform) == choiceName(95),
		"tooltip is not the full text of a long item");

	f.platform->setCurrentIndex(1);
	press(f.platform, Qt::Key_Down);
	ok &= check(f.platform->currentIndex() == 2 && shownTip(f.platform) == choiceName(95),
		"Down did not select the next item or update the tooltip");
	press(f.platform, Qt::Key_Up);
	ok &= check(f.platform->currentIndex() == 1 && shownTip(f.platform) == choiceName(31),
		"Up did not select the previous item or update the tooltip");
	{
		const QSignalBlocker blocker(f.platform);
		f.platform->setCurrentIndex(0);
	}
	ok &= check(shownTip(f.platform) == QStringLiteral("(no platform)") && f.platform->toolTip().isEmpty(),
		"a selection made with signals blocked left an old tooltip");
	return ok;
}

bool exerciseElision() {
	// The size hint reserves the bounding width of the widest text. A large digit ends
	// in a wide bearing, so the advance of this text is more than its bounding width.
	ChoiceComboBox lone;
	QFont large = lone.font();
	large.setPointSizeF(48);
	lone.setFont(large);
	lone.addItem(QStringLiteral("abc"));
	lone.addItem(QStringLiteral("Platform 1"));
	lone.setCurrentIndex(1);
	lone.resize(lone.sizeHint());
	bool ok = check(lone.displayText() == lone.currentText(), "the widest text is elided in a field at its size hint");

	const QRect screen(0, 0, 1280, 800);
	StopForm f(screen, 1.0, QStringLiteral("p31"));
	ok &= check(f.platform->displayText() == choiceName(31), "a text that fits its field is elided");
	f.platform->setCurrentIndex(f.platform->findData(QStringLiteral("p95")));
	f.dialog.resize(420, f.dialog.height());
	QApplication::processEvents();
	const QString shown = f.platform->displayText();
	ok &= check(f.platform->width() < f.platform->sizeHint().width(), "the field did not shrink with the dialog");
	ok &= check(shown != choiceName(95) && shown.contains(QChar(0x2026))
			&& shown.startsWith(choiceName(3)) && shown.endsWith(choiceName(95).right(3)),
		"a text wider than its field is not elided in the middle");
	ok &= check(f.platform->fontMetrics().horizontalAdvance(shown) <= f.platform->width(),
		"the elided text is wider than the field");
	return ok;
}

bool exerciseInvalidChoice() {
	const QRect screen(0, 0, 1280, 800);
	const QString invalidId = choiceName(95);
	const QString shown = QStringLiteral("Invalid: %1").arg(invalidId);
	int firstIndex = -2;
	bool ok = true;
	for (int round = 0; round < 2; ++round) {
		StopForm f(screen, 1.0, invalidId);
		const int index = f.platform->currentIndex();
		if (round == 0)
			firstIndex = index;
		ok &= check(index >= 0 && index == firstIndex,
			"rebuilt dialog selects a different platform item");
		ok &= check(f.platform->currentText() == shown && f.platform->currentData().toString() == invalidId,
			"invalid platform lost its text or data");
		ok &= check(f.platform->itemData(index, Qt::ToolTipRole).toString() == kExplanation,
			"invalid platform item lost its explanation");
		ok &= check(shownTip(f.platform) == shown,
			"tooltip of the invalid item is not its full text");
		f.platform->showPopup();
		QApplication::processEvents();
		ok &= check(f.platform->currentText() == shown && f.platform->currentData().toString() == invalidId,
			"opening the popup changed the invalid selection");
		f.platform->hidePopup();
	}
	return ok;
}
// Turns the wheel one step down over target. The event goes to the widget under the pointer, which is the line edit
// of a spin box. A widget that ignores a wheel event passes it on to its parent, so the event goes up the parents
// until one accepts it.
void wheelDown(QWidget* target) {
	const QPoint center = target->rect().center();
	const QPoint global = target->mapToGlobal(center);
	QWidget* under = target->childAt(center);
	for (QWidget* receiver = under ? under : target; receiver; receiver = receiver->parentWidget()) {
		QWheelEvent wheel(QPointF(receiver->mapFromGlobal(global)), QPointF(global), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
			Qt::NoScrollPhase, false);
		QApplication::sendEvent(receiver, &wheel);
		if (wheel.isAccepted())
			break;
	}
	QApplication::processEvents();
}

// A tall form whose scroll area has room to scroll, with one field of each kind that offers a value to the wheel.
// A form that goes through DialogLayout::install has every field guarded by that call. A form in a plain scroll area
// guards only the fields that guard themselves, ChoiceComboBox and CompactDoubleSpinBox.
struct WheelForm {
	QDialog dialog;
	ChoiceComboBox* choice = nullptr;
	QComboBox* plain = nullptr;
	CompactDoubleSpinBox* compact = nullptr;
	QDoubleSpinBox* spin = nullptr;
	QScrollArea* scroll = nullptr;
	bool installed;

	explicit WheelForm(bool throughDialogLayout) : installed(throughDialogLayout) {
		auto* body = new QWidget;
		auto* form = new QFormLayout(body);
		choice = new ChoiceComboBox(body);
		plain = new QComboBox(body);
		for (const QString& entry : {QStringLiteral("first"), QStringLiteral("second"), QStringLiteral("third")}) {
			choice->addItem(entry);
			plain->addItem(entry);
		}
		compact = new CompactDoubleSpinBox(body);
		compact->setRange(0, 100);
		compact->setValue(50);
		spin = new QDoubleSpinBox(body);
		spin->setRange(0, 100);
		spin->setValue(50);
		form->addRow(QStringLiteral("Choice"), choice);
		form->addRow(QStringLiteral("Plain choice"), plain);
		form->addRow(QStringLiteral("Compact number"), compact);
		form->addRow(QStringLiteral("Number"), spin);
		for (int row = 0; row < 40; ++row)
			form->addRow(QStringLiteral("Line %1").arg(row), new QLineEdit(body));
		if (throughDialogLayout) {
			auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
			scroll = DialogLayout::install(dialog, QStringLiteral("Wheel"), QString(), body, buttons, QRect(0, 0, 1280, 600));
		} else {
			scroll = new QScrollArea;
			scroll->setWidgetResizable(true);
			scroll->setWidget(body);
			auto* layout = new QVBoxLayout(&dialog);
			layout->addWidget(scroll);
			dialog.resize(600, 400);
		}
		dialog.show();
		QApplication::setActiveWindow(&dialog);
		QApplication::processEvents();
	}

	// The fields with the values that a wheel step would change and that the form guards.
	QList<QWidget*> fields() const {
		if (installed)
			return {choice, plain, compact, spin};
		return {choice, compact};
	}
	QString value(QWidget* field) const {
		if (auto* combo = qobject_cast<QComboBox*>(field))
			return combo->currentText();
		return qobject_cast<QAbstractSpinBox*>(field)->text();
	}
};

// The wheel scrolls a form over a combo box or a spin box without changing it, until the field has keyboard focus.
bool exerciseWheel(bool throughDialogLayout) {
	WheelForm f(throughDialogLayout);
	const QString form = throughDialogLayout ? QStringLiteral("installed form: ") : QStringLiteral("plain scroll area: ");
	bool ok = check(f.scroll->verticalScrollBar()->maximum() > 0, qPrintable(form + "the wheel form does not scroll, so the test proves nothing"));
	for (QWidget* field : f.fields()) {
		const QString name = form + field->metaObject()->className();
		const QString before = f.value(field);
		f.scroll->verticalScrollBar()->setValue(0);
		wheelDown(field);
		ok &= check(f.value(field) == before, qPrintable(name + ": the wheel changed a field without focus"));
		ok &= check(f.scroll->verticalScrollBar()->value() > 0, qPrintable(name + ": the wheel over a field without focus did not scroll the form"));
	}
	for (QWidget* field : f.fields()) {
		const QString name = form + field->metaObject()->className();
		const QString before = f.value(field);
		field->setFocus();
		QApplication::processEvents();
		ok &= check(field->hasFocus(), qPrintable(name + ": the field did not take focus, so the test proves nothing"));
		f.scroll->verticalScrollBar()->setValue(0);
		wheelDown(field);
		ok &= check(f.value(field) != before, qPrintable(name + ": the wheel did not change a field with focus"));
		ok &= check(f.scroll->verticalScrollBar()->value() == 0, qPrintable(name + ": the wheel over a field with focus scrolled the form"));
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
	const bool small = exerciseLayout(QRect(0, 0, 1280, 800), 1.0);
	const bool scaledSmall = exerciseLayout(QRect(0, 0, 1280, 800), 1.5);
	const bool large = exerciseLayout(QRect(0, 0, 1920, 1080), 1.5);
	const bool selection = exerciseSelection();
	const bool elision = exerciseElision();
	const bool invalid = exerciseInvalidChoice();
	const bool wheelInstalled = exerciseWheel(true);
	const bool wheelPlain = exerciseWheel(false);
	return small && scaledSmall && large && selection && elision && invalid && wheelInstalled && wheelPlain ? 0 : 1;
}
