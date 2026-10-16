#include "update/UpdateNotices.h"
#include "update/ReleaseInfo.h"
#include "update/UpdateRecord.h"
#include "update/UpdateSettings.h"

#include <QCoreApplication>
#include <QMessageBox>
#include <QSettings>

void reportRolledBackUpdate(QWidget* parent) {
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
	QMessageBox dialog(QMessageBox::Warning, QStringLiteral("EGTRAIN Update"), message.text, QMessageBox::Ok, parent);
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
		QMessageBox::information(parent, QStringLiteral("EGTRAIN Update"),
			QStringLiteral("EGTRAIN was updated to version %1.").arg(expected));
	} else if (check == InstallerCheck::NotUpdated) {
		QMessageBox::warning(parent, QStringLiteral("EGTRAIN Update"),
			QStringLiteral("The installer for EGTRAIN %1 finished, but this copy of EGTRAIN is still version %2.")
				.arg(expected, running));
	} else if (check == InstallerCheck::Failed) {
		const QString code = record->exitCode ? QStringLiteral(" (exit code %1)").arg(*record->exitCode) : QString();
		QMessageBox::warning(parent, QStringLiteral("EGTRAIN Update"),
			QStringLiteral("The installer for EGTRAIN %1 did not finish%2, so the installation may be incomplete. "
						   "EGTRAIN %3 is running. Download the installer from the release page and run it again.")
				.arg(expected, code, running));
	}
}
