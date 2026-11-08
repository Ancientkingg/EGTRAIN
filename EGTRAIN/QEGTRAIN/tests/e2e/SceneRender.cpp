#include "tests/e2e/MainWindowTestAccess.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QGraphicsScene>
#include <QPixmap>
#include <QRectF>
#include <QSettings>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "app/MainWindowSupport.h"
#include "graphics/NetworkScene.h"
#include "graphics/NetworkView.h"
#include "graphics/items/PassengerItem.h"
#include "graphics/items/PlatformItem.h"
#include "graphics/items/StationOverlayItem.h"
#include "simulation/SimulationWorker.h"
#include "widgets/ElidedLabel.h"

// Opens the scene named by QEGTRAIN_E2E_SCENE if none is open, runs it, and checks that the network view
// draws real geometry inside the viewport instead of a collapsed point. It runs again every 500 ms,
// at most 40 times, until a train is drawn. QEGTRAIN_E2E_SCREENSHOT names a file for a picture of the view. A case without
// passengers must have View > Passengers disabled, with a reason; for a case with passengers the run starts with them turned on,
// and the check turns them off and on again with the two controls.
void MainWindowTestAccess::runSceneRender(MainWindow& w) {
	if (scenarioStarted)
		return;

	if (!w.m_document->isLoaded()) {
		QString sceneDir = qEnvironmentVariable("QEGTRAIN_E2E_SCENE");
		if (sceneDir.isEmpty() || !w.openSceneDirectory(sceneDir)) {
			std::fprintf(stderr, "E2E_SCENE_RENDER_FAIL: scene did not open\n");
			std::fflush(stderr);
			QCoreApplication::exit(2);
			return;
		}
	}
	const bool hasPassengers = !w.m_document->model().passengers.empty();
	if (!w.m_runController->m_worker && w.m_presenter->runTrains().isEmpty()) {
		// Before the first run Follow is off and its tooltip says why.
		const bool services = !w.m_document->mutableModel().services.empty();
		const QString reason = services ? QStringLiteral("Run the case first, then you can follow a train.")
										: QStringLiteral("This case has no trains to follow.");
		const bool explained = w.m_followTrainCombo->count() == 1 && w.m_followTrainCombo->currentData().toInt() == -1
			&& w.m_followTrainCombo->currentText() == (services ? QStringLiteral("Run the case first") : QStringLiteral("No trains to follow"))
			&& !w.m_followAction->isEnabled() && !w.m_followAction->isChecked() && w.m_followAction->toolTip() == reason
			&& w.m_followTrainCombo->toolTip() == reason && w.m_followTrainCombo->accessibleDescription() == reason
			&& w.m_followStatusLabel->toolTip() == reason
			&& w.findChild<QToolButton*>("actionFollowButton")->accessibleDescription() == reason;
		if (!explained) {
			std::fprintf(stderr, "E2E_SCENE_RENDER_FAIL: Follow before the first run does not say why it is off\n");
			std::fflush(stderr);
			QCoreApplication::exit(2);
			return;
		}
		std::fprintf(stdout, services ? "E2E_FOLLOW_NO_RUN_OK\n" : "E2E_FOLLOW_NO_SERVICES_OK\n");
		std::fflush(stdout);
		// Passengers are off at the start, and the controls are disabled, with the reason, when the case has none.
		const bool controlsEnabled = w.m_showPassengersAction->isEnabled() && w.m_passengerLayerCheck->isEnabled();
		const bool reasoned = hasPassengers
			|| (w.m_showPassengersAction->toolTip().contains("no passengers") && w.m_passengerLayerCheck->toolTip().contains("no passengers"));
		if (w.m_showPassengersAction->isChecked() || w.m_passengerLayerCheck->isChecked() || controlsEnabled != hasPassengers || !reasoned) {
			std::fprintf(stderr, "E2E_SCENE_RENDER_FAIL: View > Passengers is not off, or not disabled for a case without passengers\n");
			std::fflush(stderr);
			QCoreApplication::exit(2);
			return;
		}
		if (hasPassengers) {
			// The run starts with passengers on. The display data comes with the run, so a preview says so.
			w.m_showPassengersAction->setChecked(true);
			if (!w.m_passengerLayerCheck->isChecked() || !w.statusBar()->currentMessage().contains("next run")) {
				std::fprintf(stderr, "E2E_SCENE_RENDER_FAIL: View > Passengers did not turn the check box on and say when it takes effect\n");
				std::fflush(stderr);
				QCoreApplication::exit(2);
				return;
			}
		}
		w.m_runController->runScene();
		if (!w.m_runController->m_worker) {
			std::fprintf(stderr, "E2E_SCENE_RENDER_FAIL: scene run did not start\n");
			std::fflush(stderr);
			QCoreApplication::exit(2);
			return;
		}
	}

	if (w.m_presenter->runTrains().isEmpty() && scenarioAttempts < 40) {
		++scenarioAttempts;
		QTimer::singleShot(500, &w, [&w] { MainWindowTestAccess::runSceneRender(w); });
		return;
	}
	scenarioStarted = true;

	bool ok = true;
	QStringList failures;

	if (w.m_presenter->runTrains().isEmpty()) {
		ok = false;
		failures << "no train items rendered";
	}

	if (w.scene->items().size() < 10) {
		ok = false;
		failures << "scene has too few items";
	}

	// The platform bars are drawn with or without passengers and show with the stations in the near look. The counters
	// and icons on them are the passenger items of a run with passengers, which show only where the bars show and which
	// View > Passengers turns off and on.
	const QList<PlatformItem*>& platforms = PresenterAccess::allPlatforms(*w.m_presenter);
	const auto barsShown = [&platforms](bool shown) {
		return !platforms.isEmpty() && std::all_of(platforms.cbegin(), platforms.cend(), [shown](const PlatformItem* platform) {
			return platform->isVisible() == shown;
		});
	};
	const auto passengersHidden = [&platforms]() {
		return std::none_of(platforms.cbegin(), platforms.cend(), [](const PlatformItem* platform) {
			return (platform->textIcon && platform->textIcon->isVisible())
				|| std::any_of(platform->passengerIcons.cbegin(), platform->passengerIcons.cend(),
					[](const PassengerItem* icon) { return icon->isVisible(); });
		});
	};
	const auto iconsShown = [&platforms]() {
		return std::all_of(platforms.cbegin(), platforms.cend(), [](const PlatformItem* platform) {
			return std::all_of(platform->passengerIcons.cbegin(), platform->passengerIcons.cend(),
				[](const PassengerItem* icon) { return icon->isVisible(); });
		});
	};
	w.fitView();
	w.updateViewportOverlays();
	// At Fit the Stations switch leaves the bars hidden when it turns them on again.
	w.m_stationLayerCheck->setChecked(false);
	w.m_stationLayerCheck->setChecked(true);
	const bool farHidden = barsShown(false) && passengersHidden();
	// Zoomed in until the tracks at the stations are far enough apart for the near look.
	const qreal spacing = PresenterAccess::stationTrackSpacing(*w.m_presenter) * std::abs(w.networkView->transform().m11());
	if (spacing > 0.0)
		w.networkView->zoomBy(1.05 * StationOverlayItem::NearTrackSpacingPixels / spacing);
	w.updateViewportOverlays();
	const bool nearShown = barsShown(true) && iconsShown();
	w.m_stationLayerCheck->setChecked(false);
	const bool layerHidden = barsShown(false) && passengersHidden();
	w.m_stationLayerCheck->setChecked(true);
	if (!farHidden || !nearShown || !layerHidden || !barsShown(true) || !iconsShown()) {
		ok = false;
		failures << "the platform bars and their passengers do not hide at Fit and with the stations, and show in the near look";
	}
	if (!hasPassengers) {
		if (std::any_of(platforms.cbegin(), platforms.cend(),
				[](const PlatformItem* platform) { return platform->textIcon || !platform->passengerIcons.isEmpty(); })) {
			ok = false;
			failures << "passenger counters or icons without passengers";
		}
	} else {
		w.m_passengerLayerCheck->setChecked(false);
		bool toggled = !w.m_showPassengersAction->isChecked() && barsShown(true) && passengersHidden()
			&& !QSettings().value(kShowPassengersKey).toBool();
		w.m_showPassengersAction->setChecked(true);
		toggled = toggled && w.m_passengerLayerCheck->isChecked() && barsShown(true) && iconsShown()
			&& QSettings().value(kShowPassengersKey).toBool();
		if (!toggled) {
			ok = false;
			failures << "View > Passengers did not turn the passenger display off and on, or turned the platform bars off";
		}
	}
	w.fitView();
	w.updateViewportOverlays();
	if (ok) {
		std::fprintf(stdout, hasPassengers ? "E2E_PASSENGERS_TOGGLE_OK\n" : "E2E_PASSENGERS_DISABLED_OK\n");
		std::fflush(stdout);
	}

	QRectF bounds = w.scene->itemsBoundingRect();
	if (bounds.width() < 100.0) {
		ok = false;
		failures << "network geometry did not spread horizontally";
	}

	// the items existing is not enough; they must be inside the viewport
	QRectF visible = w.networkView->mapToScene(w.networkView->viewport()->rect()).boundingRect();
	if (!visible.intersects(bounds)) {
		ok = false;
		failures << "network geometry is outside the viewport";
	}

	QString screenshotPath = qEnvironmentVariable("QEGTRAIN_E2E_SCREENSHOT");
	if (!screenshotPath.isEmpty()) {
		QPixmap image = w.networkView->grab();
		if (!image.save(screenshotPath)) {
			ok = false;
			failures << "screenshot save failed";
		}
	}

	if (ok) {
		std::fprintf(stdout, "E2E_SCENE_RENDER_OK\n");
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

	std::fprintf(stderr, "E2E_SCENE_RENDER_FAIL: %s\n", failures.join(", ").toStdString().c_str());
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
