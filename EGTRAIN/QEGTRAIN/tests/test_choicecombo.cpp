#include "widgets/ChoiceComboBox.h"
#include "widgets/CompactDoubleSpinBox.h"
#include "widgets/DialogLayout.h"
#include "widgets/FocusWheel.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTest>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>

namespace {

QString choiceName(int length) {
	return QStringLiteral("Stop_abcdefghijklmnopqrstuvwxyz_0123456789_abcdefghijklmnopqrstuvwxyz_0123456789_"
						  "abcdefghijklmnopqrstuvwxyz")
		.left(length);
}

const QString kExplanation = QStringLiteral("The route does not use this platform.");

// A combo box that adjusts to its contents resizes itself 20 ms after its items change, which
// undoes the size the layout gave it, and the layout restores that size in the next turn of the
// event loop. Wait out the timer, then let the layout requests it queued run.
void settleLayout() {
	QElapsedTimer waited;
	waited.start();
	while (waited.elapsed() < 100)
		QApplication::processEvents(QEventLoop::AllEvents, 20);
	QApplication::processEvents();
}

struct StopForm {
	QDialog dialog;
	ChoiceComboBox* station = nullptr;
	ChoiceComboBox* platform = nullptr;
	ChoiceComboBox* mode = nullptr;
	QScrollArea* scroll = nullptr;

	// Fills the combos the way the stop editor does: the platform list is rebuilt
	// with signals blocked, and an unknown platform gets an item marked "(not on the route)".
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
		form->addRow(new QCheckBox(QStringLiteral("Planned arrival"), &dialog), new QLineEdit(&dialog));

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
				platform->addItem(QStringLiteral("%1 (not on the route)").arg(platformId), platformId);
				platform->setItemData(platform->count() - 1, kExplanation, Qt::ToolTipRole);
			}
			platform->setCurrentIndex(std::max(0, platform->findData(platformId)));
		}

		auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
		scroll = DialogLayout::install(dialog, QStringLiteral("Edit Timetable Stop"), QString(), body, buttons, screen);
		DialogLayout::fitWidthToContent(dialog);
		dialog.show();
		settleLayout();
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
// guards only the fields that guard themselves, ChoiceComboBox and CompactDoubleSpinBox. A watched form has every field
// guarded, also the ones that appear after the watch, as in an editor dock.
enum class FormKind { Installed,
	Plain,
	Watched };

struct WheelForm {
	QDialog dialog;
	ChoiceComboBox* choice = nullptr;
	QComboBox* plain = nullptr;
	CompactDoubleSpinBox* compact = nullptr;
	QDoubleSpinBox* spin = nullptr;
	QSlider* slider = nullptr;
	QComboBox* lateCombo = nullptr;
	QSpinBox* lateSpin = nullptr;
	QScrollArea* scroll = nullptr;
	FormKind kind;

	explicit WheelForm(FormKind formKind) : kind(formKind) {
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
		slider = new QSlider(Qt::Horizontal, body);
		slider->setRange(0, 100);
		slider->setValue(50);
		form->addRow(QStringLiteral("Choice"), choice);
		form->addRow(QStringLiteral("Plain choice"), plain);
		form->addRow(QStringLiteral("Compact number"), compact);
		form->addRow(QStringLiteral("Number"), spin);
		form->addRow(QStringLiteral("Slider"), slider);
		for (int row = 0; row < 40; ++row)
			form->addRow(QStringLiteral("Line %1").arg(row), new QLineEdit(body));
		if (kind == FormKind::Installed) {
			auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
			scroll = DialogLayout::install(dialog, QStringLiteral("Wheel"), QString(), body, buttons, QRect(0, 0, 1280, 600));
		} else {
			scroll = new QScrollArea;
			scroll->setWidgetResizable(true);
			scroll->setWidget(body);
			auto* layout = new QVBoxLayout(&dialog);
			layout->addWidget(scroll);
			dialog.resize(600, 400);
			if (kind == FormKind::Watched)
				FocusWheel::watch(body);
		}
		dialog.show();
		dialog.activateWindow();
		QApplication::processEvents();
		if (kind == FormKind::Watched) {
			// Fields inside a widget that is added later, the way a table or a tab page adds its editors.
			auto* page = new QWidget(body);
			auto* pageLayout = new QFormLayout(page);
			lateCombo = new QComboBox(page);
			lateCombo->addItems({QStringLiteral("one"), QStringLiteral("two"), QStringLiteral("three")});
			lateSpin = new QSpinBox(page);
			lateSpin->setRange(0, 100);
			lateSpin->setValue(50);
			pageLayout->addRow(QStringLiteral("Late choice"), lateCombo);
			pageLayout->addRow(QStringLiteral("Late number"), lateSpin);
			form->insertRow(0, page);
			page->show();
			QApplication::processEvents();
		}
	}

	// The fields with the values that a wheel step would change and that the form guards.
	QList<QWidget*> fields() const {
		if (kind == FormKind::Installed)
			return {choice, plain, compact, spin, slider};
		if (kind == FormKind::Watched)
			return {choice, plain, compact, spin, slider, lateCombo, lateSpin};
		return {choice, compact};
	}
	QString value(QWidget* field) const {
		if (auto* combo = qobject_cast<QComboBox*>(field))
			return combo->currentText();
		if (auto* bar = qobject_cast<QSlider*>(field))
			return QString::number(bar->value());
		return qobject_cast<QAbstractSpinBox*>(field)->text();
	}
};

} // namespace

// The choice combo box of the stop editor and the dialogs fits its form, shows its whole text as a tooltip, elides a
// long text in the middle, keeps a choice that is not on the route, and leaves the wheel to the form until it has focus.
class TestChoiceCombo : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void layout_data();
	void layout();
	void selection();
	void elision();
	void invalidChoice();
	void itemTip();
	void popupHeight();
	void wheel_data();
	void wheel();
};

void TestChoiceCombo::initTestCase() {
	QFile qss(QStringLiteral(EGTRAIN_DIALOG_QSS));
	QVERIFY2(qss.open(QIODevice::ReadOnly), "cannot load application QSS");
	// The test binary has no Qt resources, so point the stylesheet at the icon files.
	QString styleSheet = QString::fromUtf8(qss.readAll());
	styleSheet.replace(QStringLiteral(":/icons/"),
		QFileInfo(qss).absolutePath() + QStringLiteral("/../icons/"));
	qApp->setStyleSheet(styleSheet);
}

void TestChoiceCombo::layout_data() {
	QTest::addColumn<QRect>("screen");
	QTest::addColumn<qreal>("scale");
	QTest::newRow("1280x800 at 1.0") << QRect(0, 0, 1280, 800) << qreal(1.0);
	QTest::newRow("1280x800 at 1.5") << QRect(0, 0, 1280, 800) << qreal(1.5);
	QTest::newRow("1920x1080 at 1.5") << QRect(0, 0, 1920, 1080) << qreal(1.5);
}

void TestChoiceCombo::layout() {
	QFETCH(QRect, screen);
	QFETCH(qreal, scale);
	StopForm f(screen, scale, choiceName(95));
	// The dialog stays within the size budget.
	QCOMPARE_LE(f.dialog.width(), screen.width() * 9 / 10);
	QCOMPARE_LE(f.dialog.height(), screen.height() * 4 / 5);
	QVERIFY2(f.platform->currentText().endsWith(QStringLiteral(" (not on the route)")),
		"layout form does not hold the invalid platform item");
	// The form needs no horizontal scroll bar.
	QVERIFY(!f.scroll->horizontalScrollBar()->isVisible());
	QCOMPARE(f.scroll->horizontalScrollBar()->maximum(), 0);
	const QRect viewport = f.scroll->viewport()->rect();
	for (ChoiceComboBox* combo : f.combos()) {
		const QRect rect(combo->mapTo(f.scroll->viewport(), QPoint()), combo->size());
		QVERIFY2(viewport.contains(rect), "combo lies outside the scroll viewport");
		QVERIFY2(combo->width() >= combo->sizeHint().width()
				|| f.dialog.width() == f.dialog.maximumWidth(),
			"dialog stays narrower than its content although the budget allows more");
	}
	// A long choice combo can shrink.
	QCOMPARE_LT(f.platform->minimumSizeHint().width(), f.platform->sizeHint().width() / 2);

	const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
	for (ChoiceComboBox* combo : f.combos()) {
		int widest = 0;
		for (int i = 0; i < combo->count(); ++i)
			widest = std::max(widest, combo->view()->fontMetrics().horizontalAdvance(combo->itemText(i)));
		combo->showPopup();
		QApplication::processEvents();
		QVERIFY2(combo->view()->isVisible(), "popup did not open");
		// The popup is as wide as its widest item within the screen budget.
		QCOMPARE_GE(combo->view()->width(), std::min(widest, available.width() * 9 / 10));
		QVERIFY2(available.contains(combo->view()->window()->frameGeometry()),
			"popup lies outside the available screen area");
		combo->hidePopup();
		QApplication::processEvents();
	}
}

void TestChoiceCombo::selection() {
	const QRect screen(0, 0, 1280, 800);
	StopForm f(screen, 1.0, QStringLiteral("p95"));
	for (ChoiceComboBox* combo : f.combos())
		QVERIFY2(tooltipMatches(combo), "tooltip is not the current text");
	// The tooltip is the full text of a long item.
	QCOMPARE(f.platform->currentText(), choiceName(95));
	QCOMPARE(shownTip(f.platform), choiceName(95));

	f.platform->setCurrentIndex(1);
	press(f.platform, Qt::Key_Down);
	// Down selects the next item and updates the tooltip.
	QCOMPARE(f.platform->currentIndex(), 2);
	QCOMPARE(shownTip(f.platform), choiceName(95));
	press(f.platform, Qt::Key_Up);
	// Up selects the previous item and updates the tooltip.
	QCOMPARE(f.platform->currentIndex(), 1);
	QCOMPARE(shownTip(f.platform), choiceName(31));
	{
		const QSignalBlocker blocker(f.platform);
		f.platform->setCurrentIndex(0);
	}
	// A selection made with signals blocked leaves no old tooltip.
	QCOMPARE(shownTip(f.platform), QStringLiteral("(no platform)"));
	QVERIFY(f.platform->toolTip().isEmpty());
}

void TestChoiceCombo::elision() {
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
	// The widest text is not elided in a field at its size hint.
	QCOMPARE(lone.displayText(), lone.currentText());

	const QRect screen(0, 0, 1280, 800);
	StopForm f(screen, 1.0, QStringLiteral("p31"));
	// A text that fits its field is not elided.
	QCOMPARE(f.platform->displayText(), choiceName(31));
	f.platform->setCurrentIndex(f.platform->findData(QStringLiteral("p95")));
	f.dialog.resize(420, f.dialog.height());
	QApplication::processEvents();
	const QString shown = f.platform->displayText();
	// The field shrinks with the dialog.
	QCOMPARE_LT(f.platform->width(), f.platform->sizeHint().width());
	// A text wider than its field is elided in the middle.
	QCOMPARE_NE(shown, choiceName(95));
	QVERIFY(shown.contains(QChar(0x2026)));
	QVERIFY(shown.startsWith(choiceName(3)));
	QVERIFY(shown.endsWith(choiceName(95).right(3)));
	// The elided text is not wider than the field.
	QCOMPARE_LE(f.platform->fontMetrics().horizontalAdvance(shown), f.platform->width());
}

// A platform that is not on the route keeps its item, text, data and explanation, also in a rebuilt dialog.
void TestChoiceCombo::invalidChoice() {
	const QRect screen(0, 0, 1280, 800);
	const QString invalidId = choiceName(95);
	const QString shown = QStringLiteral("%1 (not on the route)").arg(invalidId);
	int firstIndex = -2;
	for (int round = 0; round < 2; ++round) {
		StopForm f(screen, 1.0, invalidId);
		const int index = f.platform->currentIndex();
		if (round == 0)
			firstIndex = index;
		// The rebuilt dialog selects the same platform item.
		QCOMPARE_GE(index, 0);
		QCOMPARE(index, firstIndex);
		// The invalid platform keeps its text and data.
		QCOMPARE(f.platform->currentText(), shown);
		QCOMPARE(f.platform->currentData().toString(), invalidId);
		// The invalid platform item keeps its explanation.
		QCOMPARE(f.platform->itemData(index, Qt::ToolTipRole).toString(), kExplanation);
		// The tooltip of the invalid item is its full text followed by its explanation.
		QCOMPARE(shownTip(f.platform), shown + QLatin1Char('\n') + kExplanation);
		f.platform->showPopup();
		QApplication::processEvents();
		// Opening the popup does not change the invalid selection.
		QCOMPARE(f.platform->currentText(), shown);
		QCOMPARE(f.platform->currentData().toString(), invalidId);
		f.platform->hidePopup();
	}
}

// The tip shows the current text, then the tooltip of the current item (the id behind a label), then the tooltip of the
// field, and a line that is already shown only once.
void TestChoiceCombo::itemTip() {
	ChoiceComboBox combo;
	combo.addItem(QStringLiteral("B0 0.000 to 0.230 km"), QStringLiteral("@0-B0@"));
	combo.setItemData(0, QStringLiteral("@0-B0@"), Qt::ToolTipRole);
	combo.addItem(QStringLiteral("(none)"));
	combo.addItem(QStringLiteral("route0"));
	combo.setItemData(2, QStringLiteral("The route of the scope."), Qt::ToolTipRole);
	combo.setToolTip(QStringLiteral("The route of the scope."));
	combo.show();
	QApplication::processEvents();
	// The tip shows the text, the id of the item and the tip of the field.
	QCOMPARE(shownTip(&combo), QStringLiteral("B0 0.000 to 0.230 km\n@0-B0@\nThe route of the scope."));
	combo.setCurrentIndex(1);
	// An item without a tooltip shows the text and the tip of the field.
	QCOMPARE(shownTip(&combo), QStringLiteral("(none)\nThe route of the scope."));
	combo.setCurrentIndex(2);
	// A tooltip that the item and the field share is shown once.
	QCOMPARE(shownTip(&combo), QStringLiteral("route0\nThe route of the scope."));
}

// A field with focus opens a list that shows each of its items whole, without scrolling.
void TestChoiceCombo::popupHeight() {
	StopForm f(QRect(0, 0, 1280, 800), 1.0, QStringLiteral("p31"));
	f.mode->setFocus();
	QApplication::processEvents();
	QVERIFY2(f.mode->hasFocus(), "the planned time display did not take focus, so the test proves nothing");
	f.mode->showPopup();
	QApplication::processEvents();
	QAbstractItemView* list = f.mode->view();
	// A list that cuts its last item can scroll, so a list that cannot scroll shows every item whole.
	const bool whole = list->isVisible() && list->verticalScrollBar()->maximum() == 0;
	QString problem;
	if (!whole) {
		int rows = 0;
		for (int row = 0; row < f.mode->count(); ++row)
			rows += list->visualRect(f.mode->model()->index(row, 0)).height();
		problem = QStringLiteral("the list of a field with focus scrolls: the popup is %1 px, its list %2 px for %3 px of items, and it scrolls by %4")
					  .arg(list->window()->height())
					  .arg(list->viewport()->height())
					  .arg(rows)
					  .arg(list->verticalScrollBar()->maximum());
	}
	f.mode->hidePopup();
	QVERIFY2(whole, qPrintable(problem));
}

void TestChoiceCombo::wheel_data() {
	QTest::addColumn<int>("kind");
	QTest::newRow("installed form") << static_cast<int>(FormKind::Installed);
	QTest::newRow("plain scroll area") << static_cast<int>(FormKind::Plain);
	QTest::newRow("watched form") << static_cast<int>(FormKind::Watched);
}

// The wheel scrolls a form over a combo box, spin box or slider without changing it, until the field has keyboard focus.
void TestChoiceCombo::wheel() {
	QFETCH(int, kind);
	WheelForm f(static_cast<FormKind>(kind));
	QVERIFY2(f.scroll->verticalScrollBar()->maximum() > 0, "the wheel form does not scroll, so the test proves nothing");
	// An installed form opens with the focus on its first field.
	if (QWidget* focused = QApplication::focusWidget())
		focused->clearFocus();
	for (QWidget* field : f.fields()) {
		const QString name = QString::fromLatin1(field->metaObject()->className());
		const QString before = f.value(field);
		f.scroll->verticalScrollBar()->setValue(0);
		wheelDown(field);
		QVERIFY2(f.value(field) == before, qPrintable(name + ": the wheel changed a field without focus"));
		QVERIFY2(f.scroll->verticalScrollBar()->value() > 0, qPrintable(name + ": the wheel over a field without focus did not scroll the form"));
	}
	for (QWidget* field : f.fields()) {
		const QString name = QString::fromLatin1(field->metaObject()->className());
		const QString before = f.value(field);
		field->setFocus();
		QApplication::processEvents();
		QVERIFY2(field->hasFocus(), qPrintable(name + ": the field did not take focus, so the test proves nothing"));
		f.scroll->verticalScrollBar()->setValue(0);
		wheelDown(field);
		QVERIFY2(f.value(field) != before, qPrintable(name + ": the wheel did not change a field with focus"));
		QVERIFY2(f.scroll->verticalScrollBar()->value() == 0, qPrintable(name + ": the wheel over a field with focus scrolled the form"));
	}
}

QTEST_MAIN(TestChoiceCombo)
#include "test_choicecombo.moc"
