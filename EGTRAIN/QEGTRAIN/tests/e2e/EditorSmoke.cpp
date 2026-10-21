#include "tests/e2e/MainWindowTestAccess.h"

#include "app/DispatchController.h"
#include "app/MainWindowSupport.h"
#include "app/MainWindowTestHooks.h"
#include "ui_MainWindow.h"

#include "diagrams/DiagramWindow.h"
#include "diagrams/TractionCurve.h"
#include "editors/CompositionPanel.h"
#include "editors/EntranceDelayPanel.h"
#include "editors/IncidentPanel.h"
#include "editors/InfrastructurePanel.h"
#include "editors/TrainUnitPanel.h"
#include "editors/TrainUnitSourceLinker.h"
#include "editors/PassengerPanel.h"
#include "editors/ScenarioPanel.h"
#include "editors/ServicePanel.h"
#include "editors/TimetableStopsPanel.h"
#include "editors/ValidationPanel.h"
#include "graphics/items/HighlightEffect.h"
#include "graphics/NetworkScene.h"
#include "recovery/RecoveryController.h"
#include "recovery/RecoveryStore.h"
#include "scene/SceneBundle.h"
#include "scene/SceneDiagnostic.h"
#include "scene/SceneHistory.h"
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
#include "simulation/SimulationWorker.h"
#include "util/TimeFormat.h"
#include "widgets/ColorChoiceButton.h"
#include "widgets/FocusWheel.h"
#include <QAbstractButton>
#include <QAbstractItemDelegate>
#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QChar>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGraphicsItem>
#include <QHeaderView>
#include <QHelpEvent>
#include <QIODevice>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QMessageBox>
#include <QMenuBar>
#include <QMetaObject>
#include <QObject>
#include <QPlainTextEdit>
#include <QPoint>
#include <QPointer>
#include <QPointF>
#include <QPushButton>
#include <QRect>
#include <QRectF>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScreen>
#include <QScrollArea>
#include <QSlider>
#include <QScrollBar>
#include <QSettings>
#include <QSize>
#include <QSpinBox>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVariant>
#include <QWheelEvent>
#include <QWidget>
#include <QWindow>
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
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

// QtGui exports this function for Qt Test. It delivers a mouse event as the window system does, so that
// QGuiApplication::mouseButtons() follows the button and a second press makes a double click.
Q_GUI_EXPORT void qt_handleMouseEvent(QWindow* window, const QPointF& local, const QPointF& global, Qt::MouseButtons state,
	Qt::MouseButton button, QEvent::Type type, Qt::KeyboardModifiers mods, int timestamp);

namespace {

// Writes CSV text to a path, all of it or nothing.
bool writeCsvFile(const QString& path, const std::string& content) {
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly))
		return false;
	const QByteArray bytes = QByteArray::fromStdString(content);
	return file.write(bytes) == bytes.size() && file.commit();
}

// The text of the tip that a widget shows when the pointer rests on it.
QString shownToolTip(QWidget* widget) {
	QHelpEvent help(QEvent::ToolTip, QPoint(4, 4), widget->mapToGlobal(QPoint(4, 4)));
	QApplication::sendEvent(widget, &help);
	const QString text = QToolTip::text();
	QToolTip::hideText();
	return text;
}

// Clicks Import Passengers with a folder given in place of the dialog that asks for one.
void importPassengersFrom(QPushButton* button, const QString& folder) {
	mainWindowTestHooks().passengerSource = [folder] { return folder; };
	button->click();
	QApplication::processEvents();
	mainWindowTestHooks().passengerSource = nullptr;
}

// The bytes that Save writes, which are what an edit authors. The parts of the model that the window derives are not in them.
std::string authoredSceneBytes(const SceneModel& model) {
	std::string bytes = encodeSceneMetadata(model).dump();
	for (const nlohmann::json& part : {encodeInfrastructure(model), encodeStations(model), encodeSignalling(model),
			 encodeRollingStock(model), encodeServices(model), encodeScenarios(model), encodeViews(model),
			 encodePassengers(model.passengers)})
		bytes += part.dump();
	return bytes;
}

// Answers the message box that opens during the next moments with the button.
void answerMessageBox(QObject* context, QMessageBox::StandardButton button) {
	QTimer::singleShot(25, context, [button]() {
		for (QWidget* widget : QApplication::topLevelWidgets()) {
			auto* dialog = qobject_cast<QMessageBox*>(widget);
			if (!dialog || !dialog->isVisible())
				continue;
			if (QAbstractButton* clicked = dialog->button(button))
				clicked->click();
			else
				dialog->done(button);
			break;
		}
	});
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
		if (!opened || !w.m_document->isLoaded()) {
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
			if (!reopened || !w.m_document->isLoaded() || QDir(w.m_session->m_sceneDir).absolutePath() != QDir(scenePath).absolutePath()) {
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
				if (!switched || !w.m_document->isLoaded() || QDir(w.m_session->m_sceneDir).absolutePath() != QDir(alternateScenePath).absolutePath()) {
					ok = false;
					failures << "scene: alternate scene did not open";
				}
				if (!w.m_presenter->allTrains.isEmpty() || !w.m_presenter->allArcs.isEmpty()) {
					ok = false;
					failures << "scene: legacy scene state not cleared on alternate";
				}
				if (w.m_infrastructureTable->currentRow() != -1 || w.m_presenter->m_previewHasSelectedTrack || w.m_document->isDirty()) {
					ok = false;
					failures << "scene: newly opened case has an implicit infrastructure selection";
				}
				w.refreshInfrastructurePanel();
				if (w.m_infrastructureTable->currentRow() != -1 || w.m_presenter->m_previewHasSelectedTrack || w.m_document->isDirty()) {
					ok = false;
					failures << "scene: a refresh selected a row that nobody selected";
				}
				w.m_infrastructureFacetCombo->setCurrentIndex(w.m_infrastructureFacetCombo->findData("tracks"));
				w.m_infrastructureTable->setCurrentCell(0, 0);
				const QString explicitlySelectedTrack = w.m_infrastructurePanel->selectionId();
				w.refreshInfrastructurePanel();
				if (explicitlySelectedTrack.isEmpty() || !w.m_presenter->m_previewHasSelectedTrack || w.m_document->isDirty()
					|| w.m_infrastructurePanel->selectionId() != explicitlySelectedTrack) {
					ok = false;
					failures << "scene: explicit track selection was lost or dirtied the scene";
				}
				bool restored = w.openSceneDirectory(scenePath);
				if (!restored || !w.m_document->isLoaded()) {
					ok = false;
					failures << "scene: scene did not reopen after alternate";
				}
				if (w.m_presenter->m_previewHasSelectedTrack || w.m_document->isDirty()) {
					ok = false;
					failures << "scene: explicit selection leaked into another case";
				}
			}
			if (QDir(w.m_session->m_sceneDir).absolutePath() != QDir(scenePath).absolutePath()) {
				ok = false;
				failures << "scene: primary scene not restored";
			}
		}
	}

	// An ampersand in an action text marks a mnemonic, so the Help entry has to be written with two to show one. The icon
	// text is the text as drawn, without the mnemonic markers and the trailing dots.
	{
		const QAction* privacyAction = w.findChild<QAction*>("actionPrivacyDiagnostics");
		if (!privacyAction || privacyAction->iconText() != QStringLiteral("Privacy & diagnostics")) {
			ok = false;
			failures << "help menu: the privacy entry is missing or does not show Privacy & diagnostics";
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
			const std::vector<SceneDiagnostic> originalDiagnostics = w.m_session->m_sceneDiagnostics;
			const std::string originalName = w.m_document->mutableModel().name;
			const bool originalDirty = w.m_document->isDirty();
			const quint64 originalRevision = w.m_document->revision();
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
				w.m_validation->updateStatus();
				w.m_scenarioPanel->updatePresentation();
				w.m_validation->refreshLoadedData();
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
				const bool dirtyBeforeToggle = w.m_document->isDirty();
				const quint64 revisionBeforeToggle = w.m_document->revision();
				detailsAction->setChecked(true);
				QApplication::processEvents();
				if (!w.m_loadedDataDock->isVisible() || !QSettings().value(kAdvancedDetailsKey).toBool())
					failures << "presentation: advanced details did not open or persist";
				if (w.m_caseNameEdit->text() != originalEditorText + " pending"
					|| w.m_document->mutableModel().name != originalName || w.m_document->isDirty() != dirtyBeforeToggle
					|| w.m_document->revision() != revisionBeforeToggle)
					failures << "presentation: advanced toggle replaced or committed focused case text";
				w.m_caseNameEdit->setText(originalEditorText);
			} else {
				failures << "presentation: case name editor is unavailable for focus preservation";
			}

			detailsAction->setChecked(false);
			if (w.m_loadedDataDock->isVisible() || w.m_validationDock->isVisible())
				failures << "presentation: normal mode did not close diagnostic docks";
			w.m_session->m_sceneDiagnostics.clear();
			applyPresentation();
			if (!w.m_validation->statusLabel()->text().isEmpty()
				|| w.m_caseReadinessLabel->text() != QStringLiteral("Ready to run")
				|| firstScenarioText().contains("Warning")
				|| firstScenarioText().contains("Invalid"))
				failures << "presentation: clean normal mode exposed diagnostic warnings";

			w.m_session->m_sceneDiagnostics = {syntheticDiagnostic(SceneSeverity::Warning,
				"E2E non-blocking warning")};
			applyPresentation();
			if (!w.m_validation->statusLabel()->text().isEmpty()
				|| w.m_caseReadinessLabel->text() != QStringLiteral("Ready to run")
				|| firstScenarioText().contains("Warning"))
				failures << "presentation: normal mode exposed a non-blocking warning";

			w.m_session->m_sceneDiagnostics = {syntheticDiagnostic(SceneSeverity::Error,
				"E2E invalid scenario")};
			applyPresentation();
			if (!w.m_validation->statusLabel()->text().contains("Not ready")
				|| w.m_caseReadinessLabel->text() != QStringLiteral("E2E invalid scenario")
				|| !firstScenarioText().contains("Invalid"))
				failures << "presentation: normal mode hid an actionable invalid scenario";

			detailsAction->setChecked(true);
			w.m_session->m_sceneDiagnostics = {syntheticDiagnostic(SceneSeverity::Warning,
				"E2E non-blocking warning")};
			applyPresentation();
			if (!w.m_validation->statusLabel()->text().contains("Validation:")
				|| !w.m_validation->statusLabel()->text().contains("warning")
				|| !firstScenarioText().contains("Warning"))
				failures << "presentation: advanced mode hid full warning details";

			w.m_session->m_sceneDiagnostics = originalDiagnostics;
			applyPresentation();
			if (w.m_document->mutableModel().name != originalName || w.m_document->isDirty() != originalDirty
				|| w.m_document->revision() != originalRevision)
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
		if (!w.m_document->isLoaded() || originalScenePath.isEmpty()) {
			facetFailure(facetOk, "new case", "the original scene was not available to restore");
		} else {
			discardConfirmation();
			w.m_session->newScene();
			QApplication::processEvents();
			if (!w.m_document->isLoaded() || !w.m_document->isDirty() || !w.m_session->m_sceneDir.isEmpty() || w.m_session->m_sceneIsBundle || !w.m_saveSceneAction || !w.m_saveSceneAction->isEnabled() || !w.m_saveSceneAsAction || !w.m_saveSceneAsAction->isEnabled() || !w.m_saveSceneAsFolderAction || !w.m_saveSceneAsFolderAction->isEnabled())
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
				const auto* caseNote = w.findChild<QLabel*>("caseSettingsNote");
				if (!caseNote || caseNote->text() != "Changes apply when editing finishes." || !caseNote->wordWrap()
					|| baseTimeEdit->placeholderText() != "HH:MM:SS")
					facetFailure(facetOk, "new case", "case settings lost the note about when changes apply or the base time format");
				nameEdit->setText("E2E New Case");
				descriptionEdit->setText("new case editor smoke");
				baseTimeEdit->setText("09:15:30");
				durationEdit->setValue(7200.0);
				bufferEdit->setValue(30.0);
				recoveryEdit->setValue(7.5);
				w.commitPendingCaseSettings();
				if (w.m_document->mutableModel().name != "E2E New Case" || w.m_document->mutableModel().description != "new case editor smoke" || w.m_document->mutableModel().baseTime != "09:15:30" || !w.m_document->mutableModel().settings.hasDuration || w.m_document->mutableModel().settings.durationSeconds != 7200.0 || !w.m_document->mutableModel().settings.hasBufferTime || w.m_document->mutableModel().settings.bufferTimeSeconds != 30.0 || !w.m_document->mutableModel().settings.hasRecoveryTime || w.m_document->mutableModel().settings.recoveryTimePercent != 7.5 || w.m_runController->m_startOffsetSeconds != 9 * 3600 + 15 * 60 + 30)
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
				if (!setInfrastructureCell("tracks", 0, 0, "e2e-yard") || w.m_document->mutableModel().tracks.size() != 2 || w.m_document->mutableModel().tracks[0].id != "e2e-main")
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
				const std::size_t blocksBeforeInvalidAdd = w.m_document->mutableModel().blocks.size();
				const bool orphanTrackAddDisabled = setInfrastructureCell("blocks", 1, 1, QString())
					&& !infrastructureAdd->isEnabled();
				infrastructureAdd->click();
				w.m_infrastructurePanel->addEntity(); // also exercise the handler's defensive check
				QApplication::processEvents();
				if (!orphanTrackAddDisabled || w.m_document->mutableModel().blocks.size() != blocksBeforeInvalidAdd)
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
						if (sceneSignals(w.m_document->mutableModel()).front().protectedSection
							!= protectedSection->itemData(blockChoice).toString().toStdString())
							facetFailure(facetOk, "stations/signalling", "signal section choice did not commit");
					}
				}
				// Every edit of the table rebuilds it and deletes the lists, so a list is fetched again after each edit.
				const auto areaSystemCombo = [&]() { return qobject_cast<QComboBox*>(infrastructureTable->cellWidget(0, 3)); };
				const auto areaTrackCombo = [&]() { return qobject_cast<QComboBox*>(infrastructureTable->cellWidget(0, 4)); };
				const auto areaLevelError = [&w]() {
					for (const SceneDiagnostic& diagnostic : w.m_session->m_sceneDiagnostics)
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
					if (!combo || combo->count() != extraItems + 1 + static_cast<int>(w.m_document->mutableModel().tracks.size()))
						return false;
					if (combo->itemText(extraItems) != QStringLiteral("(all tracks)") || !combo->itemData(extraItems).toString().isEmpty())
						return false;
					for (std::size_t index = 0; index < w.m_document->mutableModel().tracks.size(); ++index) {
						const int item = extraItems + 1 + static_cast<int>(index);
						const QString id = QString::fromStdString(w.m_document->mutableModel().tracks[index].id);
						if (combo->itemData(item).toString() != id || !combo->itemText(item).startsWith(id + QStringLiteral(" (blocks "))
							|| !combo->itemText(item).endsWith(QStringLiteral(" km)")))
							return false;
					}
					return true;
				};
				if (!addInfrastructureRow("signalling_areas"))
					facetFailure(facetOk, "stations/signalling", "signalling area row could not be added");
				const SceneBlockExtent networkExtent = sceneBlockExtent(buildSceneSectionInventory(w.m_document->mutableModel()), "");
				const bool signallingAreaStartsUnchosen = w.m_document->mutableModel().signallingAreas.size() == 1
					&& networkExtent.found && networkExtent.startKm == 0.0 && networkExtent.endKm > 0.0
					&& w.m_document->mutableModel().signallingAreas.front().startKm == networkExtent.startKm
					&& w.m_document->mutableModel().signallingAreas.front().endKm == networkExtent.endKm
					&& w.m_document->mutableModel().signallingAreas.front().level == kSignallingLevelUnset
					&& w.m_document->mutableModel().signallingAreas.front().trackId.empty();
				if (!signallingAreaStartsUnchosen)
					facetFailure(facetOk, "stations/signalling", "Add did not create a signalling area with the network extent and no chosen system");
				QString firstSceneError;
				for (const SceneDiagnostic& diagnostic : w.m_session->m_sceneDiagnostics)
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
				const bool networkAreaAuthored = w.m_document->mutableModel().signallingAreas.size() == 1
					&& w.m_document->mutableModel().signallingAreas.front().id == "e2e-signalling-area"
					&& w.m_document->mutableModel().signallingAreas.front().startKm == 0.25
					&& w.m_document->mutableModel().signallingAreas.front().endKm == 1.75
					&& w.m_document->mutableModel().signallingAreas.front().level == levelValue(SignallingLevel::VirtualCoupling)
					&& w.m_document->mutableModel().signallingAreas.front().trackId.empty()
					&& systemListHolds(chosenSystem, 0) && chosenSystem->currentText() == QStringLiteral("4 Virtual coupling")
					&& areaLevelError().isEmpty();
				if (!networkAreaAuthored)
					facetFailure(facetOk, "stations/signalling", "network-wide signalling area was not canonical");
				if (!chooseListItem(areaTrackCombo(), QStringLiteral("e2e-main"))
					|| w.m_document->mutableModel().signallingAreas.front().trackId != "e2e-main"
					|| !trackListHolds(areaTrackCombo(), 0) || areaTrackCombo()->currentData().toString() != QStringLiteral("e2e-main"))
					facetFailure(facetOk, "stations/signalling", "signalling area track-scope edit did not apply");
				const bool areaTrackRenameUpdated = setInfrastructureCell("tracks", 0, 0, "e2e-main-renamed")
					&& w.m_document->mutableModel().signallingAreas.front().trackId == "e2e-main-renamed"
					&& setInfrastructureCell("tracks", 0, 0, "e2e-main")
					&& w.m_document->mutableModel().signallingAreas.front().trackId == "e2e-main";
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
					w.m_document->mutableModel().signallingAreas.front().level = 9;
					w.m_infrastructurePanel->refreshTable();
					const QComboBox* staleSystem = areaSystemCombo();
					if (!staleSystem || staleSystem->itemText(0) != QStringLiteral("Invalid level 9")
						|| staleSystem->currentText() != QStringLiteral("Invalid level 9") || !systemListHolds(staleSystem, 1))
						facetFailure(facetOk, "stations/signalling", "a stale level was not listed as invalid above the six systems");
					if (!chooseListItem(areaSystemCombo(), levelValue(SignallingLevel::EtcsLevel2))
						|| w.m_document->mutableModel().signallingAreas.front().level != levelValue(SignallingLevel::EtcsLevel2)
						|| !systemListHolds(areaSystemCombo(), 0)
						|| areaSystemCombo()->currentText() != QString::fromStdString(signallingLevelName(levelValue(SignallingLevel::EtcsLevel2))))
						facetFailure(facetOk, "stations/signalling", "choosing a system did not replace the stale level");
					w.m_document->mutableModel().signallingAreas.front().trackId = "ghost";
					w.m_infrastructurePanel->refreshTable();
					const QComboBox* staleTrack = areaTrackCombo();
					if (!staleTrack || staleTrack->itemText(0) != QStringLiteral("Invalid track: ghost")
						|| staleTrack->currentText() != QStringLiteral("Invalid track: ghost") || !trackListHolds(staleTrack, 1))
						facetFailure(facetOk, "stations/signalling", "a stale track was not listed as invalid above the tracks");
					if (!chooseListItem(areaTrackCombo(), QString()) || !w.m_document->mutableModel().signallingAreas.front().trackId.empty()
						|| !trackListHolds(areaTrackCombo(), 0) || areaTrackCombo()->currentText() != QStringLiteral("(all tracks)"))
						facetFailure(facetOk, "stations/signalling", "choosing all tracks did not replace the stale track");
					// The values that the save and reload comparison expects.
					if (!chooseListItem(areaSystemCombo(), levelValue(SignallingLevel::VirtualCoupling))
						|| !chooseListItem(areaTrackCombo(), QStringLiteral("e2e-main"))
						|| w.m_document->mutableModel().signallingAreas.front().level != levelValue(SignallingLevel::VirtualCoupling)
						|| w.m_document->mutableModel().signallingAreas.front().trackId != "e2e-main")
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
						if (w.m_document->mutableModel().routes.empty()
							|| w.m_document->mutableModel().routes.front().blocks
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
				if (!recreateIncompleteRow("block_dependencies", w.m_document->mutableModel().blockDependencies)) {
					facetFailure(facetOk, "stations/signalling", "dependency row add/delete cycle failed");
				}
				const QString dependencyBlock = selectSectionCell("block_dependencies", 0, 0,
					"connection e2e-switch /");
				const QString dependencyDependsOn = selectSectionCell("block_dependencies", 0, 1,
					"base block e2e-main-block-inserted / track e2e-main");
				if (dependencyBlock.isEmpty() || dependencyDependsOn.isEmpty())
					facetFailure(facetOk, "stations/signalling", "dependency selectors did not expose catalog choices");
				if (!recreateIncompleteRow("single_track_restrictions",
						w.m_document->mutableModel().singleTrackRestrictions)) {
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
				if (!recreateIncompleteRow("station_boundaries", w.m_document->mutableModel().stationBoundaries)) {
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
						if (w.m_document->mutableModel().stationBoundaries.empty()
							|| w.m_document->mutableModel().stationBoundaries.front().hasExitBlock
							|| !w.m_document->mutableModel().stationBoundaries.front().exitBlock.empty())
							facetFailure(facetOk, "stations/signalling", "boundary (none) did not clear exit atomically");
					}
				}
				const bool m3TablesAuthored = w.m_document->mutableModel().stations.size() == 2 && w.m_document->mutableModel().stations[0].platforms.size() == 1 && w.m_document->mutableModel().stations[1].platforms.size() == 1 && sceneSignals(w.m_document->mutableModel()).size() == 1 && w.m_document->mutableModel().routes.size() == 1 && w.m_document->mutableModel().blockDependencies.size() == 1 && w.m_document->mutableModel().singleTrackRestrictions.size() == 1 && w.m_document->mutableModel().stationBoundaries.size() == 1;
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
						const ScenePlatform& platform = w.m_document->mutableModel().stations.front().platforms.front();
						if (!platform.hasLength || platform.lengthM != 123.75
							|| !platform.hasWidth || platform.widthM != 4.25)
							facetFailure(facetOk, "platform geometry", "public geometry edits did not set presence/value");
					}
				} else {
					facetFailure(facetOk, "platform geometry", "platform facet was unavailable");
				}
				const std::vector<SceneDiagnostic> nativeDiagnostics =
					buildInfrastructureAndSignallingFromScene(defaultRun(), w.m_document->mutableModel());
				bool nativeRouteMatches = !hasErrors(nativeDiagnostics) && defaultRun().infra.numRoutes == 1
					&& defaultRun().infra.routes.size() == 1 && defaultRun().infra.routes.front().N_Block_Sections == 3;
				if (nativeRouteMatches)
					for (std::size_t index = 0; index < expectedNativeRoute.size(); ++index)
						nativeRouteMatches = nativeRouteMatches
							&& defaultRun().infra.routes.front().sequence_of_block_sections[index].ID
								== expectedNativeRoute[index];
				if (!nativeRouteMatches)
					facetFailure(facetOk, "stations/signalling",
						"publicly authored switched route did not retain its order in native staging");
				w.refreshValidationPanel();
				const std::string renamedBlockId = "e2e-main-block-renamed";
				const bool blockRenamed = setInfrastructureCell("blocks", 1, 0,
					QString::fromStdString(renamedBlockId));
				const SceneSectionInventory renamedInventory = buildSceneSectionInventory(w.m_document->mutableModel());
				const auto resolvesThroughRenamedBlock = [&](const std::string& reference) {
					const SceneSectionDescriptor* section = renamedInventory.resolve(reference);
					return section
						&& (section->sourceBlockId == renamedBlockId || section->firstBlockId == renamedBlockId || section->secondBlockId == renamedBlockId);
				};
				const bool blockReferencesUpdated = blockRenamed
					&& std::any_of(w.m_document->mutableModel().blocks.begin(), w.m_document->mutableModel().blocks.end(),
						[&](const SceneBlock& block) { return block.id == renamedBlockId; })
					&& w.m_document->mutableModel().routes.size() == 1 && w.m_document->mutableModel().routes[0].blocks.size() == 3
					&& renamedInventory.resolve(w.m_document->mutableModel().routes[0].blocks[0])
					&& resolvesThroughRenamedBlock(w.m_document->mutableModel().routes[0].blocks[1])
					&& renamedInventory.resolve(w.m_document->mutableModel().routes[0].blocks[2])
					&& w.m_document->mutableModel().blockDependencies.size() == 1
					&& resolvesThroughRenamedBlock(w.m_document->mutableModel().blockDependencies[0].block)
					&& resolvesThroughRenamedBlock(w.m_document->mutableModel().blockDependencies[0].dependsOn)
					&& w.m_document->mutableModel().singleTrackRestrictions.size() == 1
					&& resolvesThroughRenamedBlock(w.m_document->mutableModel().singleTrackRestrictions[0].startBlock)
					&& resolvesThroughRenamedBlock(w.m_document->mutableModel().singleTrackRestrictions[0].protectedStartBlock)
					&& w.m_document->mutableModel().stationBoundaries.size() == 1
					&& resolvesThroughRenamedBlock(w.m_document->mutableModel().stationBoundaries[0].entranceBlock)
					&& !sceneSignals(w.m_document->mutableModel()).empty()
					&& resolvesThroughRenamedBlock(sceneSignals(w.m_document->mutableModel()).front().protectedSection);
				if (!blockReferencesUpdated)
					facetFailure(facetOk, "infrastructure", "block ID rename did not update decorated/composite references");
				if (w.m_addServiceButton && w.m_serviceListWidget && w.m_serviceRouteCombo && w.m_addStopButton) {
					w.m_addServiceButton->click();
					QApplication::processEvents();
					if (!w.m_document->mutableModel().services.empty())
						w.m_document->mutableModel().services.front().composition = "e2e-unresolved-composition";
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
						const bool platformMoveUpdatedStop = !w.m_document->mutableModel().services.empty()
							&& w.m_document->mutableModel().services.front().stops.size() == 2
							&& setInfrastructureCell("platforms", 1, 0, "e2e-station-a")
							&& w.m_document->mutableModel().services.front().stops.back().stationId == "e2e-station-a"
							&& setInfrastructureCell("platforms", 1, 0, "e2e-station-b")
							&& w.m_document->mutableModel().services.front().stops.back().stationId == "e2e-station-b";
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
					if (facetName == "tracks") actualSize = w.m_document->mutableModel().tracks.size();
					else if (facetName == "nodes") actualSize = w.m_document->mutableModel().nodes.size();
					else if (facetName == "arcs") actualSize = w.m_document->mutableModel().arcs.size();
					else if (facetName == "blocks") actualSize = w.m_document->mutableModel().blocks.size();
					else if (facetName == "connections") actualSize = w.m_document->mutableModel().connections.size();
					else if (facetName == "stations") actualSize = w.m_document->mutableModel().stations.size();
					else if (facetName == "platforms") {
						actualSize = 0;
						for (const auto& station : w.m_document->mutableModel().stations)
							actualSize += station.platforms.size();
					} else if (facetName == "signals") actualSize = sceneSignals(w.m_document->mutableModel()).size();
					else if (facetName == "routes") actualSize = w.m_document->mutableModel().routes.size();
					if (actualSize != expectedSize)
						facetFailure(facetOk, "delete integrity", QString("referenced %1 delete was not refused").arg(label));
				};
				refuseInfrastructureDelete("tracks", 0, 0, w.m_document->mutableModel().tracks.size(), "track");
				refuseInfrastructureDelete("nodes", 0, 0, w.m_document->mutableModel().nodes.size(), "node");
				if (chooseBlockTrack("e2e-main"))
					refuseInfrastructureDelete("blocks", 0, 0, w.m_document->mutableModel().blocks.size(), "block");
				else
					facetFailure(facetOk, "delete integrity", "block facet filter unavailable");
				refuseInfrastructureDelete("connections", 0, 0, w.m_document->mutableModel().connections.size(), "connection");
				refuseInfrastructureDelete("stations", 0, 0, w.m_document->mutableModel().stations.size(), "station");
				std::size_t platformCount = 0;
				for (const auto& station : w.m_document->mutableModel().stations)
					platformCount += station.platforms.size();
				refuseInfrastructureDelete("platforms", 0, 1, platformCount, "platform");
				refuseInfrastructureDelete("signals", 0, 0, sceneSignals(w.m_document->mutableModel()).size(), "signal");
				refuseInfrastructureDelete("routes", 0, 0, w.m_document->mutableModel().routes.size(), "route");
				expectedTracks = w.m_document->mutableModel().tracks;
				expectedNodes = w.m_document->mutableModel().nodes;
				expectedArcs = w.m_document->mutableModel().arcs;
				expectedBlocks = w.m_document->mutableModel().blocks;
				expectedConnections = w.m_document->mutableModel().connections;
				expectedRoutes = w.m_document->mutableModel().routes;
				expectedBlockDependencies = w.m_document->mutableModel().blockDependencies;
				expectedSingleTrackRestrictions = w.m_document->mutableModel().singleTrackRestrictions;
				expectedStationBoundaries = w.m_document->mutableModel().stationBoundaries;
				expectedSignallingAreas = w.m_document->mutableModel().signallingAreas;
				expectedStations = w.m_document->mutableModel().stations;
				expectedSignals = sceneSignals(w.m_document->mutableModel());
				expectedNewCaseServices = w.m_document->mutableModel().services;
				expectedNewCaseIncidents = defaultScenario(w.m_document->mutableModel())
					? defaultScenario(w.m_document->mutableModel())->incidents
					: std::vector<SceneIncident>();
				if (loadTrackPreview(w.m_document->mutableModel()).lines.size() < 2)
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
			const SceneSaveResult folderSave = ::saveScene(w.m_document->mutableModel(), newCaseFolder.toStdString());
			const SceneSaveResult bundleSave = saveSceneBundle(w.m_document->mutableModel(), newCaseBundle.toStdString());
			if (!folderSave.success() || !bundleSave.success())
				facetFailure(facetOk, "new case", "folder or bundle save failed");
			std::string expectedName = "E2E New Case";
			std::string expectedDescription = "new case editor smoke";
			const auto verifyNewCase = [&]() {
				const SceneScenario* scenario = defaultScenario(static_cast<const SceneModel&>(w.m_document->mutableModel()));
				return w.m_document->isLoaded() && !w.m_document->isDirty() && w.m_document->mutableModel().schemaVersion == 1 && w.m_document->mutableModel().name == expectedName && w.m_document->mutableModel().description == expectedDescription && w.m_document->mutableModel().baseTime == "09:15:30" && w.m_document->mutableModel().settings.hasDuration && w.m_document->mutableModel().settings.durationSeconds == 7200.0 && w.m_document->mutableModel().settings.hasBufferTime && w.m_document->mutableModel().settings.bufferTimeSeconds == 30.0 && w.m_document->mutableModel().settings.hasRecoveryTime && w.m_document->mutableModel().settings.recoveryTimePercent == 7.5 && w.m_document->mutableModel().defaultScenarioId == "baseline" && scenario && scenario->id == "baseline" && scenario->name == "Baseline" && sameIncidents(scenario->incidents, expectedNewCaseIncidents) && scenario->entranceDelays.empty() && (expectedTracks.empty() || sameInfrastructure(w.m_document->mutableModel(), expectedTracks, expectedNodes, expectedArcs, expectedBlocks, expectedConnections)) && (expectedRoutes.empty() || sameBlockReferences(w.m_document->mutableModel(), expectedRoutes, expectedBlockDependencies, expectedSingleTrackRestrictions, expectedStationBoundaries)) && (expectedSignallingAreas.empty() || sameSignallingAreas(w.m_document->mutableModel().signallingAreas, expectedSignallingAreas)) && (expectedStations.empty() || sameStations(w.m_document->mutableModel().stations, expectedStations)) && (expectedSignals.empty() || sameSignals(sceneSignals(w.m_document->mutableModel()), expectedSignals)) && (expectedNewCaseServices.empty() || sameServices(w.m_document->mutableModel().services, expectedNewCaseServices));
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
			const SceneSaveResult updatedBundleSave = saveSceneBundle(w.m_document->mutableModel(), newCaseBundle.toStdString());
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
				const bool cleanBeforeNew = !w.m_document->isDirty()
					&& w.m_document->mutableModel().name == expectedName
					&& nameEdit->text() == pendingNewName
					&& QApplication::focusWidget() == nameEdit;
				if (!cleanBeforeNew)
					facetFailure(facetOk, "new case", "New action precondition was not a clean scene with focused pending text");
				else {
					cancelConfirmation();
					w.m_newSceneAction->trigger();
					QApplication::processEvents();
					if (w.m_session->m_sceneDir != QFileInfo(newCaseFolder).absoluteFilePath()
						|| !w.m_document->isDirty() || w.m_document->mutableModel().name != pendingNewName.toStdString()
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
				|| w.m_document->mutableModel().settings.hasDuration || w.m_document->mutableModel().settings.hasBufferTime
				|| w.m_document->mutableModel().settings.hasRecoveryTime)
				facetFailure(facetOk, "new case", "canonical scene with absent optional settings did not load");
			else if (descriptionEdit && w.m_caseSettingsDock) {
				w.m_caseSettingsDock->show();
				w.m_caseSettingsDock->raise();
				descriptionEdit->setText("metadata-only edit");
				w.commitCaseSettings();
				const bool flagsRemainAbsent = !w.m_document->mutableModel().settings.hasDuration
					&& !w.m_document->mutableModel().settings.hasBufferTime && !w.m_document->mutableModel().settings.hasRecoveryTime;
				if (!flagsRemainAbsent || !w.m_saveSceneAction || !w.m_saveSceneAction->isEnabled())
					facetFailure(facetOk, "new case", "metadata-only edit did not keep optional settings absent before Save");
				else {
					w.m_saveSceneAction->trigger();
					if (w.m_document->isDirty() || w.m_document->mutableModel().settings.hasDuration || w.m_document->mutableModel().settings.hasBufferTime
						|| w.m_document->mutableModel().settings.hasRecoveryTime
						|| !w.openSceneDirectory(absentSettingsFolder)
						|| w.m_document->mutableModel().settings.hasDuration || w.m_document->mutableModel().settings.hasBufferTime
						|| w.m_document->mutableModel().settings.hasRecoveryTime)
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
	if (w.m_document->isLoaded()) {
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
			for (const auto& service : w.m_document->mutableModel().services) {
				for (std::size_t first = 0; first < service.stops.size() && !passengerService; ++first) {
					for (std::size_t last = first + 1; last < service.stops.size(); ++last) {
						if (service.stops[first].stationId == service.stops[last].stationId)
							continue;
						const bool knownOrigin = std::any_of(w.m_document->mutableModel().stations.begin(),
							w.m_document->mutableModel().stations.end(), [&](const SceneStation& station) {
								return station.id == service.stops[first].stationId;
							});
						const bool knownDestination = std::any_of(w.m_document->mutableModel().stations.begin(),
							w.m_document->mutableModel().stations.end(), [&](const SceneStation& station) {
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
				e2ePassengerId = sceneUniquePassengerId(w.m_document->mutableModel(), "e2e_passenger");
				const int passengerCountBefore = passengerList->count();
				passengerAdd->click();
				QApplication::processEvents();
				if (passengerList->count() != passengerCountBefore + 1) {
					facetFailure(facetOk, "passenger", "Add did not create a passenger row");
				} else {
					passengerId->setText(QString::fromStdString(e2ePassengerId));
					if (!commitLineEdit(passengerId))
						facetFailure(facetOk, "passenger", "passenger ID edit did not commit");
					e2eJourneyId = sceneUniquePassengerJourneyId(w.m_document->mutableModel(), "e2e_journey");
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
						e2eLegId = sceneUniquePassengerLegId(w.m_document->mutableModel(), "e2e_leg");
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
						const std::string secondLegId = sceneUniquePassengerLegId(w.m_document->mutableModel(), "e2e_leg_second");
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
						const std::string temporaryJourneyId = sceneUniquePassengerJourneyId(w.m_document->mutableModel(), "e2e_temp_journey");
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
						const std::string temporaryPassengerId = sceneUniquePassengerId(w.m_document->mutableModel(), "e2e_temp_passenger");
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
								const QTableWidgetItem* statusItem = importResults->item(row, 3);
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
							if (importResults->isHidden() || w.m_passengerPanel->importResultHeading()->isHidden())
								facetFailure(facetOk, "passenger import", "result table or its heading was hidden after an import");
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
									&& w.m_passengerJourneyOriginCombo->currentText().startsWith("Invalid: missing-origin")
									&& w.m_passengerJourneyOriginCombo
										   ->itemData(w.m_passengerJourneyOriginCombo->currentIndex(), Qt::ToolTipRole)
										   .toString()
										   .startsWith("Station missing-origin");
								const bool legStateAccurate = unresolvedLeg
									? legService->currentText().startsWith("Invalid: missing-service")
										&& legService->itemData(legService->currentIndex(), Qt::ToolTipRole).toString().startsWith("Service missing-service")
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
							const auto passengersBeforeFailedImport = w.m_document->mutableModel().passengers;
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
								if (!samePassengers(passengersBeforeFailedImport, w.m_document->mutableModel().passengers)
									|| importResults->rowCount() != 1 || !importResults->item(0, 3)
									|| importResults->item(0, 3)->text() != "Rejected")
									facetFailure(facetOk, "passenger import", "a failed import changed the scene or hid its rejection");
							}
						}

						const auto infrastructureDiagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), w.m_document->mutableModel());
						const auto operationsDiagnostics = buildOperationsFromScene(defaultRun(),
							w.m_document->mutableModel(), w.m_runSelection.scenarioId(), SceneRunSelection());
						const auto stagedPassenger = std::find_if(defaultRun().passengers.begin(), defaultRun().passengers.end(),
							[&](const Passenger& passenger) { return passenger.ID == e2ePassengerId; });
						bool nativePassengerOk = !hasErrors(infrastructureDiagnostics)
							&& !hasErrors(operationsDiagnostics) && stagedPassenger != defaultRun().passengers.end();
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
						if (!w.m_document->mutableModel().stations.empty() && !w.m_document->mutableModel().stations.front().platforms.empty()) {
							const ScenePlatform& sourcePlatform = w.m_document->mutableModel().stations.front().platforms.front();
							const double expectedLength = sourcePlatform.hasLength ? sourcePlatform.lengthM : 100.0;
							const double expectedWidth = sourcePlatform.hasWidth ? sourcePlatform.widthM : 2.5;
							const int expectedCapacity = static_cast<int>((expectedLength * expectedWidth)
								/ (3.14159 * std::pow(0.8, 2)) * 0.8);
							const auto stagedPlatform = std::find_if(defaultRun().platforms.begin(), defaultRun().platforms.end(),
								[&](const StationPlatform& platform) { return platform.ID == sourcePlatform.id; });
							if (stagedPlatform != defaultRun().platforms.end())
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
	if (w.m_document->isLoaded()) {
		if (!w.m_validation->tree() || w.m_validation->tree()->topLevelItemCount() <= 0) {
			ok = false;
			failures << "validation: loaded data tree empty";
		} else if (w.m_validation->tree()->topLevelItem(0)->childCount() <= 0) {
			ok = false;
			failures << "validation: loaded data tree lacks drilldown rows";
		}
		if (hasErrors(w.m_session->m_sceneDiagnostics)) {
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
			&& w.m_runSelection.scenarioId() == w.m_document->mutableModel().defaultScenarioId;
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
				if (w.m_incidentPanel->emptyStateLabel()->text() != "This scenario has no incidents. Use Add Incident."
					|| w.m_entranceDelayPanel->emptyStateLabel()->text() != "This scenario has no entrance delays. Use Add.") {
					ok = false;
					failures << "scenario: a blank scenario did not say that it has no incidents and no entrance delays";
				}
				w.m_duplicateScenarioButton->click();
				QApplication::processEvents();
			}
			if (w.m_scenarioListWidget->count() != originalScenarioCount + 2) {
				ok = false;
				failures << "scenario: blank or duplicate did not add an isolated scenario";
			}
			w.m_scenarioListWidget->setCurrentRow(0);
			QApplication::processEvents();
			if (w.m_runSelection.scenarioId() != w.m_document->mutableModel().defaultScenarioId) {
				ok = false;
				failures << "scenario: selection did not return to canonical default";
			}
			const int countBeforeDefaultDelete = w.m_scenarioListWidget->count();
			w.m_deleteScenarioButton->click();
			w.m_scenarioPanel->deleteScenario();
			QApplication::processEvents();
			if (w.m_scenarioListWidget->count() != countBeforeDefaultDelete
				|| w.m_runSelection.scenarioId() != w.m_document->mutableModel().defaultScenarioId
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
				|| w.m_runSelection.scenarioId() != w.m_document->mutableModel().defaultScenarioId
				|| !defaultScenario(static_cast<const SceneModel&>(w.m_document->mutableModel()))) {
				ok = false;
				failures << "scenario: non-default deletion did not select a valid default";
			}
			if (w.m_scenarioListWidget->count() > originalScenarioCount) {
				for (int row = 0; row < w.m_scenarioListWidget->count(); ++row) {
					if (w.m_document->mutableModel().scenarios[static_cast<std::size_t>(row)].id == w.m_document->mutableModel().defaultScenarioId)
						continue;
					w.m_scenarioListWidget->setCurrentRow(row);
					acceptConfirmation();
					w.m_deleteScenarioButton->click();
					QApplication::processEvents();
					break;
				}
			}
		}
		bool explorerOk = w.m_validation->tree() && w.m_validation->tree()->topLevelItemCount() > 0;
		QTreeWidgetItem* caseRoot = explorerOk ? w.m_validation->tree()->topLevelItem(0) : nullptr;
		explorerOk = explorerOk && caseRoot->text(0) == "Case Study"
			&& caseRoot->text(3) != "Invalid" && caseRoot->text(3) != "Failed";
		for (const QString& category : {QStringLiteral("Source path"), QStringLiteral("Canonical schema version"),
				 QStringLiteral("Runtime"), QStringLiteral("Results")}) {
			const QList<QTreeWidgetItem*> rows = w.m_validation->tree()->findItems(
				category, Qt::MatchExactly | Qt::MatchRecursive, 0);
			QTreeWidgetItem* row = rows.isEmpty() ? nullptr : rows.front();
			if (!row || row->text(3).isEmpty())
				explorerOk = false;
		}
		if (w.m_session->m_sceneIsBundle) {
			const QList<QTreeWidgetItem*> bundleRows = w.m_validation->tree()->findItems(
				"Bundle format version", Qt::MatchExactly | Qt::MatchRecursive, 0);
			QTreeWidgetItem* bundleRow = bundleRows.isEmpty() ? nullptr : bundleRows.front();
			if (!bundleRow || bundleRow->text(1) != "1")
				explorerOk = false;
		}
		QTreeWidgetItem* trainTarget = nullptr;
		for (QTreeWidgetItemIterator it(w.m_validation->tree()); *it && !trainTarget; ++it) {
			if ((*it)->data(0, ValidationPanel::kTargetTypeRole).toString() == "train_unit")
				trainTarget = *it;
		}
		if (!trainTarget || !w.m_trainUnitListWidget) {
			explorerOk = false;
		} else {
			w.m_validation->activateItem(trainTarget);
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
			&& w.m_stopTableWidget->horizontalHeaderItem(3)->text().simplified() == "Planned arrival";
		const bool hasPlannedDeparture = w.m_stopTableWidget && w.m_stopTableWidget->columnCount() == 5
			&& w.m_stopTableWidget->horizontalHeaderItem(4)->text().simplified() == "Planned departure";
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
		expectedCompositions = w.m_document->mutableModel().compositions;
		expectedTrainUnits = w.m_document->mutableModel().trainUnits;
		expectedServices = w.m_document->mutableModel().services;
		expectedIncidents = w.selectedScenarioIncidents();
	}

	std::string editedTrainUnitId;
	if (!w.m_document->isLoaded() || !w.m_trainUnitListWidget) {
		bool facetOk = false;
		facetFailure(facetOk, "train unit", "scene or train-unit list unavailable");
	} else {
		bool facetOk = true;
		const int originalCount = w.m_trainUnitListWidget->count();
		const auto headings = w.m_trainUnitDock->findChildren<QLabel*>("rollingStockSectionHeading");
		const QStringList sectionTitles{"Rolling stock unit ID", "Rolling stock engine", "Rolling stock unit characteristics"};
		const QTabWidget* unitTabs = w.m_trainUnitPanel->tabs();
		// Showing a page raises it, which changes the order of the children, so each heading is found by its title.
		const auto headingTitled = [&headings](const QString& title) -> const QLabel* {
			const auto found = std::find_if(headings.begin(), headings.end(), [&title](const QLabel* heading) { return heading->text() == title; });
			return found == headings.end() ? nullptr : *found;
		};
		const QLabel* idHeading = headingTitled(sectionTitles[0]);
		const QLabel* engineHeading = headingTitled(sectionTitles[1]);
		const QLabel* characteristicsHeading = headingTitled(sectionTitles[2]);
		bool layoutOk = headings.size() == sectionTitles.size() && idHeading && engineHeading && characteristicsHeading;
		for (int i = 0; layoutOk && i < headings.size(); ++i) {
			layoutOk = headings[i]->font().bold() && headings[i]->font().pointSizeF() > w.m_trainUnitIdEdit->font().pointSizeF();
		}
		// The unit id is above the tabs. The engine, its table and the Plot button are on the Traction page, in that
		// order, and the characteristics are on the Physical page.
		layoutOk = layoutOk && unitTabs->count() == 2 && unitTabs->tabText(0) == "Traction" && unitTabs->tabText(1) == "Physical"
			&& idHeading->y() < unitTabs->y() && unitTabs->widget(0)->isAncestorOf(engineHeading)
			&& unitTabs->widget(0)->isAncestorOf(w.m_trainUnitTractionTable)
			&& unitTabs->widget(0)->isAncestorOf(w.m_plotTrainUnitTractionButton)
			&& unitTabs->widget(1)->isAncestorOf(characteristicsHeading)
			&& unitTabs->widget(1)->isAncestorOf(w.m_trainUnitPhysicalEdits[0])
			&& w.m_trainUnitTractionTable->y() > engineHeading->y()
			&& w.m_plotTrainUnitTractionButton->y() > w.m_trainUnitTractionTable->y();
		const bool wasDirty = w.m_document->isDirty();
		w.m_document->setDirty(false);
		const QSize oldSize = w.m_trainUnitDock->size();
		const QSize oldTableSize = w.m_trainUnitTractionTable->size();
		const bool wasFloating = w.m_trainUnitDock->isFloating();
		w.m_trainUnitDock->setFloating(true);
		w.m_trainUnitDock->resize(1400, 1200);
		QApplication::processEvents();
		layoutOk = layoutOk && !w.m_document->isDirty() && w.m_trainUnitTractionTable->width() > oldTableSize.width()
			&& w.m_trainUnitTractionTable->height() > oldTableSize.height();
		const QString layoutShot = QDir(qEnvironmentVariable("QEGTRAIN_E2E_OUT", QDir::tempPath()))
									   .filePath("rolling-stock-expanded.png");
		layoutOk = w.m_trainUnitDock->grab().save(layoutShot, "PNG") && layoutOk;
		w.m_trainUnitDock->setFloating(wasFloating);
		w.m_trainUnitDock->resize(oldSize);
		w.m_document->setDirty(wasDirty);
		w.m_trainUnitListWidget->setCurrentRow(-1);
		layoutOk = layoutOk && !w.m_trainUnitIdEdit->isEnabled() && !w.m_trainUnitSourceDataEdit->isEnabled()
			&& !w.m_trainUnitSourceTractionEdit->isEnabled() && !w.m_trainUnitTractionTable->isEnabled()
			&& w.m_trainUnitPanel->emptyStateLabel()->text() == "Select a rolling stock unit or use Add Unit."
			&& unitTabs->isHidden();
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
			const std::string initialTrainUnitId = w.m_document->mutableModel().trainUnits[originalCount].id;
			w.m_compositionListWidget->setCurrentRow(0);
			acceptUnitChoice(QString::fromStdString(initialTrainUnitId));
			w.m_compositionPanel->addUnit();
			QApplication::processEvents();
			bool assignedInitialId = false;
			if (w.m_compositionListWidget->currentRow() >= 0
				&& w.m_compositionListWidget->currentRow() < static_cast<int>(w.m_document->mutableModel().compositions.size())) {
				const auto& units = w.m_document->mutableModel().compositions[w.m_compositionListWidget->currentRow()].units;
				assignedInitialId = std::find(units.begin(), units.end(), initialTrainUnitId) != units.end();
			}
			if (!assignedInitialId)
				facetFailure(facetOk, "train unit", "composition assignment did not apply under the initial id");

			const std::string initialIdBeforeDuplicateRename = w.m_document->mutableModel().trainUnits[originalCount].id;
			const std::vector<SceneComposition> compositionsBeforeDuplicateRename = w.m_document->mutableModel().compositions;
			if (originalCount <= 0) {
				facetFailure(facetOk, "train unit", "scene lacks another train unit id for duplicate rename coverage");
			} else if (w.m_trainUnitIdEdit) {
				const std::string conflictingId = w.m_document->mutableModel().trainUnits.front().id;
				w.m_trainUnitIdEdit->setText(QString::fromStdString(conflictingId));
				w.m_trainUnitPanel->commitIdEdit();
				const bool selectedIdUnchanged = w.m_document->mutableModel().trainUnits[originalCount].id == initialIdBeforeDuplicateRename;
				const bool editorIdRestored = w.m_trainUnitIdEdit->text().trimmed().toStdString() == initialIdBeforeDuplicateRename;
				const bool referencesUnchanged = sameCompositions(compositionsBeforeDuplicateRename, w.m_document->mutableModel().compositions);
				if (!selectedIdUnchanged || !editorIdRestored || !referencesUnchanged)
					facetFailure(facetOk, "train unit", "duplicate-id rename changed the selected unit or composition references");
			}

			editedTrainUnitId = sceneUniqueTrainUnitId(w.m_document->mutableModel(), "e2e_train_unit");
			if (!w.m_trainUnitIdEdit) {
				facetFailure(facetOk, "train unit", "id editor unavailable");
			} else {
				w.m_trainUnitIdEdit->setText(QString::fromStdString(editedTrainUnitId));
				w.m_trainUnitPanel->commitIdEdit();
			}
			bool oldReferencePresent = false;
			bool newReferencePresent = false;
			for (const auto& composition : w.m_document->mutableModel().compositions) {
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
				const SceneTrainUnit& editedUnit = w.m_document->mutableModel().trainUnits[originalCount];
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
			const SceneTrainPhysical& committedPhysical = w.m_document->mutableModel().trainUnits[originalCount].physical;
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
			const int initialCurveRows = static_cast<int>(w.m_document->mutableModel().trainUnits[originalCount].tractionCurve.size());
			w.m_trainUnitPanel->addTractionRow();
			if (static_cast<int>(w.m_document->mutableModel().trainUnits[originalCount].tractionCurve.size()) != initialCurveRows + 1) {
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
				if (static_cast<int>(w.m_document->mutableModel().trainUnits[originalCount].tractionCurve.size()) != initialCurveRows + 2)
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
					if (static_cast<int>(w.m_document->mutableModel().trainUnits[originalCount].tractionCurve.size()) != initialCurveRows + 1)
						facetFailure(facetOk, "train unit", "traction row delete did not apply");
					else if (w.m_document->mutableModel().trainUnits[originalCount].tractionCurve[initialCurveRows] != tractionValues)
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
				const std::string duplicateId = w.m_document->mutableModel().trainUnits[duplicateRow].id;
				w.m_compositionListWidget->setCurrentRow(0);
				acceptUnitChoice(QString::fromStdString(duplicateId));
				w.m_compositionPanel->addUnit();
				QApplication::processEvents();
				const int referencedCount = w.m_trainUnitListWidget->count();
				w.m_trainUnitPanel->deleteUnit();
				const bool duplicateStillPresent = std::any_of(w.m_document->mutableModel().trainUnits.begin(),
					w.m_document->mutableModel().trainUnits.end(), [&](const SceneTrainUnit& unit) { return unit.id == duplicateId; });
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
					|| std::any_of(w.m_document->mutableModel().trainUnits.begin(), w.m_document->mutableModel().trainUnits.end(),
						[&](const SceneTrainUnit& unit) { return unit.id == duplicateId; }))
					facetFailure(facetOk, "train unit", "unreferenced-unit delete did not apply");
			}
		}
		if (!w.m_document->mutableModel().trainUnits.empty()) {
			w.m_trainUnitListWidget->setCurrentRow(0);
			if (!w.m_trainUnitSourceDataEdit || w.m_trainUnitSourceDataEdit->text().toStdString() != w.m_document->mutableModel().trainUnits.front().sourceDataFile
				|| !w.m_trainUnitSourceTractionEdit || w.m_trainUnitSourceTractionEdit->text().toStdString() != w.m_document->mutableModel().trainUnits.front().sourceTractionFile)
				facetFailure(facetOk, "train unit", "source references are not visible and unchanged");
		}
		expectedCompositions = w.m_document->mutableModel().compositions;
		if (facetOk)
			std::fprintf(stdout, "E2E_EDITOR_TRAIN_UNIT_OK\n");
	}

	if (!w.m_document->mutableModel().trainUnits.empty()) {
		bool facetOk = true;
		w.m_trainUnitListWidget->setCurrentRow(0);
		sourceLinkUnitId = w.m_document->mutableModel().trainUnits.front().id;
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
		link.acceptedPhysical = w.m_document->mutableModel().trainUnits.front().physical;
		link.acceptedTraction = w.m_document->mutableModel().trainUnits.front().tractionCurve;
		w.m_trainUnitSourceLinker->m_links[sourceLinkUnitId] = link;
		w.m_trainUnitSourceLinker->refreshWatches();
		writeSource(sourcePhysicalPath, "151000 0 0 36.111111111111 0.75 1.45 0.004 0.75 71\n");
		writeSource(sourceTractionPath, "0 40 210000 0 0\n");
		settle();
		if (w.m_document->mutableModel().trainUnits.front().physical.length_m != 71.0
			|| w.m_document->mutableModel().trainUnits.front().tractionCurve != std::vector<std::array<double, 5>>{{0, 40, 210000, 0, 0}})
			facetFailure(facetOk, "source links", "complete file changes did not apply");
		const quint64 acceptedRevision = w.m_document->revision();
		w.m_trainUnitSourceLinker->m_links.at(sourceLinkUnitId).tractionDeferred = true;
		writeSource(sourceTractionPath, "0 40 210000 0 0\n");
		bool unchangedPrompted = false;
		answerReload("Keep local", &unchangedPrompted);
		w.m_trainUnitSourceLinker->m_pendingPaths.insert(sourceTractionPath);
		w.m_trainUnitSourceLinker->processChanges();
		settle();
		if (w.m_document->revision() != acceptedRevision || unchangedPrompted
			|| w.m_trainUnitSourceLinker->m_links.at(sourceLinkUnitId).tractionDeferred)
			facetFailure(facetOk, "source links", "unchanged deferred save prompted or dirtied the scene");
		writeSource(sourceTractionPath, "0 40 220000 0 0\npartial\n");
		settle();
		if (w.m_document->mutableModel().trainUnits.front().tractionCurve.front()[2] != 210000
			|| !w.m_trainUnitSourceLinker->m_links.at(sourceLinkUnitId).tractionStatus.startsWith("Rejected"))
			facetFailure(facetOk, "source links", "malformed file replaced accepted data");
		QFile::remove(sourceTractionPath);
		settle();
		writeSource(sourceTractionPath, "0 40 220000 0 0\n");
		settle();
		if (w.m_document->mutableModel().trainUnits.front().tractionCurve.front()[2] != 220000
			|| !w.m_trainUnitSourceLinker->watchedFiles().contains(sourceTractionPath))
			facetFailure(facetOk, "source links", "deletion/recreation did not recover the watch");
		// The focused editor must commit before comparing with the accepted source.
		w.m_trainUnitDock->show();
		w.m_trainUnitDock->raise();
		w.activateWindow();
		w.m_trainUnitPanel->tabs()->setCurrentIndex(1);
		w.m_trainUnitPhysicalEdits[8]->setFocus();
		QApplication::processEvents();
		if (!w.m_trainUnitPhysicalEdits[8]->hasFocus())
			facetFailure(facetOk, "source links", "length editor did not receive focus");
		w.m_trainUnitPhysicalEdits[8]->findChild<QLineEdit*>()->setText("73");
		writeSource(sourcePhysicalPath, "151000 0 0 36.111111111111 0.75 1.45 0.004 0.75 72\n");
		answerReload("Keep local");
		w.m_trainUnitSourceLinker->processFile(sourcePhysicalPath, false);
		if (w.m_document->mutableModel().trainUnits.front().physical.length_m != 73.0)
			facetFailure(facetOk, "source links", "focused local edit was overwritten");
		const quint64 keptRevision = w.m_document->revision();
		settle();
		if (w.m_document->revision() != keptRevision)
			facetFailure(facetOk, "source links", "rejected content was reapplied");
		answerReload("Reload");
		w.m_trainUnitSourceLinker->retry(sourceLinkUnitId, false);
		w.m_trainUnitSourceLinker->processChanges();
		if (w.m_document->mutableModel().trainUnits.front().physical.length_m != 72.0)
			facetFailure(facetOk, "source links", "explicit retry did not reload");
		// A duplicate shares values, not the authority to watch an external file.
		w.m_trainUnitPanel->duplicateUnit();
		const std::string duplicateId = w.m_document->mutableModel().trainUnits[1].id;
		if (w.m_trainUnitSourceLinker->m_links.count(duplicateId))
			facetFailure(facetOk, "source links", "duplicate inherited a live link");
		w.m_trainUnitSourceLinker->m_links[duplicateId] = w.m_trainUnitSourceLinker->m_links.at(sourceLinkUnitId);
		writeSource(sourceTractionPath, "0 40 230000 0 0\n");
		settle();
		if (w.m_document->mutableModel().trainUnits[0].tractionCurve.front()[2] != 230000
			|| w.m_document->mutableModel().trainUnits[1].tractionCurve.front()[2] != 230000)
			facetFailure(facetOk, "source links",
				QString("shared source values %1 / %2; status %3")
					.arg(w.m_document->mutableModel().trainUnits[0].tractionCurve.front()[2])
					.arg(w.m_document->mutableModel().trainUnits[1].tractionCurve.front()[2])
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
	if (!w.m_document->isLoaded() || !w.m_compositionListWidget || expectedCompositions.empty()) {
		bool facetOk = false;
		facetFailure(facetOk, "composition", "scene or composition list unavailable");
	} else {
		bool facetOk = true;
		const int originalCount = w.m_compositionListWidget->count();
		int assignedCompositionRow = -1;
		for (int row = 0; row < static_cast<int>(w.m_document->mutableModel().compositions.size()); ++row) {
			const std::string& compositionId = w.m_document->mutableModel().compositions[static_cast<std::size_t>(row)].id;
			if (std::any_of(w.m_document->mutableModel().services.begin(), w.m_document->mutableModel().services.end(),
					[&compositionId](const SceneService& service) { return service.composition == compositionId; })) {
				assignedCompositionRow = row;
				break;
			}
		}
		if (assignedCompositionRow < 0) {
			facetFailure(facetOk, "composition", "no service-assigned composition available for rename coverage");
		} else {
			w.m_compositionListWidget->setCurrentRow(-1);
			if (w.m_compositionPanel->emptyStateLabel()->text() != "Select a composition or use Add Composition.")
				facetFailure(facetOk, "composition", "no composition selected did not say what to select");
			w.m_compositionListWidget->setCurrentRow(assignedCompositionRow);
			if (!w.m_compositionPanel->emptyStateLabel()->text().isEmpty())
				facetFailure(facetOk, "composition", "a selected composition still shows the empty-state message");
			// the selected unit surfaces its source files and a plottable traction curve
			if (w.m_compositionUnitsListWidget && w.m_compositionUnitsListWidget->count() > 0 && w.m_compositionUnitsListWidget->item(0)) {
				w.m_compositionUnitsListWidget->setCurrentRow(0);
				const SceneTrainUnit* tractionUnit =
					sceneTrainUnitById(w.m_document->mutableModel(), w.m_compositionUnitsListWidget->item(0)->text().toStdString());
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
					w.m_document->mutableModel().compositions[static_cast<std::size_t>(assignedCompositionRow)].id;
				const std::string duplicateCompositionId =
					w.m_document->mutableModel().compositions[static_cast<std::size_t>(assignedCompositionRow + 1)].id;
				w.m_compositionListWidget->setCurrentRow(assignedCompositionRow);
				if (!w.m_compositionIdEdit) {
					facetFailure(facetOk, "composition", "id editor unavailable");
				} else {
					w.m_compositionIdEdit->setText(QString());
					w.m_compositionPanel->commitIdEdit();
					const bool emptyRejected = w.m_document->mutableModel().compositions[static_cast<std::size_t>(assignedCompositionRow)].id
							== originalAssignedCompositionId
						&& w.m_compositionIdEdit->text() == QString::fromStdString(originalAssignedCompositionId)
						&& w.m_compositionListWidget->currentRow() == assignedCompositionRow;
					w.m_compositionIdEdit->setText(QString::fromStdString(duplicateCompositionId));
					w.m_compositionPanel->commitIdEdit();
					const bool duplicateRejected = w.m_document->mutableModel().compositions[static_cast<std::size_t>(assignedCompositionRow)].id
							== originalAssignedCompositionId
						&& w.m_compositionIdEdit->text() == QString::fromStdString(originalAssignedCompositionId)
						&& w.m_compositionListWidget->currentRow() == assignedCompositionRow;
					if (!emptyRejected || !duplicateRejected)
						facetFailure(facetOk, "composition", "empty or duplicate ID was not rejected with the prior text restored");

					editedCompositionId = sceneUniqueCompositionId(w.m_document->mutableModel(), "e2e_composition");
					w.m_compositionIdEdit->setText(QString::fromStdString(editedCompositionId));
					w.m_compositionPanel->commitIdEdit();
					int migratedServiceCount = 0;
					int staleServiceCount = 0;
					for (const auto& service : w.m_document->mutableModel().services) {
						if (service.composition == editedCompositionId)
							++migratedServiceCount;
						if (service.composition == originalAssignedCompositionId)
							++staleServiceCount;
					}
					if (w.m_document->mutableModel().compositions[static_cast<std::size_t>(assignedCompositionRow)].id != editedCompositionId
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
				|| !std::any_of(w.m_document->mutableModel().compositions.begin(), w.m_document->mutableModel().compositions.end(),
					[&](const SceneComposition& composition) {
						return composition.id == editedCompositionId;
					}))
				facetFailure(facetOk, "composition", "service-referenced composition delete was not refused");
			w.m_compositionPanel->addComposition();
			const std::string temporaryCompositionId = w.m_document->mutableModel().compositions.back().id;
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
			for (int row = 0; row < static_cast<int>(w.m_document->mutableModel().compositions.size()); ++row) {
				if (w.m_document->mutableModel().compositions[row].id == editedCompositionId) {
					editedRow = row;
					break;
				}
			}
			if (editedRow < 0 || w.m_document->mutableModel().compositions[editedRow].units != expectedCompositions[static_cast<std::size_t>(assignedCompositionRow)].units)
				facetFailure(facetOk, "composition", "edited composition was not retained");
			if (facetOk)
				std::fprintf(stdout, "E2E_EDITOR_COMPOSITION_OK\n");
		}
	}

	std::string editedServiceId;
	if (!w.m_document->isLoaded() || !w.m_serviceListWidget || expectedServices.empty() || editedCompositionId.empty()) {
		bool facetOk = false;
		facetFailure(facetOk, "service", "scene or service setup unavailable");
	} else {
		bool facetOk = true;
		const double precisePerformancePercent = 99.12345678901234;
		const double preciseMaximumSpeedKmh = 876.5432109876543;
		const int originalCount = w.m_serviceListWidget->count();
		w.m_serviceListWidget->setCurrentRow(0);
		const std::string unknownCategory = "Regional heritage / custom";
		w.m_document->mutableModel().services[0].category = unknownCategory;
		expectedServices[0].category = unknownCategory;
		w.m_servicePanel->updateDetail();
		if (!w.m_serviceCategoryCombo || w.m_serviceCategoryCombo->currentData().toString().toStdString() != unknownCategory
			|| !w.m_serviceCategoryCombo->currentText().startsWith("Unknown: "))
			facetFailure(facetOk, "service", "unknown category is not preserved in the chooser");
		const std::string storedColor = "#3c8dd2";
		w.m_document->mutableModel().services[0].visualizationColor = storedColor;
		expectedServices[0].visualizationColor = storedColor;
		w.m_servicePanel->updateDetail();
		if (!w.m_serviceColorButton || !w.m_serviceColorButton->isEnabled()
			|| w.m_serviceColorButton->valueText() != QString::fromStdString(storedColor)
			|| w.m_serviceColorButton->swatchColor() != QColor(0x3C, 0x8D, 0xD2))
			facetFailure(facetOk, "service colour", "stored colour is not shown in the editor");
		const auto originalService = w.m_document->mutableModel().services[0];
		const auto sourceRoute = std::find_if(w.m_document->mutableModel().routes.begin(), w.m_document->mutableModel().routes.end(),
			[&](const SceneRoute& route) { return route.id == originalService.route; });
		if (sourceRoute != w.m_document->mutableModel().routes.end()) {
			SceneRoute duplicateRoute = *sourceRoute;
			duplicateRoute.id += "-same-description";
			w.m_document->mutableModel().routes.push_back(duplicateRoute);
			w.m_servicePanel->updateDetail();
			w.m_serviceRouteCombo->setCurrentIndex(w.m_serviceRouteCombo->findData(QString::fromStdString(duplicateRoute.id)));
			if (w.m_document->mutableModel().services[0].route != duplicateRoute.id
				|| !shownToolTip(w.m_serviceRouteCombo).contains("not scheduled calls"))
				facetFailure(facetOk, "service routes", "descriptive route selection lost canonical identity or context");
			w.m_document->mutableModel().services[0].route = "missing-route";
			w.m_servicePanel->updateDetail();
			if (w.m_serviceRouteCombo->currentData().toString() != "missing-route"
				|| !sameStop(w.m_document->mutableModel().services[0].stops.front(), originalService.stops.front())
				|| !w.m_stopTableWidget->item(0, 0)->toolTip().contains("unresolved"))
				facetFailure(facetOk, "service routes", "missing route or invalid stop was silently replaced");
			w.m_document->mutableModel().services[0] = originalService;
			w.m_document->mutableModel().routes.pop_back();
			w.m_servicePanel->updateDetail();
		}
		w.m_servicePanel->duplicateService();
		if (w.m_serviceListWidget->count() != originalCount + 1) {
			facetFailure(facetOk, "service", "duplicate did not apply");
		} else {
			w.m_serviceListWidget->setCurrentRow(1);
			SceneService categoryExpected = w.m_document->mutableModel().services[1];
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
					if (index < 0 || !sameServices({categoryExpected}, {w.m_document->mutableModel().services[1]}))
						facetFailure(facetOk, "service", "category selection changed other service settings");
				}
			}
			if (w.m_serviceColorButton) {
				auto* defaultColorButton = w.m_serviceColorButton->findChild<QPushButton*>("serviceColorDefaultButton");
				const bool dirtyBeforeColor = w.m_document->isDirty();
				// an edit changes the colour and marks the scene modified; an equal value changes nothing
				const auto colorStep = [&](const char* what, const std::function<void()>& action,
										   const std::string& expectedColor) {
					w.m_document->setDirty(false);
					const quint64 revisionBefore = w.m_document->revision();
					const SceneService before = w.m_document->mutableModel().services[1];
					action();
					SceneService expected = before;
					expected.visualizationColor = expectedColor;
					const bool changed = expectedColor != before.visualizationColor;
					if (!sameServices({expected}, {w.m_document->mutableModel().services[1]}) || w.m_document->isDirty() != changed
						|| (w.m_document->revision() != revisionBefore) != changed)
						facetFailure(facetOk, "service colour",
							QString("%1 gave the wrong model or dirty state").arg(QLatin1String(what)));
				};
				// the comparison used below sees a colour that differs alone
				SceneService withColor = w.m_document->mutableModel().services[1];
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
					w.m_document->mutableModel().services[1].visualizationColor = "not-a-colour";
					w.m_servicePanel->updateDetail();
					w.m_serviceListWidget->setCurrentRow(0);
					w.m_serviceListWidget->setCurrentRow(1);
					if (w.m_serviceColorButton->valueText() != "Invalid: not-a-colour"
						|| w.m_document->mutableModel().services[1].visualizationColor != "not-a-colour")
						facetFailure(facetOk, "service colour", "invalid stored colour is not shown as invalid and kept");
					colorStep("choosing a colour over an invalid text", [&]() { w.m_serviceColorButton->chooseColor(QColor(0x3C, 0x8D, 0xD2)); }, storedColor);
					w.m_document->mutableModel().services[1].visualizationColor = "not-a-colour";
					w.m_servicePanel->updateDetail();
					colorStep("Default over an invalid text", [&]() { defaultColorButton->click(); }, "");
					// the colour stays on the service for the save and reload below
					colorStep("choosing the final colour", [&]() { w.m_serviceColorButton->chooseColor(QColor(0x3C, 0x8D, 0xD2)); }, storedColor);
					if (w.m_serviceColorButton->valueText() != QString::fromStdString(storedColor))
						facetFailure(facetOk, "service colour", "chosen colour is not shown");
				}
				w.m_document->setDirty(dirtyBeforeColor);
			} else {
				facetFailure(facetOk, "service colour", "colour control unavailable");
			}
			const std::string oldServiceId = w.m_document->mutableModel().services[1].id;
			int referenceScenarioRow = -1;
			std::size_t temporaryDelayCount = 0;
			int temporaryDelayRow = -1;
			std::size_t temporaryIncidentCount = 0;
			for (int scenarioRow = 0; scenarioRow < static_cast<int>(w.m_document->mutableModel().scenarios.size()); ++scenarioRow) {
				if (w.m_document->mutableModel().scenarios[static_cast<std::size_t>(scenarioRow)].id == w.m_runSelection.scenarioId()) {
					referenceScenarioRow = scenarioRow;
					break;
				}
			}
			if (referenceScenarioRow >= 0) {
				SceneScenario& referenceScenario = w.m_document->mutableModel().scenarios[static_cast<std::size_t>(referenceScenarioRow)];
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
				incident.id = sceneUniqueIncidentId(w.m_document->mutableModel(), "e2e_service_rename_incident");
				incident.type = "train_breakdown";
				incident.target = oldServiceId;
				incident.endSeconds = 1.0;
				referenceScenario.incidents.push_back(incident);
				++temporaryIncidentCount;
			}
			w.m_runSelection.setExcluded(SceneServiceOccurrence{oldServiceId, 1}, true);
			editedServiceId = sceneUniqueServiceId(w.m_document->mutableModel(), "e2e_service");
			if (!w.m_serviceIdEdit) {
				facetFailure(facetOk, "service", "id editor unavailable");
			} else {
				const std::string duplicateServiceId = w.m_document->mutableModel().services.front().id;
				const auto referencesStillOld = [&]() {
					if (referenceScenarioRow < 0)
						return true;
					const SceneScenario& scenario = w.m_document->mutableModel().scenarios[static_cast<std::size_t>(referenceScenarioRow)];
					const bool delayStillOld = temporaryDelayCount == 0
						|| (!scenario.entranceDelays.empty() && scenario.entranceDelays.back().serviceId == oldServiceId);
					const bool incidentStillOld = temporaryIncidentCount == 0
						|| (!scenario.incidents.empty() && scenario.incidents.back().target == oldServiceId);
					return delayStillOld && incidentStillOld
						&& w.m_runSelection.isExcluded(SceneServiceOccurrence{oldServiceId, 1});
				};
				w.m_serviceIdEdit->setText(QString());
				w.m_servicePanel->commitIdEdit();
				const bool emptyRejected = w.m_document->mutableModel().services[1].id == oldServiceId
					&& w.m_serviceIdEdit->text() == QString::fromStdString(oldServiceId)
					&& referencesStillOld();
				w.m_serviceIdEdit->setText(QString::fromStdString(duplicateServiceId));
				w.m_servicePanel->commitIdEdit();
				const bool duplicateRejected = w.m_document->mutableModel().services[1].id == oldServiceId
					&& w.m_serviceIdEdit->text() == QString::fromStdString(oldServiceId)
					&& referencesStillOld();
				if (!emptyRejected || !duplicateRejected)
					facetFailure(facetOk, "service", "empty or duplicate ID rename changed canonical references");
				w.m_serviceIdEdit->setText(QString::fromStdString(editedServiceId));
				w.m_servicePanel->commitIdEdit();
				if (w.m_document->mutableModel().services.size() <= 1 || w.m_document->mutableModel().services[1].id != editedServiceId)
					facetFailure(facetOk, "service", "id edit did not apply");
			}
			bool referencesUpdated = true;
			if (referenceScenarioRow >= 0) {
				const SceneScenario& referenceScenario = w.m_document->mutableModel().scenarios[static_cast<std::size_t>(referenceScenarioRow)];
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
				|| !std::any_of(w.m_document->mutableModel().services.begin(), w.m_document->mutableModel().services.end(),
					[&](const SceneService& service) { return service.id == editedServiceId; }))
				facetFailure(facetOk, "service", "operation-referenced service delete was not refused");
			if (referenceScenarioRow >= 0) {
				SceneScenario& referenceScenario = w.m_document->mutableModel().scenarios[static_cast<std::size_t>(referenceScenarioRow)];
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
			const bool dirtyBeforeTyping = w.m_document->isDirty();
			const QString typingFailure = [&]() -> QString {
				if (!w.m_servicePerformancePercentEdit || !w.m_servicePerformancePercentEdit->isEnabled())
					return QStringLiteral("running performance editor unavailable");
				QLineEdit* performanceField = w.m_servicePerformancePercentEdit->findChild<QLineEdit*>();
				if (!performanceField)
					return QStringLiteral("running performance editor has no text field");
				if (w.m_serviceListWidget->currentRow() != 1 || w.m_document->mutableModel().services.size() < 2)
					return QStringLiteral("running performance editor is not showing service row 1");
				const double valueBeforeTyping = w.m_document->mutableModel().services[1].performancePercent;
				if (w.m_servicePerformancePercentEdit->value() != valueBeforeTyping)
					return QString("running performance editor shows %1 but the service has %2")
						.arg(w.m_servicePerformancePercentEdit->value())
						.arg(valueBeforeTyping);
				if (valueBeforeTyping == 9.0 || valueBeforeTyping == 93.0 || valueBeforeTyping == 94.0)
					return QString("service running performance %1 equals a value typed by the check").arg(valueBeforeTyping);
				const auto typeDigits = [&](const QString& digits) -> QString {
					const double valueBefore = w.m_document->mutableModel().services[1].performancePercent;
					w.m_document->setDirty(false);
					const quint64 revisionBefore = w.m_document->revision();
					w.m_servicePerformancePercentEdit->selectAll();
					QString typedSoFar;
					for (const QChar digit : digits) {
						QKeyEvent keyPress(QEvent::KeyPress, digit.unicode(), Qt::NoModifier, QString(digit));
						QApplication::sendEvent(performanceField, &keyPress);
						typedSoFar += digit;
						const double appliedValue = w.m_document->mutableModel().services[1].performancePercent;
						if (appliedValue != valueBefore || w.m_document->revision() != revisionBefore || w.m_document->isDirty())
							return QString("running performance was applied while typing \"%1\": the service has %2 instead of %3 "
										   "and the scene revision moved by %4")
								.arg(typedSoFar)
								.arg(appliedValue)
								.arg(valueBefore)
								.arg(w.m_document->revision() - revisionBefore);
					}
					if (w.m_servicePerformancePercentEdit->text() != digits)
						return QString("running performance editor shows \"%1\" after typing \"%2\"")
							.arg(w.m_servicePerformancePercentEdit->text(), digits);
					return QString();
				};
				QString failure = typeDigits(QStringLiteral("94"));
				if (!failure.isEmpty())
					return failure;
				const quint64 revisionBeforeReturn = w.m_document->revision();
				QKeyEvent returnPress(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
				QKeyEvent returnRelease(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier);
				QApplication::sendEvent(w.m_servicePerformancePercentEdit, &returnPress);
				QApplication::sendEvent(w.m_servicePerformancePercentEdit, &returnRelease);
				if (w.m_document->mutableModel().services[1].performancePercent != 94.0 || w.m_document->revision() != revisionBeforeReturn + 1
					|| !w.m_document->isDirty())
					return QString("Return did not apply the typed running performance once: the service has %1 instead of 94 "
								   "and the scene revision moved by %2")
						.arg(w.m_document->mutableModel().services[1].performancePercent)
						.arg(w.m_document->revision() - revisionBeforeReturn);
				failure = typeDigits(QStringLiteral("93"));
				if (!failure.isEmpty())
					return failure;
				const quint64 revisionBeforeCommit = w.m_document->revision();
				w.m_servicePanel->commitPendingEditors();
				if (w.m_document->mutableModel().services[1].performancePercent != 93.0 || w.m_document->revision() != revisionBeforeCommit + 1
					|| !w.m_document->isDirty())
					return QString("committing pending settings did not apply the typed running performance once: the service has %1 "
								   "instead of 93 and the scene revision moved by %2")
						.arg(w.m_document->mutableModel().services[1].performancePercent)
						.arg(w.m_document->revision() - revisionBeforeCommit);
				if (w.m_servicePerformancePercentEdit->value() != 93.0 || w.m_servicePerformancePercentEdit->text() != "93")
					return QString("running performance editor shows \"%1\" after the pending commit of 93")
						.arg(w.m_servicePerformancePercentEdit->text());
				return QString();
			}();
			w.m_document->setDirty(dirtyBeforeTyping);
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
				&& w.m_document->mutableModel().services[1].operatingCode == "1723"
				&& w.m_document->mutableModel().services[1].performancePercent == 95.5
				&& w.m_document->mutableModel().services[1].hasRepeat && w.m_document->mutableModel().services[1].headwaySeconds == 1800.0
				&& w.m_document->mutableModel().services[1].hasRepeatCount && w.m_document->mutableModel().services[1].repeatCount == 3
				&& w.m_document->mutableModel().services[1].hasOperatingCodeStep && w.m_document->mutableModel().services[1].operatingCodeStep == 2
				&& w.m_document->mutableModel().services[1].hasMaximumSpeed && w.m_document->mutableModel().services[1].maximumSpeedKmh == 100.0;
			if (!occurrencePreviewOk)
				facetFailure(facetOk, "service", "occurrence preview did not derive the requested codes or fields");
			w.m_document->mutableModel().services[1].performancePercent = precisePerformancePercent;
			w.m_document->mutableModel().services[1].hasMaximumSpeed = true;
			w.m_document->mutableModel().services[1].maximumSpeedKmh = preciseMaximumSpeedKmh;
			w.m_servicePanel->updateDetail();
			if (w.m_serviceMaximumSpeedKmhEdit->text() != QString::number(preciseMaximumSpeedKmh, 'g', 6))
				facetFailure(facetOk, "service", "speed editor did not use compact display precision");
			w.m_serviceMaximumSpeedKmhEdit->setFocus();
			w.m_serviceMaximumSpeedKmhEdit->interpretText();
			w.m_serviceMaximumSpeedKmhEdit->clearFocus();
			w.m_servicePanel->commitPendingEditors();
			if (w.m_document->mutableModel().services[1].performancePercent != precisePerformancePercent
				|| w.m_document->mutableModel().services[1].maximumSpeedKmh != preciseMaximumSpeedKmh)
				facetFailure(facetOk, "service", "untouched precise service settings changed during pending commit");
			const SceneService serviceBeforeSelection = w.m_document->mutableModel().services[1];
			const bool dirtyBeforeSelection = w.m_document->isDirty();
			w.m_servicePanel->selectNoneOccurrences();
			if (w.m_runSelection.selectedInPeriodCount(w.m_document->mutableModel(), w.serviceOccurrenceDuration()) != 0)
				facetFailure(facetOk, "service", "empty GUI selection counted all services in period");
			if (w.m_serviceOccurrenceTable) {
				for (int previewRow = 0; previewRow < w.m_serviceOccurrenceTable->rowCount(); ++previewRow) {
					QTableWidgetItem* include = w.m_serviceOccurrenceTable->item(previewRow, 0);
					if (include && include->data(Qt::UserRole).toString().toStdString() == editedServiceId
						&& include->data(Qt::UserRole + 1).toInt() == 1)
						include->setCheckState(Qt::Checked);
				}
			}
			const bool oneOccurrenceSelected = w.m_runSelection.selectedCount(w.m_document->mutableModel(), w.serviceOccurrenceDuration()) == 1
				&& w.m_runSelection.selectedOccurrences(w.m_document->mutableModel(), w.serviceOccurrenceDuration())
						.count(SceneServiceOccurrence{editedServiceId, 1})
					== 1;
			const bool selectionDidNotDirty = w.m_document->isDirty() == dirtyBeforeSelection
				&& sameServices({serviceBeforeSelection}, {w.m_document->mutableModel().services[1]});
			if (!oneOccurrenceSelected || !selectionDidNotDirty)
				facetFailure(facetOk, "service", "occurrence selection changed canonical fields or dirty state");
		}
		w.m_servicePanel->addService();
		if (w.m_serviceListWidget->count() != originalCount + 2) {
			facetFailure(facetOk, "service", "add did not apply");
		} else {
			const std::string unreferencedServiceId = w.m_document->mutableModel().services.back().id;
			if (w.m_selectNoneOccurrencesButton) {
				w.m_selectNoneOccurrencesButton->click();
				QApplication::processEvents();
			}
			const bool occurrenceExclusionCreated = w.m_runSelection.isExcluded(
				SceneServiceOccurrence{unreferencedServiceId, 1});
			acceptConfirmation();
			w.m_servicePanel->deleteService();
			if (w.m_serviceListWidget->count() != originalCount + 1
				|| std::any_of(w.m_document->mutableModel().services.begin(), w.m_document->mutableModel().services.end(),
					[&](const SceneService& service) { return service.id == unreferencedServiceId; })
				|| (occurrenceExclusionCreated
					&& w.m_runSelection.isExcluded(SceneServiceOccurrence{unreferencedServiceId, 1})))
				facetFailure(facetOk, "service", "unreferenced service delete did not prune occurrence exclusions");
		}
		int editedRow = -1;
		for (int row = 0; row < static_cast<int>(w.m_document->mutableModel().services.size()); ++row) {
			if (w.m_document->mutableModel().services[row].id == editedServiceId) {
				editedRow = row;
				break;
			}
		}
		if (editedRow < 0) {
			facetFailure(facetOk, "service", "edited service was not retained");
		} else {
			const SceneService& edited = w.m_document->mutableModel().services[editedRow];
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

	if (!w.m_document->isLoaded() || !w.m_serviceListWidget || !w.m_stopTableWidget || editedServiceId.empty()) {
		bool facetOk = false;
		facetFailure(facetOk, "timetable", "scene or edited service unavailable");
	} else {
		bool facetOk = true;
		int serviceRow = -1;
		for (int row = 0; row < static_cast<int>(w.m_document->mutableModel().services.size()); ++row) {
			if (w.m_document->mutableModel().services[row].id == editedServiceId) {
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
			const int originalStopCount = static_cast<int>(w.m_document->mutableModel().services[serviceRow].stops.size());
			if (originalStopCount <= 0)
				facetFailure(facetOk, "timetable", "no baseline stop available for move coverage");
			if (originalStopCount > 0) {
				const SceneStop before = w.m_document->mutableModel().services[serviceRow].stops.front();
				const bool dirtyBefore = w.m_document->isDirty();
				w.m_stopTableWidget->setCurrentCell(originalStopCount - 1, 0);
				for (const int mode : {1, 0}) {
					w.m_stopTimeModeCombo->setCurrentIndex(mode);
					const auto& selected = w.m_document->mutableModel().services[serviceRow].stops.back();
					const QString expected = selected.hasPlannedArrival
						? QString::fromStdString(formatPlannedTime(selected.plannedArrivalSeconds,
							  mode == 1, baseTimeToSeconds(w.m_document->mutableModel().baseTime)))
						: QStringLiteral("\u2014");
					if (w.m_stopTableWidget->currentRow() != originalStopCount - 1
						|| w.m_stopTableWidget->item(originalStopCount - 1, 3)->text() != expected
						|| w.m_document->isDirty() != dirtyBefore || w.m_moveStopDownButton->isEnabled()
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
					if (arrival->text().toStdString() != formatPlannedTime(90.0, true, baseTimeToSeconds(w.m_document->mutableModel().baseTime)))
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
					else if (const auto* error = dialog->findChild<QLabel*>("stopEditorErrorLabel");
						!error->isVisible() || error->property("dialogStatus").toString() != "error" || !error->styleSheet().isEmpty())
						facetFailure(facetOk, "timetable", "the stop dialog error is not an error callout of the stylesheet");
					dialog->findChild<QPushButton*>("stopEditorCancelButton")->click();
				});
				timer.start(0);
				QMetaObject::invokeMethod(w.m_stopTableWidget, "cellClicked", Qt::DirectConnection,
					Q_ARG(int, 0), Q_ARG(int, 0));
				if (!sameStop(before, w.m_document->mutableModel().services[serviceRow].stops.front()) || dirtyBefore != w.m_document->isDirty())
					facetFailure(facetOk, "timetable", "Cancel changed canonical stop or dirty state");
				if (!editStopDialog(0, [](QDialog*) {})
					|| !sameStop(before, w.m_document->mutableModel().services[serviceRow].stops.front()))
					facetFailure(facetOk, "timetable", "unchanged Accept lost optional values or precision");
				for (const QString arrivalText : {QStringLiteral("90.1234567890123"), QStringLiteral("0"), QString()}) {
					const bool accepted = editStopDialog(0, [&](QDialog* dialog) {
						dialog->findChild<QCheckBox*>("stopEditorArrivalPresent")->setChecked(true);
						dialog->findChild<QLineEdit*>("stopEditorArrivalEdit")->setText(arrivalText);
						dialog->findChild<QCheckBox*>("stopEditorDeparturePresent")->setChecked(false);
					});
					const auto& stop = w.m_document->mutableModel().services[serviceRow].stops.front();
					if (!accepted || stop.hasPlannedArrival != !arrivalText.isEmpty() || stop.hasPlannedDeparture
						|| (stop.hasPlannedArrival && stop.plannedArrivalSeconds != arrivalText.toDouble()))
						facetFailure(facetOk, "timetable", "modal lost fractional, zero or absent planned time");
				}
				w.m_document->mutableModel().services[serviceRow].stops.front().platformId = "unknown-platform";
				const SceneStop unknownPlatform = w.m_document->mutableModel().services[serviceRow].stops.front();
				bool unknownPlatformShown = false;
				if (!editStopDialog(0, [&](QDialog* dialog) {
						auto* platform = dialog->findChild<QComboBox*>("stopEditorPlatformCombo");
						unknownPlatformShown = platform->currentText() == "Invalid: unknown-platform"
							&& platform->currentData().toString() == "unknown-platform"
							&& !platform->itemData(platform->currentIndex(), Qt::ToolTipRole).toString().isEmpty();
					})
					|| !unknownPlatformShown || !sameStop(unknownPlatform, w.m_document->mutableModel().services[serviceRow].stops.front()))
					facetFailure(facetOk, "timetable", "unknown imported platform was replaced or not explained");
				w.m_document->mutableModel().services[serviceRow].stops.front() = before;
				const auto savedStops = w.m_document->mutableModel().services[serviceRow].stops;
				const auto savedStations = w.m_document->mutableModel().stations;
				const auto traversal = sceneServiceTraversal(w.m_document->mutableModel(), w.m_document->mutableModel().services[serviceRow]);
				if (!traversal.visits.empty()) {
					const auto& visit = traversal.visits.front();
					for (auto& station : w.m_document->mutableModel().stations) {
						if (station.id != visit.stationId) continue;
						const auto platform = std::find_if(station.platforms.begin(), station.platforms.end(),
							[&](const ScenePlatform& p) { return p.id == visit.platformId; });
						if (platform != station.platforms.end()) {
							ScenePlatform alternative = *platform;
							alternative.id += "-ambiguous";
							station.platforms.push_back(alternative);
						}
					}
					w.m_document->mutableModel().services[serviceRow].stops.clear();
					w.m_timetableStopsPanel->refresh();
					const bool accepted = editStopDialog(-1, [](QDialog*) {});
					if (accepted || !w.m_document->mutableModel().services[serviceRow].stops.empty())
						facetFailure(facetOk, "timetable", "ambiguous new stop accepted without a platform");
				} else {
					facetFailure(facetOk, "timetable", "ambiguous stop fixture has no route visits");
				}
				w.m_document->mutableModel().stations = savedStations;
				w.m_document->mutableModel().services[serviceRow].stops = savedStops;
				w.m_timetableStopsPanel->refresh();
			}
			// Give this repeat-call fixture a second real anchor before the final
			// station, instead of calling the same consumed node twice.
			SceneStop finalStop = w.m_document->mutableModel().services[serviceRow].stops.back();
			const auto finalResolution = resolveSceneServiceStops(w.m_document->mutableModel(), w.m_document->mutableModel().services[serviceRow],
				sceneServiceTraversal(w.m_document->mutableModel(), w.m_document->mutableModel().services[serviceRow]))
											 .back();
			if (finalStop.platformId.empty() && finalResolution.candidatePlatformIds.size() == 1)
				finalStop.platformId = finalResolution.candidatePlatformIds.front();
			for (auto& station : w.m_document->mutableModel().stations)
				for (auto& platform : station.platforms)
					if (station.id == finalStop.stationId && platform.id == finalStop.platformId
						&& !platform.nodeIds.empty()) {
						const auto anchor = std::find_if(w.m_document->mutableModel().nodes.begin(), w.m_document->mutableModel().nodes.end(),
							[&](const SceneNode& node) { return node.id == platform.nodeIds.front(); });
						const SceneNode* preceding = nullptr;
						if (anchor != w.m_document->mutableModel().nodes.end())
							for (const auto& node : w.m_document->mutableModel().nodes)
								if (node.trackId == anchor->trackId && node.xKm < anchor->xKm
									&& (!preceding || node.xKm > preceding->xKm))
									preceding = &node;
						if (preceding)
							platform.nodeIds.insert(platform.nodeIds.begin(), preceding->id);
					}
			double lastTime = 0.0;
			for (const auto& stop : w.m_document->mutableModel().services[serviceRow].stops) {
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
			if (!accepted || static_cast<int>(w.m_document->mutableModel().services[serviceRow].stops.size()) != originalStopCount + 1)
				facetFailure(facetOk, "timetable", "modal add stop did not apply");
			else {
				const auto& stop = w.m_document->mutableModel().services[serviceRow].stops.back();
				if (stop.stationId != finalStop.stationId || stop.platformId != finalStop.platformId
					|| !stop.hasPlannedArrival || stop.plannedArrivalSeconds != arrivalSeconds
					|| !stop.hasPlannedDeparture || stop.plannedDepartureSeconds != arrivalSeconds + 60
					|| stop.dwellSeconds != 60.0)
					facetFailure(facetOk, "timetable", "modal values were not committed together");
			}
			SceneStop editedStop;
			if (static_cast<int>(w.m_document->mutableModel().services[serviceRow].stops.size()) > originalStopCount)
				editedStop = w.m_document->mutableModel().services[serviceRow].stops.back();
			if (facetOk) {
				w.m_stopTableWidget->setCurrentCell(originalStopCount, 0);
				w.m_timetableStopsPanel->moveStopUp();
				if (w.m_document->mutableModel().services[serviceRow].stops[originalStopCount - 1].stationId != editedStop.stationId)
					facetFailure(facetOk, "timetable", "move up did not apply");
				w.m_timetableStopsPanel->moveStopDown();
				if (!sameStop(w.m_document->mutableModel().services[serviceRow].stops.back(), editedStop))
					facetFailure(facetOk, "timetable", "move down did not restore edited stop");
				// The source scene's final stop intentionally omits a planned departure;
				// leave the edited stop before it so the model remains valid for save/reload.
				w.m_stopTableWidget->setCurrentCell(originalStopCount, 0);
				w.m_timetableStopsPanel->moveStopUp();
			}
			// Insertion positions. Each prepared timetable leaves out one stop of the full
			// timetable, so the position where it belongs has a free station.
			{
				auto& stops = w.m_document->mutableModel().services[serviceRow].stops;
				const std::vector<SceneStop> fullStops = stops;
				const bool dirtySaved = w.m_document->isDirty();
				const auto prepare = [&](std::vector<SceneStop> prepared) {
					stops = std::move(prepared);
					w.m_document->setDirty(false);
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
					const quint64 revisionBefore = w.m_document->revision();
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
					if (!w.m_document->isDirty() || w.m_document->revision() == revisionBefore
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
					const quint64 revisionBefore = w.m_document->revision();
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
					if (!opened || !sameStopList(stops, gapStops) || w.m_document->isDirty() || w.m_document->revision() != revisionBefore)
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
						|| blockedError != eligibilityText || !sameStopList(stops, gapStops) || w.m_document->isDirty()
						|| w.m_document->revision() != revisionBefore)
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
					const SceneSaveResult saved = ::saveScene(w.m_document->mutableModel(), reloadPath.toStdString());
					const SceneLoadResult reloaded = ::loadScene(reloadPath.toStdString());
					const auto reloadedService = std::find_if(reloaded.scene.services.begin(), reloaded.scene.services.end(),
						[&](const SceneService& candidate) { return candidate.id == editedServiceId; });
					if (!saved.success() || reloadedService == reloaded.scene.services.end()
						|| reloadedService->stops.size() != stops.size()
						|| !std::equal(stops.begin(), stops.end(), reloadedService->stops.begin(), sameStop))
						facetFailure(facetOk, "timetable", "inserted stop order or planned times changed after save and reload");
				}
				stops = fullStops;
				w.m_document->setDirty(dirtySaved);
				w.m_timetableStopsPanel->refresh();
				w.m_session->updateSceneWindowTitle();
			}
			// Loaded invalid drafts remain removable; adding through the UI above
			// must not create one.
			w.m_document->mutableModel().services[serviceRow].stops.push_back(editedStop);
			w.m_timetableStopsPanel->refresh();
			w.m_stopTableWidget->setCurrentCell(originalStopCount + 1, 0);
			if (static_cast<int>(w.m_document->mutableModel().services[serviceRow].stops.size()) != originalStopCount + 2) {
				facetFailure(facetOk, "timetable", "temporary stop add did not apply");
			} else {
				w.m_timetableStopsPanel->removeStop();
				if (static_cast<int>(w.m_document->mutableModel().services[serviceRow].stops.size()) != originalStopCount + 1)
					facetFailure(facetOk, "timetable", "stop delete did not apply");
			}
			if (facetOk && !sameStop(w.m_document->mutableModel().services[serviceRow].stops[originalStopCount - 1], editedStop))
				facetFailure(facetOk, "timetable", "edited stop was not retained");
		}
		if (facetOk)
			std::fprintf(stdout, "E2E_EDITOR_TIMETABLE_OK\n");
	}

	std::string entranceDelayServiceId = editedServiceId;
	std::string entranceDelayStationId;
	double entranceDelaySeconds = 12.75;
	if (!w.m_document->isLoaded() || editedServiceId.empty() || !w.m_entranceDelayListWidget
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
			tabs->setCurrentWidget(w.m_entranceDelayPanel);
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
				const auto editedService = std::find_if(w.m_document->mutableModel().services.begin(), w.m_document->mutableModel().services.end(),
					[&](const SceneService& candidate) { return candidate.id == entranceDelayServiceId; });
				int repeatedDepartureRow = -1;
				std::string repeatedStationId;
				if (editedService != w.m_document->mutableModel().services.end()) {
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
						std::distance(w.m_document->mutableModel().services.begin(), editedService)));
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
					const SceneEntranceDelay* delay = w.m_entranceDelayPanel->selectedEntranceDelay();
					const bool editedDelay = delay && delay->serviceId == entranceDelayServiceId
						&& delay->occurrence == 2 && delay->stationId == entranceDelayStationId
						&& delay->delaySeconds == entranceDelaySeconds && editedService != w.m_document->mutableModel().services.end()
						&& editedService->hasRepeat
						&& sceneServiceOccurrenceCount(*editedService, w.serviceOccurrenceDuration()) >= 2
						&& w.m_entranceDelayOccurrenceContextLabel
						&& w.m_entranceDelayOccurrenceContextLabel->text().contains("1725");
					if (!editedDelay)
						facetFailure(facetOk, "entrance delay", "service, occurrence, station, or seconds edit did not persist");
					const SceneRunSelection delayedOccurrence{{entranceDelayServiceId, 2}};
					const auto infrastructureDiagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), w.m_document->mutableModel());
					const auto operationsDiagnostics = buildOperationsFromScene(defaultRun(),
						w.m_document->mutableModel(), w.m_runSelection.scenarioId(), delayedOccurrence);
					if (hasErrors(infrastructureDiagnostics) || hasErrors(operationsDiagnostics)
						|| defaultRun().numTrains != 1
						|| defaultRun().trains[0].trainDescription != entranceDelayServiceId + "-2"
						|| defaultRun().trains[0].EntranceDelay != entranceDelaySeconds)
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
	if (!w.m_document->isLoaded() || !w.m_incidentListWidget || editedServiceId.empty()) {
		bool facetOk = false;
		facetFailure(facetOk, "incident", "scene or edited service unavailable");
	} else {
		bool facetOk = true;
		const double preciseBreakdownSpeed = 40.125;
		const int originalCount = w.m_incidentListWidget->count();
		editedIncidentId = sceneUniqueIncidentId(w.m_document->mutableModel(), "e2e_incident");
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
			const std::string temporaryServiceId = w.m_document->mutableModel().services.back().id;
			if (w.m_incidentTargetCombo->findData(QString::fromStdString(temporaryServiceId)) < 0)
				facetFailure(facetOk, "incident", "service add did not refresh breakdown targets");
			acceptConfirmation();
			w.m_servicePanel->deleteService();
			if (w.m_serviceListWidget->count() != serviceCount
				|| w.m_incidentTargetCombo->findData(QString::fromStdString(temporaryServiceId)) >= 0)
				facetFailure(facetOk, "incident", "service delete did not refresh breakdown targets");
		}
		const QString review = w.m_runController->runReviewText();
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
		// The dock names the selected scenario above three tabs, and a check box stands in front of the field it switches on.
		auto* scenarioTabs = w.findChild<QTabWidget*>("scenarioEditorTabs");
		auto* scenarioHeader = w.findChild<QLabel*>("scenarioHeaderLabel");
		if (!scenarioTabs || !scenarioHeader || scenarioTabs->count() != 3 || scenarioTabs->tabText(0) != "Scenario"
			|| scenarioTabs->tabText(1) != "Incidents" || scenarioTabs->tabText(2) != "Entrance delays"
			|| scenarioTabs->widget(0) != w.m_scenarioPanel || scenarioTabs->widget(1) != w.m_incidentPanel
			|| scenarioTabs->widget(2) != w.m_entranceDelayPanel)
			facetFailure(facetOk, "incident", "the dock lacks the Scenario, Incidents and Entrance delays tabs");
		else if (scenarioHeader->parentWidget() != scenarioTabs->parentWidget()
			|| scenarioHeader->text() != w.m_scenarioPanel->headerText()
			|| !w.m_scenarioPanel->headerText().contains(QString::fromStdString(w.m_runSelection.scenarioId())))
			facetFailure(facetOk, "incident", "the header above the tabs does not name the selected scenario");
		const auto checkBeforeField = [](QWidget* check, QWidget* field) {
			QFormLayout* form = field && field->parentWidget() ? field->parentWidget()->findChild<QFormLayout*>() : nullptr;
			int row = -1;
			QFormLayout::ItemRole role = QFormLayout::SpanningRole;
			if (!form)
				return false;
			form->getWidgetPosition(field, &row, &role);
			return row >= 0 && role == QFormLayout::FieldRole && form->itemAt(row, QFormLayout::LabelRole)
				&& form->itemAt(row, QFormLayout::LabelRole)->widget() == check;
		};
		if (!checkBeforeField(w.m_incidentHasEndSecondsCheck, w.m_incidentEndSecondsEdit)
			|| !checkBeforeField(w.m_incidentHasReducedSpeedCheck, w.m_incidentReducedSpeedKmhEdit))
			facetFailure(facetOk, "incident", "the recovery end and reduced speed fields do not follow their check boxes");
		if (!w.m_incidentPanel->emptyStateLabel()->isHidden() || !w.m_entranceDelayPanel->emptyStateLabel()->isHidden())
			facetFailure(facetOk, "incident", "an empty-state message shows although an incident and an entrance delay are selected");
		// Opening an incident from Loaded Data shows the Incidents tab, whichever tab was current.
		if (scenarioTabs) {
			QTreeWidgetItem incidentTarget;
			incidentTarget.setText(0, QString::fromStdString(editedIncidentId));
			scenarioTabs->setCurrentWidget(w.m_scenarioPanel);
			w.openLoadedDataTarget(QStringLiteral("incident"), &incidentTarget);
			const SceneIncident* opened = w.m_incidentPanel->selectedIncident();
			if (scenarioTabs->currentWidget() != w.m_incidentPanel || !opened || opened->id != editedIncidentId)
				facetFailure(facetOk, "incident", "opening an incident from Loaded Data did not show it on the Incidents tab");
		}
		if (facetOk)
			std::fprintf(stdout, "E2E_EDITOR_INCIDENT_OK\n");
	}

	if (w.m_document->isLoaded()) {
		bool facetOk = true;
		w.refreshValidationPanel();
		QApplication::processEvents();
		SceneDiagnosticCounts counts = countDiagnostics(w.m_session->m_sceneDiagnostics);
		if (!w.m_validation->table() || !w.m_validation->statusLabel() || counts.errors != 0
			|| w.m_validation->table()->rowCount() != static_cast<int>(w.m_session->m_sceneDiagnostics.size())
			|| !w.m_validation->statusLabel()->text().startsWith("Validation:"))
			facetFailure(facetOk, "validation", "refresh did not report a clean, complete result");
		if (facetOk)
			std::fprintf(stdout, "E2E_EDITOR_VALIDATION_OK\n");
	}

	if (w.m_document->isLoaded()) {
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
			for (int row = 0; row < static_cast<int>(w.m_document->mutableModel().services.size()); ++row) {
				if (w.m_document->mutableModel().services[static_cast<std::size_t>(row)].id != editedServiceId)
					continue;
				w.m_serviceListWidget->setCurrentRow(row);
				w.m_serviceOperatingCodeEdit->setText("1731");
				w.m_servicePanel->commitPendingEditors();
				if (w.m_document->mutableModel().services[static_cast<std::size_t>(row)].operatingCode != "1731")
					facetFailure(facetOk, "save/reload", "pending service text did not commit before persistence");
				break;
			}
			expectedTrainUnits = w.m_document->mutableModel().trainUnits;
			expectedCompositions = w.m_document->mutableModel().compositions;
			expectedServices = w.m_document->mutableModel().services;
			expectedStations = w.m_document->mutableModel().stations;
			expectedPassengers = w.m_document->mutableModel().passengers;
			expectedIncidents = w.selectedScenarioIncidents();
			if (const SceneScenario* scenario = w.selectedScenario())
				expectedEntranceDelays = scenario->entranceDelays;
			auto result = ::saveScene(w.m_document->mutableModel(), outScenePath.toStdString());
			if (!result.success()) {
				facetFailure(facetOk, "save/reload", "save failed");
			} else if (!w.openSceneDirectory(outScenePath)) {
				facetFailure(facetOk, "save/reload", "saved scene did not reload");
			} else if (!sameTrainUnits(expectedTrainUnits, w.m_document->mutableModel().trainUnits)) {
				facetFailure(facetOk, "save/reload", "train-unit facet changed after reload");
			} else if (!sameCompositions(expectedCompositions, w.m_document->mutableModel().compositions)) {
				facetFailure(facetOk, "save/reload", "composition facet changed after reload");
			} else if (!sameServices(expectedServices, w.m_document->mutableModel().services)) {
				facetFailure(facetOk, "save/reload", "service/timetable facet changed after reload");
			} else if (!std::any_of(w.m_document->mutableModel().services.begin(), w.m_document->mutableModel().services.end(),
						   [&](const SceneService& service) {
							   return service.id == editedServiceId && service.visualizationColor == "#3c8dd2";
						   })) {
				facetFailure(facetOk, "save/reload", "service colour changed after reload");
			} else if (!sameStations(expectedStations, w.m_document->mutableModel().stations)) {
				facetFailure(facetOk, "save/reload", "platform geometry changed after reload");
			} else if (!samePassengers(expectedPassengers, w.m_document->mutableModel().passengers)) {
				facetFailure(facetOk, "save/reload", "passenger hierarchy changed after reload");
			} else if (!sameIncidents(expectedIncidents, w.selectedScenarioIncidents())) {
				facetFailure(facetOk, "save/reload", "incident facet changed after reload");
			} else if (!w.selectedScenario()
				|| !sameEntranceDelays(expectedEntranceDelays, w.selectedScenario()->entranceDelays)) {
				facetFailure(facetOk, "save/reload", "entrance-delay facet changed after reload");
			} else if (w.m_document->isDirty() || hasErrors(w.m_session->m_sceneDiagnostics)) {
				facetFailure(facetOk, "save/reload", "reloaded scene is dirty or invalid");
			} else {
				const auto triggerPendingSave = [&]() {
					if (!w.m_saveSceneAction || !w.m_saveSceneAction->isEnabled())
						return false;
					w.m_saveSceneAction->trigger();
					QApplication::processEvents();
					return !w.m_document->isDirty();
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
				const int trainUnitRow = modelRowFor(w.m_document->mutableModel().trainUnits, editedTrainUnitId);
				const int compositionRow = modelRowFor(w.m_document->mutableModel().compositions, editedCompositionId);
				const int serviceRow = modelRowFor(w.m_document->mutableModel().services, editedServiceId);
				const int passengerRow = modelRowFor(w.m_document->mutableModel().passengers, e2ePassengerId);
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
					if (!triggerPendingSave() || w.m_document->mutableModel().passengers[static_cast<std::size_t>(passengerRow)].id != pendingPassengerId)
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
						for (auto& station : w.m_document->mutableModel().stations) {
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
							if (!triggerPendingSave() || w.m_document->mutableModel().stations.empty()
								|| w.m_document->mutableModel().stations.front().platforms.empty()
								|| !w.m_document->mutableModel().stations.front().platforms.front().hasLength
								|| w.m_document->mutableModel().stations.front().platforms.front().lengthM != pendingLengthValue)
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
						tabs->setCurrentWidget(w.m_entranceDelayPanel);
					w.m_entranceDelayListWidget->setCurrentRow(entranceDelayRow);
					QApplication::processEvents();
					const double pendingEntranceDelay = entranceDelaySeconds + 0.5;
					QLineEdit* pendingDelayEdit = spinTextEdit(w.m_entranceDelaySecondsEdit);
					pendingDelayEdit->setText(QString::number(pendingEntranceDelay, 'g', 16));
					pendingDelayEdit->setFocus();
					QApplication::processEvents();
					if (!w.m_entranceDelayPanel->selectedEntranceDelay() || w.m_entranceDelayPanel->selectedEntranceDelay()->delaySeconds == pendingEntranceDelay
						|| !triggerPendingSave() || !w.m_entranceDelayPanel->selectedEntranceDelay()
						|| w.m_entranceDelayPanel->selectedEntranceDelay()->delaySeconds != pendingEntranceDelay) {
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
						const int committedTrainRow = modelRowFor(w.m_document->mutableModel().trainUnits, editedTrainUnitId);
						if (committedTrainRow < 0
							|| !w.m_document->mutableModel().trainUnits[static_cast<std::size_t>(committedTrainRow)].hasPhysical
							|| w.m_document->mutableModel().trainUnits[static_cast<std::size_t>(committedTrainRow)].physical.mass_of_traction_unit_kg
								!= pendingPhysicalMass)
							facetFailure(facetOk, "save/reload", "focused train-unit text was not committed before Save");
					}

					const std::string pendingCompositionId = sceneUniqueCompositionId(w.m_document->mutableModel(), "e2e_pending_composition");
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
						const int committedCompositionRow = modelRowFor(w.m_document->mutableModel().compositions, pendingCompositionId);
						if (committedCompositionRow < 0
							|| !std::any_of(
								w.m_document->mutableModel().services.begin(), w.m_document->mutableModel().services.end(),
								[&](const SceneService& service) { return service.composition == pendingCompositionId; }))
							facetFailure(facetOk, "save/reload", "focused composition text was not committed before Save");
						else
							editedCompositionId = pendingCompositionId;
					}

					const int pendingServiceRow = modelRowFor(w.m_document->mutableModel().services, editedServiceId);
					int pendingStopRow = -1;
					if (pendingServiceRow >= 0) {
						const auto& stops = w.m_document->mutableModel().services[static_cast<std::size_t>(pendingServiceRow)].stops;
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
						for (int row = 0; row < static_cast<int>(w.m_document->mutableModel().scenarios.size()); ++row)
							if (w.m_document->mutableModel().scenarios[static_cast<std::size_t>(row)].id == w.m_runSelection.scenarioId())
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
						const int pendingDwell = static_cast<int>(w.m_document->mutableModel().services[static_cast<std::size_t>(pendingServiceRow)].stops[static_cast<std::size_t>(pendingStopRow)].dwellSeconds)
							+ 1;
						if (!editStopDialog(pendingStopRow, [&](QDialog* dialog) {
								auto* dwell = dialog->findChild<QLineEdit*>("stopEditorDwellEdit");
								dwell->setText(QString::number(pendingDwell));
								dwell->setFocus();
							}))
							facetFailure(facetOk, "save/reload", "focused modal dwell was not accepted");
						if (!triggerPendingSave()
							|| w.m_document->mutableModel().services[static_cast<std::size_t>(pendingServiceRow)].stops[static_cast<std::size_t>(pendingStopRow)].dwellSeconds
								!= pendingDwell)
							facetFailure(facetOk, "save/reload", "focused stop dwell text was not committed before Save");

						if (w.m_incidentDock) {
							w.m_incidentDock->show();
							w.m_incidentDock->raise();
						}
						if (auto* tabs = w.findChild<QTabWidget*>("scenarioEditorTabs"))
							tabs->setCurrentWidget(w.m_scenarioPanel);
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
								tabs->setCurrentWidget(w.m_incidentPanel);
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
							if (w.m_document->mutableModel().routes.empty()) {
								facetFailure(facetOk, "save/reload", "route unavailable for blocked Run coverage");
							} else {
								const std::vector<std::string> validRoute = w.m_document->mutableModel().routes.front().blocks;
								w.m_document->mutableModel().routes.front().blocks.clear();
								w.m_runSceneAction->trigger();
								QApplication::processEvents();
								if (w.m_runController->m_worker || !w.m_incidentPanel->selectedIncident()
									|| !w.m_incidentPanel->selectedIncident()->hasEndSeconds
									|| w.m_incidentPanel->selectedIncident()->endSeconds != pendingIncidentEnd)
									facetFailure(facetOk, "save/reload", "Run did not flush focused incident text before validation");
								w.m_document->mutableModel().routes.front().blocks = validRoute;
								w.refreshValidationPanel();
							}
						}
					}
					const std::string pendingSource = "e2e/source-data-pending-save.txt";
					w.m_trainUnitListWidget->setCurrentRow(trainUnitRow);
					w.m_trainUnitSourceDataEdit->setText(QString::fromStdString(pendingSource));
					w.m_trainUnitSourceDataEdit->setFocus();
					if (w.m_document->mutableModel().trainUnits[static_cast<std::size_t>(trainUnitRow)].sourceDataFile
						== pendingSource) {
						facetFailure(facetOk, "save/reload", "pending source edit committed before Save");
					} else if (!w.m_session->saveSceneToCurrentDir()) {
						facetFailure(facetOk, "save/reload", "pending editor values were not saved");
					} else {
						expectedTrainUnits = w.m_document->mutableModel().trainUnits;
						expectedCompositions = w.m_document->mutableModel().compositions;
						expectedServices = w.m_document->mutableModel().services;
						expectedStations = w.m_document->mutableModel().stations;
						expectedPassengers = w.m_document->mutableModel().passengers;
						expectedIncidents = w.selectedScenarioIncidents();
						if (const SceneScenario* scenario = w.selectedScenario())
							expectedEntranceDelays = scenario->entranceDelays;
						if (!w.openSceneDirectory(outScenePath)
							|| !sameTrainUnits(expectedTrainUnits, w.m_document->mutableModel().trainUnits)
							|| !sameCompositions(expectedCompositions, w.m_document->mutableModel().compositions)
							|| !sameServices(expectedServices, w.m_document->mutableModel().services)
							|| !sameStations(expectedStations, w.m_document->mutableModel().stations)
							|| !samePassengers(expectedPassengers, w.m_document->mutableModel().passengers)
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
					const SceneSaveResult bundleSave = saveSceneBundle(w.m_document->mutableModel(), bundlePath.toStdString());
					if (!bundleSave.success() || !w.openSceneDirectory(bundlePath) || !w.selectedScenario()
						|| !sameStations(expectedStations, w.m_document->mutableModel().stations)
						|| !samePassengers(expectedPassengers, w.m_document->mutableModel().passengers)
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
		const bool serviceStructure = servicePages && servicePages->count() == 3 && servicePages->tabText(0) == "Service"
			&& servicePages->tabText(1) == "Timetable" && servicePages->tabText(2) == "Run occurrences"
			&& w.m_serviceOccurrenceTable && w.m_serviceOccurrenceTable->parentWidget()
			&& w.m_stopTableWidget && w.m_stopTimeBaseLabel && w.m_stopTimeBaseLabel->wordWrap()
			&& servicePages->widget(1)->isAncestorOf(w.m_stopTableWidget) && servicePages->widget(1)->isAncestorOf(w.m_stopTimeModeCombo)
			&& servicePages->widget(1)->isAncestorOf(w.m_addStopButton);
		// The time mode, the base time sentence, the stop table and its five buttons are whole inside the visible part of
		// the Services dock when the Timetable tab is open, so no action of the timetable is below the fold, and the five
		// columns of the table fit it without a horizontal scroll bar. Returns what is missing, or nothing.
		const auto timetableTabFailure = [&w]() -> QString {
			QTabWidget* pages = w.m_servicePanel->pages();
			const int previousTab = pages->currentIndex();
			pages->setCurrentIndex(1);
			QApplication::processEvents();
			QWidget* visibleArea = w.m_serviceDock->findChild<QScrollArea*>("editorContentScroll")->viewport();
			QString failure;
			const std::array<QWidget*, 8> parts = {w.m_stopTimeModeCombo, w.m_stopTimeBaseLabel, w.m_stopTableWidget, w.m_addStopButton,
				w.m_insertStopAfterButton, w.m_timetableStopsPanel->removeButton(), w.m_moveStopUpButton, w.m_moveStopDownButton};
			for (QWidget* part : parts) {
				const QRect area(part->mapTo(visibleArea, QPoint(0, 0)), part->size());
				if (!part->isVisible() || !visibleArea->rect().contains(area))
					failure = QString("%1 is not whole on the Timetable tab").arg(part->objectName());
			}
			const QHeaderView* columns = w.m_stopTableWidget->horizontalHeader();
			if (w.m_stopTableWidget->horizontalScrollBar()->isVisible() || columns->length() > w.m_stopTableWidget->viewport()->width())
				failure = QString("the columns of %1 need a horizontal scroll bar on the Timetable tab").arg(w.m_stopTableWidget->objectName());
			// The planned times as clock times are whole, not elided, in the columns of the table.
			const int previousMode = w.m_stopTimeModeCombo->currentIndex();
			w.m_stopTimeModeCombo->setCurrentIndex(w.m_stopTimeModeCombo->findData(true));
			QApplication::processEvents();
			QStyleOptionViewItem option;
			option.initFrom(w.m_stopTableWidget);
			option.widget = w.m_stopTableWidget;
			option.font = w.m_stopTableWidget->font();
			option.fontMetrics = QFontMetrics(option.font);
			for (int row = 0; row < w.m_stopTableWidget->rowCount(); ++row) {
				for (int column = 3; column < 5; ++column) {
					const QModelIndex cell = w.m_stopTableWidget->model()->index(row, column);
					if (w.m_stopTableWidget->itemDelegate()->sizeHint(option, cell).width() > w.m_stopTableWidget->columnWidth(column))
						failure = QString("the clock time '%1' is elided on the Timetable tab").arg(cell.data().toString());
				}
			}
			w.m_stopTimeModeCombo->setCurrentIndex(previousMode);
			pages->setCurrentIndex(previousTab);
			QApplication::processEvents();
			return failure;
		};
		const bool signalDetails = w.m_inspector->signallingGroupDetails && w.m_inspector->signallingGroupDetails->isReadOnly()
			&& w.m_inspector->signallingGroupDetails->lineWrapMode() == QPlainTextEdit::WidgetWidth;
		const bool resultsStructure = w.m_runResultsDock
			&& w.m_runResultsDock->findChild<QToolButton*>("runResultsViewsButton")
			&& w.m_runResultsDock->findChildren<QAction*>(QRegularExpression("^resultView_")).size() == 12
			&& w.m_runResultsDock->findChildren<QPushButton*>(QRegularExpression("^resultView_")).size() == 2;
		if (!serviceStructure || !signalDetails || !resultsStructure)
			facetFailure(ok, "presentation", "editor tabs, wrapped signal details or result controls unavailable");
		// The Diagrams menu and the Run Results menu name each result view the same, apart from a trailing ellipsis.
		const auto viewNames = [](const QList<QAction*>& actions) {
			QStringList names;
			for (const QAction* action : actions)
				if (!action->isSeparator())
					names << QString(action->text()).remove(QRegularExpression("\\.\\.\\.$"));
			names.sort();
			return names;
		};
		const QToolButton* viewsButton = w.m_runResultsDock ? w.m_runResultsDock->findChild<QToolButton*>("runResultsViewsButton") : nullptr;
		if (!viewsButton || viewNames(viewsButton->menu()->actions()) != viewNames(w.m_diagramsMenu->actions()))
			facetFailure(ok, "presentation", "the Diagrams menu and the Run Results menu name the result views differently");
		for (QDockWidget* dock : w.findChildren<QDockWidget*>()) dock->hide();
		w.showNormal();
		w.resize(1280, 800);
		QApplication::processEvents();
		// Blocks and Routes have the most controls in the Infrastructure editor. The combo is above the table, the buttons are
		// in a row under it, and they are in the first view of the dock without sideways scrolling. The buttons of the route
		// sections below the table are in the first view when `allButtons` is set. A small floating dock can need a scroll to reach them,
		// so there the check scrolls to the end and requires them to be in view.
		// The wheel over a field of a dock that has no focus leaves the field as it is and scrolls the dock.
		int dockWheelFields = 0;
		const auto dockWheelFailure = [&dockWheelFields](QDockWidget* dock) -> QString {
			auto* scroll = dock->findChild<QScrollArea*>("editorContentScroll");
			if (!scroll)
				return dock->objectName() + " has no scroll area";
			const auto stateOf = [](QWidget* field) -> QString {
				if (auto* combo = qobject_cast<QComboBox*>(field))
					return QString::number(combo->currentIndex());
				if (auto* slider = qobject_cast<QSlider*>(field))
					return QString::number(slider->value());
				return qobject_cast<QAbstractSpinBox*>(field)->text();
			};
			// Only a field that no other scroll area holds hands the wheel to the dock.
			const auto heldByOtherArea = [scroll](const QWidget* field) {
				for (const QWidget* parent = field->parentWidget(); parent && parent != scroll->viewport(); parent = parent->parentWidget()) {
					if (qobject_cast<const QAbstractScrollArea*>(parent))
						return true;
				}
				return false;
			};
			scroll->verticalScrollBar()->setValue(0);
			bool scrolled = false;
			for (QWidget* field : scroll->widget()->findChildren<QWidget*>()) {
				if (!FocusWheel::isValueField(field) || !field->isVisible() || !field->isEnabled() || field->hasFocus())
					continue;
				const QString before = stateOf(field);
				const QPoint center = field->rect().center();
				const QPoint global = field->mapToGlobal(center);
				QWidget* under = field->childAt(center);
				for (QWidget* receiver = under ? under : field; receiver; receiver = receiver->parentWidget()) {
					QWheelEvent wheel(QPointF(receiver->mapFromGlobal(global)), QPointF(global), QPoint(), QPoint(0, -120), Qt::NoButton,
						Qt::NoModifier, Qt::NoScrollPhase, false);
					QApplication::sendEvent(receiver, &wheel);
					if (wheel.isAccepted())
						break;
				}
				QApplication::processEvents();
				++dockWheelFields;
				if (stateOf(field) != before)
					return QString("a mouse wheel changed %1 in %2 without focus").arg(field->objectName().isEmpty() ? field->metaObject()->className() : field->objectName(), dock->objectName());
				if (!scrolled && !heldByOtherArea(field) && scroll->verticalScrollBar()->maximum() > 0) {
					if (scroll->verticalScrollBar()->value() == 0)
						return QString("a mouse wheel over a field of %1 did not scroll the dock").arg(dock->objectName());
					scrolled = true;
				}
			}
			return QString();
		};
		const auto checkInfrastructureControls = [&](const char* where, bool allButtons) {
			auto* scroll = w.m_infrastructureDock->findChild<QScrollArea*>("editorContentScroll");
			const QString shownFacet = w.m_infrastructurePanel->facetKey();
			for (const char* facet : {"blocks", "routes"}) {
				w.m_infrastructureFacetCombo->setCurrentIndex(w.m_infrastructureFacetCombo->findData(QString::fromLatin1(facet)));
				QApplication::processEvents();
				scroll->verticalScrollBar()->setValue(0);
				scroll->horizontalScrollBar()->setValue(0);
				QList<QWidget*> controls = {w.m_addInfrastructureButton, w.m_deleteInfrastructureButton};
				if (QString::fromLatin1(facet) == "blocks")
					controls << w.m_insertBlockButton << w.m_moveBlockUpButton << w.m_moveBlockDownButton;
				const QList<QWidget*> sectionButtons = {w.m_addRouteSectionButton, w.m_removeRouteSectionButton,
					w.m_moveRouteSectionUpButton, w.m_moveRouteSectionDownButton};
				if (QString::fromLatin1(facet) == "routes" && allButtons)
					controls << sectionButtons;
				const QRect view = scroll->viewport()->rect();
				for (const QWidget* control : controls) {
					const QRect placed(control->mapTo(scroll->viewport(), QPoint(0, 0)), control->size());
					if (!control->isVisible() || !view.contains(placed))
						facetFailure(ok, "presentation",
							QString("%1 is not in the first view of the infrastructure editor (%2, %3)")
								.arg(control->objectName(), facet, where));
				}
				if (QString::fromLatin1(facet) == "routes" && !allButtons) {
					// A small dock does not show the route section buttons at once: scrolling down brings all four into view.
					scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
					for (const QWidget* control : sectionButtons) {
						const QRect placed(control->mapTo(scroll->viewport(), QPoint(0, 0)), control->size());
						if (!control->isVisible() || !view.contains(placed))
							facetFailure(ok, "presentation",
								QString("%1 cannot be scrolled into view in the infrastructure editor (%2, %3)")
									.arg(control->objectName(), facet, where));
					}
					scroll->verticalScrollBar()->setValue(0);
				}
				const int comboTop = w.m_infrastructureFacetCombo->mapTo(scroll->viewport(), QPoint(0, 0)).y();
				const int tableTop = w.m_infrastructureTable->mapTo(scroll->viewport(), QPoint(0, 0)).y();
				const int buttonsTop = w.m_addInfrastructureButton->mapTo(scroll->viewport(), QPoint(0, 0)).y();
				if (!(comboTop < tableTop && tableTop + w.m_infrastructureTable->height() <= buttonsTop))
					facetFailure(ok, "presentation",
						QString("the infrastructure buttons are not under the table (%1, %2)").arg(facet, where));
				if (scroll->horizontalScrollBar()->isVisible())
					facetFailure(ok, "presentation",
						QString("the infrastructure editor scrolls sideways (%1, %2)").arg(facet, where));
			}
			w.m_infrastructureFacetCombo->setCurrentIndex(w.m_infrastructureFacetCombo->findData(shownFacet));
			QApplication::processEvents();
		};
		// The buttons of the composition editor are whole inside the visible part of the dock, and the dock does not scroll sideways.
		// A docked dock is first made `dockWidth` pixels wide, narrower than the editor needed when its buttons were in one row.
		const auto compositionButtonFailure = [&w](int dockWidth = 0) -> QString {
			if (dockWidth > 0 && !w.m_compositionDock->isFloating())
				w.resizeDocks({w.m_compositionDock}, {dockWidth}, Qt::Horizontal);
			auto* scroll = w.m_compositionDock->findChild<QScrollArea*>("editorContentScroll");
			scroll->verticalScrollBar()->setValue(0);
			scroll->horizontalScrollBar()->setValue(0);
			QApplication::processEvents();
			const CompositionPanel* panel = w.m_compositionPanel;
			const std::array<const QWidget*, 7> buttons = {panel->addButton(), panel->duplicateButton(), panel->deleteButton(),
				panel->addUnitButton(), panel->removeUnitButton(), panel->moveUnitUpButton(), panel->moveUnitDownButton()};
			for (const QWidget* button : buttons) {
				const QRect placed(button->mapTo(scroll->viewport(), QPoint(0, 0)), button->size());
				if (placed.left() < 0 || placed.right() >= scroll->viewport()->width())
					return QString("%1 is cut off at the side of the Compositions dock").arg(button->objectName());
			}
			return scroll->horizontalScrollBar()->isVisible() ? QString("the Compositions dock scrolls sideways") : QString();
		};
		// The link, traction row and plot buttons of the rolling stock editor on both tabs are whole inside the visible part of the
		// dock, and the dock does not scroll sideways. A docked dock is first made `dockWidth` pixels wide.
		const auto trainUnitButtonFailure = [&w](int dockWidth = 0) -> QString {
			if (dockWidth > 0 && !w.m_trainUnitDock->isFloating())
				w.resizeDocks({w.m_trainUnitDock}, {dockWidth}, Qt::Horizontal);
			TrainUnitPanel* panel = w.m_trainUnitPanel;
			if (!panel->selectedUnit() && panel->listWidget()->count() > 0)
				panel->listWidget()->setCurrentRow(0);
			if (!panel->selectedUnit())
				return QStringLiteral("no rolling stock unit is available to check");
			auto* scroll = w.m_trainUnitDock->findChild<QScrollArea*>("editorContentScroll");
			QString failure;
			for (const int tab : {0, 1}) {
				panel->tabs()->setCurrentIndex(tab);
				scroll->horizontalScrollBar()->setValue(0);
				// The scroll bars follow a change of the panel over a few passes of the event loop.
				for (int pass = 0; pass < 3; ++pass)
					QApplication::processEvents();
				QList<QPushButton*> buttons = {panel->linkButton(tab == 0), panel->unlinkButton(tab == 0), panel->retryButton(tab == 0)};
				if (tab == 0)
					buttons << panel->addTractionRowButton() << panel->removeTractionRowButton() << panel->plotButton();
				for (const QPushButton* button : buttons) {
					const QRect placed(button->mapTo(scroll->viewport(), QPoint(0, 0)), button->size());
					if (placed.left() < 0 || placed.right() >= scroll->viewport()->width())
						failure = QString("%1 is cut off at the side of the Rolling stock dock").arg(button->objectName());
				}
				if (failure.isEmpty() && scroll->horizontalScrollBar()->isVisible())
					failure = QStringLiteral("the Rolling stock dock scrolls sideways");
			}
			panel->tabs()->setCurrentIndex(0);
			return failure;
		};
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
				// A dock opens at the width of its size hint. That leaves the network canvas at least 35 percent of the width
				// beside the Case and Layers rail, however large the text is, and the Rolling stock dock holds its buttons.
				w.m_caseLayersDock->show();
				// The size hint of the dock is kept until its layout is told that the window changed.
				dock->widget()->updateGeometry();
				QApplication::processEvents();
				w.resizeDocks({dock}, {dock->sizeHint().width()}, Qt::Horizontal);
				QApplication::processEvents();
				const int besideRail = w.width() - w.m_caseLayersDock->width();
				if (w.centralWidget->width() * 100 < besideRail * 35)
					facetFailure(ok, "presentation",
						QString("%1 leaves the network %2 of %3 pixels at %4 times the font at 1280 by 800 pixels")
							.arg(dock->objectName())
							.arg(w.centralWidget->width())
							.arg(besideRail)
							.arg(scale));
				if (dock == w.m_trainUnitDock && scale == 1.0) {
					const QString failure = trainUnitButtonFailure();
					if (!failure.isEmpty())
						facetFailure(ok, "presentation", failure + " at the width that it opens with");
				}
				w.m_caseLayersDock->hide();
				QApplication::processEvents();
				if (dock == w.m_serviceDock) {
					const QString failure = timetableTabFailure();
					if (!failure.isEmpty())
						facetFailure(ok, "presentation", failure + " at 1280 by 800 pixels");
				}
				if (scale == 1.0) {
					const QString failure = dockWheelFailure(dock);
					if (!failure.isEmpty())
						facetFailure(ok, "presentation", failure);
				}
				if (dock == w.m_compositionDock) {
					const QString failure = compositionButtonFailure(640);
					if (!failure.isEmpty())
						facetFailure(ok, "presentation", failure + " at 1280 by 800 pixels");
				}
				if (dock == w.m_trainUnitDock) {
					const QString failure = trainUnitButtonFailure(qRound(480 * scale));
					if (!failure.isEmpty())
						facetFailure(ok, "presentation", failure + QString(" at %1 times the font in a dock of %2 pixels").arg(scale).arg(qRound(480 * scale)));
				}
				if (dock == w.m_passengerDock) {
					auto* scroll = dock->findChild<QScrollArea*>("editorContentScroll");
					scroll->verticalScrollBar()->setValue(0);
					scroll->horizontalScrollBar()->setValue(0);
					const QPoint addCenter = w.m_addPassengerButton->mapTo(scroll->viewport(),
						w.m_addPassengerButton->rect().center());
					if (!scroll->viewport()->rect().contains(addCenter))
						facetFailure(ok, "presentation", "passenger Add action is outside the initial viewport");
				}
				if (dock == w.m_infrastructureDock)
					checkInfrastructureControls("docked", true);
				dock->setFloating(true);
				dock->resize(750, 600);
				QApplication::processEvents();
				if (dock->width() > 750 || dock->height() > 600
					|| !dock->findChild<QScrollArea*>("editorContentScroll"))
					facetFailure(ok, "presentation", dock->objectName() + " cannot bound its floating content");
				else if (dock == w.m_serviceDock) {
					const QString failure = timetableTabFailure();
					if (!failure.isEmpty())
						facetFailure(ok, "presentation", failure + " in a floating dock of 750 by 600 pixels");
				}
				if (dock == w.m_compositionDock) {
					const QString failure = compositionButtonFailure();
					if (!failure.isEmpty())
						facetFailure(ok, "presentation", failure + " in a floating dock of 750 by 600 pixels");
				}
				if (dock == w.m_trainUnitDock) {
					const QString failure = trainUnitButtonFailure();
					if (!failure.isEmpty())
						facetFailure(ok, "presentation", failure + " in a floating dock of 750 by 600 pixels");
				}
				if (dock == w.m_infrastructureDock)
					checkInfrastructureControls("floating", false);
				dock->setFloating(false);
				dock->hide();
			}
			dock->setFont(normalFont);
		}
		{
			// Once a dock has been resized, Qt opens a dock at the width that its side of the window had last. The Rolling stock
			// dock that opens after a narrow Incidents dock is still as wide as its own size hint.
			w.m_caseLayersDock->show();
			w.m_incidentDock->show();
			QApplication::processEvents();
			w.resizeDocks({w.m_incidentDock}, {300}, Qt::Horizontal);
			QApplication::processEvents();
			w.m_incidentDock->hide();
			w.m_trainUnitDock->show();
			QApplication::processEvents();
			const QString failure = trainUnitButtonFailure();
			if (!failure.isEmpty())
				facetFailure(ok, "presentation", failure + " when it opens after a narrower dock at 1280 by 800 pixels");
			w.m_trainUnitDock->hide();
			w.m_caseLayersDock->hide();
			QApplication::processEvents();
		}
		if (ok) {
			// A window of 1280 by 698 pixels with Loaded Data below the network: the Case and Layers rail is too short for its
			// content and scrolls, and a long case name without spaces and the first layer toggle stay whole.
			const std::string caseName = w.m_document->mutableModel().name;
			w.m_document->mutableModel().name = "Amsterdam_Hilversum_Student";
			w.updateCaseLayersPanel();
			w.resize(1280, 698);
			w.m_caseLayersDock->show();
			w.m_loadedDataDock->show();
			w.m_loadedDataDock->raise();
			QApplication::processEvents();
			auto* rail = w.m_caseLayersDock->findChild<QScrollArea*>("caseLayersScroll");
			const QWidget* railViewport = rail ? rail->viewport() : nullptr;
			const auto insideRail = [railViewport](const QWidget* widget) {
				const QPoint left = widget->mapTo(railViewport, QPoint(0, 0));
				return left.x() >= 0 && left.x() + widget->width() <= railViewport->width();
			};
			if (!rail || !rail->verticalScrollBar()->isVisible())
				facetFailure(ok, "presentation", "the case and layers rail does not scroll with Loaded Data open, so the check proves nothing");
			else if (rail->horizontalScrollBar()->isVisible() || rail->horizontalScrollBarPolicy() != Qt::ScrollBarAlwaysOff
				|| rail->widget()->width() > railViewport->width())
				facetFailure(ok, "presentation", "the case and layers rail scrolls sideways with Loaded Data open");
			else {
				const QRect nameArea = w.m_caseNameLabel->contentsRect();
				const QRect nameText = w.m_caseNameLabel->fontMetrics().boundingRect(QRect(0, 0, nameArea.width(), 100000),
					Qt::TextWordWrap, w.m_caseNameLabel->text());
				if (!insideRail(w.m_caseNameLabel) || nameText.width() > nameArea.width()
					|| w.m_caseNameLabel->height() < w.m_caseNameLabel->heightForWidth(w.m_caseNameLabel->width()))
					facetFailure(ok, "presentation", "the case name is cut in the case and layers rail with Loaded Data open");
				if (!insideRail(w.m_stationLayerCheck) || w.m_stationLayerCheck->width() < w.m_stationLayerCheck->sizeHint().width())
					facetFailure(ok, "presentation", "the first layer toggle is cut in the case and layers rail with Loaded Data open");
			}
			w.m_loadedDataDock->hide();
			w.m_caseLayersDock->hide();
			w.m_document->mutableModel().name = caseName;
			w.updateCaseLayersPanel();

			// The fields of a service at the same size: the running performance field is not cut at the right.
			w.m_serviceDock->show();
			w.m_serviceDock->raise();
			w.resizeDocks({w.m_serviceDock}, {w.m_serviceDock->sizeHint().width()}, Qt::Horizontal);
			QApplication::processEvents();
			auto* serviceFields = w.m_servicePanel->pages() ? qobject_cast<QScrollArea*>(w.m_servicePanel->pages()->widget(0)) : nullptr;
			QDoubleSpinBox* performanceEdit = w.m_servicePanel->performancePercentEdit();
			if (!serviceFields || !performanceEdit->isVisible())
				facetFailure(ok, "presentation", "the fields of a service are not shown at 1280 by 698 pixels");
			else {
				const QPoint editRight = performanceEdit->mapTo(serviceFields->viewport(), QPoint(performanceEdit->width(), 0));
				if (serviceFields->horizontalScrollBar()->isVisible() || editRight.x() > serviceFields->viewport()->width())
					facetFailure(ok, "presentation", "the fields of a service are cut at the right at 1280 by 698 pixels");
			}
			const QString timetableFailure = timetableTabFailure();
			if (!timetableFailure.isEmpty())
				facetFailure(ok, "presentation", timetableFailure + " at 1280 by 698 pixels");

			// Beside Case and Layers in a window of 819 by 614 pixels (1024 by 768 at 125 percent) the Services dock opens at the
			// width of its size hint, and its content fits without a horizontal scroll bar. Tabs that do not fit scroll and keep their
			// whole names. Floated, the dock opens inside the screen, as wide as its content.
			w.resize(819, 614);
			w.m_caseLayersDock->show();
			w.m_serviceDock->widget()->updateGeometry();
			QApplication::processEvents();
			w.resizeDocks({w.m_serviceDock}, {w.m_serviceDock->sizeHint().width()}, Qt::Horizontal);
			QApplication::processEvents();
			auto* serviceScroll = w.m_serviceDock->findChild<QScrollArea*>("editorContentScroll");
			const QTabBar* serviceTabs = w.m_servicePanel->pages()->tabBar();
			const int lastTabRight = serviceTabs->tabRect(serviceTabs->count() - 1).right();
			if (serviceScroll->horizontalScrollBar()->isVisible() || w.m_servicePanel->width() > serviceScroll->viewport()->width())
				facetFailure(ok, "presentation", "the Services dock scrolls sideways at 819 by 614 pixels");
			else if (lastTabRight >= serviceTabs->width() && (!serviceTabs->usesScrollButtons() || serviceTabs->elideMode() != Qt::ElideNone))
				facetFailure(ok, "presentation",
					QString("the tabs of the Services dock need %1 of %2 pixels and are cut at 819 by 614 pixels").arg(lastTabRight + 1).arg(serviceTabs->width()));
			w.m_serviceDock->setFloating(true);
			QApplication::processEvents();
			if (!w.m_serviceDock->screen()->availableGeometry().contains(w.m_serviceDock->frameGeometry())
				|| serviceScroll->horizontalScrollBar()->isVisible())
				facetFailure(ok, "presentation", "the floated Services dock opens outside the screen or narrower than its content");
			// A double click on the title of the docked dock floats it inside the screen as well, once the button is released, also
			// when it last floated past the edge of the screen. Qt keeps the floating geometry of a dock when it is resized.
			const QRect serviceScreen = w.m_serviceDock->screen()->availableGeometry();
			w.m_serviceDock->setGeometry(QRect(QPoint(serviceScreen.right() - 100, serviceScreen.top()), w.m_serviceDock->size() - QSize(1, 1)));
			QApplication::processEvents();
			w.m_serviceDock->setFloating(false);
			QApplication::processEvents();
			const QPoint serviceTitle = w.m_serviceDock->mapTo(&w, QPoint(w.m_serviceDock->width() / 2, w.m_serviceDock->widget()->y() / 2));
			int clickTime = 1000000;
			for (const QEvent::Type type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease, QEvent::MouseButtonPress, QEvent::MouseButtonRelease})
				qt_handleMouseEvent(w.windowHandle(), serviceTitle, w.mapToGlobal(serviceTitle),
					type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton, Qt::LeftButton, type, Qt::NoModifier, ++clickTime);
			QElapsedTimer floatWaited;
			floatWaited.start();
			while (floatWaited.elapsed() < 2000 && !(w.m_serviceDock->isFloating() && serviceScreen.contains(w.m_serviceDock->frameGeometry()))) {
				QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
				QThread::msleep(5);
			}
			if (!w.m_serviceDock->isFloating() || !serviceScreen.contains(w.m_serviceDock->frameGeometry()))
				facetFailure(ok, "presentation", "the Services dock floated by a double click opens outside the screen");
			w.m_serviceDock->setFloating(false);
			w.m_caseLayersDock->hide();
			w.resize(1280, 698);
			w.m_serviceDock->hide();

			// The Compositions dock at the same size: its buttons are inside the dock, docked and floating.
			w.m_compositionDock->show();
			w.m_compositionDock->raise();
			QApplication::processEvents();
			const QString docked698 = compositionButtonFailure(640);
			if (!docked698.isEmpty())
				facetFailure(ok, "presentation", docked698 + " at 1280 by 698 pixels");
			w.m_compositionDock->setFloating(true);
			QApplication::processEvents();
			const QString floating698 = compositionButtonFailure();
			if (!floating698.isEmpty())
				facetFailure(ok, "presentation", floating698 + " in a floating dock at 1280 by 698 pixels");
			w.m_compositionDock->setFloating(false);
			w.m_compositionDock->hide();

			// The Incidents dock at the same size: the scenario header sits above the tabs, and the Add button and the
			// fields of the selected incident are inside the viewport of the dock, also at 1.5 times the font.
			const QFont incidentFont = w.m_incidentDock->font();
			for (const double scale : {1.0, 1.5}) {
				QFont font = incidentFont;
				font.setPointSizeF(incidentFont.pointSizeF() * scale);
				w.m_incidentDock->setFont(font);
				w.m_incidentDock->show();
				w.m_incidentDock->raise();
				auto* incidentTabs = w.m_incidentDock->findChild<QTabWidget*>("scenarioEditorTabs");
				auto* incidentHeader = w.m_incidentDock->findChild<QLabel*>("scenarioHeaderLabel");
				auto* incidentScroll = w.m_incidentDock->findChild<QScrollArea*>("editorContentScroll");
				if (!incidentTabs || !incidentHeader || !incidentScroll || !w.m_incidentPanel->selectedIncident()) {
					facetFailure(ok, "presentation", "the Incidents dock lacks its header, tabs or a selected incident");
					break;
				}
				const auto inViewport = [incidentScroll](const QWidget* widget) {
					const QPoint topLeft = widget->mapTo(incidentScroll->viewport(), QPoint(0, 0));
					return widget->isVisibleTo(incidentScroll) && QRect(topLeft, widget->size()).intersects(incidentScroll->viewport()->rect())
						&& incidentScroll->viewport()->rect().contains(widget->mapTo(incidentScroll->viewport(), widget->rect().center()));
				};
				incidentTabs->setCurrentWidget(w.m_scenarioPanel);
				QApplication::processEvents();
				incidentScroll->verticalScrollBar()->setValue(0);
				if (incidentHeader->mapTo(w.m_incidentDock, QPoint(0, 0)).y() >= incidentTabs->mapTo(w.m_incidentDock, QPoint(0, 0)).y()
					|| !inViewport(incidentHeader))
					facetFailure(ok, "presentation", "the scenario header is not above the tabs at 1280 by 698 pixels");
				for (const QPushButton* button : {w.m_blankScenarioButton, w.m_duplicateScenarioButton, w.m_deleteScenarioButton,
						 w.m_scenarioPanel->importButton(), w.m_scenarioPanel->exportButton()}) {
					if (!inViewport(button))
						facetFailure(ok, "presentation", "a scenario button is outside the dock viewport at 1280 by 698 pixels");
				}
				incidentTabs->setCurrentWidget(w.m_incidentPanel);
				QApplication::processEvents();
				incidentScroll->verticalScrollBar()->setValue(0);
				const bool endCheckFirst = w.m_incidentHasEndSecondsCheck->mapTo(w.m_incidentPanel, QPoint(0, w.m_incidentHasEndSecondsCheck->height())).y()
					<= w.m_incidentEndSecondsEdit->mapTo(w.m_incidentPanel, QPoint(0, 0)).y();
				if (!inViewport(w.m_addIncidentButton) || !inViewport(w.m_incidentIdEdit) || !inViewport(w.m_incidentTypeCombo))
					facetFailure(ok, "presentation", "the incident list or the first fields are outside the dock viewport at 1280 by 698 pixels");
				if (!endCheckFirst)
					facetFailure(ok, "presentation", "the recovery end check box does not come before its field");
				if (incidentScroll->horizontalScrollBar()->isVisible())
					facetFailure(ok, "presentation", "the Incidents dock scrolls sideways at 1280 by 698 pixels");
				incidentTabs->setCurrentWidget(w.m_entranceDelayPanel);
				QApplication::processEvents();
				incidentScroll->verticalScrollBar()->setValue(0);
				if (!inViewport(w.m_addEntranceDelayButton) || !inViewport(w.m_entranceDelayServiceCombo))
					facetFailure(ok, "presentation", "the entrance delay list or its first field is outside the dock viewport at 1280 by 698 pixels");
				if (incidentScroll->horizontalScrollBar()->isVisible())
					facetFailure(ok, "presentation", "the Entrance delays tab scrolls the dock sideways at 1280 by 698 pixels");
				incidentTabs->setCurrentWidget(w.m_scenarioPanel);
			}
			w.m_incidentDock->hide();
			w.m_incidentDock->setFont(incidentFont);
			w.resize(1280, 800);
			QApplication::processEvents();
		}
		if (ok) {
			// Run Results over Loaded Data in a window of 1280 by 800 pixels, after a run whose case name and baseline have
			// 300 characters without a space: the labels wrap or clip, so the dock needs no more width than its buttons.
			w.showNormal();
			w.resize(1280, 800);
			w.m_loadedDataDock->show();
			w.m_runResultsDock->show();
			w.m_runResultsDock->raise();
			QApplication::processEvents();
			const int shortMinimumWidth = w.m_runResultsDock->minimumSizeHint().width();

			RunProvenance provenance;
			provenance.caseName = std::string(300, 'W');
			provenance.appliedScenario = "baseline";
			CompletedRunData completed;
			completed.provenance = provenance;
			completed.results.trains.emplace_back();
			completed.results.trains.back().trainId = "train-1";
			w.m_runController->m_completedRun = std::make_shared<const CompletedRunData>(completed);
			DelayRunSnapshot baseline;
			baseline.provenance = provenance;
			baseline.scenarioId = "baseline";
			w.m_runController->m_delayBaseline = baseline;
			w.refreshRunResults();
			w.updateDiagramActions();
			QApplication::processEvents();

			const QDockWidget* results = w.m_runResultsDock;
			const QStringList summaryLines = w.m_runResultsSummaryLabel->text().split('\n');
			QStringList captions;
			for (const QLabel* label : results->findChildren<QLabel*>(QRegularExpression("Caption$")))
				captions << label->text();
			if (!w.tabifiedDockWidgets(w.m_loadedDataDock).contains(w.m_runResultsDock) || !results->isVisible())
				facetFailure(ok, "presentation", "Run Results is not shown over Loaded Data, so the check proves nothing");
			else if (w.width() > 1280 || w.height() > 800)
				facetFailure(ok, "presentation", "Run Results enlarged the main window");
			else if (results->minimumSizeHint().width() > shortMinimumWidth || results->minimumSizeHint().width() > w.width() / 2
				|| results->minimumSizeHint().height() > w.height() / 2)
				facetFailure(ok, "presentation", "a long run identity raises the minimum size of Run Results");
			else if (!summaryLines.first().startsWith("Run: ") || !summaryLines.contains("Status: Completed")
				|| !summaryLines.last().startsWith("Delay baseline: "))
				facetFailure(ok, "presentation", "the run, the status and the delay baseline are not on lines of their own in Run Results");
			else if (captions != QStringList({"Results", "Delay comparison"}))
				facetFailure(ok, "presentation", "the button rows of Run Results have no captions");
			else if (w.m_runResultsSummaryLabel->width() > results->width() || w.m_delayFeedbackLabel->width() > results->width())
				facetFailure(ok, "presentation", "a long run identity widens the labels of Run Results");

			// The line says how many ticked occurrences are in the run period when some are not, and stays short otherwise.
			w.m_runController->m_lastRunSelectedOccurrences = 196;
			w.m_runController->m_lastRunTotalOccurrences = 196;
			w.m_runController->m_lastRunSelectedInPeriodOccurrences = 185;
			w.refreshRunResultsSummary();
			const QString outsidePeriod = w.m_runResultsSummaryLabel->text();
			w.m_runController->m_lastRunSelectedInPeriodOccurrences = 196;
			w.refreshRunResultsSummary();
			const QString insidePeriod = w.m_runResultsSummaryLabel->text();
			if (!outsidePeriod.contains("\nOccurrences: 196/196 selected, 185 in the run period\nStatus: Completed")
				|| !insidePeriod.contains("\nOccurrences: 196/196 selected\nStatus: Completed"))
				facetFailure(ok, "presentation", "the occurrences line of Run Results does not say how many selected occurrences are in the run period");

			// With a short run identity the dock opens at a third of the window height, or higher only as far as the table needs to
			// show three rows.
			completed.provenance.caseName = "Paimpol";
			w.m_runController->m_completedRun = std::make_shared<const CompletedRunData>(completed);
			w.m_runController->m_delayBaseline.reset();
			w.m_runResultsDock->hide();
			w.refreshRunResults();
			QApplication::processEvents();
			const int shownRows = w.m_runResultsTable->viewport()->height() / w.m_runResultsTable->verticalHeader()->defaultSectionSize();
			if (results->height() > w.height() / 3 && shownRows > 3)
				facetFailure(ok, "presentation",
					QString("Run Results opens %1 pixels high with %2 rows in a window %3 pixels high").arg(results->height()).arg(shownRows).arg(w.height()));

			// Four editors opened from the Editors menu while Run Results is open share one group of tabs, and each shows at least
			// three rows of fields once raised.
			const QList<QDockWidget*> editorDocks{w.m_caseSettingsDock, w.m_infrastructureDock, w.m_serviceDock, w.m_trainUnitDock};
			for (QDockWidget* dock : editorDocks)
				w.m_editorActions.value(dock)->trigger();
			for (QDockWidget* dock : editorDocks.mid(1))
				if (!w.tabifiedDockWidgets(w.m_caseSettingsDock).contains(dock))
					facetFailure(ok, "presentation", QString("%1 is not tabbed with Case Settings").arg(dock->objectName()));
			const int rowHeight = w.m_caseNameEdit->sizeHint().height();
			for (QDockWidget* dock : editorDocks) {
				dock->raise();
				QApplication::processEvents();
				const int shown = dock->findChild<QScrollArea*>("editorContentScroll")->viewport()->height();
				if (shown < 3 * rowHeight)
					facetFailure(ok, "presentation",
						QString("%1 shows %2 pixels of its fields with four editors and Run Results open at 1280 by 800 pixels")
							.arg(dock->objectName())
							.arg(shown));
			}
			// Choosing Case Settings in the Editors menu while it is open behind Rolling stock units brings it to the front.
			QAction* caseSettingsAction = w.m_editorActions.value(w.m_caseSettingsDock);
			caseSettingsAction->trigger();
			QApplication::processEvents();
			if (!caseSettingsAction->isChecked() || w.m_caseSettingsDock->visibleRegion().isEmpty())
				facetFailure(ok, "presentation", "choosing Case Settings behind another editor tab in the Editors menu does not bring it to the front");

			// In a window of 1024 by 768 pixels, beside Case and Layers and with Run Results as high as it opens after a run, Rolling
			// stock units shows its list, its buttons in one row, the ID field and its tabs without a vertical scroll, and Services
			// shows its list, its buttons in one row, its tabs and room for three rows of fields. In a window of 819 by 614 pixels
			// (1024 by 768 at 125 percent) the taller controls of this test leave room for the list, the buttons and the ID field of
			// Rolling stock units and for one row of Services fields. The Timetable tab leaves the height of the Services list as it
			// is. How many fields fill that room, and whether the narrower dock scrolls sideways, depends on the width of the font.
			w.m_loadedDataDock->hide();
			w.m_caseLayersDock->show();
			const auto inView = [](const QWidget* part, const QWidget* view) {
				const int top = part->mapTo(view, QPoint(0, 0)).y();
				return part->isVisible() && top >= 0 && top + part->height() <= view->height();
			};
			const auto partName = [](const QWidget* part) { return qobject_cast<const QTabBar*>(part) ? QStringLiteral("the tab row") : part->objectName(); };
			const auto dockView = [](QDockWidget* dock) {
				auto* scroll = dock->findChild<QScrollArea*>("editorContentScroll");
				scroll->verticalScrollBar()->setValue(0);
				return scroll->viewport();
			};
			TrainUnitPanel* units = w.m_trainUnitPanel;
			ServicePanel* services = w.m_servicePanel;
			for (const QSize size : {QSize(1024, 768), QSize(819, 614)}) {
				const bool fullSize = size.width() == 1024;
				const QString where = QString(" at %1 by %2 pixels after a run").arg(size.width()).arg(size.height());
				for (QDockWidget* dock : editorDocks)
					dock->hide();
				w.m_runResultsDock->hide();
				w.resize(size);
				QApplication::processEvents();
				w.refreshRunResults();
				QApplication::processEvents();
				for (QDockWidget* dock : editorDocks)
					w.m_editorActions.value(dock)->trigger();
				QApplication::processEvents();
				w.m_serviceDock->widget()->updateGeometry();
				QApplication::processEvents();
				w.resizeDocks({w.m_serviceDock}, {w.m_serviceDock->sizeHint().width()}, Qt::Horizontal);
				QApplication::processEvents();
				w.m_trainUnitDock->raise();
				units->tabs()->setCurrentIndex(0);
				QApplication::processEvents();
				const QWidget* unitsView = dockView(w.m_trainUnitDock);
				QList<const QWidget*> unitParts{units->listWidget(), units->addButton(), units->duplicateButton(), units->deleteButton(), units->idEdit()};
				if (fullSize)
					unitParts.append(units->tabs()->tabBar());
				for (const QWidget* part : unitParts)
					if (!inView(part, unitsView))
						facetFailure(ok, "presentation", QString("%1 of Rolling stock units is not in view%2").arg(partName(part), where));
				if (units->addButton()->y() != units->deleteButton()->y() || units->duplicateButton()->y() != units->deleteButton()->y())
					facetFailure(ok, "presentation", "the buttons under the list of Rolling stock units are not in one row");

				w.m_serviceDock->raise();
				services->pages()->setCurrentIndex(0);
				QApplication::processEvents();
				const QWidget* servicesView = dockView(w.m_serviceDock);
				auto* fieldsScroll = qobject_cast<QScrollArea*>(services->pages()->widget(0));
				fieldsScroll->verticalScrollBar()->setValue(0);
				const QList<const QWidget*> serviceParts{services->listWidget(), services->addButton(), services->duplicateButton(),
					services->deleteButton(), services->pages()->tabBar()};
				for (const QWidget* part : serviceParts)
					if (!inView(part, servicesView))
						facetFailure(ok, "presentation", QString("%1 of Services is not in view%2").arg(partName(part), where));
				if (services->addButton()->y() != services->deleteButton()->y() || services->duplicateButton()->y() != services->deleteButton()->y())
					facetFailure(ok, "presentation", "the buttons under the list of Services are not in one row");
				const int fieldsTop = fieldsScroll->viewport()->mapTo(servicesView, QPoint(0, 0)).y();
				const int fieldsShown = std::min(servicesView->height(), fieldsTop + fieldsScroll->viewport()->height()) - fieldsTop;
				if (fieldsShown < (fullSize ? 3 : 1) * rowHeight)
					facetFailure(ok, "presentation", QString("Services shows %1 pixels of its fields%2").arg(fieldsShown).arg(where));
				const int listHeight = services->listWidget()->height();
				services->pages()->setCurrentIndex(1);
				QApplication::processEvents();
				if (services->listWidget()->height() != listHeight || !inView(services->pages()->tabBar(), servicesView))
					facetFailure(ok, "presentation",
						QString("the Timetable tab changes the list of Services from %1 to %2 pixels high or moves the tab row out of view%3")
							.arg(listHeight)
							.arg(services->listWidget()->height())
							.arg(where));
				services->pages()->setCurrentIndex(0);
			}
			// With Incidents as a fifth editor the tabs of the group keep their whole names and scroll.
			w.m_editorActions.value(w.m_incidentDock)->trigger();
			QApplication::processEvents();
			const QTabBar* editorTabs = nullptr;
			for (const QTabBar* bar : w.findChildren<QTabBar*>(QString(), Qt::FindDirectChildrenOnly))
				for (int index = 0; index < bar->count(); ++index)
					if (bar->isVisible() && bar->tabText(index) == w.m_caseSettingsDock->windowTitle())
						editorTabs = bar;
			if (!editorTabs || editorTabs->count() != 5 || editorTabs->elideMode() != Qt::ElideNone || !editorTabs->usesScrollButtons())
				facetFailure(ok, "presentation", "the tabs of five editors are cut instead of scrolling");
			// Tab and Shift+Tab go from the network view round the window and back. They reach the editor in front, also just after
			// its tab is chosen, and pass over the editors behind it, which Qt keeps out of sight, and the scroll buttons of their tab bars.
			const QList<QDockWidget*> fiveEditors = editorDocks + QList<QDockWidget*>{w.m_incidentDock};
			w.activateWindow();
			for (QDockWidget* front : {w.m_incidentDock, w.m_caseSettingsDock}) {
				front->raise();
				for (const bool forward : {true, false}) {
					w.networkView->setFocus(Qt::TabFocusReason);
					QApplication::processEvents();
					QStringList hiddenStops;
					bool reachedFront = false;
					for (int press = 0; press < 150 && QApplication::focusWidget(); ++press) {
						QKeyEvent key(QEvent::KeyPress, forward ? Qt::Key_Tab : Qt::Key_Backtab, forward ? Qt::NoModifier : Qt::ShiftModifier);
						QApplication::sendEvent(QApplication::focusWidget(), &key);
						QApplication::processEvents();
						QWidget* focused = QApplication::focusWidget();
						if (!focused || focused == w.networkView)
							break;
						reachedFront = reachedFront || front->isAncestorOf(focused);
						if ((qobject_cast<QTabBar*>(focused->parentWidget()) && focused->parentWidget()->parentWidget() == &w)
							|| std::any_of(fiveEditors.cbegin(), fiveEditors.cend(),
								[front, focused](const QDockWidget* dock) { return dock != front && dock->isAncestorOf(focused); }))
							hiddenStops << QString("%1(%2)").arg(focused->metaObject()->className(), focused->objectName());
					}
					if (QApplication::focusWidget() != w.networkView || !reachedFront || !hiddenStops.isEmpty())
						facetFailure(ok, "presentation",
							QString("%1 with %2 in front does not go round the visible controls; hidden stops: %3")
								.arg(forward ? "Tab" : "Shift+Tab", front->objectName(), hiddenStops.mid(0, 5).join(", ")));
				}
			}
			w.m_incidentDock->hide();
			w.m_caseLayersDock->hide();
			w.resize(1280, 800);
			for (QDockWidget* dock : editorDocks)
				dock->hide();

			w.m_runController->m_completedRun.reset();
			w.m_runController->m_delayBaseline.reset();
			w.m_runResultsTable->setRowCount(0);
			w.m_runResultsDock->hide();
			w.m_loadedDataDock->hide();
			w.updateDiagramActions();
		}
		if (ok && dockWheelFields == 0)
			facetFailure(ok, "presentation", "no editor dock had a field for the wheel check");
		if (ok) std::fprintf(stdout, "E2E_EDITOR_LAYOUT_OK\n");
	}
	if (ok && w.m_document->isLoaded()) {
		// Visiting an editor must not change the case: tabbing through the fields, resizing, scrolling and reopening the docks
		// leave the dirty flag and the revision as they were.
		bool facetOk = true;
		bool dirtyBefore = w.m_document->isDirty();
		quint64 revisionBefore = w.m_document->revision();
		const auto expectUntouched = [&](const QDockWidget* dock, const char* step) {
			if (w.m_document->isDirty() == dirtyBefore && w.m_document->revision() == revisionBefore)
				return;
			facetFailure(facetOk, "untouched fields",
				QString("%1 changed the case during %2").arg(dock->objectName(), step));
			dirtyBefore = w.m_document->isDirty();
			revisionBefore = w.m_document->revision();
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
	// Opening another case shows its data in the docks, also when it lists the selected service at another row.
	if (ok && w.m_document->isLoaded()) {
		QTemporaryDir caseChangeTempDir;
		QString caseChangeOut = qEnvironmentVariable("QEGTRAIN_E2E_OUT");
		if (caseChangeOut.isEmpty())
			caseChangeOut = caseChangeTempDir.path();
		const QString changedPath = QDir(caseChangeOut).filePath("editor_smoke_changed_case");
		QDir(changedPath).removeRecursively();
		const auto caseChangeFailure = [&](const char* message) {
			ok = false;
			failures << QString("case change: ") + message;
		};
		if (!w.openSceneDirectory(scenePath) || !w.m_document->isLoaded()) {
			caseChangeFailure("the scene did not open");
		} else {
			const SceneModel original = w.m_document->model();
			const auto withStops = std::find_if(original.services.begin(), original.services.end(),
				[](const SceneService& service) { return !service.stops.empty(); });
			if (withStops == original.services.end()) {
				caseChangeFailure("the scene has no service with a stop");
			} else {
				const std::string serviceId = withStops->id;
				SceneModel changed = original;
				changed.name += " changed";
				for (SceneService& service : changed.services) {
					if (service.id == serviceId)
						service.stops.front().dwellSeconds += 7.0;
				}
				std::reverse(changed.services.begin(), changed.services.end());
				const auto dwellText = [](const SceneModel& model, const std::string& id) {
					for (const SceneService& service : model.services) {
						if (service.id == id)
							return QString::fromStdString(csv::formatDouble(service.stops.front().dwellSeconds));
					}
					return QString();
				};
				const auto serviceShown = [&w](const std::string& id) {
					const QListWidgetItem* item = w.m_serviceListWidget->currentItem();
					return item && item->text() == QString::fromStdString(id);
				};
				const auto shownDwell = [&w] {
					const QTableWidgetItem* item = w.m_timetableStopsPanel->table()->item(0, 2);
					return item ? item->text() : QString();
				};
				if (!::saveScene(changed, changedPath.toStdString()).success()) {
					caseChangeFailure("the changed copy did not save");
				} else {
					w.m_serviceListWidget->setCurrentRow(static_cast<int>(withStops - original.services.begin()));
					if (!serviceShown(serviceId) || shownDwell() != dwellText(original, serviceId)
						|| w.m_caseNameEdit->text() != QString::fromStdString(original.name))
						caseChangeFailure("the case did not show its own data");
					if (!w.openSceneDirectory(changedPath)) {
						caseChangeFailure("the changed copy did not open");
					} else {
						if (!serviceShown(serviceId))
							caseChangeFailure("the selected service was not kept");
						if (shownDwell() != dwellText(changed, serviceId))
							caseChangeFailure("the Timetable tab kept the dwell of the previous case");
						if (w.m_caseNameEdit->text() != QString::fromStdString(changed.name))
							caseChangeFailure("Case Settings kept the name of the previous case");
					}
					if (!w.openSceneDirectory(scenePath)) {
						caseChangeFailure("the original did not open again");
					} else {
						if (!serviceShown(serviceId) || shownDwell() != dwellText(original, serviceId))
							caseChangeFailure("the Timetable tab kept the dwell of the changed case");
						if (w.m_caseNameEdit->text() != QString::fromStdString(original.name))
							caseChangeFailure("Case Settings kept the name of the changed case");
					}
				}
			}
		}
		if (ok)
			std::fprintf(stdout, "E2E_EDITOR_CASE_CHANGE_OK\n");
	}
	// Last, because it opens copies of the scene and leaves another scene and selection behind.
	if (ok && w.m_document->isLoaded()) {
		QTemporaryDir undoTempDir;
		QString undoOut = qEnvironmentVariable("QEGTRAIN_E2E_OUT");
		if (undoOut.isEmpty())
			undoOut = undoTempDir.path();
		const QStringList undoFailures = checkUndoRedo(w, scenePath, undoOut);
		for (const QString& failure : undoFailures) {
			ok = false;
			failures << failure;
		}
		if (undoFailures.isEmpty())
			std::fprintf(stdout, "E2E_EDITOR_UNDO_REDO_OK\n");
	}

	if (ok && w.m_document->isLoaded()) {
		QTemporaryDir recoveryTempDir;
		QString recoveryOut = qEnvironmentVariable("QEGTRAIN_E2E_OUT");
		if (recoveryOut.isEmpty())
			recoveryOut = recoveryTempDir.path();
		const QStringList recoveryFailures = checkRecovery(w, scenePath, recoveryOut);
		for (const QString& failure : recoveryFailures) {
			ok = false;
			failures << failure;
		}
		if (recoveryFailures.isEmpty())
			std::fprintf(stdout, "E2E_EDITOR_RECOVERY_OK\n");
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

// Undo and Redo on a copy of the scene at scenePath, which is written below outFolder. An edit in each editor adds one step to
// the history, Undo and Redo walk the scene through the states before and after the steps, and the history keeps its place
// across Save, a new edit, a failed open and runs, and starts again for another scene. Returns what failed.
QStringList MainWindowTestAccess::checkUndoRedo(MainWindow& w, const QString& scenePath, const QString& outFolder) {
	QStringList failures;
	const auto expect = [&failures](bool condition, const QString& message) {
		if (!condition)
			failures << QStringLiteral("undo/redo: ") + message;
		return condition;
	};
	const auto process = []() { QApplication::processEvents(); };
	const QString previousScenePath = w.m_session->m_sceneDir;
	const auto finish = [&]() {
		simulation.setNextRunOutputFolder({});
		if (!previousScenePath.isEmpty() && !w.openSceneDirectory(previousScenePath))
			failures << QStringLiteral("undo/redo: the scene before the checks did not reopen");
		return failures;
	};

	// The Edit menu stands before View and holds the two actions with the keys of the platform, Find after them and
	// Preferences last, which macOS shows in the application menu. The editor docks carry Undo and Redo too, for the time
	// that a dock floats as a window of its own.
	{
		const QList<QAction*> menus = w.menuBar()->actions();
		const int editMenuIndex = menus.indexOf(w.ui->menuEdit->menuAction());
		expect(editMenuIndex >= 0 && menus.value(editMenuIndex + 1) == w.ui->menuView->menuAction(),
			"the Edit menu is not before View");
		const QList<QAction*> editActions = w.ui->menuEdit->actions();
		expect(editActions.mid(0, 2) == QList<QAction*>({w.m_undoAction, w.m_redoAction}) && editActions.contains(w.m_findAction),
			"the Edit menu does not hold Undo, Redo and Find");
		const QAction* preferences = editActions.last();
		expect(preferences->objectName() == QStringLiteral("actionSettings") && preferences->menuRole() == QAction::PreferencesRole
				&& preferences->shortcut() == QKeySequence(QKeySequence::Preferences),
			"the Edit menu does not end with Preferences");
		expect(w.m_undoAction->shortcut() == QKeySequence(QKeySequence::Undo)
				&& w.m_redoAction->shortcuts() == QKeySequence::keyBindings(QKeySequence::Redo)
				&& w.m_undoAction->shortcutContext() == Qt::WindowShortcut
				&& w.m_redoAction->shortcutContext() == Qt::WindowShortcut,
			"Undo and Redo do not use the standard keys of the platform");
		for (QDockWidget* dock : {w.m_caseSettingsDock, w.m_infrastructureDock, w.m_trainUnitDock, w.m_compositionDock,
				 w.m_serviceDock, w.m_incidentDock, w.m_passengerDock})
			expect(dock->actions().contains(w.m_undoAction) && dock->actions().contains(w.m_redoAction),
				dock->objectName() + " does not carry Undo and Redo");
	}

	// The scene is a copy, so that Save does not touch the committed scene.
	const QString copyPath = QDir(outFolder).filePath("editor_smoke_undo_scene");
	QDir(copyPath).removeRecursively();
	if (!w.openSceneDirectory(scenePath) || !::saveScene(w.m_document->mutableModel(), copyPath.toStdString()).success()
		|| !w.openSceneDirectory(copyPath)) {
		failures << QStringLiteral("undo/redo: the copy of the scene did not open");
		return finish();
	}
	expect(!w.m_history->canUndo() && !w.m_history->canRedo() && w.m_history->isAtSavedPoint() && !w.m_document->isDirty()
			&& !w.m_undoAction->isEnabled() && !w.m_redoAction->isEnabled(),
		"a scene that was just opened has a history to undo");

	// Typing that is not committed is no edit.
	{
		const quint64 revision = w.m_document->revision();
		w.m_caseNameEdit->setText("typing that is not committed");
		process();
		expect(w.m_history->undoCount() == 0 && w.m_document->revision() == revision && !w.m_undoAction->isEnabled(),
			"typing created a step");
		w.m_caseNameEdit->setText(QString::fromStdString(w.m_document->mutableModel().name));
	}

	std::vector<std::string> states{authoredSceneBytes(w.m_document->mutableModel())};
	// The scenarios that the window marks as edited, in each state.
	std::vector<std::set<std::string>> modifiedStates{w.modifiedScenarioIds()};
	std::size_t position = 0;
	std::optional<std::size_t> savedState = 0;

	// What the editors show must be the scene, whatever way the scene was reached.
	const auto editorMismatch = [&w]() -> QString {
		const SceneModel& model = w.m_document->mutableModel();
		const SceneScenario* scenario = w.selectedScenario();
		const auto count = [](const QListWidget* list) { return static_cast<std::size_t>(list->count()); };
		if (w.m_caseNameEdit->text().toStdString() != model.name)
			return QStringLiteral("case settings");
		if (count(w.m_trainUnitListWidget) != model.trainUnits.size())
			return QStringLiteral("rolling stock");
		if (count(w.m_compositionListWidget) != model.compositions.size())
			return QStringLiteral("composition");
		if (count(w.m_serviceListWidget) != model.services.size())
			return QStringLiteral("service");
		const int serviceRow = w.m_serviceListWidget->currentRow();
		if (serviceRow >= 0 && serviceRow < w.m_serviceListWidget->count()
			&& static_cast<std::size_t>(w.m_stopTableWidget->rowCount()) != model.services[static_cast<std::size_t>(serviceRow)].stops.size())
			return QStringLiteral("timetable");
		if (count(w.m_scenarioListWidget) != model.scenarios.size())
			return QStringLiteral("scenario");
		if (!scenario || count(w.m_incidentListWidget) != scenario->incidents.size())
			return QStringLiteral("incident");
		if (count(w.m_entranceDelayListWidget) != scenario->entranceDelays.size())
			return QStringLiteral("entrance delay");
		if (count(w.m_passengerListWidget) != model.passengers.size())
			return QStringLiteral("passenger");
		const QString facet = w.m_infrastructureFacetCombo->currentData().toString();
		const std::size_t rows = facet == QLatin1String("tracks") ? model.tracks.size()
			: facet == QLatin1String("arcs")					  ? model.arcs.size()
			: facet == QLatin1String("nodes")					  ? model.nodes.size()
																  : static_cast<std::size_t>(w.m_infrastructureTable->rowCount());
		if (static_cast<std::size_t>(w.m_infrastructureTable->rowCount()) != rows)
			return QStringLiteral("infrastructure");
		return {};
	};
	// The validation that the window shows is the validation of the scene it holds.
	const auto validationCurrent = [&w]() {
		const std::vector<SceneDiagnostic> shown = w.m_session->m_sceneDiagnostics;
		w.refreshValidationPanel();
		return shown.size() == w.m_session->m_sceneDiagnostics.size() && w.m_validation->table()->rowCount() == static_cast<int>(shown.size())
			&& std::equal(shown.begin(), shown.end(), w.m_session->m_sceneDiagnostics.begin(),
				[](const SceneDiagnostic& a, const SceneDiagnostic& b) {
					return a.code == b.code && a.path == b.path && a.message == b.message;
				});
	};
	// The scene, the unsaved mark, the run results and the editors after Undo or Redo reached state `at`.
	const auto checkReached = [&](std::size_t at, const char* how) {
		const QString where = QStringLiteral("%1 to state %2").arg(QLatin1String(how)).arg(at);
		const bool dirty = !savedState || *savedState != at;
		expect(authoredSceneBytes(w.m_document->mutableModel()) == states[at], where + " did not restore the scene");
		expect(w.m_history->undoCount() == at && w.m_history->redoCount() == states.size() - 1 - at,
			where + " moved the history to another place");
		expect(w.m_document->isDirty() == dirty && w.isWindowModified() == dirty && w.m_history->isAtSavedPoint() == !dirty,
			where + " left a wrong unsaved mark");
		expect(!w.m_runController->m_resultsAvailable && !w.m_runController->m_completedRun, where + " kept results of a run");
		const QString mismatch = editorMismatch();
		expect(mismatch.isEmpty(), where + " left the " + mismatch + " editor with another scene");
		expect(validationCurrent(), where + " left the validation of another scene");
		expect(w.modifiedScenarioIds() == modifiedStates[at], where + " left other scenarios marked as edited");
		expect(w.m_runController->m_startOffsetSeconds == baseTimeToSeconds(w.m_document->mutableModel().baseTime), where + " left the start time of another scene");
		expect(std::any_of(w.m_document->mutableModel().scenarios.begin(), w.m_document->mutableModel().scenarios.end(),
				   [&w](const SceneScenario& scenario) { return scenario.id == w.m_runSelection.scenarioId(); }),
			where + " left a scenario selected that the scene does not have");
		expect(w.m_undoAction->isEnabled() == (at > 0) && w.m_redoAction->isEnabled() == (at + 1 < states.size()),
			where + " left the actions in a wrong state");
	};
	const auto undo = [&]() {
		if (!expect(w.m_undoAction->isEnabled(), QStringLiteral("Undo is disabled at state %1").arg(position)))
			return;
		w.m_undoAction->trigger();
		process();
		checkReached(--position, "Undo");
	};
	const auto redo = [&]() {
		if (!expect(w.m_redoAction->isEnabled(), QStringLiteral("Redo is disabled at state %1").arg(position)))
			return;
		w.m_redoAction->trigger();
		process();
		checkReached(++position, "Redo");
	};
	// One edit must add exactly one step, that the scene and the head of the history agree on, and drop what could be redone.
	const auto step = [&](const char* name, const char* label, const std::function<void()>& edit) {
		const std::size_t undoBefore = w.m_history->undoCount();
		const std::string before = authoredSceneBytes(w.m_document->mutableModel());
		edit();
		process();
		const std::string after = authoredSceneBytes(w.m_document->mutableModel());
		const QString what = QLatin1String(name);
		bool good = expect(after != before, what + " did not change the scene");
		good = expect(w.m_history->undoCount() == undoBefore + 1, what + " did not add exactly one step") && good;
		good = expect(!w.m_history->canRedo() && !w.m_redoAction->isEnabled(), what + " left a step to redo") && good;
		good = expect(authoredSceneBytes(*w.m_history->head()) == after, what + " changed the scene outside the history")
			&& good;
		good = expect(w.m_history->undoLabel() == label, what + " is labelled '" + QString::fromStdString(w.m_history->undoLabel()) + "'")
			&& good;
		good = expect(w.m_document->isDirty() && w.isWindowModified() && w.m_undoAction->isEnabled(), what + " left the scene clean")
			&& good;
		if (good) {
			if (savedState && *savedState > position)
				savedState.reset();
			states.resize(position + 1);
			states.push_back(after);
			modifiedStates.resize(position + 1);
			modifiedStates.push_back(w.modifiedScenarioIds());
			++position;
		}
		return good;
	};

	const auto acceptUnitChoice = [&w](const QString& unitId) {
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
	// Opens the dialog of a stop, edits it and accepts it or rejects it. Returns whether it was accepted.
	const auto editStopDialog = [&](int stopRow, bool accept, const std::function<void(QDialog*)>& edit) {
		bool accepted = false;
		QTimer timer;
		timer.setSingleShot(true);
		QObject::connect(&timer, &QTimer::timeout, &w, [&]() {
			auto* dialog = w.findChild<QDialog*>("stopEditorDialog");
			if (!dialog)
				return;
			edit(dialog);
			if (accept) {
				dialog->findChild<QPushButton*>("stopEditorAcceptButton")->click();
				accepted = dialog->result() == QDialog::Accepted;
			}
			if (!accepted)
				dialog->reject();
		});
		timer.start(0);
		w.m_timetableStopsPanel->editStop(stopRow);
		process();
		return accepted;
	};
	const auto selectServiceWithStops = [&](std::size_t stops) {
		for (std::size_t row = 0; row < w.m_document->mutableModel().services.size(); ++row) {
			if (w.m_document->mutableModel().services[row].stops.size() < stops)
				continue;
			w.m_serviceListWidget->setCurrentRow(static_cast<int>(row));
			process();
			return true;
		}
		return false;
	};

	// Edits in the case settings and the infrastructure. A run then follows, so that Undo has results to discard.
	if (!step("a case setting", "Edit", [&] {
			w.m_caseNameEdit->setText("E2E undo case");
			w.commitPendingCaseSettings();
		}))
		return finish();
	if (!step("a base time", "Edit", [&] {
			w.m_caseBaseTimeEdit->setText("09:15:30");
			w.commitPendingCaseSettings();
		}))
		return finish();
	if (!step("an infrastructure cell", "Edit", [&] {
			const int arcs = w.m_infrastructureFacetCombo->findData("arcs");
			if (arcs < 0)
				return;
			w.m_infrastructureFacetCombo->setCurrentIndex(arcs);
			process();
			if (QTableWidgetItem* cell = w.m_infrastructureTable->item(0, 6))
				cell->setText("20.5");
		}))
		return finish();
	{
		simulation.setNextRunOutputFolder(QDir(outFolder).filePath("undo_run_output").toStdString());
		w.m_runSceneAction->trigger();
		if (!expect(w.m_runController->m_worker != nullptr, "the run did not start"))
			return finish();
		const quint64 revision = w.m_document->revision();
		w.undoScene();
		w.redoScene();
		expect(!w.m_undoAction->isEnabled() && !w.m_redoAction->isEnabled() && w.m_document->revision() == revision,
			"Undo or Redo worked while a run used the scene");
		QElapsedTimer waited;
		waited.start();
		while ((w.m_runController->m_worker || !w.m_runController->m_resultsAvailable) && waited.elapsed() < 60000)
			QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
		if (!expect(!w.m_runController->m_worker && w.m_runController->m_resultsAvailable, "the run did not complete"))
			return finish();
		expect(w.m_undoAction->isEnabled() && w.m_history->undoCount() == position
				&& authoredSceneBytes(*w.m_history->head()) == states[position],
			"a run changed the history");
		undo();
		redo();
	}

	// Edits in the rolling stock, composition, service, timetable, scenario, incident, entrance delay and passenger editors.
	if (!step("an added rolling stock unit", "Add rolling stock unit", [&] { w.m_trainUnitPanel->addUnit(); }))
		return finish();
	// A link to a source file is session state: it stays with a unit that the scene still has and goes with one that it lost.
	const std::string linkedUnitId = w.m_document->mutableModel().trainUnits.front().id;
	const std::string addedUnitId = w.m_document->mutableModel().trainUnits.back().id;
	{
		QFile linkedFile(QDir(outFolder).filePath("undo_link.txt"));
		if (!linkedFile.open(QIODevice::WriteOnly)) {
			failures << QStringLiteral("undo/redo: the file for the links was not written");
			return finish();
		}
		linkedFile.close();
		TrainUnitSourceLinker::Link link;
		link.dataPath = linkedFile.fileName();
		for (const std::string& unitId : {linkedUnitId, addedUnitId})
			w.m_trainUnitSourceLinker->m_links[unitId] = link;
		w.m_trainUnitSourceLinker->refreshWatches();
	}
	if (!step("a duplicated composition", "Add composition", [&] {
			w.m_compositionListWidget->setCurrentRow(0);
			w.m_compositionPanel->duplicateComposition();
		}))
		return finish();
	if (!step("a unit added to a composition", "Add composition unit", [&] {
			w.m_compositionListWidget->setCurrentRow(w.m_compositionListWidget->count() - 1);
			acceptUnitChoice(QString::fromStdString(w.m_document->mutableModel().trainUnits.back().id));
			w.m_compositionPanel->addUnit();
		}))
		return finish();

	// Save keeps the history, and the scene is clean at the state that was saved.
	{
		const std::size_t steps = w.m_history->undoCount();
		if (!expect(w.m_session->saveSceneToCurrentDir(), "the copy of the scene was not saved"))
			return finish();
		savedState = position;
		modifiedStates[position].clear();
		expect(!w.m_document->isDirty() && !w.isWindowModified() && w.m_history->isAtSavedPoint() && w.m_history->undoCount() == steps
				&& w.m_undoAction->isEnabled(),
			"Save did not keep the history or did not mark the scene as saved");
	}

	std::set<std::string> serviceIdsBefore;
	for (const SceneService& service : w.m_document->mutableModel().services)
		serviceIdsBefore.insert(service.id);
	if (!step("a duplicated service", "Add service", [&] {
			w.m_serviceListWidget->setCurrentRow(0);
			w.m_servicePanel->duplicateService();
		}))
		return finish();
	std::string duplicatedServiceId;
	for (const SceneService& service : w.m_document->mutableModel().services) {
		if (serviceIdsBefore.count(service.id) == 0)
			duplicatedServiceId = service.id;
	}
	if (!step("a service field", "Edit", [&] {
			w.m_serviceOperatingCodeEdit->setText("1731");
			w.m_servicePanel->commitPendingEditors();
		}))
		return finish();
	if (!selectServiceWithStops(2)) {
		failures << QStringLiteral("undo/redo: no service has two stops to reorder");
		return finish();
	}
	if (!step("a moved stop", "Edit", [&] {
			w.m_stopTableWidget->setCurrentCell(0, 0);
			w.m_moveStopDownButton->click();
		}))
		return finish();
	// A stop dialog that is accepted is one step, and one that is cancelled is none.
	{
		const std::size_t steps = w.m_history->undoCount();
		const quint64 revision = w.m_document->revision();
		const bool accepted = editStopDialog(0, false, [](QDialog* dialog) {
			dialog->findChild<QCheckBox*>("stopEditorArrivalPresent")->setChecked(true);
			dialog->findChild<QLineEdit*>("stopEditorArrivalEdit")->setText("456");
		});
		expect(!accepted && w.m_history->undoCount() == steps && w.m_document->revision() == revision,
			"a cancelled stop dialog created a step");
	}
	if (!step("an accepted stop dialog", "Edit", [&] {
			editStopDialog(0, true, [](QDialog* dialog) {
				dialog->findChild<QCheckBox*>("stopEditorArrivalPresent")->setChecked(true);
				dialog->findChild<QLineEdit*>("stopEditorArrivalEdit")->setText("123.5");
			});
		}))
		return finish();
	if (!step("a deleted stop", "Delete stop", [&] {
			w.m_stopTableWidget->setCurrentCell(0, 0);
			w.m_timetableStopsPanel->removeStop();
		}))
		return finish();
	if (!step("an added scenario", "Add scenario", [&] { w.m_blankScenarioButton->click(); }))
		return finish();
	if (!step("a scenario name", "Edit", [&] {
			w.m_scenarioNameEdit->setText("E2E scenario");
			w.m_scenarioPanel->commitNameEdit();
		}))
		return finish();
	if (!step("an added incident", "Add incident", [&] { w.m_addIncidentButton->click(); }))
		return finish();
	if (!step("an added entrance delay", "Add entrance delay", [&] { w.m_addEntranceDelayButton->click(); }))
		return finish();
	if (!step("an added passenger", "Add passenger", [&] { w.m_addPassengerButton->click(); }))
		return finish();
	// The passenger editor still shows the new id when Undo restores the old one, and must not write it back.
	if (!step("a renamed passenger", "Edit", [&] {
			w.m_passengerIdEdit->setText("e2e_renamed_passenger");
			QMetaObject::invokeMethod(w.m_passengerIdEdit, "editingFinished", Qt::DirectConnection);
		}))
		return finish();
	// The duplicate is the service that nothing refers to.
	if (!step("a deleted service", "Delete service", [&] {
			for (std::size_t row = 0; row < w.m_document->mutableModel().services.size(); ++row) {
				if (w.m_document->mutableModel().services[row].id == duplicatedServiceId)
					w.m_serviceListWidget->setCurrentRow(static_cast<int>(row));
			}
			answerMessageBox(&w, QMessageBox::Yes);
			w.m_servicePanel->deleteService();
		}))
		return finish();
	expect(w.m_undoAction->text() == QLatin1String("Undo Delete service") && w.m_redoAction->text() == QLatin1String("Redo"),
		"the actions do not name the edit that Undo reverts");

	// Undo all the way back and Redo all the way forward. The saved state is the one where the scene is clean.
	const std::size_t stepCount = position;
	while (position > 0 && failures.isEmpty())
		undo();
	expect(!w.m_undoAction->isEnabled() && w.m_redoAction->isEnabled(), "Undo is available before the first edit");
	expect(w.m_trainUnitSourceLinker->linkOf(linkedUnitId) && !w.m_trainUnitSourceLinker->linkOf(addedUnitId),
		"Undo dropped the link of a unit that the scene still has, or kept the link of one that it lost");
	while (position < stepCount && failures.isEmpty())
		redo();
	expect(!w.m_redoAction->isEnabled() && w.m_undoAction->text() == QLatin1String("Undo Delete service"),
		"Redo is available after the last edit");
	if (!failures.isEmpty())
		return finish();

	// A new edit after Undo discards what could be redone, and keeps the saved state when that state is still in the history.
	const auto editDescription = [&](const char* text) {
		return [&w, text] {
			w.m_caseDescriptionEdit->setText(text);
			w.commitPendingCaseSettings();
		};
	};
	undo();
	undo();
	if (!step("an edit after Undo", "Edit", editDescription("after Undo")))
		return finish();
	expect(savedState.has_value() && !w.m_redoAction->isEnabled(), "an edit after Undo lost the saved state or kept Redo");
	while (position > *savedState && failures.isEmpty())
		undo();
	if (!failures.isEmpty())
		return finish();
	// Undo past the saved state and an edit then drop the saved state, so the scene stays unsaved until it is saved again.
	undo();
	if (!step("an edit after Undo past the saved state", "Edit", editDescription("again")))
		return finish();
	expect(!savedState.has_value() && w.m_document->isDirty(), "an edit that dropped the saved state left the scene clean");
	undo();
	redo();
	if (!failures.isEmpty())
		return finish();
	{
		const std::size_t steps = w.m_history->undoCount();
		if (!expect(w.m_session->saveSceneToCurrentDir(), "the copy of the scene was not saved again"))
			return finish();
		savedState = position;
		modifiedStates[position].clear();
		expect(!w.m_document->isDirty() && w.m_history->undoCount() == steps, "Save again did not mark the scene as saved");
		undo();
		redo();
	}

	// A failed open keeps the history. Opening a scene or creating one starts it again.
	{
		const std::size_t steps = w.m_history->undoCount();
		expect(!w.openSceneDirectory(QDir(outFolder).filePath("no_such_scene")), "a scene that is not there opened");
		expect(w.m_history->undoCount() == steps && w.m_undoAction->isEnabled() && w.m_history->isAtSavedPoint(),
			"a failed open changed the history");
		if (!w.openSceneDirectory(copyPath)) {
			failures << QStringLiteral("undo/redo: the saved copy did not open");
			return finish();
		}
		expect(!w.m_history->canUndo() && !w.m_history->canRedo() && w.m_history->isAtSavedPoint() && !w.m_document->isDirty()
				&& !w.m_undoAction->isEnabled() && !w.m_redoAction->isEnabled(),
			"opening a scene kept the history of the one before");
		answerMessageBox(&w, QMessageBox::Discard);
		w.m_session->newScene();
		process();
		expect(w.m_document->isDirty() && !w.m_history->canUndo() && !w.m_history->canRedo() && !w.m_history->isAtSavedPoint()
				&& !w.m_undoAction->isEnabled() && !w.m_redoAction->isEnabled(),
			"a new scene kept the history of the one before or started clean");
		const std::string newSceneName = w.m_document->mutableModel().name;
		w.m_caseNameEdit->setText("E2E new case");
		w.commitPendingCaseSettings();
		process();
		expect(w.m_history->undoCount() == 1 && w.m_undoAction->isEnabled(), "an edit of a new scene is not one step");
		// A value that is still being typed is committed as an edit first, and Undo then reverts that edit.
		w.m_caseNameEdit->setText("typed but not committed");
		w.m_undoAction->trigger();
		process();
		expect(w.m_document->mutableModel().name == "E2E new case" && w.m_caseNameEdit->text() == "E2E new case" && w.m_history->undoCount() == 1
				&& w.m_history->redoCount() == 1,
			"Undo did not revert the value that was still being typed");
		w.m_redoAction->trigger();
		process();
		expect(w.m_document->mutableModel().name == "typed but not committed", "Redo did not bring back the value that was typed");
		w.m_undoAction->trigger();
		w.m_undoAction->trigger();
		process();
		expect(w.m_document->isDirty() && w.m_document->mutableModel().name == newSceneName && !w.m_undoAction->isEnabled() && w.m_redoAction->isEnabled(),
			"Undo in a new scene did not return to its first state or marked it clean");
	}
	return finish();
}

// Autosave and recovery on a copy of the scene at scenePath, which is written below outFolder. A scripted run keeps no copies
// until a folder is named for them. With one, an edit leads to a copy of the scene in that folder after the interval, and
// Save, Undo to the saved state, a new scene and another scene take the copy away, while a failed open and Cancel keep it. A
// closed window leaves nothing. A window that ends without closing leaves a copy that the next start offers, and a recovered
// scene is an unsaved scene without a file of its own. Returns what failed.
QStringList MainWindowTestAccess::checkRecovery(MainWindow& w, const QString& scenePath, const QString& outFolder) {
	QStringList failures;
	const auto expect = [&failures](bool condition, const QString& message) {
		if (!condition)
			failures << QStringLiteral("recovery: ") + message;
		return condition;
	};
	const auto process = []() { QApplication::processEvents(); };
	const QString previousScenePath = w.m_session->m_sceneDir;
	const auto previousDialogShown = mainWindowTestHooks().dialogShown;
	const QString root = QDir(outFolder).filePath("recovery_root");
	const QString bundle = QFileInfo(QDir(outFolder).filePath("recovery_source.egscene")).absoluteFilePath();
	const QString savedBundle = QDir(outFolder).filePath("recovery_saved.egscene");
	const QString blocker = QDir(outFolder).filePath("recovery_blocker");
	const auto finish = [&]() {
		if (w.m_recovery)
			w.m_recovery->shutdown();
		qunsetenv("QEGTRAIN_RECOVERY_DIR");
		qunsetenv("QEGTRAIN_RECOVERY_INTERVAL_MS");
		mainWindowTestHooks().dialogShown = previousDialogShown;
		w.startRecovery();
		expect(!w.m_recovery, "the window keeps copies after the folder is no longer named");
		QDir(root).removeRecursively();
		if (!previousScenePath.isEmpty() && !w.openSceneDirectory(previousScenePath))
			failures << QStringLiteral("recovery: the scene before the checks did not reopen");
		return failures;
	};

	const auto waitFor = [](const std::function<bool()>& done) {
		QElapsedTimer waited;
		waited.start();
		while (!done() && waited.elapsed() < 15000) {
			QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
			QThread::msleep(5);
		}
		return done();
	};
	// A copy is written or removed once the controller has nothing left to do.
	const auto settle = [&]() { return expect(waitFor([&w] { return w.m_recovery->isIdle(); }), "the autosave did not finish"); };
	const auto fileBytes = [](const QString& path) {
		QFile file(path);
		return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray("unreadable");
	};
	QStringList leftFolders;
	const auto instanceFolders = [&root]() { return QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot); };
	const auto recordOf = [&root](const QString& folder) {
		QFile file(QDir(QDir(root).filePath(folder)).filePath("recovery.json"));
		return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
	};
	// The record of the window's own folder, empty when it holds none.
	const auto ownRecord = [&]() {
		QStringList own = instanceFolders();
		for (const QString& left : leftFolders)
			own.removeAll(left);
		return own.size() == 1 ? recordOf(own.first()) : QJsonObject();
	};
	const auto editCase = [&w](const char* name) {
		w.m_caseNameEdit->setText(name);
		w.commitPendingCaseSettings();
	};
	// Leaves the copy of a scene named name, as a window that ended without closing does.
	const auto leaveCopy = [&](const QString& name) {
		RecoveryStore crashed(root);
		SceneModel scene = w.m_document->mutableModel();
		scene.name = name.toStdString();
		RecoverySource source;
		source.kind = RecoverySourceKind::Bundle;
		source.path = bundle;
		source.displayName = name;
		source.sizeBytes = QFileInfo(bundle).size();
		source.modified = QFileInfo(bundle).lastModified().toUTC();
		if (!crashed.open().ok || !crashed.write(scene, source).ok)
			return QString();
		return crashed.instanceId();
	};
	// Clicks the buttons of the recovery prompt that opens next, in this order.
	const auto answerPrompt = [](const QStringList& buttons) {
		mainWindowTestHooks().dialogShown = [buttons](QDialog& dialog, const char* kind) {
			if (std::strcmp(kind, "recoveryPrompt") != 0)
				return;
			QTimer::singleShot(0, &dialog, [&dialog, buttons]() {
				for (const QString& name : buttons) {
					if (auto* button = dialog.findChild<QPushButton*>(name))
						button->click();
				}
			});
		};
	};

	QDir(root).removeRecursively();
	QFile::remove(bundle);
	expect(!w.m_recovery, "the window keeps copies in a scripted run that names no folder");
	qputenv("QEGTRAIN_RECOVERY_DIR", root.toUtf8());
	qputenv("QEGTRAIN_RECOVERY_INTERVAL_MS", "50");
	w.startRecovery();
	if (!expect(w.m_recovery && w.m_recovery->intervalMs() == 50, "the window keeps no copies in the folder that is named for them"))
		return finish();

	// An opened bundle is the source of its copies, and a scene without edits has none.
	if (!w.openSceneDirectory(scenePath) || !saveSceneBundle(w.m_document->mutableModel(), bundle.toStdString()).success() || !w.openSceneDirectory(bundle)) {
		failures << QStringLiteral("recovery: the bundle for the checks did not open");
		return finish();
	}
	QByteArray originalBytes = fileBytes(bundle);
	QDateTime originalTime = QFileInfo(bundle).lastModified();
	expect(!w.m_document->isDirty() && w.m_recoverySource.kind == RecoverySourceKind::Bundle && w.m_recoverySource.path == bundle
			&& w.m_recoverySource.sizeBytes == QFileInfo(bundle).size()
			&& w.m_recoverySource.modified.toMSecsSinceEpoch() == originalTime.toMSecsSinceEpoch(),
		"an opened bundle is not the source of its copies");
	expect(w.m_recovery->isIdle() && instanceFolders().isEmpty(), "a scene without edits has a copy");

	// Three edits lead to one copy, and the file that the scene came from stays as it was.
	editCase("E2E recovery one");
	w.m_caseBaseTimeEdit->setText("09:15:30");
	w.commitPendingCaseSettings();
	editCase("E2E recovery three");
	expect(w.m_document->isDirty() && !w.m_recovery->isIdle(), "an edit did not start the autosave");
	if (!settle())
		return finish();
	{
		const QStringList folders = instanceFolders();
		const QJsonObject record = folders.size() == 1 ? recordOf(folders.first()) : QJsonObject();
		const QJsonObject source = record.value("source").toObject();
		expect(record.value("display_name").toString() == "E2E recovery three" && source.value("kind").toString() == "bundle"
				&& source.value("path").toString() == bundle,
			"the copy does not name the scene and its source");
		const QString copyPath = folders.size() == 1
			? QDir(QDir(root).filePath(folders.first())).filePath(record.value("bundle_file").toString())
			: QString();
		const SceneLoadResult copy = loadScenePath(copyPath.toStdString());
		expect(!copyPath.isEmpty() && !hasErrors(copy.diagnostics) && copy.scene.name == "E2E recovery three"
				&& copy.scene.baseTime == "09:15:30",
			"the copy does not hold the edits");
	}
	expect(RecoveryStore(root).scan().candidates.isEmpty(), "the copy of a running window is offered");
	expect(fileBytes(bundle) == originalBytes && QFileInfo(bundle).lastModified() == originalTime, "the autosave changed the scene file");

	// Undo to the saved state takes the copy away, and Redo brings the edits and their copy back.
	while (w.m_history->canUndo())
		w.undoScene();
	process();
	if (!settle())
		return finish();
	expect(!w.m_document->isDirty() && ownRecord().isEmpty(), "Undo to the saved state left a copy");
	while (w.m_history->canRedo())
		w.redoScene();
	process();
	if (!settle())
		return finish();
	expect(w.m_document->isDirty() && ownRecord().value("display_name").toString() == "E2E recovery three", "Redo from the saved state left no copy");

	// Save takes the copy away and reads the file that it wrote as the new source.
	if (!expect(w.m_session->saveSceneToCurrentDir(), "the bundle was not saved"))
		return finish();
	if (!settle())
		return finish();
	originalBytes = fileBytes(bundle);
	originalTime = QFileInfo(bundle).lastModified();
	expect(!w.m_document->isDirty() && ownRecord().isEmpty() && w.m_recoverySource.sizeBytes == QFileInfo(bundle).size()
			&& w.m_recoverySource.modified.toMSecsSinceEpoch() == originalTime.toMSecsSinceEpoch(),
		"Save left a copy or did not read the saved file");

	// Opening the same scene again drops its edits and their copy.
	editCase("E2E recovery four");
	if (!settle())
		return finish();
	expect(!ownRecord().isEmpty(), "an edit after Save has no copy");
	if (!expect(w.openSceneDirectory(bundle), "the bundle did not open again"))
		return finish();
	if (!settle())
		return finish();
	expect(!w.m_document->isDirty() && ownRecord().isEmpty(), "opening the scene again left the copy of the edits that it dropped");

	// A new scene is recoverable. Discard followed by an open that fails, and Cancel, keep its copy. An open that succeeds takes it.
	w.m_session->newScene();
	process();
	if (!settle())
		return finish();
	expect(w.m_document->isDirty() && w.m_session->m_sceneDir.isEmpty()
			&& ownRecord().value("source").toObject().value("kind").toString() == "untitled",
		"a new scene has no copy");
	answerMessageBox(&w, QMessageBox::Discard);
	expect(!w.m_session->requestOpenScene(QDir(outFolder).filePath("no_such_recovery_scene")), "a scene that is not there opened");
	if (!settle())
		return finish();
	expect(w.m_document->isDirty() && !ownRecord().isEmpty(), "Discard followed by an open that failed removed the copy");
	answerMessageBox(&w, QMessageBox::Cancel);
	expect(!w.m_session->requestOpenScene(bundle) && w.m_document->isDirty() && !ownRecord().isEmpty(), "Cancel removed the copy");
	answerMessageBox(&w, QMessageBox::Discard);
	expect(w.m_session->requestOpenScene(bundle), "the bundle did not open after Discard");
	if (!settle())
		return finish();
	expect(!w.m_document->isDirty() && ownRecord().isEmpty(), "opening a scene after Discard left the copy of the one that it dropped");

	// Closing the window keeps the copy when the user cancels, and leaves nothing otherwise.
	editCase("E2E recovery five");
	if (!settle())
		return finish();
	{
		QCloseEvent cancelled;
		answerMessageBox(&w, QMessageBox::Cancel);
		w.closeEvent(&cancelled);
		expect(!cancelled.isAccepted() && !ownRecord().isEmpty(), "Cancel at close removed the copy");
		QCloseEvent closing;
		answerMessageBox(&w, QMessageBox::Discard);
		w.closeEvent(&closing);
		expect(closing.isAccepted() && instanceFolders().isEmpty(), "closing the window left a copy");
	}
	// The controller of a closed window takes no more copies.
	w.startRecovery();
	if (!expect(w.m_recovery != nullptr, "the window keeps no copies after the folder was cleared"))
		return finish();

	// Decide Later keeps a copy that a window left, and Discard removes it.
	if (!w.openSceneDirectory(bundle)) {
		failures << QStringLiteral("recovery: the bundle did not open before the prompt");
		return finish();
	}
	const QStringList recentBefore = QSettings().value("recentScenes").toStringList();
	const QString laterId = leaveCopy("E2E left copy");
	leftFolders << laterId;
	answerPrompt({"recoveryLaterButton"});
	expect(!laterId.isEmpty() && !w.offerRecovery(), "Decide Later opened a copy");
	expect(QDir(root).exists(laterId) && w.m_session->m_sceneDir == bundle && !w.m_document->isDirty(), "Decide Later changed the scene or removed the copy");
	answerPrompt({"recoveryDiscardButton", "recoveryDiscardButton"});
	expect(!w.offerRecovery() && !QDir(root).exists(laterId) && w.m_session->m_sceneDir == bundle && !w.m_document->isDirty(),
		"Discard did not remove the copy or changed the scene");

	// Recover opens the copy as an unsaved scene without a file, and the original stays as it was until Save As.
	const QString recoveredId = leaveCopy("E2E recovered case");
	leftFolders << recoveredId;
	answerPrompt({"recoveryRecoverButton"});
	const bool offered = w.offerRecovery();
	if (!settle())
		return finish();
	expect(offered && w.m_session->m_sceneDir.isEmpty() && !w.m_session->m_sceneIsBundle && w.m_document->isDirty() && w.m_document->mutableModel().name == "E2E recovered case",
		"the recovered copy is not an unsaved scene without a file");
	expect(!w.m_history->isAtSavedPoint() && !w.m_history->canUndo(), "the recovered copy has a saved state or a history");
	expect(w.m_recoveredSourceHint == bundle && QFileInfo(w.m_session->saveSceneAsStartPath()).fileName() == QFileInfo(bundle).fileName(),
		"Save As does not start at the file that the copy came from");
	expect(QSettings().value("recentScenes").toStringList() == recentBefore, "the recovered copy is a recent scene");
	expect(w.statusBar()->currentMessage().startsWith("Recovered"), "the window does not say that it opened a recovered copy");
	expect(fileBytes(bundle) == originalBytes && QFileInfo(bundle).lastModified() == originalTime, "recovering changed the original");
	leftFolders.removeAll(recoveredId);
	expect(!QDir(root).exists(recoveredId) && ownRecord().value("display_name").toString() == "E2E recovered case"
			&& ownRecord().value("source").toObject().value("path").toString() == bundle,
		"the window did not take over the recovered copy");
	editCase("E2E recovered edit");
	if (!settle())
		return finish();
	expect(ownRecord().value("display_name").toString() == "E2E recovered edit"
			&& ownRecord().value("source").toObject().value("path").toString() == bundle,
		"an edit of a recovered scene has no copy that names the original");
	// Save As, from the step after the file dialog: the recovered scene becomes a scene with a file, and its copy goes.
	{
		const SceneSaveResult saved = saveSceneBundle(w.m_document->mutableModel(), savedBundle.toStdString());
		w.m_session->m_sceneDir = QFileInfo(savedBundle).absoluteFilePath();
		w.m_session->m_sceneIsBundle = true;
		w.m_session->m_sceneBundleVersion = kCurrentSceneBundleVersion;
		expect(w.m_session->finishSceneSave(saved, telemetry::OperationObservation(), telemetry::ExportKind::SceneBundle), "Save As failed");
		if (!settle())
			return finish();
		expect(!w.m_document->isDirty() && ownRecord().isEmpty() && w.m_recoveredSourceHint.isEmpty()
				&& w.m_recoverySource.path == w.m_session->m_sceneDir,
			"Save As left the copy or the hint of a recovered scene");
		expect(fileBytes(bundle) == originalBytes && QFileInfo(bundle).lastModified() == originalTime, "Save As changed the original");
	}

	// A folder that cannot be made does not stop the editing, and the window says so once.
	w.m_recovery->shutdown();
	QFile blockerFile(blocker);
	if (!blockerFile.open(QIODevice::WriteOnly)) {
		failures << QStringLiteral("recovery: the file for the blocked folder was not written");
		return finish();
	}
	blockerFile.close();
	qputenv("QEGTRAIN_RECOVERY_DIR", QDir(blocker).filePath("recovery").toUtf8());
	w.startRecovery();
	if (!w.m_recovery || !w.openSceneDirectory(bundle)) {
		failures << QStringLiteral("recovery: the window did not start with a folder that cannot be made");
		return finish();
	}
	int unavailable = 0;
	const QMetaObject::Connection counter = QObject::connect(w.statusBar(), &QStatusBar::messageChanged,
		[&unavailable](const QString& message) { unavailable += message.startsWith("Autosave unavailable") ? 1 : 0; });
	const std::size_t steps = w.m_history->undoCount();
	editCase("E2E unwritable one");
	settle();
	editCase("E2E unwritable two");
	settle();
	QObject::disconnect(counter);
	expect(unavailable == 1, "the window reported the failed autosave " + QString::number(unavailable) + " times");
	expect(w.m_document->isDirty() && w.m_history->undoCount() == steps + 2 && w.m_document->mutableModel().name == "E2E unwritable two",
		"a failed autosave stopped the editing");

	// A copy that is written again ends the message.
	w.m_recovery->shutdown();
	qputenv("QEGTRAIN_RECOVERY_DIR", QDir(outFolder).filePath("recovery-writable").toUtf8());
	w.startRecovery();
	editCase("E2E writable again");
	settle();
	expect(!w.m_recoveryFailureShown && !w.statusBar()->currentMessage().startsWith("Autosave unavailable"),
		"the message about the failed autosave stayed after a copy was written");
	return finish();
}
