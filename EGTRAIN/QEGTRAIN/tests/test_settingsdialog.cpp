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
#include <QTest>
#include <QTextDocument>

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

// A telemetry build with a collector, so that the window offers the privacy choices.
static TelemetryContext telemetryContext() {
	return TelemetryContext{true, true, true, QStringLiteral("https://example.org/collect"), QStringLiteral("1")};
}

// The Settings window saves what it shows to the user settings and resets it with Restore Defaults, and shows long
// recent paths without scrolling sideways.
class TestSettingsDialog : public QObject {
	Q_OBJECT

private slots:
	void savesSettingsAndRestoresDefaults();
	void elidesLongRecentPath();
};

// Each setting changed in the window is read back by a new settings object, and Restore Defaults resets every one of them.
void TestSettingsDialog::savesSettingsAndRestoresDefaults() {
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	const QString file = dir.filePath(QStringLiteral("settings.ini"));
	const TelemetryContext context = telemetryContext();
	{
		QSettings settings(file, QSettings::IniFormat);
		settings.setValue(kRecentScenesKey, QStringList{QStringLiteral("/cases/a.egscene"), QStringLiteral("/cases/b")});
		settings.sync();
	}

	// Each setting changed in the window is read back by a new settings object.
	{
		QSettings settings(file, QSettings::IniFormat);
		TelemetryConsent consent(settings, context);
		SettingsDialog dialog(settings, consent);
		// The recent cases are listed.
		QCOMPARE(dialog.findChild<QListWidget*>(QStringLiteral("settingsRecentCases"))->count(), 2);
		dialog.findChild<QPushButton*>(QStringLiteral("settingsClearRecentCases"))->click();
		for (const char* name : {"settingsShowPassengers", "settingsAdvancedDetails", "settingsAutomaticUpdateChecks", "settingsUsage",
				 "settingsDiagnostics"})
			dialog.findChild<QCheckBox*>(QString::fromLatin1(name))->setChecked(true);
		dialog.accept();
	}
	{
		QSettings settings(file, QSettings::IniFormat);
		TelemetryConsent consent(settings, context);
		QVERIFY2(!settings.contains(kRecentScenesKey), "Clear removes the recent cases");
		QVERIFY2(settings.value(kShowPassengersKey).toBool(), "show passengers is saved");
		QVERIFY2(settings.value(kAdvancedDetailsKey).toBool(), "advanced details are saved");
		// Automatic update checks are saved.
		QCOMPARE(readUpdateCheckState(settings), UpdateCheckState::Enabled);
		// The privacy choices are saved.
		QVERIFY(consent.usageEnabled());
		QVERIFY(consent.diagnosticsEnabled());
	}

	// Restore Defaults resets every setting of the window.
	{
		QSettings settings(file, QSettings::IniFormat);
		settings.setValue(kRecentScenesKey, QStringList{QStringLiteral("/cases/a.egscene")});
		TelemetryConsent consent(settings, context);
		SettingsDialog dialog(settings, consent);
		// The window lists the saved cases.
		QCOMPARE(dialog.findChild<QListWidget*>(QStringLiteral("settingsRecentCases"))->count(), 1);
		for (const char* name : {"settingsShowPassengers", "settingsAdvancedDetails", "settingsAutomaticUpdateChecks", "settingsUsage",
				 "settingsDiagnostics"})
			QVERIFY2(dialog.findChild<QCheckBox*>(QString::fromLatin1(name))->isChecked(), name);
		dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::RestoreDefaults)->click();
		dialog.accept();
	}
	{
		QSettings settings(file, QSettings::IniFormat);
		TelemetryConsent consent(settings, context);
		QVERIFY2(!settings.contains(kRecentScenesKey), "Restore Defaults clears the recent cases");
		// Restore Defaults turns the display settings off.
		QVERIFY(!settings.value(kShowPassengersKey).toBool());
		QVERIFY(!settings.value(kAdvancedDetailsKey).toBool());
		// Restore Defaults asks about update checks again.
		QCOMPARE(readUpdateCheckState(settings), UpdateCheckState::Unknown);
		// Restore Defaults turns the privacy choices off.
		QVERIFY(!consent.usageEnabled());
		QVERIFY(!consent.diagnosticsEnabled());
	}
}

// A recent path wider than the window shows with its middle elided and the whole path as its tooltip, and nothing
// scrolls sideways. A path that fits shows whole. The other settings start unset, which the window shows as after
// Restore Defaults.
void TestSettingsDialog::elidesLongRecentPath() {
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	const QString file = dir.filePath(QStringLiteral("settings.ini"));
	const QString shortPath = display_path::format(QStringLiteral("/cases/a.egscene"));
	const QString longPath = display_path::format(QStringLiteral("/cases/") + QString(300, QLatin1Char('x')) + QStringLiteral("/end.egscene"));
	QSettings settings(file, QSettings::IniFormat);
	settings.setValue(kRecentScenesKey, QStringList{shortPath, longPath});
	TelemetryConsent consent(settings, telemetryContext());
	SettingsDialog dialog(settings, consent);
	dialog.show();
	QApplication::processEvents();
	const auto* list = dialog.findChild<QListWidget*>(QStringLiteral("settingsRecentCases"));
	if (list == nullptr)
		QFAIL("the window has no recent case list");
	const RowTextProbe probe;
	const QString drawn = probe.drawnText(list, 1);
	// A long recent path has its middle elided and a short one shows whole.
	QCOMPARE_GT(list->fontMetrics().horizontalAdvance(longPath), dialog.width());
	QCOMPARE(probe.drawnText(list, 0), shortPath);
	QVERIFY(drawn.contains(QChar(0x2026)));
	QVERIFY(drawn.startsWith(longPath.left(8)));
	QVERIFY(drawn.endsWith(longPath.right(12)));
	QTextDocument tip;
	tip.setHtml(list->item(1)->toolTip());
	// The tooltip of a long recent path holds the whole path.
	QCOMPARE(tip.toPlainText().remove(QChar(0x200B)), longPath);
	bool scrolls = false;
	for (const QAbstractScrollArea* area : dialog.findChildren<QAbstractScrollArea*>())
		scrolls = scrolls || area->horizontalScrollBar()->isVisible();
	QVERIFY2(!scrolls, "no horizontal scroll bar shows for a long recent path");
}

QTEST_MAIN(TestSettingsDialog)
#include "test_settingsdialog.moc"
