#include "app/RunController.h"
// This header brings in SceneModel.h in the way that the member named signals needs, before the other scene headers do.
#include "editors/SceneDocument.h"
#include "app/DispatchController.h"
#include "app/MainWindowSupport.h"
#include "app/MainWindowTestHooks.h"
#include "app/PathMessages.h"
#include "app/RunResultsDock.h"
#include "app/SceneOutputDirectory.h"
#include "app/UpdateTelemetryController.h"
#include "diagrams/DelayAnalysisWindow.h"
#include "diagrams/ResultFrame.h"
#include "editors/CountText.h"
#include "editors/ServicePanel.h"
#include "editors/ValidationPanel.h"
#include "simulation/SimulationRun.h"
#include "util/PlaybackProfiler.h"
#include "util/TimeFormat.h"
#include "widgets/DialogLayout.h"
#include "widgets/UserPrompts.h"
#include <QApplication>
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
#include <exception>

QString completedRunContext(const RunProvenance& provenance) {
	return runContextText(provenance);
}

RunController::RunController(QMainWindow* window, SceneDocument* document, SceneSession* session, RunSelection& selection, Host& host)
	: QObject(window), m_window(window), m_document(document), m_session(session), m_selection(selection), m_host(host),
	  m_startOffsetSeconds(initial_variables.startingSimulationTime) {}

bool RunController::nextRunShowsPassengers() const {
	return initial_variables.PAX_GUI;
}

void RunController::setNextRunShowsPassengers(bool on) {
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

const QString& RunController::runtimeStatus() const {
	static const QString notBuiltText = QStringLiteral("Not built");
	static const QString readyText = QStringLiteral("Ready");
	static const QString runningText = QStringLiteral("Running");
	static const QString completedText = QStringLiteral("Completed");
	static const QString stoppedText = QStringLiteral("Stopped");
	static const QString failedText = QStringLiteral("Failed");
	switch (m_state) {
		case RunState::NotBuilt:
			return notBuiltText;
		case RunState::Ready:
			return readyText;
		case RunState::Running:
			return runningText;
		case RunState::Completed:
			return completedText;
		case RunState::Stopped:
			return stoppedText;
		case RunState::Failed:
			return failedText;
	}
	return notBuiltText;
}

void RunController::setState(RunState state) {
	m_state = state;
	if (state == RunState::Ready || state == RunState::Running)
		emit loadedDataChanged();
}

bool RunController::hasRawRunResults() {
	for (int i = 0; i < simulation.run().numTrains; ++i)
		if (simulation.run().trains[i].trajectorySize() > 0)
			return true;
	return false;
}

// Raw trajectories can survive a scene edit; the state of the run is the
// ownership boundary for result actions.
bool RunController::hasRunResults() const {
	return resultsAvailable() && m_completedRun && hasRawRunResults();
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
	emit playbackEnded();
	// Retain the final frame for passenger inspection until scene teardown.
	m_worker = nullptr;
	m_workerThread = nullptr;
	m_document->setEditable(true);
	emit runPhaseChanged(RunPhase::Idle);
}

// The Run action always prepares the currently loaded canonical scene first.
void RunController::runCurrent() {
	if (m_worker)
		return;
	if (m_document->isLoaded())
		runScene();
	else
		m_window->statusBar()->showMessage("Open a case first", 5000);
}

namespace {

// One line of the run details for an incident, such as "Incident i1: train breakdown of svc.a #2, from 100 s, no end,
// reduced speed 40 km/h".
QString incidentLine(const SceneIncident& incident) {
	const QString type = incident.type == "signal_failure" ? QStringLiteral("signal failure")
		: incident.type == "train_breakdown"			   ? QStringLiteral("train breakdown")
														   : QString::fromStdString(incident.type);
	QString target = incident.target.empty() ? QStringLiteral("no target") : QString::fromStdString(incident.target);
	if (incident.hasOccurrence || incident.occurrence != 1)
		target += QStringLiteral(" #%1").arg(incident.occurrence);
	const QString start = QString::number(incident.startSeconds, 'g', 12);
	QString line = QStringLiteral("Incident %1: %2 of %3, ").arg(QString::fromStdString(incident.id), type, target)
		+ (incident.hasEndSeconds || incident.endSeconds != 0.0
				? QStringLiteral("%1 to %2 s").arg(start, QString::number(incident.endSeconds, 'g', 12))
				: QStringLiteral("from %1 s, no end").arg(start));
	if (incident.hasReducedSpeed)
		line += QStringLiteral(", reduced speed %1 km/h").arg(QString::number(incident.reducedSpeedKmh, 'g', 12));
	if (incident.terminateAtDestination)
		line += QStringLiteral(", ends its run at the destination");
	return line;
}

} // namespace

QString RunController::runReviewText(bool advanced) const {
	const SceneModel& model = m_document->model();
	const double duration = m_host.serviceOccurrenceDuration();
	const int total = m_host.totalServiceOccurrences();
	const std::vector<SceneIncident>& incidents = m_host.selectedScenarioIncidents();
	QStringList lines{QStringLiteral("Case: %1").arg(QString::fromStdString(model.name)),
		QStringLiteral("Scenario: %1").arg(m_host.scenarioContext()),
		QStringLiteral("Services: %1").arg(static_cast<int>(model.services.size())),
		QStringLiteral("Run occurrences: ")
			+ ServicePanel::occurrenceSummaryText(total, m_selection.selectedCount(model, duration),
				m_selection.selectedInPeriodCount(model, duration), total),
		QStringLiteral("Compositions: %1").arg(static_cast<int>(model.compositions.size()))};
	if (!incidents.empty())
		lines << QStringLiteral("Incidents: %1").arg(static_cast<int>(incidents.size()));
	if (!advanced)
		return lines.join('\n');
	const QString validation = ValidationPanel::statusText(countDiagnostics(m_session->diagnostics()), true);
	if (!validation.isEmpty())
		lines << QStringLiteral("Validation: ") + validation;
	for (const SceneIncident& incident : incidents)
		lines << incidentLine(incident);
	if (const SceneScenario* scenario = m_host.selectedScenario()) {
		for (const SceneEntranceDelay& delay : scenario->entranceDelays) {
			const auto service = std::find_if(model.services.begin(), model.services.end(),
				[&delay](const SceneService& candidate) { return candidate.id == delay.serviceId; });
			const QString code = service == model.services.end()
				? QString()
				: QString::fromStdString(sceneServiceOccurrenceOperatingCode(*service, delay.occurrence));
			lines << QStringLiteral("Entrance delay: %1 s for %2 #%3%4 at %5")
						 .arg(QString::number(delay.delaySeconds, 'g', 12),
							 delay.serviceId.empty() ? QStringLiteral("no service") : QString::fromStdString(delay.serviceId))
						 .arg(delay.occurrence)
						 .arg(code.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(code),
							 delay.stationId.empty() ? QStringLiteral("no station") : QString::fromStdString(delay.stationId));
		}
	}
	return lines.join('\n');
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
	const int incidents = static_cast<int>(m_host.selectedScenarioIncidents().size());
	const long long baseSeconds = baseTimeToSeconds(m_document->model().baseTime);
	auto* facts = new QGridLayout();
	facts->setHorizontalSpacing(20);
	facts->setVerticalSpacing(8);
	const auto addFact = [&](const QString& label, const QString& value) {
		auto* name = new QLabel(label, body);
		name->setObjectName("runReviewFactLabel");
		auto* fact = new QLabel(value, body);
		fact->setTextFormat(Qt::PlainText);
		fact->setWordWrap(true);
		fact->setObjectName("runReviewFactValue");
		const int row = facts->rowCount();
		facts->addWidget(name, row, 0);
		facts->addWidget(fact, row, 1);
	};
	// A count of zero is left out; the status below says when no train starts.
	if (selectedInPeriod > 0)
		addFact("Trains", QString::number(selectedInPeriod));
	addFact("Start time", QString::fromStdString(formatSimTime(0, baseSeconds)));
	addFact("Duration",
		QString("%1, until %2")
			.arg(RunResultsDock::durationText(static_cast<long long>(duration)),
				QString::fromStdString(formatSimTime(static_cast<long long>(duration), baseSeconds))));
	if (incidents > 0)
		addFact("Incidents", QString::number(incidents));
	layout->addLayout(facts);

	auto* status = new QLabel(body);
	status->setObjectName("runReviewStatus");
	const bool noServicesInPeriod = selectedInPeriod == 0;
	// The validator message names the route sections and tracks without a signalling level.
	QString unsignalledSections;
	for (const SceneDiagnostic& diagnostic : m_session->diagnostics()) {
		if (diagnostic.code == "scene.signalling.level.missing") {
			unsignalledSections = QString::fromStdString(diagnostic.message);
			break;
		}
	}
	// Only something that needs attention has a line; the dialog and its Run button say that the case can run.
	status->setText(noServicesInPeriod
			? QStringLiteral("No selected trains start in this period.")
			: !unsignalledSections.isEmpty()
			? unsignalledSections + QLatin1Char('.')
			: detailsEnabled && counts.warnings > 0
			? countText(counts.warnings, QStringLiteral("validation warning"), QStringLiteral("validation warnings"))
				+ QStringLiteral(". See Case Validation.")
			: QString());
	status->setProperty("warning", !status->text().isEmpty());
	status->setWordWrap(true);
	status->setVisible(!status->text().isEmpty());
	layout->addWidget(status);

	auto* detailsToggle = new QToolButton(body);
	detailsToggle->setText("Run details");
	detailsToggle->setObjectName("runReviewDetailsToggle");
	detailsToggle->setCheckable(true);
	detailsToggle->setArrowType(Qt::RightArrow);
	detailsToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	// The text size of the dialog; macOS gives a tool button a smaller font.
	detailsToggle->setFont(QApplication::font());
	auto* details = new QLabel(body);
	details->setObjectName("runReviewDetails");
	details->setTextFormat(Qt::PlainText);
	details->setWordWrap(true);
	details->setTextInteractionFlags(Qt::TextSelectableByMouse);
	details->setText(runReviewText(detailsEnabled));
	details->setToolTip(QStringLiteral("An occurrence is in the run period when it enters from 0 up to %1 s.")
			.arg(QString::number(duration, 'g', 12)));
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
	buttons->addButton(QDialogButtonBox::Cancel)->setObjectName("runReviewCancelButton");
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
	if (!resultsAvailable() || !m_completedRun || m_completedRun->results.trains.empty())
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

	// The tooltips of the two buttons say why one is disabled.
	if (m_delayBaseline)
		state.feedback = QStringLiteral("Baseline: %1").arg(completedRunContext(m_delayBaseline->provenance));
	else if (!resultsShown && !m_delayBaselineStatus.isEmpty())
		state.feedback = m_delayBaselineStatus;
	else
		state.feedback = QStringLiteral("No baseline set");
	return state;
}

void RunController::setDelayBaseline() {
	const DelayRunSnapshot snapshot = completedDelaySnapshot();
	if (snapshot.scenarioId.empty() || snapshot.run.trains.empty()) {
		showError("Delay Baseline Unavailable", "Complete a run with no incidents or entrance delays first.");
		return;
	}
	if (snapshot.hasIncidents || snapshot.hasEntranceDelays) {
		showError("Delay Baseline Rejected", "The delay baseline must have no incidents and no entrance delays.");
		return;
	}
	m_delayBaseline = snapshot;
	m_delayBaselineStatus.clear();
	m_window->statusBar()->showMessage(QString("Delay baseline set to scenario %1").arg(QString::fromStdString(snapshot.scenarioId)), 5000);
	emit delayBaselineChanged();
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
		m_window->statusBar()->showMessage("Run the case first", 5000);
		return nullptr;
	}
	auto* window = new DelayAnalysisWindow(completedDelaySnapshot(), m_delayBaseline, m_window);
	window->setTelemetryCapture([this] { return m_host.captureTelemetryOperation(); });
	window->setAttribute(Qt::WA_DeleteOnClose);
	window->show();
	return window;
}

bool RunController::sceneChanged() {
	if (m_delayBaseline)
		m_delayBaselineStatus = QStringLiteral("Baseline cleared: the case changed");
	m_delayBaseline.reset();
	if (!m_worker)
		return false;
	m_sceneChangedDuringRun = true;
	return true;
}

void RunController::discardResults() {
	setState(RunState::NotBuilt);
	m_runtimeDiagnostics.clear();
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

	emit runStarting();

	// create worker and thread; sync slider before run() starts
	m_worker = new SimulationWorker();
	m_document->setEditable(false);
	m_worker->setDelayMs(m_host.stepDelayMs());
	emit runPanelsChanged();
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
			simulationObservation(ObservationSlot::Rejected) = connect(
				&simulation, &DispatchController::executionRejected, &simulation,
				[observation] { observation->reject(telemetry::Error::InvalidInput); }, Qt::DirectConnection);
			if (observationStep) observationStep(1);
			simulationObservation(ObservationSlot::Began) = connect(
				&simulation, &DispatchController::executionBegan, &simulation,
				[observation] { observation->begin(); }, Qt::DirectConnection);
			if (observationStep) observationStep(2);
			simulationObservation(ObservationSlot::Returned) = connect(
				&simulation, &DispatchController::executionReturned, &simulation,
				[observation](qint64 elapsedMs, bool cancelled) {
					observation->finish(
						cancelled ? telemetry::SimulationObservation::Outcome::Cancelled : telemetry::SimulationObservation::Outcome::Completed,
						elapsedMs);
				},
				Qt::DirectConnection);
			simulationObservation(ObservationSlot::Failed) = connect(
				&simulation, &DispatchController::executionFailed, &simulation,
				[observation](qint64 elapsedMs) {
					observation->finish(telemetry::SimulationObservation::Outcome::Failed, elapsedMs, telemetry::Error::InternalFailure);
				},
				Qt::DirectConnection);
			if (observationStep) observationStep(3);
			if (!simulationObservation(ObservationSlot::Rejected) || !simulationObservation(ObservationSlot::Began)
				|| !simulationObservation(ObservationSlot::Returned) || !simulationObservation(ObservationSlot::Failed))
				rollback();
		} catch (...) {
			rollback();
		}
	}
	// A slot of workerReady can connect to the worker, so it runs before the thread starts.
	emit workerReady();
	m_workerThread->start();
	emit runPhaseChanged(RunPhase::Running);
}

bool RunController::deferUntilRunStopped(std::function<void()> action) {
	if (!m_worker)
		return false;
	m_afterRunAction = std::move(action);
	m_worker->requestStop();
	emit runPhaseChanged(RunPhase::Stopping);
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
	m_host.endFollowing();
	if (PlaybackProfiler::enabled() && PlaybackProfiler::instance().frozen()) {
		PlaybackProfiler::instance().emitRecords(true);
		clearSimulationWorker(false);
		QCoreApplication::exit(0);
		return;
	}
	const bool sceneChangedDuringRun = m_sceneChangedDuringRun;
	m_sceneChangedDuringRun = false;
	// Why an exception ended the run, or empty.
	const QString failure = QString::fromStdString(simulation.lastRunError());
	// A stop that arrives after the last stage leaves a completed run.
	const bool stopped = failure.isEmpty() && m_worker && m_worker->isStopRequested() && !simulation.lastRunCompleted();
	const bool stoppedByUser = stopped && !sceneChangedDuringRun;
	const bool kept = failure.isEmpty() && !sceneChangedDuringRun && !stopped && hasRawRunResults();
	if (kept)
		setState(RunState::Completed);
	else
		setState(stoppedByUser ? RunState::Stopped : RunState::Failed);
	if (resultsAvailable()) {
		// Freeze result values before a subsequent run replaces the runtime trains.
		m_completedRun = std::make_shared<const CompletedRunData>(
			captureCompletedRun(simulation.run(), &m_document->model(), m_startOffsetSeconds, m_pendingRunProvenance));
		emit resultsKept();
	} else {
		m_pendingRunProvenance = RunProvenance();
		m_completedRun.reset();
		emit resultsDiscarded(stoppedByUser, sceneChangedDuringRun);
	}
	m_pendingRunProvenance = RunProvenance();
	emit loadedDataChanged();

	if (m_host.runFinished())
		return;

	// print last services
	if (!stopped && failure.isEmpty())
		simulation.printLastTrainServicePathDiagram();

	emit runProgressEnded();
	const QString status = sceneChangedDuringRun
		? QStringLiteral("Results discarded: the case changed during the run")
		: !failure.isEmpty()  ? QStringLiteral("Simulation failed")
		: stopped			  ? QStringLiteral("Simulation stopped")
		: !resultsAvailable() ? QStringLiteral("Simulation ended without results")
							  : QStringLiteral("Simulation complete");
	m_window->statusBar()->showMessage(status);
	// The Run Results dock raised by resultsKept is the completion notice;
	// diagram entries switch on here instead of a modal prompt chain.
	// cleanup thread
	clearSimulationWorker(false);
	if (resultsAvailable() && !stopped)
		m_host.replayReady(simulation.takeReplayCandidate());
	else
		simulation.resetReplayCandidate();
	emit runEnded();
	// The track preview that replaces a run without results cleared the status bar.
	if (!resultsAvailable())
		m_window->statusBar()->showMessage(status);
	if (!failure.isEmpty())
		showError(QStringLiteral("Simulation Failed"),
			QStringLiteral("The run stopped because of an error:\n\n%1").arg(failure));

	emit runSettled();
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
		showError("Cannot Run Case", ValidationPanel::statusText(countDiagnostics(m_session->diagnostics()), false) + QStringLiteral("\n\nFix the errors in Case Validation."));
		return;
	}
	m_selection.pruneExcluded(m_document->model(), m_host.serviceOccurrenceDuration());
	m_lastRunTotalOccurrences = m_host.totalServiceOccurrences();
	m_lastRunSelectedOccurrences = m_selection.selectedCount(m_document->model(), m_host.serviceOccurrenceDuration());
	m_lastRunSelectedInPeriodOccurrences = m_selection.selectedInPeriodCount(m_document->model(), m_host.serviceOccurrenceDuration());
	if (m_lastRunSelectedOccurrences <= 0) {
		operation.failure(telemetry::Operation::Simulation, telemetry::Error::InvalidInput);
		showError("Cannot Run Case", "Select at least one occurrence on the Run occurrences tab of Services.");
		return;
	}
	if (!showRunReview())
		return;

	emit runPreparing();
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
		[&]() -> std::vector<SceneDiagnostic> {
			try {
				return simulation.prepareScene(m_document->model(), m_selection.scenarioId(), selection);
			} catch (const std::exception& error) {
				simulation.resetState();
				SceneDiagnostic diagnostic;
				diagnostic.code = "run.prepare.exception";
				diagnostic.message = std::string("Cannot prepare the run: ") + error.what();
				return {diagnostic};
			}
		},
		timingIdentityOk);
	m_runtimeDiagnostics = diagnostics;
	if (hasErrors(diagnostics)) {
		operation.failure(telemetry::Operation::Simulation, telemetryDiagnosticError(diagnostics));
		setState(RunState::Failed);
		emit sceneActionsChanged();
		m_host.validateScene();
		showError("Cannot Run Case", firstDiagnosticMessage(diagnostics));
		return;
	}
	setState(RunState::Ready);
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
		config.horizon = static_cast<int>(initial_variables.times);
		config.structural = qEnvironmentVariable("QEGTRAIN_PLAYBACK_PROFILE_STRUCTURAL") == QLatin1String("1");
		PlaybackProfiler::instance().configure(config);
	}
	if (m_host.startupTimingHoldsRun(timingIdentityOk))
		return;

	// scene stays loaded; only the case-study load path clears it
	m_session->updateSceneWindowTitle();
	emit caseLayersChanged();
	emit sceneActionsChanged();

	setState(RunState::Running);
	startSimulation(operation);
	m_window->statusBar()->showMessage(QStringLiteral("Running"));
}
