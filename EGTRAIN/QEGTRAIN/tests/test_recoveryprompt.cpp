#include "recovery/RecoveryPrompt.h"

#include <QApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>

#include <iostream>

namespace {

bool check(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << '\n';
	return condition;
}

QDateTime writtenTime() {
	return QDateTime(QDate(2026, 10, 14), QTime(9, 5), Qt::LocalTime).toUTC();
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

bool exerciseRows() {
	const RecoveryScan scan = makeScan();
	RecoveryPrompt dialog(scan);
	dialog.show();
	QApplication::processEvents();
	const Parts parts = partsOf(dialog);
	bool ok = check(parts.rows.size() == 8 && parts.selectors.size() == 8, "one row for each candidate");
	if (!ok)
		return false;
	ok &= check(parts.recover && parts.discard && parts.later && parts.confirmation, "buttons and confirmation are found by object name");
	ok &= check(parts.recover->text() == QStringLiteral("Recover") && parts.discard->text() == QStringLiteral("Discard")
			&& parts.later->text() == QStringLiteral("Decide Later"),
		"button labels");

	const QString when = QLocale().toString(writtenTime().toLocalTime(), QLocale::ShortFormat);
	ok &= check(when.contains(QStringLiteral("2026")) && when.contains(QStringLiteral("09:05")), "the time shows the date and the local time");
	const char* const names[] = {"New case study", "Line 7", "Old depot", "Newer file", "Same file", "Unknown file", "Future copy", "Old format"};
	const char* const sources[] = {"New case study, not saved", "/home/test/cases/Line 7.egscene", "/home/test/cases/old_depot",
		"/home/test/cases/Newer.egscene", "/home/test/cases/Same.egscene", "/home/test/cases/Unknown.egscene",
		"/home/test/cases/Future.egscene", "/home/test/cases/Format.egscene"};
	const char* const notes[] = {"", "The original changed after this copy was made.", "The original is missing.",
		"The original is newer than this copy, so the copy is probably obsolete.", "",
		"The original could not be compared with this copy.",
		"This copy was made by a newer version of EGTRAIN and cannot be opened by this version.",
		"This copy uses a different scene version and cannot be opened by this version of EGTRAIN."};
	for (int row = 0; row < 8; ++row) {
		const QFrame* frame = parts.rows.at(row);
		ok &= check(parts.selectors.at(row)->text() == QString::fromLatin1(names[row]), "display name");
		ok &= check(textOf(frame, "recoveryTime") == QStringLiteral("Autosaved ") + when, "autosaved time");
		ok &= check(textOf(frame, "recoverySource") == QDir::toNativeSeparators(QString::fromLatin1(sources[row])), "source");
		const QString note = QString::fromLatin1(notes[row]);
		ok &= check(textOf(frame, "recoveryNote") == note, "source status note");
		ok &= check(shownText(frame, "recoveryNote") == !note.isEmpty(), "a note shows only when there is one");
	}

	auto* context = dialog.findChild<QLabel*>(QStringLiteral("dialogContext"));
	ok &= check(context && context->text().startsWith(QStringLiteral("EGTRAIN closed before these cases were saved.")), "context names several cases");
	ok &= check(!dialog.findChild<QLabel*>(QStringLiteral("recoveryRemoved")), "no line about removed copies when none were removed");
	ok &= check(dialog.windowTitle() == QStringLiteral("Recover Unsaved Work"), "window title");
	return ok;
}

bool exerciseSelectionAndDefault() {
	// Recover is the default unless the original may hold newer work or the copy cannot be opened.
	const bool recoverDefault[] = {true, false, true, false, true, false, false, false};
	const bool recoverEnabled[] = {true, true, true, true, true, true, false, false};
	const RecoveryScan scan = makeScan();
	RecoveryPrompt dialog(scan);
	dialog.show();
	QApplication::processEvents();
	const Parts parts = partsOf(dialog);
	bool ok = check(dialog.selectedIndex() == 0 && parts.selectors.at(0)->isChecked(), "the first row is selected");
	for (int row = 0; row < 8; ++row) {
		parts.selectors.at(row)->setChecked(true);
		ok &= check(dialog.selectedIndex() == row, "selecting a row selects it");
		int checked = 0;
		for (const QRadioButton* selector : parts.selectors)
			checked += selector->isChecked() ? 1 : 0;
		ok &= check(checked == 1, "one row is selected at a time");
		ok &= check(parts.recover->isDefault() == recoverDefault[row] && parts.later->isDefault() == !recoverDefault[row] && !parts.discard->isDefault(),
			"default button of the row");
		ok &= check(parts.recover->isEnabled() == recoverEnabled[row], "Recover is enabled only for a copy that can be opened");
		ok &= check(parts.discard->isEnabled() && parts.later->isEnabled(), "Discard and Decide Later stay enabled");
	}

	// A copy that cannot be opened is skipped when the first row is chosen.
	RecoveryScan unusableFirst;
	unusableFirst.candidates.append(makeCandidate(QStringLiteral("Future copy"), RecoverySourceKind::Bundle, QStringLiteral("/home/test/a.egscene"),
		RecoverySourceStatus::Unchanged, RecoveryCandidateState::NewerRecordVersion));
	unusableFirst.candidates.append(
		makeCandidate(QStringLiteral("Line 7"), RecoverySourceKind::Untitled, QString(), RecoverySourceStatus::NotApplicable));
	RecoveryPrompt skipping(unusableFirst);
	ok &= check(skipping.selectedIndex() == 1, "the first copy that can be opened is selected");
	RecoveryScan onlyUnusable;
	onlyUnusable.candidates.append(unusableFirst.candidates.first());
	RecoveryPrompt unusable(onlyUnusable);
	const Parts unusableParts = partsOf(unusable);
	ok &= check(unusable.selectedIndex() == 0 && !unusableParts.recover->isEnabled() && unusableParts.later->isDefault()
			&& unusableParts.discard->isEnabled(),
		"a copy that cannot be opened can only be discarded or left");
	return ok;
}

bool exerciseOutcomes() {
	const RecoveryScan scan = makeScan();
	bool ok = true;
	{
		RecoveryPrompt dialog(scan);
		dialog.show();
		QApplication::processEvents();
		const Parts parts = partsOf(dialog);
		parts.selectors.at(4)->setChecked(true);
		parts.recover->click();
		ok &= check(dialog.result() == RecoveryPrompt::Recover && dialog.selectedIndex() == 4 && !dialog.isVisible(),
			"Recover finishes with the selected row");
	}
	{
		RecoveryPrompt dialog(scan);
		dialog.show();
		QApplication::processEvents();
		const Parts parts = partsOf(dialog);
		parts.selectors.at(2)->setChecked(true);
		parts.later->click();
		ok &= check(dialog.result() == RecoveryPrompt::Later && !dialog.isVisible(), "Decide Later finishes");
	}
	{
		RecoveryPrompt dialog(scan);
		dialog.show();
		QApplication::processEvents();
		pressKey(dialog, Qt::Key_Escape);
		ok &= check(dialog.result() == RecoveryPrompt::Later && !dialog.isVisible(), "Escape decides later");
	}
	{
		RecoveryPrompt dialog(scan);
		dialog.show();
		QApplication::processEvents();
		dialog.close();
		ok &= check(dialog.result() == RecoveryPrompt::Later && !dialog.isVisible(), "closing the window decides later");
	}
	{
		RecoveryPrompt dialog(scan);
		dialog.show();
		QApplication::processEvents();
		const Parts parts = partsOf(dialog);
		parts.selectors.at(4)->setChecked(true);
		pressKey(dialog, Qt::Key_Return);
		ok &= check(dialog.result() == RecoveryPrompt::Recover && dialog.selectedIndex() == 4, "Enter recovers an unchanged copy");
	}
	{
		RecoveryPrompt dialog(scan);
		dialog.show();
		QApplication::processEvents();
		const Parts parts = partsOf(dialog);
		parts.selectors.at(1)->setChecked(true);
		pressKey(dialog, Qt::Key_Return);
		ok &= check(dialog.result() == RecoveryPrompt::Later && !dialog.isVisible(), "Enter does not recover a copy whose original changed");
	}
	{
		RecoveryPrompt dialog(scan);
		dialog.show();
		QApplication::processEvents();
		const Parts parts = partsOf(dialog);
		parts.selectors.at(6)->setChecked(true);
		parts.recover->click();
		ok &= check(dialog.isVisible() && dialog.result() != RecoveryPrompt::Recover, "a copy that cannot be opened is not recovered");
	}
	return ok;
}

bool exerciseDiscard() {
	const RecoveryScan scan = makeScan();
	RecoveryPrompt dialog(scan);
	dialog.show();
	QApplication::processEvents();
	const Parts parts = partsOf(dialog);
	bool ok = check(parts.confirmation && !parts.confirmation->isVisible(), "no confirmation before Discard is clicked");

	parts.selectors.at(2)->setChecked(true);
	parts.discard->click();
	ok &= check(dialog.isVisible() && dialog.result() != RecoveryPrompt::Discard, "the first click on Discard does not finish");
	ok &= check(parts.confirmation->isVisible()
			&& parts.confirmation->text() == QStringLiteral("Delete the recovery copy of \"Old depot\"? It cannot be brought back."),
		"the confirmation names the case");
	ok &= check(parts.discard->text() == QStringLiteral("Confirm Discard"), "the button asks to confirm");

	parts.selectors.at(3)->setChecked(true);
	ok &= check(!parts.confirmation->isVisible() && parts.discard->text() == QStringLiteral("Discard"), "choosing another row cancels the confirmation");
	parts.discard->click();
	ok &= check(dialog.isVisible() && parts.confirmation->text().contains(QStringLiteral("\"Newer file\"")), "the confirmation names the new case");
	parts.discard->click();
	ok &= check(dialog.result() == RecoveryPrompt::Discard && dialog.selectedIndex() == 3 && !dialog.isVisible(),
		"the second click discards the selected row");

	RecoveryPrompt later(scan);
	later.show();
	QApplication::processEvents();
	const Parts laterParts = partsOf(later);
	laterParts.discard->click();
	ok &= check(later.isVisible(), "the dialog stays open after the first click");
	pressKey(later, Qt::Key_Escape);
	ok &= check(later.result() == RecoveryPrompt::Later, "leaving after the first click discards nothing");
	return ok;
}

bool exerciseText() {
	bool ok = true;
	RecoveryScan single;
	single.candidates.append(makeCandidate(QStringLiteral("A & B"), RecoverySourceKind::Untitled, QString(), RecoverySourceStatus::NotApplicable));
	single.candidates.append(makeCandidate(QString(), RecoverySourceKind::Bundle, QString(), RecoverySourceStatus::NotApplicable));
	single.candidates[1].writtenAt = QDateTime();
	single.removedOverCap = 3;
	RecoveryPrompt dialog(single);
	dialog.show();
	QApplication::processEvents();
	const Parts parts = partsOf(dialog);
	ok &= check(parts.selectors.at(0)->text() == QStringLiteral("A && B") && parts.selectors.at(0)->accessibleName() == QStringLiteral("A & B"),
		"an ampersand in a name is not a shortcut");
	ok &= check(parts.selectors.at(1)->text() == QStringLiteral("Untitled case"), "a case without a name is called untitled");
	ok &= check(textOf(parts.rows.at(1), "recoveryTime") == QStringLiteral("Autosave time unknown"), "an unknown time is said so");
	ok &= check(textOf(parts.rows.at(1), "recoverySource") == QStringLiteral("New case study, not saved"), "a source without a path is a new case");
	auto* removed = dialog.findChild<QLabel*>(QStringLiteral("recoveryRemoved"));
	ok &= check(removed && removed->isVisible()
			&& removed->text() == QStringLiteral("3 older recovery copies were removed, because EGTRAIN keeps the newest 20."),
		"the dialog says that older copies were removed");

	RecoveryScan oneRemoved;
	oneRemoved.candidates.append(single.candidates.first());
	oneRemoved.removedOverCap = 1;
	RecoveryPrompt singleDialog(oneRemoved);
	auto* oneLine = singleDialog.findChild<QLabel*>(QStringLiteral("recoveryRemoved"));
	ok &= check(oneLine && oneLine->text() == QStringLiteral("1 older recovery copy was removed, because EGTRAIN keeps the newest 20."),
		"one removed copy");
	auto* context = singleDialog.findChild<QLabel*>(QStringLiteral("dialogContext"));
	ok &= check(context && context->text().startsWith(QStringLiteral("EGTRAIN closed before this case was saved.")), "context names one case");

	RecoveryPrompt empty{RecoveryScan()};
	const Parts none = partsOf(empty);
	empty.show();
	QApplication::processEvents();
	none.discard->click();
	ok &= check(empty.selectedIndex() == -1 && none.rows.isEmpty() && !none.recover->isEnabled() && !none.discard->isEnabled() && none.later->isDefault()
			&& empty.isVisible(),
		"without rows only Decide Later is available");
	return ok;
}

bool exerciseGeometry(const QRect& screen, qreal scale) {
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
	QFont font = applicationFont;
	font.setPointSizeF(12 * scale);
	QApplication::setFont(font);
	RecoveryPrompt dialog(scan, nullptr, screen);
	dialog.show();
	QApplication::processEvents();
	const Parts parts = partsOf(dialog);
	auto* scroll = dialog.findChild<QScrollArea*>(QStringLiteral("dialogBodyScroll"));
	auto* footer = dialog.findChild<QDialogButtonBox*>();
	bool ok = check(dialog.width() <= screen.width() * 9 / 10 && dialog.height() <= screen.height() * 4 / 5, "the dialog stays inside the available area");
	ok &= check(scroll && footer && scroll->verticalScrollBar()->maximum() > 0, "many rows scroll");
	ok &= check(scroll && scroll->horizontalScrollBar()->maximum() == 0, "a long name or path never scrolls sideways");
	ok &= check(footer && footer->isVisible() && footer->geometry().bottom() <= dialog.height()
			&& scroll->geometry().bottom() <= footer->geometry().top(),
		"the buttons stay reachable below the list");
	const QString shown = parts.selectors.at(0)->text();
	ok &= check(shown.size() < longName.size() && shown.contains(QChar(0x2026)) && parts.selectors.at(0)->accessibleName() == longName,
		"a long name is cut and kept whole for the accessible name");
	ok &= check(parts.selectors.at(0)->width() <= scroll->viewport()->width(), "the name fits the list");

	parts.discard->click();
	QApplication::processEvents();
	ok &= check(parts.confirmation->isVisible() && dialog.rect().contains(parts.confirmation->geometry())
			&& parts.confirmation->geometry().bottom() <= footer->geometry().top(),
		"the confirmation stays inside the dialog above the buttons");
	QApplication::setFont(applicationFont);
	return ok;
}

// The dialog opens at the height its content needs, so the confirmation that Discard shows has to make the dialog taller
// instead of taking height from the list.
bool exerciseConfirmationHeight() {
	RecoveryScan scan;
	scan.candidates.append(makeCandidate(QStringLiteral("Line 7"), RecoverySourceKind::Bundle, QStringLiteral("/home/test/cases/Line 7.egscene"),
		RecoverySourceStatus::Changed));
	RecoveryPrompt dialog(scan, nullptr, QRect(0, 0, 1920, 1080));
	dialog.show();
	QApplication::processEvents();
	const Parts parts = partsOf(dialog);
	auto* scroll = dialog.findChild<QScrollArea*>(QStringLiteral("dialogBodyScroll"));
	const int before = dialog.height();
	bool ok = check(scroll && scroll->verticalScrollBar()->maximum() == 0, "one recovery copy opens without a scroll bar");
	parts.discard->click();
	QApplication::processEvents();
	QApplication::processEvents();
	ok &= check(parts.confirmation->isVisible() && dialog.height() > before, "the confirmation makes the dialog taller");
	ok &= check(scroll && scroll->verticalScrollBar()->maximum() == 0, "the confirmation does not make the list scroll");
	return ok;
}

} // namespace

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	QLocale::setDefault(QLocale::c());
	bool ok = exerciseRows();
	ok &= exerciseSelectionAndDefault();
	ok &= exerciseOutcomes();
	ok &= exerciseDiscard();
	ok &= exerciseText();
	ok &= exerciseGeometry(QRect(0, 0, 1280, 800), 1.0);
	ok &= exerciseGeometry(QRect(0, 0, 800, 600), 1.5);
	ok &= exerciseConfirmationHeight();
	return ok ? 0 : 1;
}
