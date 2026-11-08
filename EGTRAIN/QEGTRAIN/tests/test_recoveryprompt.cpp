#include "recovery/RecoveryPrompt.h"

#include <QApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QProxyStyle>
#include <QPushButton>
#include <QRadioButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QTest>

namespace {

QDateTime writtenTime() {
	return QDateTime(QDate(2026, 10, 14), QTime(9, 5)).toUTC();
}

RecoveryCandidate makeCandidate(const QString& name, RecoverySourceKind kind, const QString& path, RecoverySourceStatus status,
	RecoveryCandidateState state = RecoveryCandidateState::Openable) {
	RecoveryCandidate candidate;
	candidate.instanceId = name;
	candidate.state = state;
	candidate.source.kind = kind;
	candidate.source.path = path;
	candidate.source.displayName = name;
	candidate.writtenAt = writtenTime();
	candidate.sourceStatus = status;
	return candidate;
}

// Eight rows: one for each source status, and the two copies that this version cannot open.
RecoveryScan makeScan() {
	using Kind = RecoverySourceKind;
	using Status = RecoverySourceStatus;
	using State = RecoveryCandidateState;
	RecoveryScan scan;
	auto add = [&scan](const char* name, Kind kind, const char* file, Status status, State state = State::Openable) {
		const QString path = *file ? QStringLiteral("/home/test/cases/") + QString::fromLatin1(file) : QString();
		scan.candidates.append(makeCandidate(QString::fromLatin1(name), kind, path, status, state));
	};
	add("New case study", Kind::Untitled, "", Status::NotApplicable);
	add("Line 7", Kind::Bundle, "Line 7.egscene", Status::Changed);
	add("Old depot", Kind::Directory, "old_depot", Status::Missing);
	add("Newer file", Kind::Bundle, "Newer.egscene", Status::NewerThanCopy);
	add("Same file", Kind::Bundle, "Same.egscene", Status::Unchanged);
	add("Unknown file", Kind::Bundle, "Unknown.egscene", Status::Unknown);
	add("Future copy", Kind::Bundle, "Future.egscene", Status::Unchanged, State::NewerRecordVersion);
	add("Old format", Kind::Bundle, "Format.egscene", Status::Unchanged, State::OtherSceneVersion);
	return scan;
}

// A case without a name and with an unknown time after a case whose name holds an ampersand.
RecoveryScan makeTextScan() {
	RecoveryScan single;
	single.candidates.append(makeCandidate(QStringLiteral("A & B"), RecoverySourceKind::Untitled, QString(), RecoverySourceStatus::NotApplicable));
	single.candidates.append(makeCandidate(QString(), RecoverySourceKind::Bundle, QString(), RecoverySourceStatus::NotApplicable));
	single.candidates[1].writtenAt = QDateTime();
	single.removedOverCap = 3;
	return single;
}

struct Parts {
	QVector<QFrame*> rows;
	QVector<QRadioButton*> selectors;
	QPushButton* recover = nullptr;
	QPushButton* discard = nullptr;
	QPushButton* later = nullptr;
	QLabel* confirmation = nullptr;
};

Parts partsOf(RecoveryPrompt& dialog) {
	Parts parts;
	for (QFrame* row : dialog.findChildren<QFrame*>(QStringLiteral("recoveryRow"))) {
		parts.rows.append(row);
		parts.selectors.append(row->findChild<QRadioButton*>(QStringLiteral("recoverySelect")));
	}
	parts.recover = dialog.findChild<QPushButton*>(QStringLiteral("recoveryRecoverButton"));
	parts.discard = dialog.findChild<QPushButton*>(QStringLiteral("recoveryDiscardButton"));
	parts.later = dialog.findChild<QPushButton*>(QStringLiteral("recoveryLaterButton"));
	parts.confirmation = dialog.findChild<QLabel*>(QStringLiteral("recoveryConfirmation"));
	return parts;
}

QString textOf(const QFrame* row, const char* name) {
	const auto* label = row->findChild<QLabel*>(QString::fromLatin1(name));
	return label ? label->text() : QStringLiteral("<no label>");
}

bool shownText(const QFrame* row, const char* name) {
	const auto* label = row->findChild<QLabel*>(QString::fromLatin1(name));
	return label && label->isVisible();
}

void pressKey(QWidget& target, int key) {
	QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
	QApplication::sendEvent(&target, &press);
	QApplication::processEvents();
}

// The order of the footer and the focus policy of its buttons on macOS, where Discard comes first.
class MacFooterStyle : public QProxyStyle {
public:
	MacFooterStyle() : QProxyStyle(QStringLiteral("Fusion")) {}
	int styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget, QStyleHintReturn* returnData) const override {
		if (hint == SH_DialogButtonLayout)
			return QDialogButtonBox::MacLayout;
		if (hint == SH_Button_FocusPolicy)
			return Qt::TabFocus;
		return QProxyStyle::styleHint(hint, option, widget, returnData);
	}
};

} // namespace

class TestRecoveryPrompt : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void showsOneRowPerCandidate();
	void showsRow_data();
	void showsRow();
	void selectsOneRowAndItsDefault();
	void selectsFirstOpenableCopy();
	void recoverFinishesWithSelectedRow();
	void decideLaterFinishes();
	void escapeDecidesLater();
	void closingDecidesLater();
	void enterRecoversUnchangedCopy();
	void enterKeepsCopyOfChangedOriginal();
	void unusableCopyIsNotRecovered();
	void discardAsksForConfirmation();
	void leavingAfterFirstDiscardKeepsCopy();
	void namesRowsPlainly();
	void saysOneCopyWasRemoved();
	void offersOnlyLaterWithoutRows();
	void staysInsideScreen_data();
	void staysInsideScreen();
	void confirmationMakesDialogTaller();
	void macFooterKeepsRowDefault_data();
	void macFooterKeepsRowDefault();
};

void TestRecoveryPrompt::initTestCase() {
	QLocale::setDefault(QLocale::c());
}

void TestRecoveryPrompt::showsOneRowPerCandidate() {
	const RecoveryScan scan = makeScan();
	RecoveryPrompt dialog(scan);
	dialog.show();
	QApplication::processEvents();
	const Parts parts = partsOf(dialog);
	// One row for each candidate.
	QCOMPARE(parts.rows.size(), 8);
	QCOMPARE(parts.selectors.size(), 8);
	if (parts.recover == nullptr || parts.discard == nullptr || parts.later == nullptr || parts.confirmation == nullptr)
		QFAIL("buttons and confirmation are found by object name");
	// Button labels.
	QCOMPARE(parts.recover->text(), QStringLiteral("Recover"));
	QCOMPARE(parts.discard->text(), QStringLiteral("Discard"));
	QCOMPARE(parts.later->text(), QStringLiteral("Decide Later"));

	const QString when = QLocale().toString(writtenTime().toLocalTime(), QLocale::ShortFormat);
	// The time shows the date and the local time.
	QVERIFY(when.contains(QStringLiteral("2026")));
	QVERIFY(when.contains(QStringLiteral("09:05")));

	auto* context = dialog.findChild<QLabel*>(QStringLiteral("dialogContext"));
	// Context names several cases.
	if (context == nullptr)
		QFAIL("the dialog has no context line");
	QCOMPARE(context->text(),
		QStringLiteral("EGTRAIN closed before these cases were saved. Recovering opens an unsaved copy. The originals stay unchanged until you save."));
	QVERIFY2(!dialog.findChild<QLabel*>(QStringLiteral("recoveryRemoved")), "no line about removed copies when none were removed");
	// Window title.
	QCOMPARE(dialog.windowTitle(), QStringLiteral("Recover Unsaved Work"));
}

void TestRecoveryPrompt::showsRow_data() {
	QTest::addColumn<int>("row");
	QTest::addColumn<QString>("source");
	QTest::addColumn<QString>("note");
	QTest::newRow("New case study") << 0 << "Not saved yet" << "";
	QTest::newRow("Line 7") << 1 << "/home/test/cases/Line 7.egscene" << "The original changed after this copy was made.";
	QTest::newRow("Old depot") << 2 << "/home/test/cases/old_depot" << "The original is missing.";
	QTest::newRow("Newer file") << 3 << "/home/test/cases/Newer.egscene"
								<< "The original is newer than this copy, so the copy is probably obsolete.";
	QTest::newRow("Same file") << 4 << "/home/test/cases/Same.egscene" << "";
	QTest::newRow("Unknown file") << 5 << "/home/test/cases/Unknown.egscene" << "The original could not be compared with this copy.";
	QTest::newRow("Future copy") << 6 << "/home/test/cases/Future.egscene"
								 << "This copy was made by a newer version of EGTRAIN and cannot be opened by this version.";
	QTest::newRow("Old format") << 7 << "/home/test/cases/Format.egscene" << "This version of EGTRAIN cannot open this copy.";
}

// The row name is the display name of the case.
void TestRecoveryPrompt::showsRow() {
	QFETCH(int, row);
	QFETCH(QString, source);
	QFETCH(QString, note);
	const RecoveryScan scan = makeScan();
	RecoveryPrompt dialog(scan);
	dialog.show();
	QApplication::processEvents();
	const Parts parts = partsOf(dialog);
	if (parts.rows.size() != 8 || parts.selectors.size() != 8)
		QFAIL("one row for each candidate");
	const QString when = QLocale().toString(writtenTime().toLocalTime(), QLocale::ShortFormat);
	const QFrame* frame = parts.rows.at(row);
	// Display name.
	QCOMPARE(parts.selectors.at(row)->text(), QString::fromLatin1(QTest::currentDataTag()));
	// Autosaved time.
	QCOMPARE(textOf(frame, "recoveryTime"), QStringLiteral("Autosaved ") + when);
	// Source.
	QCOMPARE(textOf(frame, "recoverySource"), QDir::toNativeSeparators(source));
	// Source status note.
	QCOMPARE(textOf(frame, "recoveryNote"), note);
	// A note shows only when there is one.
	QCOMPARE(shownText(frame, "recoveryNote"), !note.isEmpty());
}

// Recover is the default unless the original may hold newer work or the copy cannot be opened. Each row is selected
// after the one before it.
void TestRecoveryPrompt::selectsOneRowAndItsDefault() {
	const bool recoverDefault[] = {true, false, true, false, true, false, false, false};
	const bool recoverEnabled[] = {true, true, true, true, true, true, false, false};
	const RecoveryScan scan = makeScan();
	RecoveryPrompt dialog(scan);
	dialog.show();
	QApplication::processEvents();
	const Parts parts = partsOf(dialog);
	// The first row is selected.
	QCOMPARE(dialog.selectedIndex(), 0);
	QVERIFY(parts.selectors.at(0)->isChecked());
	for (int row = 0; row < 8; ++row) {
		parts.selectors.at(row)->setChecked(true);
		// Selecting a row selects it.
		QCOMPARE(dialog.selectedIndex(), row);
		int checked = 0;
		for (const QRadioButton* selector : parts.selectors)
			checked += selector->isChecked() ? 1 : 0;
		// One row is selected at a time.
		QCOMPARE(checked, 1);
		// Default button of the row.
		QCOMPARE(parts.recover->isDefault(), recoverDefault[row]);
		QCOMPARE(parts.later->isDefault(), !recoverDefault[row]);
		QVERIFY(!parts.discard->isDefault());
		// Recover is enabled only for a copy that can be opened.
		QCOMPARE(parts.recover->isEnabled(), recoverEnabled[row]);
		// Discard and Decide Later stay enabled.
		QVERIFY(parts.discard->isEnabled());
		QVERIFY(parts.later->isEnabled());
	}
}

// A copy that cannot be opened is skipped when the first row is chosen.
void TestRecoveryPrompt::selectsFirstOpenableCopy() {
	RecoveryScan unusableFirst;
	unusableFirst.candidates.append(makeCandidate(QStringLiteral("Future copy"), RecoverySourceKind::Bundle, QStringLiteral("/home/test/a.egscene"),
		RecoverySourceStatus::Unchanged, RecoveryCandidateState::NewerRecordVersion));
	unusableFirst.candidates.append(
		makeCandidate(QStringLiteral("Line 7"), RecoverySourceKind::Untitled, QString(), RecoverySourceStatus::NotApplicable));
	RecoveryPrompt skipping(unusableFirst);
	// The first copy that can be opened is selected.
	QCOMPARE(skipping.selectedIndex(), 1);
	RecoveryScan onlyUnusable;
	onlyUnusable.candidates.append(unusableFirst.candidates.first());
	RecoveryPrompt unusable(onlyUnusable);
	const Parts unusableParts = partsOf(unusable);
	// A copy that cannot be opened can only be discarded or left.
	QCOMPARE(unusable.selectedIndex(), 0);
	QVERIFY(!unusableParts.recover->isEnabled());
	QVERIFY(unusableParts.later->isDefault());
	QVERIFY(unusableParts.discard->isEnabled());
}

void TestRecoveryPrompt::recoverFinishesWithSelectedRow() {
	const RecoveryScan scan = makeScan();
	RecoveryPrompt dialog(scan);
	dialog.show();
	QApplication::processEvents();
	const Parts parts = partsOf(dialog);
	parts.selectors.at(4)->setChecked(true);
	parts.recover->click();
	// Recover finishes with the selected row.
	QCOMPARE(dialog.result(), int(RecoveryPrompt::Recover));
	QCOMPARE(dialog.selectedIndex(), 4);
	QVERIFY(!dialog.isVisible());
}

void TestRecoveryPrompt::decideLaterFinishes() {
	const RecoveryScan scan = makeScan();
	RecoveryPrompt dialog(scan);
	dialog.show();
	QApplication::processEvents();
	const Parts parts = partsOf(dialog);
	parts.selectors.at(2)->setChecked(true);
	parts.later->click();
	// Decide Later finishes.
	QCOMPARE(dialog.result(), int(RecoveryPrompt::Later));
	QVERIFY(!dialog.isVisible());
}

void TestRecoveryPrompt::escapeDecidesLater() {
	const RecoveryScan scan = makeScan();
	RecoveryPrompt dialog(scan);
	dialog.show();
	QApplication::processEvents();
	pressKey(dialog, Qt::Key_Escape);
	// Escape decides later.
	QCOMPARE(dialog.result(), int(RecoveryPrompt::Later));
	QVERIFY(!dialog.isVisible());
}

void TestRecoveryPrompt::closingDecidesLater() {
	const RecoveryScan scan = makeScan();
	RecoveryPrompt dialog(scan);
	dialog.show();
	QApplication::processEvents();
	dialog.close();
	// Closing the window decides later.
	QCOMPARE(dialog.result(), int(RecoveryPrompt::Later));
	QVERIFY(!dialog.isVisible());
}

void TestRecoveryPrompt::enterRecoversUnchangedCopy() {
	const RecoveryScan scan = makeScan();
	RecoveryPrompt dialog(scan);
	dialog.show();
	QApplication::processEvents();
	const Parts parts = partsOf(dialog);
	parts.selectors.at(4)->setChecked(true);
	pressKey(dialog, Qt::Key_Return);
	// Enter recovers an unchanged copy.
	QCOMPARE(dialog.result(), int(RecoveryPrompt::Recover));
	QCOMPARE(dialog.selectedIndex(), 4);
}

void TestRecoveryPrompt::enterKeepsCopyOfChangedOriginal() {
	const RecoveryScan scan = makeScan();
	RecoveryPrompt dialog(scan);
	dialog.show();
	QApplication::processEvents();
	const Parts parts = partsOf(dialog);
	parts.selectors.at(1)->setChecked(true);
	pressKey(dialog, Qt::Key_Return);
	// Enter does not recover a copy whose original changed.
	QCOMPARE(dialog.result(), int(RecoveryPrompt::Later));
	QVERIFY(!dialog.isVisible());
}

void TestRecoveryPrompt::unusableCopyIsNotRecovered() {
	const RecoveryScan scan = makeScan();
	RecoveryPrompt dialog(scan);
	dialog.show();
	QApplication::processEvents();
	const Parts parts = partsOf(dialog);
	parts.selectors.at(6)->setChecked(true);
	parts.recover->click();
	// A copy that cannot be opened is not recovered.
	QVERIFY(dialog.isVisible());
	QVERIFY(dialog.result() != RecoveryPrompt::Recover);
}

void TestRecoveryPrompt::discardAsksForConfirmation() {
	const RecoveryScan scan = makeScan();
	RecoveryPrompt dialog(scan);
	dialog.show();
	QApplication::processEvents();
	const Parts parts = partsOf(dialog);
	// No confirmation before Discard is clicked.
	if (parts.confirmation == nullptr)
		QFAIL("the dialog has no confirmation");
	QVERIFY(!parts.confirmation->isVisible());

	parts.selectors.at(2)->setChecked(true);
	parts.discard->click();
	// The first click on Discard does not finish.
	QVERIFY(dialog.isVisible());
	QVERIFY(dialog.result() != RecoveryPrompt::Discard);
	// The confirmation names the case.
	QVERIFY(parts.confirmation->isVisible());
	QCOMPARE(parts.confirmation->text(), QStringLiteral("Delete the recovery copy of \"Old depot\"? It cannot be brought back."));
	// The button asks to confirm.
	QCOMPARE(parts.discard->text(), QStringLiteral("Confirm Discard"));

	parts.selectors.at(3)->setChecked(true);
	// Choosing another row cancels the confirmation.
	QVERIFY(!parts.confirmation->isVisible());
	QCOMPARE(parts.discard->text(), QStringLiteral("Discard"));
	parts.discard->click();
	// The confirmation names the new case.
	QVERIFY(dialog.isVisible());
	QVERIFY(parts.confirmation->text().contains(QStringLiteral("\"Newer file\"")));
	parts.discard->click();
	// The second click discards the selected row.
	QCOMPARE(dialog.result(), int(RecoveryPrompt::Discard));
	QCOMPARE(dialog.selectedIndex(), 3);
	QVERIFY(!dialog.isVisible());
}

void TestRecoveryPrompt::leavingAfterFirstDiscardKeepsCopy() {
	const RecoveryScan scan = makeScan();
	RecoveryPrompt later(scan);
	later.show();
	QApplication::processEvents();
	const Parts laterParts = partsOf(later);
	laterParts.discard->click();
	QVERIFY2(later.isVisible(), "the dialog stays open after the first click");
	pressKey(later, Qt::Key_Escape);
	// Leaving after the first click discards nothing.
	QCOMPARE(later.result(), int(RecoveryPrompt::Later));
}

void TestRecoveryPrompt::namesRowsPlainly() {
	const RecoveryScan single = makeTextScan();
	RecoveryPrompt dialog(single);
	dialog.show();
	QApplication::processEvents();
	const Parts parts = partsOf(dialog);
	// An ampersand in a name is not a shortcut.
	QCOMPARE(parts.selectors.at(0)->text(), QStringLiteral("A && B"));
	QCOMPARE(parts.selectors.at(0)->accessibleName(), QStringLiteral("A & B"));
	// A case without a name is called untitled.
	QCOMPARE(parts.selectors.at(1)->text(), QStringLiteral("Untitled case"));
	// An unknown time is said so.
	QCOMPARE(textOf(parts.rows.at(1), "recoveryTime"), QStringLiteral("Autosave time unknown"));
	// A source without a path is a new case.
	QCOMPARE(textOf(parts.rows.at(1), "recoverySource"), QStringLiteral("Not saved yet"));
	auto* removed = dialog.findChild<QLabel*>(QStringLiteral("recoveryRemoved"));
	// The dialog says that older copies were removed.
	if (removed == nullptr)
		QFAIL("the dialog has no line about removed copies");
	QVERIFY(removed->isVisible());
	QCOMPARE(removed->text(), QStringLiteral("Removed 3 older copies. EGTRAIN keeps the newest 20."));
}

void TestRecoveryPrompt::saysOneCopyWasRemoved() {
	RecoveryScan oneRemoved;
	oneRemoved.candidates.append(makeTextScan().candidates.first());
	oneRemoved.removedOverCap = 1;
	RecoveryPrompt singleDialog(oneRemoved);
	auto* oneLine = singleDialog.findChild<QLabel*>(QStringLiteral("recoveryRemoved"));
	// One removed copy.
	if (oneLine == nullptr)
		QFAIL("the dialog has no line about removed copies");
	QCOMPARE(oneLine->text(), QStringLiteral("Removed 1 older copy. EGTRAIN keeps the newest 20."));
	auto* context = singleDialog.findChild<QLabel*>(QStringLiteral("dialogContext"));
	// Context names one case.
	if (context == nullptr)
		QFAIL("the dialog has no context line");
	QCOMPARE(context->text(), QStringLiteral("EGTRAIN closed before this case was saved. The original stays unchanged until you save."));
}

void TestRecoveryPrompt::offersOnlyLaterWithoutRows() {
	RecoveryPrompt empty{RecoveryScan()};
	const Parts none = partsOf(empty);
	empty.show();
	QApplication::processEvents();
	none.discard->click();
	// Without rows only Decide Later is available.
	QCOMPARE(empty.selectedIndex(), -1);
	QVERIFY(none.rows.isEmpty());
	QVERIFY(!none.recover->isEnabled());
	QVERIFY(!none.discard->isEnabled());
	QVERIFY(none.later->isDefault());
	QVERIFY(empty.isVisible());
}

void TestRecoveryPrompt::staysInsideScreen_data() {
	QTest::addColumn<QRect>("screen");
	QTest::addColumn<qreal>("scale");
	QTest::newRow("1280x800") << QRect(0, 0, 1280, 800) << qreal(1.0);
	QTest::newRow("800x600 at 1.5") << QRect(0, 0, 800, 600) << qreal(1.5);
}

void TestRecoveryPrompt::staysInsideScreen() {
	QFETCH(QRect, screen);
	QFETCH(qreal, scale);
	const QString longName(2500, QLatin1Char('X'));
	const QString longPath = QStringLiteral("/home/test/") + QString(2500, QLatin1Char('d')) + QStringLiteral("/Line.egscene");
	RecoveryScan scan;
	for (int i = 0; i < 12; ++i) {
		RecoveryCandidate candidate = makeCandidate(longName, RecoverySourceKind::Bundle, longPath, RecoverySourceStatus::NewerThanCopy);
		candidate.instanceId = QString::number(i);
		scan.candidates.append(candidate);
	}
	scan.removedOverCap = 2;
	const QFont applicationFont = QApplication::font();
	const auto restoreFont = qScopeGuard([&applicationFont] { QApplication::setFont(applicationFont); });
	QFont font = applicationFont;
	font.setPointSizeF(12 * scale);
	QApplication::setFont(font);
	RecoveryPrompt dialog(scan, nullptr, screen);
	dialog.show();
	QApplication::processEvents();
	const Parts parts = partsOf(dialog);
	auto* scroll = dialog.findChild<QScrollArea*>(QStringLiteral("dialogBodyScroll"));
	auto* footer = dialog.findChild<QDialogButtonBox*>();
	// The dialog stays inside the available area.
	QCOMPARE_LE(dialog.width(), screen.width() * 9 / 10);
	QCOMPARE_LE(dialog.height(), screen.height() * 4 / 5);
	// Many rows scroll.
	if (scroll == nullptr || footer == nullptr)
		QFAIL("the dialog has no scroll area or no footer");
	QCOMPARE_GT(scroll->verticalScrollBar()->maximum(), 0);
	// A long name or path never scrolls sideways.
	QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
	// The buttons stay reachable below the list.
	QVERIFY(footer->isVisible());
	QCOMPARE_LE(footer->geometry().bottom(), dialog.height());
	QCOMPARE_LE(scroll->geometry().bottom(), footer->geometry().top());
	const QString shown = parts.selectors.at(0)->text();
	// A long name is cut and kept whole for the accessible name.
	QCOMPARE_LT(shown.size(), longName.size());
	QVERIFY(shown.contains(QChar(0x2026)));
	QCOMPARE(parts.selectors.at(0)->accessibleName(), longName);
	// The name fits the list.
	QCOMPARE_LE(parts.selectors.at(0)->width(), scroll->viewport()->width());

	parts.discard->click();
	QApplication::processEvents();
	// The confirmation stays inside the dialog above the buttons.
	QVERIFY(parts.confirmation->isVisible());
	QVERIFY(dialog.rect().contains(parts.confirmation->geometry()));
	QCOMPARE_LE(parts.confirmation->geometry().bottom(), footer->geometry().top());
}

// The dialog opens at the height its content needs, so the confirmation that Discard shows has to make the dialog taller
// instead of taking height from the list.
void TestRecoveryPrompt::confirmationMakesDialogTaller() {
	RecoveryScan scan;
	scan.candidates.append(makeCandidate(QStringLiteral("Line 7"), RecoverySourceKind::Bundle, QStringLiteral("/home/test/cases/Line 7.egscene"),
		RecoverySourceStatus::Changed));
	RecoveryPrompt dialog(scan, nullptr, QRect(0, 0, 1920, 1080));
	dialog.show();
	QApplication::processEvents();
	const Parts parts = partsOf(dialog);
	auto* scroll = dialog.findChild<QScrollArea*>(QStringLiteral("dialogBodyScroll"));
	const int before = dialog.height();
	// One recovery copy opens without a scroll bar.
	if (scroll == nullptr)
		QFAIL("the dialog has no scroll area");
	QCOMPARE(scroll->verticalScrollBar()->maximum(), 0);
	parts.discard->click();
	QApplication::processEvents();
	QApplication::processEvents();
	// The confirmation makes the dialog taller.
	QVERIFY(parts.confirmation->isVisible());
	QCOMPARE_GT(dialog.height(), before);
	// The confirmation does not make the list scroll.
	QCOMPARE(scroll->verticalScrollBar()->maximum(), 0);
}

void TestRecoveryPrompt::macFooterKeepsRowDefault_data() {
	QTest::addColumn<bool>("unchanged");
	QTest::newRow("unchanged") << true;
	QTest::newRow("newer than copy") << false;
}

// With the footer of macOS the prompt opens with the default of the selected row and the focus on that row. A footer
// button that gets the focus leaves the default where it is, so Return presses it. Runs last: it changes the
// application style.
void TestRecoveryPrompt::macFooterKeepsRowDefault() {
	QFETCH(bool, unchanged);
	QApplication::setStyle(new MacFooterStyle);
	const RecoverySourceStatus status = unchanged ? RecoverySourceStatus::Unchanged : RecoverySourceStatus::NewerThanCopy;
	RecoveryScan scan;
	scan.candidates.append(makeCandidate(QStringLiteral("Same file"), RecoverySourceKind::Bundle, QStringLiteral("/home/test/cases/Same.egscene"), status));
	scan.candidates.append(makeCandidate(QStringLiteral("Newer file"), RecoverySourceKind::Bundle, QStringLiteral("/home/test/cases/Newer.egscene"),
		RecoverySourceStatus::NewerThanCopy));
	RecoveryPrompt dialog(scan);
	dialog.show();
	dialog.activateWindow();
	QApplication::processEvents();
	const Parts parts = partsOf(dialog);
	QPushButton* expected = unchanged ? parts.recover : parts.later;
	QVERIFY2(dialog.isActiveWindow(), "the prompt is active, so that the focus is real");
	QVERIFY2(dialog.focusWidget() == parts.selectors.at(0), "the selected row has the focus when the prompt opens");
	// Only the default of the row is the default button.
	for (const QPushButton* button : {parts.recover, parts.discard, parts.later})
		QCOMPARE(button->isDefault(), button == expected);
	parts.discard->setFocus(Qt::TabFocusReason);
	QApplication::processEvents();
	// Discard gets the focus and leaves the default.
	QVERIFY(parts.discard->hasFocus());
	QVERIFY(expected->isDefault());
	QVERIFY(!parts.discard->isDefault());
	pressKey(*parts.discard, Qt::Key_Return);
	// Return presses the default button while Discard has the focus.
	QCOMPARE(dialog.result(), int(expected == parts.recover ? RecoveryPrompt::Recover : RecoveryPrompt::Later));
}

QTEST_MAIN(TestRecoveryPrompt)
#include "test_recoveryprompt.moc"
