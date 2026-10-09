#include "telemetry/TelemetryConsent.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUuid>
#include <limits>

namespace {
constexpr char kKey[] = "telemetry/consentV1";
constexpr int kGenerationLimit = std::numeric_limits<int>::max();

bool validUsageId(const QString& id) {
	static const QRegularExpression uuidV4(QStringLiteral(
		"^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$"));
	return id.size() == 36 && uuidV4.match(id).hasMatch();
}

bool advanceGeneration(int& generation) {
	if (generation >= kGenerationLimit - 1) {
		generation = kGenerationLimit;
		return false;
	}
	++generation;
	return true;
}
}

bool TelemetryContext::capable() const {
	const QUrl url(endpoint);
	return packaged && enabled && interactive && !termsVersion.isEmpty()
		&& !endpoint.isEmpty() && url.isValid() && url.scheme() == QStringLiteral("https")
		&& !url.host().isEmpty() && url.userInfo().isEmpty() && url.fragment().isEmpty()
		&& url.port(-1) != 0 && url.toString(QUrl::FullyEncoded) == endpoint;
}

QString TelemetryContext::domain() const { return QUrl(endpoint).host(); }

bool telemetryInteractiveEnvironment(const QProcessEnvironment& env, const QString& platform) {
	bool interactive = !env.contains(QStringLiteral("CI"))
		&& !env.contains(QStringLiteral("GITHUB_ACTIONS"))
		&& !env.contains(QStringLiteral("QEGTRAIN_AUTOSTART"))
		&& !env.contains(QStringLiteral("QEGTRAIN_STARTUP_TIMING"))
		&& !env.contains(QStringLiteral("QEGTRAIN_PLAYBACK_PROFILE"))
		&& env.value(QStringLiteral("QT_QPA_PLATFORM")) != QStringLiteral("offscreen")
		&& env.value(QStringLiteral("QT_QPA_PLATFORM")) != QStringLiteral("minimal")
		&& platform != QStringLiteral("offscreen")
		&& platform != QStringLiteral("minimal");
	for (const QString& key : env.keys())
		if (key.startsWith(QStringLiteral("QEGTRAIN_E2E_")))
			interactive = false;
	return interactive;
}

TelemetryContext applicationTelemetryContext() {
	TelemetryContext context;
	context.packaged = EGTRAIN_PACKAGED_BUILD;
	context.enabled = EGTRAIN_ENABLE_TELEMETRY;
	context.endpoint = QStringLiteral(EGTRAIN_TELEMETRY_URL);
	context.interactive = qobject_cast<QGuiApplication*>(QCoreApplication::instance())
		&& telemetryInteractiveEnvironment(QProcessEnvironment::systemEnvironment(),
			QGuiApplication::platformName());
	return context;
}

TelemetryConsent::TelemetryConsent(QSettings& settings, TelemetryContext context, QObject* parent)
	: QObject(parent), m_settings(settings), m_context(std::move(context)) {
	m_last = read();
}

bool TelemetryConsent::available() const { return m_context.capable(); }

TelemetryConsent::State TelemetryConsent::read() {
	m_settings.sync();
	if (m_settings.status() != QSettings::NoError) {
		m_failed = true;
		return {};
	}
	const QJsonDocument doc = QJsonDocument::fromJson(m_settings.value(QString::fromLatin1(kKey)).toByteArray());
	if (!doc.isObject()) return {};
	const QJsonObject obj = doc.object();
	State state;
	state.endpoint = obj.value(QStringLiteral("endpoint")).toString();
	state.terms = obj.value(QStringLiteral("terms")).toString();
	state.handled = obj.value(QStringLiteral("handled")).toBool();
	state.usage = obj.value(QStringLiteral("usage")).toBool();
	state.diagnostics = obj.value(QStringLiteral("diagnostics")).toBool();
	state.id = obj.value(QStringLiteral("id")).toString();
	state.diagnosticsGeneration = obj.value(QStringLiteral("diagnosticsGeneration")).toInt();
	if (state.diagnosticsGeneration < 0) state.diagnosticsGeneration = kGenerationLimit;
	return state;
}

TelemetryConsent::ObservationStatus TelemetryConsent::observeState(State& state) {
	if (!available()) return ObservationStatus::Unavailable;
	const QString path = lockPath();
	QLockFile lock(path);
	if (!acquire(lock, path)) return ObservationStatus::Error;
	m_settings.sync();
	if (m_settings.status() != QSettings::NoError) return ObservationStatus::Error;
	const QByteArray bytes = m_settings.value(QString::fromLatin1(kKey)).toByteArray();
	if (bytes.isEmpty()) return ObservationStatus::Disabled;
	if (bytes.size() > 4096) return ObservationStatus::Error;
	QJsonParseError error;
	const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) return ObservationStatus::Error;
	const QJsonObject object = document.object();
	const QJsonValue generation = object.value(QStringLiteral("diagnosticsGeneration"));
	if (!generation.isDouble() || generation.toDouble() < 0
		|| generation.toDouble() >= kGenerationLimit
		|| generation.toDouble() != static_cast<int>(generation.toDouble())) return ObservationStatus::Error;
	state.diagnosticsGeneration = static_cast<int>(generation.toDouble());
	state.endpoint = object.value(QStringLiteral("endpoint")).toString();
	state.terms = object.value(QStringLiteral("terms")).toString();
	if (state.endpoint != m_context.endpoint || state.terms != m_context.termsVersion)
		return ObservationStatus::Mismatch;
	if (!object.value(QStringLiteral("handled")).isBool()
		|| !object.value(QStringLiteral("usage")).isBool()
		|| !object.value(QStringLiteral("diagnostics")).isBool()
		|| !object.value(QStringLiteral("id")).isString()) return ObservationStatus::Error;
	state.handled = object.value(QStringLiteral("handled")).toBool();
	state.usage = object.value(QStringLiteral("usage")).toBool();
	state.diagnostics = object.value(QStringLiteral("diagnostics")).toBool();
	state.id = object.value(QStringLiteral("id")).toString();
	return ObservationStatus::Enabled;
}

TelemetryConsent::UsageObservation TelemetryConsent::observeUsage() {
	State state;
	const auto status = observeState(state);
	if (status != ObservationStatus::Enabled) return {status, {}};
	if (state.usage && !validUsageId(state.id)) return {ObservationStatus::Error, {}};
	return state.handled && state.usage ? UsageObservation{status, state.id}
										: UsageObservation{ObservationStatus::Disabled, {}};
}

TelemetryConsent::DiagnosticsObservation TelemetryConsent::observeDiagnostics() {
	State state;
	const auto status = observeState(state);
	if (status != ObservationStatus::Enabled) return {status, 0};
	return state.handled && state.diagnostics ? DiagnosticsObservation{status, state.diagnosticsGeneration}
											  : DiagnosticsObservation{ObservationStatus::Disabled, 0};
}

bool TelemetryConsent::persist(const State& state) {
	QJsonObject obj{{QStringLiteral("endpoint"), state.endpoint},
		{QStringLiteral("terms"), state.terms},
		{QStringLiteral("handled"), state.handled},
		{QStringLiteral("usage"), state.usage},
		{QStringLiteral("diagnostics"), state.diagnostics},
		{QStringLiteral("id"), state.id},
		{QStringLiteral("diagnosticsGeneration"), state.diagnosticsGeneration}};
	m_settings.setValue(QString::fromLatin1(kKey), QJsonDocument(obj).toJson(QJsonDocument::Compact));
	m_settings.sync();
	if (m_settings.status() != QSettings::NoError) {
		m_failed = true;
		return false;
	}
	return true;
}

bool TelemetryConsent::valid(const State& state) const {
	return available() && !m_failed && state.handled
		&& state.diagnosticsGeneration < kGenerationLimit
		&& (!state.usage || validUsageId(state.id))
		&& state.endpoint == m_context.endpoint && state.terms == m_context.termsVersion;
}

QString TelemetryConsent::lockPath() const {
#ifdef Q_OS_WIN
	// Native Windows QSettings uses the registry, not a writable settings file.
	if (m_settings.format() == QSettings::NativeFormat)
		return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
			.filePath(QStringLiteral("telemetry-consent.lock"));
#endif
	return m_settings.fileName() + QStringLiteral(".telemetry-consent.lock");
}

bool TelemetryConsent::acquire(QLockFile& lock, const QString& path) {
	const QFileInfo location(path);
	if (location.fileName().isEmpty() || !QDir().mkpath(location.absolutePath()) || !lock.tryLock(100)) {
		m_failed = true;
		return false;
	}
	return true;
}

void TelemetryConsent::notify(const Transition& change) {
	if (m_failed || change.usage || change.diagnostics)
		emit revoked(m_failed || change.usage, m_failed || change.diagnostics);
	if (change.receiver) emit receiverChanged();
}

// Called only with the shared settings lock held. Signals are emitted by the
// caller after unlocking so direct slots may safely re-read committed state.
void TelemetryConsent::refreshLocked(Transition& change) {
	State current = read();
	if (m_failed) return;
	const bool receiverChangedNow = !current.endpoint.isEmpty() && current.endpoint != m_context.endpoint;
	const bool termsChanged = !current.terms.isEmpty() && current.terms != m_context.termsVersion;
	if (receiverChangedNow || termsChanged) {
		State reset;
		reset.endpoint = m_context.endpoint;
		reset.terms = m_context.termsVersion;
		reset.diagnosticsGeneration = current.diagnosticsGeneration;
		const bool advanced = advanceGeneration(reset.diagnosticsGeneration);
		change.usage = change.diagnostics = true;
		change.receiver = receiverChangedNow;
		if (!persist(reset)) return;
		m_last = reset;
		if (!advanced) m_failed = true;
		return;
	}
	const bool invalidated = valid(m_last) && !valid(current);
	const bool receiverMoved = current.endpoint != m_last.endpoint || current.terms != m_last.terms;
	const bool usageRevoked = m_last.usage && (!current.usage || m_last.id != current.id);
	const bool diagnosticsRevoked = m_last.diagnostics
		&& (!current.diagnostics || m_last.diagnosticsGeneration != current.diagnosticsGeneration);
	m_last = current;
	if (receiverMoved || invalidated) {
		change.usage = change.diagnostics = true;
		change.receiver = receiverMoved;
	} else {
		change.usage |= usageRevoked;
		change.diagnostics |= diagnosticsRevoked;
	}
}

void TelemetryConsent::refresh() {
	if (!available() || m_failed) return;
	Transition change;
	const QString path = lockPath();
	QLockFile lock(path);
	if (acquire(lock, path)) {
		refreshLocked(change);
		lock.unlock();
	}
	notify(change);
}

bool TelemetryConsent::promptRequired() {
	if (!available()) return false;
	refresh();
	return !m_failed && (!valid(m_last) || (m_last.usage && !validUsageId(m_last.id)));
}
bool TelemetryConsent::usageEnabled() {
	refresh();
	return valid(m_last) && m_last.usage && validUsageId(m_last.id);
}
bool TelemetryConsent::diagnosticsEnabled() {
	refresh();
	return valid(m_last) && m_last.diagnostics;
}
QString TelemetryConsent::usageInstallationId() {
	return usageEnabled() ? m_last.id : QString();
}

bool TelemetryConsent::save(bool usage, bool diagnostics) {
	if (!available()) return false;
	refresh();
	if (m_failed) return false;
#ifdef EGTRAIN_CONSENT_TEST_HOOK
	if (m_beforeWriteForTesting) m_beforeWriteForTesting();
#endif
	Transition change;
	const QString path = lockPath();
	QLockFile lock(path);
	if (!acquire(lock, path)) {
		notify(change);
		return false;
	}
	// Re-read under the same cross-process lock as every write. An intervening
	// revocation cannot be overwritten by an earlier controller's cached state.
	refreshLocked(change);
	bool saved = false;
	if (!m_failed && m_last.diagnosticsGeneration < kGenerationLimit) {
		change.usage |= m_last.usage && !usage;
		const bool diagnosticsRevoked = m_last.diagnostics && !diagnostics;
		change.diagnostics |= diagnosticsRevoked;
		State next;
		next.endpoint = m_context.endpoint;
		next.terms = m_context.termsVersion;
		next.handled = true;
		next.usage = usage;
		next.diagnostics = diagnostics;
		next.diagnosticsGeneration = m_last.diagnosticsGeneration;
		if (diagnosticsRevoked && !advanceGeneration(next.diagnosticsGeneration)) {
			next.usage = next.diagnostics = false;
			if (persist(next)) m_last = next;
			m_failed = true;
		} else {
			next.id = usage && m_last.usage && validUsageId(m_last.id) ? m_last.id : QString();
			if (usage && next.id.isEmpty()) {
				// Staging and final writes stay inside one lock transaction.
				State staging = next;
				staging.diagnostics = false;
				// An interrupted staging write must invalidate earlier diagnostic records.
				if (m_last.diagnostics && !diagnosticsRevoked) {
					change.diagnostics = true;
					if (!advanceGeneration(staging.diagnosticsGeneration)) {
						staging.usage = false;
						staging.diagnostics = false;
						persist(staging);
						m_failed = true;
					}
					next.diagnosticsGeneration = staging.diagnosticsGeneration;
				}
				if (!m_failed && persist(staging)) {
					m_last = staging;
					next.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
				}
			}
			if (!m_failed && persist(next)) {
				m_last = next;
				saved = true;
			}
		}
	}
	lock.unlock();
	notify(change);
	return saved;
}
void TelemetryConsent::dismiss() { save(false, false); }
