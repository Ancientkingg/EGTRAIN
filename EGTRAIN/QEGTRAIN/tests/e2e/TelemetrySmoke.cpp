#include "tests/e2e/MainWindowTestAccess.h"

#include "app/MainWindowSupport.h"
#include "app/TelemetrySmokeSupport.h"
#include "diagrams/DiagramWindow.h"
#include "diagrams/RunResults.h"
#include "diagrams/TimetableTableWindow.h"
#include "scene/SceneBundle.h"
#include "simulation/SimulationWorker.h"
#include "telemetry/TelemetryConsent.h"
#include "telemetry/TelemetrySender.h"
#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <cstdio>
#include <memory>
#include <new>
#include <string>

namespace {

// The counters and the failure point that the scripted sender and the script share. They exist once the telemetry of
// the window has started.
std::shared_ptr<telemetry_smoke::State> smoke;

// Lets a diagram write its files with the provenance of the run, as the window does.
void attachRunProvenance(DiagramWindow* window, RunProvenance provenance) {
	window->setProvenanceWriter([provenance = std::move(provenance)](
									const QString& path, const char* kind, const std::string& bytes) {
		return writeRunArtifactWithProvenance(path.toStdString(), kind, bytes, provenance);
	});
}

void refuseTelemetry(const char* reason) {
	std::fprintf(stderr, "E2E_TELEMETRY_MOCK_REFUSED reason=%s\n", reason);
}

} // namespace

// Starts the telemetry of the window with the settings and the queue in the folder that QEGTRAIN_E2E_TELEMETRY_SMOKE names,
// which must be below the temporary directory, with a loopback endpoint and a sender whose requests the script answers. The
// consent that the mode QEGTRAIN_E2E_TELEMETRY_MODE asks for is saved first. It then starts the script that drives the window.
bool MainWindowTestAccess::startTelemetrySmoke(MainWindow& w) {
	try {
		const QString requestedDir = qEnvironmentVariable("QEGTRAIN_E2E_TELEMETRY_SMOKE");
		const QString isolatedDir = QDir(requestedDir).canonicalPath();
		const QString temporaryRoot = QDir(QDir::tempPath()).canonicalPath();
		// Compare resolved paths: Python and Qt may use different spellings of macOS /var.
		const bool isolated = !requestedDir.isEmpty() && QDir(requestedDir).isAbsolute()
			&& QDir(requestedDir).exists() && !temporaryRoot.isEmpty()
			&& isolatedDir.startsWith(temporaryRoot + QLatin1Char('/'));
		if (!isolated) {
			refuseTelemetry("invalid_isolation_root");
			return false;
		}
		const TelemetryContext context = {true, true, true, QStringLiteral("https://127.0.0.1:19497/collect"), QStringLiteral("1")};
		w.m_telemetrySettings.reset(new QSettings(QDir(isolatedDir).filePath("consent.ini"), QSettings::IniFormat));
		w.m_telemetryConsent.reset(new TelemetryConsent(*w.m_telemetrySettings, context));
		const QString smokeMode = qEnvironmentVariable("QEGTRAIN_E2E_TELEMETRY_MODE", "mixed");
		if (!w.m_telemetryConsent->save(smokeMode != "diagnostics" && smokeMode != "late" && smokeMode != "import_late",
				smokeMode != "usage" && smokeMode != "late" && smokeMode != "import_late")) {
			refuseTelemetry("consent_storage_failure");
			return false;
		}
		telemetry::Application metadata;
		metadata.version = QCoreApplication::applicationVersion();
		smoke = std::make_shared<telemetry_smoke::State>();
		w.m_telemetrySender.reset(new telemetry::TelemetrySender(context, metadata,
			QDir(isolatedDir).filePath("queue"), telemetry_smoke::options(isolatedDir, smokeMode, smoke)));
		QTimer::singleShot(0, &w, [&w] { runTelemetrySmoke(w, 0); });
		return true;
	} catch (...) {
		refuseTelemetry("initialization_exception");
		w.m_telemetrySender.reset();
		w.m_telemetryConsent.reset();
		w.m_telemetrySettings.reset();
		return false;
	}
}

// Fails the step of the observation that the script has chosen, as an allocation that fails would.
void MainWindowTestAccess::telemetryObservationStep(int step) {
	telemetry_smoke::failObservationInstallation(smoke.get(), step);
}

// Stops the run at the point of the run that the mode names: when the worker starts, when the run returns, or when it
// reaches the postprocessing.
void MainWindowTestAccess::telemetryRunStarting(MainWindow& w) {
	const QString smokeMode = qEnvironmentVariable("QEGTRAIN_E2E_TELEMETRY_MODE");
	if (smoke && smokeMode == "stop_before")
		QObject::connect(w.m_worker, &SimulationWorker::simulationStarted, w.m_worker,
			&SimulationWorker::requestStop, Qt::DirectConnection);
	if (smoke && smokeMode == "completion_stop") {
		auto* worker = w.m_worker.data();
		w.m_simulationObservations[3] = QObject::connect(
			&simulation, &DispatchController::executionReturned, &simulation,
			[worker](qint64, bool) { worker->requestStop(); }, Qt::DirectConnection);
	}
	if (smoke && smokeMode == "stop_postprocessing") {
		auto* worker = w.m_worker.data();
		w.m_simulationObservations[4] = QObject::connect(
			&simulation, &DispatchController::executionPostprocessing, &simulation,
			[worker] { worker->requestStop(); }, Qt::DirectConnection);
	}
}

void MainWindowTestAccess::telemetryWindowDestroyed() {
	std::fprintf(stdout, "E2E_TELEMETRY_SHUTDOWN\n");
	std::fflush(stdout);
}

// The script runs in stages, 500 ms apart unless a stage waits for something. Stage 0 waits for the sender to poll and
// does the actions of the mode that come before a case is open. Stage 1 opens, reloads and exports in every way. Stage 2
// starts a run. Stages 3 and 5 wait for the run to end and export its results, and stage 4 ends the application.
void MainWindowTestAccess::runTelemetrySmoke(MainWindow& w, int stage) {
	if (!smoke || !w.m_telemetrySender) return;
	const QString root = QDir(qEnvironmentVariable("QEGTRAIN_E2E_TELEMETRY_SMOKE")).canonicalPath();
	const QString mode = qEnvironmentVariable("QEGTRAIN_E2E_TELEMETRY_MODE", "mixed");
	const auto later = [&w](int next, int delay = 500) {
		QTimer::singleShot(delay, &w, [&w, next] { runTelemetrySmoke(w, next); });
	};
	if (stage == 0) {
		if ((mode != "retired_restart" && smoke->polls.load() < 1)
			|| (mode == "retired" && smoke->posts.load() < 1)) {
			later(0, 20);
			return;
		}
		telemetry_smoke::dismissMessages();
		if (mode == "import_late" || mode == "import_revoke") {
			if (mode == "import_revoke" && smoke->posts.load() < 1) {
				later(0, 20);
				return;
			}
			const int poll = smoke->polls.load();
			telemetry_smoke::chooseDirectory(root + "/private-legacy", "Select Legacy Case Folder",
				[&w, mode, poll, changed = false]() mutable {
					if (!changed) {
						changed = true;
						if (mode == "import_revoke") w.m_telemetryConsent->save(false, false);
						w.m_telemetryConsent->save(true, true);
						w.m_telemetrySender->requestConsentRefresh();
					}
					return smoke->polls.load() > poll;
				});
			telemetry_smoke::chooseDirectory(root + "/imported", "Select Scene Destination");
			w.m_session->actionLoad_Network();
			if (w.m_session->m_sceneDir != QFileInfo(root + "/imported").canonicalFilePath()) {
				std::fprintf(stderr, "E2E_TELEMETRY_IMPORT_FAILED\n");
				QCoreApplication::exit(2);
				return;
			}
			std::fprintf(stdout, "E2E_TELEMETRY_IMPORTED\n");
		} else if (mode == "late") {
			// Real actions while disabled must not be reconstructed after enable.
			w.openSceneDirectory(w.m_session->m_sceneDir);
			w.m_caseSettingsDock->hide();
			w.revealEditorDock(w.m_caseSettingsDock);
			const int poll = smoke->polls.load();
			saveSceneBundle(w.m_document->mutableModel(), (root + "/historical.egscene").toStdString());
			telemetry_smoke::chooseFile(root + "/historical.egscene", [&w, poll, changed = false]() mutable {
				if (!changed) {
					changed = true;
					w.m_telemetryConsent->save(true, true);
					w.m_telemetrySender->requestConsentRefresh();
				}
				return smoke->polls.load() > poll;
			});
			w.m_session->openSceneDialog();
		} else if (mode == "revoke") {
			if (smoke->posts.load() < 1) {
				later(0, 20);
				return;
			}
			DiagramWindow diagram("Private in-flight chart", &w);
			diagram.setTelemetryCapture([&w] { return w.captureTelemetryOperation(); });
			diagram.setCsvProvider([](const QStringList&) { return std::string("private,content\n"); }, "stale.csv");
			attachRunProvenance(&diagram, w.captureRunProvenance());
			QDir().mkpath(root + "/stale.csv.provenance.json");
			const int poll = smoke->polls.load();
			telemetry_smoke::chooseFile(root + "/stale.csv", [&w, poll, changed = false]() mutable {
				if (!changed) {
					changed = true;
					w.m_telemetryConsent->save(false, false);
					w.m_telemetryConsent->save(true, true);
					w.m_telemetrySender->requestConsentRefresh();
				}
				return smoke->polls.load() > poll;
			});
			QMetaObject::invokeMethod(&diagram, "exportCsv", Qt::DirectConnection);
		}
		later(1);
		return;
	}
	if (stage == 1) {
		const QString scenePath = root + "/case";
		w.openSceneDirectory(scenePath);
		w.openSceneDirectory(scenePath); // Successful reload is a separate occurrence.
		w.openSceneDirectory(root + "/invalid");
		w.openSceneDirectory(root + "/ambiguous");
		if (mode == "unsupported") w.openSceneDirectory(root + "/newer");
		if (mode == "malformed_bundle") {
			w.openSceneDirectory(root + "/missing-schema.egscene");
			w.openSceneDirectory(root + "/noninteger-schema.egscene");
		}
		telemetry_smoke::chooseFile(QString());
		w.m_session->openSceneDialog();
		for (auto it = w.m_editorActions.cbegin(); it != w.m_editorActions.cend(); ++it) {
			it.key()->hide();
			it.value()->trigger();		  // Built-in QAction ordering must not hide the prior state.
			w.revealEditorDock(it.key()); // Focus/tab activation is not another open.
		}
		w.hide();
		w.show(); // Parent visibility never counts editor opens.
		DiagramWindow diagram("Private chart name", &w);
		diagram.setTelemetryCapture([&w] { return w.captureTelemetryOperation(); });
		diagram.setCsvProvider([](const QStringList&) { return std::string("private,content\n1,2\n"); }, "chart.csv");
		attachRunProvenance(&diagram, w.captureRunProvenance());
		for (int repeat = 0; repeat < 2; ++repeat) {
			telemetry_smoke::chooseFile(root + "/chart.csv");
			QMetaObject::invokeMethod(&diagram, "exportCsv", Qt::DirectConnection);
		}
		telemetry_smoke::chooseFile(QString());
		QMetaObject::invokeMethod(&diagram, "exportCsv", Qt::DirectConnection);
		QDir().mkpath(root + "/blocked.csv.provenance.json");
		telemetry_smoke::chooseFile(root + "/blocked.csv");
		QMetaObject::invokeMethod(&diagram, "exportCsv", Qt::DirectConnection);
		telemetry_smoke::chooseFile(root + "/chart.png");
		QMetaObject::invokeMethod(&diagram, "exportPng", Qt::DirectConnection);
		TimetableTableWindow table({}, w.m_startOffsetSeconds, [](const QStringList&) { return std::string("private,table\n"); }, &w);
		table.setTelemetryCapture([&w] { return w.captureTelemetryOperation(); });
		table.setRunProvenance(w.captureRunProvenance());
		telemetry_smoke::chooseFile(root + "/table.csv");
		QMetaObject::invokeMethod(&table, "exportCsv", Qt::DirectConnection);
		telemetry_smoke::chooseFile(root + "/table.png");
		QMetaObject::invokeMethod(&table, "exportPng", Qt::DirectConnection);
		telemetry_smoke::chooseFile(QString());
		QMetaObject::invokeMethod(&table, "exportCsv", Qt::DirectConnection);
		telemetry_smoke::chooseFile(QString());
		QMetaObject::invokeMethod(&table, "exportPng", Qt::DirectConnection);
		if (mode == "table_failure") {
			QDir().mkpath(root + "/blocked_table.png.provenance.json");
			telemetry_smoke::chooseFile(root + "/blocked_table.png");
			QMetaObject::invokeMethod(&table, "exportPng", Qt::DirectConnection);
		}
		w.m_session->saveSceneToCurrentDir(); // Directory save has no usage kind.
		QDir(root + "/failed.egscene").removeRecursively();
		telemetry_smoke::chooseFile(root + "/failed.egscene", {}, [root] {
			QDir().mkpath(root + "/failed.egscene");
		});
		w.m_session->saveSceneAsToBundle(); // Failed Save As still requests scene_bundle, not current directory kind.
		telemetry_smoke::chooseFile(root + "/saved.egscene");
		w.m_session->saveSceneAsToBundle();
		telemetry_smoke::chooseFile(QString());
		w.m_session->saveSceneAsToBundle();
		++w.m_document->mutableModel().schemaVersion;
		telemetry_smoke::chooseFile(root + "/schema.egscene");
		w.m_session->saveSceneAsToBundle(); // Unsupported schema before staging: diagnostics only.
		--w.m_document->mutableModel().schemaVersion;
		if (mode == "bundle") w.openSceneDirectory(root + "/saved.egscene");
		w.openSceneDirectory(scenePath);
		const auto services = w.m_document->mutableModel().services;
		w.m_document->mutableModel().services.clear();
		w.runScene(); // Handled preflight failure: no usage run pair.
		w.m_document->mutableModel().services = services;
		later(2);
		return;
	}
	if (stage == 2) {
		if (mode == "install_failure") smoke->installFailurePoint = 0;
		if (mode == "run_revoke" || mode == "discard") {
			w.m_speedSlider->setValue(kMaxStepDelayMs - 10);
			auto* timer = new QTimer(&w);
			timer->setInterval(20);
			QObject::connect(timer, &QTimer::timeout, &w, [&w, timer, mode] {
				if (!smoke->startedPosts.load()) return;
				timer->stop();
				timer->deleteLater();
				if (mode == "discard") {
					w.m_document->markChanged(SceneDocument::AllChanges);
					return;
				}
				w.m_telemetryConsent->save(false, false);
				w.m_telemetryConsent->save(true, true);
				w.m_telemetrySender->requestConsentRefresh();
			});
			timer->start();
		}
		if (mode == "zero") {
			w.teardownGUI();
			simulation.resetState();
			w.startSimulation(w.captureTelemetryOperation());
		} else w.runCurrent();
		if (mode == "teardown" || mode == "replacement") w.clearSimulationWorker(true);
		if (mode == "replacement") {
			w.openSceneDirectory(root + "/case");
			w.runCurrent(); // A queued GUI completion for the joined worker is stale.
		}
		later(3, 50);
		return;
	}
	if (stage == 3 || stage == 5) {
		if (w.m_worker) {
			later(stage, 50);
			return;
		}
		if (mode == "install_failure") {
			const int ordinal = smoke->completedProbeRuns;
			if (!w.m_resultsAvailable || !w.m_completedRun
				|| !telemetry_smoke::recordRun(root, ordinal, buildRunSummaryCsv(w.m_completedRun->results),
					QString::fromStdString(initial_variables.OutputMainFolder) + "/EnergyConsumptionPerTrain.txt")) {
				std::fprintf(stderr, "E2E_TELEMETRY_INSTALL_RUN_FAILED\n");
				QCoreApplication::exit(2);
				return;
			}
			++smoke->completedProbeRuns;
			if (ordinal < 4) {
				w.teardownGUI();
				simulation.resetState();
				simulation.runSimulation(); // Zero-train guard probes any orphaned rejection observer.
				smoke->installFailurePoint = ordinal < 3 ? ordinal + 1 : -1;
				w.runCurrent();
				later(3, 50);
				return;
			}
			std::fprintf(stdout, "E2E_TELEMETRY_INSTALL_PROBES runs=%d failures=%d\n",
				smoke->completedProbeRuns, smoke->installFailures);
		}
		if (stage == 3 && mode == "repeat") {
			w.runCurrent();
			later(5, 50);
			return;
		}
		if (w.m_resultsAvailable && mode != "teardown") {
			telemetry_smoke::chooseFile(root + "/summary.csv");
			if (auto* button = w.findChild<QPushButton*>("resultView_ExportCSV")) button->click();
			telemetry_smoke::chooseFile(root + "/summary.png");
			if (auto* button = w.findChild<QPushButton*>("resultView_ExportPNG")) button->click();
		}
		later(4, 2000);
		return;
	}
	std::fprintf(stdout, "E2E_TELEMETRY_ACTIONS_DONE mode=%s posts=%d\n",
		mode.toUtf8().constData(), smoke->posts.load());
	std::fflush(stdout);
	QCoreApplication::quit();
}
