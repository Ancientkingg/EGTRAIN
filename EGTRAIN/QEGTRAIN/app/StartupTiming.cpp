#include "app/StartupTiming.h"

#include "app/RunSelection.h"
#include "util/PlaybackProfiler.h"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>
#include <algorithm>
#include <chrono>
#include <cstdio>

namespace {

using StartupClock = std::chrono::steady_clock;
StartupClock::time_point g_startupTimingOrigin;
quint64 g_startupTimingSequence = 0;
StartupIdentity g_startupIdentity;

bool sameStartupSettings(const SceneSimulationSettings& left,
	const SceneSimulationSettings& right) {
	return left.hasDuration == right.hasDuration && left.durationSeconds == right.durationSeconds
		&& left.hasBufferTime == right.hasBufferTime
		&& left.bufferTimeSeconds == right.bufferTimeSeconds
		&& left.hasRecoveryTime == right.hasRecoveryTime
		&& left.recoveryTimePercent == right.recoveryTimePercent;
}

bool sameStartupIdentity(const StartupIdentity& left, const StartupIdentity& right) {
	return left.set && right.set && left.path == right.path
		&& left.schemaVersion == right.schemaVersion && left.name == right.name
		&& left.baseTime == right.baseTime && left.defaultScenarioId == right.defaultScenarioId
		&& sameStartupSettings(left.settings, right.settings)
		&& left.collectionSizes == right.collectionSizes;
}

} // namespace

bool startupTimingEnabled() {
	return qEnvironmentVariable("QEGTRAIN_STARTUP_TIMING") == QLatin1String("1");
}

void beginStartupTiming() {
	if (startupTimingEnabled()) {
		g_startupTimingOrigin = StartupClock::now();
		g_startupTimingSequence = 0;
		g_startupIdentity = StartupIdentity();
	}
}

qint64 startupTimingNowNanoseconds() {
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
		StartupClock::now() - g_startupTimingOrigin)
		.count();
}

void recordStartupTiming(const QString& phase, int iteration, int generation,
	qint64 elapsedNanoseconds, const QString& invocation, const QString& source,
	bool identityOk, bool canonicalPreloadNested) {
	if (!startupTimingEnabled())
		return;
	QJsonObject record{{QStringLiteral("event"), QStringLiteral("complete")},
		{QStringLiteral("sequence"), static_cast<qint64>(g_startupTimingSequence++)},
		{QStringLiteral("phase"), phase}, {QStringLiteral("iteration"), iteration},
		{QStringLiteral("temperature"), iteration == 0 ? QStringLiteral("fresh_process") : QStringLiteral("warm_in_process")},
		{QStringLiteral("elapsed_ms"), elapsedNanoseconds / 1000000.0},
		{QStringLiteral("identity_ok"), identityOk}};
	if (generation >= 0)
		record.insert(QStringLiteral("generation"), generation);
	if (!invocation.isEmpty())
		record.insert(QStringLiteral("invocation"), invocation);
	if (!source.isEmpty())
		record.insert(QStringLiteral("source"), source);
	if (canonicalPreloadNested)
		record.insert(QStringLiteral("canonical_preload_nested"), true);
	if (phase.endsWith(QLatin1String("paint")))
		record.insert(QStringLiteral("paint_scope"), QStringLiteral("completed_qt_viewport_paint_handling"));
	const QByteArray json = QJsonDocument(record).toJson(QJsonDocument::Compact);
	std::fprintf(stdout, "QEGTRAIN_TIMING %s\n", json.constData());
	std::fflush(stdout);
}

void failStartupTiming(const QString& message) {
	std::fprintf(stderr, "QEGTRAIN_TIMING_ERROR %s\n", message.toUtf8().constData());
	std::fflush(stderr);
	QCoreApplication::exit(2);
}

void setStartupTimingPreloadIdentity(const StartupIdentity& identity) {
	g_startupIdentity = identity;
}

bool matchesExpectedStartupIdentity(const StartupIdentity& current, bool compareInputSnapshot) {
	return sameStartupIdentity(g_startupIdentity, current)
		&& (!compareInputSnapshot || g_startupIdentity.inputSnapshot == current.inputSnapshot);
}

StartupTimingTrials::StartupTimingTrials() {
	if (startupTimingEnabled()) {
		bool ok = false;
		m_warmTrials = qEnvironmentVariableIntValue("QEGTRAIN_STARTUP_WARM_TRIALS", &ok);
		if (!ok || m_warmTrials < 0)
			m_warmTrials = 0;
	}
}

void StartupTimingTrials::rememberScene(const QString& path) {
	if (m_scenePath.isEmpty())
		m_scenePath = path;
}

bool StartupTimingTrials::nextIteration() {
	if (m_iteration >= m_warmTrials)
		return false;
	++m_iteration;
	return true;
}

bool selectPlaybackProfileScenario(const SceneModel& model, RunSelection& selection) {
	if (!PlaybackProfiler::enabled())
		return true;
	const std::string requested = qEnvironmentVariable(
		"QEGTRAIN_PLAYBACK_PROFILE_SCENARIO")
									  .toStdString();
	const SceneScenario* configured = requested == "default"
		? defaultScenario(model)
		: nullptr;
	if (requested != "default") {
		const auto match = std::find_if(model.scenarios.cbegin(), model.scenarios.cend(),
			[&requested](const SceneScenario& scenario) { return scenario.id == requested; });
		if (match != model.scenarios.cend())
			configured = &*match;
	}
	if (!configured) {
		std::fprintf(stderr, "QEGTRAIN_PLAYBACK_PROFILE {\"type\":\"error\",\"reason\":\"scenario_unavailable\"}\n");
		QCoreApplication::exit(2);
		return false;
	}
	selection.selectScenario(configured->id);
	return true;
}

void configurePlaybackProfile(const SceneModel& model, const std::string& scenario, bool passengerGui, int horizon) {
	if (!PlaybackProfiler::enabled())
		return;
	PlaybackProfiler::RunConfig config;
	config.trial = qEnvironmentVariableIntValue("QEGTRAIN_PLAYBACK_PROFILE_TRIAL");
	config.view = qEnvironmentVariable("QEGTRAIN_PLAYBACK_PROFILE_VIEW").toStdString();
#if defined(Q_OS_MACOS)
	config.platform = "macOS";
#elif defined(Q_OS_WIN)
	config.platform = "Windows";
#else
	config.platform = "Linux";
#endif
	config.architecture = QSysInfo::currentCpuArchitecture().toStdString();
	config.qtPlatform = QGuiApplication::platformName().toStdString();
	config.buildType = EGTRAIN_BUILD_TYPE;
	config.caseName = model.name;
	config.requestedScenario = qEnvironmentVariable(
		"QEGTRAIN_PLAYBACK_PROFILE_SCENARIO")
								   .toStdString();
	config.scenario = scenario;
	config.defaultScenario = model.defaultScenarioId;
	config.targetZoom = config.view == "dense" ? 3.0 : 1.0;
	config.durationMs = qEnvironmentVariableIntValue("QEGTRAIN_PLAYBACK_PROFILE_DURATION_MS");
	config.delayMs = qEnvironmentVariableIntValue("QEGTRAIN_PLAYBACK_PROFILE_DELAY_MS");
	config.passengerGui = passengerGui;
	config.horizon = horizon;
	config.structural = qEnvironmentVariable("QEGTRAIN_PLAYBACK_PROFILE_STRUCTURAL") == QLatin1String("1");
	PlaybackProfiler::instance().configure(config);
}
