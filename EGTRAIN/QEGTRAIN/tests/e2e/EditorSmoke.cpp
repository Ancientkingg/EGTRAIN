#include "tests/e2e/MainWindowTestAccess.h"

#include "app/MainWindowSupport.h"
#include "app/MainWindowTestHooks.h"
#include "ui_MainWindow.h"

#include "diagrams/DiagramWindow.h"
#include "diagrams/TractionCurve.h"
#include "editors/CompositionPanel.h"
#include "editors/IncidentPanel.h"
#include "editors/TrainUnitPanel.h"
#include "editors/TrainUnitSourceLinker.h"
#include "editors/PassengerPanel.h"
#include "editors/ScenarioPanel.h"
#include "editors/ServicePanel.h"
#include "editors/TimetableStopsPanel.h"
#include "graphics/items/HighlightEffect.h"
#include "scene/SceneBundle.h"
#include "scene/SceneDiagnostic.h"
#include "scene/SceneIds.h"
#include "scene/SceneModel.h"
#include "scene/SceneReferences.h"
#include "scene/SceneWriter.h"
#include "scene/SectionInventory.h"
#include "scene/SignallingLevel.h"
#include "scene/SignallingLevelNames.h"
#include "scene/StopInsertion.h"
#include "scene/TrackPreview.h"
#include "simulation/Infrastructure.h"
#include "simulation/Passengers.h"
#include "simulation/RollingStock.h"
#include "simulation/Signalling.h"
#include "simulation/SimulationRun.h"
#include "util/TimeFormat.h"
#include "widgets/ColorChoiceButton.h"
#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QChar>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QGraphicsItem>
#include <QIODevice>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QMessageBox>
#include <QMetaObject>
#include <QObject>
#include <QPlainTextEdit>
#include <QPoint>
#include <QPointer>
#include <QPointF>
#include <QPushButton>
#include <QRectF>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSize>
#include <QSpinBox>
#include <QString>
#include <QStringList>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVariant>
#include <QWheelEvent>
#include <QWidget>
#include <QtCharts/QAbstractAxis>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

// Writes CSV text to a path, all of it or nothing.
bool writeCsvFile(const QString& path, const std::string& content) {
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly))
		return false;
	const QByteArray bytes = QByteArray::fromStdString(content);
	return file.write(bytes) == bytes.size() && file.commit();
}

// Clicks Import Passengers with a folder given in place of the dialog that asks for one.
void importPassengersFrom(QPushButton* button, const QString& folder) {
	mainWindowTestHooks().passengerSource = [folder] { return folder; };
	button->click();
	QApplication::processEvents();
	mainWindowTestHooks().passengerSource = nullptr;
}

} // namespace

// Opens the scene named by QEGTRAIN_E2E_SCENE, and QEGTRAIN_E2E_SCENE_ALT when that is another one, and drives each editor of
// the window through its controls: a new case, infrastructure, stations and signalling, passengers, the explorer, train units
// and their source links, compositions, services and their occurrences, timetables, entrance delays, incidents, validation,
// saving and reloading, the layout of the docks, and that visiting the docks leaves the case untouched. Every facet prints its
// E2E_EDITOR_*_OK marker when its checks hold.
// QEGTRAIN_E2E_OUT names a folder for the files that it writes. It prints E2E_EDITOR_SMOKE_OK and exits with 0, or prints
// the failures and exits with 1.
void MainWindowTestAccess::runEditorSmoke(MainWindow& w) {
	if (editorSmokeStarted)
		return;
	editorSmokeStarted = true;

	bool ok = true;
	QStringList failures;
	std::vector<SceneTrainUnit> expectedTrainUnits;
	std::vector<SceneComposition> expectedCompositions;
	std::vector<SceneService> expectedServices;
	std::vector<SceneIncident> expectedIncidents;
	std::vector<SceneTrack> expectedTracks;
	std::vector<SceneNode> expectedNodes;
	std::vector<SceneArc> expectedArcs;
	std::vector<SceneBlock> expectedBlocks;
	std::vector<SceneConnection> expectedConnections;
	std::vector<SceneRoute> expectedRoutes;
	std::vector<SceneBlockDependency> expectedBlockDependencies;
	std::vector<SceneSingleTrackRestriction> expectedSingleTrackRestrictions;
	std::vector<SceneStationBoundary> expectedStationBoundaries;
	std::vector<SceneSignallingArea> expectedSignallingAreas;
	std::vector<SceneStation> expectedStations;
	std::vector<SceneSignal> expectedSignals;
	std::vector<ScenePassenger> expectedPassengers;
	std::string e2ePassengerId;
	std::string e2eJourneyId;
	std::string e2eLegId;
	std::vector<SceneService> expectedNewCaseServices;
	std::vector<SceneIncident> expectedNewCaseIncidents;
	std::vector<SceneEntranceDelay> expectedEntranceDelays;
	QTemporaryDir sourceLinkFixture;
	QString sourcePhysicalPath;
	QString sourceTractionPath;
	std::string sourceLinkUnitId;

	auto facetFailure = [&](bool& facetOk, const char* facet, const QString& message) {
		facetOk = false;
		ok = false;
		failures << QString("%1: %2").arg(facet).arg(message);
	};
	auto acceptConfirmation = [&w]() {
		QTimer::singleShot(25, &w, []() {
			for (QWidget* widget : QApplication::topLevelWidgets()) {
				auto* dialog = qobject_cast<QMessageBox*>(widget);
				if (!dialog || !dialog->isVisible())
					continue;
				if (auto* button = dialog->button(QMessageBox::Yes))
					button->click();
				else
					dialog->done(QMessageBox::Yes);
				break;
			}
		});
	};
	auto cancelConfirmation = [&w]() {
		QTimer::singleShot(25, &w, []() {
			for (QWidget* widget : QApplication::topLevelWidgets()) {
				auto* dialog = qobject_cast<QMessageBox*>(widget);
				if (!dialog || !dialog->isVisible())
					continue;
				if (auto* button = dialog->button(QMessageBox::Cancel))
					button->click();
				else
					dialog->done(QMessageBox::Cancel);
				break;
			}
		});
	};
	auto discardConfirmation = [&w]() {
		QTimer::singleShot(25, &w, []() {
			for (QWidget* widget : QApplication::topLevelWidgets()) {
				auto* dialog = qobject_cast<QMessageBox*>(widget);
				if (!dialog || !dialog->isVisible())
					continue;
				if (auto* button = dialog->button(QMessageBox::Discard))
					button->click();
				else
					dialog->done(QMessageBox::Discard);
				break;
			}
		});
	};
	auto acceptUnitChoice = [&w](const QString& unitId) {
		QTimer::singleShot(25, &w, [unitId]() {
			for (QWidget* widget : QApplication::topLevelWidgets()) {
				auto* dialog = qobject_cast<QInputDialog*>(widget);
				if (!dialog || !dialog->isVisible())
					continue;
				dialog->setTextValue(unitId);
				QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
				break;
			}
		});
	};
	auto editStopDialog = [&w](int row, const std::function<void(QDialog*)>& edit,
							  QPushButton* opener = nullptr, const std::function<void(QDialog*)>& afterAccept = nullptr) {
		bool accepted = false;
		QTimer timer;
		timer.setSingleShot(true);
		QObject::connect(&timer, &QTimer::timeout, &w, [&]() {
			auto* dialog = w.findChild<QDialog*>("stopEditorDialog");
			if (!dialog) return;
			edit(dialog);
			dialog->findChild<QPushButton*>("stopEditorAcceptButton")->click();
			accepted = dialog->result() == QDialog::Accepted;
			if (afterAccept) afterAccept(dialog);
			if (!accepted) dialog->reject();
		});
		timer.start(0);
		if (opener) opener->click();
		else if (row < 0) w.m_addStopButton->click();
		else w.m_timetableStopsPanel->editStop(row);
		w.activateWindow();
		QApplication::processEvents();
		return accepted;
	};
	auto sameStop = [](const SceneStop& left, const SceneStop& right) {
		return left.stationId == right.stationId && left.platformId == right.platformId && left.hasPlannedArrival == right.hasPlannedArrival && left.hasPlannedDeparture == right.hasPlannedDeparture && left.plannedArrivalSeconds == right.plannedArrivalSeconds && left.plannedDepartureSeconds == right.plannedDepartureSeconds && left.dwellSeconds == right.dwellSeconds;
	};
	auto sameCompositions = [](const std::vector<SceneComposition>& left, const std::vector<SceneComposition>& right) {
		if (left.size() != right.size())
			return false;
		return std::equal(left.begin(), left.end(), right.begin(), [](const SceneComposition& a, const SceneComposition& b) {
			return a.id == b.id && a.units == b.units;
		});
	};
	auto sameTrainUnits = [](const std::vector<SceneTrainUnit>& left, const std::vector<SceneTrainUnit>& right) {
		if (left.size() != right.size())
			return false;
		return std::equal(left.begin(), left.end(), right.begin(), [](const SceneTrainUnit& a, const SceneTrainUnit& b) {
			return a.id == b.id && a.hasPhysical == b.hasPhysical && a.physical.mass_of_traction_unit_kg == b.physical.mass_of_traction_unit_kg && a.physical.mass_of_a_wagon_kg == b.physical.mass_of_a_wagon_kg && a.physical.number_of_wagons == b.physical.number_of_wagons && a.physical.max_speed_ms == b.physical.max_speed_ms && a.physical.max_deceleration_ms2 == b.physical.max_deceleration_ms2 && a.physical.frontal_area_m2 == b.physical.frontal_area_m2 && a.physical.resistance_coefficient == b.physical.resistance_coefficient && a.physical.jerk_ms3 == b.physical.jerk_ms3 && a.physical.length_m == b.physical.length_m && a.tractionCurve == b.tractionCurve && a.sourceDataFile == b.sourceDataFile && a.sourceTractionFile == b.sourceTractionFile;
		});
	};
	auto sameServices = [&](const std::vector<SceneService>& left, const std::vector<SceneService>& right) {
		if (left.size() != right.size())
			return false;
		return std::equal(left.begin(), left.end(), right.begin(), [&](const SceneService& a, const SceneService& b) {
			if (a.id != b.id || a.operatingCode != b.operatingCode || a.category != b.category || a.visualizationColor != b.visualizationColor || a.composition != b.composition || a.route != b.route
				|| a.performancePercent != b.performancePercent || a.hasMaximumSpeed != b.hasMaximumSpeed
				|| a.maximumSpeedKmh != b.maximumSpeedKmh || a.through != b.through
				|| a.hasEntryTime != b.hasEntryTime || a.entryTimeSeconds != b.entryTimeSeconds
				|| a.hasRepeat != b.hasRepeat || a.headwaySeconds != b.headwaySeconds
				|| a.hasRepeatCount != b.hasRepeatCount || a.repeatCount != b.repeatCount
				|| a.hasOperatingCodeStep != b.hasOperatingCodeStep
				|| a.operatingCodeStep != b.operatingCodeStep || a.stops.size() != b.stops.size())
				return false;
			return std::equal(a.stops.begin(), a.stops.end(), b.stops.begin(), sameStop);
		});
	};
	auto sameIncidents = [](const std::vector<SceneIncident>& left, const std::vector<SceneIncident>& right) {
		if (left.size() != right.size())
			return false;
		return std::equal(left.begin(), left.end(), right.begin(), [](const SceneIncident& a, const SceneIncident& b) {
			return a.id == b.id && a.type == b.type && a.target == b.target
				&& a.startSeconds == b.startSeconds && a.endSeconds == b.endSeconds
				&& a.hasOccurrence == b.hasOccurrence && a.occurrence == b.occurrence
				&& a.hasReducedSpeed == b.hasReducedSpeed
				&& a.reducedSpeedKmh == b.reducedSpeedKmh
				&& a.terminateAtDestination == b.terminateAtDestination
				&& a.hasEndSeconds == b.hasEndSeconds;
		});
	};
	auto sameEntranceDelays = [](const std::vector<SceneEntranceDelay>& left,
								  const std::vector<SceneEntranceDelay>& right) {
		if (left.size() != right.size())
			return false;
		return std::equal(left.begin(), left.end(), right.begin(), [](const SceneEntranceDelay& a, const SceneEntranceDelay& b) {
			return a.serviceId == b.serviceId && a.occurrence == b.occurrence
				&& a.stationId == b.stationId && a.delaySeconds == b.delaySeconds;
		});
	};
	auto sameStations = [](const std::vector<SceneStation>& left, const std::vector<SceneStation>& right) {
		if (left.size() != right.size())
			return false;
		return std::equal(left.begin(), left.end(), right.begin(), [](const SceneStation& a, const SceneStation& b) {
			if (a.id != b.id || a.name != b.name || a.hasPosition != b.hasPosition || a.positionKm != b.positionKm || a.platforms.size() != b.platforms.size())
				return false;
			return std::equal(a.platforms.begin(), a.platforms.end(), b.platforms.begin(),
				[](const ScenePlatform& x, const ScenePlatform& y) {
					return x.id == y.id && x.nodeIds == y.nodeIds
						&& x.hasLength == y.hasLength && x.lengthM == y.lengthM
						&& x.hasWidth == y.hasWidth && x.widthM == y.widthM;
				});
		});
	};
	auto samePassengers = [](const std::vector<ScenePassenger>& left,
							  const std::vector<ScenePassenger>& right) {
		if (left.size() != right.size())
			return false;
		return std::equal(left.begin(), left.end(), right.begin(), [](const ScenePassenger& a, const ScenePassenger& b) {
			if (a.id != b.id || a.journeys.size() != b.journeys.size())
				return false;
			return std::equal(a.journeys.begin(), a.journeys.end(), b.journeys.begin(),
				[](const ScenePassengerJourney& x, const ScenePassengerJourney& y) {
					if (x.id != y.id || x.activity != y.activity || x.originStationId != y.originStationId
						|| x.destinationStationId != y.destinationStationId
						|| x.plannedDepartureStartSeconds != y.plannedDepartureStartSeconds
						|| x.plannedDepartureEndSeconds != y.plannedDepartureEndSeconds
						|| x.plannedArrivalStartSeconds != y.plannedArrivalStartSeconds
						|| x.plannedArrivalEndSeconds != y.plannedArrivalEndSeconds
						|| x.legs.size() != y.legs.size())
						return false;
					return std::equal(x.legs.begin(), x.legs.end(), y.legs.begin(),
						[](const ScenePassengerLeg& u, const ScenePassengerLeg& v) {
							return u.id == v.id && u.originStationId == v.originStationId
								&& u.destinationStationId == v.destinationStationId
								&& u.serviceId == v.serviceId && u.occurrence == v.occurrence;
						});
				});
		});
	};
	auto sameSignals = [](const std::vector<SceneSignal>& left, const std::vector<SceneSignal>& right) {
		if (left.size() != right.size())
			return false;
		return std::equal(left.begin(), left.end(), right.begin(), [](const SceneSignal& a, const SceneSignal& b) {
			return a.id == b.id && a.protectedSection == b.protectedSection;
		});
	};
	auto sameSignallingAreas = [](const std::vector<SceneSignallingArea>& left,
								   const std::vector<SceneSignallingArea>& right) {
		if (left.size() != right.size())
			return false;
		return std::equal(left.begin(), left.end(), right.begin(), [](const SceneSignallingArea& a, const SceneSignallingArea& b) {
			return a.id == b.id && a.startKm == b.startKm && a.endKm == b.endKm
				&& a.level == b.level && a.trackId == b.trackId;
		});
	};
	auto sameInfrastructure = [](const SceneModel& left, const std::vector<SceneTrack>& tracks,
								  const std::vector<SceneNode>& nodes, const std::vector<SceneArc>& arcs,
								  const std::vector<SceneBlock>& blocks, const std::vector<SceneConnection>& connections) {
		if (left.tracks.size() != tracks.size() || left.nodes.size() != nodes.size() || left.arcs.size() != arcs.size() || left.blocks.size() != blocks.size() || left.connections.size() != connections.size())
			return false;
		for (std::size_t i = 0; i < tracks.size(); ++i)
			if (left.tracks[i].id != tracks[i].id)
				return false;
		for (std::size_t i = 0; i < nodes.size(); ++i) {
			if (left.nodes[i].id != nodes[i].id || left.nodes[i].trackId != nodes[i].trackId || left.nodes[i].xKm != nodes[i].xKm || left.nodes[i].yKm != nodes[i].yKm)
				return false;
		}
		for (std::size_t i = 0; i < arcs.size(); ++i) {
			if (left.arcs[i].id != arcs[i].id || left.arcs[i].trackId != arcs[i].trackId || left.arcs[i].fromNodeId != arcs[i].fromNodeId || left.arcs[i].toNodeId != arcs[i].toNodeId || left.arcs[i].curvatureRadiusM != arcs[i].curvatureRadiusM || left.arcs[i].gradientPercent != arcs[i].gradientPercent || left.arcs[i].speedLimitMs != arcs[i].speedLimitMs)
				return false;
		}
		for (std::size_t i = 0; i < blocks.size(); ++i)
			if (left.blocks[i].id != blocks[i].id || left.blocks[i].trackId != blocks[i].trackId || left.blocks[i].lengthKm != blocks[i].lengthKm)
				return false;
		for (std::size_t i = 0; i < connections.size(); ++i)
			if (left.connections[i].id != connections[i].id || left.connections[i].fromNodeId != connections[i].fromNodeId || left.connections[i].toNodeId != connections[i].toNodeId || left.connections[i].hasSpeedLimit != connections[i].hasSpeedLimit || left.connections[i].speedLimitMs != connections[i].speedLimitMs)
				return false;
		return true;
	};
	auto sameBlockReferences = [](const SceneModel& left, const std::vector<SceneRoute>& routes,
								   const std::vector<SceneBlockDependency>& dependencies,
								   const std::vector<SceneSingleTrackRestriction>& restrictions,
								   const std::vector<SceneStationBoundary>& boundaries) {
		if (left.routes.size() != routes.size() || left.blockDependencies.size() != dependencies.size() || left.singleTrackRestrictions.size() != restrictions.size() || left.stationBoundaries.size() != boundaries.size())
			return false;
		for (std::size_t index = 0; index < routes.size(); ++index) {
			if (left.routes[index].id != routes[index].id || left.routes[index].blocks != routes[index].blocks || left.routes[index].hasCorridor != routes[index].hasCorridor || left.routes[index].corridor != routes[index].corridor || left.routes[index].reversed != routes[index].reversed)
				return false;
		}
		for (std::size_t index = 0; index < dependencies.size(); ++index) {
			if (left.blockDependencies[index].block != dependencies[index].block || left.blockDependencies[index].dependsOn != dependencies[index].dependsOn)
				return false;
		}
		for (std::size_t index = 0; index < restrictions.size(); ++index) {
			if (left.singleTrackRestrictions[index].startBlock != restrictions[index].startBlock || left.singleTrackRestrictions[index].endBlock != restrictions[index].endBlock || left.singleTrackRestrictions[index].protectedStartBlock != restrictions[index].protectedStartBlock || left.singleTrackRestrictions[index].protectedEndBlock != restrictions[index].protectedEndBlock)
				return false;
		}
		for (std::size_t index = 0; index < boundaries.size(); ++index) {
			if (left.stationBoundaries[index].entranceBlock != boundaries[index].entranceBlock || left.stationBoundaries[index].hasExitBlock != boundaries[index].hasExitBlock || left.stationBoundaries[index].exitBlock != boundaries[index].exitBlock || left.stationBoundaries[index].direction != boundaries[index].direction)
				return false;
		}
		return true;
	};

	// step a: load scene from env
	QString scenePath = qEnvironmentVariable("QEGTRAIN_E2E_SCENE");
	if (scenePath.isEmpty()) {
		ok = false;
		failures << "scene: no scene path";
	} else {
		bool opened = w.openSceneDirectory(scenePath);
		if (!opened || !w.m_sceneLoaded) {
			ok = false;
			failures << "scene: scene did not open";
		} else {
			// simulate a clicked-item highlight; the scene owns the effect, so a
			// reopen must also reset the cached pointer or the next click reuses
			// a deleted effect
			if (w.scene && !w.m_presenter->effect) {
				QGraphicsItem* highlightCarrier = w.scene->addRect(QRectF(0, 0, 1, 1));
				w.m_presenter->effect = new HighlightEffect(Qt::blue, 1);
				highlightCarrier->setGraphicsEffect(w.m_presenter->effect);
			}
			bool reopened = w.openSceneDirectory(scenePath);
			if (!reopened || !w.m_sceneLoaded || QDir(w.m_sceneDir).absolutePath() != QDir(scenePath).absolutePath()) {
				ok = false;
				failures << "scene: scene did not reopen";
			}
			if (w.m_presenter->effect != nullptr) {
				ok = false;
				failures << "scene: highlight effect not reset on reopen";
			}
			if (!w.m_presenter->allTrains.isEmpty() || !w.m_presenter->allArcs.isEmpty()) {
				ok = false;
				failures << "scene: legacy scene state not cleared";
			}
			QString alternateScenePath = qEnvironmentVariable("QEGTRAIN_E2E_SCENE_ALT");
			if (!alternateScenePath.isEmpty() && QDir(alternateScenePath).absolutePath() != QDir(scenePath).absolutePath()) {
				bool switched = w.openSceneDirectory(alternateScenePath);
				if (!switched || !w.m_sceneLoaded || QDir(w.m_sceneDir).absolutePath() != QDir(alternateScenePath).absolutePath()) {
					ok = false;
					failures << "scene: alternate scene did not open";
				}
				if (!w.m_presenter->allTrains.isEmpty() || !w.m_presenter->allArcs.isEmpty()) {
					ok = false;
					failures << "scene: legacy scene state not cleared on alternate";
				}
				if (w.m_infrastructureTable->currentRow() != -1 || w.m_presenter->m_previewHasSelectedTrack || w.m_sceneDirty) {
					ok = false;
					failures << "scene: newly opened case has an implicit infrastructure selection";
				}
				w.refreshInfrastructurePanel();
				if (w.m_infrastructureTable->currentRow() != -1 || w.m_presenter->m_previewHasSelectedTrack || w.m_sceneDirty) {
					ok = false;
					failures << "scene: a refresh selected a row that nobody selected";
				}
				w.m_infrastructureFacetCombo->setCurrentIndex(w.m_infrastructureFacetCombo->findData("tracks"));
				w.m_infrastructureTable->setCurrentCell(0, 0);
				const QString explicitlySelectedTrack = w.m_infrastructureSelectionId;
				w.refreshInfrastructurePanel();
				if (explicitlySelectedTrack.isEmpty() || !w.m_presenter->m_previewHasSelectedTrack || w.m_sceneDirty
					|| w.m_infrastructureSelectionId != explicitlySelectedTrack) {
					ok = false;
					failures << "scene: explicit track selection was lost or dirtied the scene";
				}
				bool restored = w.openSceneDirectory(scenePath);
				if (!restored || !w.m_sceneLoaded) {
					ok = false;
					failures << "scene: scene did not reopen after alternate";
				}
				if (w.m_presenter->m_previewHasSelectedTrack || w.m_sceneDirty) {
					ok = false;
					failures << "scene: explicit selection leaked into another case";
				}
			}
			if (QDir(w.m_sceneDir).absolutePath() != QDir(scenePath).absolutePath()) {
				ok = false;
				failures << "scene: primary scene not restored";
			}
		}
	}

	// Optional diagnostics must not replace a focused editor value or hide a
	// blocking error. Exercise both presentation modes without committing edits.
	{
		QAction* detailsAction = w.findChild<QAction*>("actionAdvancedDetails");
		if (!detailsAction || detailsAction->isChecked()) {
			ok = false;
			failures << "presentation: fresh settings profile did not start in normal mode";
		} else if (!w.m_loadedDataDock || w.m_loadedDataDock->isVisible()) {
			ok = false;
			failures << "presentation: loaded data opened automatically in normal mode";
		} else if (!w.m_scenarioListWidget || w.m_scenarioListWidget->count() == 0) {
			ok = false;
			failures << "presentation: scenario list is unavailable for diagnostic checks";
		} else {
			const std::vector<SceneDiagnostic> originalDiagnostics = w.m_sceneDiagnostics;
			const std::string originalName = w.m_sceneModel.name;
			const bool originalDirty = w.m_sceneDirty;
			const quint64 originalRevision = w.m_sceneRevision;
			const QString originalEditorText = w.m_caseNameEdit ? w.m_caseNameEdit->text() : QString();
			const auto syntheticDiagnostic = [](SceneSeverity severity, const char* message) {
				SceneDiagnostic diagnostic;
				diagnostic.severity = severity;
				diagnostic.code = "e2e.presentation";
				diagnostic.message = message;
				diagnostic.path = "scenarios[0]";
				return diagnostic;
			};
			const auto applyPresentation = [&w]() {
				w.updateDiagnosticPresentation();
				w.m_scenarioPanel->updatePresentation();
				w.refreshLoadedDataTree();
				QApplication::processEvents();
			};
			const auto firstScenarioText = [&w]() {
				return w.m_scenarioListWidget && w.m_scenarioListWidget->item(0)
					? w.m_scenarioListWidget->item(0)->text()
					: QString();
			};

			if (w.m_caseNameEdit) {
				w.m_caseNameEdit->setFocus();
				w.m_caseNameEdit->setText(originalEditorText + " pending");
				const bool dirtyBeforeToggle = w.m_sceneDirty;
				const quint64 revisionBeforeToggle = w.m_sceneRevision;
				detailsAction->setChecked(true);
				QApplication::processEvents();
				if (!w.m_loadedDataDock->isVisible() || !QSettings().value(kAdvancedDetailsKey).toBool())
					failures << "presentation: advanced details did not open or persist";
				if (w.m_caseNameEdit->text() != originalEditorText + " pending"
					|| w.m_sceneModel.name != originalName || w.m_sceneDirty != dirtyBeforeToggle
					|| w.m_sceneRevision != revisionBeforeToggle)
					failures << "presentation: advanced toggle replaced or committed focused case text";
				w.m_caseNameEdit->setText(originalEditorText);
			} else {
				failures << "presentation: case name editor is unavailable for focus preservation";
			}

			detailsAction->setChecked(false);
			if (w.m_loadedDataDock->isVisible() || w.m_validationDock->isVisible())
				failures << "presentation: normal mode did not close diagnostic docks";
			w.m_sceneDiagnostics.clear();
			applyPresentation();
			if (!w.m_validationStatusLabel->text().isEmpty()
				|| w.m_caseReadinessLabel->text() != QStringLiteral("Ready to run")
				|| firstScenarioText().contains("Warning")
				|| firstScenarioText().contains("Invalid"))
				failures << "presentation: clean normal mode exposed diagnostic warnings";

			w.m_sceneDiagnostics = {syntheticDiagnostic(SceneSeverity::Warning,
				"E2E non-blocking warning")};
			applyPresentation();
			if (!w.m_validationStatusLabel->text().isEmpty()
				|| w.m_caseReadinessLabel->text() != QStringLiteral("Ready to run")
				|| firstScenarioText().contains("Warning"))
				failures << "presentation: normal mode exposed a non-blocking warning";

			w.m_sceneDiagnostics = {syntheticDiagnostic(SceneSeverity::Error,
				"E2E invalid scenario")};
			applyPresentation();
			if (!w.m_validationStatusLabel->text().contains("Not ready")
				|| w.m_caseReadinessLabel->text() != QStringLiteral("E2E invalid scenario")
				|| !firstScenarioText().contains("Invalid"))
				failures << "presentation: normal mode hid an actionable invalid scenario";

			detailsAction->setChecked(true);
			w.m_sceneDiagnostics = {syntheticDiagnostic(SceneSeverity::Warning,
				"E2E non-blocking warning")};
			applyPresentation();
			if (!w.m_validationStatusLabel->text().contains("Validation:")
				|| !w.m_validationStatusLabel->text().contains("warning")
				|| !firstScenarioText().contains("Warning"))
				failures << "presentation: advanced mode hid full warning details";

			w.m_sceneDiagnostics = originalDiagnostics;
			applyPresentation();
			if (w.m_sceneModel.name != originalName || w.m_sceneDirty != originalDirty
				|| w.m_sceneRevision != originalRevision)
				failures << "presentation: diagnostic checks changed scene state";
		}
	}

	// step b: create, edit, save, and reopen a blank canonical case before the
	// existing loaded-scene editor facets run.
	{
		bool facetOk = true;
		const QString originalScenePath = scenePath;
		QTemporaryDir newCaseTempDir;
		QString outBase = qEnvironmentVariable("QEGTRAIN_E2E_OUT");
		if (outBase.isEmpty())
			outBase = newCaseTempDir.path();
		if (!w.m_sceneLoaded || originalScenePath.isEmpty()) {
			facetFailure(facetOk, "new case", "the original scene was not available to restore");
		} else {
			discardConfirmation();
			w.newScene();
			QApplication::processEvents();
			if (!w.m_sceneLoaded || !w.m_sceneDirty || !w.m_sceneDir.isEmpty() || w.m_sceneIsBundle || !w.m_saveSceneAction || !w.m_saveSceneAction->isEnabled() || !w.m_saveSceneAsAction || !w.m_saveSceneAsAction->isEnabled() || !w.m_saveSceneAsFolderAction || !w.m_saveSceneAsFolderAction->isEnabled())
				facetFailure(facetOk, "new case", "new case did not start unsaved and dirty with save actions enabled");
			if (!w.m_runSceneAction || w.m_runSceneAction->isEnabled() || !w.ui->actionSimulationStart || w.ui->actionSimulationStart->isEnabled())
				facetFailure(facetOk, "new case", "Run actions were enabled for an empty case");

			auto* nameEdit = w.findChild<QLineEdit*>("caseNameEdit");
			auto* descriptionEdit = w.findChild<QLineEdit*>("caseDescriptionEdit");
			auto* baseTimeEdit = w.findChild<QLineEdit*>("caseBaseTimeEdit");
			auto* durationEdit = w.findChild<QDoubleSpinBox*>("caseDurationSecondsEdit");
			auto* bufferEdit = w.findChild<QDoubleSpinBox*>("caseBufferSecondsEdit");
			auto* recoveryEdit = w.findChild<QDoubleSpinBox*>("caseRecoveryPercentEdit");
			if (!nameEdit || !descriptionEdit || !baseTimeEdit || !durationEdit || !bufferEdit || !recoveryEdit || !w.m_caseSettingsDock || !w.m_caseSettingsDock->isVisible()) {
				facetFailure(facetOk, "new case", "case settings controls or dock are unavailable");
			} else {
				nameEdit->setText("E2E New Case");
				descriptionEdit->setText("new case editor smoke");
				baseTimeEdit->setText("09:15:30");
				durationEdit->setValue(7200.0);
				bufferEdit->setValue(30.0);
				recoveryEdit->setValue(7.5);
				w.commitPendingCaseSettings();
				if (w.m_sceneModel.name != "E2E New Case" || w.m_sceneModel.description != "new case editor smoke" || w.m_sceneModel.baseTime != "09:15:30" || !w.m_sceneModel.settings.hasDuration || w.m_sceneModel.settings.durationSeconds != 7200.0 || !w.m_sceneModel.settings.hasBufferTime || w.m_sceneModel.settings.bufferTimeSeconds != 30.0 || !w.m_sceneModel.settings.hasRecoveryTime || w.m_sceneModel.settings.recoveryTimePercent != 7.5 || w.m_startOffsetSeconds != 9 * 3600 + 15 * 60 + 30)
					facetFailure(facetOk, "new case", "case settings edits did not commit all six values");
			}

			auto* infrastructureDock = w.findChild<QDockWidget*>("infrastructureDock");
			auto* infrastructureFacet = w.findChild<QComboBox*>("infrastructureFacetCombo");
			auto* infrastructureTable = w.findChild<QTableWidget*>("infrastructureTable");
			auto* infrastructureAdd = w.findChild<QPushButton*>("infrastructureAddButton");
			auto* infrastructureDelete = w.findChild<QPushButton*>("infrastructureDeleteButton");
			auto* routeSectionDetail = w.findChild<QWidget*>("routeSectionDetailWidget");
			auto* blockTrackFilter = w.findChild<QComboBox*>("blockTrackFilterCombo");
			auto* blockInsert = w.findChild<QPushButton*>("blockInsertButton");
			auto* blockMoveUp = w.findChild<QPushButton*>("blockMoveUpButton");
			auto* blockMoveDown = w.findChild<QPushButton*>("blockMoveDownButton");
			if (!infrastructureDock || !infrastructureFacet || !infrastructureTable
				|| !infrastructureAdd || !infrastructureDelete || !routeSectionDetail
				|| !blockTrackFilter || !blockInsert || !blockMoveUp || !blockMoveDown) {
				facetFailure(facetOk, "infrastructure", "infrastructure controls or dock are unavailable");
			} else {
				infrastructureDock->show();
				infrastructureDock->raise();
				if (routeSectionDetail->isVisible())
					facetFailure(facetOk, "infrastructure", "route section editor was visible outside Routes");
				auto chooseInfrastructureFacet = [&](const char* facet) {
					const int index = infrastructureFacet->findData(QString::fromLatin1(facet));
					if (index < 0)
						return false;
					infrastructureFacet->setCurrentIndex(index);
					QApplication::processEvents();
					return true;
				};
				auto chooseBlockTrack = [&](const char* trackId) {
					if (!chooseInfrastructureFacet("blocks"))
						return false;
					const int index = blockTrackFilter->findData(QString::fromLatin1(trackId));
					if (index < 0)
						return false;
					blockTrackFilter->setCurrentIndex(index);
					QApplication::processEvents();
					return true;
				};
				auto setInfrastructureCell = [&](const char* facet, int row, int column, const QString& value) {
					if (!chooseInfrastructureFacet(facet) || row < 0 || row >= infrastructureTable->rowCount() || !infrastructureTable->item(row, column))
						return false;
					infrastructureTable->item(row, column)->setText(value);
					QApplication::processEvents();
					return true;
				};
				auto addInfrastructureRow = [&](const char* facet) {
					if (!chooseInfrastructureFacet(facet))
						return false;
					infrastructureAdd->click();
					QApplication::processEvents();
					return true;
				};
				if (!addInfrastructureRow("tracks") || !setInfrastructureCell("tracks", 0, 0, "e2e-main") || !addInfrastructureRow("tracks") || !setInfrastructureCell("tracks", 1, 0, "e2e-yard"))
					facetFailure(facetOk, "infrastructure", "track creation or ID editing did not apply");
				if (!setInfrastructureCell("tracks", 0, 0, "e2e-yard") || w.m_sceneModel.tracks.size() != 2 || w.m_sceneModel.tracks[0].id != "e2e-main")
					facetFailure(facetOk, "infrastructure", "duplicate track ID was not rejected safely");
				for (int row = 0; row < 3; ++row) {
					if (!addInfrastructureRow("nodes") || !setInfrastructureCell("nodes", row, 0, QString("e2e-main-node-%1").arg(row)) || !setInfrastructureCell("nodes", row, 1, "e2e-main") || !setInfrastructureCell("nodes", row, 2, QString::number(row)) || !setInfrastructureCell("nodes", row, 3, QString::number(row == 1 ? 0.25 : 0.0)))
						facetFailure(facetOk, "infrastructure", "main-track node field edit did not apply");
				}
				for (int row = 3; row < 6; ++row) {
					const double x = row == 4 ? 1.5 : static_cast<double>(row - 3);
					if (!addInfrastructureRow("nodes") || !setInfrastructureCell("nodes", row, 0, QString("e2e-yard-node-%1").arg(row - 3)) || !setInfrastructureCell("nodes", row, 1, "e2e-yard") || !setInfrastructureCell("nodes", row, 2, QString::number(x)) || !setInfrastructureCell("nodes", row, 3, "1.0"))
						facetFailure(facetOk, "infrastructure", "yard-track node field edit did not apply");
				}
				const QStringList mainNodeIds = {"e2e-main-node-0", "e2e-main-node-1", "e2e-main-node-2"};
				const QStringList yardNodeIds = {"e2e-yard-node-0", "e2e-yard-node-1", "e2e-yard-node-2"};
				for (int row = 0; row < 2; ++row) {
					if (!addInfrastructureRow("arcs") || !setInfrastructureCell("arcs", row, 0, QString("e2e-main-arc-%1").arg(row)) || !setInfrastructureCell("arcs", row, 1, "e2e-main") || !setInfrastructureCell("arcs", row, 2, mainNodeIds[row]) || !setInfrastructureCell("arcs", row, 3, mainNodeIds[row + 1]) || !setInfrastructureCell("arcs", row, 4, row == 0 ? "0" : "1250.5") || !setInfrastructureCell("arcs", row, 5, row == 0 ? "-1.5" : "2.25") || !setInfrastructureCell("arcs", row, 6, row == 0 ? "22.5" : "18.75"))
						facetFailure(facetOk, "infrastructure", "main-track arc field edit did not apply");
				}
				for (int row = 0; row < 2; ++row)
					if (!addInfrastructureRow("arcs")
						|| !setInfrastructureCell("arcs", row + 2, 0, QString("e2e-yard-arc-%1").arg(row))
						|| !setInfrastructureCell("arcs", row + 2, 1, "e2e-yard")
						|| !setInfrastructureCell("arcs", row + 2, 2, yardNodeIds[row])
						|| !setInfrastructureCell("arcs", row + 2, 3, yardNodeIds[row + 1])
						|| !setInfrastructureCell("arcs", row + 2, 4, "0")
						|| !setInfrastructureCell("arcs", row + 2, 5, "0")
						|| !setInfrastructureCell("arcs", row + 2, 6, "16.5"))
						facetFailure(facetOk, "infrastructure", "yard-track arc field edit did not apply");
				if (!chooseBlockTrack("e2e-main"))
					facetFailure(facetOk, "infrastructure", "main block track filter was unavailable");
				for (int row = 0; row < 2; ++row) {
					if (!addInfrastructureRow("blocks") || !setInfrastructureCell("blocks", row, 0, QString("e2e-main-block-%1").arg(row)) || !setInfrastructureCell("blocks", row, 1, "e2e-main") || !setInfrastructureCell("blocks", row, 2, "0.75"))
						facetFailure(facetOk, "infrastructure", "main-track block field edit did not apply");
				}
				if (!chooseBlockTrack("e2e-yard")) {
					facetFailure(facetOk, "infrastructure", "yard block track filter was unavailable");
				} else {
					const std::array<double, 3> yardBlockLengths = {0.5, 1.25, 0.25};
					for (int row = 0; row < 3; ++row)
						if (!addInfrastructureRow("blocks")
							|| !setInfrastructureCell("blocks", row, 0,
								QString("e2e-yard-block-%1").arg(row))
							|| !setInfrastructureCell("blocks", row, 1, "e2e-yard")
							|| !setInfrastructureCell("blocks", row, 2,
								QString::number(yardBlockLengths[static_cast<std::size_t>(row)])))
							facetFailure(facetOk, "infrastructure",
								"yard-track block field edit did not apply");
				}
				if (!chooseBlockTrack("e2e-main")) {
					facetFailure(facetOk, "infrastructure", "main block filter could not be restored");
				} else {
					infrastructureTable->setCurrentCell(1, 0);
					blockInsert->click();
					QApplication::processEvents();
					const int insertedRow = infrastructureTable->currentRow();
					if (insertedRow < 0 || infrastructureTable->rowCount() != 3
						|| !infrastructureTable->item(insertedRow, 0)) {
						facetFailure(facetOk, "infrastructure", "block insert did not create a selected row");
					} else {
						infrastructureTable->item(insertedRow, 0)->setText("e2e-main-block-inserted");
						infrastructureTable->item(insertedRow, 2)->setText("0.5");
						QApplication::processEvents();
						blockMoveUp->click();
						QApplication::processEvents();
						blockMoveDown->click();
						QApplication::processEvents();
						const QStringList expectedOrder = {"e2e-main-block-0",
							"e2e-main-block-inserted", "e2e-main-block-1"};
						const std::array<double, 3> expectedStarts = {0.0, 0.75, 1.25};
						const std::array<double, 3> expectedEnds = {0.75, 1.25, 2.0};
						bool placementColumnsVisible = infrastructureTable->columnCount() == 7
							&& infrastructureTable->rowCount() == 3;
						for (int visibleRow = 0; visibleRow < infrastructureTable->rowCount(); ++visibleRow)
							placementColumnsVisible = placementColumnsVisible
								&& infrastructureTable->item(visibleRow, 0)
								&& infrastructureTable->item(visibleRow, 0)->text() == expectedOrder[visibleRow]
								&& infrastructureTable->item(visibleRow, 3)
								&& infrastructureTable->item(visibleRow, 3)->text() == QString::number(visibleRow + 1)
								&& infrastructureTable->item(visibleRow, 4)
								&& qFuzzyCompare(infrastructureTable->item(visibleRow, 4)->text().toDouble() + 1.0,
									expectedStarts[static_cast<std::size_t>(visibleRow)] + 1.0)
								&& infrastructureTable->item(visibleRow, 5)
								&& qFuzzyCompare(infrastructureTable->item(visibleRow, 5)->text().toDouble() + 1.0,
									expectedEnds[static_cast<std::size_t>(visibleRow)] + 1.0)
								&& infrastructureTable->item(visibleRow, 6)
								&& infrastructureTable->item(visibleRow, 6)->text() == "complete";
						if (!placementColumnsVisible)
							facetFailure(facetOk, "infrastructure", "block placement columns were not visible after reorder");
					}
				}
				const std::size_t blocksBeforeInvalidAdd = w.m_sceneModel.blocks.size();
				const bool orphanTrackAddDisabled = setInfrastructureCell("blocks", 1, 1, QString())
					&& !infrastructureAdd->isEnabled();
				infrastructureAdd->click();
				w.addInfrastructureEntity(); // also exercise the handler's defensive check
				QApplication::processEvents();
				if (!orphanTrackAddDisabled || w.m_sceneModel.blocks.size() != blocksBeforeInvalidAdd)
					facetFailure(facetOk, "infrastructure", "Add accepted an invalid selected block track");
				if (!setInfrastructureCell("blocks", 0, 1, "e2e-main") || !infrastructureAdd->isEnabled())
					facetFailure(facetOk, "infrastructure", "orphan block could not be restored to a valid track");
				if (!addInfrastructureRow("stations") || !setInfrastructureCell("stations", 0, 0, "e2e-station-a") || !setInfrastructureCell("stations", 0, 1, "E2E A") || !setInfrastructureCell("stations", 0, 2, "true") || !setInfrastructureCell("stations", 0, 3, "0.5") || !addInfrastructureRow("stations") || !setInfrastructureCell("stations", 1, 0, "e2e-station-b") || !setInfrastructureCell("stations", 1, 1, "E2E B") || !setInfrastructureCell("stations", 1, 2, "true") || !setInfrastructureCell("stations", 1, 3, "1.5"))
					facetFailure(facetOk, "stations/signalling", "station table authoring did not apply");
				if (!addInfrastructureRow("platforms") || !setInfrastructureCell("platforms", 0, 0, "e2e-station-a") || !setInfrastructureCell("platforms", 0, 1, "e2e-platform-a") || !setInfrastructureCell("platforms", 0, 2, mainNodeIds[0]) || !addInfrastructureRow("platforms") || !setInfrastructureCell("platforms", 1, 0, "e2e-station-b") || !setInfrastructureCell("platforms", 1, 1, "e2e-platform-b") || !setInfrastructureCell("platforms", 1, 2, yardNodeIds[2]))
					facetFailure(facetOk, "stations/signalling", "platform table authoring or station move did not apply");
				if (!addInfrastructureRow("signals") || !setInfrastructureCell("signals", 0, 0, "e2e-signal"))
					facetFailure(facetOk, "stations/signalling", "signal table authoring did not apply");
				if (!chooseInfrastructureFacet("signals")) {
					facetFailure(facetOk, "stations/signalling", "signal section choices were unavailable");
				} else {
					auto* protectedSection = qobject_cast<QComboBox*>(infrastructureTable->cellWidget(0, 1));
					const int blockChoice = protectedSection ? protectedSection->findText(
																   QStringLiteral("base block e2e-main-block-inserted / track e2e-main"))
															 : -1;
					if (blockChoice < 0) {
						facetFailure(facetOk, "stations/signalling", "creator-facing signal section choice was missing");
					} else {
						protectedSection->setCurrentIndex(blockChoice);
						QApplication::processEvents();
						if (sceneSignals(w.m_sceneModel).front().protectedSection
							!= protectedSection->itemData(blockChoice).toString().toStdString())
							facetFailure(facetOk, "stations/signalling", "signal section choice did not commit");
					}
				}
				// Every edit of the table rebuilds it and deletes the lists, so a list is fetched again after each edit.
				const auto areaSystemCombo = [&]() { return qobject_cast<QComboBox*>(infrastructureTable->cellWidget(0, 3)); };
				const auto areaTrackCombo = [&]() { return qobject_cast<QComboBox*>(infrastructureTable->cellWidget(0, 4)); };
				const auto areaLevelError = [&w]() {
					for (const SceneDiagnostic& diagnostic : w.m_sceneDiagnostics)
						if (diagnostic.severity == SceneSeverity::Error && diagnostic.code == "scene.signalling_area.level")
							return QString::fromStdString(diagnostic.message);
					return QString();
				};
				const auto chooseListItem = [](QComboBox* combo, const QVariant& data) {
					const int index = combo ? combo->findData(data) : -1;
					if (index < 0)
						return false;
					combo->setCurrentIndex(index);
					QApplication::processEvents();
					return true;
				};
				const int systemCount = levelValue(SignallingLevel::Bacc) - levelValue(SignallingLevel::Atb) + 1;
				// The six systems in level order after the given number of extra items.
				const auto systemListHolds = [systemCount](const QComboBox* combo, int extraItems) {
					if (!combo || combo->count() != systemCount + extraItems)
						return false;
					for (int level = levelValue(SignallingLevel::Atb); level <= levelValue(SignallingLevel::Bacc); ++level) {
						const int item = level - levelValue(SignallingLevel::Atb) + extraItems;
						if (combo->itemText(item) != QString::fromStdString(signallingLevelName(level)) || combo->itemData(item).toInt() != level)
							return false;
					}
					return true;
				};
				// "(all tracks)" and every track of the scene in scene order after the given number of extra items.
				const auto trackListHolds = [&w](const QComboBox* combo, int extraItems) {
					if (!combo || combo->count() != extraItems + 1 + static_cast<int>(w.m_sceneModel.tracks.size()))
						return false;
					if (combo->itemText(extraItems) != QStringLiteral("(all tracks)") || !combo->itemData(extraItems).toString().isEmpty())
						return false;
					for (std::size_t index = 0; index < w.m_sceneModel.tracks.size(); ++index) {
						const int item = extraItems + 1 + static_cast<int>(index);
						const QString id = QString::fromStdString(w.m_sceneModel.tracks[index].id);
						if (combo->itemData(item).toString() != id || !combo->itemText(item).startsWith(id + QStringLiteral(" (blocks "))
							|| !combo->itemText(item).endsWith(QStringLiteral(" km)")))
							return false;
					}
					return true;
				};
				if (!addInfrastructureRow("signalling_areas"))
					facetFailure(facetOk, "stations/signalling", "signalling area row could not be added");
				const SceneBlockExtent networkExtent = sceneBlockExtent(buildSceneSectionInventory(w.m_sceneModel), "");
				const bool signallingAreaStartsUnchosen = w.m_sceneModel.signallingAreas.size() == 1
					&& networkExtent.found && networkExtent.startKm == 0.0 && networkExtent.endKm > 0.0
					&& w.m_sceneModel.signallingAreas.front().startKm == networkExtent.startKm
					&& w.m_sceneModel.signallingAreas.front().endKm == networkExtent.endKm
					&& w.m_sceneModel.signallingAreas.front().level == kSignallingLevelUnset
					&& w.m_sceneModel.signallingAreas.front().trackId.empty();
				if (!signallingAreaStartsUnchosen)
					facetFailure(facetOk, "stations/signalling", "Add did not create a signalling area with the network extent and no chosen system");
				QString firstSceneError;
				for (const SceneDiagnostic& diagnostic : w.m_sceneDiagnostics)
					if (diagnostic.severity == SceneSeverity::Error) {
						firstSceneError = QString::fromStdString(diagnostic.message);
						break;
					}
				if (!areaLevelError().contains("has no signalling system") || !w.m_runSceneAction || w.m_runSceneAction->isEnabled()
					|| !w.m_caseReadinessLabel || firstSceneError.isEmpty() || w.m_caseReadinessLabel->text() != firstSceneError)
					facetFailure(facetOk, "stations/signalling", "an area without a chosen system did not block the case with a diagnostic");
				const QComboBox* newAreaSystem = areaSystemCombo();
				const QComboBox* newAreaTrack = areaTrackCombo();
				if (!systemListHolds(newAreaSystem, 1) || newAreaSystem->itemText(0) != QStringLiteral("Choose a signalling system")
					|| newAreaSystem->currentIndex() != 0 || newAreaSystem->currentText() != QStringLiteral("Choose a signalling system"))
					facetFailure(facetOk, "stations/signalling", "a new area did not offer the placeholder and the six systems");
				if (!trackListHolds(newAreaTrack, 0) || newAreaTrack->currentText() != QStringLiteral("(all tracks)")
					|| newAreaTrack->itemText(1) != QStringLiteral("e2e-main (blocks 0.000000 to 2.000000 km)"))
					facetFailure(facetOk, "stations/signalling", "a new area did not offer all tracks and every track of the scene");
				if (!setInfrastructureCell("signalling_areas", 0, 0, "e2e-signalling-area")
					|| !setInfrastructureCell("signalling_areas", 0, 1, "0.25")
					|| !setInfrastructureCell("signalling_areas", 0, 2, "1.75")
					|| !chooseListItem(areaSystemCombo(), levelValue(SignallingLevel::VirtualCoupling)))
					facetFailure(facetOk, "stations/signalling", "network-wide signalling area authoring did not apply");
				const QComboBox* chosenSystem = areaSystemCombo();
				const bool networkAreaAuthored = w.m_sceneModel.signallingAreas.size() == 1
					&& w.m_sceneModel.signallingAreas.front().id == "e2e-signalling-area"
					&& w.m_sceneModel.signallingAreas.front().startKm == 0.25
					&& w.m_sceneModel.signallingAreas.front().endKm == 1.75
					&& w.m_sceneModel.signallingAreas.front().level == levelValue(SignallingLevel::VirtualCoupling)
					&& w.m_sceneModel.signallingAreas.front().trackId.empty()
					&& systemListHolds(chosenSystem, 0) && chosenSystem->currentText() == QStringLiteral("4 Virtual coupling")
					&& areaLevelError().isEmpty();
				if (!networkAreaAuthored)
					facetFailure(facetOk, "stations/signalling", "network-wide signalling area was not canonical");
				if (!chooseListItem(areaTrackCombo(), QStringLiteral("e2e-main"))
					|| w.m_sceneModel.signallingAreas.front().trackId != "e2e-main"
					|| !trackListHolds(areaTrackCombo(), 0) || areaTrackCombo()->currentData().toString() != QStringLiteral("e2e-main"))
					facetFailure(facetOk, "stations/signalling", "signalling area track-scope edit did not apply");
				const bool areaTrackRenameUpdated = setInfrastructureCell("tracks", 0, 0, "e2e-main-renamed")
					&& w.m_sceneModel.signallingAreas.front().trackId == "e2e-main-renamed"
					&& setInfrastructureCell("tracks", 0, 0, "e2e-main")
					&& w.m_sceneModel.signallingAreas.front().trackId == "e2e-main";
				if (!areaTrackRenameUpdated)
					facetFailure(facetOk, "stations/signalling", "track rename did not preserve signalling-area scope");
				if (!chooseInfrastructureFacet("signalling_areas")) {
					facetFailure(facetOk, "stations/signalling", "signalling area lists were unavailable after the track rename");
				} else {
					// A mouse wheel over a list scrolls the table and must not change the value. Without the guard a combo box
					// changes its value (and accepts the event) on a style that scrolls combo boxes, and accepts the event on one
					// that does not.
					QComboBox* wheelLists[] = {areaSystemCombo(), areaTrackCombo()};
					for (QComboBox* list : wheelLists) {
						if (!list) {
							facetFailure(facetOk, "stations/signalling", "a signalling area list was missing for the wheel check");
							continue;
						}
						const int indexBefore = list->currentIndex();
						QWheelEvent wheel(QPointF(list->rect().center()), QPointF(list->mapToGlobal(list->rect().center())),
							QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
						QApplication::sendEvent(list, &wheel);
						QApplication::processEvents();
						if (list->currentIndex() != indexBefore)
							facetFailure(facetOk, "stations/signalling", "a mouse wheel changed the value of a signalling area list");
						if (wheel.isAccepted())
							facetFailure(facetOk, "stations/signalling", "a signalling area list kept a mouse wheel event instead of handing it on");
					}
					// A level or track that does not exist is listed first and goes when a real choice replaces it.
					w.m_sceneModel.signallingAreas.front().level = 9;
					w.refreshInfrastructureTable();
					const QComboBox* staleSystem = areaSystemCombo();
					if (!staleSystem || staleSystem->itemText(0) != QStringLiteral("Invalid level 9")
						|| staleSystem->currentText() != QStringLiteral("Invalid level 9") || !systemListHolds(staleSystem, 1))
						facetFailure(facetOk, "stations/signalling", "a stale level was not listed as invalid above the six systems");
					if (!chooseListItem(areaSystemCombo(), levelValue(SignallingLevel::EtcsLevel2))
						|| w.m_sceneModel.signallingAreas.front().level != levelValue(SignallingLevel::EtcsLevel2)
						|| !systemListHolds(areaSystemCombo(), 0)
						|| areaSystemCombo()->currentText() != QString::fromStdString(signallingLevelName(levelValue(SignallingLevel::EtcsLevel2))))
						facetFailure(facetOk, "stations/signalling", "choosing a system did not replace the stale level");
					w.m_sceneModel.signallingAreas.front().trackId = "ghost";
					w.refreshInfrastructureTable();
					const QComboBox* staleTrack = areaTrackCombo();
					if (!staleTrack || staleTrack->itemText(0) != QStringLiteral("Invalid track: ghost")
						|| staleTrack->currentText() != QStringLiteral("Invalid track: ghost") || !trackListHolds(staleTrack, 1))
						facetFailure(facetOk, "stations/signalling", "a stale track was not listed as invalid above the tracks");
					if (!chooseListItem(areaTrackCombo(), QString()) || !w.m_sceneModel.signallingAreas.front().trackId.empty()
						|| !trackListHolds(areaTrackCombo(), 0) || areaTrackCombo()->currentText() != QStringLiteral("(all tracks)"))
						facetFailure(facetOk, "stations/signalling", "choosing all tracks did not replace the stale track");
					// The values that the save and reload comparison expects.
					if (!chooseListItem(areaSystemCombo(), levelValue(SignallingLevel::VirtualCoupling))
						|| !chooseListItem(areaTrackCombo(), QStringLiteral("e2e-main"))
						|| w.m_sceneModel.signallingAreas.front().level != levelValue(SignallingLevel::VirtualCoupling)
						|| w.m_sceneModel.signallingAreas.front().trackId != "e2e-main")
						facetFailure(facetOk, "stations/signalling", "the signalling area could not be restored through its lists");
				}
				if (!addInfrastructureRow("connections") || !setInfrastructureCell("connections", 0, 0, "e2e-switch") || !setInfrastructureCell("connections", 0, 1, mainNodeIds[1]) || !setInfrastructureCell("connections", 0, 2, yardNodeIds[1]) || !setInfrastructureCell("connections", 0, 3, "true") || !setInfrastructureCell("connections", 0, 4, "9.25"))
					facetFailure(facetOk, "stations/signalling", "connection field edit did not apply");
				std::array<std::string, 3> expectedNativeRoute;
				if (!addInfrastructureRow("routes") || !setInfrastructureCell("routes", 0, 0, "e2e-block-route") || !setInfrastructureCell("routes", 0, 2, "true") || !setInfrastructureCell("routes", 0, 3, "e2e-corridor") || !setInfrastructureCell("routes", 0, 4, "false")) {
					facetFailure(facetOk, "stations/signalling", "route table authoring did not apply");
				} else {
					auto* routeCatalog = w.findChild<QComboBox*>("routeSectionCatalogCombo");
					auto* routeAddSection = w.findChild<QPushButton*>("routeAddSectionButton");
					auto* routeList = w.findChild<QListWidget*>("routeSectionList");
					auto* routeRemoveSection = w.findChild<QPushButton*>("routeRemoveSectionButton");
					auto* routeMoveUp = w.findChild<QPushButton*>("routeMoveUpButton");
					auto* routeMoveDown = w.findChild<QPushButton*>("routeMoveDownButton");
					const int mainChoice = routeCatalog ? routeCatalog->findText(
															  QStringLiteral("base block e2e-main-block-0 / track e2e-main"))
														: -1;
					const int yardChoice = routeCatalog ? routeCatalog->findText(
															  QStringLiteral("base block e2e-yard-block-2 / track e2e-yard"))
														: -1;
					int connectionChoice = -1;
					if (routeCatalog)
						for (int index = 0; index < routeCatalog->count(); ++index)
							if (routeCatalog->itemText(index).startsWith("connection e2e-switch /")) {
								connectionChoice = index;
								break;
							}
					if (!routeCatalog || !routeAddSection || !routeList || !routeRemoveSection
						|| !routeMoveUp || !routeMoveDown || mainChoice < 0 || connectionChoice < 0
						|| yardChoice < 0) {
						facetFailure(facetOk, "stations/signalling",
							"route section catalog did not expose the switched path");
					} else {
						const std::array<int, 3> choices = {mainChoice, connectionChoice, yardChoice};
						for (std::size_t index = 0; index < choices.size(); ++index) {
							expectedNativeRoute[index] = routeCatalog->itemData(choices[index]).toString().toStdString();
							routeCatalog->setCurrentIndex(choices[index]);
							routeAddSection->click();
							QApplication::processEvents();
						}
						routeList->setCurrentRow(1);
						routeMoveUp->click();
						QApplication::processEvents();
						routeMoveDown->click();
						QApplication::processEvents();
						routeCatalog->setCurrentIndex(mainChoice);
						routeAddSection->click();
						QApplication::processEvents();
						routeRemoveSection->click();
						QApplication::processEvents();
						if (w.m_sceneModel.routes.empty()
							|| w.m_sceneModel.routes.front().blocks
								!= std::vector<std::string>(expectedNativeRoute.begin(), expectedNativeRoute.end()))
							facetFailure(facetOk, "stations/signalling",
								"ordered route section controls did not retain the switched path");
						if (!routeSectionDetail->isVisible() || !chooseInfrastructureFacet("blocks")
							|| routeSectionDetail->isVisible())
							facetFailure(facetOk, "stations/signalling",
								"route section editor did not follow the selected infrastructure facet");
					}
				}
				const auto selectSectionCell = [&](const char* facet, int row, int column,
												   const QString& preferredPrefix = QString()) {
					if (!chooseInfrastructureFacet(facet))
						return QString();
					auto* combo = qobject_cast<QComboBox*>(infrastructureTable->cellWidget(row, column));
					if (!combo)
						return QString();
					int choice = -1;
					for (int index = 0; index < combo->count(); ++index) {
						if (!combo->itemData(index).toString().isEmpty()
							&& (preferredPrefix.isEmpty() || combo->itemText(index).startsWith(preferredPrefix))) {
							choice = index;
							break;
						}
					}
					if (choice < 0)
						return QString();
					combo->setCurrentIndex(choice);
					QApplication::processEvents();
					return combo->itemData(choice).toString();
				};
				const auto recreateIncompleteRow = [&](const char* facet, const auto& rows) {
					if (!addInfrastructureRow(facet))
						return false;
					acceptConfirmation();
					infrastructureDelete->click();
					QApplication::processEvents();
					return rows.empty() && addInfrastructureRow(facet);
				};
				if (!recreateIncompleteRow("block_dependencies", w.m_sceneModel.blockDependencies)) {
					facetFailure(facetOk, "stations/signalling", "dependency row add/delete cycle failed");
				}
				const QString dependencyBlock = selectSectionCell("block_dependencies", 0, 0,
					"connection e2e-switch /");
				const QString dependencyDependsOn = selectSectionCell("block_dependencies", 0, 1,
					"base block e2e-main-block-inserted / track e2e-main");
				if (dependencyBlock.isEmpty() || dependencyDependsOn.isEmpty())
					facetFailure(facetOk, "stations/signalling", "dependency selectors did not expose catalog choices");
				if (!recreateIncompleteRow("single_track_restrictions",
						w.m_sceneModel.singleTrackRestrictions)) {
					facetFailure(facetOk, "stations/signalling", "restriction row add/delete cycle failed");
				}
				const QString restrictionStart = selectSectionCell("single_track_restrictions", 0, 0,
					"base block e2e-main-block-inserted / track e2e-main");
				const QString restrictionEnd = selectSectionCell("single_track_restrictions", 0, 1,
					"base block e2e-yard-block-2 / track e2e-yard");
				const QString restrictionProtectedStart = selectSectionCell("single_track_restrictions", 0, 2,
					"connection e2e-switch /");
				const QString restrictionProtectedEnd = selectSectionCell("single_track_restrictions", 0, 3,
					"base block e2e-yard-block-2 / track e2e-yard");
				if (restrictionStart.isEmpty() || restrictionEnd.isEmpty() || restrictionProtectedStart.isEmpty() || restrictionProtectedEnd.isEmpty())
					facetFailure(facetOk, "stations/signalling", "restriction selectors did not expose catalog choices");
				if (!recreateIncompleteRow("station_boundaries", w.m_sceneModel.stationBoundaries)) {
					facetFailure(facetOk, "stations/signalling", "boundary row add/delete cycle failed");
				}
				const QString boundaryEntrance = selectSectionCell("station_boundaries", 0, 0,
					"base block e2e-main-block-inserted / track e2e-main");
				const QString boundaryExit = selectSectionCell("station_boundaries", 0, 1,
					"base block e2e-yard-block-2 / track e2e-yard");
				if (!setInfrastructureCell("station_boundaries", 0, 2, "false")
					|| boundaryEntrance.isEmpty() || boundaryExit.isEmpty())
					facetFailure(facetOk, "stations/signalling", "boundary selectors did not expose entrance and exit choices");
				if (chooseInfrastructureFacet("station_boundaries")) {
					auto* exitCombo = qobject_cast<QComboBox*>(infrastructureTable->cellWidget(0, 1));
					if (exitCombo) {
						const int noneChoice = exitCombo->findData(QString());
						if (noneChoice >= 0)
							exitCombo->setCurrentIndex(noneChoice);
						QApplication::processEvents();
						if (w.m_sceneModel.stationBoundaries.empty()
							|| w.m_sceneModel.stationBoundaries.front().hasExitBlock
							|| !w.m_sceneModel.stationBoundaries.front().exitBlock.empty())
							facetFailure(facetOk, "stations/signalling", "boundary (none) did not clear exit atomically");
					}
				}
				const bool m3TablesAuthored = w.m_sceneModel.stations.size() == 2 && w.m_sceneModel.stations[0].platforms.size() == 1 && w.m_sceneModel.stations[1].platforms.size() == 1 && sceneSignals(w.m_sceneModel).size() == 1 && w.m_sceneModel.routes.size() == 1 && w.m_sceneModel.blockDependencies.size() == 1 && w.m_sceneModel.singleTrackRestrictions.size() == 1 && w.m_sceneModel.stationBoundaries.size() == 1;
				if (!m3TablesAuthored)
					facetFailure(facetOk, "stations/signalling", "canonical M3 rows were not created through the table controls");
				if (chooseInfrastructureFacet("platforms")) {
					auto* lengthEdit = qobject_cast<QDoubleSpinBox*>(infrastructureTable->cellWidget(0, 3));
					auto* widthEdit = qobject_cast<QDoubleSpinBox*>(infrastructureTable->cellWidget(0, 4));
					if (!lengthEdit || !widthEdit) {
						facetFailure(facetOk, "platform geometry", "length/width editors were unavailable");
					} else {
						lengthEdit->setValue(123.75);
						widthEdit->setValue(4.25);
						QApplication::processEvents();
						const ScenePlatform& platform = w.m_sceneModel.stations.front().platforms.front();
						if (!platform.hasLength || platform.lengthM != 123.75
							|| !platform.hasWidth || platform.widthM != 4.25)
							facetFailure(facetOk, "platform geometry", "public geometry edits did not set presence/value");
					}
				} else {
					facetFailure(facetOk, "platform geometry", "platform facet was unavailable");
				}
				const std::vector<SceneDiagnostic> nativeDiagnostics =
					buildInfrastructureAndSignallingFromScene(defaultRun(), w.m_sceneModel);
				bool nativeRouteMatches = !hasErrors(nativeDiagnostics) && N_Routes == 1
					&& train_route.size() == 1 && train_route.front().N_Block_Sections == 3;
				if (nativeRouteMatches)
					for (std::size_t index = 0; index < expectedNativeRoute.size(); ++index)
						nativeRouteMatches = nativeRouteMatches
							&& train_route.front().sequence_of_block_sections[index].ID
								== expectedNativeRoute[index];
				if (!nativeRouteMatches)
					facetFailure(facetOk, "stations/signalling",
						"publicly authored switched route did not retain its order in native staging");
				w.refreshValidationPanel();
				const std::string renamedBlockId = "e2e-main-block-renamed";
				const bool blockRenamed = setInfrastructureCell("blocks", 1, 0,
					QString::fromStdString(renamedBlockId));
				const SceneSectionInventory renamedInventory = buildSceneSectionInventory(w.m_sceneModel);
				const auto resolvesThroughRenamedBlock = [&](const std::string& reference) {
					const SceneSectionDescriptor* section = renamedInventory.resolve(reference);
					return section
						&& (section->sourceBlockId == renamedBlockId || section->firstBlockId == renamedBlockId || section->secondBlockId == renamedBlockId);
				};
				const bool blockReferencesUpdated = blockRenamed
					&& std::any_of(w.m_sceneModel.blocks.begin(), w.m_sceneModel.blocks.end(),
						[&](const SceneBlock& block) { return block.id == renamedBlockId; })
					&& w.m_sceneModel.routes.size() == 1 && w.m_sceneModel.routes[0].blocks.size() == 3
					&& renamedInventory.resolve(w.m_sceneModel.routes[0].blocks[0])
					&& resolvesThroughRenamedBlock(w.m_sceneModel.routes[0].blocks[1])
					&& renamedInventory.resolve(w.m_sceneModel.routes[0].blocks[2])
					&& w.m_sceneModel.blockDependencies.size() == 1
					&& resolvesThroughRenamedBlock(w.m_sceneModel.blockDependencies[0].block)
					&& resolvesThroughRenamedBlock(w.m_sceneModel.blockDependencies[0].dependsOn)
					&& w.m_sceneModel.singleTrackRestrictions.size() == 1
					&& resolvesThroughRenamedBlock(w.m_sceneModel.singleTrackRestrictions[0].startBlock)
					&& resolvesThroughRenamedBlock(w.m_sceneModel.singleTrackRestrictions[0].protectedStartBlock)
					&& w.m_sceneModel.stationBoundaries.size() == 1
					&& resolvesThroughRenamedBlock(w.m_sceneModel.stationBoundaries[0].entranceBlock)
					&& !sceneSignals(w.m_sceneModel).empty()
					&& resolvesThroughRenamedBlock(sceneSignals(w.m_sceneModel).front().protectedSection);
				if (!blockReferencesUpdated)
					facetFailure(facetOk, "infrastructure", "block ID rename did not update decorated/composite references");
				if (w.m_addServiceButton && w.m_serviceListWidget && w.m_serviceRouteCombo && w.m_addStopButton) {
					w.m_addServiceButton->click();
					QApplication::processEvents();
					if (!w.m_sceneModel.services.empty())
						w.m_sceneModel.services.front().composition = "e2e-unresolved-composition";
					if (w.m_serviceListWidget->count() != 1 || w.m_serviceRouteCombo->findData("e2e-block-route") < 0)
						facetFailure(facetOk, "stations/signalling", "service route choices did not refresh immediately");
					else {
						w.m_serviceRouteCombo->setCurrentIndex(w.m_serviceRouteCombo->findData("e2e-block-route"));
						for (const QString& suffix : {QStringLiteral("a"), QStringLiteral("b")}) {
							const bool accepted = editStopDialog(-1, [&](QDialog* dialog) {
								auto* station = dialog->findChild<QComboBox*>("stopEditorStationCombo");
								auto* platform = dialog->findChild<QComboBox*>("stopEditorPlatformCombo");
								const int stationIndex = station->findData("e2e-station-" + suffix);
								station->setCurrentIndex(stationIndex);
								const int platformIndex = platform->findData("e2e-platform-" + suffix);
								platform->setCurrentIndex(platformIndex);
								if (stationIndex < 0 || platformIndex < 0)
									facetFailure(facetOk, "stations/signalling", "modal route-compatible choices missing");
							});
							if (!accepted)
								facetFailure(facetOk, "stations/signalling", "modal stop was not accepted");
						}
						const bool platformMoveUpdatedStop = !w.m_sceneModel.services.empty()
							&& w.m_sceneModel.services.front().stops.size() == 2
							&& setInfrastructureCell("platforms", 1, 0, "e2e-station-a")
							&& w.m_sceneModel.services.front().stops.back().stationId == "e2e-station-a"
							&& setInfrastructureCell("platforms", 1, 0, "e2e-station-b")
							&& w.m_sceneModel.services.front().stops.back().stationId == "e2e-station-b";
						if (!platformMoveUpdatedStop)
							facetFailure(facetOk, "stations/signalling", "moving a referenced platform did not keep its stop usable");
					}
				} else {
					facetFailure(facetOk, "stations/signalling", "service controls unavailable for M3 assignment coverage");
				}
				if (w.m_addIncidentButton && w.m_incidentListWidget && w.m_incidentTargetCombo) {
					w.m_addIncidentButton->click();
					QApplication::processEvents();
					if (w.m_incidentTargetCombo->findText("e2e-signal") < 0)
						facetFailure(facetOk, "stations/signalling", "signal incident target choices did not refresh immediately");
					else {
						w.m_incidentTargetCombo->setCurrentText("e2e-signal");
						if (w.m_incidentStartSecondsEdit && w.m_incidentEndSecondsEdit) {
							w.m_incidentStartSecondsEdit->setText("10");
							w.m_incidentEndSecondsEdit->setText("20");
							w.m_incidentPanel->commitStartSeconds();
							w.m_incidentPanel->commitEndSeconds();
						}
						const SceneIncident* signalIncident = w.m_incidentPanel->selectedIncident();
						if (!signalIncident || signalIncident->target != "e2e-signal")
							facetFailure(facetOk, "stations/signalling",
								"bound signal_failure target did not persist canonically");
					}
					w.m_addIncidentButton->click();
					QApplication::processEvents();
					const bool blockTargetOffered = w.m_incidentTargetCombo
						&& w.m_incidentTargetCombo->findText(QString::fromStdString(renamedBlockId)) >= 0;
					if (!blockTargetOffered) {
						facetFailure(facetOk, "stations/signalling", "block signal_failure target was not offered");
					} else {
						w.m_incidentTargetCombo->setCurrentText(QString::fromStdString(renamedBlockId));
						w.m_incidentPanel->commitTarget();
						const SceneIncident* blockIncident = w.m_incidentPanel->selectedIncident();
						if (!blockIncident || blockIncident->target != renamedBlockId)
							facetFailure(facetOk, "stations/signalling", "block signal_failure target did not persist canonically");
					}
					w.refreshValidationPanel();
				} else {
					facetFailure(facetOk, "stations/signalling", "incident controls unavailable for signal coverage");
				}
				auto refuseInfrastructureDelete = [&](const char* facet, int row, int column,
													  std::size_t expectedSize, const char* label) {
					if (!chooseInfrastructureFacet(facet) || row < 0 || row >= infrastructureTable->rowCount()) {
						facetFailure(facetOk, "delete integrity", QString("%1 facet row unavailable").arg(label));
						return;
					}
					infrastructureTable->setCurrentCell(row, column);
					QApplication::processEvents();
					infrastructureDelete->click();
					QApplication::processEvents();
					const QString facetName = QString::fromLatin1(facet);
					std::size_t actualSize = expectedSize;
					if (facetName == "tracks") actualSize = w.m_sceneModel.tracks.size();
					else if (facetName == "nodes") actualSize = w.m_sceneModel.nodes.size();
					else if (facetName == "arcs") actualSize = w.m_sceneModel.arcs.size();
					else if (facetName == "blocks") actualSize = w.m_sceneModel.blocks.size();
					else if (facetName == "connections") actualSize = w.m_sceneModel.connections.size();
					else if (facetName == "stations") actualSize = w.m_sceneModel.stations.size();
					else if (facetName == "platforms") {
						actualSize = 0;
						for (const auto& station : w.m_sceneModel.stations)
							actualSize += station.platforms.size();
					} else if (facetName == "signals") actualSize = sceneSignals(w.m_sceneModel).size();
					else if (facetName == "routes") actualSize = w.m_sceneModel.routes.size();
					if (actualSize != expectedSize)
						facetFailure(facetOk, "delete integrity", QString("referenced %1 delete was not refused").arg(label));
				};
				refuseInfrastructureDelete("tracks", 0, 0, w.m_sceneModel.tracks.size(), "track");
				refuseInfrastructureDelete("nodes", 0, 0, w.m_sceneModel.nodes.size(), "node");
				if (chooseBlockTrack("e2e-main"))
					refuseInfrastructureDelete("blocks", 0, 0, w.m_sceneModel.blocks.size(), "block");
				else
					facetFailure(facetOk, "delete integrity", "block facet filter unavailable");
				refuseInfrastructureDelete("connections", 0, 0, w.m_sceneModel.connections.size(), "connection");
				refuseInfrastructureDelete("stations", 0, 0, w.m_sceneModel.stations.size(), "station");
				std::size_t platformCount = 0;
				for (const auto& station : w.m_sceneModel.stations)
					platformCount += station.platforms.size();
				refuseInfrastructureDelete("platforms", 0, 1, platformCount, "platform");
				refuseInfrastructureDelete("signals", 0, 0, sceneSignals(w.m_sceneModel).size(), "signal");
				refuseInfrastructureDelete("routes", 0, 0, w.m_sceneModel.routes.size(), "route");
				expectedTracks = w.m_sceneModel.tracks;
				expectedNodes = w.m_sceneModel.nodes;
				expectedArcs = w.m_sceneModel.arcs;
				expectedBlocks = w.m_sceneModel.blocks;
				expectedConnections = w.m_sceneModel.connections;
				expectedRoutes = w.m_sceneModel.routes;
				expectedBlockDependencies = w.m_sceneModel.blockDependencies;
				expectedSingleTrackRestrictions = w.m_sceneModel.singleTrackRestrictions;
				expectedStationBoundaries = w.m_sceneModel.stationBoundaries;
				expectedSignallingAreas = w.m_sceneModel.signallingAreas;
				expectedStations = w.m_sceneModel.stations;
				expectedSignals = sceneSignals(w.m_sceneModel);
				expectedNewCaseServices = w.m_sceneModel.services;
				expectedNewCaseIncidents = defaultScenario(w.m_sceneModel)
					? defaultScenario(w.m_sceneModel)->incidents
					: std::vector<SceneIncident>();
				if (loadTrackPreview(w.m_sceneModel).lines.size() < 2)
					facetFailure(facetOk, "infrastructure", "authored topology did not reach the existing preview");
				if (facetOk)
					std::fprintf(stdout, "E2E_EDITOR_INFRASTRUCTURE_OK\n");
				if (facetOk)
					std::fprintf(stdout, "E2E_EDITOR_STATIONS_SIGNALLING_OK\n");
			}

			const QString newCaseFolder = QDir(outBase).filePath("editor_smoke_new_case");
			const QString newCaseBundle = QDir(outBase).filePath("editor_smoke_new_case.egscene");
			const QString absentSettingsFolder = QDir(outBase).filePath("editor_smoke_new_case_absent_settings");
			if (!QDir().mkpath(outBase))
				facetFailure(facetOk, "new case", "output directory could not be created");
			QDir(newCaseFolder).removeRecursively();
			QDir(absentSettingsFolder).removeRecursively();
			QFile::remove(newCaseBundle);
			const SceneSaveResult folderSave = ::saveScene(w.m_sceneModel, newCaseFolder.toStdString());
			const SceneSaveResult bundleSave = saveSceneBundle(w.m_sceneModel, newCaseBundle.toStdString());
			if (!folderSave.success() || !bundleSave.success())
				facetFailure(facetOk, "new case", "folder or bundle save failed");
			std::string expectedName = "E2E New Case";
			std::string expectedDescription = "new case editor smoke";
			const auto verifyNewCase = [&]() {
				const SceneScenario* scenario = defaultScenario(static_cast<const SceneModel&>(w.m_sceneModel));
				return w.m_sceneLoaded && !w.m_sceneDirty && w.m_sceneModel.schemaVersion == 1 && w.m_sceneModel.name == expectedName && w.m_sceneModel.description == expectedDescription && w.m_sceneModel.baseTime == "09:15:30" && w.m_sceneModel.settings.hasDuration && w.m_sceneModel.settings.durationSeconds == 7200.0 && w.m_sceneModel.settings.hasBufferTime && w.m_sceneModel.settings.bufferTimeSeconds == 30.0 && w.m_sceneModel.settings.hasRecoveryTime && w.m_sceneModel.settings.recoveryTimePercent == 7.5 && w.m_sceneModel.defaultScenarioId == "baseline" && scenario && scenario->id == "baseline" && scenario->name == "Baseline" && sameIncidents(scenario->incidents, expectedNewCaseIncidents) && scenario->entranceDelays.empty() && (expectedTracks.empty() || sameInfrastructure(w.m_sceneModel, expectedTracks, expectedNodes, expectedArcs, expectedBlocks, expectedConnections)) && (expectedRoutes.empty() || sameBlockReferences(w.m_sceneModel, expectedRoutes, expectedBlockDependencies, expectedSingleTrackRestrictions, expectedStationBoundaries)) && (expectedSignallingAreas.empty() || sameSignallingAreas(w.m_sceneModel.signallingAreas, expectedSignallingAreas)) && (expectedStations.empty() || sameStations(w.m_sceneModel.stations, expectedStations)) && (expectedSignals.empty() || sameSignals(sceneSignals(w.m_sceneModel), expectedSignals)) && (expectedNewCaseServices.empty() || sameServices(w.m_sceneModel.services, expectedNewCaseServices));
			};
			if (!w.openSceneDirectory(newCaseFolder) || !verifyNewCase())
				facetFailure(facetOk, "new case", "folder save did not reopen with the edited settings");

			const QString savedName = QStringLiteral("E2E New Case Saved");
			const QString savedDescription = QStringLiteral("new case editor smoke saved");
			if (nameEdit && descriptionEdit && w.m_caseSettingsDock) {
				w.activateWindow();
				w.m_caseSettingsDock->show();
				w.m_caseSettingsDock->raise();
				descriptionEdit->setText(savedDescription);
				w.commitCaseSettings();
				nameEdit->setFocus();
				nameEdit->setText(savedName);
				QApplication::processEvents();
				if (!w.m_saveSceneAction || !w.m_saveSceneAction->isEnabled() || QApplication::focusWidget() != nameEdit)
					facetFailure(facetOk, "new case", "Save action was not ready for focused pending case text");
				else
					w.m_saveSceneAction->trigger();
				expectedName = savedName.toStdString();
				expectedDescription = savedDescription.toStdString();
				if (!w.openSceneDirectory(newCaseFolder) || !verifyNewCase()
					|| !nameEdit || nameEdit->text() != savedName)
					facetFailure(facetOk, "new case", "Save action did not persist focused pending case text");
			} else {
				facetFailure(facetOk, "new case", "case settings controls or dock are unavailable for Save action");
			}

			QFile::remove(newCaseBundle);
			const SceneSaveResult updatedBundleSave = saveSceneBundle(w.m_sceneModel, newCaseBundle.toStdString());
			if (!updatedBundleSave.success() || !w.openSceneDirectory(newCaseBundle) || !verifyNewCase())
				facetFailure(facetOk, "new case", "bundle save did not reopen with the edited settings");
			if (!w.openSceneDirectory(newCaseFolder) || !verifyNewCase())
				facetFailure(facetOk, "new case", "saved folder could not be restored for New action check");

			const QString pendingNewName = QStringLiteral("E2E Pending New Case");
			if (nameEdit && w.m_caseSettingsDock && w.m_newSceneAction) {
				w.m_caseSettingsDock->show();
				w.m_caseSettingsDock->raise();
				nameEdit->setFocus();
				nameEdit->setText(pendingNewName);
				QApplication::processEvents();
				const bool cleanBeforeNew = !w.m_sceneDirty
					&& w.m_sceneModel.name == expectedName
					&& nameEdit->text() == pendingNewName
					&& QApplication::focusWidget() == nameEdit;
				if (!cleanBeforeNew)
					facetFailure(facetOk, "new case", "New action precondition was not a clean scene with focused pending text");
				else {
					cancelConfirmation();
					w.m_newSceneAction->trigger();
					QApplication::processEvents();
					if (w.m_sceneDir != QFileInfo(newCaseFolder).absoluteFilePath()
						|| !w.m_sceneDirty || w.m_sceneModel.name != pendingNewName.toStdString()
						|| nameEdit->text() != pendingNewName)
						facetFailure(facetOk, "new case", "Cancel did not preserve the dirty scene after pending New action text");
				}
			} else {
				facetFailure(facetOk, "new case", "New action or case settings controls are unavailable");
			}

			SceneModel absentSettings = makeNewSceneModel();
			absentSettings.name = "E2E Absent Settings";
			absentSettings.description = "metadata-only baseline";
			absentSettings.settings.hasDuration = false;
			absentSettings.settings.durationSeconds = 0.0;
			absentSettings.settings.hasBufferTime = false;
			absentSettings.settings.bufferTimeSeconds = 0.0;
			absentSettings.settings.hasRecoveryTime = false;
			absentSettings.settings.recoveryTimePercent = 0.0;
			const SceneSaveResult absentSave = ::saveScene(absentSettings, absentSettingsFolder.toStdString());
			if (!absentSave.success() || !w.openSceneDirectory(absentSettingsFolder)
				|| w.m_sceneModel.settings.hasDuration || w.m_sceneModel.settings.hasBufferTime
				|| w.m_sceneModel.settings.hasRecoveryTime)
				facetFailure(facetOk, "new case", "canonical scene with absent optional settings did not load");
			else if (descriptionEdit && w.m_caseSettingsDock) {
				w.m_caseSettingsDock->show();
				w.m_caseSettingsDock->raise();
				descriptionEdit->setText("metadata-only edit");
				w.commitCaseSettings();
				const bool flagsRemainAbsent = !w.m_sceneModel.settings.hasDuration
					&& !w.m_sceneModel.settings.hasBufferTime && !w.m_sceneModel.settings.hasRecoveryTime;
				if (!flagsRemainAbsent || !w.m_saveSceneAction || !w.m_saveSceneAction->isEnabled())
					facetFailure(facetOk, "new case", "metadata-only edit did not keep optional settings absent before Save");
				else {
					w.m_saveSceneAction->trigger();
					if (w.m_sceneDirty || w.m_sceneModel.settings.hasDuration || w.m_sceneModel.settings.hasBufferTime
						|| w.m_sceneModel.settings.hasRecoveryTime
						|| !w.openSceneDirectory(absentSettingsFolder)
						|| w.m_sceneModel.settings.hasDuration || w.m_sceneModel.settings.hasBufferTime
						|| w.m_sceneModel.settings.hasRecoveryTime)
						facetFailure(facetOk, "new case", "metadata-only Save created optional settings");
				}
			} else {
				facetFailure(facetOk, "new case", "case settings controls or dock are unavailable for metadata-only Save");
			}
			if (!w.openSceneDirectory(originalScenePath))
				facetFailure(facetOk, "new case", "original scene could not be restored");
		}
		if (facetOk)
			std::fprintf(stdout, "E2E_EDITOR_NEW_CASE_OK\n");
	}

	// step c: exercise the passenger authoring surface, legacy import seam, and
	// native passenger/platform staging entirely through public controls.
	if (w.m_sceneLoaded) {
		bool facetOk = true;
		auto* passengerDock = w.findChild<QDockWidget*>("passengerDock");
		auto* passengerList = w.findChild<QListWidget*>("passengerListWidget");
		auto* passengerAdd = w.findChild<QPushButton*>("passengerAddButton");
		auto* passengerDelete = w.findChild<QPushButton*>("passengerDeleteButton");
		auto* passengerImport = w.findChild<QPushButton*>("passengerImportButton");
		auto* passengerId = w.findChild<QLineEdit*>("passengerIdEdit");
		auto* passengerTabs = w.findChild<QTabWidget*>("passengerEditorTabs");
		auto* journeyList = w.findChild<QListWidget*>("passengerJourneyListWidget");
		auto* journeyAdd = w.findChild<QPushButton*>("passengerJourneyAddButton");
		auto* journeyDelete = w.findChild<QPushButton*>("passengerJourneyDeleteButton");
		auto* journeyId = w.findChild<QLineEdit*>("passengerJourneyIdEdit");
		auto* journeyActivity = w.findChild<QLineEdit*>("passengerJourneyActivityEdit");
		auto* journeyOrigin = w.findChild<QComboBox*>("passengerJourneyOriginCombo");
		auto* journeyDestination = w.findChild<QComboBox*>("passengerJourneyDestinationCombo");
		auto* legList = w.findChild<QListWidget*>("passengerLegListWidget");
		auto* legAdd = w.findChild<QPushButton*>("passengerLegAddButton");
		auto* legDelete = w.findChild<QPushButton*>("passengerLegDeleteButton");
		auto* legMoveUp = w.findChild<QPushButton*>("passengerLegMoveUpButton");
		auto* legMoveDown = w.findChild<QPushButton*>("passengerLegMoveDownButton");
		auto* legId = w.findChild<QLineEdit*>("passengerLegIdEdit");
		auto* legOrigin = w.findChild<QComboBox*>("passengerLegOriginCombo");
		auto* legDestination = w.findChild<QComboBox*>("passengerLegDestinationCombo");
		auto* legService = w.findChild<QComboBox*>("passengerLegServiceCombo");
		auto* legOccurrence = w.findChild<QSpinBox*>("passengerLegOccurrenceSpin");
		auto* importResults = w.findChild<QTableWidget*>("passengerImportResultTable");
		const auto commitLineEdit = [](QLineEdit* edit) {
			return edit && QMetaObject::invokeMethod(edit, "editingFinished", Qt::DirectConnection);
		};
		const auto selectData = [](QComboBox* combo, const QString& value) {
			if (!combo)
				return false;
			const int index = combo->findData(value);
			if (index < 0)
				return false;
			combo->setCurrentIndex(index);
			QApplication::processEvents();
			return combo->currentData().toString() == value;
		};
		if (!passengerDock || !passengerList || !passengerAdd || !passengerDelete
			|| !passengerImport || !passengerId || !passengerTabs || !journeyList
			|| !journeyAdd || !journeyDelete || !journeyId || !journeyActivity
			|| !journeyOrigin || !journeyDestination || !legList || !legAdd
			|| !legDelete || !legMoveUp || !legMoveDown || !legId || !legOrigin
			|| !legDestination || !legService || !legOccurrence || !importResults) {
			facetFailure(facetOk, "passenger", "public passenger controls were unavailable");
		} else {
			passengerDock->show();
			passengerDock->raise();
			QApplication::processEvents();
			const SceneService* passengerService = nullptr;
			std::string originStationId;
			std::string destinationStationId;
			for (const auto& service : w.m_sceneModel.services) {
				for (std::size_t first = 0; first < service.stops.size() && !passengerService; ++first) {
					for (std::size_t last = first + 1; last < service.stops.size(); ++last) {
						if (service.stops[first].stationId == service.stops[last].stationId)
							continue;
						const bool knownOrigin = std::any_of(w.m_sceneModel.stations.begin(),
							w.m_sceneModel.stations.end(), [&](const SceneStation& station) {
								return station.id == service.stops[first].stationId;
							});
						const bool knownDestination = std::any_of(w.m_sceneModel.stations.begin(),
							w.m_sceneModel.stations.end(), [&](const SceneStation& station) {
								return station.id == service.stops[last].stationId;
							});
						if (!knownOrigin || !knownDestination)
							continue;
						passengerService = &service;
						originStationId = service.stops[first].stationId;
						destinationStationId = service.stops[last].stationId;
						break;
					}
				}
				if (passengerService)
					break;
			}
			if (!passengerService) {
				facetFailure(facetOk, "passenger", "no service with two canonical station stops was available");
			} else {
				e2ePassengerId = sceneUniquePassengerId(w.m_sceneModel, "e2e_passenger");
				const int passengerCountBefore = passengerList->count();
				passengerAdd->click();
				QApplication::processEvents();
				if (passengerList->count() != passengerCountBefore + 1) {
					facetFailure(facetOk, "passenger", "Add did not create a passenger row");
				} else {
					passengerId->setText(QString::fromStdString(e2ePassengerId));
					if (!commitLineEdit(passengerId))
						facetFailure(facetOk, "passenger", "passenger ID edit did not commit");
					e2eJourneyId = sceneUniquePassengerJourneyId(w.m_sceneModel, "e2e_journey");
					journeyAdd->click();
					QApplication::processEvents();
					ScenePassenger* authoredPassenger = w.m_passengerPanel->selectedPassenger();
					if (!authoredPassenger || authoredPassenger->journeys.empty()) {
						facetFailure(facetOk, "passenger", "Journey Add did not create a selected journey");
					} else {
						journeyId->setText(QString::fromStdString(e2eJourneyId));
						journeyActivity->setText("e2e commute");
						if (!commitLineEdit(journeyId) || !commitLineEdit(journeyActivity)
							|| !selectData(journeyOrigin, QString::fromStdString(originStationId))
							|| !selectData(journeyDestination, QString::fromStdString(destinationStationId)))
							facetFailure(facetOk, "passenger", "journey fields or typed station references did not commit");
						const std::array<double, 4> windows = {100.0, 200.0, 300.0, 400.0};
						for (std::size_t index = 0; index < windows.size(); ++index)
							if (w.m_passengerJourneyWindowEdits[index])
								w.m_passengerJourneyWindowEdits[index]->setValue(windows[index]);
						passengerTabs->setCurrentIndex(1);
						legAdd->click();
						QApplication::processEvents();
						e2eLegId = sceneUniquePassengerLegId(w.m_sceneModel, "e2e_leg");
						const auto configureLeg = [&](const std::string& id) {
							legId->setText(QString::fromStdString(id));
							const bool idOk = commitLineEdit(legId);
							const bool serviceOk = selectData(legService,
								QString::fromStdString(passengerService->id));
							const bool originOk = selectData(legOrigin,
								QString::fromStdString(originStationId));
							const bool destinationOk = selectData(legDestination,
								QString::fromStdString(destinationStationId));
							legOccurrence->setValue(1);
							QApplication::processEvents();
							return idOk && serviceOk && originOk && destinationOk
								&& w.m_passengerPanel->selectedPassengerLeg() && w.m_passengerPanel->selectedPassengerLeg()->id == id
								&& w.m_passengerPanel->selectedPassengerLeg()->serviceId == passengerService->id
								&& w.m_passengerPanel->selectedPassengerLeg()->occurrence == 1;
						};
						if (!configureLeg(e2eLegId))
							facetFailure(facetOk, "passenger", "leg ID, typed stops, service, or occurrence did not commit");
						const std::string secondLegId = sceneUniquePassengerLegId(w.m_sceneModel, "e2e_leg_second");
						legAdd->click();
						QApplication::processEvents();
						if (!configureLeg(secondLegId))
							facetFailure(facetOk, "passenger", "second leg authoring did not commit");
						legMoveUp->click();
						QApplication::processEvents();
						const bool movedUp = w.m_passengerPanel->selectedPassengerJourney() && w.m_passengerPanel->selectedPassengerJourney()->legs.size() >= 2
							&& w.m_passengerPanel->selectedPassengerJourney()->legs[0].id == secondLegId;
						legMoveDown->click();
						QApplication::processEvents();
						const bool movedDown = w.m_passengerPanel->selectedPassengerJourney() && w.m_passengerPanel->selectedPassengerJourney()->legs.size() >= 2
							&& w.m_passengerPanel->selectedPassengerJourney()->legs[0].id == e2eLegId;
						if (!movedUp || !movedDown)
							facetFailure(facetOk, "passenger", "leg Move Up/Down did not retain order");
						acceptConfirmation();
						legDelete->click();
						QApplication::processEvents();
						if (!w.m_passengerPanel->selectedPassengerJourney() || w.m_passengerPanel->selectedPassengerJourney()->legs.size() != 1
							|| w.m_passengerPanel->selectedPassengerJourney()->legs.front().id != e2eLegId)
							facetFailure(facetOk, "passenger", "leg Delete did not remove only the temporary leg");

						const std::size_t journeyCountBefore = w.m_passengerPanel->selectedPassenger()
							? w.m_passengerPanel->selectedPassenger()->journeys.size()
							: 0;
						journeyAdd->click();
						QApplication::processEvents();
						const std::string temporaryJourneyId = sceneUniquePassengerJourneyId(w.m_sceneModel, "e2e_temp_journey");
						journeyId->setText(QString::fromStdString(temporaryJourneyId));
						commitLineEdit(journeyId);
						acceptConfirmation();
						journeyDelete->click();
						QApplication::processEvents();
						if (!w.m_passengerPanel->selectedPassenger() || w.m_passengerPanel->selectedPassenger()->journeys.size() != journeyCountBefore)
							facetFailure(facetOk, "passenger", "Journey Delete did not remove the temporary journey");
						const int passengerCountWithAuthor = passengerList->count();
						passengerAdd->click();
						QApplication::processEvents();
						const std::string temporaryPassengerId = sceneUniquePassengerId(w.m_sceneModel, "e2e_temp_passenger");
						passengerId->setText(QString::fromStdString(temporaryPassengerId));
						commitLineEdit(passengerId);
						acceptConfirmation();
						passengerDelete->click();
						QApplication::processEvents();
						if (passengerList->count() != passengerCountWithAuthor)
							facetFailure(facetOk, "passenger", "Passenger Delete did not remove the temporary passenger");

						QTemporaryDir passengerFixture;
						const QString passengerDir = passengerFixture.isValid()
							? QDir(passengerFixture.path()).filePath("Passengers")
							: QString();
						const bool fixtureReady = !passengerDir.isEmpty() && QDir().mkpath(passengerDir)
							&& writeCsvFile(QDir(passengerDir).filePath("DAS_FrenchCaseStudy.csv"),
								"c0,person_id,c2,c3,journey_no,activity,destination,c7,c8,c9,arrival,c11,origin,c13,departure\n"
									+ std::string("x,e2e_imported,x,x,1,import,") + destinationStationId
									+ ",x,x,x,8.25,x," + originStationId + ",x,7.25\n"
									+ "x," + e2ePassengerId + ",x,x,1,collision," + destinationStationId
									+ ",x,x,x,8.25,x," + originStationId + ",x,7.25\n"
									+ "x,e2e_unresolved,x,x,1,unresolved," + destinationStationId
									+ ",x,x,x,8.25,x,missing-origin,x,7.25\n"
									+ "malformed,row\n")
							&& writeCsvFile(QDir(passengerDir).filePath("RouteChoiceFC_EQ1.csv"),
								"person_id,destination,nb_transfers,r_service_lines_id_1\n"
									+ std::string("e2e_imported,") + destinationStationId + ",0,"
									+ (passengerService->operatingCode.empty() ? passengerService->id
																			   : passengerService->operatingCode)
									+ "-1\n"
									+ e2ePassengerId + "," + destinationStationId + ",0,"
									+ (passengerService->operatingCode.empty() ? passengerService->id
																			   : passengerService->operatingCode)
									+ "-1\n"
									+ "e2e_unresolved," + destinationStationId + ",0,missing-service-1\n"
									+ "bad\n");
						if (!fixtureReady) {
							facetFailure(facetOk, "passenger import", "could not create the exact legacy passenger fixture");
						} else {
							importPassengersFrom(passengerImport, passengerFixture.path());
							bool sawAccepted = false;
							bool sawRejected = false;
							bool sawUnresolved = false;
							int rejectedCollisionRows = 0;
							for (int row = 0; row < importResults->rowCount(); ++row) {
								const QTableWidgetItem* statusItem = importResults->item(row, 2);
								if (!statusItem)
									continue;
								sawAccepted = sawAccepted || statusItem->text() == "Accepted";
								sawRejected = sawRejected || statusItem->text() == "Rejected";
								sawUnresolved = sawUnresolved || statusItem->text() == "Unresolved";
								if (statusItem->text() == "Rejected" && importResults->item(row, 1)
									&& importResults->item(row, 1)->text() == "2")
									++rejectedCollisionRows;
							}
							if (!sawAccepted || !sawRejected || !sawUnresolved || rejectedCollisionRows < 2)
								facetFailure(facetOk, "passenger import", "result table did not expose accepted, rejected, and unresolved rows");
							const auto findPassengerRow = [&](const std::string& id) {
								const auto matches = passengerList->findItems(QString::fromStdString(id), Qt::MatchExactly);
								return matches.isEmpty() ? -1 : passengerList->row(matches.front());
							};
							const int unresolvedRow = findPassengerRow("e2e_unresolved");
							if (unresolvedRow < 0) {
								facetFailure(facetOk, "passenger import", "unresolved passenger was not appended for correction");
							} else {
								passengerList->setCurrentRow(unresolvedRow);
								passengerTabs->setCurrentIndex(1);
								QApplication::processEvents();
								const ScenePassengerJourney* unresolvedJourney = w.m_passengerPanel->selectedPassengerJourney();
								const ScenePassengerLeg* unresolvedLeg = w.m_passengerPanel->selectedPassengerLeg();
								const bool invalidOriginVisible = unresolvedJourney
									&& unresolvedJourney->originStationId == "missing-origin"
									&& w.m_passengerJourneyOriginCombo->currentText().startsWith("Invalid: missing-origin");
								const bool legStateAccurate = unresolvedLeg
									? legService->currentText().startsWith("Invalid: missing-service")
										&& legOrigin->currentText().startsWith("Invalid:")
									: legService->currentText() == "Select a leg"
										&& legOrigin->currentText() == "Select a journey or leg";
								if (!invalidOriginVisible || !legStateAccurate)
									facetFailure(facetOk, "passenger import", "selected unresolved references or neutral absent-leg state were not visible");
								passengerTabs->setCurrentIndex(0);
								acceptConfirmation();
								passengerDelete->click();
								QApplication::processEvents();
								if (findPassengerRow("e2e_unresolved") >= 0)
									facetFailure(facetOk, "passenger import", "invalid imported passenger was not deletable through the public control");
							}
							const auto passengersBeforeFailedImport = w.m_sceneModel.passengers;
							QTemporaryDir partialFixture;
							const QString partialPassengerDir = QDir(partialFixture.path()).filePath("Passengers");
							const bool partialReady = partialFixture.isValid()
								&& QDir().mkpath(partialPassengerDir)
								&& writeCsvFile(QDir(partialPassengerDir).filePath("DAS_FrenchCaseStudy.csv"),
									"c0,person_id,c2,c3,journey_no,activity,destination,c7,c8,c9,arrival,c11,origin,c13,departure\n");
							if (!partialReady) {
								facetFailure(facetOk, "passenger import", "could not create the partial import fixture");
							} else {
								importPassengersFrom(passengerImport, partialFixture.path());
								if (!samePassengers(passengersBeforeFailedImport, w.m_sceneModel.passengers)
									|| importResults->rowCount() != 1 || !importResults->item(0, 2)
									|| importResults->item(0, 2)->text() != "Rejected")
									facetFailure(facetOk, "passenger import", "a failed import changed the scene or hid its rejection");
							}
						}

						const auto infrastructureDiagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), w.m_sceneModel);
						const auto operationsDiagnostics = buildOperationsFromScene(defaultRun(),
							w.m_sceneModel, w.m_runSelection.scenarioId(), SceneRunSelection());
						const auto stagedPassenger = std::find_if(AllDailyPassengers.begin(), AllDailyPassengers.end(),
							[&](const Passenger& passenger) { return passenger.ID == e2ePassengerId; });
						bool nativePassengerOk = !hasErrors(infrastructureDiagnostics)
							&& !hasErrors(operationsDiagnostics) && stagedPassenger != AllDailyPassengers.end();
						if (nativePassengerOk) {
							const auto stagedJourney = std::find_if(stagedPassenger->Journeys.begin(),
								stagedPassenger->Journeys.end(), [&](const Journey& journey) {
									return journey.ID == e2eJourneyId;
								});
							nativePassengerOk = stagedJourney != stagedPassenger->Journeys.end();
							if (nativePassengerOk) {
								const auto stagedLeg = std::find_if(stagedJourney->Trips.begin(),
									stagedJourney->Trips.end(), [&](const Trip& trip) {
										return trip.TripID == e2eLegId;
									});
								nativePassengerOk = stagedLeg != stagedJourney->Trips.end();
							}
						}
						bool nativePlatformOk = false;
						if (!w.m_sceneModel.stations.empty() && !w.m_sceneModel.stations.front().platforms.empty()) {
							const ScenePlatform& sourcePlatform = w.m_sceneModel.stations.front().platforms.front();
							const double expectedLength = sourcePlatform.hasLength ? sourcePlatform.lengthM : 100.0;
							const double expectedWidth = sourcePlatform.hasWidth ? sourcePlatform.widthM : 2.5;
							const int expectedCapacity = static_cast<int>((expectedLength * expectedWidth)
								/ (3.14159 * std::pow(0.8, 2)) * 0.8);
							const auto stagedPlatform = std::find_if(AllStationPlatforms.begin(), AllStationPlatforms.end(),
								[&](const StationPlatform& platform) { return platform.ID == sourcePlatform.id; });
							if (stagedPlatform != AllStationPlatforms.end())
								nativePlatformOk = stagedPlatform->length == expectedLength
									&& stagedPlatform->width == expectedWidth
									&& stagedPlatform->Max_Passenger_Volume == expectedCapacity;
						}
						if (!nativePassengerOk || !nativePlatformOk)
							facetFailure(facetOk, "passenger native staging", "passenger leg or platform geometry did not reach native staging");
					}
				}
			}
		}
		if (facetOk)
			std::fprintf(stdout, "E2E_EDITOR_PASSENGER_OK\n");
	}

	// step b: validation assertions
	if (w.m_sceneLoaded) {
		if (!w.m_loadedDataTree || w.m_loadedDataTree->topLevelItemCount() <= 0) {
			ok = false;
			failures << "validation: loaded data tree empty";
		} else if (w.m_loadedDataTree->topLevelItem(0)->childCount() <= 0) {
			ok = false;
			failures << "validation: loaded data tree lacks drilldown rows";
		}
		if (hasErrors(w.m_sceneDiagnostics)) {
			ok = false;
			failures << "validation: errors on open";
		}
		if (!w.m_compositionListWidget || w.m_compositionListWidget->count() <= 0) {
			ok = false;
			failures << "composition: pane empty";
		}
		if (!w.m_serviceListWidget || w.m_serviceListWidget->count() <= 0) {
			ok = false;
			failures << "service: pane empty";
		}
		bool scenarioOk = w.m_scenarioListWidget && w.m_scenarioListWidget->count() > 0
			&& w.m_runSelection.scenarioId() == w.m_sceneModel.defaultScenarioId;
		if (!scenarioOk) {
			ok = false;
			failures << "scenario: canonical default was not initially selected";
		} else {
			const int originalScenarioCount = w.m_scenarioListWidget->count();
			if (!w.m_blankScenarioButton || !w.m_duplicateScenarioButton || !w.m_deleteScenarioButton) {
				ok = false;
				failures << "scenario: scenario action buttons unavailable";
			} else {
				w.m_blankScenarioButton->click();
				QApplication::processEvents();
				w.m_duplicateScenarioButton->click();
				QApplication::processEvents();
			}
			if (w.m_scenarioListWidget->count() != originalScenarioCount + 2) {
				ok = false;
				failures << "scenario: blank or duplicate did not add an isolated scenario";
			}
			w.m_scenarioListWidget->setCurrentRow(0);
			QApplication::processEvents();
			if (w.m_runSelection.scenarioId() != w.m_sceneModel.defaultScenarioId) {
				ok = false;
				failures << "scenario: selection did not return to canonical default";
			}
			const int countBeforeDefaultDelete = w.m_scenarioListWidget->count();
			w.m_deleteScenarioButton->click();
			w.m_scenarioPanel->deleteScenario();
			QApplication::processEvents();
			if (w.m_scenarioListWidget->count() != countBeforeDefaultDelete
				|| w.m_runSelection.scenarioId() != w.m_sceneModel.defaultScenarioId
				|| w.m_deleteScenarioButton->isEnabled()) {
				ok = false;
				failures << "scenario: default deletion was not refused or button stayed enabled";
			}
			const int nonDefaultRow = w.m_scenarioListWidget->count() - 1;
			w.m_scenarioListWidget->setCurrentRow(nonDefaultRow);
			QApplication::processEvents();
			acceptConfirmation();
			w.m_deleteScenarioButton->click();
			QApplication::processEvents();
			if (w.m_scenarioListWidget->count() != originalScenarioCount + 1
				|| w.m_runSelection.scenarioId() != w.m_sceneModel.defaultScenarioId
				|| !defaultScenario(static_cast<const SceneModel&>(w.m_sceneModel))) {
				ok = false;
				failures << "scenario: non-default deletion did not select a valid default";
			}
			if (w.m_scenarioListWidget->count() > originalScenarioCount) {
				for (int row = 0; row < w.m_scenarioListWidget->count(); ++row) {
					if (w.m_sceneModel.scenarios[static_cast<std::size_t>(row)].id == w.m_sceneModel.defaultScenarioId)
						continue;
					w.m_scenarioListWidget->setCurrentRow(row);
					acceptConfirmation();
					w.m_deleteScenarioButton->click();
					QApplication::processEvents();
					break;
				}
			}
		}
		bool explorerOk = w.m_loadedDataTree && w.m_loadedDataTree->topLevelItemCount() > 0;
		QTreeWidgetItem* caseRoot = explorerOk ? w.m_loadedDataTree->topLevelItem(0) : nullptr;
		explorerOk = explorerOk && caseRoot->text(0) == "Case Study"
			&& caseRoot->text(3) != "Invalid" && caseRoot->text(3) != "Failed";
		for (const QString& category : {QStringLiteral("Source path"), QStringLiteral("Canonical schema version"),
				 QStringLiteral("Runtime"), QStringLiteral("Results")}) {
			const QList<QTreeWidgetItem*> rows = w.m_loadedDataTree->findItems(
				category, Qt::MatchExactly | Qt::MatchRecursive, 0);
			QTreeWidgetItem* row = rows.isEmpty() ? nullptr : rows.front();
			if (!row || row->text(3).isEmpty())
				explorerOk = false;
		}
		if (w.m_sceneIsBundle) {
			const QList<QTreeWidgetItem*> bundleRows = w.m_loadedDataTree->findItems(
				"Bundle format version", Qt::MatchExactly | Qt::MatchRecursive, 0);
			QTreeWidgetItem* bundleRow = bundleRows.isEmpty() ? nullptr : bundleRows.front();
			if (!bundleRow || bundleRow->text(1) != "1")
				explorerOk = false;
		}
		QTreeWidgetItem* trainTarget = nullptr;
		for (QTreeWidgetItemIterator it(w.m_loadedDataTree); *it && !trainTarget; ++it) {
			if ((*it)->data(0, kLoadedDataTargetTypeRole).toString() == "train_unit")
				trainTarget = *it;
		}
		if (!trainTarget || !w.m_trainUnitListWidget) {
			explorerOk = false;
		} else {
			w.activateLoadedDataItem(trainTarget);
			explorerOk = explorerOk && w.m_trainUnitListWidget->currentItem()
				&& w.m_trainUnitListWidget->currentItem()->text() == trainTarget->text(0)
				&& w.m_trainUnitDock && w.m_trainUnitDock->isVisible();
		}
		bool hasParameterSource = false;
		bool hasTractionSource = false;
		for (QLabel* label : w.findChildren<QLabel*>()) {
			hasParameterSource = hasParameterSource || label->text() == "Parameter source reference";
			hasTractionSource = hasTractionSource || label->text() == "Tractive-effort source reference";
		}
		bool hasPlotButton = false;
		for (QPushButton* button : w.findChildren<QPushButton*>())
			hasPlotButton = hasPlotButton || button->text() == "Plot input traction characteristic";
		const bool hasEditableTrainSources = w.m_trainUnitSourceDataEdit && w.m_trainUnitSourceTractionEdit;
		const bool hasPlannedArrival = w.m_stopTableWidget && w.m_stopTableWidget->columnCount() == 5
			&& w.m_stopTableWidget->horizontalHeaderItem(3)->text() == "Planned arrival";
		const bool hasPlannedDeparture = w.m_stopTableWidget && w.m_stopTableWidget->columnCount() == 5
			&& w.m_stopTableWidget->horizontalHeaderItem(4)->text() == "Planned departure";
		bool axesOk = false;
		if (w.m_trainUnitListWidget && w.m_trainUnitListWidget->count() > 0) {
			w.m_trainUnitListWidget->setCurrentRow(0);
			if (w.m_plotTrainUnitTractionButton && w.m_plotTrainUnitTractionButton->isEnabled()) {
				w.m_plotTrainUnitTractionButton->click();
				QApplication::processEvents();
				for (QChartView* view : w.findChildren<QChartView*>()) {
					QChart* chart = view->chart();
					if (!chart)
						continue;
					bool hasSpeedAxis = false;
					bool hasEffortAxis = false;
					for (QAbstractAxis* axis : chart->axes()) {
						hasSpeedAxis = hasSpeedAxis || axis->titleText() == "Speed (km/h)";
						hasEffortAxis = hasEffortAxis || axis->titleText() == "Tractive effort (kN)";
					}
					axesOk = hasSpeedAxis && hasEffortAxis;
					view->window()->close();
				}
			}
		}
		// Exercise the shared input plot, including Qt's actual zoom/reset axes.
		int tractionPlotIndex = 0;
		for (const auto& fixture : std::vector<std::pair<std::array<double, 5>, bool>>{
				 {{0, 10, 20000, 1000, 0}, false}, {{0, 10, 20000, 0, 0}, false},
				 {{0, 10, 0, 0, 0}, false}, {{5, 5, 20000, 0, 0}, false},
				 {{0, 10, -1000, 200, 0}, true}, {{0, 1, 1, -128, 2048}, true}}) {
			SceneTrainUnit unit;
			unit.id = "Input traction axis check";
			unit.tractionCurve = {fixture.first};
			w.plotTrainUnitTraction(unit);
			QApplication::processEvents();
			DiagramWindow* window = w.findChildren<DiagramWindow*>().last();
			QChartView* view = window->findChild<QChartView*>();
			QChart* chart = view->chart();
			auto* axis = qobject_cast<QValueAxis*>(chart->axes(Qt::Vertical).value(0));
			auto* series = qobject_cast<QLineSeries*>(chart->series().value(0));
			const auto samples = sampleTractionCurve(unit.tractionCurve);
			bool plotOk = axis && series && axis->min() == 0.0 && axis->max() > 0.0
				&& series->count() == static_cast<int>(samples.size())
				&& (samples.size() != 1 || series->pointsVisible());
			for (int i = 0; plotOk && i < series->count(); ++i) {
				plotOk = series->at(i) == QPointF(samples[i].first * 3.6, samples[i].second / 1000.0)
					&& series->at(i).y() <= axis->max();
			}
			const QLabel* tractionWarning = window->findChild<QLabel*>("diagramWarning");
			plotOk = plotOk && chart->title().contains("negative") == fixture.second
				&& tractionWarning && tractionWarning->text().contains("Negative") == fixture.second
				&& !window->findChild<QWidget*>("diagramDetailsPanel");
			if (axis) {
				const double maximum = axis->max();
				chart->zoom(2.0);
				plotOk = plotOk && axis->min() > 0.0;
				for (QPushButton* button : window->findChildren<QPushButton*>())
					if (button->text() == "Reset zoom")
						button->click();
				plotOk = plotOk && axis->min() == 0.0 && axis->max() == maximum;
			}
			const QString plotPath = QDir(qEnvironmentVariable("QEGTRAIN_E2E_OUT", QDir::tempPath()))
										 .filePath(QString("input-traction-%1.png").arg(tractionPlotIndex++));
			plotOk = view->grab().save(plotPath, "PNG") && plotOk;
			if (!plotOk)
				failures << QString("input traction axes/reset/samples: %1").arg(plotPath);
			axesOk = axesOk && plotOk;
			window->close();
		}
		if (!explorerOk || !hasParameterSource || !hasTractionSource || !hasPlotButton || !hasEditableTrainSources
			|| !hasPlannedArrival || !hasPlannedDeparture || !axesOk) {
			ok = false;
			failures << "explorer: load review, activation, generic provenance, plot axes, or timetable labels missing";
		} else {
			std::fprintf(stdout, "E2E_EDITOR_EXPLORER_OK\n");
		}
		expectedCompositions = w.m_sceneModel.compositions;
		expectedTrainUnits = w.m_sceneModel.trainUnits;
		expectedServices = w.m_sceneModel.services;
		expectedIncidents = w.selectedScenarioIncidents();
	}

	std::string editedTrainUnitId;
	if (!w.m_sceneLoaded || !w.m_trainUnitListWidget) {
		bool facetOk = false;
		facetFailure(facetOk, "train unit", "scene or train-unit list unavailable");
	} else {
		bool facetOk = true;
		const int originalCount = w.m_trainUnitListWidget->count();
		const auto headings = w.m_trainUnitDock->findChildren<QLabel*>("rollingStockSectionHeading");
		const QStringList sectionTitles{"Rolling stock unit ID", "Rolling stock engine", "Rolling stock unit characteristics"};
		bool layoutOk = headings.size() == sectionTitles.size();
		for (int i = 0; layoutOk && i < headings.size(); ++i) {
			layoutOk = headings[i]->text() == sectionTitles[i] && headings[i]->font().bold()
				&& headings[i]->font().pointSizeF() > w.m_trainUnitIdEdit->font().pointSizeF()
				&& (i == 0 || headings[i - 1]->y() < headings[i]->y());
		}
		layoutOk = layoutOk && headings.size() == 3
			&& w.m_trainUnitTractionTable->y() > headings[1]->y()
			&& w.m_plotTrainUnitTractionButton->y() < headings[2]->y();
		const bool wasDirty = w.m_sceneDirty;
		w.m_sceneDirty = false;
		const QSize oldSize = w.m_trainUnitDock->size();
		const QSize oldTableSize = w.m_trainUnitTractionTable->size();
		const bool wasFloating = w.m_trainUnitDock->isFloating();
		w.m_trainUnitDock->setFloating(true);
		w.m_trainUnitDock->resize(1400, 1200);
		QApplication::processEvents();
		layoutOk = layoutOk && !w.m_sceneDirty && w.m_trainUnitTractionTable->width() > oldTableSize.width()
			&& w.m_trainUnitTractionTable->height() > oldTableSize.height();
		const QString layoutShot = QDir(qEnvironmentVariable("QEGTRAIN_E2E_OUT", QDir::tempPath()))
									   .filePath("rolling-stock-expanded.png");
		layoutOk = w.m_trainUnitDock->grab().save(layoutShot, "PNG") && layoutOk;
		w.m_trainUnitDock->setFloating(wasFloating);
		w.m_trainUnitDock->resize(oldSize);
		w.m_sceneDirty = wasDirty;
		w.m_trainUnitListWidget->setCurrentRow(-1);
		layoutOk = layoutOk && !w.m_trainUnitIdEdit->isEnabled() && !w.m_trainUnitSourceDataEdit->isEnabled()
			&& !w.m_trainUnitSourceTractionEdit->isEnabled() && !w.m_trainUnitTractionTable->isEnabled();
		for (auto* edit : w.m_trainUnitPhysicalEdits)
			layoutOk = layoutOk && edit && !edit->isEnabled();
		if (!layoutOk)
			facetFailure(facetOk, "rolling stock layout", "section order, typography, resize or empty selection changed");
		w.m_trainUnitListWidget->setCurrentRow(0);
		w.m_trainUnitPanel->addUnit();
		if (w.m_trainUnitListWidget->count() != originalCount + 1) {
			facetFailure(facetOk, "train unit", "add did not apply");
		} else {
			w.m_trainUnitListWidget->setCurrentRow(originalCount);
			const std::string initialTrainUnitId = w.m_sceneModel.trainUnits[originalCount].id;
			w.m_compositionListWidget->setCurrentRow(0);
			acceptUnitChoice(QString::fromStdString(initialTrainUnitId));
			w.m_compositionPanel->addUnit();
			QApplication::processEvents();
			bool assignedInitialId = false;
			if (w.m_compositionListWidget->currentRow() >= 0
				&& w.m_compositionListWidget->currentRow() < static_cast<int>(w.m_sceneModel.compositions.size())) {
				const auto& units = w.m_sceneModel.compositions[w.m_compositionListWidget->currentRow()].units;
				assignedInitialId = std::find(units.begin(), units.end(), initialTrainUnitId) != units.end();
			}
			if (!assignedInitialId)
				facetFailure(facetOk, "train unit", "composition assignment did not apply under the initial id");

			const std::string initialIdBeforeDuplicateRename = w.m_sceneModel.trainUnits[originalCount].id;
			const std::vector<SceneComposition> compositionsBeforeDuplicateRename = w.m_sceneModel.compositions;
			if (originalCount <= 0) {
				facetFailure(facetOk, "train unit", "scene lacks another train unit id for duplicate rename coverage");
			} else if (w.m_trainUnitIdEdit) {
				const std::string conflictingId = w.m_sceneModel.trainUnits.front().id;
				w.m_trainUnitIdEdit->setText(QString::fromStdString(conflictingId));
				w.m_trainUnitPanel->commitIdEdit();
				const bool selectedIdUnchanged = w.m_sceneModel.trainUnits[originalCount].id == initialIdBeforeDuplicateRename;
				const bool editorIdRestored = w.m_trainUnitIdEdit->text().trimmed().toStdString() == initialIdBeforeDuplicateRename;
				const bool referencesUnchanged = sameCompositions(compositionsBeforeDuplicateRename, w.m_sceneModel.compositions);
				if (!selectedIdUnchanged || !editorIdRestored || !referencesUnchanged)
					facetFailure(facetOk, "train unit", "duplicate-id rename changed the selected unit or composition references");
			}

			editedTrainUnitId = sceneUniqueTrainUnitId(w.m_sceneModel, "e2e_train_unit");
			if (!w.m_trainUnitIdEdit) {
				facetFailure(facetOk, "train unit", "id editor unavailable");
			} else {
				w.m_trainUnitIdEdit->setText(QString::fromStdString(editedTrainUnitId));
				w.m_trainUnitPanel->commitIdEdit();
			}
			bool oldReferencePresent = false;
			bool newReferencePresent = false;
			for (const auto& composition : w.m_sceneModel.compositions) {
				for (const auto& unitId : composition.units) {
					oldReferencePresent = oldReferencePresent || unitId == initialTrainUnitId;
					newReferencePresent = newReferencePresent || unitId == editedTrainUnitId;
				}
			}
			if (oldReferencePresent || !newReferencePresent)
				facetFailure(facetOk, "train unit", "rename did not update composition references");

			w.m_trainUnitListWidget->setCurrentRow(originalCount);
			const QString initialSourceData = QStringLiteral("e2e/source-data-initial.txt");
			const QString initialSourceTraction = QStringLiteral("e2e/source-traction-initial.txt");
			const QString editedSourceData = QStringLiteral("e2e/source-data-final.txt");
			const QString editedSourceTraction = QStringLiteral("e2e/source-traction-final.txt");
			if (!w.m_trainUnitSourceDataEdit || !w.m_trainUnitSourceTractionEdit) {
				facetFailure(facetOk, "train unit", "source reference editors unavailable");
			} else {
				w.m_trainUnitSourceDataEdit->setText(initialSourceData);
				w.m_trainUnitSourceTractionEdit->setText(initialSourceTraction);
				QMetaObject::invokeMethod(w.m_trainUnitSourceDataEdit, "editingFinished", Qt::DirectConnection);
				w.m_trainUnitSourceDataEdit->setText(editedSourceData);
				w.m_trainUnitSourceTractionEdit->setText(editedSourceTraction);
				QMetaObject::invokeMethod(w.m_trainUnitSourceTractionEdit, "editingFinished", Qt::DirectConnection);
				const SceneTrainUnit& editedUnit = w.m_sceneModel.trainUnits[originalCount];
				if (editedUnit.sourceDataFile != editedSourceData.toStdString()
					|| editedUnit.sourceTractionFile != editedSourceTraction.toStdString())
					facetFailure(facetOk, "train unit", "source reference edits did not commit both fields");
			}

			const double physicalValues[] = {
				101.1234567890123, 51.0000000012345, 2.0, 40.1234567890123,
				1.2000000012345, 9.5000000012345, 0.0300000012345, 0.4000000012345,
				84.1234567890123};
			for (int field = 0; field < 9; ++field) {
				if (w.m_trainUnitPhysicalEdits[static_cast<size_t>(field)])
					w.m_trainUnitPhysicalEdits[static_cast<size_t>(field)]->setValue(physicalValues[field]);
				w.m_trainUnitPanel->commitPhysical(field);
			}
			const SceneTrainPhysical& committedPhysical = w.m_sceneModel.trainUnits[originalCount].physical;
			if (committedPhysical.mass_of_traction_unit_kg != physicalValues[0])
				facetFailure(facetOk, "train unit", "traction-unit mass did not commit the requested value");
			if (committedPhysical.mass_of_a_wagon_kg != physicalValues[1])
				facetFailure(facetOk, "train unit", "wagon mass did not commit the requested value");
			if (committedPhysical.number_of_wagons != physicalValues[2])
				facetFailure(facetOk, "train unit", "wagon count did not commit the requested value");
			if (committedPhysical.max_speed_ms != physicalValues[3])
				facetFailure(facetOk, "train unit", "maximum speed did not commit the requested value");
			if (committedPhysical.max_deceleration_ms2 != physicalValues[4])
				facetFailure(facetOk, "train unit", "maximum deceleration did not commit the requested value");
			if (committedPhysical.frontal_area_m2 != physicalValues[5])
				facetFailure(facetOk, "train unit", "frontal area did not commit the requested value");
			if (committedPhysical.resistance_coefficient != physicalValues[6])
				facetFailure(facetOk, "train unit", "resistance coefficient did not commit the requested value");
			if (committedPhysical.jerk_ms3 != physicalValues[7])
				facetFailure(facetOk, "train unit", "jerk did not commit the requested value");
			if (committedPhysical.length_m != physicalValues[8])
				facetFailure(facetOk, "train unit", "length did not commit the requested value");
			const int initialCurveRows = static_cast<int>(w.m_sceneModel.trainUnits[originalCount].tractionCurve.size());
			w.m_trainUnitPanel->addTractionRow();
			if (static_cast<int>(w.m_sceneModel.trainUnits[originalCount].tractionCurve.size()) != initialCurveRows + 1) {
				facetFailure(facetOk, "train unit", "traction row add did not apply");
			} else if (w.m_trainUnitTractionTable) {
				const int row = w.m_trainUnitTractionTable->rowCount() - 1;
				w.m_trainUnitTractionTable->setCurrentCell(row, 0);
				const std::array<double, 5> tractionValues = {{0.123456789012345, 10.1234567890123, 100.1234567890123,
					2.1234567890123, 0.5000000012345}};
				for (int column = 0; column < 5; ++column) {
					auto* edit = qobject_cast<QDoubleSpinBox*>(w.m_trainUnitTractionTable->cellWidget(row, column));
					if (!edit) {
						facetFailure(facetOk, "train unit", "traction numeric editor unavailable");
						continue;
					}
					edit->setValue(tractionValues[column]);
				}
				w.m_trainUnitPanel->addTractionRow();
				if (static_cast<int>(w.m_sceneModel.trainUnits[originalCount].tractionCurve.size()) != initialCurveRows + 2)
					facetFailure(facetOk, "train unit", "second traction row add did not apply");
				else {
					const int secondRow = w.m_trainUnitTractionTable->rowCount() - 1;
					auto* secondRowEdit = qobject_cast<QDoubleSpinBox*>(w.m_trainUnitTractionTable->cellWidget(secondRow, 0));
					if (!secondRowEdit) {
						facetFailure(facetOk, "train unit", "second traction numeric editor unavailable");
					} else {
						secondRowEdit->setValue(10.9876543210987);
						w.m_trainUnitTractionTable->setCurrentCell(row, 0);
						secondRowEdit->setFocus();
						QMetaObject::invokeMethod(secondRowEdit, "editingFinished", Qt::DirectConnection);
					}
					w.m_trainUnitPanel->removeTractionRow();
					if (static_cast<int>(w.m_sceneModel.trainUnits[originalCount].tractionCurve.size()) != initialCurveRows + 1)
						facetFailure(facetOk, "train unit", "traction row delete did not apply");
					else if (w.m_sceneModel.trainUnits[originalCount].tractionCurve[initialCurveRows] != tractionValues)
						facetFailure(facetOk, "train unit", "editing a non-current traction row deleted the wrong row");
				}
			}

			w.m_trainUnitListWidget->setCurrentRow(originalCount);
			w.m_trainUnitPanel->duplicateUnit();
			if (w.m_trainUnitListWidget->count() != originalCount + 2) {
				facetFailure(facetOk, "train unit", "duplicate did not apply");
			} else {
				const int duplicateRow = originalCount + 1;
				w.m_trainUnitListWidget->setCurrentRow(duplicateRow);
				const std::string duplicateId = w.m_sceneModel.trainUnits[duplicateRow].id;
				w.m_compositionListWidget->setCurrentRow(0);
				acceptUnitChoice(QString::fromStdString(duplicateId));
				w.m_compositionPanel->addUnit();
				QApplication::processEvents();
				const int referencedCount = w.m_trainUnitListWidget->count();
				w.m_trainUnitPanel->deleteUnit();
				const bool duplicateStillPresent = std::any_of(w.m_sceneModel.trainUnits.begin(),
					w.m_sceneModel.trainUnits.end(), [&](const SceneTrainUnit& unit) { return unit.id == duplicateId; });
				if (w.m_trainUnitListWidget->count() != referencedCount || !duplicateStillPresent)
					facetFailure(facetOk, "train unit", "referenced-unit delete was not refused");
				w.m_compositionListWidget->setCurrentRow(0);
				for (int unitRow = 0; unitRow < w.m_compositionUnitsListWidget->count(); ++unitRow) {
					if (w.m_compositionUnitsListWidget->item(unitRow)->text().toStdString() == duplicateId) {
						w.m_compositionUnitsListWidget->setCurrentRow(unitRow);
						w.m_compositionPanel->removeUnit();
						break;
					}
				}
				acceptConfirmation();
				w.m_trainUnitPanel->deleteUnit();
				if (w.m_trainUnitListWidget->count() != originalCount + 1
					|| std::any_of(w.m_sceneModel.trainUnits.begin(), w.m_sceneModel.trainUnits.end(),
						[&](const SceneTrainUnit& unit) { return unit.id == duplicateId; }))
					facetFailure(facetOk, "train unit", "unreferenced-unit delete did not apply");
			}
		}
		if (!w.m_sceneModel.trainUnits.empty()) {
			w.m_trainUnitListWidget->setCurrentRow(0);
			if (!w.m_trainUnitSourceDataEdit || w.m_trainUnitSourceDataEdit->text().toStdString() != w.m_sceneModel.trainUnits.front().sourceDataFile
				|| !w.m_trainUnitSourceTractionEdit || w.m_trainUnitSourceTractionEdit->text().toStdString() != w.m_sceneModel.trainUnits.front().sourceTractionFile)
				facetFailure(facetOk, "train unit", "source references are not visible and unchanged");
		}
		expectedCompositions = w.m_sceneModel.compositions;
		if (facetOk)
			std::fprintf(stdout, "E2E_EDITOR_TRAIN_UNIT_OK\n");
	}

	if (!w.m_sceneModel.trainUnits.empty()) {
		bool facetOk = true;
		w.m_trainUnitListWidget->setCurrentRow(0);
		sourceLinkUnitId = w.m_sceneModel.trainUnits.front().id;
		sourcePhysicalPath = sourceLinkFixture.filePath("physical.txt");
		sourceTractionPath = sourceLinkFixture.filePath("traction.txt");
		const auto writeSource = [&](const QString& path, const QByteArray& bytes) {
			QSaveFile file(path);
			if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
				facetFailure(facetOk, "source links", "temporary source write failed");
		};
		const auto settle = []() {
			QEventLoop loop;
			QTimer::singleShot(1500, &loop, &QEventLoop::quit);
			loop.exec();
		};
		const auto answerReload = [&w](const QString& choice, bool* prompted = nullptr) {
			QTimer::singleShot(25, &w, [choice, prompted]() {
				for (QWidget* widget : QApplication::topLevelWidgets()) {
					if (auto* dialog = qobject_cast<QMessageBox*>(widget)) {
						if (prompted) *prompted = true;
						for (QAbstractButton* button : dialog->buttons())
							if (button->text() == choice) button->click();
					}
				}
			});
		};
		TrainUnitSourceLinker::Link link;
		link.dataPath = sourcePhysicalPath;
		link.tractionPath = sourceTractionPath;
		link.acceptedPhysical = w.m_sceneModel.trainUnits.front().physical;
		link.acceptedTraction = w.m_sceneModel.trainUnits.front().tractionCurve;
		w.m_trainUnitSourceLinker->m_links[sourceLinkUnitId] = link;
		w.m_trainUnitSourceLinker->refreshWatches();
		writeSource(sourcePhysicalPath, "151000 0 0 36.111111111111 0.75 1.45 0.004 0.75 71\n");
		writeSource(sourceTractionPath, "0 40 210000 0 0\n");
		settle();
		if (w.m_sceneModel.trainUnits.front().physical.length_m != 71.0
			|| w.m_sceneModel.trainUnits.front().tractionCurve != std::vector<std::array<double, 5>>{{0, 40, 210000, 0, 0}})
			facetFailure(facetOk, "source links", "complete file changes did not apply");
		const quint64 acceptedRevision = w.m_sceneRevision;
		w.m_trainUnitSourceLinker->m_links.at(sourceLinkUnitId).tractionDeferred = true;
		writeSource(sourceTractionPath, "0 40 210000 0 0\n");
		bool unchangedPrompted = false;
		answerReload("Keep local", &unchangedPrompted);
		w.m_trainUnitSourceLinker->m_pendingPaths.insert(sourceTractionPath);
		w.m_trainUnitSourceLinker->processChanges();
		settle();
		if (w.m_sceneRevision != acceptedRevision || unchangedPrompted
			|| w.m_trainUnitSourceLinker->m_links.at(sourceLinkUnitId).tractionDeferred)
			facetFailure(facetOk, "source links", "unchanged deferred save prompted or dirtied the scene");
		writeSource(sourceTractionPath, "0 40 220000 0 0\npartial\n");
		settle();
		if (w.m_sceneModel.trainUnits.front().tractionCurve.front()[2] != 210000
			|| !w.m_trainUnitSourceLinker->m_links.at(sourceLinkUnitId).tractionStatus.startsWith("Rejected"))
			facetFailure(facetOk, "source links", "malformed file replaced accepted data");
		QFile::remove(sourceTractionPath);
		settle();
		writeSource(sourceTractionPath, "0 40 220000 0 0\n");
		settle();
		if (w.m_sceneModel.trainUnits.front().tractionCurve.front()[2] != 220000
			|| !w.m_trainUnitSourceLinker->watchedFiles().contains(sourceTractionPath))
			facetFailure(facetOk, "source links", "deletion/recreation did not recover the watch");
		// The focused editor must commit before comparing with the accepted source.
		w.m_trainUnitDock->show();
		w.m_trainUnitDock->raise();
		w.activateWindow();
		w.m_trainUnitPhysicalEdits[8]->setFocus();
		QApplication::processEvents();
		if (!w.m_trainUnitPhysicalEdits[8]->hasFocus())
			facetFailure(facetOk, "source links", "length editor did not receive focus");
		w.m_trainUnitPhysicalEdits[8]->findChild<QLineEdit*>()->setText("73");
		writeSource(sourcePhysicalPath, "151000 0 0 36.111111111111 0.75 1.45 0.004 0.75 72\n");
		answerReload("Keep local");
		w.m_trainUnitSourceLinker->processFile(sourcePhysicalPath, false);
		if (w.m_sceneModel.trainUnits.front().physical.length_m != 73.0)
			facetFailure(facetOk, "source links", "focused local edit was overwritten");
		const quint64 keptRevision = w.m_sceneRevision;
		settle();
		if (w.m_sceneRevision != keptRevision)
			facetFailure(facetOk, "source links", "rejected content was reapplied");
		answerReload("Reload");
		w.m_trainUnitSourceLinker->retry(sourceLinkUnitId, false);
		w.m_trainUnitSourceLinker->processChanges();
		if (w.m_sceneModel.trainUnits.front().physical.length_m != 72.0)
			facetFailure(facetOk, "source links", "explicit retry did not reload");
		// A duplicate shares values, not the authority to watch an external file.
		w.m_trainUnitPanel->duplicateUnit();
		const std::string duplicateId = w.m_sceneModel.trainUnits[1].id;
		if (w.m_trainUnitSourceLinker->m_links.count(duplicateId))
			facetFailure(facetOk, "source links", "duplicate inherited a live link");
		w.m_trainUnitSourceLinker->m_links[duplicateId] = w.m_trainUnitSourceLinker->m_links.at(sourceLinkUnitId);
		writeSource(sourceTractionPath, "0 40 230000 0 0\n");
		settle();
		if (w.m_sceneModel.trainUnits[0].tractionCurve.front()[2] != 230000
			|| w.m_sceneModel.trainUnits[1].tractionCurve.front()[2] != 230000)
			facetFailure(facetOk, "source links",
				QString("shared source values %1 / %2; status %3")
					.arg(w.m_sceneModel.trainUnits[0].tractionCurve.front()[2])
					.arg(w.m_sceneModel.trainUnits[1].tractionCurve.front()[2])
					.arg(w.m_trainUnitSourceLinker->m_links.at(duplicateId).tractionStatus));
		w.m_trainUnitIdEdit->setText(QString::fromStdString(duplicateId + "_renamed"));
		w.m_trainUnitPanel->commitIdEdit();
		if (w.m_trainUnitSourceLinker->m_links.count(duplicateId)
			|| !w.m_trainUnitSourceLinker->m_links.count(duplicateId + "_renamed"))
			facetFailure(facetOk, "source links", "rename did not migrate the link");
		acceptConfirmation();
		w.m_trainUnitPanel->deleteUnit();
		if (w.m_trainUnitSourceLinker->m_links.size() != 1
			|| !w.m_trainUnitSourceLinker->watchedFiles().contains(sourceTractionPath))
			facetFailure(facetOk, "source links", "deleting one shared-file unit removed the remaining watch");
		w.m_trainUnitListWidget->setCurrentRow(0);
		w.m_trainUnitSourceLinker->unlink(sourceLinkUnitId, false);
		w.m_trainUnitSourceLinker->unlink(sourceLinkUnitId, true);
		if (!w.m_trainUnitSourceLinker->m_links.empty() || !w.m_trainUnitSourceLinker->watchedFiles().isEmpty()
			|| !w.m_trainUnitSourceLinker->watchedDirectories().isEmpty())
			facetFailure(facetOk, "source links", "unlink retained watches");
		QFile::remove(sourcePhysicalPath);
		QFile::remove(sourceTractionPath);
		if (facetOk)
			std::fprintf(stdout, "E2E_EDITOR_SOURCE_LINKS_OK\n");
	}

	std::string editedCompositionId;
	if (!w.m_sceneLoaded || !w.m_compositionListWidget || expectedCompositions.empty()) {
		bool facetOk = false;
		facetFailure(facetOk, "composition", "scene or composition list unavailable");
	} else {
		bool facetOk = true;
		const int originalCount = w.m_compositionListWidget->count();
		int assignedCompositionRow = -1;
		for (int row = 0; row < static_cast<int>(w.m_sceneModel.compositions.size()); ++row) {
			const std::string& compositionId = w.m_sceneModel.compositions[static_cast<std::size_t>(row)].id;
			if (std::any_of(w.m_sceneModel.services.begin(), w.m_sceneModel.services.end(),
					[&compositionId](const SceneService& service) { return service.composition == compositionId; })) {
				assignedCompositionRow = row;
				break;
			}
		}
		if (assignedCompositionRow < 0) {
			facetFailure(facetOk, "composition", "no service-assigned composition available for rename coverage");
		} else {
			w.m_compositionListWidget->setCurrentRow(assignedCompositionRow);
			// the selected unit surfaces its source files and a plottable traction curve
			if (w.m_compositionUnitsListWidget && w.m_compositionUnitsListWidget->count() > 0 && w.m_compositionUnitsListWidget->item(0)) {
				w.m_compositionUnitsListWidget->setCurrentRow(0);
				const SceneTrainUnit* tractionUnit =
					sceneTrainUnitById(w.m_sceneModel, w.m_compositionUnitsListWidget->item(0)->text().toStdString());
				if (tractionUnit && !tractionUnit->tractionCurve.empty()) {
					if (!w.m_plotTractionButton || !w.m_plotTractionButton->isEnabled())
						facetFailure(facetOk, "composition", "traction plot disabled for a unit with a curve");
					if (sampleTractionCurve(tractionUnit->tractionCurve).empty())
						facetFailure(facetOk, "composition", "traction curve produced no plot samples");
					if (w.m_compositionUnitSourceDataLabel && w.m_compositionUnitSourceDataLabel->text().isEmpty())
						facetFailure(facetOk, "composition", "unit source data label empty");
				}
			}
			w.m_compositionPanel->duplicateComposition();
			if (w.m_compositionListWidget->count() != originalCount + 1) {
				facetFailure(facetOk, "composition", "duplicate did not apply");
			} else {
				const std::string originalAssignedCompositionId =
					w.m_sceneModel.compositions[static_cast<std::size_t>(assignedCompositionRow)].id;
				const std::string duplicateCompositionId =
					w.m_sceneModel.compositions[static_cast<std::size_t>(assignedCompositionRow + 1)].id;
				w.m_compositionListWidget->setCurrentRow(assignedCompositionRow);
				if (!w.m_compositionIdEdit) {
					facetFailure(facetOk, "composition", "id editor unavailable");
				} else {
					w.m_compositionIdEdit->setText(QString());
					w.m_compositionPanel->commitIdEdit();
					const bool emptyRejected = w.m_sceneModel.compositions[static_cast<std::size_t>(assignedCompositionRow)].id
							== originalAssignedCompositionId
						&& w.m_compositionIdEdit->text() == QString::fromStdString(originalAssignedCompositionId)
						&& w.m_compositionListWidget->currentRow() == assignedCompositionRow;
					w.m_compositionIdEdit->setText(QString::fromStdString(duplicateCompositionId));
					w.m_compositionPanel->commitIdEdit();
					const bool duplicateRejected = w.m_sceneModel.compositions[static_cast<std::size_t>(assignedCompositionRow)].id
							== originalAssignedCompositionId
						&& w.m_compositionIdEdit->text() == QString::fromStdString(originalAssignedCompositionId)
						&& w.m_compositionListWidget->currentRow() == assignedCompositionRow;
					if (!emptyRejected || !duplicateRejected)
						facetFailure(facetOk, "composition", "empty or duplicate ID was not rejected with the prior text restored");

					editedCompositionId = sceneUniqueCompositionId(w.m_sceneModel, "e2e_composition");
					w.m_compositionIdEdit->setText(QString::fromStdString(editedCompositionId));
					w.m_compositionPanel->commitIdEdit();
					int migratedServiceCount = 0;
					int staleServiceCount = 0;
					for (const auto& service : w.m_sceneModel.services) {
						if (service.composition == editedCompositionId)
							++migratedServiceCount;
						if (service.composition == originalAssignedCompositionId)
							++staleServiceCount;
					}
					if (w.m_sceneModel.compositions[static_cast<std::size_t>(assignedCompositionRow)].id != editedCompositionId
						|| migratedServiceCount <= 0 || staleServiceCount != 0
						|| w.m_compositionListWidget->currentRow() != assignedCompositionRow
						|| !w.m_serviceCompositionCombo
						|| w.m_serviceCompositionCombo->findText(QString::fromStdString(editedCompositionId)) < 0)
						facetFailure(facetOk, "composition", "id edit did not apply");
				}
			}
			const int referencedCompositionCount = w.m_compositionListWidget->count();
			w.m_compositionListWidget->setCurrentRow(assignedCompositionRow);
			w.m_compositionPanel->deleteComposition();
			if (w.m_compositionListWidget->count() != referencedCompositionCount
				|| !std::any_of(w.m_sceneModel.compositions.begin(), w.m_sceneModel.compositions.end(),
					[&](const SceneComposition& composition) {
						return composition.id == editedCompositionId;
					}))
				facetFailure(facetOk, "composition", "service-referenced composition delete was not refused");
			w.m_compositionPanel->addComposition();
			const std::string temporaryCompositionId = w.m_sceneModel.compositions.back().id;
			if (w.m_compositionListWidget->count() != originalCount + 2
				|| !w.m_serviceCompositionCombo
				|| w.m_serviceCompositionCombo->findText(
					   QString::fromStdString(temporaryCompositionId))
					< 0) {
				facetFailure(facetOk, "composition", "add did not refresh the service selector");
			} else {
				acceptConfirmation();
				w.m_compositionPanel->deleteComposition();
				if (w.m_compositionListWidget->count() != originalCount + 1
					|| w.m_serviceCompositionCombo->findText(
						   QString::fromStdString(temporaryCompositionId))
						>= 0)
					facetFailure(facetOk, "composition", "delete did not refresh the service selector");
			}
			int editedRow = -1;
			for (int row = 0; row < static_cast<int>(w.m_sceneModel.compositions.size()); ++row) {
				if (w.m_sceneModel.compositions[row].id == editedCompositionId) {
					editedRow = row;
					break;
				}
			}
			if (editedRow < 0 || w.m_sceneModel.compositions[editedRow].units != expectedCompositions[static_cast<std::size_t>(assignedCompositionRow)].units)
				facetFailure(facetOk, "composition", "edited composition was not retained");
			if (facetOk)
				std::fprintf(stdout, "E2E_EDITOR_COMPOSITION_OK\n");
		}
	}

	std::string editedServiceId;
	if (!w.m_sceneLoaded || !w.m_serviceListWidget || expectedServices.empty() || editedCompositionId.empty()) {
		bool facetOk = false;
		facetFailure(facetOk, "service", "scene or service setup unavailable");
	} else {
		bool facetOk = true;
		const double precisePerformancePercent = 99.12345678901234;
		const double preciseMaximumSpeedKmh = 876.5432109876543;
		const int originalCount = w.m_serviceListWidget->count();
		w.m_serviceListWidget->setCurrentRow(0);
		const std::string unknownCategory = "Regional heritage / custom";
		w.m_sceneModel.services[0].category = unknownCategory;
		expectedServices[0].category = unknownCategory;
		w.m_servicePanel->updateDetail();
		if (!w.m_serviceCategoryCombo || w.m_serviceCategoryCombo->currentData().toString().toStdString() != unknownCategory
			|| !w.m_serviceCategoryCombo->currentText().startsWith("Unknown: "))
			facetFailure(facetOk, "service", "unknown category is not preserved in the chooser");
		const std::string storedColor = "#3c8dd2";
		w.m_sceneModel.services[0].visualizationColor = storedColor;
		expectedServices[0].visualizationColor = storedColor;
		w.m_servicePanel->updateDetail();
		if (!w.m_serviceColorButton || !w.m_serviceColorButton->isEnabled()
			|| w.m_serviceColorButton->valueText() != QString::fromStdString(storedColor)
			|| w.m_serviceColorButton->swatchColor() != QColor(0x3C, 0x8D, 0xD2))
			facetFailure(facetOk, "service colour", "stored colour is not shown in the editor");
		const auto originalService = w.m_sceneModel.services[0];
		const auto sourceRoute = std::find_if(w.m_sceneModel.routes.begin(), w.m_sceneModel.routes.end(),
			[&](const SceneRoute& route) { return route.id == originalService.route; });
		if (sourceRoute != w.m_sceneModel.routes.end()) {
			SceneRoute duplicateRoute = *sourceRoute;
			duplicateRoute.id += "-same-description";
			w.m_sceneModel.routes.push_back(duplicateRoute);
			w.m_servicePanel->updateDetail();
			w.m_serviceRouteCombo->setCurrentIndex(w.m_serviceRouteCombo->findData(QString::fromStdString(duplicateRoute.id)));
			if (w.m_sceneModel.services[0].route != duplicateRoute.id
				|| !w.m_serviceRouteCombo->toolTip().contains("not scheduled calls"))
				facetFailure(facetOk, "service routes", "descriptive route selection lost canonical identity or context");
			w.m_sceneModel.services[0].route = "missing-route";
			w.m_servicePanel->updateDetail();
			if (w.m_serviceRouteCombo->currentData().toString() != "missing-route"
				|| !sameStop(w.m_sceneModel.services[0].stops.front(), originalService.stops.front())
				|| !w.m_stopTableWidget->item(0, 0)->toolTip().contains("unresolved"))
				facetFailure(facetOk, "service routes", "missing route or invalid stop was silently replaced");
			w.m_sceneModel.services[0] = originalService;
			w.m_sceneModel.routes.pop_back();
			w.m_servicePanel->updateDetail();
		}
		w.m_servicePanel->duplicateService();
		if (w.m_serviceListWidget->count() != originalCount + 1) {
			facetFailure(facetOk, "service", "duplicate did not apply");
		} else {
			w.m_serviceListWidget->setCurrentRow(1);
			SceneService categoryExpected = w.m_sceneModel.services[1];
			if (categoryExpected.category != unknownCategory)
				facetFailure(facetOk, "service", "duplicate lost category");
			if (categoryExpected.visualizationColor != storedColor)
				facetFailure(facetOk, "service colour", "duplicate lost the colour");
			if (w.m_serviceCategoryCombo) {
				for (const QString& category : {QString(), QStringLiteral("Intercity"), QStringLiteral("Regional"),
						 QStringLiteral("High speed/international"), QStringLiteral("Freight"),
						 QStringLiteral("Metro/urban"), QStringLiteral("Suburban"), QString::fromStdString(unknownCategory)}) {
					const int index = w.m_serviceCategoryCombo->findData(category);
					w.m_serviceCategoryCombo->setCurrentIndex(index);
					categoryExpected.category = category.toStdString();
					if (index < 0 || !sameServices({categoryExpected}, {w.m_sceneModel.services[1]}))
						facetFailure(facetOk, "service", "category selection changed other service settings");
				}
			}
			if (w.m_serviceColorButton) {
				auto* defaultColorButton = w.m_serviceColorButton->findChild<QPushButton*>("serviceColorDefaultButton");
				const bool dirtyBeforeColor = w.m_sceneDirty;
				// an edit changes the colour and marks the scene modified; an equal value changes nothing
				const auto colorStep = [&](const char* what, const std::function<void()>& action,
										   const std::string& expectedColor) {
					w.m_sceneDirty = false;
					const quint64 revisionBefore = w.m_sceneRevision;
					const SceneService before = w.m_sceneModel.services[1];
					action();
					SceneService expected = before;
					expected.visualizationColor = expectedColor;
					const bool changed = expectedColor != before.visualizationColor;
					if (!sameServices({expected}, {w.m_sceneModel.services[1]}) || w.m_sceneDirty != changed
						|| (w.m_sceneRevision != revisionBefore) != changed)
						facetFailure(facetOk, "service colour",
							QString("%1 gave the wrong model or dirty state").arg(QLatin1String(what)));
				};
				// the comparison used below sees a colour that differs alone
				SceneService withColor = w.m_sceneModel.services[1];
				withColor.visualizationColor = "#112233";
				SceneService withoutColor = withColor;
				withoutColor.visualizationColor.clear();
				if (sameServices({withColor}, {withoutColor}))
					facetFailure(facetOk, "service colour", "service comparison ignores the colour");
				if (!defaultColorButton)
					facetFailure(facetOk, "service colour", "Default button unavailable");
				else {
					colorStep("choosing a colour", [&]() { w.m_serviceColorButton->chooseColor(QColor(0xAB, 0xCD, 0xEF)); }, "#abcdef");
					colorStep("choosing the same colour again", [&]() { w.m_serviceColorButton->chooseColor(QColor(0xAB, 0xCD, 0xEF)); }, "#abcdef");
					colorStep("Default", [&]() { defaultColorButton->click(); }, "");
					colorStep("Default without a colour", [&]() { defaultColorButton->click(); }, "");
					if (w.m_serviceColorButton->valueText() != "Default")
						facetFailure(facetOk, "service colour", "Default did not show the default look");
					// a stored text that is not a colour is shown and kept until changed
					w.m_sceneModel.services[1].visualizationColor = "not-a-colour";
					w.m_servicePanel->updateDetail();
					w.m_serviceListWidget->setCurrentRow(0);
					w.m_serviceListWidget->setCurrentRow(1);
					if (w.m_serviceColorButton->valueText() != "Invalid: not-a-colour"
						|| w.m_sceneModel.services[1].visualizationColor != "not-a-colour")
						facetFailure(facetOk, "service colour", "invalid stored colour is not shown as invalid and kept");
					colorStep("choosing a colour over an invalid text", [&]() { w.m_serviceColorButton->chooseColor(QColor(0x3C, 0x8D, 0xD2)); }, storedColor);
					w.m_sceneModel.services[1].visualizationColor = "not-a-colour";
					w.m_servicePanel->updateDetail();
					colorStep("Default over an invalid text", [&]() { defaultColorButton->click(); }, "");
					// the colour stays on the service for the save and reload below
					colorStep("choosing the final colour", [&]() { w.m_serviceColorButton->chooseColor(QColor(0x3C, 0x8D, 0xD2)); }, storedColor);
					if (w.m_serviceColorButton->valueText() != QString::fromStdString(storedColor))
						facetFailure(facetOk, "service colour", "chosen colour is not shown");
				}
				w.m_sceneDirty = dirtyBeforeColor;
			} else {
				facetFailure(facetOk, "service colour", "colour control unavailable");
			}
			const std::string oldServiceId = w.m_sceneModel.services[1].id;
			int referenceScenarioRow = -1;
			std::size_t temporaryDelayCount = 0;
			int temporaryDelayRow = -1;
			std::size_t temporaryIncidentCount = 0;
			for (int scenarioRow = 0; scenarioRow < static_cast<int>(w.m_sceneModel.scenarios.size()); ++scenarioRow) {
				if (w.m_sceneModel.scenarios[static_cast<std::size_t>(scenarioRow)].id == w.m_runSelection.scenarioId()) {
					referenceScenarioRow = scenarioRow;
					break;
				}
			}
			if (referenceScenarioRow >= 0) {
				SceneScenario& referenceScenario = w.m_sceneModel.scenarios[static_cast<std::size_t>(referenceScenarioRow)];
				if (w.m_addEntranceDelayButton && w.m_entranceDelayListWidget && w.m_entranceDelayServiceCombo) {
					w.m_addEntranceDelayButton->click();
					QApplication::processEvents();
					temporaryDelayRow = w.m_entranceDelayListWidget->count() - 1;
					const int serviceIndex = w.m_entranceDelayServiceCombo->findData(QString::fromStdString(oldServiceId));
					if (temporaryDelayRow < 0 || serviceIndex < 0) {
						facetFailure(facetOk, "service", "public entrance-delay controls did not expose the duplicated service");
					} else {
						w.m_entranceDelayListWidget->setCurrentRow(temporaryDelayRow);
						w.m_entranceDelayServiceCombo->setCurrentIndex(serviceIndex);
						QApplication::processEvents();
						if (!referenceScenario.entranceDelays.empty()
							&& referenceScenario.entranceDelays.back().serviceId == oldServiceId)
							++temporaryDelayCount;
					}
				} else {
					facetFailure(facetOk, "service", "public entrance-delay controls unavailable for rename coverage");
				}
				SceneIncident incident;
				incident.id = sceneUniqueIncidentId(w.m_sceneModel, "e2e_service_rename_incident");
				incident.type = "train_breakdown";
				incident.target = oldServiceId;
				incident.endSeconds = 1.0;
				referenceScenario.incidents.push_back(incident);
				++temporaryIncidentCount;
			}
			w.m_runSelection.setExcluded(SceneServiceOccurrence{oldServiceId, 1}, true);
			editedServiceId = sceneUniqueServiceId(w.m_sceneModel, "e2e_service");
			if (!w.m_serviceIdEdit) {
				facetFailure(facetOk, "service", "id editor unavailable");
			} else {
				const std::string duplicateServiceId = w.m_sceneModel.services.front().id;
				const auto referencesStillOld = [&]() {
					if (referenceScenarioRow < 0)
						return true;
					const SceneScenario& scenario = w.m_sceneModel.scenarios[static_cast<std::size_t>(referenceScenarioRow)];
					const bool delayStillOld = temporaryDelayCount == 0
						|| (!scenario.entranceDelays.empty() && scenario.entranceDelays.back().serviceId == oldServiceId);
					const bool incidentStillOld = temporaryIncidentCount == 0
						|| (!scenario.incidents.empty() && scenario.incidents.back().target == oldServiceId);
					return delayStillOld && incidentStillOld
						&& w.m_runSelection.isExcluded(SceneServiceOccurrence{oldServiceId, 1});
				};
				w.m_serviceIdEdit->setText(QString());
				w.m_servicePanel->commitIdEdit();
				const bool emptyRejected = w.m_sceneModel.services[1].id == oldServiceId
					&& w.m_serviceIdEdit->text() == QString::fromStdString(oldServiceId)
					&& referencesStillOld();
				w.m_serviceIdEdit->setText(QString::fromStdString(duplicateServiceId));
				w.m_servicePanel->commitIdEdit();
				const bool duplicateRejected = w.m_sceneModel.services[1].id == oldServiceId
					&& w.m_serviceIdEdit->text() == QString::fromStdString(oldServiceId)
					&& referencesStillOld();
				if (!emptyRejected || !duplicateRejected)
					facetFailure(facetOk, "service", "empty or duplicate ID rename changed canonical references");
				w.m_serviceIdEdit->setText(QString::fromStdString(editedServiceId));
				w.m_servicePanel->commitIdEdit();
				if (w.m_sceneModel.services.size() <= 1 || w.m_sceneModel.services[1].id != editedServiceId)
					facetFailure(facetOk, "service", "id edit did not apply");
			}
			bool referencesUpdated = true;
			if (referenceScenarioRow >= 0) {
				const SceneScenario& referenceScenario = w.m_sceneModel.scenarios[static_cast<std::size_t>(referenceScenarioRow)];
				if (temporaryDelayCount > 0)
					referencesUpdated = referencesUpdated && !referenceScenario.entranceDelays.empty()
						&& referenceScenario.entranceDelays.back().serviceId == editedServiceId;
				if (temporaryIncidentCount > 0)
					referencesUpdated = referencesUpdated && !referenceScenario.incidents.empty()
						&& referenceScenario.incidents.back().target == editedServiceId;
			}
			if (!w.m_runSelection.isExcluded(SceneServiceOccurrence{editedServiceId, 1}))
				referencesUpdated = false;
			if (!referencesUpdated)
				facetFailure(facetOk, "service", "service ID rename did not update canonical references or occurrence exclusions");
			w.m_serviceListWidget->setCurrentRow(1);
			const int referencedServiceCount = w.m_serviceListWidget->count();
			w.m_servicePanel->deleteService();
			if (w.m_serviceListWidget->count() != referencedServiceCount
				|| !std::any_of(w.m_sceneModel.services.begin(), w.m_sceneModel.services.end(),
					[&](const SceneService& service) { return service.id == editedServiceId; }))
				facetFailure(facetOk, "service", "operation-referenced service delete was not refused");
			if (referenceScenarioRow >= 0) {
				SceneScenario& referenceScenario = w.m_sceneModel.scenarios[static_cast<std::size_t>(referenceScenarioRow)];
				if (temporaryDelayCount > 0 && w.m_entranceDelayListWidget && w.m_deleteEntranceDelayButton
					&& temporaryDelayRow >= 0 && temporaryDelayRow < w.m_entranceDelayListWidget->count()) {
					w.m_entranceDelayListWidget->setCurrentRow(temporaryDelayRow);
					acceptConfirmation();
					w.m_deleteEntranceDelayButton->click();
					QApplication::processEvents();
				}
				if (temporaryIncidentCount > 0 && !referenceScenario.incidents.empty())
					referenceScenario.incidents.pop_back();
			}
			w.m_servicePanel->selectAllOccurrences();
			w.m_servicePanel->commitComposition(QString::fromStdString(editedCompositionId));
			w.m_serviceRouteCombo->setCurrentIndex(w.m_serviceRouteCombo->findData(QString::fromStdString(expectedServices[0].route)));
			if (w.m_serviceOperatingCodeEdit) {
				w.m_serviceOperatingCodeEdit->setText("1723");
				QMetaObject::invokeMethod(w.m_serviceOperatingCodeEdit, "editingFinished", Qt::DirectConnection);
			}
			w.m_servicePanel->commitHasEntryTime(false);
			w.m_servicePanel->commitHasEntryTime(true);
			if (w.m_serviceEntryTimeSecondsEdit)
				w.m_serviceEntryTimeSecondsEdit->setText("600");
			w.m_servicePanel->commitEntryTimeSeconds();
			w.m_servicePanel->commitHasRepeat(true);
			if (w.m_serviceHeadwaySecondsEdit)
				w.m_serviceHeadwaySecondsEdit->setText("1800");
			w.m_servicePanel->commitHeadwaySeconds();
			if (w.m_serviceHasRepeatCountCheck)
				w.m_serviceHasRepeatCountCheck->setChecked(true);
			if (w.m_serviceRepeatCountEdit) {
				w.m_serviceRepeatCountEdit->setText("3");
				QMetaObject::invokeMethod(w.m_serviceRepeatCountEdit, "editingFinished", Qt::DirectConnection);
			}
			// Typed digits change nothing until editing ends. Return applies the typed value as one scene change,
			// and ServicePanel::commitPendingEditors applies a text that is still being typed.
			const bool dirtyBeforeTyping = w.m_sceneDirty;
			const QString typingFailure = [&]() -> QString {
				if (!w.m_servicePerformancePercentEdit || !w.m_servicePerformancePercentEdit->isEnabled())
					return QStringLiteral("running performance editor unavailable");
				QLineEdit* performanceField = w.m_servicePerformancePercentEdit->findChild<QLineEdit*>();
				if (!performanceField)
					return QStringLiteral("running performance editor has no text field");
				if (w.m_serviceListWidget->currentRow() != 1 || w.m_sceneModel.services.size() < 2)
					return QStringLiteral("running performance editor is not showing service row 1");
				const double valueBeforeTyping = w.m_sceneModel.services[1].performancePercent;
				if (w.m_servicePerformancePercentEdit->value() != valueBeforeTyping)
					return QString("running performance editor shows %1 but the service has %2")
						.arg(w.m_servicePerformancePercentEdit->value())
						.arg(valueBeforeTyping);
				if (valueBeforeTyping == 9.0 || valueBeforeTyping == 93.0 || valueBeforeTyping == 94.0)
					return QString("service running performance %1 equals a value typed by the check").arg(valueBeforeTyping);
				const auto typeDigits = [&](const QString& digits) -> QString {
					const double valueBefore = w.m_sceneModel.services[1].performancePercent;
					w.m_sceneDirty = false;
					const quint64 revisionBefore = w.m_sceneRevision;
					w.m_servicePerformancePercentEdit->selectAll();
					QString typedSoFar;
					for (const QChar digit : digits) {
						QKeyEvent keyPress(QEvent::KeyPress, digit.unicode(), Qt::NoModifier, QString(digit));
						QApplication::sendEvent(performanceField, &keyPress);
						typedSoFar += digit;
						const double appliedValue = w.m_sceneModel.services[1].performancePercent;
						if (appliedValue != valueBefore || w.m_sceneRevision != revisionBefore || w.m_sceneDirty)
							return QString("running performance was applied while typing \"%1\": the service has %2 instead of %3 "
										   "and the scene revision moved by %4")
								.arg(typedSoFar)
								.arg(appliedValue)
								.arg(valueBefore)
								.arg(w.m_sceneRevision - revisionBefore);
					}
					if (w.m_servicePerformancePercentEdit->text() != digits)
						return QString("running performance editor shows \"%1\" after typing \"%2\"")
							.arg(w.m_servicePerformancePercentEdit->text(), digits);
					return QString();
				};
				QString failure = typeDigits(QStringLiteral("94"));
				if (!failure.isEmpty())
					return failure;
				const quint64 revisionBeforeReturn = w.m_sceneRevision;
				QKeyEvent returnPress(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
				QKeyEvent returnRelease(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier);
				QApplication::sendEvent(w.m_servicePerformancePercentEdit, &returnPress);
				QApplication::sendEvent(w.m_servicePerformancePercentEdit, &returnRelease);
				if (w.m_sceneModel.services[1].performancePercent != 94.0 || w.m_sceneRevision != revisionBeforeReturn + 1
					|| !w.m_sceneDirty)
					return QString("Return did not apply the typed running performance once: the service has %1 instead of 94 "
								   "and the scene revision moved by %2")
						.arg(w.m_sceneModel.services[1].performancePercent)
						.arg(w.m_sceneRevision - revisionBeforeReturn);
				failure = typeDigits(QStringLiteral("93"));
				if (!failure.isEmpty())
					return failure;
				const quint64 revisionBeforeCommit = w.m_sceneRevision;
				w.m_servicePanel->commitPendingEditors();
				if (w.m_sceneModel.services[1].performancePercent != 93.0 || w.m_sceneRevision != revisionBeforeCommit + 1
					|| !w.m_sceneDirty)
					return QString("committing pending settings did not apply the typed running performance once: the service has %1 "
								   "instead of 93 and the scene revision moved by %2")
						.arg(w.m_sceneModel.services[1].performancePercent)
						.arg(w.m_sceneRevision - revisionBeforeCommit);
				if (w.m_servicePerformancePercentEdit->value() != 93.0 || w.m_servicePerformancePercentEdit->text() != "93")
					return QString("running performance editor shows \"%1\" after the pending commit of 93")
						.arg(w.m_servicePerformancePercentEdit->text());
				return QString();
			}();
			w.m_sceneDirty = dirtyBeforeTyping;
			if (!typingFailure.isEmpty())
				facetFailure(facetOk, "service", typingFailure);
			if (w.m_servicePerformancePercentEdit)
				w.m_servicePerformancePercentEdit->setValue(95.5);
			if (w.m_serviceHasMaximumSpeedCheck)
				w.m_serviceHasMaximumSpeedCheck->setChecked(true);
			if (w.m_serviceMaximumSpeedKmhEdit) {
				w.m_serviceMaximumSpeedKmhEdit->setValue(100.0);
				QMetaObject::invokeMethod(w.m_serviceMaximumSpeedKmhEdit, "editingFinished", Qt::DirectConnection);
			}
			if (w.m_serviceHasOperatingCodeStepCheck)
				w.m_serviceHasOperatingCodeStepCheck->setChecked(true);
			if (w.m_serviceOperatingCodeStepEdit) {
				w.m_serviceOperatingCodeStepEdit->setText("2");
				QMetaObject::invokeMethod(w.m_serviceOperatingCodeStepEdit, "editingFinished", Qt::DirectConnection);
			}
			w.m_servicePanel->refreshOccurrencePreview();
			bool occurrencePreviewOk = w.m_serviceOccurrenceTable != nullptr;
			int serviceOccurrences = 0;
			QStringList previewCodes;
			if (w.m_serviceOccurrenceTable) {
				for (int previewRow = 0; previewRow < w.m_serviceOccurrenceTable->rowCount(); ++previewRow) {
					QTableWidgetItem* include = w.m_serviceOccurrenceTable->item(previewRow, 0);
					if (!include || include->data(Qt::UserRole).toString().toStdString() != editedServiceId)
						continue;
					++serviceOccurrences;
					previewCodes << w.m_serviceOccurrenceTable->item(previewRow, 1)->text();
					if (include->checkState() != Qt::Checked)
						occurrencePreviewOk = false;
				}
				const QStringList expectedPreviewCodes({"1723", "1725", "1727"});
				if (previewCodes.size() != expectedPreviewCodes.size()
					|| !std::equal(previewCodes.cbegin(), previewCodes.cend(), expectedPreviewCodes.cbegin()))
					occurrencePreviewOk = false;
			}
			occurrencePreviewOk = occurrencePreviewOk && serviceOccurrences == 3
				&& w.m_sceneModel.services[1].operatingCode == "1723"
				&& w.m_sceneModel.services[1].performancePercent == 95.5
				&& w.m_sceneModel.services[1].hasRepeat && w.m_sceneModel.services[1].headwaySeconds == 1800.0
				&& w.m_sceneModel.services[1].hasRepeatCount && w.m_sceneModel.services[1].repeatCount == 3
				&& w.m_sceneModel.services[1].hasOperatingCodeStep && w.m_sceneModel.services[1].operatingCodeStep == 2
				&& w.m_sceneModel.services[1].hasMaximumSpeed && w.m_sceneModel.services[1].maximumSpeedKmh == 100.0;
			if (!occurrencePreviewOk)
				facetFailure(facetOk, "service", "occurrence preview did not derive the requested codes or fields");
			w.m_sceneModel.services[1].performancePercent = precisePerformancePercent;
			w.m_sceneModel.services[1].hasMaximumSpeed = true;
			w.m_sceneModel.services[1].maximumSpeedKmh = preciseMaximumSpeedKmh;
			w.m_servicePanel->updateDetail();
			if (w.m_serviceMaximumSpeedKmhEdit->text() != QString::number(preciseMaximumSpeedKmh, 'g', 6))
				facetFailure(facetOk, "service", "speed editor did not use compact display precision");
			w.m_serviceMaximumSpeedKmhEdit->setFocus();
			w.m_serviceMaximumSpeedKmhEdit->interpretText();
			w.m_serviceMaximumSpeedKmhEdit->clearFocus();
			w.m_servicePanel->commitPendingEditors();
			if (w.m_sceneModel.services[1].performancePercent != precisePerformancePercent
				|| w.m_sceneModel.services[1].maximumSpeedKmh != preciseMaximumSpeedKmh)
				facetFailure(facetOk, "service", "untouched precise service settings changed during pending commit");
			const SceneService serviceBeforeSelection = w.m_sceneModel.services[1];
			const bool dirtyBeforeSelection = w.m_sceneDirty;
			w.m_servicePanel->selectNoneOccurrences();
			if (w.m_runSelection.selectedInPeriodCount(w.m_sceneModel, w.serviceOccurrenceDuration()) != 0)
				facetFailure(facetOk, "service", "empty GUI selection counted all services in period");
			if (w.m_serviceOccurrenceTable) {
				for (int previewRow = 0; previewRow < w.m_serviceOccurrenceTable->rowCount(); ++previewRow) {
					QTableWidgetItem* include = w.m_serviceOccurrenceTable->item(previewRow, 0);
					if (include && include->data(Qt::UserRole).toString().toStdString() == editedServiceId
						&& include->data(Qt::UserRole + 1).toInt() == 1)
						include->setCheckState(Qt::Checked);
				}
			}
			const bool oneOccurrenceSelected = w.m_runSelection.selectedCount(w.m_sceneModel, w.serviceOccurrenceDuration()) == 1
				&& w.m_runSelection.selectedOccurrences(w.m_sceneModel, w.serviceOccurrenceDuration())
						.count(SceneServiceOccurrence{editedServiceId, 1})
					== 1;
			const bool selectionDidNotDirty = w.m_sceneDirty == dirtyBeforeSelection
				&& sameServices({serviceBeforeSelection}, {w.m_sceneModel.services[1]});
			if (!oneOccurrenceSelected || !selectionDidNotDirty)
				facetFailure(facetOk, "service", "occurrence selection changed canonical fields or dirty state");
		}
		w.m_servicePanel->addService();
		if (w.m_serviceListWidget->count() != originalCount + 2) {
			facetFailure(facetOk, "service", "add did not apply");
		} else {
			const std::string unreferencedServiceId = w.m_sceneModel.services.back().id;
			if (w.m_selectNoneOccurrencesButton) {
				w.m_selectNoneOccurrencesButton->click();
				QApplication::processEvents();
			}
			const bool occurrenceExclusionCreated = w.m_runSelection.isExcluded(
				SceneServiceOccurrence{unreferencedServiceId, 1});
			acceptConfirmation();
			w.m_servicePanel->deleteService();
			if (w.m_serviceListWidget->count() != originalCount + 1
				|| std::any_of(w.m_sceneModel.services.begin(), w.m_sceneModel.services.end(),
					[&](const SceneService& service) { return service.id == unreferencedServiceId; })
				|| (occurrenceExclusionCreated
					&& w.m_runSelection.isExcluded(SceneServiceOccurrence{unreferencedServiceId, 1})))
				facetFailure(facetOk, "service", "unreferenced service delete did not prune occurrence exclusions");
		}
		int editedRow = -1;
		for (int row = 0; row < static_cast<int>(w.m_sceneModel.services.size()); ++row) {
			if (w.m_sceneModel.services[row].id == editedServiceId) {
				editedRow = row;
				break;
			}
		}
		if (editedRow < 0) {
			facetFailure(facetOk, "service", "edited service was not retained");
		} else {
			const SceneService& edited = w.m_sceneModel.services[editedRow];
			if (edited.composition != editedCompositionId || edited.route != expectedServices[0].route
				|| !edited.hasEntryTime || edited.entryTimeSeconds != 600.0
				|| !edited.hasRepeat || edited.headwaySeconds != 1800.0
				|| !edited.hasRepeatCount || edited.repeatCount != 3
				|| edited.operatingCode != "1723" || edited.performancePercent != precisePerformancePercent
				|| !edited.hasMaximumSpeed || edited.maximumSpeedKmh != preciseMaximumSpeedKmh
				|| !edited.hasOperatingCodeStep || edited.operatingCodeStep != 2
				|| edited.stops.size() != expectedServices[0].stops.size())
				facetFailure(facetOk, "service", "edited fields did not persist in memory");
		}
		if (facetOk)
			std::fprintf(stdout, "E2E_EDITOR_SERVICE_OK\n");
		if (facetOk)
			std::fprintf(stdout, "E2E_EDITOR_SERVICE_OCCURRENCES_OK\n");
	}

	if (!w.m_sceneLoaded || !w.m_serviceListWidget || !w.m_stopTableWidget || editedServiceId.empty()) {
		bool facetOk = false;
		facetFailure(facetOk, "timetable", "scene or edited service unavailable");
	} else {
		bool facetOk = true;
		int serviceRow = -1;
		for (int row = 0; row < static_cast<int>(w.m_sceneModel.services.size()); ++row) {
			if (w.m_sceneModel.services[row].id == editedServiceId) {
				serviceRow = row;
				break;
			}
		}
		if (serviceRow < 0) {
			facetFailure(facetOk, "timetable", "edited service row missing");
		} else {
			w.m_serviceListWidget->setCurrentRow(serviceRow);
			QApplication::processEvents();
			w.m_serviceEntryTimeSecondsEdit->setText("0");
			w.m_servicePanel->commitEntryTimeSeconds();
			const int originalStopCount = static_cast<int>(w.m_sceneModel.services[serviceRow].stops.size());
			if (originalStopCount <= 0)
				facetFailure(facetOk, "timetable", "no baseline stop available for move coverage");
			if (originalStopCount > 0) {
				const SceneStop before = w.m_sceneModel.services[serviceRow].stops.front();
				const bool dirtyBefore = w.m_sceneDirty;
				w.m_stopTableWidget->setCurrentCell(originalStopCount - 1, 0);
				for (const int mode : {1, 0}) {
					w.m_stopTimeModeCombo->setCurrentIndex(mode);
					const auto& selected = w.m_sceneModel.services[serviceRow].stops.back();
					const QString expected = selected.hasPlannedArrival
						? QString::fromStdString(formatPlannedTime(selected.plannedArrivalSeconds,
							  mode == 1, baseTimeToSeconds(w.m_sceneModel.baseTime)))
						: QStringLiteral("\u2014");
					if (w.m_stopTableWidget->currentRow() != originalStopCount - 1
						|| w.m_stopTableWidget->item(originalStopCount - 1, 3)->text() != expected
						|| w.m_sceneDirty != dirtyBefore || w.m_moveStopDownButton->isEnabled()
						|| w.m_moveStopUpButton->isEnabled() != (originalStopCount > 1))
						facetFailure(facetOk, "timetable", "matrix display toggle changed selection, values or actions");
				}
				QTimer timer;
				timer.setSingleShot(true);
				QObject::connect(&timer, &QTimer::timeout, &w, [&]() {
					auto* dialog = w.findChild<QDialog*>("stopEditorDialog");
					if (!dialog) {
						facetFailure(facetOk, "timetable", "row activation did not open modal");
						return;
					}
					auto* arrival = dialog->findChild<QLineEdit*>("stopEditorArrivalEdit");
					auto* mode = dialog->findChild<QComboBox*>("stopEditorTimeModeCombo");
					dialog->findChild<QCheckBox*>("stopEditorArrivalPresent")->setChecked(true);
					arrival->setText("90");
					mode->setCurrentIndex(1);
					if (arrival->text().toStdString() != formatPlannedTime(90.0, true, baseTimeToSeconds(w.m_sceneModel.baseTime)))
						facetFailure(facetOk, "timetable", "clock conversion did not use case base time");
					mode->setCurrentIndex(0);
					if (arrival->text() != "90")
						facetFailure(facetOk, "timetable", "display toggle shifted offset");
					arrival->setText("invalid");
					dialog->findChild<QPushButton*>("stopEditorAcceptButton")->click();
					mode->setCurrentIndex(1);
					if (!dialog->isVisible() || mode->currentIndex() != 0 || arrival->text() != "invalid"
						|| dialog->findChild<QLabel*>("stopEditorErrorLabel")->text().isEmpty())
						facetFailure(facetOk, "timetable", "invalid time was accepted or discarded");
					dialog->findChild<QPushButton*>("stopEditorCancelButton")->click();
				});
				timer.start(0);
				QMetaObject::invokeMethod(w.m_stopTableWidget, "cellClicked", Qt::DirectConnection,
					Q_ARG(int, 0), Q_ARG(int, 0));
				if (!sameStop(before, w.m_sceneModel.services[serviceRow].stops.front()) || dirtyBefore != w.m_sceneDirty)
					facetFailure(facetOk, "timetable", "Cancel changed canonical stop or dirty state");
				if (!editStopDialog(0, [](QDialog*) {})
					|| !sameStop(before, w.m_sceneModel.services[serviceRow].stops.front()))
					facetFailure(facetOk, "timetable", "unchanged Accept lost optional values or precision");
				for (const QString arrivalText : {QStringLiteral("90.1234567890123"), QStringLiteral("0"), QString()}) {
					const bool accepted = editStopDialog(0, [&](QDialog* dialog) {
						dialog->findChild<QCheckBox*>("stopEditorArrivalPresent")->setChecked(true);
						dialog->findChild<QLineEdit*>("stopEditorArrivalEdit")->setText(arrivalText);
						dialog->findChild<QCheckBox*>("stopEditorDeparturePresent")->setChecked(false);
					});
					const auto& stop = w.m_sceneModel.services[serviceRow].stops.front();
					if (!accepted || stop.hasPlannedArrival != !arrivalText.isEmpty() || stop.hasPlannedDeparture
						|| (stop.hasPlannedArrival && stop.plannedArrivalSeconds != arrivalText.toDouble()))
						facetFailure(facetOk, "timetable", "modal lost fractional, zero or absent planned time");
				}
				w.m_sceneModel.services[serviceRow].stops.front().platformId = "unknown-platform";
				const SceneStop unknownPlatform = w.m_sceneModel.services[serviceRow].stops.front();
				bool unknownPlatformShown = false;
				if (!editStopDialog(0, [&](QDialog* dialog) {
						auto* platform = dialog->findChild<QComboBox*>("stopEditorPlatformCombo");
						unknownPlatformShown = platform->currentText() == "Invalid: unknown-platform"
							&& platform->currentData().toString() == "unknown-platform"
							&& !platform->itemData(platform->currentIndex(), Qt::ToolTipRole).toString().isEmpty();
					})
					|| !unknownPlatformShown || !sameStop(unknownPlatform, w.m_sceneModel.services[serviceRow].stops.front()))
					facetFailure(facetOk, "timetable", "unknown imported platform was replaced or not explained");
				w.m_sceneModel.services[serviceRow].stops.front() = before;
				const auto savedStops = w.m_sceneModel.services[serviceRow].stops;
				const auto savedStations = w.m_sceneModel.stations;
				const auto traversal = sceneServiceTraversal(w.m_sceneModel, w.m_sceneModel.services[serviceRow]);
				if (!traversal.visits.empty()) {
					const auto& visit = traversal.visits.front();
					for (auto& station : w.m_sceneModel.stations) {
						if (station.id != visit.stationId) continue;
						const auto platform = std::find_if(station.platforms.begin(), station.platforms.end(),
							[&](const ScenePlatform& p) { return p.id == visit.platformId; });
						if (platform != station.platforms.end()) {
							ScenePlatform alternative = *platform;
							alternative.id += "-ambiguous";
							station.platforms.push_back(alternative);
						}
					}
					w.m_sceneModel.services[serviceRow].stops.clear();
					w.m_timetableStopsPanel->refresh();
					const bool accepted = editStopDialog(-1, [](QDialog*) {});
					if (accepted || !w.m_sceneModel.services[serviceRow].stops.empty())
						facetFailure(facetOk, "timetable", "ambiguous new stop accepted without a platform");
				} else {
					facetFailure(facetOk, "timetable", "ambiguous stop fixture has no route visits");
				}
				w.m_sceneModel.stations = savedStations;
				w.m_sceneModel.services[serviceRow].stops = savedStops;
				w.m_timetableStopsPanel->refresh();
			}
			// Give this repeat-call fixture a second real anchor before the final
			// station, instead of calling the same consumed node twice.
			SceneStop finalStop = w.m_sceneModel.services[serviceRow].stops.back();
			const auto finalResolution = resolveSceneServiceStops(w.m_sceneModel, w.m_sceneModel.services[serviceRow],
				sceneServiceTraversal(w.m_sceneModel, w.m_sceneModel.services[serviceRow]))
											 .back();
			if (finalStop.platformId.empty() && finalResolution.candidatePlatformIds.size() == 1)
				finalStop.platformId = finalResolution.candidatePlatformIds.front();
			for (auto& station : w.m_sceneModel.stations)
				for (auto& platform : station.platforms)
					if (station.id == finalStop.stationId && platform.id == finalStop.platformId
						&& !platform.nodeIds.empty()) {
						const auto anchor = std::find_if(w.m_sceneModel.nodes.begin(), w.m_sceneModel.nodes.end(),
							[&](const SceneNode& node) { return node.id == platform.nodeIds.front(); });
						const SceneNode* preceding = nullptr;
						if (anchor != w.m_sceneModel.nodes.end())
							for (const auto& node : w.m_sceneModel.nodes)
								if (node.trackId == anchor->trackId && node.xKm < anchor->xKm
									&& (!preceding || node.xKm > preceding->xKm))
									preceding = &node;
						if (preceding)
							platform.nodeIds.insert(platform.nodeIds.begin(), preceding->id);
					}
			double lastTime = 0.0;
			for (const auto& stop : w.m_sceneModel.services[serviceRow].stops) {
				if (stop.hasPlannedArrival) lastTime = std::max(lastTime, stop.plannedArrivalSeconds);
				if (stop.hasPlannedDeparture) lastTime = std::max(lastTime, stop.plannedDepartureSeconds);
			}
			const double arrivalSeconds = lastTime - 120;
			const bool accepted = editStopDialog(-1, [&](QDialog* dialog) {
				auto* station = dialog->findChild<QComboBox*>("stopEditorStationCombo");
				station->setCurrentIndex(station->findData(QString::fromStdString(finalStop.stationId)));
				auto* platform = dialog->findChild<QComboBox*>("stopEditorPlatformCombo");
				platform->setCurrentIndex(platform->findData(QString::fromStdString(finalStop.platformId)));
				dialog->findChild<QCheckBox*>("stopEditorArrivalPresent")->setChecked(true);
				dialog->findChild<QCheckBox*>("stopEditorDeparturePresent")->setChecked(true);
				dialog->findChild<QLineEdit*>("stopEditorArrivalEdit")->setText(QString::number(arrivalSeconds));
				dialog->findChild<QLineEdit*>("stopEditorDepartureEdit")->setText(QString::number(arrivalSeconds + 60));
				dialog->findChild<QLineEdit*>("stopEditorDwellEdit")->setText("60");
			});
			if (!accepted || static_cast<int>(w.m_sceneModel.services[serviceRow].stops.size()) != originalStopCount + 1)
				facetFailure(facetOk, "timetable", "modal add stop did not apply");
			else {
				const auto& stop = w.m_sceneModel.services[serviceRow].stops.back();
				if (stop.stationId != finalStop.stationId || stop.platformId != finalStop.platformId
					|| !stop.hasPlannedArrival || stop.plannedArrivalSeconds != arrivalSeconds
					|| !stop.hasPlannedDeparture || stop.plannedDepartureSeconds != arrivalSeconds + 60
					|| stop.dwellSeconds != 60.0)
					facetFailure(facetOk, "timetable", "modal values were not committed together");
			}
			SceneStop editedStop;
			if (static_cast<int>(w.m_sceneModel.services[serviceRow].stops.size()) > originalStopCount)
				editedStop = w.m_sceneModel.services[serviceRow].stops.back();
			if (facetOk) {
				w.m_stopTableWidget->setCurrentCell(originalStopCount, 0);
				w.m_timetableStopsPanel->moveStopUp();
				if (w.m_sceneModel.services[serviceRow].stops[originalStopCount - 1].stationId != editedStop.stationId)
					facetFailure(facetOk, "timetable", "move up did not apply");
				w.m_timetableStopsPanel->moveStopDown();
				if (!sameStop(w.m_sceneModel.services[serviceRow].stops.back(), editedStop))
					facetFailure(facetOk, "timetable", "move down did not restore edited stop");
				// The source scene's final stop intentionally omits a planned departure;
				// leave the edited stop before it so the model remains valid for save/reload.
				w.m_stopTableWidget->setCurrentCell(originalStopCount, 0);
				w.m_timetableStopsPanel->moveStopUp();
			}
			// Insertion positions. Each prepared timetable leaves out one stop of the full
			// timetable, so the position where it belongs has a free station.
			{
				auto& stops = w.m_sceneModel.services[serviceRow].stops;
				const std::vector<SceneStop> fullStops = stops;
				const bool dirtySaved = w.m_sceneDirty;
				const auto prepare = [&](std::vector<SceneStop> prepared) {
					stops = std::move(prepared);
					w.m_sceneDirty = false;
					w.m_timetableStopsPanel->refresh();
				};
				const auto without = [&](std::size_t index) {
					std::vector<SceneStop> prepared = fullStops;
					prepared.erase(prepared.begin() + static_cast<std::ptrdiff_t>(index));
					return prepared;
				};
				const auto sameStopList = [&](const std::vector<SceneStop>& left, const std::vector<SceneStop>& right) {
					return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(), sameStop);
				};
				// Opens the dialog through `opener`, runs `inspect`, and cancels.
				const auto cancelStopDialog = [&](QPushButton* opener, const std::function<void(QDialog*)>& inspect) {
					bool opened = false;
					cancelConfirmation();
					QTimer timer;
					timer.setSingleShot(true);
					QObject::connect(&timer, &QTimer::timeout, &w, [&]() {
						auto* dialog = w.findChild<QDialog*>("stopEditorDialog");
						if (!dialog) return;
						opened = true;
						inspect(dialog);
						dialog->findChild<QPushButton*>("stopEditorCancelButton")->click();
					});
					timer.start(0);
					opener->click();
					QApplication::processEvents();
					return opened;
				};
				// Sets the position (when given) and the stop's values in the open dialog.
				const auto fillStop = [&](QDialog* dialog, const SceneStop& stop, int position) {
					if (position >= 0) {
						auto* positionCombo = dialog->findChild<QComboBox*>("stopEditorPositionCombo");
						if (!positionCombo || positionCombo->findData(position) < 0) {
							facetFailure(facetOk, "timetable", "position choice missing in the insert dialog");
							return;
						}
						positionCombo->setCurrentIndex(positionCombo->findData(position));
					}
					auto* station = dialog->findChild<QComboBox*>("stopEditorStationCombo");
					auto* platform = dialog->findChild<QComboBox*>("stopEditorPlatformCombo");
					const int stationIndex = station->findData(QString::fromStdString(stop.stationId));
					if (stationIndex < 0) {
						facetFailure(facetOk, "timetable", "station of the inserted stop is not offered at its position");
						return;
					}
					station->setCurrentIndex(stationIndex);
					if (!stop.platformId.empty())
						platform->setCurrentIndex(platform->findData(QString::fromStdString(stop.platformId)));
					dialog->findChild<QComboBox*>("stopEditorTimeModeCombo")->setCurrentIndex(0);
					dialog->findChild<QCheckBox*>("stopEditorArrivalPresent")->setChecked(stop.hasPlannedArrival);
					if (stop.hasPlannedArrival)
						dialog->findChild<QLineEdit*>("stopEditorArrivalEdit")->setText(QString::number(stop.plannedArrivalSeconds));
					dialog->findChild<QCheckBox*>("stopEditorDeparturePresent")->setChecked(stop.hasPlannedDeparture);
					if (stop.hasPlannedDeparture)
						dialog->findChild<QLineEdit*>("stopEditorDepartureEdit")->setText(QString::number(stop.plannedDepartureSeconds));
					dialog->findChild<QLineEdit*>("stopEditorDwellEdit")->setText(QString::number(stop.dwellSeconds));
				};
				// Inserts `stop` through the dialog and checks that it lands at `index` while
				// every earlier stop is unchanged and in the same order. `opener` null uses
				// Add Stop with the position `position`; otherwise the dialog must open at
				// `position`.
				const auto insertStop = [&](const char* what, QPushButton* opener, int position,
											const SceneStop& stop, std::size_t index) {
					const std::vector<SceneStop> before = stops;
					const quint64 revisionBefore = w.m_sceneRevision;
					SceneStop entered = stop;
					const bool accepted = editStopDialog(-1, [&](QDialog* dialog) {
						auto* positionCombo = dialog->findChild<QComboBox*>("stopEditorPositionCombo");
						if (opener && (!positionCombo || positionCombo->currentData().toInt() != position))
							facetFailure(facetOk, "timetable", QString("%1: dialog did not open at the expected position").arg(what));
						fillStop(dialog, stop, opener ? -1 : position);
						// A unique reachable platform is already chosen by the dialog.
						entered.platformId = dialog->findChild<QComboBox*>("stopEditorPlatformCombo")
							->currentData().toString().toStdString();
						if (!stop.platformId.empty() && entered.platformId != stop.platformId)
							facetFailure(facetOk, "timetable", QString("%1: the platform of the inserted stop is not offered").arg(what)); }, opener);
					if (!accepted || stops.size() != before.size() + 1) {
						facetFailure(facetOk, "timetable", QString("%1: insert was not applied").arg(what));
						return;
					}
					if (!sameStop(stops[index], entered))
						facetFailure(facetOk, "timetable", QString("%1: inserted stop is not at the chosen index or lost its values").arg(what));
					for (std::size_t i = 0; i < before.size(); ++i)
						if (!sameStop(stops[i < index ? i : i + 1], before[i]))
							facetFailure(facetOk, "timetable", QString("%1: an existing stop changed or moved out of order").arg(what));
					if (!w.m_sceneDirty || w.m_sceneRevision == revisionBefore
						|| w.m_stopTableWidget->currentRow() != static_cast<int>(index))
						facetFailure(facetOk, "timetable", QString("%1: scene not marked modified or new row not selected").arg(what));
				};
				if (fullStops.size() < 4 || fullStops[2].stationId != fullStops[3].stationId
					|| fullStops[1].stationId == fullStops[2].stationId) {
					facetFailure(facetOk, "timetable", "insert fixture needs a free station before a repeated final station");
				} else {
					// Add Stop on a timetable whose destination is present opens the dialog at a
					// position that has choices; Cancel changes nothing.
					prepare(without(1));
					const std::vector<SceneStop> gapStops = stops;
					const quint64 revisionBefore = w.m_sceneRevision;
					const bool opened = cancelStopDialog(w.m_addStopButton, [&](QDialog* dialog) {
						auto* positionCombo = dialog->findChild<QComboBox*>("stopEditorPositionCombo");
						auto* station = dialog->findChild<QComboBox*>("stopEditorStationCombo");
						if (!positionCombo || positionCombo->currentData().toInt() != 1
							|| positionCombo->count() != static_cast<int>(gapStops.size()) + 1
							|| positionCombo->itemText(0) != "At the start of the timetable"
							|| station->findData(QString::fromStdString(fullStops[1].stationId)) < 0)
							facetFailure(facetOk, "timetable", "Add Stop did not open at a position with choices");
						// A station that occurs twice gives two position entries with their own index.
						if (positionCombo && positionCombo->count() == 4
							&& (positionCombo->itemData(2).toInt() != 2 || positionCombo->itemData(3).toInt() != 3
								|| positionCombo->itemText(2) == positionCombo->itemText(3)
								|| !positionCombo->itemText(2).startsWith("After stop 2: ")
								|| !positionCombo->itemText(3).startsWith("After stop 3: ")))
							facetFailure(facetOk, "timetable", "repeated station did not give distinct position entries");
					});
					if (!opened || !sameStopList(stops, gapStops) || w.m_sceneDirty || w.m_sceneRevision != revisionBefore)
						facetFailure(facetOk, "timetable", "Add Stop on a present destination failed or Cancel changed the timetable");

					// A position with no station visit blocks OK and leaves the timetable unchanged.
					QString eligibilityText;
					QString blockedError;
					bool stationDisabled = false;
					bool stayedOpen = false;
					const bool blocked = !editStopDialog(-1, [&](QDialog* dialog) {
						auto* positionCombo = dialog->findChild<QComboBox*>("stopEditorPositionCombo");
						auto* station = dialog->findChild<QComboBox*>("stopEditorStationCombo");
						positionCombo->setCurrentIndex(positionCombo->findData(static_cast<int>(gapStops.size())));
						stationDisabled = !station->isEnabled() && station->count() == 1
							&& station->currentText() == "(no reachable station here)";
						eligibilityText = dialog->findChild<QLabel*>("stopEditorEligibilityLabel")->text(); }, nullptr, [&](QDialog* dialog) {
						stayedOpen = dialog->isVisible();
						blockedError = dialog->findChild<QLabel*>("stopEditorErrorLabel")->text(); });
					if (!blocked || !stationDisabled || !stayedOpen || eligibilityText.isEmpty()
						|| blockedError != eligibilityText || !sameStopList(stops, gapStops) || w.m_sceneDirty
						|| w.m_sceneRevision != revisionBefore)
						facetFailure(facetOk, "timetable", "a position without a station visit did not block OK");

					// Insert at the start, between two stops and after the last stop.
					prepare(without(0));
					insertStop("insert at the start", nullptr, 0, fullStops[0], 0);
					prepare(without(1));
					insertStop("insert between stops", nullptr, 1, fullStops[1], 1);
					prepare(without(fullStops.size() - 1));
					insertStop("insert after the last stop", nullptr, static_cast<int>(fullStops.size()) - 1,
						fullStops.back(), fullStops.size() - 1);

					// Insert After Selected on row 0 inserts between rows 0 and 1.
					prepare({});
					if (w.m_insertStopAfterButton->isEnabled() || !w.m_addStopButton->isEnabled())
						facetFailure(facetOk, "timetable", "Insert After Selected is enabled without a selected row");
					prepare(without(1));
					w.m_stopTableWidget->setCurrentCell(0, 0);
					if (!w.m_insertStopAfterButton->isEnabled())
						facetFailure(facetOk, "timetable", "Insert After Selected is disabled with a selected row");
					insertStop("Insert After Selected", w.m_insertStopAfterButton, 1, fullStops[1], 1);

					// The inserted order and planned times survive save and reload.
					QTemporaryDir reloadDir;
					const QString reloadPath = QDir(reloadDir.path()).filePath("inserted_stop_scene");
					const SceneSaveResult saved = ::saveScene(w.m_sceneModel, reloadPath.toStdString());
					const SceneLoadResult reloaded = ::loadScene(reloadPath.toStdString());
					const auto reloadedService = std::find_if(reloaded.scene.services.begin(), reloaded.scene.services.end(),
						[&](const SceneService& candidate) { return candidate.id == editedServiceId; });
					if (!saved.success() || reloadedService == reloaded.scene.services.end()
						|| reloadedService->stops.size() != stops.size()
						|| !std::equal(stops.begin(), stops.end(), reloadedService->stops.begin(), sameStop))
						facetFailure(facetOk, "timetable", "inserted stop order or planned times changed after save and reload");
				}
				stops = fullStops;
				w.m_sceneDirty = dirtySaved;
				w.m_timetableStopsPanel->refresh();
				w.updateSceneWindowTitle();
			}
			// Loaded invalid drafts remain removable; adding through the UI above
			// must not create one.
			w.m_sceneModel.services[serviceRow].stops.push_back(editedStop);
			w.m_timetableStopsPanel->refresh();
			w.m_stopTableWidget->setCurrentCell(originalStopCount + 1, 0);
			if (static_cast<int>(w.m_sceneModel.services[serviceRow].stops.size()) != originalStopCount + 2) {
				facetFailure(facetOk, "timetable", "temporary stop add did not apply");
			} else {
				w.m_timetableStopsPanel->removeStop();
				if (static_cast<int>(w.m_sceneModel.services[serviceRow].stops.size()) != originalStopCount + 1)
					facetFailure(facetOk, "timetable", "stop delete did not apply");
			}
			if (facetOk && !sameStop(w.m_sceneModel.services[serviceRow].stops[originalStopCount - 1], editedStop))
				facetFailure(facetOk, "timetable", "edited stop was not retained");
		}
		if (facetOk)
			std::fprintf(stdout, "E2E_EDITOR_TIMETABLE_OK\n");
	}

	std::string entranceDelayServiceId = editedServiceId;
	std::string entranceDelayStationId;
	double entranceDelaySeconds = 12.75;
	if (!w.m_sceneLoaded || editedServiceId.empty() || !w.m_entranceDelayListWidget
		|| !w.m_addEntranceDelayButton || !w.m_duplicateEntranceDelayButton
		|| !w.m_deleteEntranceDelayButton || !w.m_entranceDelayServiceCombo
		|| !w.m_entranceDelayOccurrenceEdit || !w.m_entranceDelayStationCombo
		|| !w.m_entranceDelaySecondsEdit) {
		bool facetOk = false;
		facetFailure(facetOk, "entrance delay", "public delay controls unavailable");
	} else {
		bool facetOk = true;
		if (w.m_incidentDock) {
			w.m_incidentDock->show();
			w.m_incidentDock->raise();
		}
		if (auto* tabs = w.findChild<QTabWidget*>("scenarioEditorTabs"))
			tabs->setCurrentIndex(1);
		const int originalDelayCount = w.m_entranceDelayListWidget->count();
		w.m_addEntranceDelayButton->click();
		QApplication::processEvents();
		if (w.m_entranceDelayListWidget->count() != originalDelayCount + 1) {
			facetFailure(facetOk, "entrance delay", "add did not apply through the public button");
		} else {
			const int delayRow = w.m_entranceDelayListWidget->count() - 1;
			w.m_entranceDelayListWidget->setCurrentRow(delayRow);
			const int serviceIndex = w.m_entranceDelayServiceCombo->findData(
				QString::fromStdString(entranceDelayServiceId));
			if (serviceIndex < 0) {
				facetFailure(facetOk, "entrance delay", "repeated service was missing from the typed selector");
			} else {
				w.m_entranceDelayServiceCombo->setCurrentIndex(serviceIndex);
				QApplication::processEvents();
				const auto editedService = std::find_if(w.m_sceneModel.services.begin(), w.m_sceneModel.services.end(),
					[&](const SceneService& candidate) { return candidate.id == entranceDelayServiceId; });
				int repeatedDepartureRow = -1;
				std::string repeatedStationId;
				if (editedService != w.m_sceneModel.services.end()) {
					const auto repeated = std::adjacent_find(editedService->stops.begin(), editedService->stops.end(),
						[](const SceneStop& first, const SceneStop& second) {
							return first.stationId == second.stationId && first.hasPlannedDeparture
								&& !second.hasPlannedDeparture;
						});
					if (repeated != editedService->stops.end()) {
						repeatedDepartureRow = static_cast<int>(std::distance(editedService->stops.begin(), repeated));
						repeatedStationId = repeated->stationId;
					}
				}
				if (repeatedDepartureRow < 0 || !w.m_serviceListWidget || !w.m_stopTableWidget
					|| !w.m_moveStopDownButton || !w.m_moveStopUpButton) {
					facetFailure(facetOk, "entrance delay", "repeated-stop selector fixture unavailable");
				} else {
					if (w.m_serviceDock) {
						w.m_serviceDock->show();
						w.m_serviceDock->raise();
					}
					w.m_serviceListWidget->setCurrentRow(static_cast<int>(
						std::distance(w.m_sceneModel.services.begin(), editedService)));
					w.m_stopTableWidget->setCurrentCell(repeatedDepartureRow, 0);
					w.m_moveStopDownButton->click();
					QApplication::processEvents();
					const bool laterDepartureExcluded = w.m_entranceDelayStationCombo->findData(
															QString::fromStdString(repeatedStationId))
						< 0;
					w.m_moveStopUpButton->click();
					QApplication::processEvents();
					if (!laterDepartureExcluded || w.m_entranceDelayStationCombo->findData(QString::fromStdString(repeatedStationId)) < 0)
						facetFailure(facetOk, "entrance delay",
							"station choices did not follow the first repeated stop");
					if (w.m_incidentDock) {
						w.m_incidentDock->show();
						w.m_incidentDock->raise();
					}
				}
				int stationIndex = -1;
				for (int index = 0; index < w.m_entranceDelayStationCombo->count(); ++index) {
					if (!w.m_entranceDelayStationCombo->itemData(index).toString().isEmpty()) {
						stationIndex = index;
						break;
					}
				}
				if (stationIndex < 0) {
					facetFailure(facetOk, "entrance delay", "repeated service had no eligible planned-departure stop");
				} else {
					entranceDelayStationId = w.m_entranceDelayStationCombo->itemData(stationIndex).toString().toStdString();
					w.m_entranceDelayStationCombo->setCurrentIndex(stationIndex);
					w.m_entranceDelayOccurrenceEdit->setValue(2);
					QMetaObject::invokeMethod(w.m_entranceDelayOccurrenceEdit, "editingFinished", Qt::DirectConnection);
					w.m_entranceDelaySecondsEdit->setValue(entranceDelaySeconds);
					QMetaObject::invokeMethod(w.m_entranceDelaySecondsEdit, "editingFinished", Qt::DirectConnection);
					const SceneEntranceDelay* delay = w.selectedEntranceDelay();
					const bool editedDelay = delay && delay->serviceId == entranceDelayServiceId
						&& delay->occurrence == 2 && delay->stationId == entranceDelayStationId
						&& delay->delaySeconds == entranceDelaySeconds && editedService != w.m_sceneModel.services.end()
						&& editedService->hasRepeat
						&& sceneServiceOccurrenceCount(*editedService, w.serviceOccurrenceDuration()) >= 2
						&& w.m_entranceDelayOccurrenceContextLabel
						&& w.m_entranceDelayOccurrenceContextLabel->text().contains("1725");
					if (!editedDelay)
						facetFailure(facetOk, "entrance delay", "service, occurrence, station, or seconds edit did not persist");
					const SceneRunSelection delayedOccurrence{{entranceDelayServiceId, 2}};
					const auto infrastructureDiagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), w.m_sceneModel);
					const auto operationsDiagnostics = buildOperationsFromScene(defaultRun(),
						w.m_sceneModel, w.m_runSelection.scenarioId(), delayedOccurrence);
					if (hasErrors(infrastructureDiagnostics) || hasErrors(operationsDiagnostics)
						|| numRegions != 1
						|| regional_train[0].trainDescription != entranceDelayServiceId + "-2"
						|| regional_train[0].EntranceDelay != entranceDelaySeconds)
						facetFailure(facetOk, "entrance delay",
							"publicly authored delay did not reach the selected native occurrence");
				}
			}
		}
		w.m_duplicateEntranceDelayButton->click();
		QApplication::processEvents();
		if (w.m_entranceDelayListWidget->count() != originalDelayCount + 2) {
			facetFailure(facetOk, "entrance delay", "duplicate did not apply through the public button");
		} else {
			acceptConfirmation();
			w.m_deleteEntranceDelayButton->click();
			QApplication::processEvents();
			if (w.m_entranceDelayListWidget->count() != originalDelayCount + 1)
				facetFailure(facetOk, "entrance delay", "delete did not apply through the public button");
		}
		if (const SceneScenario* scenario = w.selectedScenario())
			expectedEntranceDelays = scenario->entranceDelays;
		if (facetOk)
			std::fprintf(stdout, "E2E_EDITOR_ENTRANCE_DELAY_OK\n");
	}

	std::string editedIncidentId;
	if (!w.m_sceneLoaded || !w.m_incidentListWidget || editedServiceId.empty()) {
		bool facetOk = false;
		facetFailure(facetOk, "incident", "scene or edited service unavailable");
	} else {
		bool facetOk = true;
		const double preciseBreakdownSpeed = 40.125;
		const int originalCount = w.m_incidentListWidget->count();
		editedIncidentId = sceneUniqueIncidentId(w.m_sceneModel, "e2e_incident");
		w.m_incidentPanel->addIncident();
		if (w.m_incidentListWidget->count() != originalCount + 1) {
			facetFailure(facetOk, "incident", "add did not apply");
		} else {
			if (w.m_incidentIdEdit)
				w.m_incidentIdEdit->setText(QString::fromStdString(editedIncidentId));
			w.m_incidentPanel->commitIdEdit();
			if (w.m_incidentTypeCombo)
				w.m_incidentTypeCombo->setCurrentText("train_breakdown");
			if (!w.m_incidentHasReducedSpeedCheck || !w.m_incidentHasReducedSpeedCheck->isEnabled())
				facetFailure(facetOk, "incident", "breakdown controls stayed disabled after the type change");
			for (int index = 0; index < w.m_incidentTargetCombo->count(); ++index) {
				if (w.m_incidentTargetCombo->itemData(index).toString().toStdString() == editedServiceId
					&& w.m_incidentTargetCombo->itemData(index, Qt::UserRole + 1).toInt() == 2) {
					w.m_incidentTargetCombo->setCurrentIndex(index);
					break;
				}
			}
			if (w.m_incidentStartSecondsEdit)
				w.m_incidentStartSecondsEdit->setText("100");
			w.m_incidentPanel->commitStartSeconds();
			if (w.m_incidentEndSecondsEdit)
				w.m_incidentEndSecondsEdit->setText("200");
			w.m_incidentPanel->commitEndSeconds();
			if (w.m_incidentHasReducedSpeedCheck)
				w.m_incidentHasReducedSpeedCheck->setChecked(true);
			if (SceneIncident* incident = w.m_incidentPanel->selectedIncident(); !incident
				|| !incident->hasReducedSpeed || !w.m_incidentReducedSpeedKmhEdit
				|| incident->reducedSpeedKmh != w.m_incidentReducedSpeedKmhEdit->value())
				facetFailure(facetOk, "incident", "enabling the speed cap did not commit its displayed value");
			if (w.m_incidentReducedSpeedKmhEdit)
				w.m_incidentReducedSpeedKmhEdit->setValue(preciseBreakdownSpeed);
			w.m_incidentPanel->commitReducedSpeed();
			if (w.m_incidentHasEndSecondsCheck)
				w.m_incidentHasEndSecondsCheck->setChecked(false);
			if (w.m_incidentTerminateAtDestinationCheck)
				w.m_incidentTerminateAtDestinationCheck->setChecked(true);
		}
		w.m_incidentPanel->duplicateIncident();
		if (w.m_incidentListWidget->count() != originalCount + 2) {
			facetFailure(facetOk, "incident", "duplicate did not apply");
		} else {
			acceptConfirmation();
			w.m_incidentPanel->deleteIncident();
			if (w.m_incidentListWidget->count() != originalCount + 1)
				facetFailure(facetOk, "incident", "delete did not apply");
		}
		int editedRow = -1;
		const auto& incidents = w.selectedScenarioIncidents();
		for (int row = 0; row < static_cast<int>(incidents.size()); ++row) {
			if (incidents[row].id == editedIncidentId) {
				editedRow = row;
				break;
			}
		}
		if (editedRow < 0 || incidents[editedRow].type != "train_breakdown"
			|| incidents[editedRow].target != editedServiceId
			|| incidents[editedRow].startSeconds != 100.0
			|| incidents[editedRow].endSeconds != 0.0
			|| !incidents[editedRow].hasOccurrence || incidents[editedRow].occurrence != 2
			|| !incidents[editedRow].hasReducedSpeed
			|| incidents[editedRow].reducedSpeedKmh != preciseBreakdownSpeed
			|| incidents[editedRow].hasEndSeconds
			|| !incidents[editedRow].terminateAtDestination)
			facetFailure(facetOk, "incident", "edited incident was not retained");
		if (!w.m_serviceListWidget || !w.m_incidentTargetCombo) {
			facetFailure(facetOk, "incident", "service target controls unavailable");
		} else {
			const int serviceCount = w.m_serviceListWidget->count();
			w.m_servicePanel->addService();
			const std::string temporaryServiceId = w.m_sceneModel.services.back().id;
			if (w.m_incidentTargetCombo->findData(QString::fromStdString(temporaryServiceId)) < 0)
				facetFailure(facetOk, "incident", "service add did not refresh breakdown targets");
			acceptConfirmation();
			w.m_servicePanel->deleteService();
			if (w.m_serviceListWidget->count() != serviceCount
				|| w.m_incidentTargetCombo->findData(QString::fromStdString(temporaryServiceId)) >= 0)
				facetFailure(facetOk, "incident", "service delete did not refresh breakdown targets");
		}
		const QString review = w.runReviewText();
		if (editedRow >= 0) {
			w.m_incidentListWidget->setCurrentRow(editedRow);
			SceneIncident* incident = w.m_incidentPanel->selectedIncident();
			const SceneIncident saved = *incident;
			incident->hasOccurrence = false;
			incident->occurrence = 1;
			w.m_incidentPanel->updateDetail();
			w.commitPendingEditorValues();
			if (incident->hasOccurrence || !w.m_incidentTargetCombo->currentText().contains("Historical scope"))
				facetFailure(facetOk, "incident", "historical all-occurrence scope was narrowed");
			incident->hasOccurrence = true;
			incident->occurrence = std::numeric_limits<int>::max();
			w.m_incidentPanel->updateDetail();
			if (!w.m_incidentTargetCombo->currentText().contains("Invalid removed target")
				|| w.m_incidentTargetCombo->currentData().toString().toStdString() != saved.target)
				facetFailure(facetOk, "incident", "removed occurrence was silently retargeted");
			*incident = saved;
			w.m_incidentPanel->updateDetail();
		}
		const bool reviewOk = review.contains(QString("Scenario: %1").arg(w.scenarioContext()))
			&& review.contains(QString("id=%1 type=train_breakdown target=%2 start=100")
					.arg(QString::fromStdString(editedIncidentId), QString::fromStdString(editedServiceId)))
			&& review.contains("window=until-destination occurrence=2 options=")
			&& review.contains(QString("Entrance delay configuration: service=%1 occurrence=2 operating_code=1725 station=%2 seconds=%3")
					.arg(QString::fromStdString(entranceDelayServiceId), QString::fromStdString(entranceDelayStationId),
						QString::number(entranceDelaySeconds, 'g', 12)));
		if (!reviewOk)
			facetFailure(facetOk, "incident", "run review omitted complete incident or entrance-delay configuration");
		if (facetOk)
			std::fprintf(stdout, "E2E_EDITOR_INCIDENT_OK\n");
	}

	if (w.m_sceneLoaded) {
		bool facetOk = true;
		w.refreshValidationPanel();
		QApplication::processEvents();
		SceneDiagnosticCounts counts = countDiagnostics(w.m_sceneDiagnostics);
		if (!w.m_validationTable || !w.m_validationStatusLabel || counts.errors != 0
			|| w.m_validationTable->rowCount() != static_cast<int>(w.m_sceneDiagnostics.size())
			|| !w.m_validationStatusLabel->text().startsWith("Validation:"))
			facetFailure(facetOk, "validation", "refresh did not report a clean, complete result");
		if (facetOk)
			std::fprintf(stdout, "E2E_EDITOR_VALIDATION_OK\n");
	}

	if (w.m_sceneLoaded) {
		bool facetOk = true;
		QString outBase = qEnvironmentVariable("QEGTRAIN_E2E_OUT");
		QTemporaryDir tmpDir;
		if (outBase.isEmpty())
			outBase = tmpDir.path();
		QString outScenePath = QDir(outBase).filePath("editor_smoke_scene");
		QDir outputDir(outScenePath);
		if (outputDir.exists() && !outputDir.removeRecursively()) {
			facetFailure(facetOk, "save/reload", "output path could not be cleaned");
		} else {
			for (int row = 0; row < static_cast<int>(w.m_sceneModel.services.size()); ++row) {
				if (w.m_sceneModel.services[static_cast<std::size_t>(row)].id != editedServiceId)
					continue;
				w.m_serviceListWidget->setCurrentRow(row);
				w.m_serviceOperatingCodeEdit->setText("1731");
				w.m_servicePanel->commitPendingEditors();
				if (w.m_sceneModel.services[static_cast<std::size_t>(row)].operatingCode != "1731")
					facetFailure(facetOk, "save/reload", "pending service text did not commit before persistence");
				break;
			}
			expectedTrainUnits = w.m_sceneModel.trainUnits;
			expectedCompositions = w.m_sceneModel.compositions;
			expectedServices = w.m_sceneModel.services;
			expectedStations = w.m_sceneModel.stations;
			expectedPassengers = w.m_sceneModel.passengers;
			expectedIncidents = w.selectedScenarioIncidents();
			if (const SceneScenario* scenario = w.selectedScenario())
				expectedEntranceDelays = scenario->entranceDelays;
			auto result = ::saveScene(w.m_sceneModel, outScenePath.toStdString());
			if (!result.success()) {
				facetFailure(facetOk, "save/reload", "save failed");
			} else if (!w.openSceneDirectory(outScenePath)) {
				facetFailure(facetOk, "save/reload", "saved scene did not reload");
			} else if (!sameTrainUnits(expectedTrainUnits, w.m_sceneModel.trainUnits)) {
				facetFailure(facetOk, "save/reload", "train-unit facet changed after reload");
			} else if (!sameCompositions(expectedCompositions, w.m_sceneModel.compositions)) {
				facetFailure(facetOk, "save/reload", "composition facet changed after reload");
			} else if (!sameServices(expectedServices, w.m_sceneModel.services)) {
				facetFailure(facetOk, "save/reload", "service/timetable facet changed after reload");
			} else if (!std::any_of(w.m_sceneModel.services.begin(), w.m_sceneModel.services.end(),
						   [&](const SceneService& service) {
							   return service.id == editedServiceId && service.visualizationColor == "#3c8dd2";
						   })) {
				facetFailure(facetOk, "save/reload", "service colour changed after reload");
			} else if (!sameStations(expectedStations, w.m_sceneModel.stations)) {
				facetFailure(facetOk, "save/reload", "platform geometry changed after reload");
			} else if (!samePassengers(expectedPassengers, w.m_sceneModel.passengers)) {
				facetFailure(facetOk, "save/reload", "passenger hierarchy changed after reload");
			} else if (!sameIncidents(expectedIncidents, w.selectedScenarioIncidents())) {
				facetFailure(facetOk, "save/reload", "incident facet changed after reload");
			} else if (!w.selectedScenario()
				|| !sameEntranceDelays(expectedEntranceDelays, w.selectedScenario()->entranceDelays)) {
				facetFailure(facetOk, "save/reload", "entrance-delay facet changed after reload");
			} else if (w.m_sceneDirty || hasErrors(w.m_sceneDiagnostics)) {
				facetFailure(facetOk, "save/reload", "reloaded scene is dirty or invalid");
			} else {
				const auto triggerPendingSave = [&]() {
					if (!w.m_saveSceneAction || !w.m_saveSceneAction->isEnabled())
						return false;
					w.m_saveSceneAction->trigger();
					QApplication::processEvents();
					return !w.m_sceneDirty;
				};
				const auto spinTextEdit = [](QAbstractSpinBox* spin) {
					return spin ? spin->findChild<QLineEdit*>() : nullptr;
				};
				const auto modelRowFor = [](const auto& values, const std::string& id) {
					for (int row = 0; row < static_cast<int>(values.size()); ++row)
						if (values[static_cast<std::size_t>(row)].id == id)
							return row;
					return -1;
				};
				const int trainUnitRow = modelRowFor(w.m_sceneModel.trainUnits, editedTrainUnitId);
				const int compositionRow = modelRowFor(w.m_sceneModel.compositions, editedCompositionId);
				const int serviceRow = modelRowFor(w.m_sceneModel.services, editedServiceId);
				const int passengerRow = modelRowFor(w.m_sceneModel.passengers, e2ePassengerId);
				if (passengerRow < 0 || e2eJourneyId.empty() || e2eLegId.empty()
					|| !w.m_passengerDock || !w.m_passengerListWidget || !w.m_passengerIdEdit
					|| !w.m_passengerJourneyListWidget || !w.m_passengerJourneyIdEdit
					|| !w.m_passengerJourneyActivityEdit || !w.m_passengerLegListWidget
					|| !w.m_passengerLegIdEdit || !w.m_passengerLegOccurrenceEdit) {
					facetFailure(facetOk, "save/reload", "passenger pending-editor controls were unavailable after reload");
				} else {
					w.activateWindow();
					w.m_passengerDock->show();
					w.m_passengerDock->raise();
					w.m_passengerListWidget->setCurrentRow(passengerRow);
					if (w.m_passengerTabs)
						w.m_passengerTabs->setCurrentIndex(0);
					QApplication::processEvents();
					const std::string pendingPassengerId = e2ePassengerId + "_focused";
					w.m_passengerIdEdit->setText(QString::fromStdString(pendingPassengerId));
					w.m_passengerIdEdit->setFocus();
					if (!triggerPendingSave() || w.m_sceneModel.passengers[static_cast<std::size_t>(passengerRow)].id != pendingPassengerId)
						facetFailure(facetOk, "save/reload", "Save did not commit the still-focused passenger ID");
					else
						e2ePassengerId = pendingPassengerId;
					w.m_passengerJourneyListWidget->setCurrentRow(0);
					QApplication::processEvents();
					const std::string pendingJourneyId = e2eJourneyId + "_focused";
					w.m_passengerJourneyIdEdit->setText(QString::fromStdString(pendingJourneyId));
					w.m_passengerJourneyIdEdit->setFocus();
					if (!triggerPendingSave() || !w.m_passengerPanel->selectedPassengerJourney()
						|| w.m_passengerPanel->selectedPassengerJourney()->id != pendingJourneyId)
						facetFailure(facetOk, "save/reload", "Save did not commit the still-focused passenger journey ID");
					else
						e2eJourneyId = pendingJourneyId;
					const std::string pendingActivity = "e2e focused activity";
					w.m_passengerJourneyActivityEdit->setText(QString::fromStdString(pendingActivity));
					w.m_passengerJourneyActivityEdit->setFocus();
					if (!triggerPendingSave() || !w.m_passengerPanel->selectedPassengerJourney()
						|| w.m_passengerPanel->selectedPassengerJourney()->activity != pendingActivity)
						facetFailure(facetOk, "save/reload", "Save did not commit the still-focused passenger activity");
					if (w.m_passengerJourneyWindowEdits[0]) {
						QLineEdit* pendingWindow = spinTextEdit(w.m_passengerJourneyWindowEdits[0]);
						const double pendingWindowValue = 111.25;
						if (!pendingWindow) {
							facetFailure(facetOk, "save/reload", "passenger planned-window editor was unavailable");
						} else {
							pendingWindow->setText(QString::number(pendingWindowValue, 'g', 16));
							pendingWindow->setFocus();
							if (!triggerPendingSave() || !w.m_passengerPanel->selectedPassengerJourney()
								|| w.m_passengerPanel->selectedPassengerJourney()->plannedDepartureStartSeconds != pendingWindowValue)
								facetFailure(facetOk, "save/reload", "Save did not commit the still-focused passenger planned window");
						}
					}
					if (w.m_passengerTabs)
						w.m_passengerTabs->setCurrentIndex(1);
					w.m_passengerLegListWidget->setCurrentRow(0);
					QApplication::processEvents();
					const std::string pendingLegId = e2eLegId + "_focused";
					w.m_passengerLegIdEdit->setText(QString::fromStdString(pendingLegId));
					w.m_passengerLegIdEdit->setFocus();
					if (!triggerPendingSave() || !w.m_passengerPanel->selectedPassengerLeg()
						|| w.m_passengerPanel->selectedPassengerLeg()->id != pendingLegId)
						facetFailure(facetOk, "save/reload", "Save did not commit the still-focused passenger leg ID");
					else
						e2eLegId = pendingLegId;
					QLineEdit* pendingOccurrence = spinTextEdit(w.m_passengerLegOccurrenceEdit);
					const int repeatedServiceIndex = w.m_passengerLegServiceCombo
						? w.m_passengerLegServiceCombo->findData(QString::fromStdString(editedServiceId))
						: -1;
					if (!pendingOccurrence || repeatedServiceIndex < 0) {
						facetFailure(facetOk, "save/reload", "passenger occurrence editor was unavailable");
					} else {
						w.activateWindow();
						w.m_passengerDock->show();
						w.m_passengerDock->raise();
						w.m_passengerLegServiceCombo->setCurrentIndex(repeatedServiceIndex);
						QApplication::processEvents();
						pendingOccurrence->setText("2");
						pendingOccurrence->setFocus();
						QApplication::processEvents();
						const bool savedOccurrence = triggerPendingSave();
						if (!savedOccurrence || !w.m_passengerPanel->selectedPassengerLeg()
							|| w.m_passengerPanel->selectedPassengerLeg()->occurrence != 2) {
							facetFailure(facetOk, "save/reload",
								QString("Save did not commit the still-focused passenger occurrence "
										"(range %1..%2, editor %3, model %4, saved %5)")
									.arg(w.m_passengerLegOccurrenceEdit->minimum())
									.arg(w.m_passengerLegOccurrenceEdit->maximum())
									.arg(w.m_passengerLegOccurrenceEdit->value())
									.arg(w.m_passengerPanel->selectedPassengerLeg() ? w.m_passengerPanel->selectedPassengerLeg()->occurrence : -1)
									.arg(savedOccurrence));
						}
					}
					if (!w.m_infrastructureFacetCombo || !w.m_infrastructureTable
						|| w.m_infrastructureFacetCombo->findData("platforms") < 0) {
						facetFailure(facetOk, "save/reload", "platform geometry controls were unavailable for focused Save");
					} else {
						w.activateWindow();
						if (w.m_infrastructureDock) {
							w.m_infrastructureDock->show();
							w.m_infrastructureDock->raise();
						}
						w.m_infrastructureFacetCombo->setCurrentIndex(
							w.m_infrastructureFacetCombo->findData("platforms"));
						QApplication::processEvents();
						const auto enterSpinText = [](QLineEdit* editor, const QString& text) {
							editor->selectAll();
							for (const QChar character : text) {
								QKeyEvent keyPress(QEvent::KeyPress, character.unicode(), Qt::NoModifier,
									QString(character));
								QApplication::sendEvent(editor, &keyPress);
							}
						};
						int implicitPlatformRow = -1;
						ScenePlatform* implicitPlatform = nullptr;
						int platformRow = 0;
						for (auto& station : w.m_sceneModel.stations) {
							for (auto& platform : station.platforms) {
								if (!implicitPlatform && !platform.hasLength && !platform.hasWidth) {
									implicitPlatformRow = platformRow;
									implicitPlatform = &platform;
								}
								++platformRow;
							}
						}
						auto* implicitLengthEdit = implicitPlatformRow >= 0
							? qobject_cast<QDoubleSpinBox*>(w.m_infrastructureTable->cellWidget(implicitPlatformRow, 3))
							: nullptr;
						auto* implicitWidthEdit = implicitPlatformRow >= 0
							? qobject_cast<QDoubleSpinBox*>(w.m_infrastructureTable->cellWidget(implicitPlatformRow, 4))
							: nullptr;
						QLineEdit* implicitLength = spinTextEdit(implicitLengthEdit);
						QLineEdit* implicitWidth = spinTextEdit(implicitWidthEdit);
						if (!implicitPlatform || !implicitLength || !implicitWidth) {
							facetFailure(facetOk, "save/reload", "implicit platform geometry was unavailable");
						} else {
							implicitWidth->setFocus();
							QApplication::processEvents();
							if (!implicitWidth->hasFocus())
								facetFailure(facetOk, "save/reload", "implicit platform width could not receive focus");
							enterSpinText(implicitWidth, QStringLiteral("2.5"));
							implicitLength->setFocus();
							QApplication::processEvents();
							if (!implicitLength->hasFocus()) {
								facetFailure(facetOk, "save/reload", "implicit platform length could not receive focus");
							} else if (!implicitPlatform->hasWidth || implicitPlatform->widthM != 2.5) {
								facetFailure(facetOk, "save/reload",
									"same-value platform geometry was not committed on focus loss");
							} else if (!triggerPendingSave()
								|| implicitPlatform->hasLength)
								facetFailure(facetOk, "save/reload",
									"Save materialized an untouched compatibility platform length");
						}
						auto* lengthEdit = qobject_cast<QDoubleSpinBox*>(w.m_infrastructureTable->cellWidget(0, 3));
						QLineEdit* pendingLength = spinTextEdit(lengthEdit);
						if (!pendingLength) {
							facetFailure(facetOk, "save/reload", "focused platform length editor was unavailable");
						} else {
							const double pendingLengthValue = 234.5;
							pendingLength->setFocus();
							enterSpinText(pendingLength, QString::number(pendingLengthValue, 'g', 16));
							if (!triggerPendingSave() || w.m_sceneModel.stations.empty()
								|| w.m_sceneModel.stations.front().platforms.empty()
								|| !w.m_sceneModel.stations.front().platforms.front().hasLength
								|| w.m_sceneModel.stations.front().platforms.front().lengthM != pendingLengthValue)
								facetFailure(facetOk, "save/reload", "Save did not commit the still-focused platform length");
						}
					}
				}
				int entranceDelayRow = -1;
				if (const SceneScenario* scenario = w.selectedScenario()) {
					for (int row = 0; row < static_cast<int>(scenario->entranceDelays.size()); ++row) {
						const SceneEntranceDelay& delay = scenario->entranceDelays[static_cast<std::size_t>(row)];
						if (delay.serviceId == entranceDelayServiceId && delay.occurrence == 2
							&& delay.stationId == entranceDelayStationId) {
							entranceDelayRow = row;
							break;
						}
					}
				}
				if (entranceDelayRow < 0 || !w.m_entranceDelayListWidget || !w.m_entranceDelaySecondsEdit
					|| !spinTextEdit(w.m_entranceDelaySecondsEdit)) {
					facetFailure(facetOk, "save/reload", "focused entrance-delay controls were unavailable after reload");
				} else {
					w.activateWindow();
					if (w.m_incidentDock) {
						w.m_incidentDock->show();
						w.m_incidentDock->raise();
					}
					if (auto* tabs = w.findChild<QTabWidget*>("scenarioEditorTabs"))
						tabs->setCurrentIndex(1);
					w.m_entranceDelayListWidget->setCurrentRow(entranceDelayRow);
					QApplication::processEvents();
					const double pendingEntranceDelay = entranceDelaySeconds + 0.5;
					QLineEdit* pendingDelayEdit = spinTextEdit(w.m_entranceDelaySecondsEdit);
					pendingDelayEdit->setText(QString::number(pendingEntranceDelay, 'g', 16));
					pendingDelayEdit->setFocus();
					QApplication::processEvents();
					if (!w.selectedEntranceDelay() || w.selectedEntranceDelay()->delaySeconds == pendingEntranceDelay
						|| !triggerPendingSave() || !w.selectedEntranceDelay()
						|| w.selectedEntranceDelay()->delaySeconds != pendingEntranceDelay) {
						facetFailure(facetOk, "save/reload",
							"Save did not commit the still-focused entrance-delay value");
					} else {
						entranceDelaySeconds = pendingEntranceDelay;
						if (const SceneScenario* scenario = w.selectedScenario())
							expectedEntranceDelays = scenario->entranceDelays;
					}
				}
				if (trainUnitRow < 0 || compositionRow < 0 || serviceRow < 0
					|| !w.m_compositionIdEdit || !w.m_trainUnitSourceDataEdit
					|| !w.m_trainUnitPhysicalEdits[0]
					|| !spinTextEdit(w.m_trainUnitPhysicalEdits[0])) {
					facetFailure(facetOk, "save/reload", "pending editor controls were unavailable after reload");
				} else {
					w.activateWindow();
					if (w.m_trainUnitDock) {
						w.m_trainUnitDock->show();
						w.m_trainUnitDock->raise();
					}
					QApplication::processEvents();
					w.m_trainUnitListWidget->setCurrentRow(trainUnitRow);
					w.m_serviceListWidget->setCurrentRow(serviceRow);
					const double pendingPhysicalMass = std::max(
						123.0, w.m_trainUnitPhysicalEdits[0]->value() + 1.25);
					QLineEdit* pendingPhysicalEdit = spinTextEdit(w.m_trainUnitPhysicalEdits[0]);
					pendingPhysicalEdit->setText(QString::number(pendingPhysicalMass, 'g', 16));
					pendingPhysicalEdit->setFocus();
					QApplication::processEvents();
					if (!triggerPendingSave()) {
						facetFailure(facetOk, "save/reload", "Save action did not flush focused train-unit text");
					} else {
						const int committedTrainRow = modelRowFor(w.m_sceneModel.trainUnits, editedTrainUnitId);
						if (committedTrainRow < 0
							|| !w.m_sceneModel.trainUnits[static_cast<std::size_t>(committedTrainRow)].hasPhysical
							|| w.m_sceneModel.trainUnits[static_cast<std::size_t>(committedTrainRow)]
									.physical.mass_of_traction_unit_kg
								!= pendingPhysicalMass)
							facetFailure(facetOk, "save/reload", "focused train-unit text was not committed before Save");
					}

					const std::string pendingCompositionId = sceneUniqueCompositionId(w.m_sceneModel, "e2e_pending_composition");
					if (w.m_compositionDock) {
						w.m_compositionDock->show();
						w.m_compositionDock->raise();
					}
					w.m_compositionListWidget->setCurrentRow(compositionRow);
					w.m_serviceListWidget->setCurrentRow(serviceRow);
					w.m_compositionIdEdit->setText(QString::fromStdString(pendingCompositionId));
					w.m_compositionIdEdit->setFocus();
					if (!triggerPendingSave()) {
						facetFailure(facetOk, "save/reload", "Save action did not flush focused composition text");
					} else {
						const int committedCompositionRow = modelRowFor(w.m_sceneModel.compositions, pendingCompositionId);
						if (committedCompositionRow < 0
							|| !std::any_of(
								w.m_sceneModel.services.begin(), w.m_sceneModel.services.end(),
								[&](const SceneService& service) { return service.composition == pendingCompositionId; }))
							facetFailure(facetOk, "save/reload", "focused composition text was not committed before Save");
						else
							editedCompositionId = pendingCompositionId;
					}

					const int pendingServiceRow = modelRowFor(w.m_sceneModel.services, editedServiceId);
					int pendingStopRow = -1;
					if (pendingServiceRow >= 0) {
						const auto& stops = w.m_sceneModel.services[static_cast<std::size_t>(pendingServiceRow)].stops;
						for (int row = 0; row < static_cast<int>(stops.size()); ++row) {
							if (stops[static_cast<std::size_t>(row)].hasPlannedArrival && stops[static_cast<std::size_t>(row)].hasPlannedDeparture) {
								pendingStopRow = row;
								break;
							}
						}
						if (pendingStopRow < 0 && !stops.empty())
							pendingStopRow = 0;
					}
					const int scenarioRow = [&]() {
						for (int row = 0; row < static_cast<int>(w.m_sceneModel.scenarios.size()); ++row)
							if (w.m_sceneModel.scenarios[static_cast<std::size_t>(row)].id == w.m_runSelection.scenarioId())
								return row;
						return -1;
					}();
					if (pendingServiceRow < 0 || pendingStopRow < 0 || scenarioRow < 0
						|| !w.m_stopTableWidget
						|| !w.m_scenarioListWidget || !w.m_scenarioDescriptionEdit
						|| !w.m_incidentListWidget || !w.m_incidentEndSecondsEdit
						|| !w.m_incidentHasEndSecondsCheck) {
						facetFailure(facetOk, "save/reload", "pending stop, scenario, or incident controls were unavailable");
					} else {
						w.m_serviceListWidget->setCurrentRow(pendingServiceRow);
						w.m_stopTableWidget->setCurrentCell(pendingStopRow, 0);
						QApplication::processEvents();
						const int pendingDwell = static_cast<int>(w.m_sceneModel.services[static_cast<std::size_t>(pendingServiceRow)]
														 .stops[static_cast<std::size_t>(pendingStopRow)]
														 .dwellSeconds)
							+ 1;
						if (!editStopDialog(pendingStopRow, [&](QDialog* dialog) {
								auto* dwell = dialog->findChild<QLineEdit*>("stopEditorDwellEdit");
								dwell->setText(QString::number(pendingDwell));
								dwell->setFocus();
							}))
							facetFailure(facetOk, "save/reload", "focused modal dwell was not accepted");
						if (!triggerPendingSave()
							|| w.m_sceneModel.services[static_cast<std::size_t>(pendingServiceRow)]
									.stops[static_cast<std::size_t>(pendingStopRow)]
									.dwellSeconds
								!= pendingDwell)
							facetFailure(facetOk, "save/reload", "focused stop dwell text was not committed before Save");

						if (w.m_incidentDock) {
							w.m_incidentDock->show();
							w.m_incidentDock->raise();
						}
						w.m_scenarioListWidget->setCurrentRow(scenarioRow);
						QApplication::processEvents();
						w.m_scenarioDescriptionEdit->setText("pending scenario values");
						w.m_scenarioDescriptionEdit->setFocus();
						QApplication::processEvents();
						if (!triggerPendingSave() || !w.selectedScenario() || w.selectedScenario()->description != "pending scenario values")
							facetFailure(facetOk, "save/reload", "focused scenario description was not committed before Save");

						const auto& incidents = w.selectedScenarioIncidents();
						const auto pendingIncident = std::find_if(incidents.begin(), incidents.end(),
							[&](const SceneIncident& incident) { return incident.id == editedIncidentId; });
						if (pendingIncident == incidents.end()) {
							facetFailure(facetOk, "save/reload", "edited incident was unavailable for pending Save coverage");
						} else {
							const int incidentRow = static_cast<int>(std::distance(incidents.begin(), pendingIncident));
							if (auto* tabs = w.findChild<QTabWidget*>("scenarioEditorTabs"))
								tabs->setCurrentIndex(0);
							w.m_incidentListWidget->setCurrentRow(incidentRow);
							QApplication::processEvents();
							if (w.m_incidentHasEndSecondsCheck->isChecked())
								w.m_incidentHasEndSecondsCheck->setChecked(false);
							const int pendingIncidentEnd = static_cast<int>(pendingIncident->endSeconds) + 200;
							w.m_incidentEndSecondsEdit->setText(QString::number(pendingIncidentEnd));
							w.m_incidentEndSecondsEdit->setFocus();
							QApplication::processEvents();
							if (!w.m_incidentEndSecondsEdit->hasFocus())
								facetFailure(facetOk, "save/reload", "incident end field did not receive focus");
							if (w.m_sceneModel.routes.empty()) {
								facetFailure(facetOk, "save/reload", "route unavailable for blocked Run coverage");
							} else {
								const std::vector<std::string> validRoute = w.m_sceneModel.routes.front().blocks;
								w.m_sceneModel.routes.front().blocks.clear();
								w.m_runSceneAction->trigger();
								QApplication::processEvents();
								if (w.m_worker || !w.m_incidentPanel->selectedIncident()
									|| !w.m_incidentPanel->selectedIncident()->hasEndSeconds
									|| w.m_incidentPanel->selectedIncident()->endSeconds != pendingIncidentEnd)
									facetFailure(facetOk, "save/reload", "Run did not flush focused incident text before validation");
								w.m_sceneModel.routes.front().blocks = validRoute;
								w.refreshValidationPanel();
							}
						}
					}
					const std::string pendingSource = "e2e/source-data-pending-save.txt";
					w.m_trainUnitListWidget->setCurrentRow(trainUnitRow);
					w.m_trainUnitSourceDataEdit->setText(QString::fromStdString(pendingSource));
					w.m_trainUnitSourceDataEdit->setFocus();
					if (w.m_sceneModel.trainUnits[static_cast<std::size_t>(trainUnitRow)].sourceDataFile
						== pendingSource) {
						facetFailure(facetOk, "save/reload", "pending source edit committed before Save");
					} else if (!w.saveSceneToCurrentDir()) {
						facetFailure(facetOk, "save/reload", "pending editor values were not saved");
					} else {
						expectedTrainUnits = w.m_sceneModel.trainUnits;
						expectedCompositions = w.m_sceneModel.compositions;
						expectedServices = w.m_sceneModel.services;
						expectedStations = w.m_sceneModel.stations;
						expectedPassengers = w.m_sceneModel.passengers;
						expectedIncidents = w.selectedScenarioIncidents();
						if (const SceneScenario* scenario = w.selectedScenario())
							expectedEntranceDelays = scenario->entranceDelays;
						if (!w.openSceneDirectory(outScenePath)
							|| !sameTrainUnits(expectedTrainUnits, w.m_sceneModel.trainUnits)
							|| !sameCompositions(expectedCompositions, w.m_sceneModel.compositions)
							|| !sameServices(expectedServices, w.m_sceneModel.services)
							|| !sameStations(expectedStations, w.m_sceneModel.stations)
							|| !samePassengers(expectedPassengers, w.m_sceneModel.passengers)
							|| !sameIncidents(expectedIncidents, w.selectedScenarioIncidents())
							|| !w.selectedScenario()
							|| !sameEntranceDelays(expectedEntranceDelays,
								w.selectedScenario()->entranceDelays))
							facetFailure(facetOk, "save/reload", "pending editor values did not survive Save and reopen");
					}
					bool selectionRoundtripOk = !w.m_runSelection.hasExclusions();
					if (w.m_serviceOccurrenceTable) {
						for (int row = 0; row < w.m_serviceOccurrenceTable->rowCount(); ++row) {
							QTableWidgetItem* include = w.m_serviceOccurrenceTable->item(row, 0);
							selectionRoundtripOk = selectionRoundtripOk && include && include->checkState() == Qt::Checked;
						}
					}
					if (!selectionRoundtripOk)
						facetFailure(facetOk, "save/reload", "temporary occurrence selection was serialized or not reset on reopen");
					const QString bundlePath = outScenePath + ".egscene";
					QFile::remove(bundlePath);
					const SceneSaveResult bundleSave = saveSceneBundle(w.m_sceneModel, bundlePath.toStdString());
					if (!bundleSave.success() || !w.openSceneDirectory(bundlePath) || !w.selectedScenario()
						|| !sameStations(expectedStations, w.m_sceneModel.stations)
						|| !samePassengers(expectedPassengers, w.m_sceneModel.passengers)
						|| !sameEntranceDelays(expectedEntranceDelays,
							w.selectedScenario()->entranceDelays))
						facetFailure(facetOk, "save/reload", "entrance delays changed after bundle save and reopen");
				}
			}
			if (facetOk)
				std::fprintf(stdout, "E2E_EDITOR_SAVE_RELOAD_OK\n");
		}
	}

	if (ok) {
		auto* servicePages = w.findChild<QTabWidget*>("serviceEditorPages");
		const bool serviceStructure = servicePages && servicePages->count() == 2
			&& w.m_serviceOccurrenceTable && w.m_serviceOccurrenceTable->parentWidget()
			&& w.m_stopTableWidget && w.m_stopTimeBaseLabel && w.m_stopTimeBaseLabel->wordWrap();
		const bool signalDetails = w.signallingGroupDetails && w.signallingGroupDetails->isReadOnly()
			&& w.signallingGroupDetails->lineWrapMode() == QPlainTextEdit::WidgetWidth;
		const bool resultsStructure = w.m_runResultsDock
			&& w.m_runResultsDock->findChild<QToolButton*>("runResultsViewsButton")
			&& w.m_runResultsDock->findChildren<QAction*>(QRegularExpression("^resultView_")).size() == 12
			&& w.m_runResultsDock->findChildren<QPushButton*>(QRegularExpression("^resultView_")).size() == 2;
		if (!serviceStructure || !signalDetails || !resultsStructure)
			facetFailure(ok, "presentation", "editor tabs, wrapped signal details or result controls unavailable");
		for (QDockWidget* dock : w.findChildren<QDockWidget*>()) dock->hide();
		w.showNormal();
		w.resize(1280, 800);
		QApplication::processEvents();
		for (QDockWidget* dock : {w.m_caseSettingsDock, w.m_infrastructureDock, w.m_trainUnitDock,
				 w.m_compositionDock, w.m_serviceDock, w.m_incidentDock, w.m_passengerDock}) {
			const QFont normalFont = dock->font();
			for (const double scale : {1.0, 1.5}) {
				QFont font = normalFont;
				font.setPointSizeF(normalFont.pointSizeF() * scale);
				dock->setFont(font);
				dock->show();
				dock->raise();
				QApplication::processEvents();
				if (w.width() > 1280 || w.height() > 800)
					facetFailure(ok, "presentation", dock->objectName() + " enlarged the main window");
				if (dock == w.m_passengerDock) {
					auto* scroll = dock->findChild<QScrollArea*>("editorContentScroll");
					scroll->verticalScrollBar()->setValue(0);
					scroll->horizontalScrollBar()->setValue(0);
					const QPoint addCenter = w.m_addPassengerButton->mapTo(scroll->viewport(),
						w.m_addPassengerButton->rect().center());
					if (!scroll->viewport()->rect().contains(addCenter))
						facetFailure(ok, "presentation", "passenger Add action is outside the initial viewport");
				}
				dock->setFloating(true);
				dock->resize(750, 600);
				QApplication::processEvents();
				if (dock->width() > 750 || dock->height() > 600
					|| !dock->findChild<QScrollArea*>("editorContentScroll"))
					facetFailure(ok, "presentation", dock->objectName() + " cannot bound its floating content");
				dock->setFloating(false);
				dock->hide();
			}
			dock->setFont(normalFont);
		}
		if (ok) std::fprintf(stdout, "E2E_EDITOR_LAYOUT_OK\n");
	}
	if (ok && w.m_sceneLoaded) {
		// Visiting an editor must not change the case: tabbing through the fields, resizing, scrolling and reopening the docks
		// leave the dirty flag and the revision as they were.
		bool facetOk = true;
		bool dirtyBefore = w.m_sceneDirty;
		quint64 revisionBefore = w.m_sceneRevision;
		const auto expectUntouched = [&](const QDockWidget* dock, const char* step) {
			if (w.m_sceneDirty == dirtyBefore && w.m_sceneRevision == revisionBefore)
				return;
			facetFailure(facetOk, "untouched fields",
				QString("%1 changed the case during %2").arg(dock->objectName(), step));
			dirtyBefore = w.m_sceneDirty;
			revisionBefore = w.m_sceneRevision;
		};
		for (QDockWidget* dock : {w.m_caseSettingsDock, w.m_infrastructureDock, w.m_trainUnitDock,
				 w.m_compositionDock, w.m_serviceDock, w.m_incidentDock, w.m_passengerDock}) {
			dock->show();
			dock->raise();
			w.activateWindow();
			QApplication::processEvents();
			expectUntouched(dock, "opening");

			// Cell editors of the tables can be replaced while the fields are visited.
			QList<QPointer<QWidget>> fields;
			for (QWidget* child : dock->findChildren<QWidget*>()) {
				if (child->isVisible() && child->isEnabled() && (child->focusPolicy() & Qt::TabFocus)
					&& !qobject_cast<QScrollArea*>(child) && !qobject_cast<QAbstractSpinBox*>(child->parentWidget())
					&& !qobject_cast<QComboBox*>(child->parentWidget()))
					fields << child;
			}
			if (fields.isEmpty())
				facetFailure(facetOk, "untouched fields", dock->objectName() + " has no field to tab through");
			for (const QPointer<QWidget>& field : fields) {
				if (field)
					field->setFocus(Qt::TabFocusReason);
				QApplication::processEvents();
			}
			expectUntouched(dock, "focusing each field");

			// A key press that moves the focus is what a user does. A multi-line text field takes the Tab as text.
			if (!fields.isEmpty() && fields.first())
				fields.first()->setFocus(Qt::TabFocusReason);
			QStringList visited;
			for (int i = 0; i <= fields.size(); ++i) {
				QWidget* focused = QApplication::focusWidget();
				if (!focused)
					break;
				visited << QString::fromLatin1(focused->metaObject()->className()) + ":" + focused->objectName();
				auto* text = qobject_cast<QPlainTextEdit*>(focused);
				if (text && !text->isReadOnly() && !text->tabChangesFocus())
					break;
				QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
				QApplication::sendEvent(focused, &tab);
				QApplication::processEvents();
			}
			visited.removeDuplicates();
			if (visited.size() < 2)
				facetFailure(facetOk, "untouched fields", dock->objectName() + " did not pass the focus on with Tab");
			expectUntouched(dock, "tabbing through the fields");

			for (QScrollArea* scroll : dock->findChildren<QScrollArea*>("editorContentScroll")) {
				for (QScrollBar* bar : {scroll->verticalScrollBar(), scroll->horizontalScrollBar()}) {
					bar->setValue(bar->maximum());
					QApplication::processEvents();
					bar->setValue(bar->minimum());
					QApplication::processEvents();
				}
			}
			expectUntouched(dock, "scrolling");

			dock->setFloating(true);
			for (const QSize size : {QSize(750, 600), QSize(420, 320), QSize(1000, 700)}) {
				dock->resize(size);
				QApplication::processEvents();
			}
			dock->setFloating(false);
			for (const QSize size : {QSize(1000, 700), QSize(1500, 900), QSize(1280, 800)}) {
				w.resize(size);
				QApplication::processEvents();
			}
			expectUntouched(dock, "resizing");

			dock->hide();
			QApplication::processEvents();
			w.revealEditorDock(dock);
			QApplication::processEvents();
			expectUntouched(dock, "closing and reopening");
			dock->hide();
		}
		if (facetOk)
			std::fprintf(stdout, "E2E_EDITOR_UNTOUCHED_OK\n");
	}
	if (ok) {
		std::fprintf(stdout, "E2E_EDITOR_SMOKE_OK\n");
		std::fflush(stdout);
		QCoreApplication::exit(0);
		return;
	}
	std::fprintf(stderr, "E2E_EDITOR_SMOKE_FAIL: %s\n", failures.join(", ").toStdString().c_str());
	std::fflush(stderr);
	QCoreApplication::exit(1);
}
