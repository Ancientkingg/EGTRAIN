#include "app/RunController.h"
// This header brings in SceneModel.h in the way that the member named signals needs, before the other scene headers do.
#include "editors/SceneDocument.h"
#include "app/DispatchController.h"
#include "app/MainWindowSupport.h"
#include "app/MainWindowTestHooks.h"
#include "app/PathMessages.h"
#include "app/SceneOutputDirectory.h"
#include "app/UpdateTelemetryController.h"
#include "diagrams/DelayAnalysisWindow.h"
#include "simulation/SimulationRun.h"
#include "util/PlaybackProfiler.h"
#include "util/TimeFormat.h"
#include "widgets/DialogLayout.h"
#include "widgets/UserPrompts.h"
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QStatusBar>
#include <QSysInfo>
#include <QToolButton>
#include <QVBoxLayout>
// SceneSession.h holds a pointer to a QMenu that has to be complete.
#include "app/SceneSession.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

QString completedRunContext(const RunProvenance& provenance) {
	QString caseName = QString::fromStdString(provenance.caseName);
	const QString scenario = QString::fromStdString(provenance.appliedScenario);
	if (caseName.isEmpty())
		return scenario.isEmpty() ? QStringLiteral("(none)") : scenario;
	if (scenario.isEmpty())
		return caseName;
	return QStringLiteral("%1 / %2").arg(caseName, scenario);
}

RunController::RunController(QMainWindow* window, SceneDocument* document, SceneSession* session, RunSelection& selection, Host& host)
	: QObject(window), m_window(window), m_document(document), m_session(session), m_selection(selection), m_host(host),
	  m_startOffsetSeconds(initial_variables.startingSimulationTime) {}

bool RunController::passengerGui() const {
	return initial_variables.PAX_GUI;
}

void RunController::setPassengerGui(bool on) {
	initial_variables.PAX_GUI = on;
}

std::optional<double> RunController::durationOverride() const {
	return initial_variables.durationOverride ? std::optional<double>(initial_variables.times) : std::nullopt;
}

bool RunController::caseNamedAtStartup() const {
	return initial_variables.nArgProvided;
}

void RunController::markCaseNamed() {
	initial_variables.nArgProvided = true;
}

QString RunController::settingsCaseName() const {
	return QString::fromStdString(initial_variables.name);
}

QString RunController::readyMessage() const {
	return QString("Ready - %1 (%2 tracks, %3 routes)")
		.arg(settingsCaseName())
		.arg(initial_variables.numTrackLines)
		.arg(initial_variables.N_Routes);
}

// The next run writes to the folder of the scene that the window holds. A folder from Choose
// Output Folder stays in force: the controller applies it to every run it prepares.
void RunController::selectSceneOutputFolder(const std::string& sceneName) {
	if (simulation.nextRunOutputFolder().empty())
		initial_variables.OutputMainFolder = sceneOutputDirectory(sceneName).toStdString();
}

bool RunController::stopRequested() const {
	return m_worker && m_worker->isStopRequested();
}

bool RunController::requestStop() {
	if (!m_worker)
		return false;
	m_worker->requestStop();
	return true;
}

bool RunController::togglePause() {
	if (!m_worker)
		return false;
	if (m_worker->isPauseRequested()) {
		m_worker->requestResume();
		return false;
	}
	m_worker->requestPause();
	return true;
}

void RunController::setDelayMs(int delayMs) {
	if (m_worker)
		m_worker->setDelayMs(delayMs);
}

bool RunController::hasRawRunResults() {
	for (int i = 0; i < simulation.run().numTrains; ++i)
		if (simulation.run().trains[i].trajectorySize() > 0)
			return true;
	return false;
}

// Raw trajectories can survive a scene edit; the explicit availability flag is
// the ownership boundary for result actions.
bool RunController::hasRunResults() const {
	return m_resultsAvailable && m_completedRun && hasRawRunResults();
}

void RunController::showError(const QString& title, const QString& message) {
	UserPrompts([this] { return m_host.dialogsSuppressed(); }).showError(m_window, title, message, true);
}

void RunController::clearSimulationWorker(bool requestStop) {
	if (m_worker && requestStop)
		m_worker->requestStop();
	if (m_workerThread && m_workerThread->isRunning()) {
		m_workerThread->quit();
		m_workerThread->wait();
	}
	for (auto& observation : m_simulationObservations) {
		QObject::disconnect(observation);
		observation = QMetaObject::Connection();
	}
	// The mailbox can still contain the last worker publication after the
	// thread has stopped. Drop it with the other transient playback state so a
	// queued snapshotAvailable callback cannot repaint an old run.
	simulation.takeSimulationSnapshot();
	m_host.playbackEnded();
	// Retain the final frame for passenger inspection until scene teardown.
	m_worker = nullptr;
	m_workerThread = nullptr;
	m_document->setEditable(true);
	m_host.runPhaseChanged(RunPhase::Idle);
}

// The Run action always prepares the currently loaded canonical scene first.
void RunController::runCurrent() {
	if (m_worker)
		return;
	if (m_document->isLoaded())
		runScene();
	else
		m_window->statusBar()->showMessage("Open a canonical scene before running.", 5000);
}

QString RunController::runReviewText() const {
	const SceneDiagnosticCounts counts = countDiagnostics(m_session->diagnostics());
	QString incidentDetails;
	for (const SceneIncident& incident : m_host.selectedScenarioIncidents()) {
		const QString occurrence = incident.hasOccurrence || incident.occurrence != 1
			? QString::number(incident.occurrence)
			: QStringLiteral("all");
		const QString window = incident.hasEndSeconds || incident.endSeconds != 0.0
			? QString::number(incident.endSeconds, 'g', 12)
			: QStringLiteral("until-destination");
		QStringList options;
		options << (incident.hasReducedSpeed
				? QString("reduced-speed=%1 km/h").arg(QString::number(incident.reducedSpeedKmh, 'g', 12))
				: QStringLiteral("reduced-speed=none"));
		options << (incident.hasEndSeconds ? QStringLiteral("recovery-end=yes")
										   : QStringLiteral("recovery-end=no"));
		options << (incident.terminateAtDestination
				? QStringLiteral("terminate-at-destination=yes")
				: QStringLiteral("terminate-at-destination=no"));
		QString detail = QString("id=%1 type=%2 target=%3 start=%4 window=%5 occurrence=%6 options=%7")
							 .arg(QString::fromStdString(incident.id), QString::fromStdString(incident.type),
								 incident.target.empty() ? QStringLiteral("(empty)") : QString::fromStdString(incident.target))
							 .arg(QString::number(incident.startSeconds, 'g', 12), window, occurrence, options.join(","));
		if (!incidentDetails.isEmpty())
			incidentDetails += "; ";
		incidentDetails += detail;
	}
	QString entranceDelayDetails;
	const SceneScenario* scenario = m_host.selectedScenario();
	if (scenario) {
		for (const SceneEntranceDelay& delay : scenario->entranceDelays) {
			const auto service = std::find_if(m_document->model().services.begin(), m_document->model().services.end(),
				[&delay](const SceneService& candidate) { return candidate.id == delay.serviceId; });
			const QString operatingCode = service == m_document->model().services.end()
				? QStringLiteral("(unavailable)")
				: QString::fromStdString(sceneServiceOccurrenceOperatingCode(*service, delay.occurrence));
			QString detail = QString("service=%1 occurrence=%2 operating_code=%3 station=%4 seconds=%5")
								 .arg(delay.serviceId.empty() ? QStringLiteral("(empty)") : QString::fromStdString(delay.serviceId))
								 .arg(delay.occurrence)
								 .arg(operatingCode.isEmpty() ? QStringLiteral("(unavailable)") : operatingCode)
								 .arg(delay.stationId.empty() ? QStringLiteral("(empty)") : QString::fromStdString(delay.stationId))
								 .arg(QString::number(delay.delaySeconds, 'g', 12));
			if (!entranceDelayDetails.isEmpty())
				entranceDelayDetails += "; ";
			entranceDelayDetails += detail;
		}
	}
	const double duration = m_host.serviceOccurrenceDuration();
	QString summary = QString("Case study: %1\nScenario: %2\nService definitions: %3\nConfigured total: %4\nNumber of services in sim.: %5\nSelected: %6\nSelected in period: %7\nCompositions: %8\nIncidents: %9\nValidation: %10 error(s), %11 warning(s)")
						  .arg(QString::fromStdString(m_document->model().name))
						  .arg(m_host.scenarioContext())
						  .arg(static_cast<int>(m_document->model().services.size()))
						  .arg(m_host.totalServiceOccurrences())
						  .arg(m_host.inPeriodServiceOccurrences())
						  .arg(m_selection.selectedCount(m_document->model(), duration))
						  .arg(m_selection.selectedInPeriodCount(m_document->model(), duration))
						  .arg(static_cast<int>(m_document->model().compositions.size()))
						  .arg(static_cast<int>(m_host.selectedScenarioIncidents().size()))
						  .arg(counts.errors)
						  .arg(counts.warnings);
	summary += QString("\nCounting rule: scheduled entry ≥ 0 and < %1 s; these are configured identities, not observed trains.")
				   .arg(QString::number(duration, 'g', 12));
	if (!incidentDetails.isEmpty())
		summary += "\nIncident configuration: " + incidentDetails;
	if (!entranceDelayDetails.isEmpty())
		summary += "\nEntrance delay configuration: " + entranceDelayDetails;
	return summary;
}

bool RunController::showRunReview() {
	if (m_host.dialogsSuppressed() && !mainWindowTestHooks().askRunReview)
		return true;

	QDialog review(m_window);
	review.setObjectName("runReviewDialog");
	review.setWindowTitle("Run simulation");
	auto* body = new QWidget;
	body->setObjectName("runReviewBody");
	auto* layout = new QVBoxLayout(body);
	layout->setSpacing(8);
	const bool detailsEnabled = m_host.advancedDetailsEnabled();
	const SceneDiagnosticCounts counts = countDiagnostics(m_session->diagnostics());
	const double duration = m_host.serviceOccurrenceDuration();
	const int selectedInPeriod = m_selection.selectedInPeriodCount(m_document->model(), duration);
	const long long baseSeconds = baseTimeToSeconds(m_document->model().baseTime);
	auto* facts = new QGridLayout();
	facts->setHorizontalSpacing(20);
	facts->setVerticalSpacing(8);
	const auto addFact = [&](int row, const QString& label, const QString& value) {
		auto* name = new QLabel(label, body);
		name->setObjectName("runReviewFactLabel");
		auto* fact = new QLabel(value, body);
		fact->setTextFormat(Qt::PlainText);
		fact->setWordWrap(true);
		fact->setObjectName("runReviewFactValue");
		facts->addWidget(name, row, 0);
		facts->addWidget(fact, row, 1);
	};
	addFact(0, "Selected in period", QString::number(selectedInPeriod));
	addFact(1, "Start clock", QString::fromStdString(formatSimTime(0, baseSeconds)));
	addFact(2, "Duration",
		QString("%1 s elapsed (ends near %2)")
			.arg(QString::number(duration, 'g', 12),
				QString::fromStdString(formatSimTime(static_cast<long long>(duration), baseSeconds))));
	addFact(3, "Active incidents", QString::number(static_cast<int>(m_host.selectedScenarioIncidents().size())));
	layout->addLayout(facts);

	auto* status = new QLabel(body);
	status->setObjectName("runReviewStatus");
	const bool noServicesInPeriod = selectedInPeriod == 0;
	// The validator message names the route sections and tracks without a signalling level.
	QString unsignalledSections;
	for (const SceneDiagnostic& diagnostic : m_session->diagnostics()) {
		if (diagnostic.code == "scene.signalling.level.missing") {
			unsignalledSections = QString::fromStdString(diagnostic.message);
			unsignalledSections[0] = unsignalledSections[0].toLower();
			break;
		}
	}
	status->setText(noServicesInPeriod
			? QStringLiteral("Ready to run, but no selected services enter during this period.")
			: !unsignalledSections.isEmpty()
			? QString("Ready to run, but %1.").arg(unsignalledSections)
			: detailsEnabled && counts.warnings > 0
			? QString("Ready to run. Review %1 validation %2 if needed.")
				  .arg(counts.warnings)
				  .arg(counts.warnings == 1 ? "warning" : "warnings")
			: QStringLiteral("Ready to run."));
	status->setProperty("warning", noServicesInPeriod || !unsignalledSections.isEmpty() || (detailsEnabled && counts.warnings > 0));
	status->setWordWrap(true);
	layout->addWidget(status);

	auto* detailsToggle = new QToolButton(body);
	detailsToggle->setText("Run details");
	detailsToggle->setObjectName("runReviewDetailsToggle");
	detailsToggle->setCheckable(true);
	detailsToggle->setArrowType(Qt::RightArrow);
	detailsToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	auto* details = new QLabel(body);
	details->setObjectName("runReviewDetails");
	details->setTextFormat(Qt::PlainText);
	details->setWordWrap(true);
	details->setTextInteractionFlags(Qt::TextSelectableByMouse);
	const QString fullDetails = runReviewText();
	const int incidentStart = fullDetails.indexOf("\nIncident configuration:");
	const int delayStart = fullDetails.indexOf("\nEntrance delay configuration:");
	const int firstConfiguration = incidentStart < 0 ? delayStart
		: delayStart < 0							 ? incidentStart
													 : std::min(incidentStart, delayStart);
	QString displayedDetails = detailsEnabled || firstConfiguration < 0
		? fullDetails
		: fullDetails.left(firstConfiguration);
	if (!detailsEnabled) {
		const int validationStart = displayedDetails.indexOf("\nValidation:");
		const int countingStart = displayedDetails.indexOf("\nCounting rule:");
		if (validationStart >= 0 && countingStart > validationStart)
			displayedDetails.remove(validationStart, countingStart - validationStart);
	}
	details->setText(displayedDetails);
	details->hide();
	connect(detailsToggle, &QToolButton::toggled, &review, [&review, detailsToggle, details](bool shown) {
		detailsToggle->setArrowType(shown ? Qt::DownArrow : Qt::RightArrow);
		details->setVisible(shown);
		// The dialog grows for the details once the layout has made room for them.
		if (shown)
			QTimer::singleShot(0, &review, [&review] { DialogLayout::fitWidthToContent(review); });
	});
	layout->addWidget(detailsToggle);
	layout->addWidget(details);

	enum ReviewChoice { CancelRun,
		StartRun,
		ShowLoadedData,
		ShowValidation };
	ReviewChoice choice = CancelRun;
	if (detailsEnabled) {
		auto* inspection = new QHBoxLayout;
		auto* loadedButton = new QPushButton("Loaded Data", body);
		auto* validationButton = new QPushButton("Validation", body);
		inspection->addWidget(loadedButton);
		inspection->addWidget(validationButton);
		inspection->addStretch();
		layout->addLayout(inspection);
		connect(loadedButton, &QPushButton::clicked, &review, [&]() {
			choice = ShowLoadedData;
			review.accept();
		});
		connect(validationButton, &QPushButton::clicked, &review, [&]() {
			choice = ShowValidation;
			review.accept();
		});
	}
	auto* buttons = new QDialogButtonBox(&review);
	buttons->setObjectName("runReviewButtons");
	buttons->addButton(QDialogButtonBox::Cancel);
	auto* runButton = buttons->addButton("Run simulation", QDialogButtonBox::AcceptRole);
	runButton->setObjectName("runReviewRunButton");
	runButton->setDefault(true);
	connect(buttons, &QDialogButtonBox::accepted, &review, [&]() {
		choice = StartRun;
		review.accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, &review, &QDialog::reject);
	DialogLayout::install(review, "Run simulation",
		QString("%1 / %2").arg(QString::fromStdString(m_document->model().name), m_host.scenarioContext()),
		body, buttons);
	if (const auto& dialogShown = mainWindowTestHooks().dialogShown)
		dialogShown(review, "runReview");
	review.exec();
	if (const auto& dialogClosed = mainWindowTestHooks().dialogClosed)
		dialogClosed(review, "runReview");
	if (choice == StartRun)
		return true;
	if (choice == ShowLoadedData)
		m_host.showLoadedData();
	else if (choice == ShowValidation)
		m_host.showValidation();
	return false;
}

RunProvenance RunController::captureRunProvenance() const {
	RunProvenance provenance;
	provenance.caseName = m_document->model().name;
	provenance.sceneSchemaVersion = m_document->model().schemaVersion;
	provenance.input = captureSavedInput(m_session->sceneDir().toStdString(),
		m_session->sceneIsBundle() ? "bundle" : "directory", m_document->isDirty(), m_session->savedSceneSha256());
	provenance.appliedScenario = m_selection.appliedScenarioId();
	provenance.baseTimeSeconds = static_cast<double>(initial_variables.startingSimulationTime);
	provenance.durationSeconds = initial_variables.times;
	provenance.timestepSeconds = timestep;
	provenance.bufferSeconds = initial_variables.bufferTime;
	provenance.recoveryPercent = initial_variables.recoveryTimePercentage;
	provenance.paxMode = initial_variables.PAX_GUI ? 1 : 0;
	provenance.tsmMode = initial_variables.TSM;
	provenance.routeChoiceMode = initial_variables.RChoice;
	provenance.randomSeed = initial_variables.randomSeed;
	provenance.selectedOccurrences.reserve(static_cast<std::size_t>(std::max(0, simulation.run().numTrains)));
	for (int index = 0; index < simulation.run().numTrains; ++index) {
		const Train& train = simulation.run().trains[index];
		provenance.selectedOccurrences.push_back({train.serviceId, train.serviceOccurrence,
			train.operatingCode});
	}
	return provenance;
}

DelayRunSnapshot RunController::completedDelaySnapshot() const {
	DelayRunSnapshot snapshot;
	if (!m_resultsAvailable || !m_completedRun || m_completedRun->results.trains.empty())
		return snapshot;
	const CompletedRunData& run = *m_completedRun;
	snapshot.caseRevision = std::to_string(m_document->revision());
	snapshot.scenarioId = m_selection.appliedScenarioId();
	snapshot.baseTimeSeconds = static_cast<double>(baseTimeToSeconds(m_document->model().baseTime));
	snapshot.durationSeconds = std::isfinite(run.settings.horizonSteps)
		? run.settings.horizonSteps
		: 0.0;
	snapshot.timestep = run.settings.timestepSeconds;
	for (const SceneScenario& scenario : m_document->model().scenarios) {
		if (scenario.id != snapshot.scenarioId)
			continue;
		snapshot.hasIncidents = !scenario.incidents.empty();
		snapshot.hasEntranceDelays = !scenario.entranceDelays.empty();
		break;
	}
	snapshot.provenance = run.provenance;
	snapshot.run = run.results;
	snapshot.timetable = run.timetable;
	return snapshot;
}

RunController::DelayBaselineState RunController::delayBaselineState(bool resultsShown) const {
	DelayBaselineState state;
	const DelayRunSnapshot current = completedDelaySnapshot();
	state.canSet = resultsShown && !current.scenarioId.empty()
		&& !current.hasIncidents && !current.hasEntranceDelays;
	state.canCompare = resultsShown && m_delayBaseline.has_value()
		&& (current.hasIncidents || current.hasEntranceDelays);

	QString& feedback = state.feedback;
	if (!resultsShown) {
		if (m_delayBaseline) {
			feedback = QStringLiteral("Baseline: %1. Run the selected scenario to create completed results for comparison.")
						   .arg(completedRunContext(m_delayBaseline->provenance));
		} else if (!m_delayBaselineStatus.isEmpty()) {
			feedback = m_delayBaselineStatus;
		} else {
			feedback = QStringLiteral("No completed run. Run the selected scenario; a run with no incidents or entrance delays can become the baseline.");
		}
	} else if (!m_delayBaseline) {
		if (state.canSet) {
			feedback = QStringLiteral("Completed run: %1. Set this run with no incidents or entrance delays as the delay baseline.")
						   .arg(completedRunContext(current.provenance));
		} else if (current.hasEntranceDelays) {
			feedback = QStringLiteral("Completed run: %1. This run has entrance delays; run a scenario with no incidents or entrance delays to set the baseline.")
						   .arg(completedRunContext(current.provenance));
		} else {
			feedback = QStringLiteral("Completed run: %1. This run has incidents; run a scenario with no incidents or entrance delays to set the baseline.")
						   .arg(completedRunContext(current.provenance));
		}
	} else if (state.canCompare) {
		feedback = QStringLiteral("Baseline: %1. Compare delays shows the additional delay of each call against it.")
					   .arg(completedRunContext(m_delayBaseline->provenance));
	} else {
		feedback = QStringLiteral("Baseline: %1. Run a scenario with incidents or entrance delays, then compare.")
					   .arg(completedRunContext(m_delayBaseline->provenance));
	}
	if (!current.scenarioId.empty() && !m_selection.scenarioId().empty()
		&& current.scenarioId != m_selection.scenarioId()) {
		feedback += QStringLiteral(" Selected next scenario: %1; it has not been run yet.")
						.arg(QString::fromStdString(m_selection.scenarioId()));
	}
	return state;
}

void RunController::setDelayBaseline() {
	const DelayRunSnapshot snapshot = completedDelaySnapshot();
	if (snapshot.scenarioId.empty() || snapshot.run.trains.empty()) {
		QMessageBox::warning(m_window, "Delay baseline unavailable",
			"Complete a run with no incidents or entrance delays first.");
		return;
	}
	if (snapshot.hasIncidents || snapshot.hasEntranceDelays) {
		QMessageBox::warning(m_window, "Delay baseline rejected",
			"The delay baseline must have no incidents and no entrance delays.");
		return;
	}
	m_delayBaseline = snapshot;
	m_delayBaselineStatus = QString("Delay baseline set: %1. Compare it with a completed scenario that has incidents or entrance delays.")
								.arg(completedRunContext(snapshot.provenance));
	m_window->statusBar()->showMessage(QString("Delay baseline set to scenario %1").arg(QString::fromStdString(snapshot.scenarioId)), 5000);
	m_host.delayBaselineChanged();
}

void RunController::clearDelayBaseline(const QString& reason) {
	if (m_delayBaseline)
		m_delayBaselineStatus = reason;
	else
		m_delayBaselineStatus.clear();
	m_delayBaseline.reset();
}

DelayAnalysisWindow* RunController::openDelayAnalysis() {
	if (!hasRunResults()) {
		m_window->statusBar()->showMessage("Run a simulation to open the delay analysis", 5000);
		return nullptr;
	}
	auto* window = new DelayAnalysisWindow(completedDelaySnapshot(), m_delayBaseline, m_window);
	window->setTelemetryCapture([this] { return m_host.captureTelemetryOperation(); });
	window->setAttribute(Qt::WA_DeleteOnClose);
	window->show();
	return window;
}

void RunController::showDelayComparison() {
	if (DelayAnalysisWindow* window = openDelayAnalysis())
		window->showBaselineTab();
}

bool RunController::sceneChanged() {
	if (m_delayBaseline)
		m_delayBaselineStatus = QStringLiteral(
			"Delay baseline cleared after a case edit. Complete a run with no incidents or entrance delays to set a new baseline.");
	m_delayBaseline.reset();
	if (!m_worker)
		return false;
	m_sceneChangedDuringRun = true;
	m_resultsAvailable = false;
	return true;
}

void RunController::discardResults() {
	m_runtimeStatus = QStringLiteral("Not built");
	m_runtimeDiagnostics.clear();
	m_resultsAvailable = false;
	m_sceneChangedDuringRun = false;
	m_selection.clearAppliedScenario();
	m_completedRun.reset();
	m_pendingRunProvenance = RunProvenance();
}

void RunController::sceneWillBeReplaced() {
	m_lastRunSelectedOccurrences = 0;
	m_lastRunTotalOccurrences = 0;
	m_lastRunSelectedInPeriodOccurrences = 0;
}

// starts EGTRAIN simulation on a worker thread
void RunController::startSimulation(const telemetry::OperationObservation& operation) {
	if (m_worker)
		return; // already running

	m_host.runStarting();

	// create worker and thread; sync slider before run() starts
	m_worker = new SimulationWorker();
	m_document->setEditable(false);
	m_worker->setDelayMs(m_host.stepDelayMs());
	m_host.runPanelsChanged();
	m_workerThread = new QThread(this);
	m_worker->moveToThread(m_workerThread);

	// when the thread starts, begin the simulation
	connect(m_workerThread, &QThread::started, m_worker, &SimulationWorker::run);
	// when simulation finishes on the worker, handle results on main thread
	connect(m_worker, &SimulationWorker::simulationFinished, this, [this, worker = m_worker]() {
		// A stopped run can leave a queued completion after another case opens.
		if (worker && worker == m_worker)
			onSimulationFinished();
		runDeferredAction();
	});
	// clean up when thread finishes
	connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
	connect(m_workerThread, &QThread::finished, m_workerThread, &QObject::deleteLater);

	if (m_host.telemetryActive()) {
		const auto observationStep = mainWindowTestHooks().observationStep;
		const auto rollback = [this]() noexcept {
			for (auto& connection : m_simulationObservations) {
				QObject::disconnect(connection);
				connection = QMetaObject::Connection();
			}
		};
		// Telemetry is optional. Its allocation/connection failures cannot stop Run.
		try {
			if (observationStep) observationStep(0);
			auto observation = std::make_shared<telemetry::SimulationObservation>(operation);
			m_simulationObservations[0] = connect(
				&simulation, &DispatchController::executionRejected, &simulation,
				[observation] { observation->reject(telemetry::Error::InvalidInput); }, Qt::DirectConnection);
			if (observationStep) observationStep(1);
			m_simulationObservations[1] = connect(
				&simulation, &DispatchController::executionBegan, &simulation,
				[observation] { observation->begin(); }, Qt::DirectConnection);
			if (observationStep) observationStep(2);
			m_simulationObservations[2] = connect(
				&simulation, &DispatchController::executionReturned, &simulation,
				[observation](qint64 elapsedMs, bool cancelled) {
					observation->finish(
						cancelled ? telemetry::SimulationObservation::Outcome::Cancelled : telemetry::SimulationObservation::Outcome::Completed,
						elapsedMs);
				},
				Qt::DirectConnection);
			if (observationStep) observationStep(3);
			if (!m_simulationObservations[0] || !m_simulationObservations[1] || !m_simulationObservations[2])
				rollback();
		} catch (...) {
			rollback();
		}
	}
	m_host.workerReady();
	m_workerThread->start();
	m_host.runPhaseChanged(RunPhase::Running);
}

bool RunController::deferUntilRunStopped(std::function<void()> action) {
	if (!m_worker)
		return false;
	m_afterRunAction = std::move(action);
	m_worker->requestStop();
	m_host.runPhaseChanged(RunPhase::Stopping);
	m_window->statusBar()->showMessage(QStringLiteral("Stopping simulation..."));
	return true;
}

void RunController::runDeferredAction() {
	if (m_worker || !m_afterRunAction)
		return;
	const std::function<void()> action = std::move(m_afterRunAction);
	m_afterRunAction = nullptr;
	action();
}

// handle simulation completion on the main thread
void RunController::onSimulationFinished() {
	const bool hadFollowTarget = m_host.endFollowing();
	if (PlaybackProfiler::enabled() && PlaybackProfiler::instance().frozen()) {
		PlaybackProfiler::instance().emitRecords(true);
		clearSimulationWorker(false);
		QCoreApplication::exit(0);
		return;
	}
	const bool sceneChangedDuringRun = m_sceneChangedDuringRun;
	m_sceneChangedDuringRun = false;
	// A stop that arrives after the last stage leaves a completed run.
	const bool stopped = m_worker && m_worker->isStopRequested() && !simulation.lastRunCompleted();
	const bool stoppedByUser = stopped && !sceneChangedDuringRun;
	m_resultsAvailable = !sceneChangedDuringRun && !stopped && hasRawRunResults();
	m_runtimeStatus = m_resultsAvailable ? QStringLiteral("Completed")
		: stoppedByUser					 ? QStringLiteral("Stopped")
										 : QStringLiteral("Failed");
	if (m_resultsAvailable) {
		// Freeze result values before a subsequent run replaces the runtime trains.
		m_completedRun = std::make_shared<const CompletedRunData>(
			captureCompletedRun(simulation.run(), &m_document->model(), m_startOffsetSeconds, m_pendingRunProvenance));
		m_host.resultsKept();
	} else {
		m_pendingRunProvenance = RunProvenance();
		m_completedRun.reset();
		m_host.resultsDiscarded(stoppedByUser, sceneChangedDuringRun);
	}
	m_pendingRunProvenance = RunProvenance();
	m_host.loadedDataChanged();

	if (m_host.runFinished())
		return;

	// print last services
	if (!stopped)
		simulation.printLastTrainServicePathDiagram();

	m_host.runProgressEnded();
	m_window->statusBar()->showMessage(sceneChangedDuringRun
			? QStringLiteral("Simulation finished; results discarded because the scene changed during the run")
			: stopped ? QStringLiteral("Simulation stopped")
			: hadFollowTarget
			? QStringLiteral("Simulation complete - Follow disabled")
			: QStringLiteral("Simulation complete - open the Diagrams menu for results"));
	// The Run Results dock raised by resultsKept is the completion notice;
	// diagram entries switch on here instead of a modal prompt chain.
	// cleanup thread
	clearSimulationWorker(false);
	if (m_resultsAvailable && !stopped)
		m_host.replayReady(simulation.takeReplayCandidate());
	else
		simulation.resetReplayCandidate();
	m_host.runEnded();
	// The track preview that replaces a stopped run wrote its own status message.
	if (stoppedByUser)
		m_window->statusBar()->showMessage(QStringLiteral("Simulation stopped"));

	m_host.runSettled();
}

// The choice is stored in the controller and applied when the next run is prepared, so the
// folder of a run that is already active never changes.
void RunController::chooseOutputFolder() {
	const std::string& chosen = simulation.nextRunOutputFolder();
	QString dir = QFileDialog::getExistingDirectory(m_window, "Choose Output Folder",
		QString::fromStdString(chosen.empty() ? initial_variables.OutputMainFolder : chosen));
	if (dir.isEmpty())
		return;
	QDir().mkpath(dir);
	simulation.setNextRunOutputFolder(dir.toStdString());
	m_window->statusBar()->showMessage(path_message::outputFolder(dir), 4000);
}

void RunController::runScene() {
	const auto operation = m_host.captureTelemetryOperation();
	if (!m_document->isLoaded())
		return;

	if (PlaybackProfiler::enabled()) {
		const std::string requested = qEnvironmentVariable(
			"QEGTRAIN_PLAYBACK_PROFILE_SCENARIO")
										  .toStdString();
		const SceneScenario* configured = requested == "default"
			? defaultScenario(m_document->model())
			: nullptr;
		if (requested != "default") {
			const auto match = std::find_if(m_document->model().scenarios.cbegin(), m_document->model().scenarios.cend(),
				[&requested](const SceneScenario& scenario) { return scenario.id == requested; });
			if (match != m_document->model().scenarios.cend())
				configured = &*match;
		}
		if (!configured) {
			std::fprintf(stderr, "QEGTRAIN_PLAYBACK_PROFILE {\"type\":\"error\",\"reason\":\"scenario_unavailable\"}\n");
			QCoreApplication::exit(2);
			return;
		}
		m_selection.selectScenario(configured->id);
	}

	m_host.validateScene();
	if (hasErrors(m_session->diagnostics())) {
		operation.failure(telemetry::Operation::Simulation, telemetryDiagnosticError(m_session->diagnostics()));
		int errorCount = countDiagnostics(m_session->diagnostics()).errors;
		showError("Cannot Run Scene",
			QString("The scene has %1 validation error(s) that must be fixed before running.").arg(errorCount));
		return;
	}
	m_selection.pruneExcluded(m_document->model(), m_host.serviceOccurrenceDuration());
	m_lastRunTotalOccurrences = m_host.totalServiceOccurrences();
	m_lastRunSelectedOccurrences = m_selection.selectedCount(m_document->model(), m_host.serviceOccurrenceDuration());
	m_lastRunSelectedInPeriodOccurrences = m_selection.selectedInPeriodCount(m_document->model(), m_host.serviceOccurrenceDuration());
	if (m_lastRunSelectedOccurrences <= 0) {
		operation.failure(telemetry::Operation::Simulation, telemetry::Error::InvalidInput);
		showError("Cannot Run Scene", "Select at least one generated service occurrence before running.");
		return;
	}
	if (!showRunReview())
		return;

	m_host.runPreparing();
	m_host.teardownForRun();
	selectSceneOutputFolder(m_document->model().name);
	QString outputPath = QString::fromStdString(initial_variables.OutputMainFolder);
	if (outputPath.isEmpty() || QDir(outputPath).isRelative()) {
		QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
		if (base.isEmpty())
			base = QDir::homePath() + "/EGTRAIN";
		outputPath = QDir(base).absoluteFilePath(outputPath.isEmpty() ? "Output" : outputPath);
		QDir().mkpath(outputPath);
		initial_variables.OutputMainFolder = outputPath.toStdString();
	}
	initial_variables.GUI = 1;
	m_host.invalidateRunResults();
	m_selection.applySelectedScenario();
	const SceneRunSelection selection = m_selection.selectedOccurrences(m_document->model(), m_host.serviceOccurrenceDuration());
	bool timingIdentityOk = true;
	const std::vector<SceneDiagnostic> diagnostics = m_host.timedPreparation(
		[&] { return simulation.prepareScene(m_document->model(), m_selection.scenarioId(), selection); }, timingIdentityOk);
	m_runtimeDiagnostics = diagnostics;
	if (hasErrors(diagnostics)) {
		operation.failure(telemetry::Operation::Simulation, telemetryDiagnosticError(diagnostics));
		m_runtimeStatus = QStringLiteral("Failed");
		m_host.sceneActionsChanged();
		m_host.validateScene();
		showError("Cannot Run Scene", firstDiagnosticMessage(diagnostics));
		return;
	}
	m_runtimeStatus = QStringLiteral("Ready");
	m_host.loadedDataChanged();
	initial_variables.startingSimulationTime = baseTimeToSeconds(m_document->model().baseTime);
	m_startOffsetSeconds = initial_variables.startingSimulationTime;
	m_pendingRunProvenance = captureRunProvenance();

	m_host.buildRunCanvas();
	if (PlaybackProfiler::enabled()) {
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
		config.caseName = m_document->model().name;
		config.requestedScenario = qEnvironmentVariable(
			"QEGTRAIN_PLAYBACK_PROFILE_SCENARIO")
									   .toStdString();
		config.scenario = m_selection.appliedScenarioId();
		config.defaultScenario = m_document->model().defaultScenarioId;
		config.targetZoom = config.view == "dense" ? 3.0 : 1.0;
		config.durationMs = qEnvironmentVariableIntValue("QEGTRAIN_PLAYBACK_PROFILE_DURATION_MS");
		config.delayMs = qEnvironmentVariableIntValue("QEGTRAIN_PLAYBACK_PROFILE_DELAY_MS");
		config.passengerGui = initial_variables.PAX_GUI;
		config.tsm = initial_variables.TSM != 0;
		config.routeChoice = initial_variables.RChoice != 0;
		config.horizon = static_cast<int>(initial_variables.times);
		config.structural = qEnvironmentVariable("QEGTRAIN_PLAYBACK_PROFILE_STRUCTURAL") == QLatin1String("1");
		PlaybackProfiler::instance().configure(config);
	}
	if (m_host.startupTimingHoldsRun(timingIdentityOk))
		return;

	// scene stays loaded; only the case-study load path clears it
	m_session->updateSceneWindowTitle();
	m_host.caseLayersChanged();
	m_host.sceneActionsChanged();

	m_runtimeStatus = QStringLiteral("Running");
	m_host.loadedDataChanged();
	startSimulation(operation);
	m_window->statusBar()->showMessage(QString("Running scene: %1").arg(QString::fromStdString(m_document->model().name)));
}
