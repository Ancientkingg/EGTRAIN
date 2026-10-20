#include "app/UpdateTelemetryController.h"
#include "app/MainWindowTestHooks.h"
#include "app/ReleaseNotesText.h"
#include "app/UpdatePromptText.h"
#include "telemetry/TelemetryConsent.h"
#include "telemetry/TelemetryConsentDialog.h"
#include "telemetry/TelemetrySender.h"
#include "update/ReleaseInfo.h"
#include "update/SelfUpdater.h"
#include "update/UpdateChecker.h"
#include "update/UpdateSettings.h"

#include <QAction>
#include <QCoreApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QWidget>

#include <algorithm>
#include <iterator>
#include <limits>
#include <utility>

std::optional<telemetry::Error> telemetryDiagnosticError(const std::vector<SceneDiagnostic>& diagnostics) {
	std::optional<telemetry::Error> selected;
	const auto priority = [](telemetry::Error error) {
		switch (error) {
			case telemetry::Error::UnsupportedFormat: return 0;
			case telemetry::Error::InvalidInput: return 1;
			case telemetry::Error::IoFailure: return 2;
			case telemetry::Error::InternalFailure: return 3;
		}
		return 4;
	};
	static const char* invalidCodes[] = {
		"scene.arc.curvature.invalid",
		"scene.arc.gradient.invalid",
		"scene.arc.speed.invalid",
		"scene.basetime.invalid",
		"scene.basetime.missing",
		"scene.block.length.invalid",
		"scene.buffer.invalid",
		"scene.capacity.runtime",
		"scene.compatibility.schema",
		"scene.composition.empty",
		"scene.connection.speed.invalid",
		"scene.delay.invalid",
		"scene.duration.invalid",
		"scene.duration.missing",
		"scene.dwell.exceeds_window",
		"scene.dwell.invalid",
		"scene.entrance.conflict",
		"scene.entrance.occurrence.out_of_horizon",
		"scene.entrance.station",
		"scene.entrance.timetable",
		"scene.field.missing",
		"scene.id.duplicate",
		"scene.id.empty",
		"scene.id.reserved",
		"scene.incident.fields",
		"scene.incident.speed",
		"scene.incident.type",
		"scene.incident.window",
		"scene.item.invalid",
		"scene.json.parse",
		"scene.name.path",
		"scene.native.block.clipped",
		"scene.node.coordinate.invalid",
		"scene.occurrence.invalid",
		"scene.passenger.continuity",
		"scene.passenger.leg.order",
		"scene.passenger.leg.stop",
		"scene.passenger.legs.empty",
		"scene.passenger.occurrence.out_of_horizon",
		"scene.passenger.window",
		"scene.performance.invalid",
		"scene.platform.capacity.invalid",
		"scene.platform.length.invalid",
		"scene.platform.node.conflict",
		"scene.platform.nodes.none",
		"scene.platform.width.invalid",
		"scene.platforms.none",
		"scene.recovery.invalid",
		"scene.ref.ambiguous",
		"scene.ref.platform",
		"scene.ref.platform.route",
		"scene.ref.stop.order",
		"scene.ref.stop.route",
		"scene.ref.unresolved",
		"scene.repeat.count.invalid",
		"scene.repeat.invalid",
		"scene.repeat.step.invalid",
		"scene.route.direction",
		"scene.route.disconnected",
		"scene.route.empty",
		"scene.route.region_jump",
		"scene.route_choice.passengers.none",
		"scene.routes.none",
		"scene.scenario.default.missing",
		"scene.scenarios.none",
		"scene.section.missing",
		"scene.services.none",
		"scene.signal.binding.missing",
		"scene.signal.binding.unresolved",
		"scene.signalling_area.conflict",
		"scene.signalling_area.level",
		"scene.signalling_area.range",
		"scene.speed.invalid",
		"scene.station.anchor.missing",
		"scene.station.position.invalid",
		"scene.stations.none",
		"scene.stop.off_route.context",
		"scene.time.departure.missing",
		"scene.time.entry.invalid",
		"scene.time.invalid",
		"scene.time.order",
		"scene.topology.ambiguous",
		"scene.topology.arcs.missing",
		"scene.topology.arcs.none",
		"scene.topology.blocks.missing",
		"scene.topology.blocks.none",
		"scene.topology.disconnected",
		"scene.topology.loop",
		"scene.topology.nodes.missing",
		"scene.topology.nodes.none",
		"scene.topology.order",
		"scene.topology.track",
		"scene.topology.tracks.none",
		"scene.train.physical.missing",
		"scene.train.traction.empty",
		"scene.train.traction.interval",
		"scene.train.traction.order",
		"scene.train.traction.overlap",
		"scene.trains.none",
		"scene.version.missing",
	};
	for (const auto& diagnostic : diagnostics) {
		if (diagnostic.severity != SceneSeverity::Error) continue;
		std::optional<telemetry::Error> error;
		if (diagnostic.code == "scene.version.unsupported" || diagnostic.code == "scene.units.unsupported")
			error = telemetry::Error::UnsupportedFormat;
		else if (std::any_of(std::begin(invalidCodes), std::end(invalidCodes),
					 [&](const char* code) { return diagnostic.code == code; }))
			error = telemetry::Error::InvalidInput;
		else if (diagnostic.code == "scene.bundle.file.read" || diagnostic.code == "scene.bundle.temp")
			error = telemetry::Error::IoFailure;
		// Ambiguous compatibility.manifest/bundle.schema/save.write/bundle.write codes are omitted.
		if (error && (!selected || priority(*error) < priority(*selected))) selected = error;
	}
	return selected;
}

UpdateTelemetryController::UpdateTelemetryController(QWidget* window, std::function<bool()> askToSaveScene,
	std::function<void()> beforeQuit)
	: m_window(window), m_askToSaveScene(std::move(askToSaveScene)), m_beforeQuit(std::move(beforeQuit)) {}

UpdateTelemetryController::~UpdateTelemetryController() = default;

telemetry::OperationObservation UpdateTelemetryController::captureOperation() const noexcept {
	return telemetry::OperationObservation(m_telemetrySender.get());
}

void UpdateTelemetryController::stopTelemetry() {
	if (m_telemetrySender) m_telemetrySender->stop();
}

void UpdateTelemetryController::setupUpdateActions(QMenu* helpMenu) {
	if (!helpMenu || updatesSuppressedByEnvironment())
		return;

	m_checkForUpdatesAction = new QAction(QStringLiteral("Check for Updates..."), m_window);
	m_checkForUpdatesAction->setObjectName(QStringLiteral("actionCheckForUpdates"));
	m_automaticUpdateChecksAction = new QAction(
		QStringLiteral("Automatically Check for Updates"), m_window);
	m_automaticUpdateChecksAction->setObjectName(QStringLiteral("actionAutomaticallyCheckForUpdates"));
	m_automaticUpdateChecksAction->setCheckable(true);
	QSettings settings;
	{
		const QSignalBlocker blocker(m_automaticUpdateChecksAction);
		m_automaticUpdateChecksAction->setChecked(
			readUpdateCheckState(settings) == UpdateCheckState::Enabled);
	}
	helpMenu->addSeparator();
	helpMenu->addAction(m_checkForUpdatesAction);
	helpMenu->addAction(m_automaticUpdateChecksAction);

	m_updateChecker = new UpdateChecker(this);
	m_selfUpdater = new SelfUpdater(this);
	connect(m_checkForUpdatesAction, &QAction::triggered, this,
		[this]() { startUpdateCheck(true); });
	connect(m_automaticUpdateChecksAction, &QAction::toggled, this, [](bool checked) {
		QSettings preferences;
		writeUpdateCheckState(preferences, checked ? UpdateCheckState::Enabled : UpdateCheckState::Disabled);
		preferences.sync();
	});
	connect(m_updateChecker, &UpdateChecker::finished, this, &UpdateTelemetryController::handleUpdateCheckFinished);
	connect(m_selfUpdater, &SelfUpdater::progress, this, [this](qint64 received, qint64 total) {
		if (!m_updateProgress)
			return;
		if (total > 0) {
			const qint64 maxValue = std::numeric_limits<int>::max();
			m_updateProgress->setRange(0, total > maxValue ? static_cast<int>(maxValue) : static_cast<int>(total));
			m_updateProgress->setValue(received > maxValue ? static_cast<int>(maxValue) : static_cast<int>(received));
		} else {
			m_updateProgress->setRange(0, 0);
		}
	});
	connect(m_selfUpdater, &SelfUpdater::finished, this, &UpdateTelemetryController::handleSelfUpdateFinished);
	connect(m_selfUpdater, &SelfUpdater::preparing, this, [this](const QString& version) {
		if (!m_updateProgress)
			return;
		m_updateProgress->setLabelText(QStringLiteral("Verifying and preparing EGTRAIN %1...")
				.arg(version));
		m_updateProgress->setCancelButton(nullptr);
		m_updateProgress->setRange(0, 0);
	});
}

void UpdateTelemetryController::startTelemetryAfterInitialConsent() {
	if (m_telemetrySender) return;
	try {
		if (const auto telemetryStart = mainWindowTestHooks().telemetryStart) {
			if (!telemetryStart(*this)) return;
		} else {
			const TelemetryContext context = applicationTelemetryContext();
			if (!context.capable()) return;
			m_telemetrySettings.reset(new QSettings);
			m_telemetryConsent.reset(new TelemetryConsent(*m_telemetrySettings, context));
			telemetry::Application metadata;
			metadata.version = QCoreApplication::applicationVersion();
			m_telemetrySender.reset(new telemetry::TelemetrySender(context, metadata,
				QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
					.filePath(QStringLiteral("Telemetry"))));
		}
		m_telemetrySender->requestInteractiveSession();
		connect(m_telemetryConsent.get(), &TelemetryConsent::revoked, this, [this](bool usage, bool diagnostics) {
				if (m_telemetrySender) m_telemetrySender->invalidateConsent(usage, diagnostics); }, Qt::DirectConnection);
		connect(
			m_telemetryConsent.get(), &TelemetryConsent::receiverChanged, this,
			[this] { if (m_telemetrySender) m_telemetrySender->invalidateReceiver(); }, Qt::DirectConnection);
	} catch (...) {
		m_telemetrySender.reset();
		m_telemetryConsent.reset();
		m_telemetrySettings.reset();
	}
}

void UpdateTelemetryController::showPrivacySettings(bool initialPrompt) {
	QSettings previewSettings;
	TelemetryContext context = applicationTelemetryContext();
	const auto& privacyPreview = mainWindowTestHooks().privacyPreview;
	const bool preview = privacyPreview && privacyPreview(context, initialPrompt);
	TelemetryConsent previewConsent(previewSettings, context);
	TelemetryConsent& consent = preview || !m_telemetryConsent ? previewConsent : *m_telemetryConsent;
	if (!consent.available()) {
		if (!initialPrompt)
			QMessageBox::information(m_window, QStringLiteral("Privacy & diagnostics"),
				QStringLiteral("Usage statistics and diagnostics are unavailable in this build or run. Nothing is collected or sent."));
		return;
	}
	if (initialPrompt && !consent.promptRequired()) return;
	TelemetryConsentDialog dialog(consent, context.domain(), initialPrompt, m_window);
	if (const auto& dialogShown = mainWindowTestHooks().dialogShown)
		dialogShown(dialog, "privacySettings");
	dialog.exec();
	if (!preview && m_telemetrySender) m_telemetrySender->requestConsentRefresh();
}

void UpdateTelemetryController::maybePromptForUpdateChecks() {
	if (!m_updateChecker || updatesSuppressedByEnvironment())
		return;
	QSettings settings;
	const UpdateCheckState state = readUpdateCheckState(settings);
	if (state == UpdateCheckState::Unknown) {
		QMessageBox dialog(QMessageBox::Question,
			QStringLiteral("Automatically Check for EGTRAIN Updates?"),
			update_prompt::automaticChecksQuestion(),
			QMessageBox::NoButton, m_window);
		dialog.setInformativeText(update_prompt::automaticChecksDetail());
		QPushButton* enableButton = dialog.addButton(QStringLiteral("Check Automatically"),
			QMessageBox::AcceptRole);
		dialog.addButton(QStringLiteral("Don't Check Automatically"), QMessageBox::RejectRole);
		dialog.setDefaultButton(enableButton);
		dialog.exec();
		const UpdateCheckState chosen = dialog.clickedButton() == enableButton
			? UpdateCheckState::Enabled
			: UpdateCheckState::Disabled;
		writeUpdateCheckState(settings, chosen);
		settings.sync();
		if (m_automaticUpdateChecksAction) {
			const QSignalBlocker blocker(m_automaticUpdateChecksAction);
			m_automaticUpdateChecksAction->setChecked(chosen == UpdateCheckState::Enabled);
		}
		if (chosen == UpdateCheckState::Enabled)
			startUpdateCheck(false);
		return;
	}
	if (state == UpdateCheckState::Enabled)
		startUpdateCheck(false);
}

void UpdateTelemetryController::startUpdateCheck(bool manual) {
	if (!m_updateChecker || updatesSuppressedByEnvironment())
		return;
	QSettings settings;
	if (!shouldCheckForUpdates(readUpdateCheckState(settings), manual))
		return;
	if (!manual && m_updateChecker->isChecking())
		return;
	m_manualUpdateCheck = manual;
	m_updateChecker->check();
}

void UpdateTelemetryController::handleUpdateCheckFinished(const UpdateCheckResult& result) {
	const bool manual = m_manualUpdateCheck;
	m_manualUpdateCheck = false;
	if (!result.success || !result.release) {
		if (manual) {
			QMessageBox::warning(m_window, QStringLiteral("Check for Updates"),
				result.error.isEmpty() ? QStringLiteral("Could not check for updates.") : result.error);
		} else {
			qWarning().noquote() << "Automatic update check failed:"
								 << (result.error.isEmpty() ? QStringLiteral("unknown error") : result.error);
		}
		return;
	}

	const std::optional<SemanticVersion> current =
		parseStableVersion(QCoreApplication::applicationVersion().toStdString());
	if (!current) {
		const QString error = QStringLiteral("The installed application version is invalid.");
		if (manual)
			QMessageBox::warning(m_window, QStringLiteral("Check for Updates"), error);
		else
			qWarning().noquote() << "Automatic update check failed:" << error;
		return;
	}
	if (!isUpdateAvailable(*current, *result.release)) {
		if (manual)
			QMessageBox::information(m_window, QStringLiteral("Check for Updates"),
				QStringLiteral("EGTRAIN %1 is up to date.").arg(formatSemanticVersion(*current)));
		return;
	}
	{
		QSettings settings;
		if (!shouldOfferRelease(result.release->version, readFailedUpdateVersion(settings), manual))
			return;
	}

	const StableRelease& release = *result.release;
	QString availability;
	if (m_selfUpdater && !m_selfUpdater->canSelfUpdate(release)) {
		const SelfUpdateCapability capability = m_selfUpdater->capability();
		availability = capability.supported
			? QStringLiteral("\n\nNo automatic update package is available for this platform. Use the release page to update manually.")
			: QStringLiteral("\n\n%1 Use the release page to update manually.").arg(capability.reason);
	} else if (m_selfUpdater && m_selfUpdater->capability().usesInstaller) {
		availability = QStringLiteral("\n\nUpdate and Restart runs the EGTRAIN installer. EGTRAIN closes while the installer "
									  "updates this installation and starts again afterwards.");
	}
	QMessageBox dialog(QMessageBox::Information,
		QStringLiteral("EGTRAIN %1 is Available").arg(formatSemanticVersion(release.version)),
		update_prompt::availableText(formatSemanticVersion(release.version), formatSemanticVersion(*current)),
		QMessageBox::NoButton, m_window);
	setMarkdownInformativeText(dialog,
		(release.notes.isEmpty() ? QStringLiteral("No release notes were provided.") : releaseNotesMarkdown(release.notes))
			+ availability);
	QPushButton* updateButton = nullptr;
	if (m_selfUpdater && m_selfUpdater->canSelfUpdate(release))
		updateButton = dialog.addButton(QStringLiteral("Update and Restart"), QMessageBox::AcceptRole);
	QPushButton* openButton = dialog.addButton(QStringLiteral("Open Release Page"),
		QMessageBox::AcceptRole);
	dialog.addButton(QStringLiteral("Later"), QMessageBox::RejectRole);
	QPushButton* stopButton = dialog.addButton(QStringLiteral("Stop Checking"),
		QMessageBox::DestructiveRole);
	// Without a default, macOS makes the destructive button the one that Enter presses.
	dialog.setDefaultButton(updateButton ? updateButton : openButton);
	dialog.exec();
	QAbstractButton* clickedButton = dialog.clickedButton();
	if (updateButton && clickedButton == updateButton) {
		startSelfUpdate(release);
	} else if (clickedButton == stopButton) {
		QSettings settings;
		writeUpdateCheckState(settings, UpdateCheckState::Disabled);
		settings.sync();
		if (m_automaticUpdateChecksAction) {
			const QSignalBlocker blocker(m_automaticUpdateChecksAction);
			m_automaticUpdateChecksAction->setChecked(false);
		}
	} else if (clickedButton == openButton
		&& !QDesktopServices::openUrl(release.releasePage)) {
		QMessageBox::warning(m_window, QStringLiteral("Open Release Page"),
			QStringLiteral("Could not open the release page:\n%1").arg(release.releasePage.toString()));
	}
}

void UpdateTelemetryController::startSelfUpdate(const StableRelease& release) {
	if (!m_selfUpdater || m_selfUpdater->isBusy() || !m_askToSaveScene())
		return;
	m_updateProgress = new QProgressDialog(QStringLiteral("Downloading EGTRAIN %1...")
											   .arg(formatSemanticVersion(release.version)),
		QStringLiteral("Cancel"), 0, 0, m_window);
	m_updateProgress->setWindowTitle(QStringLiteral("EGTRAIN Update"));
	m_updateProgress->setWindowModality(Qt::WindowModal);
	m_updateProgress->setAutoClose(false);
	m_updateProgress->setAutoReset(false);
	m_updateProgress->setMinimumDuration(0);
	connect(m_updateProgress, &QProgressDialog::canceled, this, [this]() {
		if (!m_selfUpdater)
			return;
		if (m_selfUpdater->isPreparing()) {
			// Preparation cannot be interrupted safely; Esc or a stray cancel
			// must not hide the dialog while the update is still progressing.
			if (m_updateProgress && !m_updateProgress->isVisible())
				m_updateProgress->show();
			return;
		}
		m_selfUpdater->cancel();
	});
	m_updateProgress->show();
	m_selfUpdater->start(release);
}

void UpdateTelemetryController::handleSelfUpdateFinished(bool success, const QString& error) {
	if (m_updateProgress) {
		m_updateProgress->close();
		m_updateProgress->deleteLater();
		m_updateProgress = nullptr;
	}
	if (!success) {
		QMessageBox::warning(m_window, QStringLiteral("EGTRAIN Update"),
			error.isEmpty() ? QStringLiteral("The update was not installed. EGTRAIN is unchanged.") : error);
		return;
	}
	const bool installer = m_selfUpdater && m_selfUpdater->capability().usesInstaller;
	QMessageBox::information(m_window, QStringLiteral("EGTRAIN Update"),
		installer ? QStringLiteral("The installer is ready. EGTRAIN will close now, run the installer and start again.")
				  : QStringLiteral("The update is ready. EGTRAIN will restart now."));
	if (!m_selfUpdater || !m_selfUpdater->restart()) {
		QMessageBox::warning(m_window, QStringLiteral("EGTRAIN Update"),
			QStringLiteral("Could not start the update installer. EGTRAIN is unchanged."));
		return;
	}
	// The scene was saved or discarded before the download started, and the progress dialog has kept the window from edits.
	m_beforeQuit();
	QCoreApplication::quit();
}
