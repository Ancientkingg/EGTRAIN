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
#include <QTest>
#include <QUrl>
#include <memory>

class TestTelemetryConsent : public QObject {
	Q_OBJECT

private slots:
	void init();
	void savesEveryCombination_data();
	void savesEveryCombination();
	void dismissalPersistsDisabled_data();
	void dismissalPersistsDisabled();
	void revocationClearsSinks();
	void concurrentChangesClearOldReports();
	void overlappingWriterAndLock();
	void upgradeReceiverAndTerms();
	void unavailableContexts_data();
	void unavailableContexts();
	void interactiveEnvironmentBaseline();
	void automationMarkersSuppress_data();
	void automationMarkersSuppress();
	void offscreenIsSuppressed();
	void coreApplicationIsSuppressed();
	void observations();
	void receiverMigration();
	void damagedRecords();
	void generationOverflow();
	void updatePreferencesIndependent();
	void settingsFailureFailsClosed();

private:
	QString consentFile() const { return m_dir->filePath(QStringLiteral("consent.ini")); }

	std::unique_ptr<QTemporaryDir> m_dir;
	const TelemetryContext m_context{true, true, true, QStringLiteral("https://example.org/collect"), QStringLiteral("1")};
};

// Every slot starts from an empty settings file in a directory of its own.
void TestTelemetryConsent::init() {
	m_dir = std::make_unique<QTemporaryDir>();
	QVERIFY(m_dir->isValid());
}

void TestTelemetryConsent::savesEveryCombination_data() {
	QTest::addColumn<bool>("usage");
	QTest::addColumn<bool>("diagnostics");
	QTest::newRow("neither") << false << false;
	QTest::newRow("usage") << true << false;
	QTest::newRow("diagnostics") << false << true;
	QTest::newRow("both") << true << true;
}

void TestTelemetryConsent::savesEveryCombination() {
	QFETCH(bool, usage);
	QFETCH(bool, diagnostics);
	QSettings settings(consentFile(), QSettings::IniFormat);
	TelemetryConsent consent(settings, m_context);
	// unsaved prechecked controls cannot grant consent or an ID
	QVERIFY(consent.promptRequired());
	QVERIFY(!consent.usageEnabled());
	QVERIFY(!consent.diagnosticsEnabled());
	QVERIFY(consent.usageInstallationId().isEmpty());
	TelemetryConsentDialog dialog(consent, m_context.domain(), true);
	// initial options are prechecked
	QVERIFY(dialog.usageCheckBox()->isChecked());
	QVERIFY(dialog.diagnosticsCheckBox()->isChecked());
	dialog.usageCheckBox()->setChecked(usage);
	dialog.diagnosticsCheckBox()->setChecked(diagnostics);
	// changing prechecked controls does not grant consent
	QVERIFY(!consent.usageEnabled());
	QVERIFY(!consent.diagnosticsEnabled());
	QVERIFY(consent.usageInstallationId().isEmpty());
	dialog.findChild<QPushButton*>(QStringLiteral("saveTelemetryChoices"))->click();
	QSettings reopened(consentFile(), QSettings::IniFormat);
	TelemetryConsent restart(reopened, m_context);
	// all four combinations survive restart independently
	QVERIFY(!restart.promptRequired());
	QCOMPARE(restart.usageEnabled(), usage);
	QCOMPARE(restart.diagnosticsEnabled(), diagnostics);
	QCOMPARE(restart.usageInstallationId().isEmpty(), !usage);
	TelemetryConsentDialog later(restart, m_context.domain(), false);
	// settings reopen shows saved choices
	QCOMPARE(later.usageCheckBox()->isChecked(), usage);
	QCOMPARE(later.diagnosticsCheckBox()->isChecked(), diagnostics);
	QVERIFY2(later.findChild<QLabel*>(QStringLiteral("dialogContext"))->text().contains(QStringLiteral("saved choices remain active")),
		"settings reopening describes saved choices as active until saved changes");
	static_cast<QDialog&>(later).reject();
}

void TestTelemetryConsent::dismissalPersistsDisabled_data() {
	QTest::addColumn<int>("dismissal");
	QTest::newRow("not now") << 0;
	QTest::newRow("escape") << 1;
	QTest::newRow("window close") << 2;
}

void TestTelemetryConsent::dismissalPersistsDisabled() {
	QFETCH(int, dismissal);
	QSettings settings(consentFile(), QSettings::IniFormat);
	TelemetryConsent consent(settings, m_context);
	TelemetryConsentDialog dialog(consent, m_context.domain(), true);
	if (dismissal == 0)
		dialog.findChild<QPushButton*>(QStringLiteral("dismissTelemetryChoices"))->click();
	else if (dismissal == 1)
		static_cast<QDialog&>(dialog).reject(); // Escape
	else {
		dialog.show();
		QCoreApplication::processEvents();
		dialog.close();
	}
	// Not now, Escape and window close persist disabled and handled
	QVERIFY(!consent.promptRequired());
	QVERIFY(!consent.usageEnabled());
	QVERIFY(!consent.diagnosticsEnabled());
	QVERIFY(consent.usageInstallationId().isEmpty());
}

void TestTelemetryConsent::revocationClearsSinks() {
	QSettings settings(consentFile(), QSettings::IniFormat);
	TelemetryConsent consent(settings, m_context);
	struct FakeSink {
		bool usageQueued = true, diagnosticsQueued = true, usageActive = true, diagnosticsActive = true;
	} sink;
	QObject::connect(&consent, &TelemetryConsent::revoked, &consent, [&](bool usage, bool diagnostics) {
        if (usage) sink.usageQueued = sink.usageActive = false;
        if (diagnostics) sink.diagnosticsQueued = sink.diagnosticsActive = false; }, Qt::DirectConnection);
	consent.save(true, true);
	const QString firstId = consent.usageInstallationId();
	consent.save(false, true);
	// usage revocation synchronously clears its fake queue and active upload
	QVERIFY(!sink.usageQueued);
	QVERIFY(!sink.usageActive);
	QVERIFY(sink.diagnosticsQueued);
	QVERIFY(sink.diagnosticsActive);
	QVERIFY(consent.usageInstallationId().isEmpty());
	consent.save(true, false);
	// diagnostics revocation is independent and re-enabling usage makes a new ID
	QVERIFY(!sink.diagnosticsQueued);
	QVERIFY(!sink.diagnosticsActive);
	QVERIFY(!consent.usageInstallationId().isEmpty());
	QVERIFY(consent.usageInstallationId() != firstId);
	QSettings concurrentSettings(consentFile(), QSettings::IniFormat);
	TelemetryConsent concurrent(concurrentSettings, m_context);
	const QString currentId = concurrent.usageInstallationId();
	consent.save(false, false);
	// second instance detects revocation at collection boundary
	QVERIFY(!concurrent.usageEnabled());
	QVERIFY(concurrent.usageInstallationId().isEmpty());
	QVERIFY(!currentId.isEmpty());
}

void TestTelemetryConsent::concurrentChangesClearOldReports() {
	QSettings writerSettings(consentFile(), QSettings::IniFormat);
	TelemetryConsent writer(writerSettings, m_context);
	writer.save(false, true);
	QSettings listenerSettings(consentFile(), QSettings::IniFormat);
	TelemetryConsent listener(listenerSettings, m_context);
	struct DiagnosticsSink {
		bool queued = true, active = true;
	} diagnosticsSink;
	bool checkingReentry = false;
	int nestedRevocations = 0;
	QObject::connect(&listener, &TelemetryConsent::revoked, &listener, [&](bool, bool diagnostics) {
            if (diagnostics) diagnosticsSink.queued = diagnosticsSink.active = false;
            // A direct queue/abort slot must be able to recheck consent safely.
            if (diagnostics && !checkingReentry) {
                checkingReentry = true;
                listener.diagnosticsEnabled();
                checkingReentry = false;
            } else if (diagnostics) {
                ++nestedRevocations;
            } }, Qt::DirectConnection);
	writer.save(false, false);
	writer.save(false, true);
	// concurrent disable/re-enable clears old diagnostics reports
	QVERIFY(listener.diagnosticsEnabled());
	QVERIFY(!diagnosticsSink.queued);
	QVERIFY(!diagnosticsSink.active);
	diagnosticsSink = {};
	TelemetryContext intermediate = m_context;
	intermediate.endpoint = QStringLiteral("https://other.example/collect");
	QSettings intermediateSettings(consentFile(), QSettings::IniFormat);
	TelemetryConsent intermediateConsent(intermediateSettings, intermediate);
	intermediateConsent.promptRequired();
	intermediateConsent.save(false, true);
	QSettings backSettings(consentFile(), QSettings::IniFormat);
	TelemetryConsent back(backSettings, m_context);
	back.promptRequired();
	back.save(false, true);
	// receiver A-B-A clears old diagnostics reports
	QVERIFY(listener.diagnosticsEnabled());
	QVERIFY(!diagnosticsSink.queued);
	QVERIFY(!diagnosticsSink.active);
	diagnosticsSink = {};
	TelemetryContext otherTerms = m_context;
	otherTerms.termsVersion = QStringLiteral("2");
	QSettings otherTermsSettings(consentFile(), QSettings::IniFormat);
	TelemetryConsent otherTermsConsent(otherTermsSettings, otherTerms);
	otherTermsConsent.promptRequired();
	otherTermsConsent.save(false, true);
	QSettings backTermsSettings(consentFile(), QSettings::IniFormat);
	TelemetryConsent backTerms(backTermsSettings, m_context);
	backTerms.promptRequired();
	backTerms.save(false, true);
	// terms 1-2-1 clears old diagnostics reports
	QVERIFY(listener.diagnosticsEnabled());
	QVERIFY(!diagnosticsSink.queued);
	QVERIFY(!diagnosticsSink.active);
	QVERIFY2(nestedRevocations == 0, "direct-slot accessor does not recursively revoke");
}

void TestTelemetryConsent::overlappingWriterAndLock() {
	const QString raceFile = consentFile();
	QSettings raceSettings(raceFile, QSettings::IniFormat);
	TelemetryConsent raceWriter(raceSettings, m_context);
	raceWriter.save(false, true);
	QSettings raceListenerSettings(raceFile, QSettings::IniFormat);
	TelemetryConsent raceListener(raceListenerSettings, m_context);
	bool oldReportsQueued = true;
	QObject::connect(
		&raceListener, &TelemetryConsent::revoked, &raceListener,
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
	// overlapping writer must not restore revoked diagnostics generation
	QVERIFY(helperCompleted);
	QVERIFY(overlappingSave);
	QVERIFY(raceListener.diagnosticsEnabled());
	QVERIFY(!oldReportsQueued);
	QLockFile busyLock(raceFile + QStringLiteral(".telemetry-consent.lock"));
	QVERIFY2(busyLock.tryLock(0), "test can hold isolated consent lock");
	QSettings busySettings(raceFile, QSettings::IniFormat);
	TelemetryConsent blocked(busySettings, m_context);
	// lock contention fails closed without granting consent
	QVERIFY(!blocked.diagnosticsEnabled());
	QVERIFY(!blocked.save(false, true));
	busyLock.unlock();
}

void TestTelemetryConsent::upgradeReceiverAndTerms() {
	QSettings writerSettings(consentFile(), QSettings::IniFormat);
	TelemetryConsent writer(writerSettings, m_context);
	writer.save(false, true);
	bool revokedSlotSawEnabled = false;
	QObject::connect(&writer, &TelemetryConsent::revoked, &writer, [&](bool, bool diagnostics) {
            if (diagnostics && writer.diagnosticsEnabled()) revokedSlotSawEnabled = true; }, Qt::DirectConnection);
	writer.save(false, false);
	QVERIFY2(!revokedSlotSawEnabled, "same-instance revoked slot sees disabled state");
	QVERIFY2(writer.save(true, false), "usage enabled before upgrade");
	const QString upgradeId = writer.usageInstallationId();
	QVERIFY2(!upgradeId.isEmpty(), "upgrade test captures an enabled usage ID");
	TelemetryContext upgraded = m_context;
	QSettings upgradedSettings(consentFile(), QSettings::IniFormat);
	TelemetryConsent sameReceiver(upgradedSettings, upgraded);
	// same receiver and terms retain enabled usage and its ID on application upgrade
	QVERIFY(!sameReceiver.promptRequired());
	QVERIFY(sameReceiver.usageEnabled());
	QVERIFY(!sameReceiver.diagnosticsEnabled());
	QCOMPARE(sameReceiver.usageInstallationId(), upgradeId);
	TelemetryContext changed = m_context;
	changed.endpoint = QStringLiteral("https://other.example/collect");
	QSettings otherSettings(consentFile(), QSettings::IniFormat);
	TelemetryConsent other(otherSettings, changed);
	bool oldEndpointQueued = true;
	QObject::connect(&other, &TelemetryConsent::receiverChanged, &other, [&] { oldEndpointQueued = false; }, Qt::DirectConnection);
	// receiver change resets choices and signals old queue discard
	QVERIFY(other.promptRequired());
	QVERIFY(!other.usageEnabled());
	QVERIFY(other.usageInstallationId().isEmpty());
	QVERIFY(!oldEndpointQueued);
	other.save(true, false);
	changed.termsVersion = QStringLiteral("2");
	QSettings termsSettings(consentFile(), QSettings::IniFormat);
	TelemetryConsent terms(termsSettings, changed);
	// changed collection terms require confirmation
	QVERIFY(terms.promptRequired());
	QVERIFY(terms.usageInstallationId().isEmpty());
}

void TestTelemetryConsent::unavailableContexts_data() {
	QTest::addColumn<int>("mode");
	QTest::newRow("not packaged") << 0;
	QTest::newRow("disabled") << 1;
	QTest::newRow("not interactive") << 2;
	QTest::newRow("no endpoint") << 3;
	QTest::newRow("http endpoint") << 4;
}

void TestTelemetryConsent::unavailableContexts() {
	QFETCH(int, mode);
	TelemetryContext unavailable = m_context;
	if (mode == 0) unavailable.packaged = false;
	if (mode == 1) unavailable.enabled = false;
	if (mode == 2) unavailable.interactive = false;
	if (mode == 3) unavailable.endpoint.clear();
	if (mode == 4) unavailable.endpoint = QStringLiteral("http://example.org/collect");
	QSettings restricted(consentFile(), QSettings::IniFormat);
	TelemetryConsent denied(restricted, unavailable);
	denied.save(true, true);
	// suppressed or invalid context never grants consent or ID
	QVERIFY(!denied.available());
	QVERIFY(!denied.promptRequired());
	QVERIFY(!denied.usageEnabled());
	QVERIFY(!denied.diagnosticsEnabled());
	QVERIFY(denied.usageInstallationId().isEmpty());
}

void TestTelemetryConsent::interactiveEnvironmentBaseline() {
	QProcessEnvironment clean;
	QVERIFY2(telemetryInteractiveEnvironment(clean, QStringLiteral("cocoa")), "interactive environment has an unsuppressed baseline");
}

void TestTelemetryConsent::automationMarkersSuppress_data() {
	QTest::addColumn<QString>("key");
	for (const char* key : {"CI", "GITHUB_ACTIONS", "QEGTRAIN_AUTOSTART", "QEGTRAIN_E2E_SETTINGS_DIR"})
		QTest::newRow(key) << QString::fromLatin1(key);
}

void TestTelemetryConsent::automationMarkersSuppress() {
	QFETCH(QString, key);
	QProcessEnvironment suppressed;
	suppressed.insert(key, QStringLiteral("1"));
	QVERIFY2(!telemetryInteractiveEnvironment(suppressed, QStringLiteral("cocoa")),
		"automation marker suppresses interactive context independent of offscreen");
}

void TestTelemetryConsent::offscreenIsSuppressed() {
	QProcessEnvironment clean;
	// offscreen application is suppressed
	QVERIFY(!telemetryInteractiveEnvironment(clean, QStringLiteral("offscreen")));
	QVERIFY(!applicationTelemetryContext().interactive);
}

void TestTelemetryConsent::coreApplicationIsSuppressed() {
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
	// QCoreApplication without GUI is suppressed even with clean environment
	QVERIFY(coreProcess.waitForFinished(10000));
	QCOMPARE(coreProcess.exitStatus(), QProcess::NormalExit);
	QCOMPARE(coreProcess.exitCode(), 0);
}

// Observations do not migrate another receiver's saved state.
void TestTelemetryConsent::observations() {
	QSettings observationSettings(consentFile(), QSettings::IniFormat);
	TelemetryConsent observationWriter(observationSettings, m_context);
	QVERIFY2(observationWriter.save(false, true), "diagnostics setup for observations");
	const int initialGeneration = observationWriter.observeDiagnostics().generation;
	QSettings observationReaderSettings(consentFile(), QSettings::IniFormat);
	TelemetryContext otherReceiver = m_context;
	otherReceiver.endpoint = QStringLiteral("https://other.example/collect");
	TelemetryConsent observationReader(observationReaderSettings, otherReceiver);
	const QByteArray beforeObservation = observationReaderSettings.value(QStringLiteral("telemetry/consentV1")).toByteArray();
	// receiver mismatch observation does not migrate saved state
	QCOMPARE(observationReader.observeDiagnostics().status, TelemetryConsent::ObservationStatus::Mismatch);
	QCOMPARE(observationReader.observeUsage().status, TelemetryConsent::ObservationStatus::Mismatch);
	QCOMPARE(observationReaderSettings.value(QStringLiteral("telemetry/consentV1")).toByteArray(), beforeObservation);
	// usage ID staging advances diagnostics generation even if diagnostics remains enabled
	QVERIFY(observationWriter.save(true, true));
	QCOMPARE_GT(observationWriter.observeDiagnostics().generation, initialGeneration);
	observationSettings.setValue(QStringLiteral("telemetry/consentV1"),
		QByteArray(
			"{\"endpoint\":\"https://example.org/collect\",\"terms\":\"1\",\"handled\":true,\"usage\":false,\"diagnostics\":true,\"diagnosticsGeneration\":\"0\",\"id\":\"\"}"));
	observationSettings.sync();
	QVERIFY2(observationWriter.observeDiagnostics().status == TelemetryConsent::ObservationStatus::Error,
		"malformed generation cannot be coerced to a reusable stamp");
}

void TestTelemetryConsent::receiverMigration() {
	TelemetryContext otherReceiver = m_context;
	otherReceiver.endpoint = QStringLiteral("https://other.example/collect");
	QSettings oldSettings(consentFile(), QSettings::IniFormat);
	TelemetryConsent oldReceiver(oldSettings, m_context);
	QVERIFY2(oldReceiver.save(true, false), "old receiver consent setup");
	QSettings newSettings(consentFile(), QSettings::IniFormat);
	TelemetryConsent newReceiver(newSettings, otherReceiver);
	// initial dialog migrates receiver and saves renewed consent before sender startup
	QVERIFY(newReceiver.promptRequired());
	QVERIFY(newReceiver.save(true, false));
	QSettings workerSettings(consentFile(), QSettings::IniFormat);
	TelemetryConsent workerObservation(workerSettings, otherReceiver);
	// new receiver is eligible immediately while old receiver cannot rewrite migration
	QCOMPARE(workerObservation.observeUsage().status, TelemetryConsent::ObservationStatus::Enabled);
	QCOMPARE(oldReceiver.observeUsage().status, TelemetryConsent::ObservationStatus::Mismatch);
}

void TestTelemetryConsent::damagedRecords() {
	QSettings settings(consentFile(), QSettings::IniFormat);
	settings.setValue(QStringLiteral("telemetry/consentV1"),
		QByteArray(
			"{\"endpoint\":\"https://example.org/collect\",\"terms\":\"1\",\"handled\":true,\"usage\":true,\"diagnostics\":false,\"id\":\"\"}"));
	settings.sync();
	TelemetryConsent incomplete(settings, m_context);
	// interrupted ID write stays disabled and asks again
	QVERIFY(incomplete.promptRequired());
	QVERIFY(!incomplete.usageEnabled());
	settings.setValue(QStringLiteral("telemetry/consentV1"),
		QByteArray(
			"{\"endpoint\":\"https://example.org/collect\",\"terms\":\"1\",\"handled\":true,\"usage\":true,\"diagnostics\":true,\"id\":\"/private/path\"}"));
	settings.sync();
	TelemetryConsent invalidId(settings, m_context);
	// corrupt usage ID invalidates the record and prompts again
	QVERIFY(invalidId.promptRequired());
	QVERIFY(!invalidId.usageEnabled());
	QVERIFY(!invalidId.diagnosticsEnabled());
	QVERIFY(invalidId.usageInstallationId().isEmpty());
	settings.setValue(QStringLiteral("telemetry/consentV1"),
		QByteArray(
			"{\"endpoint\":\"https://example.org/collect\",\"terms\":\"1\",\"handled\":true,\"usage\":true,\"diagnostics\":false,\"id\":\"12345678-1234-4234-8234-123456789abc\\n\"}"));
	settings.sync();
	TelemetryConsent newlineId(settings, m_context);
	// UUIDv4 plus trailing LF must not be exposed
	QVERIFY(newlineId.promptRequired());
	QVERIFY(newlineId.usageInstallationId().isEmpty());
}

void TestTelemetryConsent::generationOverflow() {
	QSettings settings(consentFile(), QSettings::IniFormat);
	settings.setValue(QStringLiteral("telemetry/consentV1"),
		QByteArray(
			"{\"endpoint\":\"https://example.org/collect\",\"terms\":\"1\",\"handled\":true,\"usage\":false,\"diagnostics\":true,\"diagnosticsGeneration\":2147483646,\"id\":\"\"}"));
	settings.sync();
	TelemetryConsent overflow(settings, m_context);
	// diagnostics generation overflow revokes and fails closed
	QVERIFY(!overflow.save(false, false));
	QVERIFY(!overflow.diagnosticsEnabled());
	QSettings overflowSettings(consentFile(), QSettings::IniFormat);
	TelemetryConsent overflowRestart(overflowSettings, m_context);
	// generation overflow stays disabled after restart
	QVERIFY(!overflowRestart.diagnosticsEnabled());
	QVERIFY(!overflowRestart.save(false, true));
}

void TestTelemetryConsent::updatePreferencesIndependent() {
	QSettings settings(consentFile(), QSettings::IniFormat);
	TelemetryConsent consent(settings, m_context);
	settings.setValue(QStringLiteral("updates/automaticCheck"), true);
	consent.dismiss();
	QVERIFY2(readUpdateCheckState(settings) == UpdateCheckState::Enabled, "update preferences stay independent");
}

void TestTelemetryConsent::settingsFailureFailsClosed() {
	// A directory cannot be written as a settings file.
	QSettings broken(m_dir->path(), QSettings::IniFormat);
	TelemetryConsent failed(broken, m_context);
	failed.save(true, true);
	// settings I/O failure fails closed
	QVERIFY(!failed.usageEnabled());
	QVERIFY(!failed.diagnosticsEnabled());
	QVERIFY(failed.usageInstallationId().isEmpty());
}

int main(int argc, char** argv) {
	// The two child modes run without Qt Test: the race check starts the second, the core check the first.
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
	TestTelemetryConsent test;
	return QTest::qExec(&test, argc, argv);
}

#include "test_telemetryconsent.moc"
