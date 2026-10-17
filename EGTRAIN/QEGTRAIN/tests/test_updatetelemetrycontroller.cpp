#include "app/UpdateTelemetryController.h"

#include "app/MainWindowTestHooks.h"
#include "update/UpdateSettings.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QMenu>
#include <QMessageBox>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>

#include <iostream>
#include <optional>
#include <vector>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

static SceneDiagnostic errorWithCode(const char* code) {
	SceneDiagnostic diagnostic;
	diagnostic.severity = SceneSeverity::Error;
	diagnostic.code = code;
	return diagnostic;
}

static SceneDiagnostic warningWithCode(const char* code) {
	SceneDiagnostic diagnostic = errorWithCode(code);
	diagnostic.severity = SceneSeverity::Warning;
	return diagnostic;
}

// Runs the event loop for a while, so that a dialog that was wrongly opened would be answered.
static void waitFor(int milliseconds) {
	QEventLoop loop;
	QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
	loop.exec();
}

// Presses the button with this text of the first message box that shows, and counts the boxes.
static void pressInMessageBox(int& shown, const QString& buttonText) {
	shown = 0;
	QTimer::singleShot(50, qApp, [&shown, buttonText] {
		for (QWidget* widget : QApplication::topLevelWidgets()) {
			auto* box = qobject_cast<QMessageBox*>(widget);
			if (!box || !box->isVisible())
				continue;
			++shown;
			for (QAbstractButton* button : box->buttons()) {
				if (button->text() == buttonText)
					button->click();
			}
			break;
		}
	});
}

static UpdateTelemetryController* startedController = nullptr;

static bool refuseTelemetry(UpdateTelemetryController& controller) {
	startedController = &controller;
	return false;
}

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	bool ok = true;

	{
		const auto error = [](std::vector<SceneDiagnostic> diagnostics) { return telemetryDiagnosticError(diagnostics); };
		ok &= expect(!error({}), "no diagnostic maps to no error");
		ok &= expect(!error({warningWithCode("scene.json.parse")}), "a warning maps to no error");
		ok &= expect(!error({errorWithCode("scene.save.write")}), "an error whose code is ambiguous maps to no error");
		ok &= expect(error({errorWithCode("scene.version.unsupported")}) == telemetry::Error::UnsupportedFormat,
			"an unsupported version is an unsupported format");
		ok &= expect(error({errorWithCode("scene.json.parse")}) == telemetry::Error::InvalidInput, "a parse error is an invalid input");
		ok &= expect(error({errorWithCode("scene.bundle.file.read")}) == telemetry::Error::IoFailure,
			"a file that cannot be read is an input failure");
		ok &= expect(error({errorWithCode("scene.bundle.temp"), errorWithCode("scene.json.parse")}) == telemetry::Error::InvalidInput,
			"an invalid input outranks a failure to read");
		ok &= expect(error({errorWithCode("scene.json.parse"), errorWithCode("scene.units.unsupported"),
						 errorWithCode("scene.bundle.temp")})
				== telemetry::Error::UnsupportedFormat,
			"an unsupported format outranks the other errors");
	}

	QTemporaryDir settingsDir;
	if (!settingsDir.isValid()) {
		std::cerr << "failed: no temporary directory\n";
		return 1;
	}
	// The settings of the controller are the ones of the test, and an environment that suppresses updates is lifted.
	QCoreApplication::setOrganizationName("EGTRAIN-test");
	QCoreApplication::setApplicationName("test_updatetelemetrycontroller");
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
	QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settingsDir.path());
	const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
	for (const QString& key : environment.keys()) {
		if (key.startsWith("QEGTRAIN_"))
			qunsetenv(key.toLocal8Bit().constData());
	}

	{
		QWidget window;
		UpdateTelemetryController controller(&window, [] { return true; }, [] {});
		ok &= expect(controller.window() == &window && !controller.sender(), "a controller starts without a sender");
		controller.stopTelemetry();
		controller.startUpdateCheck(true);
		controller.maybePromptForUpdateChecks();

		controller.setupUpdateActions(nullptr);
		ok &= expect(!window.findChild<QAction*>("actionCheckForUpdates"), "a controller without a Help menu has no update entries");

		QMenu help;
		controller.setupUpdateActions(&help);
		auto* check = window.findChild<QAction*>("actionCheckForUpdates");
		auto* automatic = window.findChild<QAction*>("actionAutomaticallyCheckForUpdates");
		ok &= expect(check && automatic && help.actions().size() == 3 && help.actions().at(0)->isSeparator()
				&& help.actions().at(1) == check && help.actions().at(2) == automatic,
			"the update entries follow a separator in the Help menu");
		ok &= expect(automatic && automatic->isCheckable() && !automatic->isChecked(),
			"automatic checks are off until the user chose them");

		if (automatic) {
			automatic->setChecked(true);
			QSettings settings;
			ok &= expect(readUpdateCheckState(settings) == UpdateCheckState::Enabled, "turning automatic checks on is saved");
			automatic->setChecked(false);
			QSettings later;
			ok &= expect(readUpdateCheckState(later) == UpdateCheckState::Disabled, "turning automatic checks off is saved");
		}

		int shown = 0;
		pressInMessageBox(shown, "Don't Check Automatically");
		{
			QSettings settings;
			writeUpdateCheckState(settings, UpdateCheckState::Unknown);
			settings.sync();
		}
		controller.maybePromptForUpdateChecks();
		QSettings answered;
		ok &= expect(shown == 1 && readUpdateCheckState(answered) == UpdateCheckState::Disabled,
			"the first start asks about automatic checks and saves the answer");

		pressInMessageBox(shown, "Don't Check Automatically");
		controller.maybePromptForUpdateChecks();
		waitFor(150);
		ok &= expect(shown == 0, "a saved answer is not asked again");
	}

	{
		QWidget window;
		UpdateTelemetryController controller(&window, [] { return true; }, [] {});
		mainWindowTestHooks().telemetryStart = &refuseTelemetry;
		controller.startTelemetryAfterInitialConsent();
		mainWindowTestHooks().telemetryStart = nullptr;
		ok &= expect(startedController == &controller && !controller.sender(),
			"a start that the scripted run refuses leaves telemetry off");
	}

	return ok ? 0 : 1;
}
