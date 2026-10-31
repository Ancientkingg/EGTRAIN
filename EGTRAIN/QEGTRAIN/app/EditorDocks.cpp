#include "app/EditorDocks.h"
#include "editors/CaseSettingsPanel.h"
#include "editors/CompositionPanel.h"
#include "editors/EntranceDelayPanel.h"
#include "editors/IncidentPanel.h"
#include "editors/InfrastructurePanel.h"
#include "editors/PassengerPanel.h"
#include "editors/ScenarioPanel.h"
#include "editors/SceneDocument.h"
#include "editors/ServicePanel.h"
#include "editors/TimetableStopsPanel.h"
#include "editors/TrainUnitPanel.h"
#include "editors/TrainUnitSourceLinker.h"
#include "editors/infrastructure/BlockRouteFacets.h"
#include "editors/infrastructure/SignalFacets.h"
#include "editors/infrastructure/StationFacets.h"
#include "editors/infrastructure/TopologyFacets.h"
#include "app/MainWindowSupport.h"
#include "app/MainWindowTestHooks.h"
#include "app/SceneSession.h"
#include "app/UpdateTelemetryController.h"
#include "diagrams/DiagramWindow.h"
#include "scene/SceneReferences.h"
#include "telemetry/TelemetryOperation.h"
#include "widgets/EditorLayout.h"
#include "widgets/FocusWheel.h"
#include <QAction>
#include <QApplication>
#include <QCursor>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QKeySequence>
#include <QLabel>
#include <QMainWindow>
#include <QScreen>
#include <QShortcut>
#include <QSizePolicy>
#include <QStatusBar>
#include <QStringList>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <memory>
#include <utility>

namespace {

// A dock floated with its button or a double click opens inside the screen area, as wide and high as its content asks where
// the screen allows. A dock that the mouse drags out keeps the place where the user drops it.
void fitFloatingDock(QDockWidget* dock, bool floating) {
	if (!floating)
		return;
	if (QGuiApplication::mouseButtons() != Qt::NoButton) {
		// A double click floats the dock while the button is still down, and a drag moves the mouse before the button is released.
		auto* release = new QTimer(dock);
		QObject::connect(release, &QTimer::timeout, dock, [dock, release, start = QCursor::pos()]() {
			if (QGuiApplication::mouseButtons() != Qt::NoButton)
				return;
			release->deleteLater();
			if (dock->isFloating() && (QCursor::pos() - start).manhattanLength() < QApplication::startDragDistance())
				fitFloatingDock(dock, true);
		});
		release->start(50);
		return;
	}
	const QRect available = dock->screen()->availableGeometry();
	const QSize frame = dock->frameGeometry().size() - dock->size();
	dock->resize(dock->size().expandedTo(dock->sizeHint()).boundedTo(available.size() - frame));
	const QRect outer = dock->frameGeometry();
	dock->move(std::clamp(outer.left(), available.left(), available.right() - outer.width() + 1),
		std::clamp(outer.top(), available.top(), available.bottom() - outer.height() + 1));
}

} // namespace

EditorDocks::EditorDocks(QMainWindow* window, Host& host) : m_window(window), m_host(host) {}

void EditorDocks::build(SceneDocument* document, UserPrompts* prompts, RunSelection& runSelection, SceneSession* session) {
	m_document = document;
	m_session = session;
	// The editors on the right keep the full height of the window: Run Results and the other docks below the network end where
	// the editors start, so that a short window still shows several rows of fields.
	m_window->setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
	m_caseSettingsDock = new QDockWidget("Case Settings", m_window);
	m_caseSettingsDock->setObjectName("caseSettingsDock");
	m_caseSettingsDock->setProperty("manualSection", QStringLiteral("case-settings"));
	m_caseSettingsPanel = new CaseSettingsPanel(m_caseSettingsDock);
	m_caseSettingsDock->setWidget(m_caseSettingsPanel);
	m_window->addDockWidget(Qt::RightDockWidgetArea, m_caseSettingsDock);
	m_caseSettingsDock->hide();
	m_host.registerEditorDock(m_caseSettingsDock);
	m_caseSettingsPanel->setDocument(m_document);
	m_caseSettingsDock->setEnabled(false);
	QObject::connect(m_document, &SceneDocument::replaced, m_caseSettingsDock, [this] { m_caseSettingsDock->setEnabled(true); });
	QObject::connect(m_caseSettingsPanel, &CaseSettingsPanel::baseTimeChanged, m_window,
		[this](const QString& baseTime) { m_host.setStartOffset(baseTimeToSeconds(baseTime.toStdString())); });
	QObject::connect(m_caseSettingsPanel, &CaseSettingsPanel::changed, m_window, [this] { m_host.refreshValidationPanel(); });

	// Infrastructure editor: one small facet selector and a table keep all
	// canonical infrastructure fields visible without introducing a second model.
	m_infrastructureDock = new QDockWidget("Infrastructure", m_window);
	m_infrastructureDock->setObjectName("infrastructureDock");
	m_infrastructurePanel = new InfrastructurePanel(m_infrastructureDock);
	m_infrastructureDock->setWidget(m_infrastructurePanel);
	m_window->addDockWidget(Qt::RightDockWidgetArea, m_infrastructureDock);
	m_infrastructureDock->hide();
	// The editor docks share one group of tabs, so each open editor has the full height of the side instead of a share of it.
	m_window->tabifyDockWidget(m_caseSettingsDock, m_infrastructureDock);
	m_host.registerEditorDock(m_infrastructureDock);
	m_infrastructurePanel->setPrompts(prompts);
	for (std::unique_ptr<InfrastructureFacet>& facet : makeTopologyFacets())
		m_infrastructurePanel->setFacet(std::move(facet));
	for (std::unique_ptr<InfrastructureFacet>& facet : makeBlockRouteFacets())
		m_infrastructurePanel->setFacet(std::move(facet));
	for (std::unique_ptr<InfrastructureFacet>& facet : makeStationFacets())
		m_infrastructurePanel->setFacet(std::move(facet));
	for (std::unique_ptr<InfrastructureFacet>& facet : makeSignalFacets())
		m_infrastructurePanel->setFacet(std::move(facet));
	m_infrastructurePanel->setDocument(m_document);
	QObject::connect(m_infrastructurePanel, &InfrastructurePanel::selectionChanged, m_window,
		[this]() { m_host.infrastructureSelectionChanged(); });
	QObject::connect(m_infrastructurePanel, &InfrastructurePanel::changed, m_window, [this] { m_host.refreshValidationPanel(); });
	QObject::connect(m_infrastructurePanel, &InfrastructurePanel::statusMessage, m_window,
		[this](const QString& text, int timeoutMs) { m_window->statusBar()->showMessage(text, timeoutMs); });
	QObject::connect(m_infrastructureDock, &QDockWidget::visibilityChanged, m_window,
		[this](bool visible) {
			if (visible)
				m_infrastructurePanel->updateSelection();
		});
	m_host.refreshInfrastructurePanel(false);

	m_trainUnitDock = new QDockWidget("Rolling Stock Units", m_window);
	m_trainUnitDock->setObjectName("trainUnitDock");
	m_trainUnitPanel = new TrainUnitPanel(m_trainUnitDock);
	m_trainUnitDock->setWidget(m_trainUnitPanel);
	m_window->addDockWidget(Qt::RightDockWidgetArea, m_trainUnitDock);
	m_window->tabifyDockWidget(m_infrastructureDock, m_trainUnitDock);
	m_host.registerEditorDock(m_trainUnitDock);
	m_trainUnitPanel->setPrompts(prompts);
	m_trainUnitSourceLinker = new TrainUnitSourceLinker(m_window);
	m_trainUnitSourceLinker->setDialogParent(m_window);
	m_trainUnitSourceLinker->setDocument(m_document);
	QObject::connect(m_trainUnitSourceLinker, &TrainUnitSourceLinker::statusChanged, m_window,
		[this] { m_host.updateTrainUnitSourceStatus(); });
	QObject::connect(m_trainUnitSourceLinker, &TrainUnitSourceLinker::commitPendingEditsRequested, m_window,
		[this] { m_host.commitPendingEditorValues(); });
	QObject::connect(m_trainUnitSourceLinker, &TrainUnitSourceLinker::statusMessage, m_window,
		[this](const QString& message, int milliseconds) { m_window->statusBar()->showMessage(message, milliseconds); });
	QObject::connect(m_trainUnitSourceLinker, &TrainUnitSourceLinker::linksCleared, m_window, [this] {
		for (DiagramWindow* window : m_window->findChildren<DiagramWindow*>()) {
			if (window && !window->rollingStockSubject().isEmpty())
				window->close();
		}
	});
	QObject::connect(m_trainUnitSourceLinker, &TrainUnitSourceLinker::reloaded, m_window,
		[this](const QStringList& tractionUnitIds) {
			m_trainUnitPanel->refresh();
			m_host.refreshValidationPanel();
			for (const QString& unitId : tractionUnitIds)
				m_host.refreshInputTractionDiagrams(unitId.toStdString());
		});
	QObject::connect(m_trainUnitPanel, &TrainUnitPanel::detailsShown, m_window, [this] { m_host.updateTrainUnitSourceStatus(); });
	QObject::connect(m_trainUnitPanel, &TrainUnitPanel::sourceLinkRequested, m_window, [this](bool traction) {
		m_host.commitPendingEditorValues();
		if (const SceneTrainUnit* unit = m_trainUnitPanel->selectedUnit())
			m_trainUnitSourceLinker->link(unit->id, traction);
	});
	QObject::connect(m_trainUnitPanel, &TrainUnitPanel::sourceUnlinkRequested, m_window, [this](bool traction) {
		if (const SceneTrainUnit* unit = m_trainUnitPanel->selectedUnit())
			m_trainUnitSourceLinker->unlink(unit->id, traction);
	});
	QObject::connect(m_trainUnitPanel, &TrainUnitPanel::sourceRetryRequested, m_window, [this](bool traction) {
		if (const SceneTrainUnit* unit = m_trainUnitPanel->selectedUnit())
			m_trainUnitSourceLinker->retry(unit->id, traction);
	});
	QObject::connect(m_trainUnitPanel, &TrainUnitPanel::unitRenamed, m_window,
		[this](const QString& oldId, const QString& newId) {
			m_trainUnitSourceLinker->renameUnit(oldId.toStdString(), newId.toStdString());
			m_host.renameTractionDiagrams(oldId, newId);
		});
	QObject::connect(m_trainUnitPanel, &TrainUnitPanel::unitDeleted, m_window, [this](const QString& id) {
		m_trainUnitSourceLinker->forgetUnit(id.toStdString());
		m_host.refreshInputTractionDiagrams(id.toStdString());
	});
	QObject::connect(m_trainUnitPanel, &TrainUnitPanel::changed, m_window, [this] { m_host.refreshValidationPanel(); });
	const auto plotUnitTraction = [this](const QString& unitId) {
		if (const SceneTrainUnit* unit = sceneTrainUnitById(m_document->model(), unitId.toStdString()))
			m_host.plotTrainUnitTraction(*unit);
	};
	QObject::connect(m_trainUnitPanel, &TrainUnitPanel::tractionPlotRequested, m_window, plotUnitTraction);
	m_trainUnitPanel->setDocument(m_document);

	m_compositionDock = new QDockWidget("Compositions", m_window);
	m_compositionDock->setObjectName("compositionDock");
	m_compositionPanel = new CompositionPanel(m_compositionDock);
	m_compositionDock->setWidget(m_compositionPanel);
	m_window->addDockWidget(Qt::RightDockWidgetArea, m_compositionDock);
	m_compositionDock->hide();
	m_window->tabifyDockWidget(m_trainUnitDock, m_compositionDock);
	m_host.registerEditorDock(m_compositionDock);
	m_compositionPanel->setPrompts(prompts);
	m_compositionPanel->setDocument(m_document);
	QObject::connect(m_compositionPanel, &CompositionPanel::changed, m_window, [this] { m_host.refreshValidationPanel(); });
	QObject::connect(m_compositionPanel, &CompositionPanel::tractionPlotRequested, m_window, plotUnitTraction);

	// service editor: dockable panel for the service-level fields, with the timetable (stops)
	// of the selected service below them. Tabbed alongside Compositions.
	m_serviceDock = new QDockWidget("Services", m_window);
	m_serviceDock->setObjectName("serviceDock");
	m_servicePanel = new ServicePanel(m_serviceDock);

	m_timetableStopsPanel = new TimetableStopsPanel(m_servicePanel);
	m_timetableStopsPanel->setProperty("manualSection", QStringLiteral("timetable-editor"));
	m_servicePanel->setTimetableWidget(m_timetableStopsPanel);

	m_serviceDock->setWidget(m_servicePanel);
	m_window->addDockWidget(Qt::RightDockWidgetArea, m_serviceDock);
	m_serviceDock->hide();
	m_window->tabifyDockWidget(m_compositionDock, m_serviceDock);
	m_host.registerEditorDock(m_serviceDock);

	// scenario library and incident editor. each
	// incident has a type (signal_failure or train_breakdown) and a target whose
	// valid choices depend on that type, analogous to a stop's platform choices
	// depending on the selected station.
	m_incidentDock = new QDockWidget("Scenarios", m_window);
	m_incidentDock->setObjectName("incidentDock");
	QWidget* incidentWidget = new QWidget(m_incidentDock);
	QVBoxLayout* incidentLayout = new QVBoxLayout(incidentWidget);

	m_scenarioPanel = new ScenarioPanel(incidentWidget);

	// The header names the scenario whose entries the tabs show, also while the scenario list is on another tab. It stays on
	// one line, and a name wider than the dock is cut at the edge; the tooltip has all of it.
	QLabel* scenarioHeaderLabel = new QLabel(incidentWidget);
	scenarioHeaderLabel->setObjectName("scenarioHeaderLabel");
	scenarioHeaderLabel->setTextFormat(Qt::PlainText);
	scenarioHeaderLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	QFont scenarioHeaderFont = scenarioHeaderLabel->font();
	scenarioHeaderFont.setBold(true);
	scenarioHeaderLabel->setFont(scenarioHeaderFont);
	incidentLayout->addWidget(scenarioHeaderLabel, 0);

	QTabWidget* scenarioEditorTabs = new QTabWidget(incidentWidget);
	scenarioEditorTabs->setObjectName("scenarioEditorTabs");
	scenarioEditorTabs->addTab(m_scenarioPanel, "Scenario");

	m_incidentPanel = new IncidentPanel(scenarioEditorTabs);
	scenarioEditorTabs->addTab(m_incidentPanel, "Incidents");

	m_entranceDelayPanel = new EntranceDelayPanel(scenarioEditorTabs);
	scenarioEditorTabs->addTab(m_entranceDelayPanel, "Entrance delays");
	scenarioEditorTabs->setMinimumSize(0, 0);
	incidentLayout->addWidget(scenarioEditorTabs, 1);

	m_incidentDock->setWidget(incidentWidget);
	m_window->addDockWidget(Qt::RightDockWidgetArea, m_incidentDock);
	m_incidentDock->hide();
	m_incidentDock->setEnabled(false);
	m_trainUnitDock->hide();
	m_window->tabifyDockWidget(m_serviceDock, m_incidentDock);
	m_host.registerEditorDock(m_incidentDock);

	m_scenarioPanel->setPrompts(prompts);
	m_scenarioPanel->setRunSelection(&runSelection);
	m_scenarioPanel->setDiagnostics(&m_session->diagnostics());
	m_scenarioPanel->setAdvancedDetails([this] { return m_host.advancedDetailsEnabled(); });
	m_scenarioPanel->setExportObserver([this] {
		const auto operation = m_host.captureTelemetryOperation();
		return [operation](const std::vector<SceneDiagnostic>& diagnostics) {
			operation.failure(telemetry::Operation::Export, telemetryDiagnosticError(diagnostics));
		};
	});
	m_scenarioPanel->setDocument(m_document);
	const auto showScenarioHeader = [scenarioHeaderLabel](const QString& text) {
		scenarioHeaderLabel->setText(text);
		scenarioHeaderLabel->setToolTip(text);
	};
	showScenarioHeader(m_scenarioPanel->headerText());
	QObject::connect(m_scenarioPanel, &ScenarioPanel::headerTextChanged, scenarioHeaderLabel, showScenarioHeader);
	QObject::connect(m_scenarioPanel, &ScenarioPanel::scenarioSelected, m_window, [this] {
		m_host.invalidateRunResults();
		m_host.refreshIncidentPanel();
	});
	QObject::connect(m_scenarioPanel, &ScenarioPanel::statusMessage, m_window,
		[this](const QString& text, int timeoutMs) { m_window->statusBar()->showMessage(text, timeoutMs); });
	QObject::connect(m_scenarioPanel, &ScenarioPanel::changed, m_window, [this] { m_host.refreshValidationPanel(); });
	m_incidentPanel->setPrompts(prompts);
	m_incidentPanel->setRunSelection(&runSelection);
	m_incidentPanel->setOccurrenceDuration([this] { return m_host.serviceOccurrenceDuration(); });
	m_incidentPanel->setDocument(m_document);
	QObject::connect(m_incidentPanel, &IncidentPanel::changed, m_window, [this] { m_host.refreshValidationPanel(); });
	m_endTargetPickShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), m_window);
	m_endTargetPickShortcut->setContext(Qt::WindowShortcut);
	m_endTargetPickShortcut->setEnabled(false);
	QObject::connect(m_endTargetPickShortcut, &QShortcut::activated, m_incidentPanel, &IncidentPanel::endTargetPick);
	// A dock behind another tab of its group stays shown, so the panel does not see it go.
	QObject::connect(m_incidentDock, &QDockWidget::visibilityChanged, m_window, [this](bool visible) {
		if (!visible)
			m_incidentPanel->endTargetPick();
	});
	m_entranceDelayPanel->setPrompts(prompts);
	m_entranceDelayPanel->setRunSelection(&runSelection);
	m_entranceDelayPanel->setOccurrenceDuration([this] { return m_host.serviceOccurrenceDuration(); });
	m_entranceDelayPanel->setDocument(m_document);
	QObject::connect(m_entranceDelayPanel, &EntranceDelayPanel::changed, m_window, [this] { m_host.refreshValidationPanel(); });

	// Passenger authoring: a deliberately direct list/detail editor over the
	// canonical SceneModel.  Journeys and legs remain nested under the selected
	// passenger; no second tree model or transient copy is needed.
	m_passengerDock = new QDockWidget("Passengers", m_window);
	m_passengerDock->setObjectName("passengerDock");
	m_passengerPanel = new PassengerPanel(m_passengerDock);
	m_passengerDock->setWidget(m_passengerPanel);
	// A dock's viewport, rather than its form, limits the main window size.
	// Keep wide tables and enlarged-font controls reachable when space is tight.
	for (QDockWidget* dock : {m_caseSettingsDock, m_infrastructureDock, m_trainUnitDock,
			 m_compositionDock, m_serviceDock, m_incidentDock, m_passengerDock}) {
		auto* content = dock->widget();
		auto* scroll = new EditorLayout::DockScrollArea(dock);
		scroll->setObjectName("editorContentScroll");
		scroll->setWidgetResizable(true);
		scroll->setFrameShape(QFrame::NoFrame);
		scroll->setMinimumSize(0, 0);
		scroll->setFocusPolicy(Qt::NoFocus);
		scroll->setWidget(content);
		dock->setWidget(scroll);
		// The wheel scrolls the dock over a field and changes the field only once it has focus.
		FocusWheel::watch(content);
		// Once any dock has been resized, Qt opens a dock at the width that its side of the window had last. An editor dock
		// that opens narrower than its size hint is widened to it, once the window has placed the dock.
		QObject::connect(dock->toggleViewAction(), &QAction::toggled, m_window, [window = m_window, dock]() {
			if (dock->isHidden() || dock->isFloating())
				return;
			dock->widget()->updateGeometry();
			if (dock->width() < dock->sizeHint().width())
				window->resizeDocks({dock}, {dock->sizeHint().width()}, Qt::Horizontal); }, Qt::QueuedConnection);
		QObject::connect(
			dock, &QDockWidget::topLevelChanged, m_window, [dock](bool floating) { fitFloatingDock(dock, floating); });
	}
	m_window->addDockWidget(Qt::RightDockWidgetArea, m_passengerDock);
	m_passengerDock->hide();
	m_window->tabifyDockWidget(m_serviceDock, m_passengerDock);
	m_host.registerEditorDock(m_passengerDock);

	m_passengerPanel->setPrompts(prompts);
	m_passengerPanel->setDiagnostics(&m_session->diagnostics());
	m_passengerPanel->setOccurrenceDuration([this] { return m_host.serviceOccurrenceDuration(); });
	m_passengerPanel->setImportFolderChooser([this]() -> QString {
		if (const auto& passengerSource = mainWindowTestHooks().passengerSource) {
			const QString sourcePath = passengerSource();
			if (!sourcePath.isEmpty())
				return sourcePath;
		}
		const QString startDir = m_session->sceneDir().isEmpty() ? QDir::homePath() : QFileInfo(m_session->sceneDir()).absolutePath();
		return QFileDialog::getExistingDirectory(m_window, "Select Legacy Passenger Folder", startDir);
	});
	m_passengerPanel->setDocument(m_document);
	m_passengerDock->setEnabled(false);
	QObject::connect(m_passengerPanel, &PassengerPanel::statusMessage, m_window,
		[this](const QString& text, int timeoutMs) { m_window->statusBar()->showMessage(text, timeoutMs); });
	QObject::connect(m_passengerPanel, &PassengerPanel::changed, m_window, [this] { m_host.refreshValidationPanel(); });

	m_servicePanel->setPrompts(prompts);
	m_timetableStopsPanel->setPrompts(prompts);
	m_servicePanel->setRunSelection(&runSelection);
	m_servicePanel->setOccurrenceDuration([this] { return m_host.serviceOccurrenceDuration(); });
	QObject::connect(m_servicePanel, &ServicePanel::selectedServiceChanged, m_timetableStopsPanel,
		&TimetableStopsPanel::setServiceId);
	QObject::connect(m_timetableStopsPanel, &TimetableStopsPanel::changed, m_window, [this] { m_host.refreshValidationPanel(); });
	// The window does not validate the edits that its own commit makes. When the text that it took changed the scene, the
	// scene is validated here, so that the validation is current also when the dialog is cancelled.
	QObject::connect(m_timetableStopsPanel, &TimetableStopsPanel::commitPendingEditsRequested, m_window, [this] {
		const quint64 revision = m_document->revision();
		m_host.commitPendingEditorValues();
		if (m_document->revision() != revision)
			m_host.refreshValidationPanel();
	});
	QObject::connect(m_servicePanel, &ServicePanel::changed, m_window, [this] { m_host.refreshValidationPanel(); });
	m_servicePanel->setDocument(m_document);
	m_timetableStopsPanel->setDocument(m_document);
	m_serviceDock->setEnabled(false);
	m_panels = {m_caseSettingsPanel, m_trainUnitPanel, m_infrastructurePanel, m_passengerPanel, m_compositionPanel,
		m_servicePanel, m_timetableStopsPanel, m_scenarioPanel, m_incidentPanel, m_entranceDelayPanel};
}
