#include "widgets/SettingsDialog.h"

#include "telemetry/TelemetryConsent.h"
#include "update/UpdateSettings.h"
#include "widgets/DisplayPath.h"

#include <QAbstractScrollArea>
#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTemporaryDir>
#include <QTextDocument>

#include <iostream>

static bool check(bool ok, const char* text) {
	if (!ok)
		std::cerr << "failed: " << text << '\n';
	return ok;
}

// The text of a list row as the list draws it: the row text elided to the width of the text area of the row.
class RowTextProbe : public QStyledItemDelegate {
public:
	QString drawnText(const QListWidget* list, int row) const {
		const QModelIndex index = list->model()->index(row, 0);
		QStyleOptionViewItem option;
		option.initFrom(list->viewport());
		option.widget = list;
		option.rect = list->visualRect(index);
		option.textElideMode = list->textElideMode();
		initStyleOption(&option, index);
		const QRect textArea = list->style()->subElementRect(QStyle::SE_ItemViewItemText, &option, list);
		return option.fontMetrics.elidedText(option.text, option.textElideMode, textArea.width());
	}
};

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	QTemporaryDir dir;
	if (!dir.isValid())
		return 1;
	const QString file = dir.filePath(QStringLiteral("settings.ini"));
	const TelemetryContext context{true, true, true, QStringLiteral("https://example.org/collect"), QStringLiteral("1")};
	{
		QSettings settings(file, QSettings::IniFormat);
		settings.setValue(kRecentScenesKey, QStringList{QStringLiteral("/cases/a.egscene"), QStringLiteral("/cases/b")});
		settings.sync();
	}
	bool ok = true;

	// Each setting changed in the window is read back by a new settings object.
	{
		QSettings settings(file, QSettings::IniFormat);
		TelemetryConsent consent(settings, context);
		SettingsDialog dialog(settings, consent);
		ok &= check(dialog.findChild<QListWidget*>(QStringLiteral("settingsRecentCases"))->count() == 2, "the recent cases are listed");
		dialog.findChild<QPushButton*>(QStringLiteral("settingsClearRecentCases"))->click();
		for (const char* name : {"settingsShowPassengers", "settingsAdvancedDetails", "settingsAutomaticUpdateChecks", "settingsUsage",
				 "settingsDiagnostics"})
			dialog.findChild<QCheckBox*>(QString::fromLatin1(name))->setChecked(true);
		dialog.accept();
	}
	{
		QSettings settings(file, QSettings::IniFormat);
		TelemetryConsent consent(settings, context);
		ok &= check(!settings.contains(kRecentScenesKey), "Clear removes the recent cases");
		ok &= check(settings.value(kShowPassengersKey).toBool(), "show passengers is saved");
		ok &= check(settings.value(kAdvancedDetailsKey).toBool(), "advanced details are saved");
		ok &= check(readUpdateCheckState(settings) == UpdateCheckState::Enabled, "automatic update checks are saved");
		ok &= check(consent.usageEnabled() && consent.diagnosticsEnabled(), "the privacy choices are saved");
	}

	// Restore Defaults resets every setting of the window.
	{
		QSettings settings(file, QSettings::IniFormat);
		settings.setValue(kRecentScenesKey, QStringList{QStringLiteral("/cases/a.egscene")});
		TelemetryConsent consent(settings, context);
		SettingsDialog dialog(settings, consent);
		ok &= check(dialog.findChild<QListWidget*>(QStringLiteral("settingsRecentCases"))->count() == 1, "the window lists the saved cases");
		for (const char* name : {"settingsShowPassengers", "settingsAdvancedDetails", "settingsAutomaticUpdateChecks", "settingsUsage",
				 "settingsDiagnostics"})
			ok &= check(dialog.findChild<QCheckBox*>(QString::fromLatin1(name))->isChecked(), name);
		dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::RestoreDefaults)->click();
		dialog.accept();
	}
	{
		QSettings settings(file, QSettings::IniFormat);
		TelemetryConsent consent(settings, context);
		ok &= check(!settings.contains(kRecentScenesKey), "Restore Defaults clears the recent cases");
		ok &= check(!settings.value(kShowPassengersKey).toBool() && !settings.value(kAdvancedDetailsKey).toBool(),
			"Restore Defaults turns the display settings off");
		ok &= check(readUpdateCheckState(settings) == UpdateCheckState::Unknown, "Restore Defaults asks about update checks again");
		ok &= check(!consent.usageEnabled() && !consent.diagnosticsEnabled(), "Restore Defaults turns the privacy choices off");
	}

	// A recent path wider than the window shows with its middle elided and the whole path as its tooltip, and nothing
	// scrolls sideways. A path that fits shows whole.
	{
		const QString shortPath = display_path::format(QStringLiteral("/cases/a.egscene"));
		const QString longPath = display_path::format(QStringLiteral("/cases/") + QString(300, QLatin1Char('x')) + QStringLiteral("/end.egscene"));
		QSettings settings(file, QSettings::IniFormat);
		settings.setValue(kRecentScenesKey, QStringList{shortPath, longPath});
		TelemetryConsent consent(settings, context);
		SettingsDialog dialog(settings, consent);
		dialog.show();
		app.processEvents();
		const auto* list = dialog.findChild<QListWidget*>(QStringLiteral("settingsRecentCases"));
		const RowTextProbe probe;
		const QString drawn = probe.drawnText(list, 1);
		ok &= check(list->fontMetrics().horizontalAdvance(longPath) > dialog.width() && probe.drawnText(list, 0) == shortPath
				&& drawn.contains(QChar(0x2026)) && drawn.startsWith(longPath.left(8)) && drawn.endsWith(longPath.right(12)),
			"a long recent path has its middle elided and a short one shows whole");
		QTextDocument tip;
		tip.setHtml(list->item(1)->toolTip());
		ok &= check(tip.toPlainText().remove(QChar(0x200B)) == longPath, "the tooltip of a long recent path holds the whole path");
		bool scrolls = false;
		for (const QAbstractScrollArea* area : dialog.findChildren<QAbstractScrollArea*>())
			scrolls = scrolls || area->horizontalScrollBar()->isVisible();
		ok &= check(!scrolls, "no horizontal scroll bar shows for a long recent path");
	}
	return ok ? 0 : 1;
}
