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
