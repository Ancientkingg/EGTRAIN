#include "tests/e2e/MainWindowTestAccess.h"

#include "app/DispatchController.h"
#include "app/GuiSimulationSnapshot.h"
#include "app/MainWindowSupport.h"
#include "editors/InfrastructurePanel.h"
#include "graphics/items/PlatformItem.h"
#include "graphics/NetworkScene.h"
#include "graphics/SignalItemRoles.h"
#include "ui_MainWindow.h"

#include "graphics/FollowCamera.h"
#include "graphics/NetworkView.h"
#include "graphics/VisualPolish.h"
#include "graphics/items/ConnectionItem.h"
#include "graphics/items/NodeItem.h"
#include "graphics/items/PassengerItem.h"
#include "graphics/items/SelectionCueItem.h"
#include "graphics/items/SignalItem.h"
#include "graphics/items/StationNodeItem.h"
#include "graphics/items/StationOverlayItem.h"
#include "graphics/items/TrackLineItem.h"
#include "graphics/items/TrainBadgeItem.h"
#include "graphics/items/TrainBodyItem.h"
#include "graphics/items/TrainItemGroup.h"
#include "scene/SceneModel.h"
#include "simulation/SimulationRun.h"
#include "simulation/SimulationWorker.h"
#include "util/TimeFormat.h"
#include "widgets/ConsoleWidget.h"
#include "widgets/ElidedLabel.h"
#include "widgets/FindDialog.h"
#include "widgets/InfoDockWidget.h"
#include "widgets/NetworkLegendWidget.h"
#include <QAction>
#include <QApplication>
#include <QBrush>
#include <QCheckBox>
#include <QClipboard>
#include <QColor>
#include <QCoreApplication>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QGraphicsItem>
#include <QGraphicsRectItem>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneMouseEvent>
#include <QHash>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLineF>
#include <QList>
#include <QListWidget>
#include <QMenu>
#include <QMetaObject>
#include <QMouseEvent>
#include <QObject>
#include <QPen>
#include <QPoint>
#include <QPointF>
#include <QPolygonF>
#include <QRect>
#include <QRectF>
#include <QScreen>
#include <QScrollArea>
#include <QSet>
#include <QSize>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTransform>
#include <QVector>
#include <QWidget>
#include <algorithm>
#include <cstdio>
#include <functional>
#include <limits>
#include <memory>
#include <QWindow>
#include <string>
#include <utility>

namespace {

constexpr qreal kSignalDetailZoom = 8.0;

} // namespace

// Checks the controls and the look of a live run on the canvas: the command bar with its Tab order, sizes and icons, the
// docks, the speed controls, the map key, the selection cues, the signal heads, the layer toggles, the context menus, the
// Follow selector with its camera, the station squares and the colours of the services. It waits for trains on the canvas
// (QEGTRAIN_AUTOSTART), and for trains of a coloured and of another service when the services have colours, which
// renderSnapshot reports through the frameRendered hook on every live frame. QEGTRAIN_E2E_*_SCREENSHOT name the files for
// pictures of the view. It then stops the run, prints E2E_VISUAL_POLISH_OK and exits with 0, or prints the failures and
// exits with 2.
void MainWindowTestAccess::runVisualPolish(MainWindow& w) {
	if (scenarioStarted)
		return;
	if (w.m_presenter->allTrains.isEmpty() && scenarioAttempts < 20) {
		++scenarioAttempts;
		QTimer::singleShot(500, &w, [&w] { MainWindowTestAccess::runVisualPolish(w); });
		return;
	}
	// The colours that the services ask for. When there are some, the check below
	// needs trains of a coloured and of another service on the canvas, so wait for
	// them until the run ends; every rendered frame calls this function again.
	QHash<QString, QColor> serviceColors;
	for (const SceneService& service : w.m_document->mutableModel().services) {
		int red = 0, green = 0, blue = 0;
		if (sceneParseVisualizationColor(service.visualizationColor, &red, &green, &blue))
			serviceColors.insert(QString::fromStdString(service.id), QColor(red, green, blue));
	}
	if (!serviceColors.isEmpty() && w.m_snapshot && w.m_snapshot->timestep < w.m_snapshot->totalTimesteps - 1) {
		const auto isColored = [&serviceColors](const TrainItemGroup* train) {
			return train && serviceColors.contains(QString::fromStdString(train->serviceId));
		};
		const bool anyColored = std::any_of(w.m_presenter->allTrains.cbegin(), w.m_presenter->allTrains.cend(), isColored);
		const bool anyDefault = std::any_of(w.m_presenter->allTrains.cbegin(), w.m_presenter->allTrains.cend(),
			[&isColored](const TrainItemGroup* train) { return train && !isColored(train); });
		if (!anyColored || !anyDefault)
			return;
	}
	scenarioStarted = true;

	bool ok = true;
	QStringList failures;
	const auto clampedCameraCenter = [&w](const QPointF& target) {
		if (!w.networkView || !w.networkView->viewport())
			return target;
		const QRectF sceneBounds = w.networkView->sceneRect();
		if (sceneBounds.isEmpty())
			return target;
		const QRectF visibleBounds = w.networkView->mapToScene(
													  w.networkView->viewport()->rect())
										 .boundingRect();
		const auto clampAxis = [](qreal value, qreal lower, qreal upper) {
			if (lower > upper)
				return (lower + upper) / 2.0;
			return qBound(lower, value, upper);
		};
		return QPointF(
			clampAxis(target.x(), sceneBounds.left() + visibleBounds.width() / 2.0,
				sceneBounds.right() - visibleBounds.width() / 2.0),
			clampAxis(target.y(), sceneBounds.top() + visibleBounds.height() / 2.0,
				sceneBounds.bottom() - visibleBounds.height() / 2.0));
	};
	const QString timelineSentinel = QStringLiteral("E2E timeline status sentinel");
	w.statusBar()->showMessage(timelineSentinel, 10000);
	if (w.m_snapshot)
		w.updateTimeline(w.m_snapshot->timestep, w.m_snapshot->totalTimesteps);
	if (w.statusBar()->currentMessage() != timelineSentinel) {
		ok = false;
		failures << "timeline update overwrote transient status guidance";
	}
	TrainItemGroup* selectedTrain = nullptr;
	TrainBodyItem* selectedTrainBody = nullptr;
	QPen selectedTrainPen;
	QBrush selectedTrainBrush;
	// The train that the check of the entry makes run on the route of the followed train.
	int enteredTrainIndex = -1;

	if (!w.m_speedSlider || !w.m_speedLabel) {
		ok = false;
		failures << "missing speed controls";
	} else {
		if (!w.m_speedSlider->toolTip().contains("right is faster")) {
			ok = false;
			failures << "speed slider startup tooltip does not name its direction";
		}
		w.m_speedSlider->setValue(250);
		QApplication::processEvents();
		if (!w.m_speedLabel->text().contains("4.0x")) {
			ok = false;
			failures << "speed label did not update";
		}
		w.m_speedSlider->setValue(w.m_speedSlider->maximum());
	}

	if (!w.m_followAction || !w.m_followTrainCombo || w.m_followTrainCombo->count() == 0) {
		ok = false;
		failures << "missing follow controls";
	} else {
		w.m_followTrainCombo->setCurrentIndex(0);
	}

	if (w.m_presenter->allTrains.isEmpty()) {
		ok = false;
		failures << "no train items rendered";
	}
	const QVector<NetworkLegendEntry> mapKeyEntriesBeforeFit = w.m_networkLegendWidget
		? w.m_networkLegendWidget->entries()
		: QVector<NetworkLegendEntry>();
	if (!std::any_of(mapKeyEntriesBeforeFit.cbegin(), mapKeyEntriesBeforeFit.cend(),
			[](const NetworkLegendEntry& entry) { return entry.kind == NetworkLegendEntryKind::Train; })) {
		ok = false;
		failures << "map key did not refresh when trains entered the network";
	}
	const auto mapKeyHasRow = [&mapKeyEntriesBeforeFit](const QString& label) {
		return std::any_of(mapKeyEntriesBeforeFit.cbegin(), mapKeyEntriesBeforeFit.cend(),
			[&label](const NetworkLegendEntry& entry) {
				return entry.kind == NetworkLegendEntryKind::Train && entry.label == label;
			});
	};
	if (!mapKeyHasRow(QStringLiteral("Train"))) {
		ok = false;
		failures << "map key has no Train row";
	}
	if (mapKeyHasRow(QStringLiteral("Sprinter")) || mapKeyHasRow(QStringLiteral("Intercity"))) {
		ok = false;
		failures << "map key still has rows named after train kinds";
	}
	// Each train body is painted in the colour of its service, or in the default
	// colour. The expected colours were read from the services themselves.
	int coloredTrains = 0;
	int defaultTrains = 0;
	QSet<QString> coloredTrainServices;
	for (const TrainItemGroup* train : w.m_presenter->allTrains) {
		const TrainBodyItem* body = train && train->trainPolygonItemList && !train->trainPolygonItemList->isEmpty()
			? train->trainPolygonItemList->first()
			: nullptr;
		const QString serviceId = train ? QString::fromStdString(train->serviceId) : QString();
		const bool custom = serviceColors.contains(serviceId);
		const QColor expectedFill = custom ? serviceColors.value(serviceId) : defaultTrainFill();
		const QColor expectedOutline = custom ? expectedFill.darker(200) : defaultTrainOutline();
		if (!body || body->brush().color() != expectedFill || body->pen().color() != expectedOutline) {
			ok = false;
			failures << (custom ? QString("train body of service %1 is not painted in its colour").arg(serviceId)
								: QStringLiteral("train body is not painted in the default fill"));
			break;
		}
		if (custom) {
			++coloredTrains;
			coloredTrainServices.insert(serviceId);
		} else {
			++defaultTrains;
		}
	}
	if (!serviceColors.isEmpty()) {
		if (coloredTrains == 0 || defaultTrains == 0) {
			ok = false;
			failures << QString("the scene needs trains of coloured and of other services (%1 coloured, %2 default, %3 coloured services)")
							.arg(coloredTrains)
							.arg(defaultTrains)
							.arg(serviceColors.size());
		}
		for (const QString& serviceId : coloredTrainServices) {
			const bool hasRow = std::any_of(mapKeyEntriesBeforeFit.cbegin(), mapKeyEntriesBeforeFit.cend(),
				[&](const NetworkLegendEntry& entry) {
					return entry.kind == NetworkLegendEntryKind::Train && entry.color == serviceColors.value(serviceId)
						&& entry.toolTip.split(QStringLiteral(", ")).contains(serviceId);
				});
			if (!hasRow) {
				ok = false;
				failures << QString("map key has no row for the colour of service %1").arg(serviceId);
			}
		}
		if (ok)
			std::fprintf(stdout, "E2E_VISUAL_POLISH_SERVICE_COLOR_OK %d coloured, %d default\n", coloredTrains, defaultTrains);
	}
	const auto hasOperationalLegendEntry = [&mapKeyEntriesBeforeFit](const QString& label) {
		return std::any_of(mapKeyEntriesBeforeFit.cbegin(), mapKeyEntriesBeforeFit.cend(),
			[&label](const NetworkLegendEntry& entry) { return entry.label == label; });
	};
	if (hasOperationalLegendEntry(QStringLiteral("Permissive signalling"))
		|| hasOperationalLegendEntry(QStringLiteral("Blocked section"))) {
		ok = false;
		failures << "runtime key shows operational states absent from canvas paint";
	}
	if (!hasOperationalLegendEntry(QStringLiteral("Occupied section"))) {
		ok = false;
		failures << "runtime key has no row for the occupied sections";
	}

	if (qEnvironmentVariableIsSet("QEGTRAIN_E2E_VISUAL_POLISH") && w.scene && !w.m_presenter->allTrains.isEmpty()) {
		TrainItemGroup* testTrain = w.m_presenter->allTrains.first();
		if (!testTrain || !testTrain->trainPolygonItemList || testTrain->trainPolygonItemList->isEmpty()
			|| !testTrain->trainPolygonItemList->first()
			|| testTrain->trainPolygonItemList->first()->polygon().isEmpty()) {
			ok = false;
			failures << "cannot inject virtual coupling overlay";
		} else {
			w.m_presenter->paintVCouplingMsg(testTrain, "E2E virtual coupling overlay");
			const int afterFirst = w.scene->items().size();
			w.m_presenter->paintVCouplingMsg(testTrain, "E2E virtual coupling overlay");
			const int afterSecond = w.scene->items().size();
			if (afterSecond != afterFirst) {
				ok = false;
				failures << "virtual coupling overlay recreated between updates";
			}
			if (auto* overlay = w.m_presenter->m_vcMessageItems.value(testTrain->index, nullptr))
				overlay->setVisible(false);
		}
	}

	// Shell contract: the visual smoke must cover the controls users see before
	// opening any secondary panel, not only the network scene.
	if (!w.m_toolBar || w.m_toolBar->toolButtonStyle() != Qt::ToolButtonTextBesideIcon) {
		ok = false;
		failures << "toolbar is not compact text-beside-icon style";
	}
	const auto standardIconPresent = [](QAction* action) {
		return action && !action->icon().isNull();
	};
	if (!standardIconPresent(w.ui->actionSimulationStart)
		|| !standardIconPresent(w.ui->actionSimulationPause)
		|| !standardIconPresent(w.ui->actionSimulationStop)) {
		ok = false;
		failures << "simulation toolbar icons are missing or not standard media icons";
	}

	// Command-bar contract: names and order are stable so the toolbar remains
	// testable without relying on translated display text.
	const auto actionNamed = [&w](const char* objectName) {
		return w.findChild<QAction*>(objectName);
	};
	QAction* openCaseAction = actionNamed("actionOpenCase");
	QAction* runAction = actionNamed("actionRun");
	QAction* pauseAction = actionNamed("actionPause");
	QAction* stopAction = actionNamed("actionStop");
	QAction* zoomInAction = actionNamed("actionZoomIn");
	QAction* zoomOutAction = actionNamed("actionZoomOut");
	QAction* fitAction = actionNamed("actionFit");
	const QList<QAction*> requiredActions{
		openCaseAction, runAction, pauseAction, stopAction, zoomInAction, zoomOutAction, fitAction};
	if (std::any_of(requiredActions.cbegin(), requiredActions.cend(), [](QAction* action) { return !action; })) {
		ok = false;
		failures << "command-bar actions do not have the required stable object names";
	}
	if (!openCaseAction || openCaseAction->property("chooserEntryPoint").toString() != "showStartupChooser") {
		ok = false;
		failures << "Open Case is not marked as the shared startup chooser entry point";
	}
	if (w.m_toolBar) {
		const QStringList expectedToolbarObjects{
			"openCaseButton", "separatorCasePlayback", "actionRunButton", "actionPauseButton", "actionStopButton",
			"speedSlider", "speedModeLabel", "separatorPlaybackView", "actionFollowButton",
			"followTrainCombo", "actionZoomInButton", "actionZoomOutButton", "actionFitButton"};
		QStringList toolbarObjects;
		for (QAction* action : w.m_toolBar->actions()) {
			if (!action)
				continue;
			if (QWidget* widget = w.m_toolBar->widgetForAction(action)) {
				if (!widget->objectName().isEmpty())
					toolbarObjects << widget->objectName();
				else if (!action->objectName().isEmpty())
					toolbarObjects << action->objectName();
			} else if (!action->objectName().isEmpty()) {
				toolbarObjects << action->objectName();
			}
		}
		int lastIndex = -1;
		for (const QString& objectName : expectedToolbarObjects) {
			const int index = toolbarObjects.indexOf(objectName);
			if (index <= lastIndex) {
				ok = false;
				failures << QString("command-bar order is missing or out of order: %1").arg(objectName);
				break;
			}
			lastIndex = index;
		}
		const auto boundaryIndex = [&toolbarObjects](const char* objectName) {
			return toolbarObjects.indexOf(QString::fromLatin1(objectName));
		};
		const int casePlaybackBoundary = boundaryIndex("separatorCasePlayback");
		const int playbackViewBoundary = boundaryIndex("separatorPlaybackView");
		const int openCaseIndex = boundaryIndex("openCaseButton");
		const int runIndex = boundaryIndex("actionRunButton");
		const int readoutIndex = boundaryIndex("speedModeLabel");
		const int followIndex = boundaryIndex("actionFollowButton");
		if (casePlaybackBoundary <= openCaseIndex || playbackViewBoundary <= casePlaybackBoundary
			|| runIndex <= casePlaybackBoundary || readoutIndex >= playbackViewBoundary || followIndex <= playbackViewBoundary) {
			ok = false;
			failures << "command-bar Case, Playback, and View ranges are not separated in order";
		}
		const auto checkBoundary = [&w, &ok, &failures](const char* objectName) {
			QAction* boundary = w.findChild<QAction*>(objectName);
			if (!boundary || !boundary->isSeparator() || !boundary->property("toolbarGroupBoundary").toBool()) {
				ok = false;
				failures << QString("command-bar group boundary is not named: %1").arg(objectName);
			}
		};
		checkBoundary("separatorCasePlayback");
		checkBoundary("separatorPlaybackView");
		if (pauseAction && stopAction) {
			const int pauseIndex = toolbarObjects.indexOf("actionPauseButton");
			const int stopIndex = toolbarObjects.indexOf("actionStopButton");
			if (pauseIndex < 0 || stopIndex != pauseIndex + 1) {
				ok = false;
				failures << "Pause is not immediately followed by Stop in the command bar";
			}
		}
	}

	const auto toolbarWidget = [&w](QAction* action) -> QWidget* {
		return w.m_toolBar && action ? w.m_toolBar->widgetForAction(action) : nullptr;
	};
	if (!toolbarWidget(openCaseAction)) {
		ok = false;
		failures << "Open Case has no toolbar button";
	} else if (toolbarWidget(openCaseAction)->objectName() != "openCaseButton") {
		ok = false;
		failures << "Open Case toolbar button has no stable object name";
	}
	const auto checkTransportIcon = [&ok, &failures, &actionNamed](const char* actionName, const char* resource) {
		QAction* action = actionNamed(actionName);
		QFile iconFile(QString::fromLatin1(resource));
		if (!action || !iconFile.exists() || action->icon().isNull()
			|| action->property("iconResource").toString() != QString::fromLatin1(resource)) {
			ok = false;
			failures << QString("transport icon is missing or not assigned: %1").arg(resource);
		}
	};
	checkTransportIcon("actionRun", ":/icons/run.svg");
	checkTransportIcon("actionPause", ":/icons/pause.svg");
	checkTransportIcon("actionStop", ":/icons/stop.svg");

	if (w.findChild<QWidget*>("toolbarCaseLabel") || w.findChild<QWidget*>("simulationClockLabel")) {
		ok = false;
		failures << "toolbar still contains the case badge or simulation clock";
	}
	for (QAction* action : w.ui->menuSimulation->actions()) {
		if (action->text().contains("Start Time", Qt::CaseInsensitive)) {
			ok = false;
			failures << "the Simulation menu has a Start Time entry";
		}
	}
	// Set Output Folder... is the last entry above the separator before Quit.
	const QList<QAction*> fileActions = w.ui->menuFile->actions();
	const int quitIndex = fileActions.indexOf(w.ui->actionQuit);
	if (quitIndex < 2 || fileActions.at(quitIndex - 2) != w.m_outputFolderAction || !fileActions.at(quitIndex - 1)->isSeparator()) {
		ok = false;
		failures << "File > Set Output Folder... is not above the separator before Quit";
	}
	// Each menu of the menu bar has an Alt mnemonic of its own.
	// QKeySequence::mnemonic() is empty on macOS, so the letter after the & is read here.
	QString mnemonics;
	for (QAction* menu : w.menuBar()->actions()) {
		const QString title = menu->text();
		const int marker = title.indexOf('&');
		const QChar key = marker >= 0 && marker + 1 < title.size() ? title.at(marker + 1).toLower() : QChar();
		if (menu->isVisible() && (key.isNull() || mnemonics.contains(key))) {
			ok = false;
			failures << QString("the menu %1 has no mnemonic of its own").arg(title);
		}
		mnemonics += key;
	}
	if (!w.m_speedSlider || !w.m_speedSlider->accessibleDescription().contains("right is faster")) {
		ok = false;
		failures << "speed slider does not describe its direction";
	} else {
		w.m_speedSlider->setValue(w.m_speedSlider->minimum());
		const int slowDelay = stepDelayForSlider(w.m_speedSlider->value());
		w.m_speedSlider->setValue(w.m_speedSlider->maximum());
		const int fastDelay = stepDelayForSlider(w.m_speedSlider->value());
		if (fastDelay >= slowDelay) {
			ok = false;
			failures << "speed slider right-is-faster mapping changed";
		}
		if (!w.m_speedSlider->toolTip().contains("right is faster")) {
			ok = false;
			failures << "speed slider tooltip does not name its direction";
		}
	}
	if (!w.m_followAction || !w.m_followTrainCombo || w.m_followTrainCombo->minimumWidth() != 180
		|| w.m_followTrainCombo->maximumWidth() > 200) {
		ok = false;
		failures << "Follow or train selector width contract is missing";
	}

	const auto checkTabTraversal = [&ok, &failures, &toolbarWidget](QAction* firstAction, const QStringList& expected) {
		QWidget* first = toolbarWidget(firstAction);
		if (!first) {
			ok = false;
			failures << "cannot start command-bar Tab traversal";
			return;
		}
		first->setFocusPolicy(Qt::StrongFocus);
		first->setFocus(Qt::TabFocusReason);
		QStringList actual;
		QApplication::processEvents();
		if (QApplication::focusWidget() != first) {
			ok = false;
			failures << QString("command-bar focus did not start on %1").arg(first->objectName());
			return;
		}
		actual << first->objectName();
		const auto namedAncestor = [](QWidget* widget) {
			for (QWidget* current = widget; current; current = current->parentWidget()) {
				if (!current->objectName().isEmpty())
					return current;
			}
			return widget;
		};
		for (int i = 1; i < expected.size(); ++i) {
			QKeyEvent press(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
			QApplication::sendEvent(first, &press);
			QApplication::processEvents();
			QWidget* focused = QApplication::focusWidget();
			QWidget* named = namedAncestor(focused);
			actual << (named ? named->objectName() : QString());
			first = focused;
			if (!first)
				break;
		}
		if (actual.size() != expected.size()
			|| !std::equal(actual.begin(), actual.end(), expected.begin())) {
			ok = false;
			failures << QString("command-bar Tab traversal order mismatch: %1").arg(actual.join(", "));
		}
	};
	w.showNormal();
	QApplication::processEvents();
	// The window may be larger than the screen only where its own minimum size is.
	const QSize restoredLimit = w.screen()->availableSize().expandedTo(w.minimumSizeHint());
	if (w.width() > restoredLimit.width() || w.height() > restoredLimit.height()) {
		ok = false;
		failures << QString("the restored window is %1x%2, larger than the screen area %3x%4")
						.arg(w.width())
						.arg(w.height())
						.arg(restoredLimit.width())
						.arg(restoredLimit.height());
	}
	w.resize(1024, 720);
	QApplication::processEvents();
	QStringList expectedTabTraversal{"openCaseButton"};
	if (runAction && runAction->isEnabled())
		expectedTabTraversal << "actionRunButton";
	expectedTabTraversal << "actionPauseButton" << "actionStopButton" << "speedSlider"
						 << "actionFollowButton" << "followTrainCombo" << "actionZoomInButton"
						 << "actionZoomOutButton" << "actionFitButton";
	checkTabTraversal(openCaseAction, expectedTabTraversal);

	const auto caseDock = w.findChild<QDockWidget*>("caseLayersDock");
	if (!caseDock || !caseDock->isVisible()) {
		ok = false;
		failures << "case and layers dock is not visible by default";
	}
	const auto caseLabel = w.findChild<QLabel*>("caseNameLabel");
	if (!caseLabel || caseLabel->text().trimmed().isEmpty()) {
		ok = false;
		failures << "case state is empty";
	}
	if (w.findChild<QLabel*>("simulationClockLabel")) {
		ok = false;
		failures << "toolbar still owns the simulation clock";
	}

	const auto secondaryDockHidden = [](QDockWidget* dock) {
		return dock && !dock->isVisible();
	};
	if (!secondaryDockHidden(w.m_logPane) || !secondaryDockHidden(w.m_validationDock)
		|| !secondaryDockHidden(w.m_compositionDock) || !secondaryDockHidden(w.m_serviceDock)
		|| !secondaryDockHidden(w.m_incidentDock)) {
		ok = false;
		failures << "secondary diagnostics docks are visible by default";
	}
	QAction* advancedDetailsAction = actionNamed("actionAdvancedDetails");
	if (!advancedDetailsAction || !w.m_loadedDataDock) {
		ok = false;
		failures << "advanced details presentation controls are missing";
	} else {
		advancedDetailsAction->setChecked(false);
		QApplication::processEvents();
		if (w.m_loadedDataDock->isVisible()) {
			ok = false;
			failures << "loaded data review opened automatically in normal mode";
		}
		w.m_loadedDataDock->toggleViewAction()->trigger();
		QApplication::processEvents();
		if (!w.m_loadedDataDock->isVisible()) {
			ok = false;
			failures << "loaded data review is not available from its explicit View action";
		}
		w.m_loadedDataDock->hide();
	}

	w.showNormal();
	w.resize(1200, 800);
	QApplication::processEvents();
	const auto checkCommandBarAtSize = [&w, &ok, &failures](int width, int height, const char* screenshotVariable) {
		w.resize(width, height);
		QApplication::processEvents();
		if (!w.m_toolBar) {
			ok = false;
			failures << QString("command bar is missing at %1x%2").arg(width).arg(height);
			return;
		}
		const QStringList requiredWidgetNames{
			"openCaseButton", "actionRunButton", "actionPauseButton", "actionStopButton", "speedSlider",
			"speedModeLabel", "actionFollowButton", "followTrainCombo",
			"actionZoomInButton", "actionZoomOutButton", "actionFitButton"};
		if (w.m_toolBar->height() < 44) {
			ok = false;
			failures << QString("command bar is too short at %1x%2: %3px").arg(width).arg(height).arg(w.m_toolBar->height());
		}
		const int toolbarCenterY = w.m_toolBar->rect().center().y();
		QList<QWidget*> controls;
		for (const QString& name : requiredWidgetNames) {
			QWidget* widget = w.findChild<QWidget*>(name);
			if (!widget || !widget->isVisibleTo(w.m_toolBar)) {
				ok = false;
				failures << QString("command-bar control %1 is hidden at %2x%3").arg(name).arg(width).arg(height);
				continue;
			}
			const QRect geometry = widget->geometry();
			const QRect toolbarArea = w.m_toolBar->rect().adjusted(0, 0, 0, 8);
			if (!toolbarArea.contains(geometry) || geometry.height() > 36) {
				ok = false;
				failures << QString("command-bar control %1 escapes toolbar bounds at %2x%3 (%4x%5+%6+%7)")
								.arg(name)
								.arg(width)
								.arg(height)
								.arg(geometry.width())
								.arg(geometry.height())
								.arg(geometry.x())
								.arg(geometry.y());
			}
			if (qAbs(geometry.center().y() - toolbarCenterY) > 1) {
				ok = false;
				failures << QString("command-bar control %1 is not vertically centered at %2x%3")
								.arg(name)
								.arg(width)
								.arg(height);
			}
			if (name == "followTrainCombo" && geometry.width() != 180) {
				ok = false;
				failures << QString("train selector rendered width changed at %1x%2: expected 180px, has %3px")
								.arg(width)
								.arg(height)
								.arg(geometry.width());
			}
			if (auto* button = qobject_cast<QToolButton*>(widget)) {
				if (button->toolButtonStyle() != Qt::ToolButtonIconOnly
					&& !button->text().isEmpty() && button->sizeHint().width() > geometry.width()) {
					ok = false;
					failures << QString("command-bar text is clipped at %1x%2: %3 needs %4px, has %5px")
									.arg(width)
									.arg(height)
									.arg(name)
									.arg(button->sizeHint().width())
									.arg(geometry.width());
				}
			}
			controls << widget;
		}
		for (int i = 0; i < controls.size(); ++i) {
			for (int j = i + 1; j < controls.size(); ++j) {
				if (controls[i]->geometry().intersects(controls[j]->geometry())) {
					ok = false;
					failures << QString("command-bar controls overlap at %1x%2: %3/%4")
									.arg(width)
									.arg(height)
									.arg(controls[i]->objectName())
									.arg(controls[j]->objectName());
				}
			}
		}
		const QString screenshotPath = qEnvironmentVariable(screenshotVariable);
		if (!screenshotPath.isEmpty() && !w.grab().save(screenshotPath)) {
			ok = false;
			failures << QString("command-bar screenshot save failed at %1x%2").arg(width).arg(height);
		}
	};
	checkCommandBarAtSize(1024, 720, "QEGTRAIN_E2E_COMMAND_BAR_1024_SCREENSHOT");
	checkCommandBarAtSize(1200, 800, "QEGTRAIN_E2E_COMMAND_BAR_1200_SCREENSHOT");
	checkCommandBarAtSize(1440, 900, "QEGTRAIN_E2E_COMMAND_BAR_1440_SCREENSHOT");
	// A little below the toolbar's own width the group separators keep the width of their hint and the height of the buttons,
	// and what no longer fits stays reachable through the extension button, which expands the toolbar to a second row.
	if (w.m_toolBar) {
		const int fullToolbarWidth = w.m_toolBar->sizeHint().width();
		int narrowestWidth = 0;
		for (int width = fullToolbarWidth + 40; width >= fullToolbarWidth - 160; width -= 8) {
			w.resize(width, 720);
			QApplication::processEvents();
			narrowestWidth = width;
			for (const char* boundaryName : {"separatorCasePlayback", "separatorPlaybackView"}) {
				QAction* boundary = w.findChild<QAction*>(boundaryName);
				QWidget* separator = boundary ? w.m_toolBar->widgetForAction(boundary) : nullptr;
				if (!separator || !separator->isVisibleTo(w.m_toolBar) || separator->width() != separator->sizeHint().width()
					|| separator->height() < w.m_toolBar->widgetForAction(openCaseAction)->height()) {
					ok = false;
					failures << QString("toolbar separator %1 does not keep its size at a window width of %2px")
									.arg(QString::fromLatin1(boundaryName))
									.arg(width);
				}
			}
		}
		auto* extension = w.m_toolBar->findChild<QToolButton*>("qt_toolbar_ext_button");
		QWidget* fitButton = w.findChild<QWidget*>("actionFitButton");
		if (!extension || !extension->isVisible() || !fitButton) {
			ok = false;
			failures << QString("toolbar has no extension button at a window width of %1px").arg(narrowestWidth);
		} else {
			const QImage extensionImage = extension->grab().toImage();
			int drawnPixels = 0;
			for (int y = 0; y < extensionImage.height(); ++y)
				for (int x = 0; x < extensionImage.width(); ++x)
					if (extensionImage.pixel(x, y) != extensionImage.pixel(0, 0))
						++drawnPixels;
			if (drawnPixels < 16) {
				ok = false;
				failures << QString("toolbar extension button shows no arrow: %1 drawn pixels").arg(drawnPixels);
			}
			const auto fitInToolbar = [&w, fitButton]() {
				return fitButton->isVisibleTo(w.m_toolBar) && w.m_toolBar->rect().contains(fitButton->geometry());
			};
			const int collapsedHeight = w.m_toolBar->height();
			extension->click();
			QElapsedTimer expansionWait;
			expansionWait.start();
			while (!fitInToolbar() && expansionWait.elapsed() < 1000)
				QApplication::processEvents(QEventLoop::AllEvents, 20);
			if (!fitInToolbar()) {
				ok = false;
				failures << "the extension button does not bring Fit into the toolbar";
			}
			// Collapse the toolbar again and widen the window until the toolbar has settled.
			extension->click();
			w.resize(fullToolbarWidth + 40, 720);
			expansionWait.restart();
			while ((!fitInToolbar() || w.m_toolBar->height() != collapsedHeight) && expansionWait.elapsed() < 1000)
				QApplication::processEvents(QEventLoop::AllEvents, 20);
		}
	}
	// Tooltips and accessible descriptions write a shortcut as the platform does, with the Command key on macOS.
	for (const char* name : {"actionRunButton", "actionPauseButton", "actionStopButton", "actionZoomInButton", "actionZoomOutButton"}) {
		const auto* button = w.findChild<QToolButton*>(name);
		const QAction* action = button ? button->defaultAction() : nullptr;
		const QString shortcut = action ? QStringLiteral("(%1)").arg(action->shortcut().toString(QKeySequence::NativeText)) : QString();
		if (!action || !action->toolTip().endsWith(shortcut) || !button->accessibleDescription().endsWith(shortcut)) {
			ok = false;
			failures << QString("%1 does not write its shortcut as the platform does: %2").arg(name, button ? button->accessibleDescription() : QString());
		}
	}
	// The Pause button reads Resume while the run is paused. It keeps its width, so the toolbar does not move and the
	// zoom buttons stay inside it.
	if (pauseAction && w.m_toolBar) {
		w.resize(1024, 720);
		QApplication::processEvents();
		const QStringList toolbarControlNames{"openCaseButton", "actionRunButton", "actionPauseButton", "actionStopButton",
			"speedSlider", "speedModeLabel", "actionFollowButton", "followTrainCombo", "actionZoomInButton",
			"actionZoomOutButton", "actionFitButton"};
		const QString pauseText = pauseAction->text();
		const auto controlGeometries = [&w, &toolbarControlNames]() {
			QList<QRect> geometries;
			for (const QString& name : toolbarControlNames) {
				const QWidget* control = w.findChild<QWidget*>(name);
				geometries << (control ? control->geometry() : QRect());
			}
			return geometries;
		};
		const QList<QRect> pauseGeometries = controlGeometries();
		pauseAction->setText("Resume");
		QApplication::processEvents();
		const QList<QRect> resumeGeometries = controlGeometries();
		const QRect toolbarArea = w.m_toolBar->rect();
		for (const char* zoomName : {"actionZoomInButton", "actionZoomOutButton"}) {
			const QWidget* zoom = w.findChild<QWidget*>(zoomName);
			if (!zoom || !zoom->isVisibleTo(w.m_toolBar) || !toolbarArea.contains(zoom->geometry())) {
				ok = false;
				failures << QString("%1 is not inside the toolbar while the Pause button reads Resume").arg(zoomName);
			}
		}
		pauseAction->setText(pauseText);
		QApplication::processEvents();
		if (pauseGeometries != resumeGeometries) {
			ok = false;
			failures << "the toolbar controls move or change size when the Pause button reads Resume";
		}
	}
	w.resize(1200, 800);
	QApplication::processEvents();
	const int defaultNetworkWidth = w.networkView ? w.networkView->width() : 0;
	const int caseDockWidth = caseDock ? caseDock->width() : 0;
	if (!w.networkView || !caseDock || defaultNetworkWidth <= caseDockWidth) {
		ok = false;
		failures << QString("network viewport (%1px) is not wider than case/layers dock (%2px)")
						.arg(defaultNetworkWidth)
						.arg(caseDockWidth);
	}
	const auto viewImage = [&w]() {
		QApplication::processEvents();
		return w.networkView->viewport()->grab().toImage().convertToFormat(QImage::Format_ARGB32);
	};
	// The part of the image that shows a rectangle of the scene, grown by margin pixels.
	const auto imageWindow = [&w](const QImage& image, const QRectF& sceneRect, int margin) {
		const qreal ratio = image.devicePixelRatioF();
		const QRect area = w.networkView->mapFromScene(sceneRect).boundingRect().adjusted(-margin, -margin, margin, margin);
		return QRect(QPoint(qRound(area.left() * ratio), qRound(area.top() * ratio)),
			QSize(qRound(area.width() * ratio), qRound(area.height() * ratio)))
			.intersected(image.rect());
	};
	const auto selectionCueCount = [&w]() {
		int count = 0;
		if (w.scene)
			for (const QGraphicsItem* item : w.scene->items())
				if (item->type() == SelectionCueItem::Type)
					++count;
		return count;
	};
	// The selected item has a cue that is seen around it, is gone when its opacity is zero and
	// leaves the picture of the item itself as it is.
	const auto checkCueSeen = [&](const char* what, const QRectF& sceneRect, int* seen, int* withoutCue) {
		SelectionCueItem* cue = w.m_presenter->effect ? w.m_presenter->effect->cue() : nullptr;
		if (!cue || !cue->isVisible()) {
			ok = false;
			failures << QString("%1 has no visible selection cue").arg(what);
			return false;
		}
		const QImage with = viewImage();
		cue->setOpacity(0.0);
		const QImage without = viewImage();
		cue->setOpacity(1.0);
		const QRect around = imageWindow(with, sceneRect, 24);
		*seen = 0;
		*withoutCue = 0;
		for (int y = around.top(); y <= around.bottom(); ++y) {
			for (int x = around.left(); x <= around.right(); ++x) {
				const auto isCue = [](const QColor& pixel) {
					return qAbs(pixel.red() - kSelectionCueColor.red()) <= 12 && qAbs(pixel.green() - kSelectionCueColor.green()) <= 12
						&& qAbs(pixel.blue() - kSelectionCueColor.blue()) <= 12;
				};
				*seen += isCue(with.pixelColor(x, y)) ? 1 : 0;
				*withoutCue += isCue(without.pixelColor(x, y)) ? 1 : 0;
			}
		}
		// The ring lies outside the item. The window rounds to whole pixels and a train mark has round corners,
		// so two pixels at each edge are left out.
		const QRect inside = imageWindow(with, sceneRect, -2);
		bool untouched = true;
		for (int y = inside.top(); y <= inside.bottom(); ++y)
			for (int x = inside.left(); x <= inside.right(); ++x)
				untouched = untouched && with.pixel(x, y) == without.pixel(x, y);
		const qreal ringPixels = cue->ringRect().width() * qAbs(w.networkView->transform().m11());
		if (*seen < 24 || *withoutCue != 0 || ringPixels < SelectionCueItem::MinPixels - 0.01 || !untouched) {
			ok = false;
			failures << QString("%1 selection cue is not right (%2 pixels with it, %3 without, ring %4 px, item %5)")
							.arg(what)
							.arg(*seen)
							.arg(*withoutCue)
							.arg(ringPixels, 0, 'f', 1)
							.arg(untouched ? "untouched" : "painted over");
			return false;
		}
		return true;
	};
	if (!w.scene || !w.networkView) {
		ok = false;
		failures << "cannot click a train without a scene and viewport";
	} else {
		w.networkView->zoomBy(4.0);
		QApplication::processEvents();
		for (auto* candidate : w.m_presenter->allTrains) {
			if (!candidate || !candidate->isVisible() || !candidate->trainPolygonItemList)
				continue;
			for (auto* body : *candidate->trainPolygonItemList) {
				if (body && body->isVisible() && !body->polygon().isEmpty()) {
					selectedTrain = candidate;
					selectedTrainBody = body;
					break;
				}
			}
			if (selectedTrainBody)
				break;
		}
		if (!selectedTrain || !selectedTrainBody) {
			ok = false;
			failures << "no visible train body available for scene click";
		} else {
			selectedTrainPen = selectedTrainBody->pen();
			selectedTrainBrush = selectedTrainBody->brush();
			QGraphicsSceneMouseEvent event(QEvent::GraphicsSceneMousePress);
			event.setButton(Qt::LeftButton);
			event.setButtons(Qt::LeftButton);
			event.setScenePos(selectedTrainBody->sceneBoundingRect().center());
			event.setWidget(w.networkView->viewport());
			w.scene->mousePressEvent(&event);
			// Compare the synchronous click result before animations advance.
			const int comboIndex = w.m_followTrainCombo ? w.m_followTrainCombo->findData(selectedTrain->index) : -1;
			if (!w.m_followTrainCombo || comboIndex < 0 || w.m_followTrainCombo->currentIndex() != comboIndex) {
				ok = false;
				failures << "train scene click did not select its combo row";
			}
			if (!w.m_followAction || !w.m_followAction->isChecked() || w.m_followTrainIndex != selectedTrain->index) {
				ok = false;
				failures << "train scene click did not activate follow";
			}
			if (!w.m_inspector->trainInfoWidget || !w.m_inspector->trainInfoWidget->isVisible() || w.m_inspector->trainIDText->text().trimmed().isEmpty()) {
				ok = false;
				failures << "train scene click did not populate train info";
			}
			if (!selectedTrain->graphicsEffect()) {
				ok = false;
				failures << "selected train has no halo effect";
			}
			if (selectedTrainBody->pen() != selectedTrainPen || selectedTrainBody->brush() != selectedTrainBrush) {
				ok = false;
				failures << "train source paint changed under halo effect";
			}
			// The centre of the viewport in view pixels is half its size. QRect::center rounds it down, which puts it up to a
			// pixel off, and so does the rounding of mapFromScene to whole pixels.
			const QTransform sceneToView = w.networkView->viewportTransform();
			const QPointF viewportCenter = sceneToView.inverted().map(QRectF(w.networkView->viewport()->rect()).center());
			const QRectF visibleScene = w.networkView->mapToScene(w.networkView->viewport()->rect()).boundingRect();
			const QRectF sceneBounds = w.networkView->sceneRect();
			const QPointF requestedCenter = selectedTrain->sceneBoundingRect().center();
			const auto clampedCenter = [](qreal value, qreal low, qreal high, qreal halfSpan) {
				if (high - low <= halfSpan * 2.0)
					return (low + high) / 2.0;
				return qBound(low + halfSpan, value, high - halfSpan);
			};
			const QPointF expectedCenter(
				clampedCenter(requestedCenter.x(), sceneBounds.left(), sceneBounds.right(), visibleScene.width() / 2.0),
				clampedCenter(requestedCenter.y(), sceneBounds.top(), sceneBounds.bottom(), visibleScene.height() / 2.0));
			const qreal centerTolerance = 2.0;
			const qreal centerDistance = QLineF(sceneToView.map(viewportCenter), sceneToView.map(expectedCenter)).length();
			if (centerDistance > centerTolerance) {
				ok = false;
				failures << QString("train scene click did not center the view (distance=%1px tolerance=%2px view=%3,%4 expected=%5,%6)")
								.arg(centerDistance, 0, 'f', 1)
								.arg(centerTolerance, 0, 'f', 1)
								.arg(viewportCenter.x(), 0, 'f', 1)
								.arg(viewportCenter.y(), 0, 'f', 1)
								.arg(expectedCenter.x(), 0, 'f', 1)
								.arg(expectedCenter.y(), 0, 'f', 1);
			}
			QApplication::processEvents();
		}
	}
	if (!w.m_presenter->allArcs.isEmpty() && w.m_presenter->allArcs.first() && w.m_presenter->allArcs.first()->arc) {
		w.m_inspector->displayArcInfo(w.m_presenter->allArcs.first());
		if (!w.m_inspector->arcOperationalStateText || w.m_inspector->arcOperationalStateText->text().trimmed().isEmpty()
			|| !w.m_inspector->arcConnectedSignalsText || w.m_inspector->arcConnectedSignalsText->text().trimmed().isEmpty()) {
			ok = false;
			failures << "arc inspector fields are empty";
		}
	}
	StationNodeItem* stationItem = nullptr;
	if (w.scene) {
		for (auto* item : w.scene->items()) {
			StationNodeItem* candidate = qgraphicsitem_cast<StationNodeItem*>(item);
			if (candidate && candidate->node) {
				stationItem = candidate;
				break;
			}
		}
	}
	if (stationItem && stationItem->node && !stationItem->node->stationName.empty()) {
		w.m_inspector->displayStationNodeInfo(stationItem);
		if (!w.m_inspector->nodeStationNameText || w.m_inspector->nodeStationNameText->text().trimmed().isEmpty()
			|| !w.m_inspector->nodeRegionText || w.m_inspector->nodeRegionText->text().trimmed().isEmpty()
			|| !w.m_inspector->nodeConnectedTracksText || w.m_inspector->nodeConnectedTracksText->text().trimmed().isEmpty()
			|| !w.m_inspector->nodeSignalledText || w.m_inspector->nodeSignalledText->text().trimmed().isEmpty()) {
			ok = false;
			failures << "station inspector fields are empty";
		}
	}
	SignalItem* inspectorSignal = nullptr;
	for (auto* candidate : w.m_presenter->allSignals) {
		if (candidate && candidate->isVisible()) {
			inspectorSignal = candidate;
			break;
		}
	}
	if (inspectorSignal) {
		w.m_inspector->displaySignallingInfo(inspectorSignal);
		if (!w.m_inspector->signallingAspectText || w.m_inspector->signallingAspectText->text().trimmed().isEmpty()
			|| !w.m_inspector->signallingProtectedSectionText || w.m_inspector->signallingProtectedSectionText->text().trimmed().isEmpty()
			|| !w.m_inspector->signallingNextTrackText || w.m_inspector->signallingNextTrackText->text().trimmed().isEmpty()) {
			ok = false;
			failures << "signal inspector fields are empty";
		}
		// The run names the track by its id in the scene and the section by its label; the tooltips keep the numbers and ids.
		const QString trackId = QString::fromStdString(w.m_presenter->sceneTrackId(inspectorSignal->trackID));
		const std::string& aheadLabel = inspectorSignal->sectionAheadLabel;
		if (trackId.isEmpty() || w.m_inspector->signallingTrackIDText->text() != trackId
			|| !w.m_inspector->signallingTrackIDText->toolTip().contains(QStringLiteral("Runtime track number: %1").arg(inspectorSignal->trackID))
			|| (!inspectorSignal->sectionAheadId.empty()
				&& (aheadLabel.empty() || w.m_inspector->signallingIDSectionAheadText->text() != QString::fromStdString(aheadLabel)
					|| !w.m_inspector->signallingIDSectionAheadText->toolTip().contains(QString::fromStdString(inspectorSignal->sectionAheadId))))
			|| !inspectorSignal->toolTip().startsWith(QStringLiteral("Track %1 at ").arg(trackId))) {
			ok = false;
			failures << QStringLiteral("signal inspector does not name track %1 and section %2 by id and label: %3 / %4")
							.arg(inspectorSignal->trackID)
							.arg(QString::fromStdString(inspectorSignal->sectionAheadId), w.m_inspector->signallingTrackIDText->text(),
								w.m_inspector->signallingIDSectionAheadText->text());
		}
	}
	w.m_inspector->handleCloseInfoDockWidget();
	w.m_inspector->dock()->hide();

	// Every kind of item gets one cue; another selection or a click on empty canvas removes it.
	if (w.scene) {
		NodeItem* nodeItem = nullptr;
		ConnectionItem* connectionItem = nullptr;
		for (auto* item : w.scene->items()) {
			auto* node = qgraphicsitem_cast<NodeItem*>(item);
			if (node && node->node && !nodeItem)
				nodeItem = node;
			auto* connection = qgraphicsitem_cast<ConnectionItem*>(item);
			if (connection && connection->connection && !connectionItem)
				connectionItem = connection;
		}
		struct Selection {
			const char* what;
			QGraphicsItem* item;
			std::function<void()> select;
		};
		QList<Selection> selections;
		if (!w.m_presenter->allArcs.isEmpty() && w.m_presenter->allArcs.first() && w.m_presenter->allArcs.first()->arc)
			selections.push_back({"arc", w.m_presenter->allArcs.first(), [&w]() { w.m_inspector->displayArcInfo(w.m_presenter->allArcs.first()); }});
		if (nodeItem)
			selections.push_back({"node", nodeItem, [&w, nodeItem]() { w.m_inspector->displayNodeInfo(nodeItem); }});
		if (connectionItem)
			selections.push_back({"connection", connectionItem, [&w, connectionItem]() { w.m_inspector->displayConnectionInfo(connectionItem); }});
		if (stationItem && stationItem->node)
			selections.push_back({"station node", stationItem, [&w, stationItem]() { w.m_inspector->displayStationNodeInfo(stationItem); }});
		if (inspectorSignal)
			selections.push_back({"signal", inspectorSignal, [&w, inspectorSignal]() { w.m_inspector->displaySignallingInfo(inspectorSignal); }});
		if (selectedTrain && selectedTrainBody)
			selections.push_back({"train", selectedTrain, [&w, selectedTrainBody]() { w.m_inspector->displayTrainDetails(selectedTrainBody, false); }});
		bool cueOk = selections.size() >= 4;
		if (!cueOk)
			failures << "too few kinds of item to check the selection cue";
		for (const Selection& selection : selections) {
			selection.select();
			QApplication::processEvents();
			SelectionCueItem* cue = w.m_presenter->effect ? w.m_presenter->effect->cue() : nullptr;
			if (cue)
				cue->sync();
			const QRectF itemRect = selection.item->sceneBoundingRect().adjusted(-1, -1, 1, 1);
			const bool around = cue
				&& (cue->followsLine() ? itemRect.contains(cue->targetLine().center())
									   : cue->targetRect().adjusted(-1, -1, 1, 1).intersects(itemRect));
			if (!cue || selectionCueCount() != 1 || !around || selection.item->graphicsEffect() != w.m_presenter->effect) {
				cueOk = false;
				failures << QString("selecting a %1 does not give exactly one cue around it (cue %2, count %3, around %4, own effect %5)")
								.arg(selection.what)
								.arg(cue != nullptr)
								.arg(selectionCueCount())
								.arg(around)
								.arg(selection.item->graphicsEffect() == w.m_presenter->effect);
			}
			// A station node is surrounded together with the pictogram of its station, which the near look does not draw.
			if (cue && selection.item == stationItem) {
				bool pictogramSurrounded = false;
				for (const StationOverlayItem* overlay : w.m_presenter->m_stationOverlays) {
					const QGraphicsItem* picture = overlay && overlay->hasSourceIdentity()
							&& overlay->matchesSourceIdentity(stationItem->node->ID, stationItem->track)
						? overlay->pictureItem()
						: nullptr;
					const auto* mark = dynamic_cast<const StationMarkItem*>(picture);
					if (picture && picture->isVisible()
						&& ((mark && mark->isNearLook()) || cue->targetRect().contains(picture->sceneBoundingRect())))
						pictogramSurrounded = true;
				}
				if (!pictogramSurrounded) {
					cueOk = false;
					failures << "the selection cue of a station node leaves out its pictogram";
				}
			}
		}
		w.m_inspector->handleDisableHighlight();
		QApplication::processEvents();
		if (selectionCueCount() != 0 || w.m_presenter->effect) {
			cueOk = false;
			failures << "a click on empty canvas left a selection cue";
		}
		// The cue follows the layer of the item it marks.
		if (selectedTrain && selectedTrainBody && w.m_trainLayerCheck) {
			w.m_inspector->displayTrainDetails(selectedTrainBody, false);
			QApplication::processEvents();
			SelectionCueItem* cue = w.m_presenter->effect ? w.m_presenter->effect->cue() : nullptr;
			// The scene emits its change signal some events after an item changes.
			const auto settle = []() {
				for (int pass = 0; pass < 3; ++pass)
					QApplication::processEvents();
			};
			w.m_trainLayerCheck->setChecked(false);
			settle();
			const bool hidden = cue && !cue->isVisible();
			w.m_trainLayerCheck->setChecked(true);
			settle();
			if (!hidden || !cue || !cue->isVisible()) {
				cueOk = false;
				failures << "the selection cue does not follow the train layer";
			}
			w.m_inspector->handleDisableHighlight();
			QApplication::processEvents();
			if (selectionCueCount() != 0) {
				cueOk = false;
				failures << "the selection cue stayed after the train was deselected";
			}
		}
		if (cueOk) {
			std::fprintf(stdout, "E2E_SELECTION_CUE_CLEAR_OK kinds=%d\n", static_cast<int>(selections.size()));
			std::fflush(stdout);
		} else {
			ok = false;
		}
	}

	if (!w.networkView || !w.m_incidentDock) {
		ok = false;
		failures << "incident geometry controls are missing";
	} else {
		w.m_incidentDock->show();
		w.m_incidentDock->raise();
		QApplication::processEvents();
		const int incidentNetworkWidth = w.networkView->width();
		const int incidentDockWidth = w.m_incidentDock->width();
		if (incidentNetworkWidth < 560 || incidentNetworkWidth <= incidentDockWidth || incidentDockWidth > 360) {
			ok = false;
			failures << QString("incident geometry network=%1px, dock=%2px violates 560px/wider/360px limits")
							.arg(incidentNetworkWidth)
							.arg(incidentDockWidth);
		}
		w.m_incidentDock->hide();
		QApplication::processEvents();
		const int restoredNetworkWidth = w.networkView->width();
		if (qAbs(restoredNetworkWidth - defaultNetworkWidth) > 8) {
			ok = false;
			failures << QString("network viewport did not restore: before=%1px, after=%2px")
							.arg(defaultNetworkWidth)
							.arg(restoredNetworkWidth);
		}
	}

	// Signal heads retain distinct authored positions and aspects at both scales.
	if (w.networkView && w.scene && !w.m_presenter->allSignals.isEmpty()) {
		for (const qreal zoom : {1.0, kSignalDetailZoom}) {
			w.networkView->fitToTopology();
			if (zoom > 1.0)
				w.networkView->zoomBy(zoom);
			w.updateViewportOverlays();
			for (auto* signal : w.m_presenter->allSignals) {
				if (!signal || !signal->isVisible() || signal->groupedSignalCount() != 0
					|| signal->scenePos() != signal->data(kSignalAnchorRole).toPointF()) {
					ok = false;
					failures << "authored signal head was hidden, moved or aggregated";
					break;
				}
			}
		}
	}

	const auto layerToggle = [&w](const char* objectName) {
		return w.findChild<QCheckBox*>(objectName);
	};
	QCheckBox* stationLayer = layerToggle("layerStationsPlatforms");
	QCheckBox* stationNames = layerToggle("layerStationNames");
	QCheckBox* trainLayer = layerToggle("layerTrains");
	QCheckBox* trainSpeedLabels = layerToggle("layerTrainSpeedLabels");
	QCheckBox* signalLayer = layerToggle("layerSignals");
	QCheckBox* passengerLayer = layerToggle("layerPassengers");
	const QStringList mapKeyEntries = w.m_networkLegendWidget
		? w.m_networkLegendWidget->entryLabels()
		: QStringList();
	// The run shows passengers, but a case without passengers has no passenger row in the map key.
	if (w.m_document->model().passengers.empty() && mapKeyEntries.contains(QStringLiteral("Passenger count"))) {
		ok = false;
		failures << "the map key lists Passenger count for a case without passengers";
	}
	if (!stationLayer || !stationNames || !trainLayer || !trainSpeedLabels
		|| !signalLayer || !passengerLayer) {
		ok = false;
		failures << "required layer controls are missing";
	} else {
		// Checkbox slots run synchronously. Do not deliver unrelated simulation
		// frames between the ownership counts for this layer-only assertion.
		const int initialItems = w.scene ? w.scene->items().size() : 0;
		const bool initialTrainVisible = !w.m_presenter->allTrains.isEmpty() && w.m_presenter->allTrains.first()->isVisible();
		trainLayer->setChecked(!trainLayer->isChecked());
		if (w.m_presenter->allTrains.isEmpty() || w.m_presenter->allTrains.first()->isVisible() == initialTrainVisible) {
			ok = false;
			failures << "train layer toggle is not functional";
		}
		trainLayer->setChecked(!trainLayer->isChecked());
		if (w.scene && w.scene->items().size() != initialItems) {
			ok = false;
			failures << "train layer toggle changed scene ownership";
		}
		const auto checkItemLayer = [&](QCheckBox* layer, const auto& items, const char* name) {
			if (items.isEmpty())
				return;
			const bool initiallyChecked = layer->isChecked();
			QVector<bool> initiallyVisible;
			initiallyVisible.reserve(items.size());
			for (auto* item : items)
				initiallyVisible.push_back(item && item->isVisible());
			layer->setChecked(false);
			QApplication::processEvents();
			if (std::any_of(items.cbegin(), items.cend(), [](auto* item) { return item && item->isVisible(); })) {
				ok = false;
				failures << QString("%1 layer did not hide its items").arg(name);
			}
			layer->setChecked(initiallyChecked);
			QApplication::processEvents();
			bool restored = true;
			int index = 0;
			for (auto* item : items) {
				if ((item && item->isVisible()) != initiallyVisible.at(index++)) {
					restored = false;
					break;
				}
			}
			if (!restored) {
				ok = false;
				failures << QString("%1 layer did not restore its items").arg(name);
			}
		};
		checkItemLayer(stationLayer, w.m_presenter->m_stationDecorations, "station decorations");
		if (w.m_presenter->m_stationOverlays.isEmpty()) {
			ok = false;
			failures << "station-name layer has no station overlay to verify";
		} else {
			StationOverlayItem* station = w.m_presenter->m_stationOverlays.first();
			const bool stationVisible = station && station->isVisible();
			stationNames->setChecked(false);
			QApplication::processEvents();
			if (!station || station->isLabelVisible() || station->isVisible() != stationVisible) {
				ok = false;
				failures << "station-name toggle changed the station object";
			}
			stationNames->setChecked(true);
		}
		if (w.m_presenter->m_trainBadges.isEmpty()) {
			ok = false;
			failures << "train speed-label layer has no badge to verify";
		} else {
			TrainBadgeItem* badge = w.m_presenter->m_trainBadges.first();
			const bool badgeVisible = badge && badge->isVisible();
			trainSpeedLabels->setChecked(false);
			QApplication::processEvents();
			if (!badge || badge->isSpeedVisible() || badge->isVisible() != badgeVisible) {
				ok = false;
				failures << "train speed-label toggle changed the train object";
			}
			trainSpeedLabels->setChecked(true);
		}
		checkItemLayer(signalLayer, w.m_presenter->m_signalDecorations, "signal groups");
		QList<QGraphicsItem*> passengerItems;
		for (auto* platform : w.m_presenter->allPlatforms) {
			if (!platform)
				continue;
			if (platform->textIcon)
				passengerItems.append(platform->textIcon);
			for (auto* icon : platform->passengerIcons)
				if (icon)
					passengerItems.append(icon);
		}
		if (initial_variables.PAX_GUI && (passengerItems.isEmpty() || std::none_of(passengerItems.cbegin(), passengerItems.cend(), [](auto* item) {
				return item && item->isVisible();
			}))) {
			ok = false;
			failures << "PAX enabled but no visible passenger-owned graphics rendered";
		}
		checkItemLayer(passengerLayer, passengerItems, "passenger load");
		for (auto* platform : w.m_presenter->allPlatforms) {
			if (!platform || platform->passengerIcons.isEmpty() || !w.networkView)
				continue;
			PassengerItem* icon = platform->passengerIcons.first();
			const QRectF sceneBounds = icon->sceneBoundingRect();
			// The first icon stands on the long axis of the bar, whatever the direction of the bar.
			const QRectF bar = platform->rect();
			const QPointF onAxis = platform->mapToScene(
				QPointF(bar.left() + bar.width() / platform->passengerIcons.size() / 2, bar.center().y()));
			const qreal firstWidth = icon->deviceTransform(w.networkView->viewportTransform())
										 .mapRect(icon->boundingRect())
										 .width();
			if (icon->flags().testFlag(QGraphicsItem::ItemIgnoresTransformations)
				|| !qFuzzyCompare(sceneBounds.center().x() + 1.0, onAxis.x() + 1.0)
				|| !qFuzzyCompare(sceneBounds.center().y() + 1.0, onAxis.y() + 1.0)) {
				ok = false;
				failures << "passenger glyph lost scene-scale centering";
			}
			w.networkView->zoomBy(2.0);
			const qreal secondWidth = icon->deviceTransform(w.networkView->viewportTransform())
										  .mapRect(icon->boundingRect())
										  .width();
			if (firstWidth > 0.0 && secondWidth < firstWidth * 1.8) {
				ok = false;
				failures << "passenger glyph did not scale with zoom";
			}
			w.networkView->fitToTopology();
			w.updateViewportOverlays();
			break;
		}
		const QStringList currentMapKeyEntries = w.m_networkLegendWidget
			? w.m_networkLegendWidget->entryLabels()
			: QStringList();
		if (!w.m_networkLegendWidget
			|| currentMapKeyEntries.size() != mapKeyEntries.size()
			|| !std::equal(currentMapKeyEntries.cbegin(), currentMapKeyEntries.cend(), mapKeyEntries.cbegin())) {
			ok = false;
			failures << "layer toggles changed or reordered map key entries";
		}
	}

	if (w.networkView) {
		w.networkView->fitToTopology();
		w.updateViewportOverlays();
		QApplication::processEvents();
	}

	const auto captureScreenshot = [&w, &ok, &failures](const char* variable, const char* label) {
		const QString path = qEnvironmentVariable(variable);
		if (path.isEmpty())
			return;
		QApplication::processEvents();
		if (!w.grab().save(path)) {
			ok = false;
			failures << QString("%1 screenshot save failed").arg(label);
		}
	};

	if (!w.m_networkLegendWidget || !w.m_networkLegendWidget->isVisible()
		|| !w.networkView || w.networkView->isAncestorOf(w.m_networkLegendWidget)) {
		ok = false;
		failures << "map key is missing from the left rail";
	} else {
		w.m_networkLegendWidget->setExpanded(false);
		QApplication::processEvents();
		if (w.m_networkLegendWidget->isExpanded() || !w.m_showMapKeyAction) {
			ok = false;
			failures << "map key did not collapse or lacks its View action";
		} else {
			w.m_showMapKeyAction->trigger();
			QApplication::processEvents();
			if (!w.m_networkLegendWidget->isExpanded() || !w.m_caseLayersDock->isVisible()) {
				ok = false;
				failures << "View did not restore the map key";
			}
		}
	}
	if (!w.networkView) {
		ok = false;
		failures << "network viewport is missing before default capture";
	} else {
		const QRectF visibleViewport = w.networkView->mapToScene(w.networkView->viewport()->rect()).boundingRect();
		const bool visibleTrain = std::any_of(w.m_presenter->allTrains.cbegin(), w.m_presenter->allTrains.cend(),
			[&visibleViewport](const TrainItemGroup* item) {
				return item && item->isVisible() && visibleViewport.intersects(item->sceneBoundingRect());
			});
		if (!visibleTrain) {
			ok = false;
			failures << "no historical train polygon is visible in default viewport";
		}

		const bool operationalTrack = std::any_of(w.m_presenter->allArcs.cbegin(), w.m_presenter->allArcs.cend(),
			[](const TrackLineItem* item) {
				return item && item->operationalState() != TrackOperationalState::Free;
			});
		const bool operationalTrackInViewport = std::any_of(w.m_presenter->allArcs.cbegin(), w.m_presenter->allArcs.cend(),
			[&visibleViewport](const TrackLineItem* item) {
				return item && item->operationalState() != TrackOperationalState::Free
					&& visibleViewport.intersects(item->sceneBoundingRect());
			});
		if (!operationalTrack) {
			ok = false;
			failures << "playback did not render an operational track state";
		} else if (!operationalTrackInViewport) {
			ok = false;
			failures << "no non-free track intersects default viewport";
		}
	}
	if (w.m_runController->m_worker)
		w.m_runController->m_worker->requestPause();

	// Capture the readable default view before changing follow or zoom state.
	if (w.networkView) {
		for (int i = 0; i < 100; ++i)
			w.ui->actionZoomIn->trigger();
		if (w.networkView->zoomRatio() < NetworkView::maximumZoomRatio() - 1e-5) {
			ok = false;
			failures << "toolbar zoom-in did not reach the station-detail clamp";
		}
		for (int i = 0; i < 100; ++i)
			w.ui->actionZoomOut->trigger();
		if (w.networkView->zoomRatio() > 1.0 + 1e-5) {
			ok = false;
			failures << "toolbar zoom-out did not reach the Fit clamp";
		}
		QKeyEvent fitPress(QEvent::KeyPress, Qt::Key_0, Qt::ControlModifier);
		QKeyEvent fitRelease(QEvent::KeyRelease, Qt::Key_0, Qt::ControlModifier);
		QApplication::sendEvent(&w, &fitPress);
		QApplication::sendEvent(&w, &fitRelease);
		if (w.networkView->zoomLabel() != QStringLiteral("Fit")) {
			ok = false;
			failures << "Ctrl+0 did not trigger the Fit action";
		}
	}
	captureScreenshot("QEGTRAIN_E2E_SCREENSHOT", "default");
	if (w.networkView) {
		w.networkView->zoomBy(TrainBadgeItem::identityZoomThreshold());
		w.updateViewportOverlays();
		QApplication::processEvents();
	}
	captureScreenshot("QEGTRAIN_E2E_MEDIUM_SCREENSHOT", "medium");
	if (w.networkView && !w.m_presenter->allTrains.isEmpty()) {
		w.networkView->fitToTopology();
		w.networkView->centerOn(w.m_presenter->allTrains.first()->sceneBoundingRect().center());
		w.networkView->zoomBy(3.0);
		if (qAbs(w.networkView->zoomRatio() - 3.0) > 1e-5) {
			ok = false;
			failures << "dense capture did not restore the 3x zoom state";
		}
		QApplication::processEvents();
		w.m_trainSpeedLabelsCheck->setChecked(false);
		QApplication::processEvents();
		w.m_trainSpeedLabelsCheck->setChecked(true);
		QApplication::processEvents();
	} else {
		ok = false;
		failures << "cannot create dense view without a train";
	}
	captureScreenshot("QEGTRAIN_E2E_DENSE_SCREENSHOT", "dense");
	if (w.networkView && selectedTrain && selectedTrainBody) {
		w.networkView->fitToTopology();
		w.m_inspector->displayTrainDetails(selectedTrainBody, false);
		QApplication::processEvents();
		TrainBadgeItem* selectedBadge = w.m_presenter->m_trainBadges.value(selectedTrain->index, nullptr);
		const bool ordinaryMarkers = std::all_of(w.m_presenter->m_trainBadges.cbegin(), w.m_presenter->m_trainBadges.cend(),
			[](const TrainBadgeItem* badge) {
				return !badge || badge->isPromoted()
					|| badge->presentation() == TrainBadgeItem::Presentation::Overview;
			});
		if (!selectedBadge || !selectedBadge->isPromoted()
			|| selectedBadge->presentation() != TrainBadgeItem::Presentation::Detailed
			|| !ordinaryMarkers) {
			ok = false;
			failures << "selected train was not promoted over Fit markers";
		}
	} else {
		ok = false;
		failures << "cannot create selected-train Fit capture";
	}
	captureScreenshot("QEGTRAIN_E2E_SELECTED_SCREENSHOT", "selected Fit");
	if (w.networkView && selectedTrain && selectedTrainBody) {
		// The cue of a selected train is seen at Fit.
		int seen = 0;
		int withoutCue = 0;
		if (checkCueSeen("selected train at Fit", selectedTrain->sceneBoundingRect(), &seen, &withoutCue)) {
			std::fprintf(stdout, "E2E_SELECTION_CUE_FIT_OK pixels=%d without=%d\n", seen, withoutCue);
			std::fflush(stdout);
		}
		// It keeps its size on screen and does not paint over the train at detail zoom.
		w.networkView->zoomBy(12.0);
		w.networkView->centerOn(selectedTrain->sceneBoundingRect().center());
		w.updateViewportOverlays();
		QApplication::processEvents();
		if (checkCueSeen("selected train at detail zoom", selectedTrain->sceneBoundingRect(), &seen, &withoutCue)) {
			std::fprintf(stdout, "E2E_SELECTION_CUE_DETAIL_OK pixels=%d without=%d\n", seen, withoutCue);
			std::fflush(stdout);
		}
	}
	if (w.networkView) {
		w.networkView->fitToTopology();
		w.networkView->zoomBy(TrainBadgeItem::detailedZoomThreshold());
		w.updateViewportOverlays();
		QApplication::processEvents();
	}

	SignalItem* visibleSignal = nullptr;
	QGraphicsItem* signalHit = nullptr;
	bool anyVisibleSignal = false;
	for (auto* signal : w.m_presenter->allSignals) {
		if (!signal || !signal->isVisible())
			continue;
		anyVisibleSignal = true;
		QGraphicsItem* hit = w.scene->itemAt(signal->sceneBoundingRect().center(), w.networkView->viewportTransform());
		if (hit == signal || (hit && hit->parentItem() == signal)) {
			visibleSignal = signal;
			signalHit = hit;
			break;
		}
	}
	if (!anyVisibleSignal) {
		ok = false;
		failures << "no visible signal rendered";
	} else if (!visibleSignal) {
		ok = false;
		failures << "signal scene hit-test did not resolve any visible glyph";
	} else {
		if (signalHit != visibleSignal && (!signalHit || signalHit->parentItem() != visibleSignal)) {
			ok = false;
			failures << "signal scene hit-test did not resolve the glyph";
		}
	}

	const auto requestContextMenu = [&w](const QPointF& scenePos, bool keyboard) -> QMenu* {
		if (!w.scene || !w.networkView)
			return nullptr;
		QGraphicsSceneContextMenuEvent event(QEvent::GraphicsSceneContextMenu);
		event.setReason(keyboard ? QGraphicsSceneContextMenuEvent::Keyboard : QGraphicsSceneContextMenuEvent::Mouse);
		event.setScenePos(scenePos);
		event.setScreenPos(w.networkView->viewport()->mapToGlobal(w.networkView->mapFromScene(scenePos)));
		event.setWidget(w.networkView->viewport());
		w.scene->contextMenuEvent(&event);
		QApplication::processEvents();
		return w.m_sceneContextMenu.data();
	};
	const auto closeContextMenu = [&w]() {
		if (w.m_sceneContextMenu) {
			w.m_sceneContextMenu->close();
			QApplication::processEvents();
		}
	};
	const auto findMenuAction = [&ok, &failures](QMenu* menu, const QString& text) -> QAction* {
		if (!menu) {
			ok = false;
			failures << QString("context menu missing while looking for %1").arg(text);
			return nullptr;
		}
		for (auto* action : menu->actions()) {
			if (action && action->text() == text)
				return action;
		}
		ok = false;
		failures << QString("context menu missing action %1").arg(text);
		return nullptr;
	};
	const auto checkDeferredAction = [&findMenuAction, &ok, &failures](QMenu* menu, const QString& text,
										 const QString& explanation) {
		QAction* action = findMenuAction(menu, text);
		if (!action)
			return;
		if (action->isEnabled() || action->toolTip() != explanation || action->statusTip() != explanation) {
			ok = false;
			failures << QString("deferred context action explanation mismatch: %1").arg(text);
		}
	};
	const QString trainRouteExplanation = QStringLiteral(
		"Requires the train-to-route association and an infrastructure query.");
	const QString stationTimetableExplanation = QStringLiteral(
		"Requires the station timetable association and an arrivals/departures query.");
	const QString trackOccupancyExplanation = QStringLiteral(
		"Requires the track-occupancy association and an active-train query.");
	const QString trackIncidentExplanation = QStringLiteral(
		"Requires the track-incident association and an incident query.");
	const QString signalApproachExplanation = QStringLiteral(
		"Requires the signal route association and an approaching-train query.");

	selectedTrain = nullptr;
	selectedTrainBody = nullptr;
	for (auto* candidate : w.m_presenter->allTrains) {
		if (!candidate || !candidate->isVisible() || !candidate->trainPolygonItemList)
			continue;
		for (auto* body : *candidate->trainPolygonItemList) {
			if (body && body->isVisible() && !body->polygon().isEmpty()) {
				selectedTrain = candidate;
				selectedTrainBody = body;
				break;
			}
		}
		if (selectedTrainBody)
			break;
	}
	if (!selectedTrainBody) {
		ok = false;
		failures << "no visible train body available after pausing visual checks";
	}
	if (selectedTrainBody) {
		w.setFollowTrain(-1);
		QMenu* menu = requestContextMenu(selectedTrainBody->sceneBoundingRect().center(), true);
		QAction* details = findMenuAction(menu, "Show details");
		if (!details || details->icon().isNull()) {
			ok = false;
			failures << "train details action is missing refreshed icon";
		} else {
			details->trigger();
			QApplication::processEvents();
			if (!w.m_inspector->trainInfoWidget || !w.m_inspector->trainInfoWidget->isVisible() || (w.m_followAction && w.m_followAction->isChecked()) || w.m_followTrainIndex != -1) {
				ok = false;
				failures << "train context details unexpectedly changed follow mode";
			}
		}
		QAction* center = findMenuAction(menu, "Center in view");
		if (center)
			center->trigger();
		QAction* copy = findMenuAction(menu, "Copy train index");
		if (copy) {
			copy->trigger();
			if (!QApplication::clipboard() || QApplication::clipboard()->text() != QString::number(selectedTrain->index)) {
				ok = false;
				failures << "train context copy action did not copy the index";
			}
		}
		QAction* follow = findMenuAction(menu, "Follow train");
		if (follow) {
			follow->trigger();
			QApplication::processEvents();
			if (!w.m_followAction || !w.m_followAction->isChecked() || w.m_followTrainIndex != selectedTrain->index) {
				ok = false;
				failures << "train context follow action did not activate follow mode";
			}
		}
		checkDeferredAction(menu, "Planned route and related infrastructure", trainRouteExplanation);
		const QString contextPath = qEnvironmentVariable("QEGTRAIN_E2E_CONTEXT_SCREENSHOT");
		if (contextPath.isEmpty() || !menu || !menu->grab().save(contextPath)) {
			ok = false;
			failures << "context menu screenshot save failed";
		}
		closeContextMenu();
	} else {
		ok = false;
		failures << "cannot open train context menu without a train body";
	}

	if (w.scene && w.networkView) {
		const int staleTrainIndex = 2147483001;
		auto* staleGroup = new TrainItemGroup();
		staleGroup->index = staleTrainIndex;
		staleGroup->trainDescription = "__e2e_stale_train__";
		staleGroup->trainType = "E2E";
		staleGroup->trainId = 1.0;
		auto* staleBody = new TrainBodyItem(QPolygonF({QPointF(0.0, 0.0), QPointF(20.0, 0.0),
			QPointF(20.0, 10.0), QPointF(0.0, 10.0)}));
		staleBody->index = staleTrainIndex;
		staleGroup->addToGroup(staleBody);
		staleGroup->trainPolygonItemList = new QList<TrainBodyItem*>();
		staleGroup->trainPolygonItemList->append(staleBody);
		staleGroup->setPos(w.scene->itemsBoundingRect().bottomRight() + QPointF(100.0, 100.0));
		w.scene->addItem(staleGroup);
		QMenu* menu = requestContextMenu(staleBody->sceneBoundingRect().center(), false);
		QAction* details = findMenuAction(menu, "Show details");
		const bool infoVisibleBefore = w.m_inspector->dock() && w.m_inspector->dock()->isVisible();
		const bool trainInfoVisibleBefore = w.m_inspector->trainInfoWidget && w.m_inspector->trainInfoWidget->isVisible();
		const QString infoTitleBefore = w.m_inspector->dock() ? w.m_inspector->dock()->windowTitle() : QString();
		const bool effectPresentBefore = w.m_presenter->effect != nullptr;
		// The bounds of a train group are those of its bodies, so the scene has to hear
		// of the change before the body leaves.
		staleGroup->prepareForChildGeometryChange();
		staleGroup->removeFromGroup(staleBody);
		delete staleBody;
		if (details)
			details->trigger();
		QApplication::processEvents();
		if ((w.m_inspector->dock() && w.m_inspector->dock()->isVisible()) != infoVisibleBefore
			|| (w.m_inspector->trainInfoWidget && w.m_inspector->trainInfoWidget->isVisible()) != trainInfoVisibleBefore
			|| (w.m_inspector->dock() && w.m_inspector->dock()->windowTitle() != infoTitleBefore)
			|| (w.m_presenter->effect != nullptr) != effectPresentBefore) {
			ok = false;
			failures << "stale train context action changed application state";
		}
		closeContextMenu();
		if (staleGroup->trainPolygonItemList) {
			staleGroup->trainPolygonItemList->clear();
			delete staleGroup->trainPolygonItemList;
			staleGroup->trainPolygonItemList = nullptr;
		}
		w.scene->removeItem(staleGroup);
		delete staleGroup;
	}

	if (stationItem) {
		QMenu* menu = nullptr;
		if (w.networkView) {
			const QPointF stationCenter = stationItem->sceneBoundingRect().center();
			const QPoint screenPos = w.networkView->viewport()->mapToGlobal(w.networkView->mapFromScene(stationCenter));
			w.showSceneContextMenu(stationItem, stationCenter, screenPos, false);
			QApplication::processEvents();
			menu = w.m_sceneContextMenu.data();
		}
		QAction* details = findMenuAction(menu, "Show details");
		if (!details || details->icon().isNull()) {
			ok = false;
			failures << "station details action is missing refreshed icon";
		} else {
			details->trigger();
			QApplication::processEvents();
			if (!w.m_inspector->nodeInfoWidget || !w.m_inspector->nodeInfoWidget->isVisible()) {
				ok = false;
				failures << "station context details did not open station info";
			}
		}
		QAction* copy = findMenuAction(menu, "Copy node ID");
		if (copy) {
			copy->trigger();
			const QString expectedNodeId = QString::number(stationItem->node->ID, 'g',
				std::numeric_limits<double>::max_digits10);
			if (!QApplication::clipboard() || QApplication::clipboard()->text() != expectedNodeId) {
				ok = false;
				failures << "station context copy action did not preserve node precision";
			}
		}
		checkDeferredAction(menu, "Filtered station arrivals and departures", stationTimetableExplanation);
		closeContextMenu();
	} else {
		ok = false;
		failures << "cannot open station context menu without a station";
	}

	TrackLineItem* contextArc = nullptr;
	QMenu* trackMenu = nullptr;
	for (auto* candidate : w.m_presenter->allArcs) {
		if (candidate && candidate->scene() == w.scene && candidate->arc) {
			contextArc = candidate;
			break;
		}
	}
	if (contextArc && w.networkView) {
		const QPointF trackCenter = contextArc->sceneBoundingRect().center();
		const QPoint screenPos = w.networkView->viewport()->mapToGlobal(w.networkView->mapFromScene(trackCenter));
		w.showSceneContextMenu(contextArc, trackCenter, screenPos, false);
		QApplication::processEvents();
		trackMenu = w.m_sceneContextMenu.data();
	}
	if (!contextArc) {
		ok = false;
		failures << "cannot open track context menu";
	} else {
		QAction* details = findMenuAction(trackMenu, "Show details");
		if (details) {
			details->trigger();
			QApplication::processEvents();
			if (!w.m_inspector->arcInfoWidget || !w.m_inspector->arcInfoWidget->isVisible()) {
				ok = false;
				failures << "track context details did not open arc info";
			}
		}
		QAction* copy = findMenuAction(trackMenu, "Copy arc ID");
		if (copy) {
			copy->trigger();
			const QString expectedArcId = QString::number(contextArc->arc->ID, 'g',
				std::numeric_limits<double>::max_digits10);
			if (!QApplication::clipboard() || QApplication::clipboard()->text() != expectedArcId) {
				ok = false;
				failures << "track context copy action did not preserve arc precision";
			}
		}
		checkDeferredAction(trackMenu, "Trains currently using this track", trackOccupancyExplanation);
		checkDeferredAction(trackMenu, "Incidents affecting this track", trackIncidentExplanation);
		closeContextMenu();
	}

	visibleSignal = nullptr;
	for (auto* signal : w.m_presenter->allSignals) {
		if (!signal || !signal->isVisible())
			continue;
		QGraphicsItem* hit = w.scene->itemAt(signal->sceneBoundingRect().center(),
			w.networkView->viewportTransform());
		if (hit == signal || (hit && hit->parentItem() == signal)) {
			visibleSignal = signal;
			break;
		}
	}
	if (visibleSignal) {
		QMenu* menu = requestContextMenu(visibleSignal->sceneBoundingRect().center(), false);
		QAction* details = findMenuAction(menu, "Show details");
		if (!details || details->icon().isNull()) {
			ok = false;
			failures << "signal details action is missing refreshed icon";
		} else {
			details->trigger();
			QApplication::processEvents();
			if (!w.m_inspector->signallingInfoWidget || !w.m_inspector->signallingInfoWidget->isVisible()) {
				ok = false;
				failures << "signal context details did not open signal info";
			}
		}
		QAction* copy = findMenuAction(menu, "Copy signal location");
		if (copy) {
			copy->trigger();
			const QString expectedSignalLocation = QString("track %1 @ %2 (%3)")
													   .arg(visibleSignal->trackID)
													   .arg(visibleSignal->X, 0, 'g', std::numeric_limits<double>::max_digits10)
													   .arg(visibleSignal->reversedDirection ? "reverse" : "forward");
			if (!QApplication::clipboard() || QApplication::clipboard()->text() != expectedSignalLocation) {
				ok = false;
				failures << "signal context copy action did not preserve position precision";
			}
		}
		// The section of the head by the id that the run uses, not by its label.
		QAction* copySection = findMenuAction(menu, "Copy section ID");
		const std::string expectedSection = !visibleSignal->sectionAheadId.empty() ? visibleSignal->sectionAheadId : visibleSignal->sectionBehindId;
		if (copySection)
			copySection->trigger();
		if (!copySection || !QApplication::clipboard() || QApplication::clipboard()->text() != QString::fromStdString(expectedSection)) {
			ok = false;
			failures << "signal context menu did not copy the section id of the head";
		}
		checkDeferredAction(menu, "Next train approaching this signal", signalApproachExplanation);
		closeContextMenu();
	} else {
		ok = false;
		failures << "cannot open signal context menu without a signal";
	}

	if (w.scene && w.networkView) {
		const int temporarySignalTrack = 2147483000;
		const double temporarySignalPosition = 0.12345678901234566;
		const bool temporarySignalReversed = true;
		auto* temporarySignal = new SignalItem(QRectF(-6.0, -8.0, 12.0, 16.0));
		temporarySignal->trackID = temporarySignalTrack;
		temporarySignal->X = temporarySignalPosition;
		temporarySignal->setReversedDirection(temporarySignalReversed);
		temporarySignal->setPos(w.scene->itemsBoundingRect().bottomRight() + QPointF(100.0, 100.0));
		w.scene->addItem(temporarySignal);
		w.m_presenter->allSignals.push_back(temporarySignal);
		QMenu* menu = requestContextMenu(temporarySignal->sceneBoundingRect().center(), false);
		QAction* copy = findMenuAction(menu, "Copy signal location");
		const QString expectedSignalLocation = QStringLiteral(
			"track 2147483000 @ 0.12345678901234566 (reverse)");
		if (copy) {
			copy->trigger();
			if (!QApplication::clipboard() || QApplication::clipboard()->text() != expectedSignalLocation) {
				ok = false;
				failures << "temporary signal context copy action did not preserve exact precision";
			}
		}
		const QString clipboardBeforeStaleTrigger = QApplication::clipboard()
			? QApplication::clipboard()->text()
			: QString();
		// The presenter must not keep the deleted signal: a frame drawn during processEvents() below would update it.
		w.m_presenter->allSignals.removeOne(temporarySignal);
		w.scene->removeItem(temporarySignal);
		delete temporarySignal;
		if (copy)
			copy->trigger();
		QApplication::processEvents();
		if (QApplication::clipboard() && QApplication::clipboard()->text() != clipboardBeforeStaleTrigger) {
			ok = false;
			failures << "stale signal context action changed the clipboard";
		}
		closeContextMenu();
	}

	PassengerItem* contextPassenger = nullptr;
	bool temporaryContextPassenger = false;
	if (w.scene) {
		for (auto* candidate : w.scene->items()) {
			auto* passenger = qgraphicsitem_cast<PassengerItem*>(candidate);
			if (passenger && passenger->isVisible()) {
				contextPassenger = passenger;
				break;
			}
		}
	}
	if (!contextPassenger) {
		contextPassenger = new PassengerItem(w.m_presenter->pax_pixmap_scaled);
		contextPassenger->passengerId = "__e2e_context_passenger__";
		contextPassenger->setPos(w.scene->itemsBoundingRect().bottomRight() + QPointF(100.0, 100.0));
		w.scene->addItem(contextPassenger);
		temporaryContextPassenger = true;
	}
	if (contextPassenger) {
		const QString passengerId = QString::fromStdString(contextPassenger->passengerId);
		QMenu* menu = requestContextMenu(contextPassenger->sceneBoundingRect().center(), false);
		QAction* details = findMenuAction(menu, "Show details");
		if (!details || details->icon().isNull()) {
			ok = false;
			failures << "passenger details action is missing refreshed icon";
		} else {
			details->trigger();
			QApplication::processEvents();
		}
		QAction* copy = findMenuAction(menu, "Copy passenger ID");
		if (copy) {
			copy->trigger();
			QApplication::processEvents();
			if (!QApplication::clipboard() || QApplication::clipboard()->text() != passengerId) {
				ok = false;
				failures << "passenger context copy action did not copy passenger ID";
			}
		}
		closeContextMenu();
		if (temporaryContextPassenger) {
			w.scene->removeItem(contextPassenger);
			delete contextPassenger;
		}
		w.m_presenter->removePaxInfoIcon();
		w.m_presenter->paxIconItem = nullptr;
	}

	if (w.scene && w.networkView) {
		const QPointF emptyPos = w.scene->itemsBoundingRect().bottomRight() + QPointF(1000.0, 1000.0);
		auto* temporarySelectionItem = new QGraphicsRectItem(QRectF(0.0, 0.0, 10.0, 10.0));
		temporarySelectionItem->setFlag(QGraphicsItem::ItemIsSelectable, true);
		temporarySelectionItem->setPos(emptyPos);
		w.scene->addItem(temporarySelectionItem);
		if (!selectedTrainBody) {
			ok = false;
			failures << "cannot establish selection for empty-space context menu";
		} else {
			QGraphicsSceneMouseEvent selectionEvent(QEvent::GraphicsSceneMousePress);
			selectionEvent.setButton(Qt::LeftButton);
			selectionEvent.setButtons(Qt::LeftButton);
			selectionEvent.setScenePos(selectedTrainBody->sceneBoundingRect().center());
			selectionEvent.setWidget(w.networkView->viewport());
			w.scene->mousePressEvent(&selectionEvent);
			QApplication::processEvents();
			if (!w.m_presenter->effect || !w.m_inspector->dock() || !w.m_inspector->dock()->isVisible()
				|| !w.m_followAction || !w.m_followAction->isChecked() || w.m_followTrainIndex != selectedTrain->index) {
				ok = false;
				failures << "empty-space context menu could not establish selection and follow mode";
			}
		}
		temporarySelectionItem->setSelected(true);
		QMenu* menu = requestContextMenu(emptyPos, false);
		QAction* fit = findMenuAction(menu, "Fit whole network");
		QAction* clear = findMenuAction(menu, "Clear selection");
		QAction* stop = findMenuAction(menu, "Stop following train");
		if (!menu || !fit || !clear || !stop) {
			ok = false;
			failures << "empty-space context menu is incomplete";
		} else {
			fit->trigger();
			QApplication::processEvents();
			clear->trigger();
			QApplication::processEvents();
			stop->trigger();
			QApplication::processEvents();
			if ((w.m_inspector->dock() && w.m_inspector->dock()->isVisible()) || w.m_presenter->effect
				|| (w.m_followAction && w.m_followAction->isChecked()) || w.m_followTrainIndex != -1) {
				ok = false;
				failures << "empty-space context actions did not clear selection and follow mode";
			}
			if (temporarySelectionItem->isSelected()) {
				ok = false;
				failures << "empty-space context action left a temporary scene item selected";
			}
		}
		closeContextMenu();
		w.scene->removeItem(temporarySelectionItem);
		delete temporarySelectionItem;
	} else {
		ok = false;
		failures << "cannot open empty-space context menu without a scene and viewport";
	}

	if (w.scene && w.networkView) {
		const std::string stalePassengerId = "__e2e_stale_passenger__";
		auto* temporaryPassenger = new PassengerItem(w.m_presenter->pax_pixmap_scaled);
		temporaryPassenger->passengerId = stalePassengerId;
		temporaryPassenger->setPos(w.scene->itemsBoundingRect().bottomRight() + QPointF(100.0, 100.0));
		w.scene->addItem(temporaryPassenger);
		QMenu* menu = requestContextMenu(temporaryPassenger->sceneBoundingRect().center(), false);
		QAction* copy = findMenuAction(menu, "Copy passenger ID");
		const QString clipboardBefore = QStringLiteral("e2e-stale-sentinel");
		if (QApplication::clipboard())
			QApplication::clipboard()->setText(clipboardBefore);
		const bool infoVisibleBefore = w.m_inspector->dock() && w.m_inspector->dock()->isVisible();
		const QString infoTitleBefore = w.m_inspector->dock() ? w.m_inspector->dock()->windowTitle() : QString();
		w.scene->removeItem(temporaryPassenger);
		delete temporaryPassenger;
		if (copy)
			copy->trigger();
		QApplication::processEvents();
		if (QApplication::clipboard() && QApplication::clipboard()->text() != clipboardBefore) {
			ok = false;
			failures << "stale passenger context action changed the clipboard";
		}
		if ((w.m_inspector->dock() && w.m_inspector->dock()->isVisible()) != infoVisibleBefore
			|| (w.m_inspector->dock() && w.m_inspector->dock()->windowTitle() != infoTitleBefore)) {
			ok = false;
			failures << "stale passenger context action changed application state";
		}
		closeContextMenu();
	}

	if (w.m_followAction && w.m_followTrainCombo && w.m_followTrainCombo->count() > 0) {
		w.setFollowTrain(-1);
		if (std::any_of(w.m_presenter->m_stationOverlays.cbegin(), w.m_presenter->m_stationOverlays.cend(),
				[](const auto* overlay) { return overlay && overlay->isFollowed(); })) {
			ok = false;
			failures << "follow deactivation left a stale station priority";
		}

		TrainItemGroup* activeFollowTrain = selectedTrain;
		if (w.networkView && !w.m_presenter->allTrains.isEmpty()) {
			const QPointF viewCenter = w.networkView->sceneRect().center();
			qreal bestDistance = std::numeric_limits<qreal>::max();
			for (TrainItemGroup* candidate : w.m_presenter->allTrains) {
				if (!candidate || !candidate->isVisible())
					continue;
				const qreal distance = QLineF(candidate->sceneBoundingRect().center(), viewCenter).length();
				if (!activeFollowTrain || distance < bestDistance) {
					activeFollowTrain = candidate;
					bestDistance = distance;
				}
			}
		}
		if (activeFollowTrain && activeFollowTrain->trainPolygonItemList) {
			for (TrainBodyItem* body : *activeFollowTrain->trainPolygonItemList) {
				if (body && body->isVisible() && !body->polygon().isEmpty()) {
					selectedTrain = activeFollowTrain;
					selectedTrainBody = body;
					selectedTrainPen = body->pen();
					selectedTrainBrush = body->brush();
					break;
				}
			}
		}
		const int activeFollowIndex = selectedTrain ? selectedTrain->index : -1;
		if (activeFollowIndex < 0) {
			ok = false;
			failures << "no active train is available for paused follow activation";
		} else {
			if (w.m_runController->m_worker)
				w.m_runController->m_worker->requestPause();
			QObject::disconnect(&simulation, &DispatchController::snapshotAvailable,
				&w, &MainWindow::waitForUpdates);
			w.networkView->fitToTopology();
			w.networkView->zoomBy(24.0);
			QApplication::processEvents();
			w.setFollowTrain(activeFollowIndex);
			QApplication::processEvents();
			const TrainItemGroup* followed = w.resolveTrainItem(activeFollowIndex);
			// The view centres on the middle of the train along its track, which is off the middle of its bounds where
			// the train runs through a bend.
			const QPointF expectedCenter = followed ? w.m_presenter->trainCenter(activeFollowIndex) : QPointF();
			const QPointF expectedCameraCenter = clampedCameraCenter(expectedCenter);
			const QPointF cameraCenter = w.networkView->mapToScene(w.networkView->viewport()->rect().center());
			if (!w.m_followAction->isChecked() || w.m_followTrainIndex != activeFollowIndex
				|| !followed || QLineF(cameraCenter, expectedCameraCenter).length() > 10.0) {
				ok = false;
				failures << QString("paused follow activation did not center the selected train immediately "
									"(camera=%1,%2 target=%3,%4 expected=%5,%6 zoom=%7 fit=%8 bounds=%9,%10 %11x%12)")
								.arg(cameraCenter.x(), 0, 'f', 2)
								.arg(cameraCenter.y(), 0, 'f', 2)
								.arg(expectedCenter.x(), 0, 'f', 2)
								.arg(expectedCenter.y(), 0, 'f', 2)
								.arg(expectedCameraCenter.x(), 0, 'f', 2)
								.arg(expectedCameraCenter.y(), 0, 'f', 2)
								.arg(w.networkView->zoomRatio(), 0, 'f', 2)
								.arg(w.networkView->fittedScale(), 0, 'f', 6)
								.arg(w.networkView->sceneRect().left(), 0, 'f', 2)
								.arg(w.networkView->sceneRect().top(), 0, 'f', 2)
								.arg(w.networkView->sceneRect().width(), 0, 'f', 2)
								.arg(w.networkView->sceneRect().height(), 0, 'f', 2);
			}
			const int activeComboIndex = w.m_followTrainCombo->findData(activeFollowIndex);
			if (activeComboIndex < 0 || w.m_followTrainCombo->currentIndex() != activeComboIndex) {
				ok = false;
				failures << "paused follow activation did not synchronize the train selector";
			}

			w.m_followAction->setChecked(false);
			QApplication::processEvents();
			if (w.m_followAction->isChecked() || w.m_followTrainIndex != -1
				|| std::any_of(w.m_presenter->m_stationOverlays.cbegin(), w.m_presenter->m_stationOverlays.cend(),
					[](const auto* overlay) { return overlay && overlay->isFollowed(); })) {
				ok = false;
				failures << "follow disable did not clear the target and station priority";
			}

			int futureFollowIndex = -1;
			if (w.m_snapshot) {
				for (const GuiTrainState& state : w.m_snapshot->trains) {
					if (state.index != activeFollowIndex && state.departureTime > w.m_snapshot->timestep
						&& !w.resolveTrainItem(state.index)) {
						futureFollowIndex = state.index;
						break;
					}
				}
			}
			if (futureFollowIndex >= 0) {
				const int futureComboIndex = w.m_followTrainCombo->findData(futureFollowIndex);
				const QString futureName = w.m_followTrainCombo->itemData(futureComboIndex, kFollowNameRole).toString();
				const auto waitingState = std::find_if(w.m_snapshot->trains.cbegin(), w.m_snapshot->trains.cend(),
					[futureFollowIndex](const GuiTrainState& state) { return state.index == futureFollowIndex; });
				const int futureDeparture = waitingState->departureTime;
				const QString scheduled = QString::fromStdString(formatSimTime(futureDeparture, w.m_runController->m_startOffsetSeconds));
				const QString notEntered = futureName + " is scheduled to enter at " + scheduled + ". ";
				const QToolButton* followButton = w.findChild<QToolButton*>("actionFollowButton");
				// Choosing the train with Follow off says that Follow can be switched on now. The
				// list says that the time is the schedule. The train has no item yet, so there is nothing for the
				// view to move to: E2E_FOLLOW_REPLAY_BEFORE_OK checks that the view stays for a train whose item exists.
				w.m_followTrainCombo->setCurrentIndex(futureComboIndex);
				const bool offSentence = w.m_followStatusLabel->toolTip()
						== notEntered + "Follow can be switched on now and starts when it enters."
					&& w.m_followAction->isEnabled();
				w.m_followAction->setChecked(true);
				QApplication::processEvents();
				if (!w.m_followAction->isChecked() || w.m_followTrainIndex != futureFollowIndex
					|| w.statusBar()->currentMessage() != futureName + " enters at " + scheduled) {
					ok = false;
					failures << "future train follow activation was ignored or did not report waiting";
				}
				if (!offSentence || w.m_followStatusLabel->toolTip() != notEntered + "Follow starts when it enters."
					|| w.m_followStatusLabel->text() != futureName + " enters at " + scheduled
					|| w.m_followAction->toolTip() != w.m_followStatusLabel->toolTip()
					|| w.m_followTrainCombo->toolTip() != w.m_followStatusLabel->toolTip()
					|| w.m_followTrainCombo->accessibleDescription() != w.m_followStatusLabel->toolTip()
					|| !followButton || followButton->accessibleDescription() != w.m_followStatusLabel->toolTip()
					|| w.m_followTrainCombo->itemText(futureComboIndex) != futureName + " (scheduled " + scheduled + ")"
					|| w.m_followTrainCombo->currentIndex() != futureComboIndex
					|| w.m_followTrainCombo->itemData(futureComboIndex).toInt() != futureFollowIndex) {
					ok = false;
					failures << QString("the explanation of a train that has not entered is wrong (label=%1 list=%2)")
									.arg(w.m_followStatusLabel->text(), w.m_followTrainCombo->itemText(futureComboIndex));
				} else {
					std::fprintf(stdout, "E2E_FOLLOW_NOT_ENTERED_OK\n");
					std::fflush(stdout);
				}
				// The sentence does not change the width that the status bar asks for, so it cannot
				// raise the minimum width of the window.
				const QString sentence = w.m_followStatusLabel->text();
				QApplication::processEvents();
				const int barWithSentence = w.statusBar()->minimumSizeHint().width();
				w.m_followStatusLabel->clear();
				QApplication::processEvents();
				const int barWithout = w.statusBar()->minimumSizeHint().width();
				w.updateFollowAvailability();
				QApplication::processEvents();
				if (barWithSentence != barWithout || w.m_followStatusLabel->text() != sentence
					|| w.m_followStatusLabel->width() > kFollowStatusMaxWidth) {
					ok = false;
					failures << QString("the Follow sentence widens the status bar (%1 and %2)").arg(barWithSentence).arg(barWithout);
				} else {
					std::fprintf(stdout, "E2E_FOLLOW_STATUS_WIDTH_OK\n");
					std::fflush(stdout);
				}
				// At the narrowest window the label shows the whole clause, and the popup of the list is wide enough for the
				// whole entry, which is wider than the list.
				w.resize(1024, 720);
				QApplication::processEvents();
				const QString shownNarrow = w.m_followStatusLabel->displayText();
				w.m_followTrainCombo->showPopup();
				QApplication::processEvents();
				const int popupWidth = w.m_followTrainCombo->view()->width();
				w.m_followTrainCombo->hidePopup();
				const int entryWidth = w.m_followTrainCombo->view()->fontMetrics().horizontalAdvance(w.m_followTrainCombo->itemText(futureComboIndex));
				const int listWidth = w.m_followTrainCombo->width();
				// A message wider than the room left of the Follow label looks the same as its text elided to that room.
				const QString messageBefore = w.statusBar()->currentMessage();
				const QString longMessage = QStringLiteral("Simulation complete - results in the Diagrams menu. ").repeated(4);
				w.statusBar()->showMessage(longMessage);
				const QImage cutMessage = w.statusBar()->grab().toImage();
				w.statusBar()->showMessage(w.statusBar()->fontMetrics().elidedText(longMessage, Qt::ElideRight, w.m_followStatusLabel->x() - 8));
				if (w.statusBar()->grab().toImage() != cutMessage) {
					ok = false;
					failures << "a status message that does not fit is cut off without an ellipsis";
				}
				w.statusBar()->showMessage(messageBefore);
				w.resize(1200, 800);
				QApplication::processEvents();
				if (shownNarrow != futureName + " enters at " + scheduled || entryWidth <= listWidth
					|| popupWidth < entryWidth) {
					ok = false;
					failures << QString("the Follow label or list hides the scheduled time (label=%1 popup=%2 entry=%3 list=%4)")
									.arg(shownNarrow)
									.arg(popupWidth)
									.arg(entryWidth)
									.arg(listWidth);
				} else {
					std::fprintf(stdout, "E2E_FOLLOW_VISIBLE_OK\n");
					std::fflush(stdout);
				}
				// A change of the start time changes the clock times in the list as well as in the sentence.
				const long long offsetBefore = w.m_runController->m_startOffsetSeconds;
				w.setStartOffset(offsetBefore + 3600);
				const QString later = QString::fromStdString(formatSimTime(futureDeparture, w.m_runController->m_startOffsetSeconds));
				const bool laterShown = later != scheduled && w.m_followTrainCombo->itemText(futureComboIndex) == futureName + " (scheduled " + later + ")"
					&& w.m_followStatusLabel->toolTip() == futureName + " is scheduled to enter at " + later + ". Follow starts when it enters.";
				w.setStartOffset(offsetBefore);
				if (!laterShown || w.m_followTrainCombo->itemText(futureComboIndex) != futureName + " (scheduled " + scheduled + ")") {
					ok = false;
					failures << "a change of the start time left the scheduled time of a train in the list unchanged";
				} else {
					std::fprintf(stdout, "E2E_FOLLOW_CLOCK_OK\n");
					std::fflush(stdout);
				}

				const auto waitingSnapshot = w.m_snapshot;
				auto skippedSnapshot = std::make_shared<GuiSimulationSnapshot>(*waitingSnapshot);
				for (GuiTrainState& state : skippedSnapshot->trains) {
					if (state.index == futureFollowIndex) {
						state.outOfSimulation = true;
						state.routeAxisPosition = -9999;
					}
				}
				w.m_snapshot = skippedSnapshot;
				updateTrainPosition(w, skippedSnapshot->timestep);
				if (w.m_followTrainIndex != -1 || w.m_followAction->isChecked()) {
					ok = false;
					failures << "skipped train exit did not clear armed follow";
				}
				w.setFollowTrain(futureFollowIndex);
				if (w.m_followTrainIndex != -1 || w.m_followAction->isChecked()
					|| w.statusBar()->currentMessage() != futureName + " has left the network") {
					ok = false;
					failures << "exited train without graphics could be followed";
				}
				w.m_snapshot = waitingSnapshot;
				w.setFollowTrain(futureFollowIndex);

				GuiTrainState futureEntryState;
				bool haveFutureEntry = false;
				int futureEntryTime = -1;
				const GuiTrainState* entryGeometry = nullptr;
				for (const GuiTrainState& state : w.m_snapshot->trains) {
					if (state.index == futureFollowIndex)
						futureEntryState = state;
					if (state.index == activeFollowIndex)
						entryGeometry = &state;
				}
				if (entryGeometry && entryGeometry->routeIndex >= 0
					&& entryGeometry->routeAxisPosition != -9999
					&& !entryGeometry->wagonHeadPositions.empty()
					&& entryGeometry->wagonHeadPositions.size() == entryGeometry->wagonTailPositions.size()) {
					// The worker may not have simulated the future departure yet. Reuse
					// the paused active geometry to exercise the same entry path without
					// depending on worker timing or an unfilled future trajectory.
					futureEntryState.routeIndex = entryGeometry->routeIndex;
					futureEntryState.reversedDirection = entryGeometry->reversedDirection;
					futureEntryState.wagonCount = entryGeometry->wagonCount;
					futureEntryState.length = entryGeometry->length;
					futureEntryState.routeAxisPosition = entryGeometry->routeAxisPosition;
					futureEntryState.wagonHeadPositions = entryGeometry->wagonHeadPositions;
					futureEntryState.wagonTailPositions = entryGeometry->wagonTailPositions;
					futureEntryState.outOfSimulation = false;
					futureEntryTime = qMax(futureEntryState.departureTime, w.m_snapshot->timestep + 1);
					haveFutureEntry = true;
				}
				if (!haveFutureEntry) {
					ok = false;
					failures << "future train had no renderable departure snapshot";
				} else {
					auto entrySnapshot = std::make_shared<GuiSimulationSnapshot>(*w.m_snapshot);
					entrySnapshot->timestep = futureEntryTime;
					for (GuiTrainState& state : entrySnapshot->trains) {
						if (state.index == futureFollowIndex) {
							state = futureEntryState;
							break;
						}
					}
					w.networkView->centerOn(w.networkView->sceneRect().topLeft());
					const QPointF cameraBeforeEntry = w.networkView->mapToScene(
						w.networkView->viewport()->rect().center());
					w.m_snapshot = entrySnapshot;
					updateTrainPosition(w, futureEntryTime);
					enteredTrainIndex = futureFollowIndex;
					QApplication::processEvents();
					TrainItemGroup* enteredTrain = w.resolveTrainItem(futureFollowIndex);
					const QPointF enteredCenter = enteredTrain ? w.m_presenter->trainCenter(futureFollowIndex) : QPointF();
					const QPointF enteredCameraCenter = w.networkView->mapToScene(
						w.networkView->viewport()->rect().center());
					if (QLineF(cameraBeforeEntry, clampedCameraCenter(enteredCenter)).length() <= 20.0
						|| !enteredTrain || !enteredTrain->isVisible()
						|| !w.m_followAction->isChecked() || w.m_followTrainIndex != futureFollowIndex
						|| QLineF(enteredCameraCenter, clampedCameraCenter(enteredCenter)).length() > 10.0) {
						ok = false;
						failures << "future train entry did not create and center the armed follow target";
					}
					// Entered: the sentence and the list entry say that the train runs and the view follows it.
					const auto viewCentre = [&w]() { return w.networkView->mapToScene(w.networkView->viewport()->rect().center()); };
					const QString running = futureName + " is running. The view follows it.";
					if (w.m_followStatusLabel->toolTip() != running || w.m_followTrainCombo->itemText(futureComboIndex) != futureName + " (running)"
						|| w.m_followTrainCombo->currentIndex() != futureComboIndex || w.m_followTrainIndex != futureFollowIndex) {
						ok = false;
						failures << QString("the explanation of a train that has entered is wrong (label=%1 list=%2)")
										.arg(w.m_followStatusLabel->text(), w.m_followTrainCombo->itemText(futureComboIndex));
					} else {
						std::fprintf(stdout, "E2E_FOLLOW_ENTERED_OK\n");
						std::fflush(stdout);
					}
					// The toolbar button switches Follow off: the sentence stops saying that the view follows the train.
					// Switching it on again restores the sentence.
					if (QToolButton* toggleButton = w.findChild<QToolButton*>("actionFollowButton")) {
						const QString switchedOff = futureName + " is running. Follow can be switched on now.";
						toggleButton->click();
						QApplication::processEvents();
						const bool offShown = !w.m_followAction->isChecked() && w.m_followTrainIndex == -1 && !toggleButton->isChecked()
							&& w.m_followStatusLabel->toolTip() == switchedOff && w.m_followAction->toolTip() == switchedOff
							&& toggleButton->accessibleDescription() == switchedOff;
						toggleButton->click();
						QApplication::processEvents();
						if (!offShown || !w.m_followAction->isChecked() || w.m_followTrainIndex != futureFollowIndex
							|| w.m_followStatusLabel->toolTip() != running) {
							ok = false;
							failures << QString("the Follow sentence is not updated by the toolbar button (off shown=%1 label=%2)")
											.arg(offShown)
											.arg(w.m_followStatusLabel->text());
						}
					}
					// A frame that changes no state leaves the list alone, so that an open list does not flicker.
					const FollowListUnderFrames listUnderFrames = showFramesOverMarkedList(w.m_followTrainCombo, [&w, futureEntryTime]() {
						updateTrainPosition(w, futureEntryTime);
						updateTrainPosition(w, futureEntryTime);
					});
					w.setStartOffset(w.m_runController->m_startOffsetSeconds); // writes every row again
					if (listUnderFrames.changes != 0 || !listUnderFrames.rowsKept) {
						ok = false;
						failures << QString("a frame without a change of state wrote the list (%1 changes, rows kept: %2)")
										.arg(listUnderFrames.changes)
										.arg(listUnderFrames.rowsKept);
					} else {
						std::fprintf(stdout, "E2E_FOLLOW_LIST_STABLE_OK\n");
						std::fflush(stdout);
					}
					// Trains layer off: the sentence and the list entry say so, the station emphasis goes, and the view
					// stays where the user put it, also when the next frame arrives. On again: the view returns to the train.
					const auto stationEmphasised = [&w]() {
						return std::any_of(w.m_presenter->m_stationOverlays.cbegin(), w.m_presenter->m_stationOverlays.cend(),
							[](const StationOverlayItem* overlay) { return overlay && overlay->isFollowed(); });
					};
					const bool emphasisBefore = stationEmphasised();
					w.m_trainLayerCheck->setChecked(false);
					QApplication::processEvents();
					const bool emphasisGone = emphasisBefore && !stationEmphasised();
					w.networkView->centerOn(w.networkView->sceneRect().topLeft());
					const QPointF cameraAway = viewCentre();
					updateTrainPosition(w, futureEntryTime);
					const QPointF cameraHidden = viewCentre();
					const bool hiddenShown = w.m_followStatusLabel->toolTip()
							== futureName + " is running, but the Trains layer is switched off, so the view does not follow it."
						&& followButton && followButton->accessibleDescription() == w.m_followStatusLabel->toolTip()
						&& w.m_followTrainCombo->itemText(futureComboIndex) == futureName + " (hidden)"
						&& w.m_followAction->isChecked() && w.m_followTrainIndex == futureFollowIndex
						&& QLineF(cameraAway, cameraHidden).length() <= 1.0 && emphasisGone && !stationEmphasised();
					w.m_trainLayerCheck->setChecked(true);
					QApplication::processEvents();
					if (!hiddenShown || w.m_followStatusLabel->toolTip() != running || followButton->accessibleDescription() != running
						|| !stationEmphasised() || w.m_followTrainCombo->itemText(futureComboIndex) != futureName + " (running)"
						|| QLineF(viewCentre(), clampedCameraCenter(enteredCenter)).length() > 10.0) {
						ok = false;
						failures << QString("the Trains layer did not stop and resume Follow (hidden=%1 label=%2)")
										.arg(hiddenShown)
										.arg(w.m_followStatusLabel->text());
					} else {
						std::fprintf(stdout, "E2E_FOLLOW_LAYER_OK\n");
						std::fflush(stdout);
					}
					// Keep the synthetic entry separate from the active fixture so the
					// later exact viewport reselection cannot hit two train bodies.
					if (enteredTrain) {
						const QPointF separation(1000.0, 1000.0);
						enteredTrain->setPos(enteredTrain->pos() + separation);
						if (TrainBadgeItem* badge = w.m_presenter->m_trainBadges.value(futureFollowIndex, nullptr))
							badge->setPos(badge->pos() + separation);
					}
				}
			} else {
				ok = false;
				failures << "no future train was available for follow waiting regression";
			}

			w.setFollowTrain(activeFollowIndex);
			QApplication::processEvents();
			if (!w.m_followAction->isChecked() || w.m_followTrainIndex != activeFollowIndex) {
				ok = false;
				failures << "switching back to an active follow target retargeted incorrectly";
			}
		}
	} else {
		ok = false;
		failures << "follow mode controls disappeared";
	}
	if (w.networkView) {
		w.networkView->fitToTopology();
		w.updateViewportOverlays();
		QApplication::processEvents();
		TrainBadgeItem* followedBadge = w.m_presenter->m_trainBadges.value(w.m_followTrainIndex, nullptr);
		const bool ordinaryMarkers = std::all_of(w.m_presenter->m_trainBadges.cbegin(), w.m_presenter->m_trainBadges.cend(),
			[](const TrainBadgeItem* badge) {
				return !badge || badge->isPromoted()
					|| badge->presentation() == TrainBadgeItem::Presentation::Overview;
			});
		if (!followedBadge || !followedBadge->isPromoted()
			|| followedBadge->presentation() != TrainBadgeItem::Presentation::Detailed
			|| !ordinaryMarkers) {
			ok = false;
			failures << "followed train was not promoted over Fit markers";
		}
	}
	captureScreenshot("QEGTRAIN_E2E_FOLLOW_SCREENSHOT", "follow");
	if (w.networkView && w.m_followAction && w.m_followAction->isChecked()) {
		w.networkView->zoomBy(24.0);
		w.setFollowTrain(w.m_followTrainIndex);
	}
	if (w.scene && w.networkView && selectedTrain && selectedTrainBody && w.m_snapshot) {
		const auto firstSnapshot = w.m_snapshot;
		auto movedSnapshot = std::make_shared<GuiSimulationSnapshot>(*firstSnapshot);
		auto interruptedSnapshot = std::make_shared<GuiSimulationSnapshot>(*firstSnapshot);
		GuiTrainState* movedState = nullptr;
		GuiTrainState* interruptedState = nullptr;
		for (GuiTrainState& state : movedSnapshot->trains) {
			if (state.index == selectedTrain->index) {
				movedState = &state;
				break;
			}
		}
		for (GuiTrainState& state : interruptedSnapshot->trains) {
			if (state.index == selectedTrain->index) {
				interruptedState = &state;
				break;
			}
		}
		if (!movedState || !interruptedState || movedState->routeIndex < 0
			|| movedState->routeIndex >= static_cast<int>(simulation.run().infra.routes.size())
			|| movedState->routeAxisPosition == -9999
			|| movedState->wagonHeadPositions.empty()
			|| movedState->wagonHeadPositions.size() != movedState->wagonTailPositions.size()) {
			ok = false;
			failures << "follow animation regression lacks a complete active train snapshot";
		} else {
			const GuiTrainState* baseState = nullptr;
			for (const GuiTrainState& state : firstSnapshot->trains) {
				if (state.index == selectedTrain->index) {
					baseState = &state;
					break;
				}
			}
			const auto shiftState = [](GuiTrainState& state, double delta) {
				state.routeAxisPosition += delta;
				const double geoDelta = (state.reversedDirection ? -delta : delta) / 1000.0;
				for (double& position : state.wagonHeadPositions)
					position += geoDelta;
				for (double& position : state.wagonTailPositions)
					position += geoDelta;
			};
			const auto hasRenderableGeometry = [&w, selectedTrain](const GuiTrainState& state) {
				if (!selectedTrain->trainPolygonItemList)
					return false;
				return w.m_presenter->getTrainPolygonItemList(selectedTrain->trainPolygonItemList, state);
			};
			double step = 0.0;
			if (baseState) {
				for (double magnitude = 1000.0; magnitude >= 0.5 && step == 0.0; magnitude *= 0.5) {
					for (const double signedStep : {magnitude, -magnitude}) {
						GuiTrainState candidate = *baseState;
						shiftState(candidate, signedStep);
						GuiTrainState secondCandidate = *baseState;
						shiftState(secondCandidate, signedStep * 2.0);
						if (!hasRenderableGeometry(candidate) || !hasRenderableGeometry(secondCandidate))
							continue;
						*movedState = std::move(candidate);
						*interruptedState = std::move(secondCandidate);
						step = signedStep;
						break;
					}
				}
				// Candidate probing changes the displayed polygons; restore the base
				// state before delivering the snapshots through the real update path.
				hasRenderableGeometry(*baseState);
			}

			if (step == 0.0) {
				ok = false;
				failures << "follow animation regression could not create two in-route positions";
			} else {
				movedSnapshot->timestep = firstSnapshot->timestep + 1;
				interruptedSnapshot->timestep = firstSnapshot->timestep + 2;

				w.m_snapshot = firstSnapshot;
				updateTrainPosition(w, firstSnapshot->timestep);

				// The camera and the motion of the trains run on a clock of the check, which only the check advances. The
				// steps of the trains are run by hand, and the new clock first puts every train at its last place.
				const auto cameraClock = std::make_shared<qint64>(0);
				w.m_followCamera->setClock([cameraClock]() { return *cameraClock; });
				w.m_presenter->setClock([cameraClock]() { return *cameraClock; });
				w.m_presenter->stepTrains();
				const QPointF initialBodyCenter = selectedTrainBody->sceneBoundingRect().center();
				const auto tickCamera = [&w, cameraClock]() {
					*cameraClock += FollowCamera::tickIntervalMs;
					w.m_followCamera->tick();
				};
				// Runs the ticks of a catch-up until it ends.
				const auto finishCatchUp = [&w, &tickCamera]() {
					for (int ticks = 0; w.m_followCamera->running() && ticks < 200; ++ticks)
						tickCamera();
				};
				// Runs the steps of the trains for ms milliseconds, one step every 15 ms.
				const auto stepTrainsFor = [&w, cameraClock](int ms) {
					for (int elapsed = 15; elapsed <= ms; elapsed += 15) {
						*cameraClock += 15;
						w.m_presenter->stepTrains();
					}
				};
				const auto viewCentre = [&w]() { return w.networkView->mapToScene(w.networkView->viewport()->rect().center()); };
				const auto pixelsBetween = [&w](const QPointF& from, const QPointF& to) {
					return QLineF(w.networkView->mapFromScene(from), w.networkView->mapFromScene(to)).length();
				};
				// The view is centred on the train to a fraction of a pixel, and to the pixel where the edge of the scene holds it.
				constexpr qreal onTrainPixels = 1.5;
				// How far the view is from the train as it is drawn now, in logical pixels.
				const auto offTrain = [&]() {
					return pixelsBetween(viewCentre(), clampedCameraCenter(w.m_presenter->trainCenter(selectedTrain->index)));
				};
				w.setFollowTrain(selectedTrain->index);
				const QPointF cameraBefore = viewCentre();

				// A live frame starts the glide of the train where it is drawn, so the view does not move yet. Before the
				// update interval is known a glide lasts 120 ms. At every step of the glide the view is on the train as it
				// is drawn, in the same frame, and no timer of the camera runs. The steps go half way, so that the next
				// frame comes during the glide, and the view moves on in both quarters. The drawn line can bend or cross to
				// another track on the way, so the distances in the view need not grow at an even rate.
				w.m_snapshot = movedSnapshot;
				updateTrainPosition(w, movedSnapshot->timestep);
				const QPointF cameraAfterFrame = viewCentre();
				const bool glideStarted = w.m_presenter->trainMoving(selectedTrain->index);
				if (!glideStarted) {
					ok = false;
					failures << "first follow delivery did not start a glide";
				}
				qreal largestOff = offTrain();
				bool timerRan = w.m_followCamera->running();
				qreal quarterWay = 0.0;
				for (int part = 1; part <= 4; ++part) {
					stepTrainsFor(15);
					largestOff = std::max(largestOff, offTrain());
					timerRan = timerRan || w.m_followCamera->running();
					if (part == 2)
						quarterWay = pixelsBetween(cameraBefore, viewCentre());
				}
				const qreal halfWay = pixelsBetween(cameraBefore, viewCentre());
				const bool glided = glideStarted && halfWay > 10.0 && pixelsBetween(cameraBefore, cameraAfterFrame) <= 1.0
					&& largestOff <= onTrainPixels && !timerRan && quarterWay > 1.0 && halfWay > quarterWay + 1.0;
				if (!glided) {
					ok = false;
					failures << QString("the view did not move with the train at every step of its glide (afterFrame=%1px "
										"largest off the train=%2px quarter way=%3px half way=%4px timer ran=%5)")
									.arg(pixelsBetween(cameraBefore, cameraAfterFrame), 0, 'f', 1)
									.arg(largestOff, 0, 'f', 2)
									.arg(quarterWay, 0, 'f', 1)
									.arg(halfWay, 0, 'f', 1)
									.arg(timerRan);
				} else {
					std::fprintf(stdout, "E2E_FOLLOW_CAMERA_GLIDE_OK\n");
					std::fflush(stdout);
				}

				// A frame that comes during the glide starts the next glide where the train is drawn, so the view does not
				// jump. The badge stays at the centre of the drawn bodies, which change their shape wagon by wagon in a bend.
				const QPointF cameraPaused = viewCentre();
				w.m_snapshot = interruptedSnapshot;
				updateTrainPosition(w, interruptedSnapshot->timestep);
				const QPointF cameraResumed = viewCentre();
				if (!w.m_presenter->trainMoving(selectedTrain->index)) {
					ok = false;
					failures << "second follow delivery did not go on with the glide";
				}
				TrainBadgeItem* interruptedBadge = w.m_presenter->m_trainBadges.value(selectedTrain->index, nullptr);
				const qreal interruptedAnchorError = interruptedBadge
					? QLineF(interruptedBadge->scenePos(), selectedTrain->sceneBoundingRect().center()).length()
					: std::numeric_limits<qreal>::max();
				const qreal interruptedCameraOff = offTrain();
				if (pixelsBetween(cameraPaused, cameraResumed) > 1.0 || interruptedCameraOff > onTrainPixels || w.m_followCamera->running()) {
					ok = false;
					failures << QString("a frame during the glide moved the view away from the drawn train (moved=%1px off=%2px running=%3)")
									.arg(pixelsBetween(cameraPaused, cameraResumed), 0, 'f', 2)
									.arg(interruptedCameraOff, 0, 'f', 2)
									.arg(w.m_followCamera->running());
				} else {
					std::fprintf(stdout, "E2E_FOLLOW_CAMERA_RESUME_OK\n");
					std::fflush(stdout);
				}
				// The view goes with the train to its new place, where the glide of 90 ms, one and a half times the interval
				// of 60 ms between the two frames, ends. The head of the train then has the shape of the frame.
				qreal largestOffToEnd = 0.0;
				for (int part = 1; part <= 6; ++part) {
					stepTrainsFor(15);
					largestOffToEnd = std::max(largestOffToEnd, offTrain());
				}
				QPolygonF interruptedHead;
				for (const GuiTrainState& state : interruptedSnapshot->trains)
					if (state.index == selectedTrain->index)
						w.m_presenter->getTrainPolygon(&interruptedHead, 0, state);
				const QPolygonF finalHead = selectedTrain->trainPolygonItemList->at(0)->polygon();
				qreal headApart = interruptedHead.size() == finalHead.size() && !finalHead.isEmpty() ? 0.0 : std::numeric_limits<qreal>::max();
				for (int corner = 0; headApart == 0.0 && corner < finalHead.size(); ++corner)
					headApart = std::max(headApart, QLineF(finalHead[corner], interruptedHead[corner]).length());
				if (w.m_presenter->trainMoving(selectedTrain->index) || headApart > 0.01) {
					ok = false;
					failures << QString("follow glide did not end at the place of the frame (moving=%1 head apart=%2)")
									.arg(w.m_presenter->trainMoving(selectedTrain->index))
									.arg(headApart, 0, 'f', 3);
				}
				const QPointF finalBodyCenter = selectedTrainBody->sceneBoundingRect().center();
				TrainBadgeItem* finalBadge = w.m_presenter->m_trainBadges.value(selectedTrain->index, nullptr);
				const QPointF finalCameraCenter = w.networkView->mapToScene(
					w.networkView->viewport()->rect().center());
				const QPointF finalGroupCenter = selectedTrain->sceneBoundingRect().center();
				const qreal bodyMovement = QLineF(initialBodyCenter, finalBodyCenter).length();
				const qreal badgeOff = std::max(interruptedAnchorError,
					finalBadge ? QLineF(finalBadge->scenePos(), finalGroupCenter).length() : std::numeric_limits<qreal>::max());
				if (!interruptedBadge || !finalBadge || bodyMovement <= 1.0 || badgeOff > 1.0 || largestOffToEnd > onTrainPixels
					|| pixelsBetween(finalCameraCenter, clampedCameraCenter(w.m_presenter->trainCenter(selectedTrain->index))) > onTrainPixels) {
					ok = false;
					failures << QString("follow glide lost body movement, the badge on the train, or camera center "
										"(movement=%1 badge off=%2 largest off=%3px finalCamera=%4,%5 group=%6,%7)")
									.arg(bodyMovement, 0, 'f', 2)
									.arg(badgeOff, 0, 'f', 2)
									.arg(largestOffToEnd, 0, 'f', 2)
									.arg(finalCameraCenter.x(), 0, 'f', 2)
									.arg(finalCameraCenter.y(), 0, 'f', 2)
									.arg(finalGroupCenter.x(), 0, 'f', 2)
									.arg(finalGroupCenter.y(), 0, 'f', 2);
				}

				// A frame that moves the view with the followed train does not run the whole pass over the overlays: a pan
				// changes neither the overlays nor their names, so they are refreshed only when the station of the train
				// changes. A name that is hidden by hand stays hidden over a frame in which the train keeps its station.
				w.updateViewportOverlays();
				StationOverlayItem* namedStation = nullptr;
				for (StationOverlayItem* overlay : w.m_presenter->m_stationOverlays)
					if (!namedStation && overlay && overlay->nameItem() && overlay->nameItem()->isVisible())
						namedStation = overlay;
				if (namedStation)
					namedStation->nameItem()->setVisible(false);
				updateTrainPosition(w, interruptedSnapshot->timestep);
				const bool overlaysLeft = namedStation && !namedStation->nameItem()->isVisible() && offTrain() <= onTrainPixels
					&& !w.followedStationChanged();
				w.updateViewportOverlays();
				if (!overlaysLeft || !namedStation->nameItem()->isVisible()) {
					ok = false;
					failures << QString("a frame of the followed train ran the whole pass over the overlays (named station=%1)")
									.arg(namedStation ? namedStation->stationName() : QStringLiteral("none"));
				} else {
					std::fprintf(stdout, "E2E_FOLLOW_CAMERA_OVERLAYS_OK\n");
					std::fflush(stdout);
				}

				// A pan of the user and a resize of the window do not switch Follow off. The next frame leaves the view where
				// the user left it, and a catch-up brings the view back to the train, also while the train stands.
				panViewAway(w.networkView);
				const QPointF cameraPannedAway = viewCentre();
				w.m_snapshot = movedSnapshot;
				updateTrainPosition(w, movedSnapshot->timestep);
				const qreal panDistance = offTrain();
				const bool panKept = panDistance > 20.0 && pixelsBetween(cameraPannedAway, viewCentre()) <= 1.0 && w.m_followCamera->running();
				// The check measures in whole pixels and a catch-up eases in, so its first tick can move the view by less
				// than one; the check looks after four ticks.
				for (int tick = 0; tick < 4; ++tick)
					tickCamera();
				const qreal panDistanceAfterTick = offTrain();
				finishCatchUp();
				const bool panGlided = panKept && panDistanceAfterTick < panDistance && !w.m_followCamera->running() && offTrain() <= onTrainPixels;
				w.m_snapshot = interruptedSnapshot;
				updateTrainPosition(w, interruptedSnapshot->timestep);
				const QSize viewportBeforeResize = w.networkView->viewport()->size();
				w.resize(1100, 760);
				QApplication::processEvents();
				const bool viewportResized = w.networkView->viewport()->size() != viewportBeforeResize;
				w.m_snapshot = movedSnapshot;
				updateTrainPosition(w, movedSnapshot->timestep);
				const bool catchUpAfterResize = w.m_followCamera->running();
				finishCatchUp();
				const bool resizeGlided = viewportResized && catchUpAfterResize && offTrain() <= onTrainPixels;
				w.resize(1200, 800);
				QApplication::processEvents();
				if (!panGlided || !resizeGlided) {
					ok = false;
					failures << QString("a pan or a resize while following did not lead to a catch-up to the train "
										"(pan: %1px then %2px, caught up=%3, resize: viewport changed=%4, running=%5, ends on the train=%6)")
									.arg(panDistance, 0, 'f', 1)
									.arg(panDistanceAfterTick, 0, 'f', 1)
									.arg(panGlided)
									.arg(viewportResized)
									.arg(catchUpAfterResize)
									.arg(resizeGlided);
				} else {
					std::fprintf(stdout, "E2E_FOLLOW_CAMERA_PAN_OK\n");
					std::fflush(stdout);
				}

				// A zoom of the toolbar, in or out, centres the view on the train at once, also during a catch-up after a pan.
				// Fit shows the whole network and ends the catch-up.
				panViewAway(w.networkView);
				w.m_snapshot = interruptedSnapshot;
				updateTrainPosition(w, interruptedSnapshot->timestep);
				tickCamera();
				const qreal lagBeforeZoomIn = offTrain();
				w.zoomIn();
				const qreal offAfterZoomIn = offTrain();
				const bool zoomInCentred = lagBeforeZoomIn > 20.0 && offAfterZoomIn <= onTrainPixels && !w.m_followCamera->running();
				panViewAway(w.networkView);
				w.m_snapshot = movedSnapshot;
				updateTrainPosition(w, movedSnapshot->timestep);
				tickCamera();
				const qreal lagBeforeZoomOut = offTrain();
				w.zoomOut();
				const qreal offAfterZoomOut = offTrain();
				const bool zoomOutCentred = lagBeforeZoomOut > 20.0 && offAfterZoomOut <= onTrainPixels && !w.m_followCamera->running();
				panViewAway(w.networkView);
				w.m_snapshot = interruptedSnapshot;
				updateTrainPosition(w, interruptedSnapshot->timestep);
				tickCamera();
				const bool catchingUpBeforeFit = w.m_followCamera->running();
				w.fitView();
				const bool fitStopped = catchingUpBeforeFit && !w.m_followCamera->running();
				if (!zoomInCentred || !zoomOutCentred || !fitStopped) {
					ok = false;
					failures << QString("the toolbar zoom did not centre the view on the followed train, or Fit did not end the catch-up "
										"(in: lag=%1px after=%2px, out: lag=%3px after=%4px, running before Fit=%5 after Fit=%6)")
									.arg(lagBeforeZoomIn, 0, 'f', 1)
									.arg(offAfterZoomIn, 0, 'f', 1)
									.arg(lagBeforeZoomOut, 0, 'f', 1)
									.arg(offAfterZoomOut, 0, 'f', 1)
									.arg(catchingUpBeforeFit)
									.arg(w.m_followCamera->running());
				} else {
					std::fprintf(stdout, "E2E_FOLLOW_CAMERA_ZOOM_OK\n");
					std::fflush(stdout);
				}
				// Fit left no scroll range. Zoom in again so that the view can move.
				w.networkView->zoomBy(6.0);
				w.setFollowTrain(selectedTrain->index);

				// A change of the followed train cuts to the new train and does not sweep across the map. The other train
				// is the one of the entry check, put where the followed train was at first, so that a glide could reach it.
				auto cutSnapshot = std::make_shared<GuiSimulationSnapshot>(*movedSnapshot);
				for (GuiTrainState& state : cutSnapshot->trains) {
					if (state.index != enteredTrainIndex)
						continue;
					state.routeIndex = baseState->routeIndex;
					state.reversedDirection = baseState->reversedDirection;
					state.wagonCount = baseState->wagonCount;
					state.length = baseState->length;
					state.routeAxisPosition = baseState->routeAxisPosition;
					state.wagonHeadPositions = baseState->wagonHeadPositions;
					state.wagonTailPositions = baseState->wagonTailPositions;
					state.outOfSimulation = false;
				}
				w.m_snapshot = cutSnapshot;
				updateTrainPosition(w, cutSnapshot->timestep);
				TrainItemGroup* otherTrain = w.resolveTrainItem(enteredTrainIndex);
				if (otherTrain) {
					w.m_presenter->stopTrainAnimation(otherTrain->index);
					otherTrain->setPos(QPointF(0.0, 0.0));
				}
				tickCamera();
				const QPointF cameraBeforeCut = viewCentre();
				if (otherTrain)
					w.setFollowTrain(otherTrain->index);
				const bool cutToOther = otherTrain && w.m_followTrainIndex == otherTrain->index && !w.m_followCamera->running()
					&& pixelsBetween(viewCentre(), clampedCameraCenter(w.m_presenter->trainCenter(otherTrain->index))) <= 3.0
					&& pixelsBetween(cameraBeforeCut, viewCentre()) > 20.0;
				// Zoom in further so that the next checks see a move of the view.
				w.networkView->zoomBy(4.0);
				w.setFollowTrain(selectedTrain->index);

				// Switching Follow off, and the end of the followed train, stop the camera, also in a catch-up, and leave the
				// view where it is. Switching it on again with the view away from the train moves the view to the train at once.
				panViewAway(w.networkView);
				w.m_snapshot = movedSnapshot;
				updateTrainPosition(w, movedSnapshot->timestep);
				tickCamera();
				const bool catchingUpBeforeOff = w.m_followCamera->running();
				const QPointF cameraBeforeOff = viewCentre();
				w.m_followAction->setChecked(false);
				tickCamera();
				const bool offStopped = catchingUpBeforeOff && !w.m_followCamera->running() && !w.m_followCamera->hasTarget()
					&& pixelsBetween(cameraBeforeOff, viewCentre()) <= 0.5;
				panViewAway(w.networkView);
				const QPointF cameraPanned = viewCentre();
				w.setFollowTrain(selectedTrain->index);
				const bool cutOnSwitchOn = !w.m_followCamera->running() && pixelsBetween(cameraPanned, viewCentre()) > 20.0
					&& offTrain() <= 3.0;
				if (!cutToOther || !cutOnSwitchOn) {
					ok = false;
					failures << QString("the view did not move to the train at once (other train: %1, switching Follow on: %2, running=%3)")
									.arg(cutToOther)
									.arg(cutOnSwitchOn)
									.arg(w.m_followCamera->running());
				} else {
					std::fprintf(stdout, "E2E_FOLLOW_CAMERA_CUT_OK\n");
					std::fflush(stdout);
				}

				// Switching the Trains layer off, and a train with no position on the map, stop the camera. With the layer
				// on again, and when the train has its position back, the view moves to the train at once.
				w.m_snapshot = movedSnapshot;
				updateTrainPosition(w, movedSnapshot->timestep);
				const bool followingBeforeLayer = w.m_followCamera->hasTarget();
				w.m_trainLayerCheck->setChecked(false);
				const bool layerStopped = followingBeforeLayer && !w.m_followCamera->running() && !w.m_followCamera->hasTarget();
				w.m_trainLayerCheck->setChecked(true);
				const bool layerCut = w.m_followCamera->hasTarget() && !w.m_followCamera->running()
					&& offTrain() <= 3.0;
				updateTrainPosition(w, movedSnapshot->timestep);
				const bool followingBeforeBlind = w.m_followCamera->hasTarget();
				auto blindSnapshot = std::make_shared<GuiSimulationSnapshot>(*movedSnapshot);
				for (GuiTrainState& state : blindSnapshot->trains)
					if (state.index == selectedTrain->index)
						state.routeIndex = -1;
				w.m_snapshot = blindSnapshot;
				updateTrainPosition(w, blindSnapshot->timestep);
				const bool blindStopped = followingBeforeBlind && !w.m_followCamera->running() && !w.m_followCamera->hasTarget()
					&& w.m_followAction->isChecked() && w.m_followTrainIndex == selectedTrain->index;
				w.m_snapshot = movedSnapshot;
				updateTrainPosition(w, movedSnapshot->timestep);
				const bool blindCut = w.m_followCamera->hasTarget() && !w.m_followCamera->running() && offTrain() <= 3.0;
				if (!layerStopped || !layerCut || !blindStopped || !blindCut) {
					ok = false;
					failures << QString("the Trains layer or a train with no position did not stop the camera, or the view did not cut back to the train "
										"(layer: following before=%1 stopped=%2 cut back=%3, no position: following before=%4 stopped=%5 cut back=%6)")
									.arg(followingBeforeLayer)
									.arg(layerStopped)
									.arg(layerCut)
									.arg(followingBeforeBlind)
									.arg(blindStopped)
									.arg(blindCut);
				}
				w.m_snapshot = interruptedSnapshot;
				updateTrainPosition(w, interruptedSnapshot->timestep);
				const bool followingBeforeEnd = w.m_followCamera->hasTarget();
				if (!offStopped || !followingBeforeEnd) {
					ok = false;
					failures << QString("switching Follow off did not stop the camera (catching up before=%1, stopped=%2, following before the end=%3)")
									.arg(catchingUpBeforeOff)
									.arg(offStopped)
									.arg(followingBeforeEnd);
				}

				// Exiting the followed train disables Follow rather than retaining a
				// stale target, and the target can be restored without retargeting.
				auto exitedSnapshot = std::make_shared<GuiSimulationSnapshot>(*interruptedSnapshot);
				for (GuiTrainState& state : exitedSnapshot->trains)
					if (state.index == selectedTrain->index)
						state.outOfSimulation = true;
				w.m_snapshot = exitedSnapshot;
				updateTrainPosition(w, exitedSnapshot->timestep);
				QApplication::processEvents();
				const int exitedComboIndex = w.m_followTrainCombo->findData(selectedTrain->index);
				const QString exitedName = w.m_followTrainCombo->itemData(exitedComboIndex, kFollowNameRole).toString();
				const QString exitedSentence = exitedName + " has left the network. Follow cannot be switched on for it.";
				if (w.m_followAction->isChecked() || w.m_followTrainIndex != -1
					|| w.statusBar()->currentMessage() != exitedName + " has left the network") {
					ok = false;
					failures << "follow exit did not disable with explicit status";
				} else if (followingBeforeEnd && (w.m_followCamera->running() || w.m_followCamera->hasTarget())) {
					ok = false;
					failures << "follow exit did not stop the camera";
				} else if (w.m_followStatusLabel->toolTip() != exitedSentence || w.m_followTrainCombo->itemText(exitedComboIndex) != exitedName + " (finished)"
					|| !w.m_followAction->isEnabled() || w.m_followTrainCombo->currentIndex() != exitedComboIndex) {
					ok = false;
					failures << "follow exit did not explain itself in the label and the list";
				} else {
					std::fprintf(stdout, "E2E_FOLLOW_LIVE_END_OK\n");
					std::fflush(stdout);
				}
				if (offStopped && layerStopped && blindStopped && followingBeforeEnd && !w.m_followCamera->running() && !w.m_followCamera->hasTarget()) {
					std::fprintf(stdout, "E2E_FOLLOW_CAMERA_STOP_OK\n");
					std::fflush(stdout);
				}
				if (exitedComboIndex >= 0)
					w.m_followTrainCombo->setCurrentIndex(exitedComboIndex);
				w.m_followAction->setChecked(true);
				QApplication::processEvents();
				if (w.m_followAction->isChecked() || w.m_followTrainIndex != -1
					|| w.statusBar()->currentMessage() != exitedName + " has left the network") {
					ok = false;
					failures << "reselecting an exited train re-enabled Follow";
				}
				w.m_snapshot = interruptedSnapshot;
				updateTrainPosition(w, interruptedSnapshot->timestep);
				w.m_followCamera->stop();
				w.m_followCamera->setClock({});
				w.m_presenter->setClock({});
				w.setFollowTrain(selectedTrain->index);
			}
		}
	} else {
		ok = false;
		failures << "follow animation regression lacks scene, viewport, train, or snapshot";
	}
	if (w.scene && w.networkView && selectedTrain && selectedTrainBody) {
		QObject::disconnect(&simulation, &DispatchController::snapshotAvailable,
			&w, &MainWindow::waitForUpdates);
		if (w.m_runController->m_worker)
			w.m_runController->m_worker->requestPause();
		QApplication::processEvents();
		w.m_presenter->stopTrainAnimation(selectedTrain->index);
		if (w.m_runController->m_worker && !w.m_runController->m_worker->isPauseRequested()) {
			ok = false;
			failures << "train movement was not paused before reselection";
		}

		const QPointF cachedCenter = selectedTrainBody->sceneBoundingRect().center();
		w.networkView->centerOn(cachedCenter);
		w.networkView->zoomBy(4.0);
		QApplication::processEvents();
		const QPoint targetViewportCenter = w.networkView->viewport()->rect().center()
			+ QPoint(qMax(40, w.networkView->viewport()->width() / 8), 0);
		const QPointF moveDelta = w.networkView->mapToScene(targetViewportCenter) - cachedCenter;
		const QPointF originalPosition = selectedTrain->pos();
		selectedTrain->setPos(originalPosition + moveDelta);
		QApplication::processEvents();

		if (selectedTrainBody->sceneBoundingRect().contains(cachedCenter)
			|| selectedTrainBody->contains(selectedTrainBody->mapFromScene(cachedCenter))) {
			ok = false;
			failures << "moved train geometry still hits its stale cached center";
		}

		const QPointF movedCenter = selectedTrainBody->sceneBoundingRect().center();
		const QPoint clickPos = w.networkView->mapFromScene(movedCenter);
		if (!w.networkView->viewport()->rect().contains(clickPos)) {
			ok = false;
			failures << QString("recomputed train center is outside the viewport (cached=%1,%2 moved=%3,%4 click=%5,%6 viewport=%7x%8)")
							.arg(cachedCenter.x(), 0, 'f', 1)
							.arg(cachedCenter.y(), 0, 'f', 1)
							.arg(movedCenter.x(), 0, 'f', 1)
							.arg(movedCenter.y(), 0, 'f', 1)
							.arg(clickPos.x())
							.arg(clickPos.y())
							.arg(w.networkView->viewport()->width())
							.arg(w.networkView->viewport()->height());
		} else {
			const QPoint screenPos = w.networkView->viewport()->mapToGlobal(clickPos);
			QMouseEvent press(QEvent::MouseButtonPress, QPointF(clickPos), QPointF(screenPos),
				Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
			QApplication::sendEvent(w.networkView->viewport(), &press);
			QMouseEvent release(QEvent::MouseButtonRelease, QPointF(clickPos), QPointF(screenPos),
				Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
			QApplication::sendEvent(w.networkView->viewport(), &release);
			QApplication::processEvents();
		}

		if (w.m_inspector->selectedTrainIndex() != selectedTrain->index) {
			ok = false;
			failures << "viewport reselection did not select the exact moved train";
		}
		const int comboIndex = w.m_followTrainCombo ? w.m_followTrainCombo->findData(selectedTrain->index) : -1;
		if (!w.m_followAction || !w.m_followAction->isChecked() || w.m_followTrainIndex != selectedTrain->index
			|| !w.m_followTrainCombo || comboIndex < 0 || w.m_followTrainCombo->currentIndex() != comboIndex) {
			ok = false;
			failures << "viewport reselection did not follow the exact moved train";
		}
		w.networkView->fitToTopology();
		w.updateViewportOverlays();
		QApplication::processEvents();
		TrainBadgeItem* selectedBadge = w.m_presenter->m_trainBadges.value(selectedTrain->index, nullptr);
		if (!w.m_presenter->effect || selectedTrain->graphicsEffect() != w.m_presenter->effect || !selectedBadge
			|| !selectedBadge->isPromoted()
			|| selectedBadge->presentation() != TrainBadgeItem::Presentation::Detailed) {
			ok = false;
			failures << "viewport reselection did not highlight and promote the exact moved train";
		}
		if (!w.m_inspector->dock() || !w.m_inspector->dock()->isVisible()
			|| w.m_inspector->dock()->windowTitle() != QStringLiteral("Train Info")
			|| !w.m_inspector->trainInfoWidget || !w.m_inspector->trainInfoWidget->isVisible()) {
			ok = false;
			failures << "viewport reselection did not restore train info state";
		}
		auto* inspectorScroll = w.m_inspector->dock() ? qobject_cast<QScrollArea*>(w.m_inspector->dock()->widget()) : nullptr;
		for (QLineEdit* field : {w.m_inspector->trainIDText, w.m_inspector->trainTypeText, w.m_inspector->trainLengthText, w.m_inspector->trainWagonsText}) {
			if (!inspectorScroll || !field) {
				ok = false;
				failures << "train inspector viewport unavailable";
				break;
			}
			const int left = field->mapTo(inspectorScroll->viewport(), QPoint(0, 0)).x();
			if (!field->isReadOnly() || left < 0
				|| left + field->width() > inspectorScroll->viewport()->width()) {
				ok = false;
				failures << "train inspector values require horizontal scrolling";
				break;
			}
		}
		const QString expectedTrainId = QString::fromStdString(InspectorPanel::to_string_precision(selectedTrain->trainId, 0));
		const QString expectedTrainType = QString::fromStdString(selectedTrain->trainType);
		const QString expectedTrainLength = QString::fromStdString(InspectorPanel::to_string_precision(selectedTrain->trainLength, 0));
		const QString expectedWagonCount = QString::fromStdString(InspectorPanel::to_string_precision(selectedTrain->wagonCount, 0));
		if (!w.m_inspector->trainIDText || w.m_inspector->trainIDText->text() != expectedTrainId
			|| !w.m_inspector->trainTypeText || w.m_inspector->trainTypeText->text() != expectedTrainType
			|| !w.m_inspector->trainLengthText || w.m_inspector->trainLengthText->text() != expectedTrainLength
			|| !w.m_inspector->trainWagonsText || w.m_inspector->trainWagonsText->text() != expectedWagonCount) {
			ok = false;
			failures << "viewport reselection did not populate the exact moved train details";
		}
		if (selectedTrainBody->pen() != selectedTrainPen || selectedTrainBody->brush() != selectedTrainBrush) {
			ok = false;
			failures << "viewport reselection changed the moved train source paint";
		}
		selectedTrain->setPos(originalPosition);
		QApplication::processEvents();
	}
	if (!w.scene || !w.networkView) {
		ok = false;
		failures << "cannot clear selection without a scene and viewport";
	} else {
		const QRectF contentBounds = w.scene->itemsBoundingRect();
		QGraphicsSceneMouseEvent emptyEvent(QEvent::GraphicsSceneMousePress);
		emptyEvent.setButton(Qt::LeftButton);
		emptyEvent.setButtons(Qt::LeftButton);
		emptyEvent.setScenePos(contentBounds.bottomRight() + QPointF(1000.0, 1000.0));
		emptyEvent.setWidget(w.networkView->viewport());
		w.scene->mousePressEvent(&emptyEvent);
		QGraphicsSceneMouseEvent emptyRelease(QEvent::GraphicsSceneMouseRelease);
		emptyRelease.setButton(Qt::LeftButton);
		emptyRelease.setScenePos(emptyEvent.scenePos());
		emptyRelease.setWidget(w.networkView->viewport());
		w.scene->mouseReleaseEvent(&emptyRelease);
		QApplication::processEvents();
		if (w.m_presenter->effect || (w.m_inspector->dock() && w.m_inspector->dock()->isVisible())) {
			ok = false;
			failures << "empty scene click did not clear selection";
		}
		if (w.m_inspector->arcInfoWidget->isVisible() || w.m_inspector->nodeInfoWidget->isVisible() || w.m_inspector->connectionInfoWidget->isVisible()
			|| w.m_inspector->signallingInfoWidget->isVisible() || w.m_inspector->trainInfoWidget->isVisible()) {
			ok = false;
			failures << "empty scene click left an entity pane visible";
		}
		if (w.m_presenter->trainPaxInfoItem || w.m_presenter->paxIconInfoItem) {
			ok = false;
			failures << "empty scene click left a temporary passenger overlay";
		}
	}
	if (w.m_followAction) {
		w.m_followAction->setChecked(false);
		if (std::any_of(w.m_presenter->m_stationOverlays.cbegin(), w.m_presenter->m_stationOverlays.cend(),
				[](const auto* overlay) { return overlay && overlay->isFollowed(); })) {
			ok = false;
			failures << "follow toggle left a stale station priority";
		}
		QApplication::processEvents();
	}
	if (w.m_followTrainIndex != -1) {
		ok = false;
		failures << "follow toggle did not clear its train index";
	}

	// A platform id darkens a station square only at the end of a line.
	{
		Node platformNode;
		platformNode.stationName = "E2E";
		platformNode.stationPlatformId = "Platform_1";
		const auto paintedFill = [&w, &platformNode](bool lineEnd, int connections) {
			platformNode.numConnections = connections;
			const QList<QGraphicsItem*> before = w.scene->items();
			w.m_presenter->paintStationNode(QPointF(), w.m_presenter->station_node_size, 0, &platformNode, lineEnd);
			QColor fill;
			for (QGraphicsItem* item : w.scene->items()) {
				if (before.contains(item))
					continue;
				if (const auto* square = qgraphicsitem_cast<StationNodeItem*>(item))
					fill = square->brush().color();
				w.scene->removeItem(item);
				delete item;
				break;
			}
			return fill;
		};
		const QColor interiorFill = paintedFill(false, 1);
		const QColor lineEndFill = paintedFill(true, 1);
		const QColor interchangeFill = paintedFill(false, 3);
		if (interiorFill != classifyStation(false, 1).fill || lineEndFill != classifyStation(true, 1).fill
			|| interchangeFill != classifyStation(false, 3).fill) {
			ok = false;
			failures << QString("station square colours: interior %1, line end %2, interchange %3")
							.arg(interiorFill.name(), lineEndFill.name(), interchangeFill.name());
		} else {
			std::fprintf(stdout, "E2E_STATION_SQUARE_COLOUR_OK\n");
			std::fflush(stdout);
		}
	}

	if (w.networkView) {
		QString findFailure;
		if (checkFind(w, true, findFailure)) {
			std::fprintf(stdout, "E2E_FIND_OK\n");
			std::fflush(stdout);
		} else {
			ok = false;
			failures << findFailure;
		}
	}

	// Exercise the real Stop/completion lifecycle after the rendering checks.
	// Keep a selected operational track so clearing transient states cannot
	// accidentally clear independent graphics selection. Re-enable delivery
	// with a unique connection to cover a late queued snapshot as well.
	if (!w.m_runController->m_worker || w.m_presenter->allArcs.isEmpty()) {
		ok = false;
		failures << "operational track lifecycle fixture lost its running scene";
	} else {
		TrackLineItem* lifecycleTrack = nullptr;
		for (TrackLineItem* track : w.m_presenter->allArcs) {
			if (track && track->operationalState() != TrackOperationalState::Free) {
				lifecycleTrack = track;
				break;
			}
		}
		if (!w.m_outputFolderAction || w.m_outputFolderAction->isEnabled()) {
			ok = false;
			failures << "the output folder can be changed during a run";
		}
		if (!lifecycleTrack) {
			ok = false;
			failures << "operational track lifecycle fixture has no active state";
		} else {
			lifecycleTrack->setFlag(QGraphicsItem::ItemIsSelectable, true);
			lifecycleTrack->setSelected(true);
			QObject::connect(&simulation, &DispatchController::snapshotAvailable,
				&w, &MainWindow::waitForUpdates,
				static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));
			// Stop ends a catch-up of the camera where the view is. Follow is switched off again at once, so that the
			// message of the stopped run is the last one.
			bool catchingUpAtStop = false;
			if (selectedTrain && w.m_followAction) {
				w.setFollowTrain(selectedTrain->index);
				// A zoom of the user, so that the next target starts a catch-up.
				w.networkView->zoomBy(2.0);
				w.m_followCamera->follow(w.networkView->mapToScene(w.networkView->viewport()->rect().center() + QPoint(100, 0)), false);
				catchingUpAtStop = w.m_followCamera->running();
			}
			// The inspector shows the track of the run that Stop ends.
			w.m_inspector->displayArcInfo(lifecycleTrack);
			const bool inspectorShownBeforeStop = !w.m_inspector->dock()->isHidden();
			if (w.ui->actionSimulationStop)
				w.ui->actionSimulationStop->trigger();
			const bool stopEndedCatchUp = catchingUpAtStop && !w.m_followCamera->running() && !w.m_followCamera->hasTarget();
			if (w.m_followAction)
				w.m_followAction->setChecked(false);
			if (!stopEndedCatchUp) {
				ok = false;
				failures << QString("Stop did not end the catch-up of the camera (catching up before=%1)").arg(catchingUpAtStop);
			} else {
				std::fprintf(stdout, "E2E_FOLLOW_CAMERA_RUN_STOP_OK\n");
				std::fflush(stdout);
			}
			if (lifecycleTrack->operationalState() != TrackOperationalState::Free
				|| !lifecycleTrack->isSelected()) {
				ok = false;
				failures << "Stop did not immediately clear the underlay while preserving selection";
			}
			// A delivery already queued at Stop must not restore the underlay.
			QMetaObject::invokeMethod(&w, "waitForUpdates", Qt::QueuedConnection);
			QElapsedTimer stopWait;
			stopWait.start();
			while (w.m_runController->m_worker && stopWait.elapsed() < 15000)
				QApplication::processEvents(QEventLoop::AllEvents, 20);
			if (w.m_runController->m_worker) {
				ok = false;
				failures << "Stop did not complete through the worker lifecycle";
			} else if (!w.m_outputFolderAction || !w.m_outputFolderAction->isEnabled()) {
				ok = false;
				failures << "the output folder stayed disabled after the run";
			}
			if (!w.m_completedReplay.empty() || w.m_replayTimer->isActive()) {
				ok = false;
				failures << "Stop retained completed replay";
			}
			if (!inspectorShownBeforeStop || !w.m_inspector->dock()->isHidden()) {
				ok = false;
				failures << QString("Stop left the inspector open (shown before=%1)").arg(inspectorShownBeforeStop);
			}
			if (w.m_runController->m_runtimeStatus != QStringLiteral("Stopped") || w.m_runController->m_resultsAvailable
				|| w.statusBar()->currentMessage() != QStringLiteral("Simulation stopped")) {
				failures << "Stop did not leave a stopped run without results";
				ok = false;
			} else if (!w.m_showingTrackPreview || !w.m_presenter->allArcs.isEmpty() || !w.m_presenter->allTrains.isEmpty()) {
				// The stopped run is replaced by the track preview, so the queued delivery
				// has no runtime track or train left to paint.
				failures << "a stopped run left runtime graphics on the canvas";
				ok = false;
			} else {
				std::fprintf(stdout, "E2E_OPERATIONAL_TRACK_LIFECYCLE_OK\n");
				std::fflush(stdout);
				// Nothing was selected in the infrastructure editor, so the stopped preview draws no selection cue.
				const QList<QGraphicsItem*> previewItems = w.scene->items();
				const bool cueDrawn = std::any_of(previewItems.cbegin(), previewItems.cend(), [](const QGraphicsItem* item) {
					const auto* track = qgraphicsitem_cast<const TrackLineItem*>(item);
					return track && track->pen().color() == kSelectionCueColor;
				});
				if (w.m_infrastructureTable->currentRow() != -1 || !w.m_infrastructurePanel->selectionId().isEmpty()
					|| w.m_presenter->m_previewHasSelectedTrack || cueDrawn) {
					ok = false;
					failures << "Stop selected a track without a selection in the infrastructure editor";
				} else {
					std::fprintf(stdout, "E2E_STOP_PREVIEW_UNSELECTED_OK\n");
					std::fflush(stdout);
				}
			}
		}
	}

	if (ok) {
		const qreal devicePixelRatio = w.windowHandle() ? w.windowHandle()->devicePixelRatio() : 1.0;
		std::fprintf(stdout, "E2E_VISUAL_POLISH_DPR_%0.1f\n", devicePixelRatio);
		std::fprintf(stdout, "E2E_VISUAL_POLISH_OK\n");
		std::fflush(stdout);
		if (w.m_runController->m_worker && w.m_runController->m_workerThread && w.m_runController->m_workerThread->isRunning()) {
			QObject::connect(w.m_runController->m_workerThread, &QThread::finished, qApp, []() {
				QCoreApplication::exit(0);
			});
			w.m_runController->m_worker->requestStop();
			return;
		}
		QCoreApplication::exit(0);
		return;
	}

	std::fprintf(stderr, "E2E_VISUAL_POLISH_FAIL: %s\n", failures.join(", ").toStdString().c_str());
	std::fflush(stderr);
	if (w.m_runController->m_worker && w.m_runController->m_workerThread && w.m_runController->m_workerThread->isRunning()) {
		QObject::connect(w.m_runController->m_workerThread, &QThread::finished, qApp, []() {
			QCoreApplication::exit(2);
		});
		w.m_runController->m_worker->requestStop();
		return;
	}
	QCoreApplication::exit(2);
}

// The trains of the displayed snapshot as at time t, the way a frame of the run or of the replay shows them.
void MainWindowTestAccess::updateTrainPosition(MainWindow& w, int t) {
	if (!w.m_snapshot)
		return;
	w.m_presenter->updateTrainPosition(*w.m_snapshot, t,
		w.m_replayActive ? NetworkPresenter::Playback::Replay : NetworkPresenter::Playback::Live);
}

// renderSnapshot reports every frame it draws. A scenario that waits for trains starts on the first live frame that has some.
void MainWindowTestAccess::visualPolishFrame(MainWindow& w, bool historical) {
	if (!historical && !scenarioStarted && !w.m_presenter->allTrains.isEmpty())
		runVisualPolish(w);
}

// Exported by Qt Gui, both versions, and declared by QtTest for its key clicks.
Q_GUI_EXPORT bool qt_sendShortcutOverrideEvent(QObject* o, ulong timestamp, int k, Qt::KeyboardModifiers mods,
	const QString& text = QString(), bool autorep = false, ushort count = 1);

// Finds a station, a signal and, with expectTrain, a train: Ctrl+F or the Edit menu action, the kind set in the field,
// Down to the second match, and Enter in the field or, for the signal, in the result list. The view must then show that
// item at the detail zoom and the item must carry the selection ring. Esc closes the dialog. The view is fitted and nothing is selected or followed after.
bool MainWindowTestAccess::checkFind(MainWindow& w, bool expectTrain, QString& failure) {
	const auto key = [](QObject* receiver, int code, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
		// A shortcut goes first, as with a real key press: Qt 6 no longer looks for one when a key event is sent.
		if (!qt_sendShortcutOverrideEvent(receiver, 0, code, modifiers)) {
			QKeyEvent press(QEvent::KeyPress, code, modifiers);
			QKeyEvent release(QEvent::KeyRelease, code, modifiers);
			QApplication::sendEvent(receiver, &press);
			QApplication::sendEvent(receiver, &release);
		}
		QApplication::processEvents();
	};
	const auto targetLabelled = [&w](const QString& prefix) {
		const QList<NetworkPresenter::FindTarget> targets = w.m_presenter->findTargets();
		const auto found = std::find_if(targets.cbegin(), targets.cend(),
			[&prefix](const NetworkPresenter::FindTarget& target) { return target.label.startsWith(prefix); });
		return found == targets.cend() ? NetworkPresenter::FindTarget{QString(), nullptr, QPointF(), QString()} : *found;
	};
	// A window shortcut needs an active window, which the offscreen platform of Qt 6 does not give a window by itself.
	w.activateWindow();
	QApplication::processEvents();
	bool viaShortcut = true;
	for (const QString& kind : {QStringLiteral("Station: "), QStringLiteral("Signal: "), QStringLiteral("Train: ")}) {
		const NetworkPresenter::FindTarget target = targetLabelled(kind);
		if (!target.item) {
			if (kind != QStringLiteral("Train: ") || expectTrain) {
				failure = QStringLiteral("Find offers no %1").arg(kind.trimmed());
				return false;
			}
			continue;
		}
		w.m_inspector->handleDisableHighlight();
		if (viaShortcut)
			key(&w, Qt::Key_F, Qt::ControlModifier);
		else
			w.m_findAction->trigger();
		viaShortcut = false;
		auto* field = w.m_findDialog ? w.m_findDialog->findChild<QLineEdit*>() : nullptr;
		auto* results = w.m_findDialog ? w.m_findDialog->findChild<QListWidget*>() : nullptr;
		if (!field || !results || !w.m_findDialog->isVisible()) {
			failure = QStringLiteral("Find did not open");
			return false;
		}
		field->setText(kind.trimmed());
		key(field, Qt::Key_Down);
		if (!results->currentItem() || results->currentRow() != std::min(1, results->count() - 1)) {
			failure = QStringLiteral("Down left row %1 of %2 current for \"%3\"").arg(results->currentRow()).arg(results->count()).arg(kind.trimmed());
			return false;
		}
		const QString label = results->currentItem()->text();
		key(kind == QStringLiteral("Signal: ") ? static_cast<QObject*>(results) : field, Qt::Key_Return);
		const NetworkPresenter::FindTarget shown = targetLabelled(label);
		const QRectF visible = w.networkView->mapToScene(w.networkView->viewport()->rect()).boundingRect();
		// The ring of a train is on its group.
		auto* body = qgraphicsitem_cast<TrainBodyItem*>(shown.item);
		const QGraphicsItem* ringed = body && body->parentItem() ? body->parentItem() : shown.item;
		const bool selected = w.m_presenter->effect && ringed && ringed->graphicsEffect() == w.m_presenter->effect
			&& w.m_inspector->dock()->isVisible() && (!body || w.m_inspector->selectedTrainIndex() == body->index);
		if (w.m_findDialog->isVisible() || !shown.item || w.networkView->zoomRatio() < TrainBadgeItem::detailedZoomThreshold() - 1e-5
			|| !visible.contains(shown.center) || !selected) {
			failure = QStringLiteral("finding \"%1\" left the dialog open %2, zoom %3, centre shown %4, selected %5")
						  .arg(label)
						  .arg(w.m_findDialog->isVisible())
						  .arg(w.networkView->zoomRatio())
						  .arg(shown.item && visible.contains(shown.center))
						  .arg(selected);
			return false;
		}
	}
	w.m_findAction->trigger();
	// A signal is listed with the labels of its sections, and the label as well as the id of a section finds it.
	const NetworkPresenter::FindTarget signalTarget = targetLabelled(QStringLiteral("Signal: "));
	const auto* head = qgraphicsitem_cast<const SignalItem*>(signalTarget.item);
	if (!head || head->sectionAheadId.empty()) {
		failure = QStringLiteral("Find lists no signal with a section ahead");
		return false;
	}
	auto* field = w.m_findDialog->findChild<QLineEdit*>();
	auto* results = w.m_findDialog->findChild<QListWidget*>();
	const auto lists = [&](const std::string& words) {
		field->setText(QString::fromStdString(words));
		return !results->findItems(signalTarget.label, Qt::MatchExactly).isEmpty();
	};
	if (!signalTarget.label.contains(QString::fromStdString(head->sectionAheadLabel)) || !lists(head->sectionAheadLabel)
		|| !lists(head->sectionAheadId)) {
		failure = QStringLiteral("Find does not list \"%1\" by the label and the id of section %2")
					  .arg(signalTarget.label, QString::fromStdString(head->sectionAheadId));
		return false;
	}
	key(w.m_findDialog->findChild<QLineEdit*>(), Qt::Key_Escape);
	const bool closed = !w.m_findDialog->isVisible();
	w.setFollowTrain(-1);
	w.networkView->fitToTopology();
	w.m_inspector->handleDisableHighlight();
	QApplication::processEvents();
	if (!closed)
		failure = QStringLiteral("Esc did not close Find");
	return closed;
}
