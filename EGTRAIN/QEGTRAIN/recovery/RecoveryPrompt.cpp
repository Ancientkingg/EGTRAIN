#include "recovery/RecoveryPrompt.h"
#include "widgets/DialogLayout.h"

#include <QButtonGroup>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFontMetrics>
#include <QFrame>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QRadioButton>
#include <QStyle>
#include <QVBoxLayout>

namespace {

// A case name wider than this many line heights is cut in the middle. The tooltip and the
// accessible name keep the whole name. A narrower dialog clips the name instead.
constexpr int kNameWidthInLines = 24;

QString caseName(const RecoveryCandidate& candidate) {
	return candidate.source.displayName.isEmpty() ? QStringLiteral("Untitled case") : candidate.source.displayName;
}

QString autosavedText(const QDateTime& writtenAt) {
	if (!writtenAt.isValid())
		return QStringLiteral("Autosave time unknown");
	return QStringLiteral("Autosaved ") + QLocale().toString(writtenAt.toLocalTime(), QLocale::ShortFormat);
}

QString sourceText(const RecoveryCandidate& candidate) {
	if (candidate.source.kind == RecoverySourceKind::Untitled || candidate.source.path.isEmpty())
		return QStringLiteral("New case study, not saved");
	return QDir::toNativeSeparators(candidate.source.path);
}

// What to tell the user about the copy or its original; empty when there is nothing to tell.
QString noteText(const RecoveryCandidate& candidate) {
	switch (candidate.state) {
		case RecoveryCandidateState::NewerRecordVersion:
			return QStringLiteral("This copy was made by a newer version of EGTRAIN and cannot be opened by this version.");
		case RecoveryCandidateState::OtherSceneVersion:
			return QStringLiteral("This copy uses a different scene version and cannot be opened by this version of EGTRAIN.");
		case RecoveryCandidateState::Openable:
			break;
	}
	switch (candidate.sourceStatus) {
		case RecoverySourceStatus::Unknown:
			return QStringLiteral("The original could not be compared with this copy.");
		case RecoverySourceStatus::Changed:
			return QStringLiteral("The original changed after this copy was made.");
		case RecoverySourceStatus::NewerThanCopy:
			return QStringLiteral("The original is newer than this copy, so the copy is probably obsolete.");
		case RecoverySourceStatus::Missing:
			return QStringLiteral("The original is missing.");
		case RecoverySourceStatus::NotApplicable:
		case RecoverySourceStatus::Unchanged:
			break;
	}
	return QString();
}

// Whether the original may hold work that the copy lacks. A missing original cannot.
bool originalMayBeNewer(RecoverySourceStatus status) {
	return status == RecoverySourceStatus::Unknown || status == RecoverySourceStatus::Changed || status == RecoverySourceStatus::NewerThanCopy;
}

QString removedText(int removed) {
	const QString what = removed == 1 ? QStringLiteral("1 older recovery copy was") : QStringLiteral("%1 older recovery copies were").arg(removed);
	return what + QStringLiteral(" removed, because EGTRAIN keeps the newest %1.").arg(RecoveryStore::kMaxKeptInstances);
}

} // namespace

RecoveryPrompt::RecoveryPrompt(const RecoveryScan& scan, QWidget* parent, const QRect& availableGeometry, const QString& crashNote)
	: QDialog(parent), m_candidates(scan.candidates) {
	setObjectName(QStringLiteral("recoveryPrompt"));
	setWindowTitle(QStringLiteral("Recover Unsaved Work"));
	setModal(true);

	const int lineHeight = fontMetrics().height();
	auto* body = new QWidget;
	// A short list takes the spare height of the dialog, so that the context line keeps its place at the top.
	body->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
	auto* column = new QVBoxLayout(body);
	column->setContentsMargins(0, 0, 0, 0);
	column->setSpacing(qMax(8, lineHeight / 2));

	if (!crashNote.isEmpty()) {
		auto* crash = new QLabel(body);
		crash->setObjectName(QStringLiteral("recoveryCrashNote"));
		DialogLayout::setStatus(crash, DialogLayout::Severity::Warning,
			QStringLiteral("EGTRAIN closed unexpectedly. A crash note was saved to:\n%1").arg(QDir::toNativeSeparators(crashNote)));
		crash->setTextInteractionFlags(Qt::TextSelectableByMouse);
		column->addWidget(crash);
	}

	if (scan.removedOverCap > 0) {
		auto* removed = new QLabel(body);
		removed->setObjectName(QStringLiteral("recoveryRemoved"));
		DialogLayout::setStatus(removed, DialogLayout::Severity::Neutral, removedText(scan.removedOverCap));
		column->addWidget(removed);
	}

	// The detail lines start under the text of the radio button.
	const int indent = style()->pixelMetric(QStyle::PM_ExclusiveIndicatorWidth, nullptr, this)
		+ style()->pixelMetric(QStyle::PM_RadioButtonLabelSpacing, nullptr, this);
	auto* group = new QButtonGroup(this);
	QVector<QRadioButton*> selectors;
	for (int index = 0; index < m_candidates.size(); ++index) {
		const RecoveryCandidate& candidate = m_candidates.at(index);
		auto* row = new QFrame(body);
		row->setObjectName(QStringLiteral("recoveryRow"));
		row->setFrameShape(QFrame::StyledPanel);
		auto* rowColumn = new QVBoxLayout(row);
		rowColumn->setSpacing(qMax(4, lineHeight / 4));

		const QString name = caseName(candidate);
		auto* selector = new QRadioButton(row);
		selector->setObjectName(QStringLiteral("recoverySelect"));
		// An ampersand would make the next letter a shortcut.
		const QString shortName = selector->fontMetrics().elidedText(name, Qt::ElideMiddle, lineHeight * kNameWidthInLines);
		selector->setText(QString(shortName).replace(QLatin1Char('&'), QStringLiteral("&&")));
		// The paragraph makes Qt read the tooltip as rich text, so the escaped name shows as typed.
		selector->setToolTip(QStringLiteral("<p>%1</p>").arg(name.toHtmlEscaped()));
		selector->setAccessibleName(name);
		// The row takes the width of the list, so that a name never widens the dialog.
		selector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		group->addButton(selector, index);
		selectors.append(selector);
		rowColumn->addWidget(selector);

		auto* details = new QVBoxLayout;
		details->setContentsMargins(indent, 0, 0, 0);
		details->setSpacing(rowColumn->spacing());
		auto* time = new QLabel(row);
		time->setObjectName(QStringLiteral("recoveryTime"));
		DialogLayout::setHelp(time, autosavedText(candidate.writtenAt));
		details->addWidget(time);
		auto* source = new QLabel(row);
		source->setObjectName(QStringLiteral("recoverySource"));
		DialogLayout::setHelp(source, sourceText(candidate));
		details->addWidget(source);
		auto* note = new QLabel(row);
		note->setObjectName(QStringLiteral("recoveryNote"));
		const bool openable = candidate.state == RecoveryCandidateState::Openable;
		DialogLayout::setStatus(note, openable ? DialogLayout::Severity::Warning : DialogLayout::Severity::Error, noteText(candidate));
		details->addWidget(note);
		rowColumn->addLayout(details);
		column->addWidget(row);

		connect(selector, &QRadioButton::toggled, this, [this, index](bool checked) {
			if (checked)
				selectRow(index);
		});
	}

	column->addStretch(1);

	auto* actions = new QDialogButtonBox(this);
	m_recover = actions->addButton(QStringLiteral("Recover"), QDialogButtonBox::AcceptRole);
	m_recover->setObjectName(QStringLiteral("recoveryRecoverButton"));
	m_discard = actions->addButton(QStringLiteral("Discard"), QDialogButtonBox::DestructiveRole);
	m_discard->setObjectName(QStringLiteral("recoveryDiscardButton"));
	m_later = actions->addButton(QStringLiteral("Decide Later"), QDialogButtonBox::RejectRole);
	m_later->setObjectName(QStringLiteral("recoveryLaterButton"));
	connect(m_recover, &QPushButton::clicked, this, [this] { done(Recover); });
	connect(m_discard, &QPushButton::clicked, this, [this] { discardClicked(); });
	connect(m_later, &QPushButton::clicked, this, [this] { done(Later); });

	const QString context = m_candidates.size() == 1
		? QStringLiteral(
			  "EGTRAIN closed before this case was saved. Recovering opens the copy as an unsaved case. "
			  "The original stays unchanged until you save.")
		: QStringLiteral(
			  "EGTRAIN closed before these cases were saved. Choose one to recover. "
			  "Recovering opens the copy as an unsaved case. The originals stay unchanged until you save. "
			  "Copies you leave are offered again at the next start.");
	DialogLayout::install(*this, QStringLiteral("Recover Unsaved Work"), context, body, actions, availableGeometry);

	// The confirmation sits above the buttons, outside the scrolling list, so that it is always in view.
	m_confirmation = new QLabel(this);
	m_confirmation->setObjectName(QStringLiteral("recoveryConfirmation"));
	DialogLayout::setStatus(m_confirmation, DialogLayout::Severity::Warning, QString());
	auto* dialogColumn = static_cast<QVBoxLayout*>(layout());
	dialogColumn->insertWidget(dialogColumn->count() - 1, m_confirmation);

	int first = -1;
	for (int index = 0; index < m_candidates.size() && first < 0; ++index) {
		if (m_candidates.at(index).state == RecoveryCandidateState::Openable)
			first = index;
	}
	if (first < 0 && !m_candidates.isEmpty())
		first = 0;
	if (first >= 0) {
		selectors.at(first)->setChecked(true);
		selectors.at(first)->setFocus();
	} else
		selectRow(-1);
}

void RecoveryPrompt::selectRow(int index) {
	m_selected = index;
	m_discardPending = false;
	m_discard->setText(QStringLiteral("Discard"));
	DialogLayout::setStatus(m_confirmation, DialogLayout::Severity::Warning, QString());

	const bool selected = index >= 0;
	const bool openable = selected && m_candidates.at(index).state == RecoveryCandidateState::Openable;
	const bool recoverFirst = openable && !originalMayBeNewer(m_candidates.at(index).sourceStatus);
	m_recover->setEnabled(openable);
	m_discard->setEnabled(selected);
	m_recover->setDefault(recoverFirst);
	m_later->setDefault(!recoverFirst);
}

void RecoveryPrompt::discardClicked() {
	if (m_selected < 0)
		return;
	if (m_discardPending) {
		done(Discard);
		return;
	}
	m_discardPending = true;
	m_discard->setText(QStringLiteral("Confirm Discard"));
	DialogLayout::setStatus(m_confirmation, DialogLayout::Severity::Warning,
		QStringLiteral("Delete the recovery copy of \"%1\"? It cannot be brought back.").arg(caseName(m_candidates.at(m_selected))));
}
