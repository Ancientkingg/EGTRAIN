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
	{
		QSettings settings;
		writeFailedUpdateVersion(settings, record->version);
		settings.sync();
	}
	const QString version = formatSemanticVersion(record->version);
	QMessageBox dialog(QMessageBox::Warning, QStringLiteral("EGTRAIN Update"),
		QStringLiteral("The update to EGTRAIN %1 could not be started, so the previous version was restored.")
			.arg(version),
		QMessageBox::Ok, parent);
	dialog.setInformativeText(
		QStringLiteral("Automatic update checks will not offer %1 again. A newer version is offered as usual, "
					   "and Check for Updates in the Help menu still offers %1.")
			.arg(version));
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
