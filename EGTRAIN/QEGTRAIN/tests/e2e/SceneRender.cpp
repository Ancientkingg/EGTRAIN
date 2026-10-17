#include "tests/e2e/MainWindowTestAccess.h"

#include <QAction>
#include <QComboBox>
#include <QCoreApplication>
#include <QGraphicsScene>
#include <QPixmap>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <cstdio>

// Opens the scene named by QEGTRAIN_E2E_SCENE if none is open, runs it, and checks that the network view
// draws real geometry inside the viewport instead of a collapsed point. It runs again every 500 ms,
// at most 40 times, until a train is drawn. QEGTRAIN_E2E_SCREENSHOT names a file for a picture of the view.
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
	if (!w.m_worker && w.m_presenter->allTrains.isEmpty()) {
		// Before the first run Follow is off and its tooltip says why.
		const bool services = !w.m_document->mutableModel().services.empty();
		const QString reason = services ? QStringLiteral("Run the case first, then you can follow a train.")
										: QStringLiteral("This case has no trains to follow.");
		const bool explained = w.m_followTrainCombo->count() == 1 && w.m_followTrainCombo->currentData().toInt() == -1
			&& w.m_followTrainCombo->currentText() == (services ? QStringLiteral("Run the case to follow trains") : QStringLiteral("No trains to follow"))
			&& !w.m_followAction->isEnabled() && !w.m_followAction->isChecked() && w.m_followAction->toolTip() == reason
			&& w.m_followTrainCombo->toolTip() == reason && w.m_followTrainCombo->accessibleDescription() == reason
			&& w.m_followStatusLabel->text() == reason
			&& w.findChild<QToolButton*>("actionFollowButton")->accessibleDescription() == reason;
		if (!explained) {
			std::fprintf(stderr, "E2E_SCENE_RENDER_FAIL: Follow before the first run does not say why it is off\n");
			std::fflush(stderr);
			QCoreApplication::exit(2);
			return;
		}
		std::fprintf(stdout, services ? "E2E_FOLLOW_NO_RUN_OK\n" : "E2E_FOLLOW_NO_SERVICES_OK\n");
		std::fflush(stdout);
		w.runScene();
		if (!w.m_worker) {
			std::fprintf(stderr, "E2E_SCENE_RENDER_FAIL: scene run did not start\n");
			std::fflush(stderr);
			QCoreApplication::exit(2);
			return;
		}
	}

	if (w.m_presenter->allTrains.isEmpty() && scenarioAttempts < 40) {
		++scenarioAttempts;
		QTimer::singleShot(500, &w, [&w] { MainWindowTestAccess::runSceneRender(w); });
		return;
	}
	scenarioStarted = true;

	bool ok = true;
	QStringList failures;

	if (w.m_presenter->allTrains.isEmpty()) {
		ok = false;
		failures << "no train items rendered";
	}

	if (w.scene->items().size() < 10) {
		ok = false;
		failures << "scene has too few items";
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
		if (w.m_worker && w.m_workerThread && w.m_workerThread->isRunning()) {
			QObject::connect(w.m_workerThread, &QThread::finished, qApp, []() {
				QCoreApplication::exit(0);
			});
			w.m_worker->requestStop();
			return;
		}
		QCoreApplication::exit(0);
		return;
	}

	std::fprintf(stderr, "E2E_SCENE_RENDER_FAIL: %s\n", failures.join(", ").toStdString().c_str());
	std::fflush(stderr);
	if (w.m_worker && w.m_workerThread && w.m_workerThread->isRunning()) {
		QObject::connect(w.m_workerThread, &QThread::finished, qApp, []() {
			QCoreApplication::exit(2);
		});
		w.m_worker->requestStop();
		return;
	}
	QCoreApplication::exit(2);
}
