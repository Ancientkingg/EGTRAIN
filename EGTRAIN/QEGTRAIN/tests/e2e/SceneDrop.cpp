#include "tests/e2e/MainWindowTestAccess.h"

#include "app/MainWindowTestHooks.h"
#include "graphics/NetworkScene.h"
#include "scene/SceneBundle.h"
#include "widgets/DisplayPath.h"
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QList>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPoint>
#include <QPointF>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <cstdio>
#include <string>

// Sends drag and drop events to the window over the network view. Only a single local scene bundle may
// open, a drop that is refused must leave the open scene as it was, and a drop on an unsaved scene must
// ask first. The Recent Scenes menu must mark a scene that has been deleted as unavailable when it opens.
// It prints E2E_SCENE_DROP_OK and exits with 0, or prints the failures and exits with 1.
void MainWindowTestAccess::runSceneDrop(MainWindow& w) {
	bool ok = true;
	QStringList failures;
	const auto fail = [&](const QString& message) {
		ok = false;
		failures.append(message);
	};

	QTemporaryDir temp;
	if (!temp.isValid()) {
		fail(QStringLiteral("temporary directory creation failed"));
	} else {
		SceneModel droppedScene = w.m_document->mutableModel();
		droppedScene.name = "Dropped Scene";
		const QString validPath = temp.filePath(QStringLiteral("valid.EGSCENE"));
		if (!saveSceneBundle(droppedScene, validPath.toStdString()).success())
			fail(QStringLiteral("valid bundle creation failed"));

		QFile wrongExtension(temp.filePath(QStringLiteral("scene.txt")));
		if (!wrongExtension.open(QIODevice::WriteOnly) || wrongExtension.write("not a scene") < 0)
			fail(QStringLiteral("wrong-extension fixture creation failed"));
		wrongExtension.close();
		QFile malformed(temp.filePath(QStringLiteral("malformed.egscene")));
		if (!malformed.open(QIODevice::WriteOnly) || malformed.write("not a bundle") < 0)
			fail(QStringLiteral("malformed fixture creation failed"));
		malformed.close();
		const QString directoryPath = temp.filePath(QStringLiteral("directory"));
		if (!QDir().mkpath(directoryPath))
			fail(QStringLiteral("directory fixture creation failed"));

		const QPoint viewportPoint = w.networkView->viewport()->rect().center();
		const QPoint dropPoint = w.networkView->viewport()->mapTo(&w, viewportPoint);
		// This in-process check covers MainWindow's routing contract. Native file-manager
		// delivery over the viewport remains a GUI smoke-test residual.
		const auto deliver = [&](const QList<QUrl>& urls, bool hasUrls, bool dragAccepted,
								 bool dropAccepted, const QString& statusText,
								 Qt::DropActions actions = Qt::CopyAction) {
			QMimeData mime;
			if (hasUrls)
				mime.setUrls(urls);
			else
				mime.setText(QStringLiteral("no URLs"));
			QDragEnterEvent drag(dropPoint, actions, &mime, Qt::LeftButton, Qt::NoModifier);
			drag.setAccepted(false);
			QApplication::sendEvent(&w, &drag);
			if (drag.isAccepted() != dragAccepted)
				fail(QStringLiteral("unexpected drag acceptance for %1").arg(statusText));
			if (drag.isAccepted() && drag.dropAction() != Qt::CopyAction)
				fail(QStringLiteral("drag did not advertise copying for %1").arg(statusText));
			QDropEvent drop(QPointF(dropPoint), actions, &mime, Qt::LeftButton, Qt::NoModifier);
			drop.setAccepted(false);
			QApplication::sendEvent(&w, &drop);
			if (drop.isAccepted() != dropAccepted)
				fail(QStringLiteral("unexpected drop acceptance for %1").arg(statusText));
			if (drop.isAccepted() && drop.dropAction() != Qt::CopyAction)
				fail(QStringLiteral("drop did not complete as a copy for %1").arg(statusText));
			if (!statusText.isEmpty() && !w.statusBar()->currentMessage().contains(statusText, Qt::CaseInsensitive))
				fail(QStringLiteral("unclear drop result for %1: %2")
						.arg(statusText, w.statusBar()->currentMessage()));
		};

		if (!w.acceptDrops() || w.networkView->acceptDrops() || w.networkView->viewport()->acceptDrops())
			fail(QStringLiteral("central viewport does not route drops to MainWindow"));
		if (w.networkView->dragMode() != QGraphicsView::ScrollHandDrag)
			fail(QStringLiteral("canvas pan drag mode changed"));

		const quint64 revisionBeforeValidDrop = w.m_document->revision();
		deliver({QUrl::fromLocalFile(validPath)}, true, true, true, QString());
		if (w.m_session->m_sceneDir != QFileInfo(validPath).absoluteFilePath()
			|| w.m_document->mutableModel().name != "Dropped Scene" || w.m_document->revision() != revisionBeforeValidDrop + 1
			|| !w.scene || w.networkView->scene() != w.scene
			|| w.scene->items().isEmpty())
			fail(QStringLiteral("valid dropped scene did not open and render"));

		const QString scenePath = w.m_session->m_sceneDir;
		const std::string sceneName = w.m_document->mutableModel().name;
		const quint64 sceneRevision = w.m_document->revision();
		QGraphicsScene* const renderedScene = w.scene;
		const QList<QGraphicsItem*> renderedItems = w.scene ? w.scene->items() : QList<QGraphicsItem*>();
		const auto unchanged = [&](const QString& label) {
			const QList<QGraphicsItem*> currentItems = w.scene ? w.scene->items() : QList<QGraphicsItem*>();
			if (w.m_session->m_sceneDir != scenePath || w.m_document->mutableModel().name != sceneName
				|| w.m_document->revision() != sceneRevision || w.scene != renderedScene
				|| !w.scene || w.networkView->scene() != renderedScene
				|| currentItems.size() != renderedItems.size()
				|| !std::equal(currentItems.cbegin(), currentItems.cend(), renderedItems.cbegin()))
				fail(label + QStringLiteral(" replaced the current scene"));
		};

		bool canceledUnsavedPrompt = false;
		bool activatedBeforePrompt = false;
		int activations = 0;
		mainWindowTestHooks().windowBroughtToFront = [&activations](MainWindow&) { ++activations; };
		w.m_document->setDirty(true);
		QTimer cancelTimer(&w);
		cancelTimer.setInterval(0);
		QObject::connect(&cancelTimer, &QTimer::timeout, &w, [&]() {
			auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
			auto* cancel = box ? box->button(QMessageBox::Cancel) : nullptr;
			if (!cancel)
				return;
			canceledUnsavedPrompt = true;
			activatedBeforePrompt = activations > 0;
			cancelTimer.stop();
			cancel->click();
		});
		cancelTimer.start();
		deliver({QUrl::fromLocalFile(validPath)}, true, true, false, QStringLiteral("canceled"));
		cancelTimer.stop();
		if (!canceledUnsavedPrompt)
			fail(QStringLiteral("dirty-scene drop did not show the unsaved-scene prompt"));
		if (!activatedBeforePrompt)
			fail(QStringLiteral("the window was not raised and activated before the unsaved-scene prompt of a drop"));
		unchanged(QStringLiteral("canceled dirty-scene drop"));
		w.m_document->setDirty(false);

		// A drop that is refused asks nothing, so it leaves the window as it is.
		activations = 0;
		deliver({QUrl::fromLocalFile(directoryPath)}, true, false, false, QStringLiteral("directory"));
		if (activations != 0)
			fail(QStringLiteral("a refused drop raised the window"));
		mainWindowTestHooks().windowBroughtToFront = nullptr;

		deliver({QUrl::fromLocalFile(validPath)}, true, false, false,
			QStringLiteral("copying"), Qt::MoveAction);
		unchanged(QStringLiteral("move-only drop"));
		deliver({}, false, false, false, QStringLiteral("no file"));
		unchanged(QStringLiteral("no-URL drop"));
		deliver({QUrl::fromLocalFile(validPath), QUrl::fromLocalFile(validPath)}, true,
			false, false, QStringLiteral("exactly one"));
		unchanged(QStringLiteral("multiple-file drop"));
		deliver({QUrl(QStringLiteral("https://example.com/scene.egscene"))}, true,
			false, false, QStringLiteral("local"));
		unchanged(QStringLiteral("remote URL drop"));
		deliver({QUrl::fromLocalFile(directoryPath)}, true, false, false, QStringLiteral("directory"));
		unchanged(QStringLiteral("directory drop"));
		deliver({QUrl::fromLocalFile(wrongExtension.fileName())}, true,
			false, false, QStringLiteral("extension"));
		unchanged(QStringLiteral("wrong-extension drop"));
		deliver({QUrl::fromLocalFile(temp.filePath(QStringLiteral("missing.egscene")))}, true,
			false, false, QStringLiteral("does not exist"));
		unchanged(QStringLiteral("nonexistent-file drop"));
		deliver({QUrl::fromLocalFile(malformed.fileName())}, true,
			true, false, QStringLiteral("could not be opened"));
		unchanged(QStringLiteral("malformed-scene drop"));

		// A recent scene that is deleted must show as unavailable when the menu opens, without another open.
		// The name is unique to this run, because the list of recent scenes outlives the process and a second entry with the
		// same label would be told apart by its folder.
		const QString recentName = QStringLiteral("recent-") + QFileInfo(temp.path()).fileName();
		const QString recentPath = QFileInfo(temp.filePath(recentName + QStringLiteral(".egscene"))).absoluteFilePath();
		QAction* recentAction = nullptr;
		if (!QFile::copy(validPath, recentPath)) {
			fail(QStringLiteral("the recent scene fixture could not be created"));
		} else {
			w.m_session->addRecentScene(recentPath);
			for (QAction* action : w.m_session->recentScenesMenu()->actions()) {
				if (action->data().toString() == recentPath)
					recentAction = action;
			}
		}
		if (recentAction) {
			if (!recentAction->isEnabled() || recentAction->text().startsWith(QStringLiteral("Unavailable: ")))
				fail(QStringLiteral("a recent scene that exists is marked unavailable"));
			if (recentAction->text() != recentName)
				fail(QStringLiteral("a recent bundle is not labeled by its name without the extension: ") + recentAction->text());
			if (!QFile::remove(recentPath))
				fail(QStringLiteral("the recent scene fixture could not be deleted"));
			emit w.m_session->recentScenesMenu()->aboutToShow();
			if (recentAction->isEnabled() || !recentAction->text().startsWith(QStringLiteral("Unavailable: ")))
				fail(QStringLiteral("a deleted recent scene is not marked unavailable when the menu opens"));
			else if (recentAction->text() != QStringLiteral("Unavailable: ") + recentName)
				fail(QStringLiteral("an unavailable recent bundle is not labeled by its name without the extension: ") + recentAction->text());

			// The tip and the status tip show the path with the separators of Windows; the action keeps the stored path.
			const QChar savedSeparator = display_path::separator();
			display_path::separator() = QLatin1Char('\\');
			w.m_session->rebuildRecentScenesMenu();
			display_path::separator() = savedSeparator;
			QString shownPath = recentPath;
			shownPath.replace(QLatin1Char('/'), QLatin1Char('\\'));
			bool listed = false;
			for (const QAction* action : w.m_session->recentScenesMenu()->actions()) {
				if (action->data().toString() != recentPath)
					continue;
				listed = true;
				if (action->toolTip() != shownPath || action->statusTip() != shownPath)
					fail(QStringLiteral("a recent scene does not show its path with backslashes"));
			}
			if (!listed)
				fail(QStringLiteral("the recent scene is not in the rebuilt menu"));
		} else {
			fail(QStringLiteral("the recent scene is not in the menu"));
		}
	}

	if (ok)
		std::fprintf(stdout, "E2E_SCENE_DROP_OK\n");
	else
		std::fprintf(stderr, "E2E_SCENE_DROP_FAIL: %s\n", failures.join(", ").toUtf8().constData());
	std::fflush(ok ? stdout : stderr);
	QCoreApplication::exit(ok ? 0 : 1);
}
