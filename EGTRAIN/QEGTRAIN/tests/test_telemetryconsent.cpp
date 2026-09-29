#include "telemetry/TelemetryConsent.h"
#include "telemetry/TelemetryConsentDialog.h"
#include "update/UpdateSettings.h"

#include <QApplication>
#include <QCheckBox>
#include <QFile>
#include <QLabel>
#include <QLockFile>
#include <QPushButton>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>
#include <QUrl>
#include <iostream>

static bool check(bool ok, const char* text) {
    if (!ok) std::cerr << "failed: " << text << '\n';
    return ok;
}

int main(int argc, char** argv) {
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--core-only")) {
        QCoreApplication core(argc, argv);
        return applicationTelemetryContext().interactive ? 1 : 0;
    }
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--toggle-diagnostics")) {
        QCoreApplication core(argc, argv);
        QSettings helperSettings(QString::fromLocal8Bit(argv[2]), QSettings::IniFormat);
        TelemetryContext helperContext{true, true, true, QStringLiteral("https://example.org/collect"), QStringLiteral("1")};
        TelemetryConsent helper(helperSettings, helperContext);
        return helper.save(false, false) && helper.save(false, true) ? 0 : 1;
    }
    QApplication app(argc, argv);
    QTemporaryDir dir;
    if (!dir.isValid()) return 1;
    QSettings settings(dir.filePath("consent.ini"), QSettings::IniFormat);
    TelemetryContext context{true, true, true, QStringLiteral("https://example.org/collect"), QStringLiteral("1")};
    bool ok = true;
    for (int mask = 0; mask < 4; ++mask) {
        settings.clear(); settings.sync();
        TelemetryConsent consent(settings, context);
        const bool usage = (mask & 1) != 0, diagnostics = (mask & 2) != 0;
        ok &= check(consent.promptRequired() && !consent.usageEnabled()
            && !consent.diagnosticsEnabled() && consent.usageInstallationId().isEmpty(),
            "unsaved prechecked controls cannot grant consent or an ID");
        TelemetryConsentDialog dialog(consent, context.domain(), true);
        ok &= check(dialog.usageCheckBox()->isChecked() && dialog.diagnosticsCheckBox()->isChecked(),
            "initial options are prechecked");
        dialog.usageCheckBox()->setChecked(usage);
        dialog.diagnosticsCheckBox()->setChecked(diagnostics);
        ok &= check(!consent.usageEnabled() && !consent.diagnosticsEnabled()
            && consent.usageInstallationId().isEmpty(), "changing prechecked controls does not grant consent");
        dialog.findChild<QPushButton*>(QStringLiteral("saveTelemetryChoices"))->click();
        QSettings reopened(dir.filePath("consent.ini"), QSettings::IniFormat);
        TelemetryConsent restart(reopened, context);
        ok &= check(!restart.promptRequired() && restart.usageEnabled() == usage
            && restart.diagnosticsEnabled() == diagnostics
            && restart.usageInstallationId().isEmpty() == !usage,
            "all four combinations survive restart independently");
        TelemetryConsentDialog later(restart, context.domain(), false);
        ok &= check(later.usageCheckBox()->isChecked() == usage
            && later.diagnosticsCheckBox()->isChecked() == diagnostics,
            "settings reopen shows saved choices");
        ok &= check(later.findChild<QLabel*>(QStringLiteral("dialogContext"))->text()
            .contains(QStringLiteral("saved choices remain active")),
            "settings reopening describes saved choices as active until saved changes");
        static_cast<QDialog&>(later).reject();
    }
    for (int dismissal = 0; dismissal < 3; ++dismissal) {
        settings.clear(); settings.sync();
        TelemetryConsent consent(settings, context);
        TelemetryConsentDialog dialog(consent, context.domain(), true);
        if (dismissal == 0)
            dialog.findChild<QPushButton*>(QStringLiteral("dismissTelemetryChoices"))->click();
        else if (dismissal == 1)
            static_cast<QDialog&>(dialog).reject(); // Escape
        else {
            dialog.show();
            app.processEvents();
            dialog.close();
        }
        ok &= check(!consent.promptRequired() && !consent.usageEnabled()
            && !consent.diagnosticsEnabled() && consent.usageInstallationId().isEmpty(),
            "Not now, Escape and window close persist disabled and handled");
    }
    settings.clear(); settings.sync();
    TelemetryConsent consent(settings, context);
    struct FakeSink {
        bool usageQueued = true, diagnosticsQueued = true, usageActive = true, diagnosticsActive = true;
        bool oldEndpointQueued = true;
    } sink;
    QObject::connect(&consent, &TelemetryConsent::revoked, &consent, [&](bool usage, bool diagnostics) {
        if (usage) sink.usageQueued = sink.usageActive = false;
        if (diagnostics) sink.diagnosticsQueued = sink.diagnosticsActive = false;
    }, Qt::DirectConnection);
    QObject::connect(&consent, &TelemetryConsent::receiverChanged, &consent,
                     [&] { sink.oldEndpointQueued = false; }, Qt::DirectConnection);
    consent.save(true, true);
    const QString firstId = consent.usageInstallationId();
    consent.save(false, true);
    ok &= check(!sink.usageQueued && !sink.usageActive && sink.diagnosticsQueued
        && sink.diagnosticsActive && consent.usageInstallationId().isEmpty(),
        "usage revocation synchronously clears its fake queue and active upload");
    consent.save(true, false);
    ok &= check(!sink.diagnosticsQueued && !sink.diagnosticsActive
        && !consent.usageInstallationId().isEmpty() && consent.usageInstallationId() != firstId,
        "diagnostics revocation is independent and re-enabling usage makes a new ID");
    QSettings concurrentSettings(dir.filePath("consent.ini"), QSettings::IniFormat);
    TelemetryConsent concurrent(concurrentSettings, context);
    const QString currentId = concurrent.usageInstallationId();
    consent.save(false, false);
    ok &= check(!concurrent.usageEnabled() && concurrent.usageInstallationId().isEmpty()
        && !currentId.isEmpty(), "second instance detects revocation at collection boundary");
    consent.save(true, false);
    settings.clear(); settings.sync();
    QSettings writerSettings(dir.filePath("consent.ini"), QSettings::IniFormat);
    TelemetryConsent writer(writerSettings, context);
    writer.save(false, true);
    QSettings listenerSettings(dir.filePath("consent.ini"), QSettings::IniFormat);
    TelemetryConsent listener(listenerSettings, context);
    struct DiagnosticsSink { bool queued = true, active = true; } diagnosticsSink;
    bool checkingReentry = false;
    int nestedRevocations = 0;
    QObject::connect(&listener, &TelemetryConsent::revoked, &listener,
        [&](bool, bool diagnostics) {
            if (diagnostics) diagnosticsSink.queued = diagnosticsSink.active = false;
            // A direct queue/abort slot must be able to recheck consent safely.
            if (diagnostics && !checkingReentry) {
                checkingReentry = true;
                listener.diagnosticsEnabled();
                checkingReentry = false;
            } else if (diagnostics) {
                ++nestedRevocations;
            }
        }, Qt::DirectConnection);
    writer.save(false, false);
    writer.save(false, true);
    ok &= check(listener.diagnosticsEnabled() && !diagnosticsSink.queued && !diagnosticsSink.active,
        "concurrent disable/re-enable clears old diagnostics reports");
    diagnosticsSink = {};
    TelemetryContext intermediate = context;
    intermediate.endpoint = QStringLiteral("https://other.example/collect");
    QSettings intermediateSettings(dir.filePath("consent.ini"), QSettings::IniFormat);
    TelemetryConsent intermediateConsent(intermediateSettings, intermediate);
    intermediateConsent.promptRequired();
    intermediateConsent.save(false, true);
    QSettings backSettings(dir.filePath("consent.ini"), QSettings::IniFormat);
    TelemetryConsent back(backSettings, context);
    back.promptRequired();
    back.save(false, true);
    ok &= check(listener.diagnosticsEnabled() && !diagnosticsSink.queued && !diagnosticsSink.active,
        "receiver A-B-A clears old diagnostics reports");
    diagnosticsSink = {};
    TelemetryContext otherTerms = context;
    otherTerms.termsVersion = QStringLiteral("2");
    QSettings otherTermsSettings(dir.filePath("consent.ini"), QSettings::IniFormat);
    TelemetryConsent otherTermsConsent(otherTermsSettings, otherTerms);
    otherTermsConsent.promptRequired();
    otherTermsConsent.save(false, true);
    QSettings backTermsSettings(dir.filePath("consent.ini"), QSettings::IniFormat);
    TelemetryConsent backTerms(backTermsSettings, context);
    backTerms.promptRequired();
    backTerms.save(false, true);
    ok &= check(listener.diagnosticsEnabled() && !diagnosticsSink.queued && !diagnosticsSink.active,
        "terms 1-2-1 clears old diagnostics reports");
    ok &= check(nestedRevocations == 0, "direct-slot accessor does not recursively revoke");
    QTemporaryDir raceDir;
    if (!raceDir.isValid()) return 1;
    const QString raceFile = raceDir.filePath(QStringLiteral("consent.ini"));
    QSettings raceSettings(raceFile, QSettings::IniFormat);
    TelemetryConsent raceWriter(raceSettings, context);
    raceWriter.save(false, true);
    QSettings raceListenerSettings(raceFile, QSettings::IniFormat);
    TelemetryConsent raceListener(raceListenerSettings, context);
    bool oldReportsQueued = true;
    QObject::connect(&raceListener, &TelemetryConsent::revoked, &raceListener,
        [&](bool, bool diagnostics) { if (diagnostics) oldReportsQueued = false; }, Qt::DirectConnection);
    bool helperCompleted = false;
    raceWriter.setBeforeWriteForTesting([&] {
        QProcess helper;
        helper.start(QCoreApplication::applicationFilePath(),
            {QStringLiteral("--toggle-diagnostics"), raceFile});
        helperCompleted = helper.waitForFinished(10000) && helper.exitStatus() == QProcess::NormalExit
            && helper.exitCode() == 0;
    });
    const bool overlappingSave = raceWriter.save(false, true);
    ok &= check(helperCompleted && overlappingSave && raceListener.diagnosticsEnabled()
        && !oldReportsQueued, "overlapping writer must not restore revoked diagnostics generation");
    QLockFile busyLock(raceFile + QStringLiteral(".telemetry-consent.lock"));
    ok &= check(busyLock.tryLock(0), "test can hold isolated consent lock");
    QSettings busySettings(raceFile, QSettings::IniFormat);
    TelemetryConsent blocked(busySettings, context);
    ok &= check(!blocked.diagnosticsEnabled() && !blocked.save(false, true),
        "lock contention fails closed without granting consent");
    busyLock.unlock();
    writer.save(false, true);
    QObject::connect(&writer, &TelemetryConsent::revoked, &writer,
        [&](bool, bool diagnostics) {
            if (diagnostics) ok &= check(!writer.diagnosticsEnabled(),
                "same-instance revoked slot sees disabled state");
        }, Qt::DirectConnection);
    writer.save(false, false);
    ok &= check(writer.save(true, false), "usage enabled before upgrade");
    const QString upgradeId = writer.usageInstallationId();
    ok &= check(!upgradeId.isEmpty(), "upgrade test captures an enabled usage ID");
    TelemetryContext upgraded = context;
    QSettings upgradedSettings(dir.filePath("consent.ini"), QSettings::IniFormat);
    TelemetryConsent sameReceiver(upgradedSettings, upgraded);
    ok &= check(!sameReceiver.promptRequired() && sameReceiver.usageEnabled()
        && !sameReceiver.diagnosticsEnabled() && sameReceiver.usageInstallationId() == upgradeId,
        "same receiver and terms retain enabled usage and its ID on application upgrade");
    TelemetryContext changed = context; changed.endpoint = QStringLiteral("https://other.example/collect");
    QSettings otherSettings(dir.filePath("consent.ini"), QSettings::IniFormat);
    TelemetryConsent other(otherSettings, changed);
    QObject::connect(&other, &TelemetryConsent::receiverChanged, &other,
                     [&] { sink.oldEndpointQueued = false; }, Qt::DirectConnection);
    ok &= check(other.promptRequired() && !other.usageEnabled() && other.usageInstallationId().isEmpty()
        && !sink.oldEndpointQueued, "receiver change resets choices and signals old queue discard");
    other.save(true, false);
    changed.termsVersion = QStringLiteral("2");
    QSettings termsSettings(dir.filePath("consent.ini"), QSettings::IniFormat);
    TelemetryConsent terms(termsSettings, changed);
    ok &= check(terms.promptRequired() && terms.usageInstallationId().isEmpty(),
        "changed collection terms require confirmation");
    TelemetryContext unavailable = context;
    for (int mode = 0; mode < 5; ++mode) {
        unavailable = context;
        if (mode == 0) unavailable.packaged = false;
        if (mode == 1) unavailable.enabled = false;
        if (mode == 2) unavailable.interactive = false;
        if (mode == 3) unavailable.endpoint.clear();
        if (mode == 4) unavailable.endpoint = QStringLiteral("http://example.org/collect");
        QSettings restricted(dir.filePath("consent.ini"), QSettings::IniFormat);
        TelemetryConsent denied(restricted, unavailable);
        denied.save(true, true);
        ok &= check(!denied.available() && !denied.promptRequired() && !denied.usageEnabled()
            && !denied.diagnosticsEnabled() && denied.usageInstallationId().isEmpty(),
            "suppressed or invalid context never grants consent or ID");
    }
    QProcessEnvironment clean;
    ok &= check(telemetryInteractiveEnvironment(clean, QStringLiteral("cocoa")),
        "interactive environment has an unsuppressed baseline");
    for (const QString& key : {QStringLiteral("CI"), QStringLiteral("GITHUB_ACTIONS"),
         QStringLiteral("QEGTRAIN_AUTOSTART"), QStringLiteral("QEGTRAIN_E2E_SETTINGS_DIR")}) {
        QProcessEnvironment suppressed = clean;
        suppressed.insert(key, QStringLiteral("1"));
        ok &= check(!telemetryInteractiveEnvironment(suppressed, QStringLiteral("cocoa")),
            "automation marker suppresses interactive context independent of offscreen");
    }
    ok &= check(!telemetryInteractiveEnvironment(clean, QStringLiteral("offscreen"))
        && !applicationTelemetryContext().interactive, "offscreen application is suppressed");
    QProcess coreProcess;
    QProcessEnvironment coreEnvironment = QProcessEnvironment::systemEnvironment();
    for (const QString& key : coreEnvironment.keys())
        if (key.startsWith(QStringLiteral("QEGTRAIN_E2E_"))) coreEnvironment.remove(key);
    for (const QString& key : {QStringLiteral("CI"), QStringLiteral("GITHUB_ACTIONS"),
         QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("QEGTRAIN_AUTOSTART"),
         QStringLiteral("QEGTRAIN_STARTUP_TIMING"), QStringLiteral("QEGTRAIN_PLAYBACK_PROFILE")})
        coreEnvironment.remove(key);
    coreProcess.setProcessEnvironment(coreEnvironment);
    coreProcess.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--core-only")});
    ok &= check(coreProcess.waitForFinished(10000) && coreProcess.exitStatus() == QProcess::NormalExit
        && coreProcess.exitCode() == 0, "QCoreApplication without GUI is suppressed even with clean environment");
    // Observations do not migrate another receiver's saved state.
    QTemporaryDir observationDir;
    QSettings observationSettings(observationDir.filePath("consent.ini"), QSettings::IniFormat);
    TelemetryConsent observationWriter(observationSettings, context);
    ok &= check(observationWriter.save(false, true), "diagnostics setup for observations");
    const int initialGeneration = observationWriter.observeDiagnostics().generation;
    QSettings observationReaderSettings(observationDir.filePath("consent.ini"), QSettings::IniFormat);
    TelemetryContext otherReceiver = context;
    otherReceiver.endpoint = QStringLiteral("https://other.example/collect");
    TelemetryConsent observationReader(observationReaderSettings, otherReceiver);
    const QByteArray beforeObservation = observationReaderSettings.value(QStringLiteral("telemetry/consentV1")).toByteArray();
    ok &= check(observationReader.observeDiagnostics().status == TelemetryConsent::ObservationStatus::Mismatch
        && observationReader.observeUsage().status == TelemetryConsent::ObservationStatus::Mismatch
        && observationReaderSettings.value(QStringLiteral("telemetry/consentV1")).toByteArray() == beforeObservation,
        "receiver mismatch observation does not migrate saved state");
    ok &= check(observationWriter.save(true, true)
        && observationWriter.observeDiagnostics().generation > initialGeneration,
        "usage ID staging advances diagnostics generation even if diagnostics remains enabled");
    QTemporaryDir migrationDir;
    QSettings oldSettings(migrationDir.filePath("consent.ini"), QSettings::IniFormat);
    TelemetryConsent oldReceiver(oldSettings, context);
    ok &= check(oldReceiver.save(true, false), "old receiver consent setup");
    QSettings newSettings(migrationDir.filePath("consent.ini"), QSettings::IniFormat);
    TelemetryConsent newReceiver(newSettings, otherReceiver);
    ok &= check(newReceiver.promptRequired() && newReceiver.save(true, false),
        "initial dialog migrates receiver and saves renewed consent before sender startup");
    QSettings workerSettings(migrationDir.filePath("consent.ini"), QSettings::IniFormat);
    TelemetryConsent workerObservation(workerSettings, otherReceiver);
    ok &= check(workerObservation.observeUsage().status == TelemetryConsent::ObservationStatus::Enabled
        && oldReceiver.observeUsage().status == TelemetryConsent::ObservationStatus::Mismatch,
        "new receiver is eligible immediately while old receiver cannot rewrite migration");
    observationSettings.setValue(QStringLiteral("telemetry/consentV1"), QByteArray(
        "{\"endpoint\":\"https://example.org/collect\",\"terms\":\"1\",\"handled\":true,\"usage\":false,\"diagnostics\":true,\"diagnosticsGeneration\":\"0\",\"id\":\"\"}"));
    observationSettings.sync();
    ok &= check(observationWriter.observeDiagnostics().status == TelemetryConsent::ObservationStatus::Error,
        "malformed generation cannot be coerced to a reusable stamp");
    settings.setValue(QStringLiteral("telemetry/consentV1"), QByteArray(
        "{\"endpoint\":\"https://example.org/collect\",\"terms\":\"1\",\"handled\":true,\"usage\":true,\"diagnostics\":false,\"id\":\"\"}"));
    settings.sync();
    TelemetryConsent incomplete(settings, context);
    ok &= check(incomplete.promptRequired() && !incomplete.usageEnabled(),
        "interrupted ID write stays disabled and asks again");
    settings.setValue(QStringLiteral("telemetry/consentV1"), QByteArray(
        "{\"endpoint\":\"https://example.org/collect\",\"terms\":\"1\",\"handled\":true,\"usage\":true,\"diagnostics\":true,\"id\":\"/private/path\"}"));
    settings.sync();
    TelemetryConsent invalidId(settings, context);
    ok &= check(invalidId.promptRequired() && !invalidId.usageEnabled()
        && !invalidId.diagnosticsEnabled() && invalidId.usageInstallationId().isEmpty(),
        "corrupt usage ID invalidates the record and prompts again");
    settings.setValue(QStringLiteral("telemetry/consentV1"), QByteArray(
        "{\"endpoint\":\"https://example.org/collect\",\"terms\":\"1\",\"handled\":true,\"usage\":true,\"diagnostics\":false,\"id\":\"12345678-1234-4234-8234-123456789abc\\n\"}"));
    settings.sync();
    TelemetryConsent newlineId(settings, context);
    ok &= check(newlineId.promptRequired() && newlineId.usageInstallationId().isEmpty(),
        "UUIDv4 plus trailing LF must not be exposed");
    settings.setValue(QStringLiteral("telemetry/consentV1"), QByteArray(
        "{\"endpoint\":\"https://example.org/collect\",\"terms\":\"1\",\"handled\":true,\"usage\":false,\"diagnostics\":true,\"diagnosticsGeneration\":2147483646,\"id\":\"\"}"));
    settings.sync();
    TelemetryConsent overflow(settings, context);
    ok &= check(!overflow.save(false, false) && !overflow.diagnosticsEnabled(),
        "diagnostics generation overflow revokes and fails closed");
    QSettings overflowSettings(dir.filePath("consent.ini"), QSettings::IniFormat);
    TelemetryConsent overflowRestart(overflowSettings, context);
    ok &= check(!overflowRestart.diagnosticsEnabled() && !overflowRestart.save(false, true),
        "generation overflow stays disabled after restart");
    settings.setValue(QStringLiteral("updates/automaticCheck"), true);
    consent.dismiss();
    ok &= check(readUpdateCheckState(settings) == UpdateCheckState::Enabled,
        "update preferences stay independent");
    // A directory cannot be written as a settings file.
    QSettings broken(dir.path(), QSettings::IniFormat);
    TelemetryConsent failed(broken, context);
    failed.save(true, true);
    ok &= check(!failed.usageEnabled() && !failed.diagnosticsEnabled()
        && failed.usageInstallationId().isEmpty(), "settings I/O failure fails closed");
    return ok ? 0 : 1;
}
