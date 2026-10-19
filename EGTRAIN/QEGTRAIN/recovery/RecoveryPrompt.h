#pragma once

// RecoveryStore.h includes no Qt header that defines the macro "signals", because SceneModel has a
// member of that name. This header includes QDialog, so a file that uses the members of SceneModel
// includes scene/SceneModel.h before it.
#include "recovery/RecoveryStore.h"

#include <QDialog>
#include <QRect>
#include <QString>
#include <QVector>

class QLabel;
class QPushButton;

/// Offers the recovery copies that an earlier run left behind, one row for each, and reports what the
/// user chose. It is a dialog of its own and not a QMessageBox, which crashes on the Qt 5.15 offscreen
/// platform on Windows. The dialog changes nothing on disk: the caller recovers or discards the
/// copy that selectedIndex names.
///
/// Recover and Discard act on the selected row, and Decide Later keeps every copy. Recover is the
/// default button unless the original of the copy may hold newer work, and it is disabled for a copy
/// that this version cannot open. Discard asks for a second click, naming the case, before it
/// finishes. Escape and closing the window give Later.
///
/// After the dialog closes, result() holds an Outcome. A caller that discards one copy can remove
/// that row and show the dialog again for the rest.
class RecoveryPrompt : public QDialog {
public:
	/// The choice of the user, as the result code of the dialog.
	enum Outcome {
		Later = QDialog::Rejected,	 ///< Keep every copy: Decide Later, Escape or closing the window.
		Recover = QDialog::Accepted, ///< Recover the copy that selectedIndex names.
		Discard = 2,				 ///< Discard the copy that selectedIndex names, after the second click.
	};

	/// The scan names the copies and how many older ones the store removed over its limit.
	/// availableGeometry bounds the size of the dialog; the screen of the dialog is used when it is
	/// invalid. A crashNote path says above the copies that EGTRAIN closed unexpectedly and where the
	/// crash note is.
	explicit RecoveryPrompt(const RecoveryScan& scan, QWidget* parent = nullptr, const QRect& availableGeometry = QRect(),
		const QString& crashNote = QString());

	/// The row that Recover and Discard act on, as an index into scan.candidates; -1 without rows.
	int selectedIndex() const { return m_selected; }

private:
	void selectRow(int index);
	void discardClicked();

	QVector<RecoveryCandidate> m_candidates;
	int m_selected = -1;
	bool m_discardPending = false;
	QPushButton* m_recover = nullptr;
	QPushButton* m_discard = nullptr;
	QPushButton* m_later = nullptr;
	QLabel* m_confirmation = nullptr;
};
