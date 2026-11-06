#include "update/UpdateNotices.h"
#include "update/ReleaseInfo.h"
#include "update/UpdateRecord.h"
#include "update/UpdateSettings.h"
#include "util/Log.h"
#include "widgets/UserPrompts.h"

#include <QCoreApplication>
#include <QMessageBox>
#include <QSettings>

namespace {

const QString kTitle = QStringLiteral("EGTRAIN Update");

// Logs a message about an update in the category update, at warning level when it reports a failure.
void logUpdateMessage(bool failure, const QString& message) {
	if (failure) {
		EG_LOG(Update, Warning) << kTitle.toStdString() << ": " << message.toStdString() << '\n';
	} else {
		EG_LOG(Update, Info) << kTitle.toStdString() << ": " << message.toStdString() << '\n';
	}
}

// Logs and shows a message about the installer, with a warning icon when it reports a failure. The notices run only when
// updates are not suppressed, so the prompts never suppress.
void showInstallerNotice(QWidget* parent, bool failure, const QString& message) {
	logUpdateMessage(failure, message);
	if (failure)
		UserPrompts().showError(parent, kTitle, message, true);
	else
		UserPrompts().showInfo(parent, kTitle, message);
}

} // namespace

void reportInPlaceUpdate(QWidget* parent) {
	if (updatesSuppressedByEnvironment())
		return;
	const std::optional<SemanticVersion> installed =
		parseStableVersion(QCoreApplication::applicationVersion().toStdString());
	const QString path = updateRecordPath();
	if (!installed || path.isEmpty())
		return;
	const std::optional<UpdateRecord> record = takeUpdateRecord(path, *installed);
	if (!record)
		return;
	// A version that failed to start is not offered again. One that could not be applied is: that says
	// nothing against the version.
	if (record->outcome == UpdateOutcome::RolledBack) {
		QSettings settings;
		writeFailedUpdateVersion(settings, record->version);
		settings.sync();
	}
	const UpdateRecordText message = describeUpdateRecord(*record);
	const bool applied = record->outcome == UpdateOutcome::Applied;
	logUpdateMessage(!applied, message.advice.isEmpty() ? message.text : message.text + ' ' + message.advice);
	const QMessageBox::Icon icon = applied ? QMessageBox::Information : QMessageBox::Warning;
	QMessageBox dialog(icon, kTitle, message.text, QMessageBox::Ok, parent);
	dialog.setInformativeText(message.advice);
	dialog.exec();
}

void reportInstallerUpdate(QWidget* parent) {
	if (updatesSuppressedByEnvironment())
		return;
	const std::optional<SemanticVersion> installed =
		parseStableVersion(QCoreApplication::applicationVersion().toStdString());
	const QString path = updateRecordPath();
	if (!installed || path.isEmpty())
		return;
	const std::optional<InstallerRecord> record = takeInstallerRecord(path);
	if (!record)
		return;
	const InstallerCheck check = checkInstallerRecord(*record, *installed);
	const QString expected = formatSemanticVersion(record->version);
	const QString running = formatSemanticVersion(*installed);
	if (check == InstallerCheck::Updated) {
		showInstallerNotice(parent, false, QStringLiteral("EGTRAIN was updated to version %1.").arg(expected));
	} else if (check == InstallerCheck::NotUpdated) {
		showInstallerNotice(parent, true,
			QStringLiteral("The installer for EGTRAIN %1 finished, but this copy of EGTRAIN is still version %2.")
				.arg(expected, running));
	} else if (check == InstallerCheck::Failed) {
		const QString code = record->exitCode ? QStringLiteral(" (exit code %1)").arg(*record->exitCode) : QString();
		showInstallerNotice(parent, true,
			QStringLiteral("The installer for EGTRAIN %1 did not finish%2, so the installation may be incomplete. "
						   "EGTRAIN %3 is running. Download the installer from the release page and run it again.")
				.arg(expected, code, running));
	}
}
