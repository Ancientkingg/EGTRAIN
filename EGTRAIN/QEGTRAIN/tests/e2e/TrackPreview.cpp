#include "tests/e2e/MainWindowTestAccess.h"

#include "graphics/SignalItemRoles.h"
#include "ui_MainWindow.h"

#include "editors/ValidationPanel.h"
#include "graphics/NetworkScene.h"
#include "graphics/NetworkView.h"
#include "graphics/items/HighlightEffect.h"
#include "graphics/items/SelectionCueItem.h"
#include "graphics/items/SignalItem.h"
#include "scene/SceneDiagnostic.h"
#include "scene/SceneModel.h"
#include "scene/SceneWriter.h"
#include "scene/TrackPreview.h"
#include "simulation/SimulationWorker.h"
#include "widgets/ElidedLabel.h"
#include "widgets/InfoDockWidget.h"
#include "widgets/NetworkLegendWidget.h"
#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QGraphicsLineItem>
#include <QGraphicsSceneMouseEvent>
#include <QIODevice>
#include <QLabel>
#include <QLineF>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTemporaryDir>
#include <QTransform>
#include <QVector>
#include <QWidget>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {

// Copies a folder and everything in it. A file in the way at the target is replaced.
bool copyDirectoryRecursively(const QString& sourcePath, const QString& targetPath) {
	QDir sourceDir(sourcePath);
	if (!sourceDir.exists())
		return false;

	QDir targetDir(targetPath);
	if (!targetDir.exists() && !QDir().mkpath(targetPath))
		return false;

	const QFileInfoList entries = sourceDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
	for (const QFileInfo& entry : entries) {
		const QString sourceFilePath = entry.absoluteFilePath();
		const QString targetFilePath = targetDir.filePath(entry.fileName());
		if (entry.isDir()) {
			if (!copyDirectoryRecursively(sourceFilePath, targetFilePath))
				return false;
		} else {
			QFile::remove(targetFilePath);
			if (!QFile::copy(sourceFilePath, targetFilePath))
				return false;
		}
	}
	return true;
}

} // namespace

// Opens the scene named by QEGTRAIN_E2E_SCENE, which has tracks and stations but no trains or services, and checks
// that the track preview shows it: its geometry, signals, viewport and the empty Follow selector. It checks that
// Run stays off while the scene has errors and leaves the preview in place, that a partial scene saves and
// reloads unchanged, that a malformed, incomplete or newer scene is refused without replacing the open one, that
// an authored signal is inspectable, and that a click selects an item with a cue. It prints E2E_TRACK_PREVIEW_OK
// and exits with 0, or prints the failures and exits with 2.
void MainWindowTestAccess::runTrackPreview(MainWindow& w) {
	if (scenarioStarted)
		return;
	scenarioStarted = true;

	bool ok = true;
	QStringList failures;
	auto fail = [&](const QString& facet, const QString& message) {
		ok = false;
		failures << QString("%1: %2").arg(facet, message);
	};
	auto marker = [](const char* name) {
		std::fprintf(stdout, "%s\n", name);
		std::fflush(stdout);
	};
	auto hasDiagnosticCode = [](const std::vector<SceneDiagnostic>& diagnostics, const char* code) {
		for (const auto& diagnostic : diagnostics) {
			if (diagnostic.code == code)
				return true;
		}
		return false;
	};
	auto sameSceneModel = [](const SceneModel& left, const SceneModel& right) {
		const auto& leftIncidents = defaultScenarioIncidents(left);
		const auto& rightIncidents = defaultScenarioIncidents(right);
		if (left.schemaVersion != right.schemaVersion || left.name != right.name
			|| left.description != right.description || left.baseTime != right.baseTime
			|| left.stations.size() != right.stations.size() || sceneSignals(left).size() != sceneSignals(right).size()
			|| left.routes.size() != right.routes.size() || left.trainUnits.size() != right.trainUnits.size()
			|| left.compositions.size() != right.compositions.size() || left.services.size() != right.services.size()
			|| leftIncidents.size() != rightIncidents.size())
			return false;
		if (!std::equal(left.stations.begin(), left.stations.end(), right.stations.begin(), [](const auto& a, const auto& b) {
				return a.id == b.id && a.name == b.name && a.platforms.size() == b.platforms.size()
					&& std::equal(a.platforms.begin(), a.platforms.end(), b.platforms.begin(),
						[](const auto& x, const auto& y) { return x.id == y.id; });
			}))
			return false;
		if (!std::equal(sceneSignals(left).begin(), sceneSignals(left).end(), sceneSignals(right).begin(),
				[](const auto& a, const auto& b) {
					return a.id == b.id && a.protectedSection == b.protectedSection;
				}))
			return false;
		if (!std::equal(left.routes.begin(), left.routes.end(), right.routes.begin(), [](const auto& a, const auto& b) {
				return a.id == b.id && a.blocks == b.blocks;
			}))
			return false;
		if (!std::equal(left.trainUnits.begin(), left.trainUnits.end(), right.trainUnits.begin(), [](const auto& a, const auto& b) {
				return a.id == b.id && a.hasPhysical == b.hasPhysical && a.physical.mass_of_traction_unit_kg == b.physical.mass_of_traction_unit_kg
					&& a.physical.mass_of_a_wagon_kg == b.physical.mass_of_a_wagon_kg && a.physical.number_of_wagons == b.physical.number_of_wagons
					&& a.physical.max_speed_ms == b.physical.max_speed_ms && a.physical.max_deceleration_ms2 == b.physical.max_deceleration_ms2
					&& a.physical.frontal_area_m2 == b.physical.frontal_area_m2 && a.physical.resistance_coefficient == b.physical.resistance_coefficient
					&& a.physical.jerk_ms3 == b.physical.jerk_ms3 && a.physical.length_m == b.physical.length_m
					&& a.tractionCurve == b.tractionCurve && a.sourceDataFile == b.sourceDataFile
					&& a.sourceTractionFile == b.sourceTractionFile;
			}))
			return false;
		if (!std::equal(left.compositions.begin(), left.compositions.end(), right.compositions.begin(),
				[](const auto& a, const auto& b) { return a.id == b.id && a.units == b.units; }))
			return false;
		if (!std::equal(left.services.begin(), left.services.end(), right.services.begin(), [](const auto& a, const auto& b) {
				if (a.id != b.id || a.operatingCode != b.operatingCode
					|| a.composition != b.composition || a.route != b.route || a.through != b.through
					|| a.hasEntryTime != b.hasEntryTime || a.entryTimeSeconds != b.entryTimeSeconds
					|| a.hasRepeat != b.hasRepeat || a.headwaySeconds != b.headwaySeconds || a.stops.size() != b.stops.size())
					return false;
				return std::equal(a.stops.begin(), a.stops.end(), b.stops.begin(), [](const auto& x, const auto& y) {
					return x.stationId == y.stationId && x.platformId == y.platformId && x.hasPlannedArrival == y.hasPlannedArrival
						&& x.hasPlannedDeparture == y.hasPlannedDeparture && x.plannedArrivalSeconds == y.plannedArrivalSeconds
						&& x.plannedDepartureSeconds == y.plannedDepartureSeconds && x.dwellSeconds == y.dwellSeconds;
				});
			}))
			return false;
		return std::equal(leftIncidents.begin(), leftIncidents.end(), rightIncidents.begin(),
			[](const auto& a, const auto& b) {
				return a.id == b.id && a.type == b.type && a.target == b.target
					&& a.startSeconds == b.startSeconds && a.endSeconds == b.endSeconds;
			});
	};
	auto validationTableHasCode = [&](const char* code) {
		if (!w.m_validation->table())
			return false;
		for (int row = 0; row < w.m_validation->table()->rowCount(); ++row) {
			QTableWidgetItem* item = w.m_validation->table()->item(row, 1);
			if (item && item->text() == code)
				return true;
		}
		return false;
	};
	struct PreviewContentSnapshot {
		QList<QGraphicsItem*> items;
		QList<QRectF> itemBounds;
		QRectF bounds;
	};
	auto snapshotPreviewContent = [&]() {
		PreviewContentSnapshot snapshot;
		if (!w.scene)
			return snapshot;
		bool hasBounds = false;
		for (auto* item : w.scene->items()) {
			if (!item)
				continue;
			snapshot.items.push_back(item);
			const QRectF itemBounds = item->sceneBoundingRect();
			snapshot.itemBounds.push_back(itemBounds);
			if (itemBounds.isEmpty())
				continue;
			snapshot.bounds = hasBounds ? snapshot.bounds.united(itemBounds) : itemBounds;
			hasBounds = true;
		}
		return snapshot;
	};
	auto samePreviewContent = [](const PreviewContentSnapshot& left, const PreviewContentSnapshot& right) {
		if (left.items.size() != right.items.size() || left.itemBounds.size() != right.itemBounds.size())
			return false;
		for (int index = 0; index < left.items.size(); ++index) {
			if (left.items.at(index) != right.items.at(index)
				|| left.itemBounds.at(index) != right.itemBounds.at(index))
				return false;
		}
		if (left.bounds != right.bounds)
			return false;
		return true;
	};

	const QString scenePath = qEnvironmentVariable("QEGTRAIN_E2E_SCENE");
	const bool opened = !scenePath.isEmpty() && w.openSceneDirectory(scenePath);
	if (!opened || !w.m_document->isLoaded()) {
		fail("open", "incomplete scene did not open");
	} else {
		const PreviewContentSnapshot preview = snapshotPreviewContent();
		const int itemCount = preview.items.size();
		const QRectF bounds = preview.bounds;
		const QRectF visible = w.networkView->mapToScene(w.networkView->viewport()->rect()).boundingRect();
		const bool diagnosticsOk = hasDiagnosticCode(w.m_session->m_sceneDiagnostics, "scene.trains.none")
			&& hasDiagnosticCode(w.m_session->m_sceneDiagnostics, "scene.services.none")
			&& validationTableHasCode("scene.trains.none") && validationTableHasCode("scene.services.none");
		if (!diagnosticsOk)
			fail("open", "semantic diagnostics are missing from the validation panel");
		if (itemCount < 2)
			fail("open", "too few preview items");
		if (bounds.width() < 10.0)
			fail("open", "preview geometry did not spread horizontally");
		if (!visible.intersects(bounds))
			fail("open", "preview is outside the viewport");
		if (!w.m_followTrainCombo || w.m_followTrainCombo->currentText() != "No trains to follow")
			fail("follow", "empty train selector does not explain that no trains are available");
		else if (!w.m_document->mutableModel().services.empty() || w.m_followAction->isEnabled()
			|| w.m_followStatusLabel->text() != "This case has no trains to follow."
			|| w.m_followAction->toolTip() != w.m_followStatusLabel->text() || w.m_followTrainCombo->toolTip() != w.m_followStatusLabel->text()
			|| w.m_followTrainCombo->accessibleDescription() != w.m_followStatusLabel->text())
			fail("follow", "a case without services does not disable Follow with the reason");
		else
			marker("E2E_FOLLOW_NO_SERVICES_OK");
		if (diagnosticsOk && itemCount >= 2 && bounds.width() >= 10.0 && visible.intersects(bounds))
			marker("E2E_TRACK_PREVIEW_OPEN_OK");
		const bool previewHasSignalGlyph = std::any_of(w.m_presenter->m_signalDecorations.cbegin(),
			w.m_presenter->m_signalDecorations.cend(), [](QGraphicsItem* item) {
				return item && qgraphicsitem_cast<SignalItem*>(item) != nullptr;
			});
		const QVector<NetworkLegendEntry> legendEntries = w.m_networkLegendWidget
			? w.m_networkLegendWidget->entries()
			: QVector<NetworkLegendEntry>();
		const bool legendHasSignals = std::any_of(legendEntries.cbegin(),
			legendEntries.cend(), [](const NetworkLegendEntry& entry) {
				return entry.kind == NetworkLegendEntryKind::Signal;
			});
		if (previewHasSignalGlyph != legendHasSignals)
			fail("signals", "preview glyphs and map-key signal entries are out of sync");
		if (previewHasSignalGlyph && w.networkView) {
			w.updateViewportOverlays();
			const QTransform transform = w.networkView->viewportTransform();
			for (QGraphicsItem* item : w.m_presenter->m_signalDecorations) {
				auto* signal = item ? qgraphicsitem_cast<SignalItem*>(item) : nullptr;
				if (!signal || !signal->isVisible() || !item->data(kSignalAnchorRole).isValid())
					continue;
				const QPointF anchor = transform.map(item->data(kSignalAnchorRole).toPointF());
				const QPointF marker = transform.map(item->scenePos());
				if (QLineF(anchor, marker).length() > 24.0 || signal->toolTip().isEmpty())
					fail("signals", "preview signal lost its bounded boundary location or source identity");
			}
		}
		if (previewHasSignalGlyph && w.m_signalLayerCheck) {
			const bool layerWasChecked = w.m_signalLayerCheck->isChecked();
			w.m_signalLayerCheck->setChecked(false);
			QApplication::processEvents();
			const bool hidden = std::none_of(w.m_presenter->m_signalDecorations.cbegin(),
				w.m_presenter->m_signalDecorations.cend(), [](QGraphicsItem* item) {
					return item && qgraphicsitem_cast<SignalItem*>(item) != nullptr && item->isVisible();
				});
			w.m_signalLayerCheck->setChecked(layerWasChecked);
			QApplication::processEvents();
			if (!hidden)
				fail("signals", "Signals layer toggle did not hide preview glyphs");
		}
		if (!w.networkView) {
			fail("viewport", "preview viewport is missing");
		} else {
			const QRectF previewFitBounds = w.networkView->topologyBounds();
			const qreal previewBaseline = w.networkView->fittedScale();
			const bool zoomApplied = w.networkView->zoomBy(1.15);
			if (!zoomApplied || qAbs(w.networkView->zoomRatio() - 1.15) > 1e-5
				|| qAbs(qAbs(w.networkView->transform().m11()) - previewBaseline * 1.15) > 1e-5)
				fail("viewport", "preview zoom is not relative to the fitted baseline");
			w.fitView();
			if (w.networkView->zoomLabel() != QStringLiteral("Fit")
				|| w.networkView->topologyBounds() != previewFitBounds)
				fail("viewport", "Fit did not restore the preview baseline");
		}
	}

	bool runFacetOk = w.m_runSceneAction && !w.m_runSceneAction->isEnabled()
		&& w.ui->actionSimulationStart && !w.ui->actionSimulationStart->isEnabled();
	if (!runFacetOk)
		fail("run gating", "Run action is enabled while semantic errors are present");
	if (w.m_document->isLoaded()) {
		const SceneModel incomplete = w.m_document->mutableModel();
		SceneModel fixed = incomplete;
		fixed.baseTime = "08:00:00";
		fixed.settings.hasDuration = true;
		fixed.settings.durationSeconds = 3600.0;
		fixed.tracks = {{"e2e_track"}};
		fixed.nodes = {
			{"e2e_node_1", "e2e_track", 0.0, 0.0},
			{"e2e_node_2", "e2e_track", 1.0, 0.0},
		};
		fixed.arcs = {{"e2e_arc", "e2e_track", "e2e_node_1", "e2e_node_2", 0.0, 0.0, 1.0}};
		fixed.blocks = {{"e2e_block", "e2e_track", 1.0}};
		fixed.connections.clear();
		fixed.stations.clear();
		SceneStation station;
		station.id = "e2e_station";
		station.name = "E2E station";
		station.platforms.push_back({"e2e_platform", {"e2e_node_1"}});
		fixed.stations.push_back(station);
		fixed.trainUnits.clear();
		SceneTrainUnit unit;
		unit.id = "e2e_unit";
		unit.hasPhysical = true;
		unit.tractionCurve.push_back({{0.0, 1.0, 1.0, 0.0, 0.0}});
		fixed.trainUnits.push_back(unit);
		fixed.compositions.clear();
		SceneComposition composition;
		composition.id = "e2e_composition";
		composition.units.push_back(unit.id);
		fixed.compositions.push_back(composition);
		fixed.routes.clear();
		SceneRoute route;
		route.id = "e2e_route";
		route.blocks.push_back("e2e_block");
		fixed.routes.push_back(route);
		fixed.services.clear();
		SceneService service;
		service.id = "e2e_service";
		service.composition = composition.id;
		service.route = route.id;
		service.through = true;
		fixed.services.push_back(service);
		defaultScenarioIncidents(fixed).clear();
		w.m_document->mutableModel() = fixed;
		w.refreshValidationPanel();
		if (hasErrors(w.m_session->m_sceneDiagnostics) || !w.m_runSceneAction || !w.m_runSceneAction->isEnabled()
			|| !w.ui->actionSimulationStart || !w.ui->actionSimulationStart->isEnabled())
			runFacetOk = false;
		w.m_document->mutableModel() = incomplete;
		w.refreshValidationPanel();
		if (!hasErrors(w.m_session->m_sceneDiagnostics) || !w.m_runSceneAction || w.m_runSceneAction->isEnabled()
			|| !w.ui->actionSimulationStart || w.ui->actionSimulationStart->isEnabled())
			runFacetOk = false;
	}
	const PreviewContentSnapshot previewBeforeRun = snapshotPreviewContent();
	w.m_runController->runScene();
	const bool workerStarted = w.m_runController->m_worker != nullptr;
	QApplication::processEvents();
	if (workerStarted || w.m_runController->m_worker) {
		runFacetOk = false;
		fail("run gating", "invalid scene started simulation");
	}
	if (!samePreviewContent(previewBeforeRun, snapshotPreviewContent())) {
		runFacetOk = false;
		fail("run gating", "blocked run removed the preview");
	}
	if (runFacetOk)
		marker("E2E_TRACK_PREVIEW_RUN_GATED_OK");

	QTemporaryDir e2eRoot;
	QString persistedScenePath;
	SceneModel expectedPartial;
	bool saveReloadOk = true;
	if (!e2eRoot.isValid() || !w.m_document->isLoaded()) {
		saveReloadOk = false;
		fail("save/reload", "temporary scene directory unavailable");
	} else {
		expectedPartial = w.m_document->mutableModel();
		persistedScenePath = QDir(e2eRoot.path()).filePath("partial");
		const SceneSaveResult saveResult = ::saveScene(expectedPartial, persistedScenePath.toStdString());
		if (!saveResult.success()) {
			saveReloadOk = false;
			fail("save/reload", "partial scene save failed");
		} else if (!w.openSceneDirectory(persistedScenePath)) {
			saveReloadOk = false;
			fail("save/reload", "saved partial scene did not reload");
		} else if (!sameSceneModel(expectedPartial, w.m_document->mutableModel())) {
			saveReloadOk = false;
			fail("save/reload", "partial model changed after reload");
		} else if (!hasDiagnosticCode(w.m_session->m_sceneDiagnostics, "scene.trains.none")
			|| !hasDiagnosticCode(w.m_session->m_sceneDiagnostics, "scene.services.none")
			|| !w.m_runSceneAction || w.m_runSceneAction->isEnabled()
			|| !w.ui->actionSimulationStart || w.ui->actionSimulationStart->isEnabled()) {
			saveReloadOk = false;
			fail("save/reload", "reloaded diagnostics or Run gating changed");
		}
	}
	if (saveReloadOk)
		marker("E2E_TRACK_PREVIEW_SAVE_RELOAD_OK");

	bool structuralOk = saveReloadOk;
	if (structuralOk) {
		const SceneModel expectedCurrent = w.m_document->mutableModel();
		const QString expectedDir = QDir(w.m_session->m_sceneDir).absolutePath();
		const PreviewContentSnapshot expectedContent = snapshotPreviewContent();
		auto rejectedWithoutReplacement = [&](const QString& candidate, const char* label) {
			if (w.openSceneDirectory(candidate)) {
				fail("structural rejection", QString("%1 scene unexpectedly opened").arg(label));
				return false;
			}
			const bool preserved = QDir(w.m_session->m_sceneDir).absolutePath() == expectedDir
				&& sameSceneModel(w.m_document->mutableModel(), expectedCurrent)
				&& samePreviewContent(expectedContent, snapshotPreviewContent());
			if (!preserved)
				fail("structural rejection", QString("%1 failure replaced current state").arg(label));
			return preserved;
		};
		auto copyCandidate = [&](const QString& name) {
			const QString candidate = QDir(e2eRoot.path()).filePath(name);
			if (!copyDirectoryRecursively(persistedScenePath, candidate)) {
				fail("structural rejection", QString("could not create %1 candidate").arg(name));
				return QString();
			}
			return candidate;
		};

		QString malformed = copyCandidate("malformed");
		if (malformed.isEmpty()) {
			structuralOk = false;
		} else {
			QFile file(QDir(malformed).filePath("scene.json"));
			const bool wroteMalformed = file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write("{") == 1;
			file.close();
			if (!wroteMalformed) {
				structuralOk = false;
				fail("structural rejection", "could not write malformed candidate");
			} else if (!rejectedWithoutReplacement(malformed, "malformed")) {
				structuralOk = false;
			}
		}

		QString missing = copyCandidate("missing");
		if (missing.isEmpty()) {
			structuralOk = false;
		} else {
			if (!QFile::remove(QDir(missing).filePath("services.json"))) {
				structuralOk = false;
				fail("structural rejection", "could not remove required file from candidate");
			} else if (!rejectedWithoutReplacement(missing, "missing-file")) {
				structuralOk = false;
			}
		}

		QString unsupported = copyCandidate("unsupported");
		if (unsupported.isEmpty()) {
			structuralOk = false;
		} else {
			QFile file(QDir(unsupported).filePath("scene.json"));
			if (!file.open(QIODevice::ReadOnly)) {
				structuralOk = false;
				fail("structural rejection", "could not read schema candidate");
			} else {
				QByteArray contents = file.readAll();
				file.close();
				const QByteArray currentSchemaMarker = "\"schema_version\": "
					+ QByteArray::number(kCurrentSceneSchemaVersion);
				const QByteArray newerSchemaMarker = "\"schema_version\": "
					+ QByteArray::number(kCurrentSceneSchemaVersion + 1);
				if (!contents.contains(currentSchemaMarker)) {
					structuralOk = false;
					fail("structural rejection", "saved scene schema marker not found");
				} else {
					contents.replace(currentSchemaMarker, newerSchemaMarker);
					const bool wroteUnsupported = file.open(QIODevice::WriteOnly | QIODevice::Truncate)
						&& file.write(contents) == contents.size();
					file.close();
					if (!wroteUnsupported) {
						structuralOk = false;
						fail("structural rejection", "could not write unsupported-schema candidate");
					} else if (!rejectedWithoutReplacement(unsupported, "unsupported-schema")) {
						structuralOk = false;
					}
				}
			}
		}
	}
	if (structuralOk)
		marker("E2E_TRACK_PREVIEW_STRUCTURAL_REJECTION_OK");

	// Exercise identity propagation with an authored signal on a real preview
	// boundary. Render only an in-memory copy; do not change the opened scene.
	if (w.m_document->isLoaded()) {
		const TrackPreviewResult source = loadTrackPreview(w.m_document->mutableModel());
		if (source.previewSignals.empty()) {
			fail("signals", "preview identity fixture has no boundary");
		} else {
			SceneModel identified = w.m_document->mutableModel();
			const std::string sectionId = source.previewSignals.front().sectionId;
			sceneSignals(identified).push_back({"E2E_SIGNAL_PREVIEW_360", sectionId});
			w.renderTrackPreview(identified);
			w.updateViewportOverlays();
			const bool inspectable = std::any_of(w.m_presenter->m_signalDecorations.cbegin(), w.m_presenter->m_signalDecorations.cend(),
				[&](QGraphicsItem* item) {
					auto* signal = item ? qgraphicsitem_cast<SignalItem*>(item) : nullptr;
					return signal && signal->isVisible()
						&& signal->inspectionIdentity().contains("E2E_SIGNAL_PREVIEW_360")
						&& signal->toolTip().contains("E2E_SIGNAL_PREVIEW_360")
						&& signal->toolTip().contains(QString::fromStdString(sectionId));
				});
			if (!inspectable)
				fail("signals", "authored preview signal ID and boundary are not inspectable");
			else
				marker("E2E_TRACK_PREVIEW_SIGNAL_IDENTITY_OK");
		}
	}

	// A click on a preview item selects it with a cue; a click on empty canvas removes the cue.
	if (w.m_document->isLoaded() && w.scene && w.networkView) {
		const auto cueCount = [&w]() {
			int count = 0;
			for (const QGraphicsItem* item : w.scene->items())
				if (item->type() == SelectionCueItem::Type)
					++count;
			return count;
		};
		const auto clickAt = [&w](const QPointF& scenePos) {
			QGraphicsSceneMouseEvent event(QEvent::GraphicsSceneMousePress);
			event.setButton(Qt::LeftButton);
			event.setButtons(Qt::LeftButton);
			event.setScenePos(scenePos);
			event.setWidget(w.networkView->viewport());
			w.scene->mousePressEvent(&event);
			QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);
			release.setButton(Qt::LeftButton);
			release.setScenePos(scenePos);
			release.setWidget(w.networkView->viewport());
			w.scene->mouseReleaseEvent(&release);
			QApplication::processEvents();
		};
		int clicked = 0;
		bool cuesOk = true;
		for (const char* kind : {"arc", "connection", "node", "station-node", "signal"}) {
			QGraphicsItem* target = nullptr;
			for (auto* item : w.scene->items())
				if (item->isVisible() && item->data(PreviewGraphics::Kind).toString() == kind)
					target = item;
			if (!target)
				continue;
			const auto* lineItem = dynamic_cast<const QGraphicsLineItem*>(target);
			clickAt(lineItem ? target->mapToScene(lineItem->line().pointAt(0.5)) : target->sceneBoundingRect().center());
			++clicked;
			SelectionCueItem* cue = w.m_presenter->effect ? w.m_presenter->effect->cue() : nullptr;
			if (!cue || !cue->isVisible() || cueCount() != 1 || !w.m_inspector->dock()->isVisible()) {
				cuesOk = false;
				fail("selection", QString("a click on a preview %1 gives no single cue").arg(kind));
			}
		}
		const QRectF bounds = w.networkView->topologyBounds();
		clickAt(bounds.bottomRight() + QPointF(bounds.width() + 1000.0, bounds.height() + 1000.0));
		if (cueCount() != 0 || w.m_presenter->effect) {
			cuesOk = false;
			fail("selection", "a click on empty preview canvas left a cue");
		}
		if (clicked < 2)
			fail("selection", "the preview has too few kinds of item to click");
		else if (cuesOk)
			marker("E2E_SELECTION_CUE_PREVIEW_OK");
	}

	if (w.networkView) {
		QString findFailure;
		if (checkFind(w, false, findFailure))
			marker("E2E_FIND_PREVIEW_OK");
		else
			fail("find", findFailure);
	}

	if (ok) {
		std::fprintf(stdout, "E2E_TRACK_PREVIEW_OK\n");
		std::fflush(stdout);
		QCoreApplication::exit(0);
		return;
	}

	std::fprintf(stderr, "E2E_TRACK_PREVIEW_FAIL: %s\n", failures.join(", ").toStdString().c_str());
	std::fflush(stderr);
	QCoreApplication::exit(2);
}
