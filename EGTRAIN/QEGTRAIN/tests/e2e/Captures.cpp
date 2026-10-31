#include "tests/e2e/MainWindowTestAccess.h"

#include "simulation/GuiSimulationSnapshot.h"
#include "app/MainWindowTestHooks.h"
#include "diagrams/DiagramWindow.h"
#include "diagrams/TrainKeyList.h"
#include "editors/CaseSettingsPanel.h"
#include "editors/CompositionPanel.h"
#include "editors/IncidentPanel.h"
#include "editors/InfrastructurePanel.h"
#include "editors/ScenarioPanel.h"
#include "editors/ServicePanel.h"
#include "editors/TimetableStopsPanel.h"
#include "editors/TrainUnitPanel.h"
#include "graphics/NetworkPresenter.h"
#include "graphics/NetworkScene.h"
#include "graphics/NetworkView.h"
#include "graphics/items/TrainBodyItem.h"
#include "graphics/items/TrainItemGroup.h"
#include "simulation/SimulationWorker.h"
#include "widgets/CaseChooserDialog.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QItemSelectionModel>
#include <QModelIndex>
#include <QGraphicsSceneContextMenuEvent>
#include <QImage>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QMenu>
#include <QMetaObject>
#include <QPainter>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QPointer>
#include <QPushButton>
#include <QRgb>
#include <QSize>
#include <QString>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <array>
#include <cstdlib>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <vector>

namespace {

// The size of a picture of a window. Dialogs keep the size they open with.
const QSize kWindowSize(1200, 750);
// The step at which the picture of a run in progress is taken, and the zoom of the view that follows a train.
constexpr int kProgressStep = 2450;
constexpr double kFollowZoom = 25.0;
// The trains that Speed / distance shows. With all of them the lines overlap so much that the order in which they are
// drawn, which changes from run to run, changes a tenth of the picture.
constexpr int kShownTrains = 6;
// The pause between two steps, and between two looks at a step that waits.
constexpr int kStepPauseMs = 300;
constexpr int kPollMs = 50;
// The longest the captures may take before the run fails.
constexpr int kTimeoutMs = 280000;
// A picture that differs from its reference in more than this share of its pixels is reported. Colour channels that
// differ by no more than kShadeTolerance count as the same.
constexpr double kChangedShare = 0.02;
constexpr int kShadeTolerance = 32;

// The steps of the capture run. A step returns true when it is done, and false when it waits and has to be called again.
struct CaptureRun {
	std::vector<std::function<bool()>> steps;
	std::size_t next = 0;
	bool finished = false;
	QString folder;
	QJsonArray manifest;
};

CaptureRun captureRun;

// Ends the run with the reason. It returns false, for a step that returns its result.
bool failCaptures(const QString& reason) {
	if (captureRun.finished)
		return false;
	captureRun.finished = true;
	std::fprintf(stderr, "E2E_CAPTURE_FAIL: %s\n", reason.toUtf8().constData());
	std::fflush(stderr);
	QCoreApplication::exit(2);
	return false;
}

// Lets layouts and queued paints catch up with what a step changed.
void settle() {
	for (int round = 0; round < 3; ++round) {
		QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
		QApplication::processEvents();
	}
}

// Saves a picture under its name in the capture folder and lists it in the manifest.
bool saveCapture(const QPixmap& picture, const QString& name, const QString& caseName, const QString& state) {
	if (picture.devicePixelRatio() != 1.0)
		return failCaptures(QStringLiteral("%1 has a device pixel ratio of %2").arg(name).arg(picture.devicePixelRatio()));
	if (!picture.save(QDir(captureRun.folder).filePath(name)))
		return failCaptures(QStringLiteral("cannot save ") + name);
	captureRun.manifest.append(QJsonObject{{"file", name}, {"case", caseName}, {"state", state}, {"width", picture.width()},
		{"height", picture.height()}, {"version", QCoreApplication::applicationVersion()}});
	std::fprintf(stdout, "E2E_CAPTURE_SAVED %s\n", name.toUtf8().constData());
	std::fflush(stdout);
	return true;
}

// The picture of a window with an open menu drawn on it at this place of the window. The menu itself stands elsewhere,
// because it keeps to the offscreen screen, which is smaller than the window.
QPixmap withMenu(QWidget& window, QMenu& menu, const QPoint& at) {
	QPixmap picture = window.grab();
	QPainter painter(&picture);
	painter.drawPixmap(at, menu.grab());
	return picture;
}

// The share of the pixels that differ between two pictures. A pixel is the same when the reference has a pixel of nearly
// its colour at its place or next to it: from run to run, a chart or a table can come out one pixel apart and draw its
// lines a shade lighter or darker. Pictures of different sizes differ everywhere.
double changedShare(const QImage& taken, const QImage& reference) {
	if (taken.size() != reference.size() || taken.isNull())
		return 1.0;
	const QImage a = taken.convertToFormat(QImage::Format_RGB32);
	const QImage b = reference.convertToFormat(QImage::Format_RGB32);
	const auto similar = [](QRgb p, QRgb q) {
		return std::abs(qRed(p) - qRed(q)) <= kShadeTolerance && std::abs(qGreen(p) - qGreen(q)) <= kShadeTolerance
			&& std::abs(qBlue(p) - qBlue(q)) <= kShadeTolerance;
	};
	const auto sameNearby = [&a, &b, similar](int x, int y) {
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
				if (b.valid(x + dx, y + dy) && similar(a.pixel(x, y), b.pixel(x + dx, y + dy)))
					return true;
		return false;
	};
	long long changed = 0;
	for (int y = 0; y < a.height(); ++y)
		for (int x = 0; x < a.width(); ++x)
			changed += sameNearby(x, y) ? 0 : 1;
	return static_cast<double>(changed) / (static_cast<double>(a.width()) * a.height());
}

// Compares the pictures with the ones that the manifest in the reference folder lists, other than those taken by hand. It
// prints a line for each picture that is missing, has another size than the manifest says, or differs in too many pixels.
bool compareWithReference(const QString& reference) {
	QFile file(QDir(reference).filePath(QStringLiteral("captures.json")));
	if (!file.open(QIODevice::ReadOnly))
		return failCaptures(QStringLiteral("cannot read ") + file.fileName());
	const QJsonArray entries = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("captures")).toArray();
	for (const QJsonValue& value : entries) {
		const QJsonObject entry = value.toObject();
		if (entry.contains(QStringLiteral("manual")))
			continue;
		const QString name = entry.value(QStringLiteral("file")).toString();
		const QImage taken(QDir(captureRun.folder).filePath(name));
		const QSize listed(entry.value(QStringLiteral("width")).toInt(), entry.value(QStringLiteral("height")).toInt());
		if (taken.isNull()) {
			std::fprintf(stdout, "E2E_CAPTURE_MISSING %s was not taken\n", name.toUtf8().constData());
		} else if (taken.size() != listed) {
			std::fprintf(stdout, "E2E_CAPTURE_SIZE %s is %dx%d px, captures.json says %dx%d\n", name.toUtf8().constData(),
				taken.width(), taken.height(), listed.width(), listed.height());
		} else {
			const double share = changedShare(taken, QImage(QDir(reference).filePath(name)));
			if (share > kChangedShare)
				std::fprintf(stdout, "E2E_CAPTURE_CHANGED %s differs in %.1f%% of its pixels\n", name.toUtf8().constData(),
					share * 100.0);
		}
	}
	std::fflush(stdout);
	return true;
}

void finishCaptures() {
	QFile file(QDir(captureRun.folder).filePath(QStringLiteral("captures.json")));
	if (!file.open(QIODevice::WriteOnly)
		|| file.write(QJsonDocument(QJsonObject{{"captures", captureRun.manifest}}).toJson()) < 0) {
		failCaptures(QStringLiteral("cannot write ") + file.fileName());
		return;
	}
	file.close();
	const QString reference = qEnvironmentVariable("QEGTRAIN_E2E_CAPTURE_COMPARE");
	if (!reference.isEmpty() && !compareWithReference(reference))
		return;
	captureRun.finished = true;
	std::fprintf(stdout, "E2E_CAPTURES_OK %d\n", static_cast<int>(captureRun.manifest.size()));
	std::fflush(stdout);
	QCoreApplication::exit(0);
}

void advanceCaptures(QWidget& window) {
	if (captureRun.finished)
		return;
	if (captureRun.next == captureRun.steps.size()) {
		finishCaptures();
		return;
	}
	const bool done = captureRun.steps[captureRun.next]();
	if (done)
		++captureRun.next;
	QTimer::singleShot(done ? kStepPauseMs : kPollMs, &window, [&window] { advanceCaptures(window); });
}

// Calls act with the dialog that find returns once it shows, from the event loop of the dialog. act has to close it. The
// timer stops once act has run, so a timer that still runs after the call that opens the dialog means it did not open.
QPointer<QTimer> whenShown(QWidget& window, const std::function<QDialog*()>& find, const std::function<void(QDialog&)>& act) {
	auto* poll = new QTimer(&window);
	QObject::connect(poll, &QTimer::timeout, &window, [poll, find, act] {
		QDialog* dialog = find();
		if (!dialog || !dialog->isVisible())
			return;
		poll->stop();
		settle();
		act(*dialog);
	});
	poll->start(25);
	return poll;
}

bool dialogOpened(QPointer<QTimer> poll, const QString& what) {
	const bool shown = poll && !poll->isActive();
	delete poll.data();
	if (!shown)
		failCaptures(what + QStringLiteral(" did not open"));
	return shown;
}

// Selects the tab with this text in each tab widget below root that has one.
void showTab(QWidget* root, const QString& text) {
	for (QTabWidget* tabs : root->findChildren<QTabWidget*>())
		for (int index = 0; index < tabs->count(); ++index)
			if (tabs->tabText(index) == text)
				tabs->setCurrentIndex(index);
	settle();
}

bool editLine(QLineEdit* edit, const QString& value) {
	if (!edit)
		return false;
	edit->setFocus(Qt::OtherFocusReason);
	edit->setText(value);
	QMetaObject::invokeMethod(edit, "editingFinished", Qt::DirectConnection);
	edit->clearFocus();
	settle();
	return edit->text() == value;
}

bool editNumber(QDoubleSpinBox* edit, double value) {
	if (!edit)
		return false;
	edit->setValue(value);
	QMetaObject::invokeMethod(edit, "editingFinished", Qt::DirectConnection);
	settle();
	return edit->value() == value;
}

// Chooses the item whose data is this text, or else the first whose text starts with it.
bool chooseText(QComboBox* combo, const QString& text) {
	int index = combo ? combo->findData(text) : -1;
	if (index < 0 && combo)
		index = combo->findText(text, Qt::MatchStartsWith);
	if (index < 0)
		return false;
	combo->setCurrentIndex(index);
	settle();
	return true;
}

int rowWithText(const QTableWidget* table, int column, const QString& text) {
	for (int row = 0; row < table->rowCount(); ++row)
		if (table->item(row, column) && table->item(row, column)->text() == text)
			return row;
	return -1;
}

} // namespace

// Takes the pictures of docs/images into the folder that QEGTRAIN_E2E_CAPTURES names, with captures.json, which lists for
// each its case, its state, its size and the version of the program. The run starts with Netherlands loaded, opens
// Paimpol from Open a Case, runs it, edits it as the tutorial on modifying a case does, then opens and runs Milano_Brescia
// and builds the case of the tutorial on the smallest scene. Nothing is saved to a case. With QEGTRAIN_E2E_CAPTURE_COMPARE
// it compares the pictures with the ones of that folder afterwards. The run prints E2E_CAPTURES_OK and exits with 0, or
// prints E2E_CAPTURE_FAIL with the reason and exits with 2.
void MainWindowTestAccess::runCaptures(MainWindow& w) {
	captureRun.folder = qEnvironmentVariable("QEGTRAIN_E2E_CAPTURES");
	if (!QDir().mkpath(captureRun.folder)) {
		failCaptures(QStringLiteral("cannot make the folder of QEGTRAIN_E2E_CAPTURES"));
		return;
	}
	QTimer::singleShot(kTimeoutMs, &w, [] {
		failCaptures(QStringLiteral("timed out at step %1").arg(captureRun.next));
	});
	const QString scenes = QFileInfo(w.m_session->m_sceneDir).absolutePath();

	const auto windowShot = [&w]() {
		w.resize(kWindowSize);
		settle();
		return w.grab();
	};
	// Shows these docks and Case and Layers, and hides the others.
	const auto showOnly = [&w](const QList<QDockWidget*>& docks) {
		for (QDockWidget* dock : w.findChildren<QDockWidget*>())
			if (dock->parentWidget() == &w)
				dock->setVisible(dock == w.m_caseLayersDock || docks.contains(dock));
		for (QDockWidget* dock : docks)
			dock->raise();
		settle();
	};
	const auto caseIs = [&w](const char* name) {
		return w.m_document->isLoaded() && w.m_document->model().name == name;
	};
	const auto runEnded = [&w]() { return !w.m_runController->running() && w.m_runController->resultsAvailable(); };
	// Follows the first train in the list of Follow whose name holds this text, and zooms in on it.
	const auto followTrain = [&w](const QString& text) {
		const int item = w.m_followTrainCombo->findText(text, Qt::MatchContains);
		if (item < 0)
			return false;
		const int train = w.m_followTrainCombo->itemData(item).toInt();
		w.setFollowTrain(train);
		w.networkView->zoomBy(kFollowZoom / w.networkView->zoomRatio());
		if (const TrainItemGroup* group = w.resolveTrainItem(train))
			w.networkView->centerOn(group->sceneBoundingRect().center());
		settle();
		return w.m_followAction->isChecked();
	};
	// Selects a row of the table of Infrastructure as a click does, so that Delete is enabled and the row has the colour of
	// a focused table, with no editor open on one of its cells. Clearing the current cell first closes an editor that an
	// earlier edit left open.
	const auto selectRow = [&w](int row) {
		QTableWidget* table = infrastructurePanel(w)->table();
		const QAbstractItemView::EditTriggers triggers = table->editTriggers();
		table->setEditTriggers(QAbstractItemView::NoEditTriggers);
		table->setCurrentIndex(QModelIndex());
		table->selectionModel()->setCurrentIndex(table->model()->index(row, 0),
			QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
		table->setFocus(Qt::MouseFocusReason);
		table->setEditTriggers(triggers);
		settle();
		return infrastructurePanel(w)->deleteButton()->isEnabled();
	};
	const auto facet = [&w](const char* name) {
		const int index = infrastructurePanel(w)->facetCombo()->findData(QString::fromLatin1(name));
		infrastructurePanel(w)->facetCombo()->setCurrentIndex(index);
		settle();
		return index >= 0;
	};
	const auto addRows = [&w, facet](const char* name, int count) {
		if (!facet(name))
			return false;
		for (int row = 0; row < count; ++row) {
			infrastructurePanel(w)->addButton()->click();
			settle();
		}
		return true;
	};
	const auto setCell = [&w](int row, int column, const QString& value) {
		QTableWidgetItem* item = infrastructurePanel(w)->table()->item(row, column);
		if (!item)
			return false;
		infrastructurePanel(w)->table()->setCurrentCell(row, column);
		item->setText(value);
		settle();
		return true;
	};
	// Adds a stop through the dialog Add Timetable Stop, with a planned arrival or departure when the time is not negative.
	const auto addStop = [&w](const QString& station, const QString& platform, int arrival, int departure) {
		bool chosen = false;
		const QPointer<QTimer> poll = whenShown(
			w, [&w] { return w.findChild<QDialog*>(QStringLiteral("stopEditorDialog")); },
			[&](QDialog& dialog) {
				chosen = chooseText(dialog.findChild<QComboBox*>(QStringLiteral("stopEditorStationCombo")), station)
					&& chooseText(dialog.findChild<QComboBox*>(QStringLiteral("stopEditorPlatformCombo")), platform);
				dialog.findChild<QCheckBox*>(QStringLiteral("stopEditorArrivalPresent"))->setChecked(arrival >= 0);
				dialog.findChild<QCheckBox*>(QStringLiteral("stopEditorDeparturePresent"))->setChecked(departure >= 0);
				if (arrival >= 0)
					dialog.findChild<QLineEdit*>(QStringLiteral("stopEditorArrivalEdit"))->setText(QString::number(arrival));
				if (departure >= 0)
					dialog.findChild<QLineEdit*>(QStringLiteral("stopEditorDepartureEdit"))->setText(QString::number(departure));
				dialog.findChild<QPushButton*>(QStringLiteral("stopEditorAcceptButton"))->click();
				if (dialog.isVisible())
					dialog.reject();
			});
		timetableStopsPanel(w)->addButton()->click();
		return dialogOpened(poll, QStringLiteral("Add Timetable Stop")) && chosen;
	};

	auto& steps = captureRun.steps;

	// Getting started: Open a Case over Netherlands, with Paimpol selected, and then the main window with Paimpol.
	steps.push_back([&w] {
		w.resize(kWindowSize);
		const QPointer<QTimer> poll = whenShown(
			w, [&w] { return w.findChild<QDialog*>(QStringLiteral("caseChooserDialog")); },
			[](QDialog& dialog) {
				auto& chooser = static_cast<CaseChooserDialog&>(dialog);
				const QList<QListWidgetItem*> rows = chooser.list()->findItems(QStringLiteral("Paimpol"), Qt::MatchExactly);
				if (rows.isEmpty()) {
					failCaptures(QStringLiteral("Open a Case lists no Paimpol"));
					dialog.reject();
					return;
				}
				chooser.list()->setCurrentItem(rows.first());
				settle();
				if (saveCapture(dialog.grab(), QStringLiteral("getting-started-open-a-case.png"), QStringLiteral("Netherlands"),
						QStringLiteral("Open a Case over the loaded case, with Paimpol selected")))
					chooser.openButton()->click();
				else
					dialog.reject();
			});
		w.showStartupChooser();
		return dialogOpened(poll, QStringLiteral("Open a Case"));
	});
	steps.push_back([caseIs] { return caseIs("Paimpol"); });
	steps.push_back([&w, windowShot] {
		w.fitView();
		return saveCapture(windowShot(), QStringLiteral("getting-started-main-window.png"), QStringLiteral("Paimpol"),
			QStringLiteral("loaded and not run, whole network in view"));
	});

	// The dialog Run simulation, and the run, which is paused for a moment at kProgressStep with Follow on the express.
	steps.push_back([&w] {
		mainWindowTestHooks().askRunReview = true;
		const QPointer<QTimer> poll = whenShown(
			w, [&w] { return w.findChild<QDialog*>(QStringLiteral("runReviewDialog")); },
			[](QDialog& dialog) {
				if (saveCapture(dialog.grab(), QStringLiteral("getting-started-run-simulation.png"), QStringLiteral("Paimpol"),
						QStringLiteral("Run simulation of baseline, Run details closed")))
					dialog.findChild<QPushButton*>(QStringLiteral("runReviewRunButton"))->click();
				else
					dialog.reject();
			});
		w.m_runController->runCurrent();
		mainWindowTestHooks().askRunReview = false;
		return dialogOpened(poll, QStringLiteral("Run simulation"));
	});
	steps.push_back([&w, followTrain] {
		SimulationWorker* worker = w.m_runController->worker();
		if (!worker || !w.m_snapshot)
			return false;
		const int step = w.m_snapshot->timestep;
		// The run slows down before the step, so that it pauses close to it.
		worker->setDelayMs(step >= kProgressStep - 30 ? 100 : 2);
		if (!w.m_followAction->isChecked())
			followTrain(QStringLiteral("EXPRESS"));
		if (step < kProgressStep)
			return false;
		worker->requestPause();
		return true;
	});
	steps.push_back([] { return true; }); // the follow camera comes to rest
	steps.push_back([&w, windowShot] {
		SimulationWorker* worker = w.m_runController->worker();
		if (!worker)
			return failCaptures(QStringLiteral("the run of Paimpol ended before it was paused"));
		const bool saved = saveCapture(windowShot(), QStringLiteral("getting-started-run-in-progress.png"), QStringLiteral("Paimpol"),
			QStringLiteral("run of baseline held near step %1, Follow on the express").arg(kProgressStep));
		worker->setDelayMs(0);
		worker->requestResume();
		return saved;
	});
	steps.push_back(runEnded);

	// The results: the menu Open result view of Run Results, and the context menu of a train in the replay at 3500 s.
	steps.push_back([&w, showOnly] {
		showOnly({w.m_runResultsPanel->dock()});
		w.fitView();
		auto* button = w.findChild<QToolButton*>(QStringLiteral("runResultsViewsButton"));
		if (!button || !button->menu())
			return failCaptures(QStringLiteral("Run Results has no Open result view"));
		button->menu()->popup(button->mapToGlobal(QPoint(0, button->height())));
		settle();
		// The menu opens above the button, as it does when there is no room below.
		const QPoint above = button->mapTo(&w, QPoint(0, -button->menu()->height()));
		const bool saved = saveCapture(withMenu(w, *button->menu(), above), QStringLiteral("getting-started-run-results.png"), QStringLiteral("Paimpol"),
			QStringLiteral("completed run of baseline, Open result view menu open"));
		button->menu()->close();
		return saved;
	});
	steps.push_back([&w, followTrain] {
		w.m_replaySlider->setValue(3500);
		settle();
		return followTrain(QStringLiteral("ALL-STOPS")) || failCaptures(QStringLiteral("Follow does not offer the stopping train"));
	});
	steps.push_back([&w] {
		const TrainBodyItem* body = nullptr;
		for (const TrainItemGroup* train : w.m_presenter->runTrains())
			if (train && train->isVisible() && train->trainPolygonItemList
				&& QString::fromStdString(train->trainDescription).contains(QStringLiteral("ALL-STOPS")))
				for (const TrainBodyItem* candidate : *train->trainPolygonItemList)
					if (!body && candidate->isVisible())
						body = candidate;
		if (!body)
			return failCaptures(QStringLiteral("the stopping train of Paimpol is not drawn at 3500 s of the replay"));
		const QPointF center = body->sceneBoundingRect().center();
		QGraphicsSceneContextMenuEvent event(QEvent::GraphicsSceneContextMenu);
		event.setReason(QGraphicsSceneContextMenuEvent::Mouse);
		event.setScenePos(center);
		// The menu opens a little below and right of the train, so that the train stays in view.
		const QPoint below = w.networkView->mapFromScene(center) + QPoint(12, 12);
		event.setScreenPos(w.networkView->viewport()->mapToGlobal(below));
		event.setWidget(w.networkView->viewport());
		w.scene->contextMenuEvent(&event);
		settle();
		if (!w.m_sceneContextMenu)
			return failCaptures(QStringLiteral("the stopping train has no context menu"));
		const bool saved = saveCapture(withMenu(w, *w.m_sceneContextMenu, w.networkView->viewport()->mapTo(&w, below)),
			QStringLiteral("how-to-replay-menu.png"), QStringLiteral("Paimpol"),
			QStringLiteral("replay at 3500 s, context menu of the stopping train open"));
		w.m_sceneContextMenu->close();
		return saved;
	});

	// Modify a case: the case gets the name of the copy, then the stop Pontrieaux of the express in Edit Timetable Stop,
	// whose dwell becomes 180 s, and the warning that this gives in Case Validation.
	steps.push_back([&w] {
		if (!editLine(caseSettingsPanel(w)->nameEdit(), QStringLiteral("Paimpol practice")))
			return failCaptures(QStringLiteral("the case could not be named Paimpol practice"));
		w.revealEditorDock(serviceDock(w));
		const QList<QListWidgetItem*> services = servicePanel(w)->listWidget()->findItems(QStringLiteral("Guin-Paim-EXPRESS-1"), Qt::MatchExactly);
		if (services.isEmpty())
			return failCaptures(QStringLiteral("Services lists no Guin-Paim-EXPRESS-1"));
		servicePanel(w)->listWidget()->setCurrentItem(services.first());
		showTab(serviceDock(w), QStringLiteral("Timetable"));
		const int row = rowWithText(timetableStopsPanel(w)->table(), 0, QStringLiteral("Pontrieaux"));
		if (row < 0)
			return failCaptures(QStringLiteral("the express has no stop Pontrieaux"));
		const QPointer<QTimer> poll = whenShown(
			w, [&w] { return w.findChild<QDialog*>(QStringLiteral("stopEditorDialog")); },
			[](QDialog& dialog) {
				if (saveCapture(dialog.grab(), QStringLiteral("tutorial-modify-edit-timetable-stop.png"), QStringLiteral("Paimpol"),
						QStringLiteral("Edit Timetable Stop for Pontrieaux of Guin-Paim-EXPRESS-1, as committed"))) {
					dialog.findChild<QLineEdit*>(QStringLiteral("stopEditorDwellEdit"))->setText(QStringLiteral("180"));
					dialog.findChild<QPushButton*>(QStringLiteral("stopEditorAcceptButton"))->click();
				}
				if (dialog.isVisible())
					dialog.reject();
			});
		timetableStopsPanel(w)->editStop(row);
		return dialogOpened(poll, QStringLiteral("Edit Timetable Stop"));
	});
	steps.push_back([&w, showOnly, windowShot] {
		showOnly({w.m_validationDock});
		return saveCapture(windowShot(), QStringLiteral("how-to-scene-validation.png"), QStringLiteral("Paimpol"),
			QStringLiteral("Case Validation after the dwell at Pontrieaux became 180 s"));
	});

	// Modify a case: the scenario Express breakdown with its incident, after a run of baseline that is the delay baseline.
	steps.push_back([&w, showOnly] {
		showOnly({incidentDock(w), w.m_runResultsPanel->dock()});
		w.revealEditorDock(incidentDock(w));
		scenarioPanel(w)->listWidget()->setCurrentRow(0);
		scenarioPanel(w)->duplicateButton()->click();
		settle();
		bool ok = editLine(scenarioPanel(w)->nameEdit(), QStringLiteral("Express breakdown"));
		showTab(incidentDock(w), QStringLiteral("Incidents"));
		incidentPanel(w)->addButton()->click();
		settle();
		ok = ok && chooseText(incidentPanel(w)->typeCombo(), QStringLiteral("Train breakdown"))
			&& chooseText(incidentPanel(w)->targetCombo(), QStringLiteral("Guin-Paim-EXPRESS-1-1 (route route6, +10 s)"))
			&& editNumber(incidentPanel(w)->startSecondsEdit(), 900);
		incidentPanel(w)->hasEndSecondsCheck()->setChecked(true);
		ok = ok && editNumber(incidentPanel(w)->endSecondsEdit(), 1200);
		if (!ok)
			return failCaptures(QStringLiteral("the incident of Express breakdown could not be entered"));
		// The tutorial saves the case here. Saving is not needed for the pictures, and the case on disk stays as it is.
		w.m_runSelection.clearModifiedScenarios();
		w.m_document->markSaved();
		scenarioPanel(w)->listWidget()->setCurrentRow(0);
		settle();
		w.m_runController->runCurrent();
		return true;
	});
	steps.push_back(runEnded);
	steps.push_back([&w, showOnly, windowShot] {
		w.m_runController->setDelayBaseline();
		scenarioPanel(w)->listWidget()->setCurrentRow(1);
		showOnly({incidentDock(w), w.m_runResultsPanel->dock()});
		showTab(incidentDock(w), QStringLiteral("Scenario"));
		return saveCapture(windowShot(), QStringLiteral("tutorial-modify-incident-baseline.png"), QStringLiteral("Paimpol"),
			QStringLiteral("Express breakdown selected after a run of baseline set the delay baseline"));
	});

	// The README: Milano_Brescia before a run, and its Speed / distance window after one.
	steps.push_back([&w, scenes] {
		w.m_document->markSaved();
		return w.openSceneDirectory(QDir(scenes).filePath(QStringLiteral("Milano_Brescia"))) || failCaptures(QStringLiteral("cannot open Milano_Brescia"));
	});
	steps.push_back([caseIs] { return caseIs("Milano_Brescia"); });
	// The picture of the README is taken by hand, with a run on screen.
	steps.push_back([&w, showOnly] {
		showOnly({});
		w.m_runController->runCurrent();
		return true;
	});
	steps.push_back(runEnded);
	steps.push_back([&w] {
		w.buildPerTrainDiagram(0);
		const QList<DiagramWindow*> diagrams = w.findChildren<DiagramWindow*>();
		if (diagrams.isEmpty())
			return failCaptures(QStringLiteral("Speed / distance did not open"));
		// The window would keep to the offscreen screen, which is 800 by 600 pixels.
		DiagramWindow* diagram = diagrams.last();
		auto* trains = diagram->findChild<TrainKeyList*>();
		if (!trains)
			return failCaptures(QStringLiteral("Speed / distance has no list of trains"));
		const QStringList ids = trains->trainIds();
		for (int row = kShownTrains; row < ids.size(); ++row)
			trains->setTrainVisible(ids[row], false);
		diagram->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
		diagram->resize(kWindowSize);
		settle();
		const bool saved = saveCapture(diagram->grab(), QStringLiteral("case-studies-speed-distance.png"), QStringLiteral("Milano_Brescia"),
			QStringLiteral("Speed / distance of the first six trains after a run of baseline"));
		diagram->close();
		return saved;
	});

	// The smallest scene: a new case, built as the tutorial builds it.
	steps.push_back([&w] {
		w.m_document->markSaved();
		w.m_newSceneAction->trigger();
		return true;
	});
	steps.push_back([&w, showOnly, windowShot, addRows, selectRow] {
		const bool built = editLine(caseSettingsPanel(w)->nameEdit(), QStringLiteral("Minimal line")) && addRows("tracks", 1) && addRows("nodes", 3);
		showOnly({infrastructureDock(w)});
		return built && selectRow(2)
			? saveCapture(windowShot(), QStringLiteral("tutorial-minimal-nodes.png"), QStringLiteral("Minimal line"),
				  QStringLiteral("new case with one track and three nodes, Infrastructure on Nodes"))
			: failCaptures(QStringLiteral("the track and nodes of Minimal line could not be added and selected"));
	});
	steps.push_back([&w, windowShot, facet, addRows, setCell, selectRow] {
		bool built = addRows("arcs", 2) && facet("blocks");
		const int track = infrastructurePanel(w)->blockTrackFilterCombo()->findData(QStringLiteral("track"));
		infrastructurePanel(w)->blockTrackFilterCombo()->setCurrentIndex(track);
		built = built && track >= 0 && addRows("blocks", 2) && addRows("stations", 2) && addRows("platforms", 2)
			&& setCell(1, 0, QStringLiteral("station_2")) && setCell(0, 2, QStringLiteral("node")) && setCell(1, 2, QStringLiteral("node_3"));
		return built && selectRow(1)
			? saveCapture(windowShot(), QStringLiteral("tutorial-minimal-platforms.png"), QStringLiteral("Minimal line"),
				  QStringLiteral("two stations with a platform at each end node, Infrastructure on Platforms"))
			: failCaptures(QStringLiteral("the arcs, blocks, stations and platforms of Minimal line could not be added and selected"));
	});
	steps.push_back([&w, addRows] {
		bool built = addRows("signalling_areas", 1);
		auto* system = qobject_cast<QComboBox*>(infrastructurePanel(w)->table()->cellWidget(0, 3));
		built = built && system && chooseText(system, QStringLiteral("0 ATB fixed block")) && addRows("routes", 1);
		for (const QString& section : {QStringLiteral("track 0.000 to 1.000 km"), QStringLiteral("track 1.000 to 2.000 km")}) {
			built = built && chooseText(infrastructurePanel(w)->routeSectionCatalogCombo(), section);
			infrastructurePanel(w)->addRouteSectionButton()->click();
			settle();
		}
		return built || failCaptures(QStringLiteral("the signalling and route of Minimal line could not be added"));
	});
	// The placeholder unit SLT_Sprinter of Assignment_Gvc_Gdg_Ut, as the tutorial copies it.
	steps.push_back([&w] {
		w.revealEditorDock(trainUnitDock(w));
		trainUnitPanel(w)->addButton()->click();
		settle();
		const std::array<double, 9> physical = {151000, 0, 0, 36.111111111111, 0.75, 1.45, 0.004, 0.75, 70};
		for (std::size_t field = 0; field < trainUnitPanel(w)->physicalEdits().size(); ++field)
			trainUnitPanel(w)->physicalEdits()[field]->setValue(physical[field]);
		const std::array<std::array<double, 5>, 2> traction = {
			{{0, 8.611111111111, 209000, 0, 0}, {8.611111111111, 36.111111111111, 324607.2915, -17671.4923, 292.9005}}};
		for (int row = 0; row < 2; ++row) {
			trainUnitPanel(w)->addTractionRowButton()->click();
			settle();
			for (int column = 0; column < 5; ++column) {
				QTableWidgetItem* tractionCell = trainUnitPanel(w)->tractionTable()->item(row, column);
				if (!tractionCell)
					return failCaptures(QStringLiteral("the traction table of new_train_unit has no row %1").arg(row + 1));
				tractionCell->setData(Qt::EditRole, traction[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)]);
			}
		}
		settle();
		w.revealEditorDock(compositionDock(w));
		compositionPanel(w)->addButton()->click();
		settle();
		const QPointer<QTimer> poll = whenShown(
			w, [] { return qobject_cast<QDialog*>(QApplication::activeModalWidget()); },
			[](QDialog& dialog) {
				auto* input = qobject_cast<QInputDialog*>(&dialog);
				if (!input) {
					dialog.reject();
					return;
				}
				input->setTextValue(QStringLiteral("new_train_unit"));
				dialog.accept();
			});
		compositionPanel(w)->addUnitButton()->click();
		return dialogOpened(poll, QStringLiteral("Add Rolling Stock Unit"));
	});
	steps.push_back([&w, showOnly, windowShot, addStop] {
		w.revealEditorDock(serviceDock(w));
		servicePanel(w)->addButton()->click();
		settle();
		if (!chooseText(servicePanel(w)->compositionCombo(), QStringLiteral("new_composition"))
			|| !chooseText(servicePanel(w)->routeCombo(), QStringLiteral("route")))
			return failCaptures(QStringLiteral("new_service cannot take new_composition and route"));
		showTab(serviceDock(w), QStringLiteral("Timetable"));
		if (!addStop(QStringLiteral("station"), QStringLiteral("platform"), -1, 120)
			|| !addStop(QStringLiteral("station_2"), QStringLiteral("platform_2"), 250, -1))
			return failCaptures(QStringLiteral("the stops of new_service could not be added"));
		showOnly({trainUnitDock(w), compositionDock(w), serviceDock(w)});
		w.resizeDocks({serviceDock(w)}, {760}, Qt::Horizontal);
		return saveCapture(windowShot(), QStringLiteral("tutorial-minimal-service.png"), QStringLiteral("Minimal line"),
			QStringLiteral("the finished case, Services on the timetable of new_service"));
	});

	advanceCaptures(w);
}
