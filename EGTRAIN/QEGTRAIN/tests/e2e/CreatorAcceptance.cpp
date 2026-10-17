#include "tests/e2e/MainWindowTestAccess.h"

#include "app/GuiSimulationSnapshot.h"
#include "app/MainWindowSupport.h"
#include "ui_MainWindow.h"

#include "diagrams/AreaCapacityWindow.h"
#include "diagrams/DelayAnalysisWindow.h"
#include "diagrams/DiagramWindow.h"
#include "diagrams/RunResults.h"
#include "diagrams/TimetableTableWindow.h"
#include "editors/InfrastructurePanel.h"
#include "editors/TimetableStopsPanel.h"
#include "editors/ValidationPanel.h"
#include "graphics/NetworkScene.h"
#include "graphics/VisualPolish.h"
#include "graphics/items/ConnectionItem.h"
#include "graphics/items/NodeItem.h"
#include "graphics/items/PlatformItem.h"
#include "graphics/items/SignalItem.h"
#include "graphics/items/StationNodeItem.h"
#include "graphics/items/StationOverlayItem.h"
#include "graphics/items/TrackLineItem.h"
#include "scene/SceneBundle.h"
#include "scene/SceneDiagnostic.h"
#include "scene/SceneModel.h"
#include "scene/SignallingLevelNames.h"
#include <QAbstractButton>
#include <QAbstractGraphicsShapeItem>
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QGraphicsItem>
#include <QGraphicsLineItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsTextItem>
#include <QGuiApplication>
#include <QIODevice>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLineF>
#include <QList>
#include <QListWidget>
#include <QMessageBox>
#include <QMetaObject>
#include <QObject>
#include <QPen>
#include <QPlainTextEdit>
#include <QPoint>
#include <QPointF>
#include <QPointer>
#include <QPolygonF>
#include <QPushButton>
#include <QRect>
#include <QRectF>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QString>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTransform>
#include <QTreeWidgetItem>
#include <QWidget>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace {

// Where the scenario stands between two polls.
struct CreatorAcceptanceState {
	bool finished = false;
	int phase = 0;
	int polls = 0;
	QPointer<DiagramWindow> baselineDiagram;
	std::string baselineCsv; // What the baseline diagram exported when it opened
	QString baselineContext; // What the context line of the baseline diagram said when it opened
};

CreatorAcceptanceState creatorAcceptance;

// Runs the scenario again after delayMs.
void pollAgain(MainWindow& w, int delayMs) {
	QTimer::singleShot(delayMs, &w, [&w]() { MainWindowTestAccess::runCreatorAcceptance(w); });
}

} // namespace

// Builds a case through the controls of a window that starts without a scene: a new case, tracks, nodes, arcs and
// blocks, stations, platforms and signalling, rolling stock, a service, scenarios and a passenger. It saves the case
// to the folder named by QEGTRAIN_E2E_CREATOR_FOLDER and to the bundle named by QEGTRAIN_E2E_CREATOR_BUNDLE, opens
// both again, runs the baseline, entrance delay and incident scenarios, and exports every result view to
// QEGTRAIN_E2E_CREATOR_EXPORT_DIR. QEGTRAIN_E2E_OUT names a folder for the files that it writes besides. One call
// runs one stage and schedules the next, and a stage that waits for a run polls. Every stage prints its
// E2E_CREATOR_*_OK marker. The scenario prints E2E_CREATOR_ACCEPTANCE_OK and exits with 0, or prints
// E2E_CREATOR_ACCEPTANCE_FAIL with the reason and exits with 2. With QEGTRAIN_E2E_PREVIEW_ONLY it exits with 0 after
// the checks of the preview.
void MainWindowTestAccess::runCreatorAcceptance(MainWindow& w) {
	if (creatorAcceptance.finished)
		return;

	auto marker = [](const char* name) {
		std::fprintf(stdout, "%s\n", name);
		std::fflush(stdout);
	};
	auto fail = [](const QString& message) {
		if (creatorAcceptance.finished)
			return;
		creatorAcceptance.finished = true;
		std::fprintf(stderr, "E2E_CREATOR_ACCEPTANCE_FAIL: %s\n", message.toStdString().c_str());
		std::fflush(stderr);
		QCoreApplication::exit(2);
	};
	auto next = [&w]() {
		++creatorAcceptance.phase;
		pollAgain(w, 75);
	};
	auto editStopDialog = [&w](int row, const std::function<void(QDialog*)>& edit) {
		bool accepted = false;
		QTimer timer;
		timer.setSingleShot(true);
		QObject::connect(&timer, &QTimer::timeout, &w, [&]() {
			auto* dialog = w.findChild<QDialog*>("stopEditorDialog");
			if (!dialog) return;
			edit(dialog);
			dialog->findChild<QPushButton*>("stopEditorAcceptButton")->click();
			accepted = dialog->result() == QDialog::Accepted;
			if (!accepted) dialog->reject();
		});
		timer.start(0);
		if (row < 0) w.m_addStopButton->click();
		else w.m_timetableStopsPanel->editStop(row);
		return accepted;
	};
	auto process = []() { QApplication::processEvents(); };
	// The context line that the charts of a run show under their toolbar. Every creator run applies a scenario (baseline,
	// entrance or incident), so the line has both parts.
	auto chartContextLine = [](const RunProvenance& run) {
		return QStringLiteral("Run: %1 / %2").arg(QString::fromStdString(run.caseName), QString::fromStdString(run.appliedScenario));
	};
	auto editLine = [process](QLineEdit* edit, const QString& value) {
		if (!edit)
			return false;
		edit->setFocus(Qt::OtherFocusReason);
		edit->setText(value);
		QMetaObject::invokeMethod(edit, "editingFinished", Qt::DirectConnection);
		edit->clearFocus();
		process();
		return edit->text() == value;
	};
	auto choose = [process](QComboBox* combo, const QString& value) {
		if (!combo)
			return false;
		int index = combo->findData(value);
		if (index < 0)
			index = combo->findText(value);
		if (index < 0)
			return false;
		combo->setCurrentIndex(index);
		process();
		return combo->currentIndex() == index;
	};
	// The delay analysis window that opened last and is still shown.
	auto newestDelayWindow = []() -> DelayAnalysisWindow* {
		DelayAnalysisWindow* newest = nullptr;
		for (QWidget* widget : QApplication::topLevelWidgets())
			if (auto* window = dynamic_cast<DelayAnalysisWindow*>(widget); window && window->isVisible())
				newest = window;
		return newest;
	};
	auto acceptFileDialog = [&w, fail](const QString& path, bool directory) {
		auto poll = std::make_shared<std::function<void()>>();
		auto attempts = std::make_shared<int>(0);
		auto seen = std::make_shared<bool>(false);
		*poll = [&w, path, directory, poll, attempts, seen, fail]() {
			if (++*attempts > 200) {
				fail(QStringLiteral("file dialog did not appear for ") + path);
				return;
			}
			QFileDialog* fileDialog = nullptr;
			for (QWidget* widget : QApplication::topLevelWidgets()) {
				auto* dialog = qobject_cast<QFileDialog*>(widget);
				if (!dialog || !dialog->isVisible())
					continue;
				fileDialog = dialog;
				break;
			}
			if (!fileDialog) {
				if (*seen)
					return;
				QTimer::singleShot(25, &w, [poll]() { (*poll)(); });
				return;
			}
			*seen = true;
			if (directory) {
				fileDialog->setDirectory(path);
				static_cast<QDialog*>(fileDialog)->done(QDialog::Accepted);
			} else {
				const QFileInfo target(path);
				fileDialog->setDirectory(target.absolutePath());
				fileDialog->selectFile(target.fileName());
				if (QLineEdit* fileName = fileDialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit")))
					fileName->setText(target.fileName());
				QMetaObject::invokeMethod(fileDialog, "accept", Qt::DirectConnection);
			}
			QTimer::singleShot(25, &w, [poll]() { (*poll)(); });
		};
		QTimer::singleShot(0, &w, [poll]() { (*poll)(); });
	};
	auto acceptInputDialog = [&w, fail](const QString& value) {
		auto poll = std::make_shared<std::function<void()>>();
		auto attempts = std::make_shared<int>(0);
		*poll = [&w, value, poll, attempts, fail]() {
			if (++*attempts > 200) {
				fail(QStringLiteral("input dialog did not appear"));
				return;
			}
			for (QWidget* widget : QApplication::topLevelWidgets()) {
				auto* dialog = qobject_cast<QInputDialog*>(widget);
				if (!dialog || !dialog->isVisible())
					continue;
				dialog->setTextValue(value);
				dialog->accept();
				return;
			}
			QTimer::singleShot(25, &w, [poll]() { (*poll)(); });
		};
		QTimer::singleShot(0, &w, [poll]() { (*poll)(); });
	};
	auto acceptMessageBox = [&w, fail](QMessageBox::StandardButton desired = QMessageBox::Ok) {
		auto poll = std::make_shared<std::function<void()>>();
		auto attempts = std::make_shared<int>(0);
		*poll = [&w, desired, poll, attempts, fail]() {
			if (++*attempts > 200) {
				fail(QStringLiteral("message box did not appear"));
				return;
			}
			for (QWidget* widget : QApplication::topLevelWidgets()) {
				auto* dialog = qobject_cast<QMessageBox*>(widget);
				if (!dialog || !dialog->isVisible())
					continue;
				if (QAbstractButton* button = dialog->button(desired))
					button->click();
				else
					dialog->accept();
				return;
			}
			QTimer::singleShot(25, &w, [poll]() { (*poll)(); });
		};
		QTimer::singleShot(0, &w, [poll]() { (*poll)(); });
	};
	auto acceptCapacityScope = [&w, fail]() {
		auto poll = std::make_shared<std::function<void()>>();
		auto attempts = std::make_shared<int>(0);
		*poll = [&w, poll, attempts, fail]() {
			if (++*attempts > 200) {
				fail(QStringLiteral("capacity scope dialog did not appear"));
				return;
			}
			for (QWidget* widget : QApplication::topLevelWidgets()) {
				if (auto* message = qobject_cast<QMessageBox*>(widget); message && message->isVisible()) {
					const QString detail = message->text();
					message->accept();
					fail(QStringLiteral("capacity analysis rejected the creator run: ") + detail);
					return;
				}
				auto* dialog = qobject_cast<QDialog*>(widget);
				if (!dialog || !dialog->isVisible() || !dialog->isModal()
					|| dialog->windowTitle() != QStringLiteral("Capacity analysis"))
					continue;
				QComboBox* cycleEnd = nullptr;
				for (QComboBox* combo : dialog->findChildren<QComboBox*>())
					if (combo->count() >= 3
						&& combo->itemText(0).startsWith(QStringLiteral("Select the first train")))
						cycleEnd = combo;
				QDialogButtonBox* buttons = dialog->findChild<QDialogButtonBox*>();
				if (!cycleEnd || !buttons || !buttons->button(QDialogButtonBox::Ok)) {
					fail(QStringLiteral("capacity scope controls are incomplete"));
					return;
				}
				cycleEnd->setCurrentIndex(2);
				buttons->button(QDialogButtonBox::Ok)->click();
				// A refusal keeps the dialog open and says why in its status label.
				if (dialog->isVisible()) {
					const auto* status = dialog->findChild<QLabel*>("capacityScopeStatus");
					const QString detail = status ? status->text() : QString();
					dialog->reject();
					fail(QStringLiteral("capacity analysis rejected the creator run: ") + detail);
				}
				return;
			}
			QTimer::singleShot(25, &w, [poll]() { (*poll)(); });
		};
		QTimer::singleShot(0, &w, [poll]() { (*poll)(); });
	};
	auto acceptAreaCapacityScope = [&w, fail]() {
		auto poll = std::make_shared<std::function<void()>>();
		auto attempts = std::make_shared<int>(0);
		*poll = [&w, poll, attempts, fail]() {
			if (++*attempts > 200) {
				fail(QStringLiteral("capacity by area options did not appear"));
				return;
			}
			for (QWidget* widget : QApplication::topLevelWidgets()) {
				if (auto* message = qobject_cast<QMessageBox*>(widget); message && message->isVisible()) {
					const QString detail = message->text();
					message->accept();
					fail(QStringLiteral("capacity by area rejected the creator run: ") + detail);
					return;
				}
				auto* dialog = dynamic_cast<AreaCapacityScopeDialog*>(widget);
				if (!dialog || !dialog->isVisible())
					continue;
				QDialogButtonBox* buttons = dialog->findChild<QDialogButtonBox*>();
				QPushButton* ok = buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
				if (!ok || !ok->isEnabled()) {
					fail(QStringLiteral("capacity by area options do not accept the creator run"));
					return;
				}
				ok->click();
				return;
			}
			QTimer::singleShot(25, &w, [poll]() { (*poll)(); });
		};
		QTimer::singleShot(0, &w, [poll]() { (*poll)(); });
	};
	auto emitPath = [](const QString& value) { return QFileInfo(value).absoluteFilePath(); };
	auto facet = [&](const char* name) {
		const int index = w.m_infrastructureFacetCombo->findData(QString::fromLatin1(name));
		if (index < 0)
			return false;
		w.m_infrastructureFacetCombo->setCurrentIndex(index);
		process();
		return w.m_infrastructureFacetCombo->currentData().toString() == QString::fromLatin1(name);
	};
	auto addRow = [&](const char* name) {
		if (!facet(name) || !w.m_addInfrastructureButton->isEnabled())
			return false;
		w.m_addInfrastructureButton->click();
		process();
		return w.m_infrastructureTable->rowCount() > 0;
	};
	auto rowFor = [&](const QString& id) {
		for (int row = 0; row < w.m_infrastructureTable->rowCount(); ++row)
			if (w.m_infrastructureTable->item(row, 0)
				&& w.m_infrastructureTable->item(row, 0)->text() == id)
				return row;
		return -1;
	};
	auto setCell = [&](const char* name, const QString& id, int column, const QString& value) {
		if (!facet(name))
			return false;
		const int row = rowFor(id);
		if (row < 0 || !w.m_infrastructureTable->item(row, column))
			return false;
		w.m_infrastructureTable->setCurrentCell(row, column);
		w.m_infrastructureTable->item(row, column)->setText(value);
		process();
		return true;
	};
	auto setSection = [&](const char* name, int row, int column, const QString& prefix) {
		if (!facet(name))
			return QString();
		auto* combo = qobject_cast<QComboBox*>(w.m_infrastructureTable->cellWidget(row, column));
		if (!combo)
			return QString();
		for (int index = 0; index < combo->count(); ++index) {
			if (combo->itemData(index).toString().isEmpty()
				|| (!prefix.isEmpty() && !combo->itemText(index).startsWith(prefix)))
				continue;
			combo->setCurrentIndex(index);
			process();
			return combo->itemData(index).toString();
		}
		return QString();
	};

	if (creatorAcceptance.phase == 0) {
		// The first observable operation in this mode is the public New action.
		if (!w.m_newSceneAction) {
			fail(QStringLiteral("actionNewCaseStudy is unavailable"));
			return;
		}
		w.m_newSceneAction->trigger();
		marker("E2E_CREATOR_NEW_CASE_OK");
		next();
		return;
	}

	if (creatorAcceptance.phase == 1) {
		if (!w.m_document->isLoaded() || !w.m_infrastructureFacetCombo || !w.m_infrastructureTable
			|| !w.m_addInfrastructureButton || !w.m_caseSettingsDock) {
			fail(QStringLiteral("blank case or creator controls did not load"));
			return;
		}
		w.m_caseSettingsDock->show();
		w.m_caseSettingsDock->raise();
		if (!editLine(w.m_caseNameEdit, QStringLiteral("creator_acceptance_case"))
			|| !editLine(w.m_caseDescriptionEdit, QStringLiteral("synthetic creator acceptance"))
			|| !editLine(w.m_caseBaseTimeEdit, QStringLiteral("09:15:30"))) {
			fail(QStringLiteral("case metadata editing did not commit"));
			return;
		}
		w.m_caseDurationSecondsEdit->setValue(900.0);
		w.m_caseBufferSecondsEdit->setValue(30.0);
		w.m_caseRecoveryPercentEdit->setValue(5.0);
		process();

		if (!addRow("tracks")) {
			fail(QStringLiteral("track add 1 failed"));
			return;
		}
		if (!facet("tracks") || w.m_infrastructureTable->rowCount() < 1
			|| !w.m_infrastructureTable->item(0, 0)) {
			fail(QStringLiteral("track row 1 unavailable"));
			return;
		}
		w.m_infrastructureTable->item(0, 0)->setText("creator-main");
		process();
		if (w.m_infrastructureTable->item(0, 0)->text() != "creator-main") {
			fail(QStringLiteral("track rename 1 failed"));
			return;
		}
		if (!addRow("tracks")) {
			fail(QStringLiteral("track add 2 failed"));
			return;
		}
		if (!facet("tracks") || w.m_infrastructureTable->rowCount() < 2
			|| !w.m_infrastructureTable->item(1, 0)) {
			fail(QStringLiteral("track row 2 unavailable"));
			return;
		}
		w.m_infrastructureTable->item(1, 0)->setText("creator-yard");
		process();
		if (w.m_infrastructureTable->item(1, 0)->text() != "creator-yard") {
			fail(QStringLiteral("track rename 2 failed"));
			return;
		}
		const std::array<QString, 6> nodeIds = {QStringLiteral("creator-main-node-0"), QStringLiteral("creator-main-node-1"), QStringLiteral("creator-main-node-2"), QStringLiteral("creator-yard-node-0"), QStringLiteral("creator-yard-node-1"), QStringLiteral("creator-yard-node-2")};
		const std::array<double, 6> nodeX = {0.0, 1.0, 2.0, 0.0, 1.5, 2.0};
		const std::array<double, 6> nodeY = {0.0, 0.25, 0.0, 1.0, 1.0, 1.0};
		for (int row = 0; row < 6; ++row) {
			if (!addRow("nodes")) {
				fail(QStringLiteral("node row add failed"));
				return;
			}
			const int createdRow = w.m_infrastructureTable->rowCount() - 1;
			const QString generated = w.m_infrastructureTable->item(createdRow, 0)
				? w.m_infrastructureTable->item(createdRow, 0)->text()
				: QString();
			const QString track = row < 3 ? QStringLiteral("creator-main") : QStringLiteral("creator-yard");
			if (!setCell("nodes", generated, 0, nodeIds[static_cast<std::size_t>(row)])
				|| !setCell("nodes", nodeIds[static_cast<std::size_t>(row)], 1, track)
				|| !setCell("nodes", nodeIds[static_cast<std::size_t>(row)], 2, QString::number(nodeX[static_cast<std::size_t>(row)]))
				|| !setCell("nodes", nodeIds[static_cast<std::size_t>(row)], 3, QString::number(nodeY[static_cast<std::size_t>(row)]))) {
				fail(QStringLiteral("node geometry editing failed"));
				return;
			}
		}
		const std::array<QString, 4> arcIds = {QStringLiteral("creator-main-arc-0"), QStringLiteral("creator-main-arc-1"), QStringLiteral("creator-yard-arc-0"), QStringLiteral("creator-yard-arc-1")};
		for (int row = 0; row < 4; ++row) {
			if (!addRow("arcs")) {
				fail(QStringLiteral("arc row add failed"));
				return;
			}
			const int createdRow = w.m_infrastructureTable->rowCount() - 1;
			const QString generated = w.m_infrastructureTable->item(createdRow, 0)
				? w.m_infrastructureTable->item(createdRow, 0)->text()
				: QString();
			const QString track = row < 2 ? QStringLiteral("creator-main") : QStringLiteral("creator-yard");
			const int nodeOffset = row < 2 ? 0 : 3;
			const int localRow = row % 2;
			const QString from = nodeIds[static_cast<std::size_t>(nodeOffset + localRow)];
			const QString to = nodeIds[static_cast<std::size_t>(nodeOffset + localRow + 1)];
			QString arcFailure;
			if (!setCell("arcs", generated, 0, arcIds[static_cast<std::size_t>(row)]))
				arcFailure = QStringLiteral("id");
			else if (!setCell("arcs", arcIds[static_cast<std::size_t>(row)], 1, track))
				arcFailure = QStringLiteral("track");
			else if (!setCell("arcs", arcIds[static_cast<std::size_t>(row)], 2, from))
				arcFailure = QStringLiteral("from");
			else if (!setCell("arcs", arcIds[static_cast<std::size_t>(row)], 3, to))
				arcFailure = QStringLiteral("to");
			else if (!setCell("arcs", arcIds[static_cast<std::size_t>(row)], 4, row == 1 ? QStringLiteral("1250.5") : QStringLiteral("0")))
				arcFailure = QStringLiteral("curvature");
			else if (!setCell("arcs", arcIds[static_cast<std::size_t>(row)], 5, row == 0 ? QStringLiteral("-0.001953125") : (row == 1 ? QStringLiteral("0.00390625") : QStringLiteral("0.0009765625"))))
				arcFailure = QStringLiteral("gradient");
			else if (!setCell("arcs", arcIds[static_cast<std::size_t>(row)], 6, row < 2 ? QStringLiteral("22.5") : QStringLiteral("18")))
				arcFailure = QStringLiteral("speed");
			if (!arcFailure.isEmpty()) {
				fail(QStringLiteral("arc geometry editing failed (%1 row %2)").arg(arcFailure).arg(row));
				return;
			}
		}
		if (!facet("blocks")) {
			fail(QStringLiteral("block facet unavailable"));
			return;
		}
		for (const QString& track : {QStringLiteral("creator-main"), QStringLiteral("creator-yard")}) {
			const int filterIndex = w.m_blockTrackFilterCombo->findData(track);
			if (filterIndex < 0) {
				fail(QStringLiteral("block track selector unavailable"));
				return;
			}
			w.m_blockTrackFilterCombo->setCurrentIndex(filterIndex);
			process();
			for (int count = 0; count < 2; ++count) {
				w.m_addInfrastructureButton->click();
				process();
			}
			if (w.m_infrastructureTable->rowCount() != 2) {
				fail(QStringLiteral("base block rows did not add"));
				return;
			}
			const QString firstGenerated = w.m_infrastructureTable->item(0, 0)
				? w.m_infrastructureTable->item(0, 0)->text()
				: QString();
			const QString secondGenerated = w.m_infrastructureTable->item(1, 0)
				? w.m_infrastructureTable->item(1, 0)->text()
				: QString();
			const bool yard = track == QStringLiteral("creator-yard");
			QString blockFailure;
			if (!setCell("blocks", firstGenerated, 0, track + QStringLiteral("-block-0")))
				blockFailure = QStringLiteral("first id");
			else if (!setCell("blocks", secondGenerated, 0, track + QStringLiteral("-block-2")))
				blockFailure = QStringLiteral("second id");
			else if (!setCell("blocks", track + QStringLiteral("-block-0"), 2,
						 yard ? QStringLiteral("0.5") : QStringLiteral("0.75")))
				blockFailure = QStringLiteral("first length");
			else if (!setCell("blocks", track + QStringLiteral("-block-2"), 2,
						 yard ? QStringLiteral("0.25") : QStringLiteral("0.75")))
				blockFailure = QStringLiteral("second length");
			if (!blockFailure.isEmpty()) {
				fail(QStringLiteral("base block fields did not commit (%1, %2)")
						.arg(track, blockFailure));
				return;
			}
			w.m_infrastructureTable->setCurrentCell(1, 0);
			w.m_insertBlockButton->click();
			process();
			if (w.m_infrastructureTable->rowCount() != 3) {
				fail(QStringLiteral("block insert did not create the third visible block"));
				return;
			}
			const QString insertedGenerated = w.m_infrastructureTable->item(1, 0)
				? w.m_infrastructureTable->item(1, 0)->text()
				: QString();
			if (!setCell("blocks", insertedGenerated, 0, track + QStringLiteral("-block-1"))
				|| !setCell("blocks", track + QStringLiteral("-block-1"), 2,
					yard ? QStringLiteral("1.25") : QStringLiteral("0.5"))) {
				fail(QStringLiteral("inserted block fields did not commit"));
				return;
			}
			w.m_infrastructureTable->setCurrentCell(1, 0);
			w.m_moveBlockDownButton->click();
			process();
			w.m_moveBlockUpButton->click();
			process();
		}
		marker("E2E_CREATOR_INFRASTRUCTURE_OK");
		next();
		return;
	}

	if (creatorAcceptance.phase == 2) {
		if (!w.m_document->isLoaded() || !w.m_infrastructureFacetCombo || !w.m_infrastructureTable
			|| !w.m_addInfrastructureButton || !w.m_deleteInfrastructureButton) {
			fail(QStringLiteral("infrastructure controls disappeared before signalling authoring"));
			return;
		}
		if (!addRow("stations") || !setCell("stations", "station", 0, "creator-station-a")
			|| !setCell("stations", "creator-station-a", 1, "Creator A")
			|| !setCell("stations", "creator-station-a", 2, "true")
			|| !setCell("stations", "creator-station-a", 3, "1.0")
			|| !addRow("stations") || !setCell("stations", "station", 0, "creator-station-b")
			|| !setCell("stations", "creator-station-b", 1, "Creator B")
			|| !setCell("stations", "creator-station-b", 2, "true")
			|| !setCell("stations", "creator-station-b", 3, "1.5")) {
			fail(QStringLiteral("station geometry authoring failed"));
			return;
		}
		if (!addRow("platforms") || !addRow("platforms")
			|| w.m_infrastructureTable->rowCount() != 2) {
			fail(QStringLiteral("platform rows did not add"));
			return;
		}
		auto setPlatformCell = [&](int row, int column, const QString& value) {
			if (!facet("platforms") || row < 0 || row >= w.m_infrastructureTable->rowCount()
				|| !w.m_infrastructureTable->item(row, column))
				return false;
			w.m_infrastructureTable->setCurrentCell(row, column);
			w.m_infrastructureTable->item(row, column)->setText(value);
			process();
			return true;
		};
		if (!setPlatformCell(0, 0, "creator-station-a")
			|| !setPlatformCell(0, 1, "creator-platform-a")
			|| !setPlatformCell(0, 2, "creator-main-node-1")
			|| !setPlatformCell(1, 0, "creator-station-b")
			|| !setPlatformCell(1, 1, "creator-platform-b")
			|| !setPlatformCell(1, 2, "creator-yard-node-1")) {
			fail(QStringLiteral("platform station/node references did not commit"));
			return;
		}
		if (!facet("platforms")) {
			fail(QStringLiteral("platform facet unavailable for geometry"));
			return;
		}
		for (int row = 0; row < w.m_infrastructureTable->rowCount(); ++row) {
			if (auto* length = qobject_cast<QDoubleSpinBox*>(w.m_infrastructureTable->cellWidget(row, 3)))
				length->setValue(123.75);
			if (auto* width = qobject_cast<QDoubleSpinBox*>(w.m_infrastructureTable->cellWidget(row, 4)))
				width->setValue(4.25);
			process();
		}
		if (!addRow("connections")
			|| !setCell("connections", "connection", 0, "creator-switch")
			|| !setCell("connections", "creator-switch", 1, "creator-main-node-1")
			|| !setCell("connections", "creator-switch", 2, "creator-yard-node-1")
			|| !setCell("connections", "creator-switch", 3, "true")
			|| !setCell("connections", "creator-switch", 4, "9.25")) {
			fail(QStringLiteral("connection authoring failed"));
			return;
		}
		if (!addRow("signals") || !setCell("signals", "signal", 0, "creator-signal")) {
			fail(QStringLiteral("signal row did not add"));
			return;
		}
		const QString mainSection = setSection("signals", 0, 1,
			QStringLiteral("base block creator-main-block-0 /"));
		if (mainSection.isEmpty()) {
			fail(QStringLiteral("signal protected-section selector lacked the authored block"));
			return;
		}
		if (!addRow("signalling_areas")
			|| !setCell("signalling_areas", "signalling-area", 0, "creator-signalling-area")
			|| !setCell("signalling_areas", "creator-signalling-area", 1, "0")
			|| !setCell("signalling_areas", "creator-signalling-area", 2, "2")
			|| setSection("signalling_areas", 0, 3, QString::fromStdString(signallingLevelName(levelValue(SignallingLevel::Atb)))).isEmpty()) {
			fail(QStringLiteral("signalling area authoring failed"));
			return;
		}
		if (!addRow("routes")
			|| !setCell("routes", "route", 0, "creator-route")
			|| !setCell("routes", "creator-route", 2, "true")
			|| !setCell("routes", "creator-route", 3, "creator-corridor")
			|| !setCell("routes", "creator-route", 4, "false")) {
			fail(QStringLiteral("route metadata authoring failed"));
			return;
		}
		if (!facet("routes") || !w.m_routeSectionCatalogCombo || !w.m_addRouteSectionButton
			|| !w.m_routeSectionListWidget) {
			fail(QStringLiteral("route section controls unavailable"));
			return;
		}
		const std::array<QString, 3> routePrefixes = {
			QStringLiteral("base block creator-main-block-0 /"),
			QStringLiteral("connection creator-switch /"),
			QStringLiteral("base block creator-yard-block-2 /")};
		for (const QString& prefix : routePrefixes) {
			QString raw;
			for (int index = 0; index < w.m_routeSectionCatalogCombo->count(); ++index) {
				if (w.m_routeSectionCatalogCombo->itemText(index).startsWith(prefix)) {
					w.m_routeSectionCatalogCombo->setCurrentIndex(index);
					raw = w.m_routeSectionCatalogCombo->itemData(index).toString();
					break;
				}
			}
			if (raw.isEmpty()) {
				fail(QStringLiteral("route catalog lacked creator-facing section: ") + prefix);
				return;
			}
			w.m_addRouteSectionButton->click();
			process();
		}
		if (w.m_routeSectionListWidget->count() != 3) {
			fail(QStringLiteral("route section order was not authored"));
			return;
		}
		if (!addRow("block_dependencies")) {
			fail(QStringLiteral("dependency row did not add"));
			return;
		}
		if (setSection("block_dependencies", 0, 0,
				QStringLiteral("base block creator-main-block-0 /"))
				.isEmpty()
			|| setSection("block_dependencies", 0, 1,
				QStringLiteral("base block creator-yard-block-2 /"))
				.isEmpty()) {
			fail(QStringLiteral("dependency typed selectors lacked safe sections"));
			return;
		}
		if (!addRow("single_track_restrictions")) {
			fail(QStringLiteral("restriction row did not add"));
			return;
		}
		const std::array<QString, 4> restrictionPrefixes = {
			QStringLiteral("base block creator-main-block-0 /"),
			QStringLiteral("base block creator-yard-block-2 /"),
			QStringLiteral("connection creator-switch /"),
			QStringLiteral("base block creator-yard-block-2 /")};
		for (int column = 0; column < 4; ++column)
			if (setSection("single_track_restrictions", 0, column,
					restrictionPrefixes[static_cast<std::size_t>(column)])
					.isEmpty()) {
				fail(QStringLiteral("restriction typed selector lacked a safe section"));
				return;
			}
		if (!addRow("station_boundaries")
			|| setSection("station_boundaries", 0, 0,
				QStringLiteral("base block creator-main-block-0 /"))
				.isEmpty()
			|| setSection("station_boundaries", 0, 1,
				QStringLiteral("base block creator-yard-block-2 /"))
				.isEmpty()) {
			fail(QStringLiteral("station boundary typed selectors lacked safe sections"));
			return;
		}
		const QString renamedBlock = QStringLiteral("creator-main-block-renamed");
		if (!facet("blocks")) {
			fail(QStringLiteral("block facet unavailable for reference rename"));
			return;
		}
		const int mainTrackIndex = w.m_blockTrackFilterCombo->findData("creator-main");
		if (mainTrackIndex < 0) {
			fail(QStringLiteral("main block filter disappeared"));
			return;
		}
		w.m_blockTrackFilterCombo->setCurrentIndex(mainTrackIndex);
		process();
		const int mainBlockRow = rowFor(QStringLiteral("creator-main-block-0"));
		if (mainBlockRow < 0 || !w.m_infrastructureTable->item(mainBlockRow, 0)) {
			fail(QStringLiteral("referenced block row was not visible"));
			return;
		}
		w.m_infrastructureTable->item(mainBlockRow, 0)->setText(renamedBlock);
		process();
		bool referenceStillValid = false;
		if (w.m_document->mutableModel().routes.size() == 1 && w.m_document->mutableModel().routes.front().blocks.size() == 3
			&& w.m_document->mutableModel().blockDependencies.size() == 1
			&& w.m_document->mutableModel().singleTrackRestrictions.size() == 1
			&& w.m_document->mutableModel().stationBoundaries.size() == 1
			&& !sceneSignals(w.m_document->mutableModel()).empty()) {
			const auto mentions = [&renamedBlock](const std::string& value) {
				return value.find(renamedBlock.toStdString()) != std::string::npos;
			};
			const auto& route = w.m_document->mutableModel().routes.front();
			const auto& dependency = w.m_document->mutableModel().blockDependencies.front();
			const auto& restriction = w.m_document->mutableModel().singleTrackRestrictions.front();
			const auto& boundary = w.m_document->mutableModel().stationBoundaries.front();
			referenceStillValid = mentions(route.blocks.front()) && mentions(dependency.block)
				&& mentions(restriction.startBlock) && mentions(boundary.entranceBlock)
				&& mentions(sceneSignals(w.m_document->mutableModel()).front().protectedSection);
		}
		if (!referenceStillValid) {
			fail(QStringLiteral("public block rename did not retain all references"));
			return;
		}
		const std::size_t blockCount = w.m_document->mutableModel().blocks.size();
		const int renamedRow = rowFor(renamedBlock);
		if (renamedRow < 0) {
			fail(QStringLiteral("renamed block could not be selected for delete check"));
			return;
		}
		w.m_infrastructureTable->setCurrentCell(renamedRow, 0);
		w.m_deleteInfrastructureButton->click();
		process();
		if (w.m_document->mutableModel().blocks.size() != blockCount) {
			fail(QStringLiteral("referenced delete changed canonical data"));
			return;
		}
		marker("E2E_CREATOR_STATIONS_SIGNALLING_OK");
		next();
		return;
	}

	if (creatorAcceptance.phase == 3) {
		if (!w.m_document->isLoaded() || !w.m_trainUnitDock || !w.m_trainUnitListWidget
			|| !w.m_addTrainUnitButton || !w.m_trainUnitIdEdit || !w.m_compositionDock
			|| !w.m_addCompositionButton || !w.m_addUnitButton || !w.m_addTrainUnitTractionButton) {
			fail(QStringLiteral("rolling-stock creator controls are unavailable"));
			return;
		}
		w.m_trainUnitDock->show();
		w.m_trainUnitDock->raise();
		auto setUnit = [&](int row, const QString& id, double seed) {
			w.m_trainUnitListWidget->setCurrentRow(row);
			process();
			if (!editLine(w.m_trainUnitIdEdit, id))
				return false;
			const std::array<double, 9> values = {90000.0 + seed, 22000.0 + seed,
				2.0, 30.0, 0.8, 9.0, 0.002, 0.4, 80.0};
			for (std::size_t index = 0; index < values.size(); ++index) {
				if (!w.m_trainUnitPhysicalEdits[index])
					return false;
				w.m_trainUnitPhysicalEdits[index]->setValue(values[index]);
			}
			if (!editLine(w.m_trainUnitSourceDataEdit, QStringLiteral("creator-parameters.csv"))
				|| !editLine(w.m_trainUnitSourceTractionEdit, QStringLiteral("creator-traction.csv")))
				return false;
			w.m_addTrainUnitTractionButton->click();
			process();
			if (!w.m_trainUnitTractionTable || w.m_trainUnitTractionTable->rowCount() < 1)
				return false;
			const std::array<double, 5> traction = {0.0, 30.0, 220000.0, -3500.0, 0.0};
			for (int column = 0; column < 5; ++column)
				if (auto* edit = qobject_cast<QDoubleSpinBox*>(w.m_trainUnitTractionTable->cellWidget(0, column)))
					edit->setValue(traction[static_cast<std::size_t>(column)]);
				else
					return false;
			process();
			return true;
		};
		w.m_addTrainUnitButton->click();
		process();
		if (w.m_trainUnitListWidget->count() != 1 || !setUnit(0, QStringLiteral("creator-unit-a"), 0.0)) {
			fail(QStringLiteral("first train unit did not author through controls"));
			return;
		}
		w.m_addTrainUnitButton->click();
		process();
		if (w.m_trainUnitListWidget->count() != 2 || !setUnit(1, QStringLiteral("creator-unit-b"), 5000.0)) {
			fail(QStringLiteral("second train unit did not author through controls"));
			return;
		}
		w.m_compositionDock->show();
		w.m_compositionDock->raise();
		w.m_addCompositionButton->click();
		process();
		if (w.m_compositionListWidget->count() != 1
			|| !editLine(w.m_compositionIdEdit, QStringLiteral("creator-composition"))) {
			fail(QStringLiteral("composition row did not author through controls"));
			return;
		}
		acceptInputDialog(QStringLiteral("creator-unit-a"));
		w.m_addUnitButton->click();
		process();
		acceptInputDialog(QStringLiteral("creator-unit-b"));
		w.m_addUnitButton->click();
		process();
		if (w.m_compositionUnitsListWidget->count() != 2
			|| w.m_document->mutableModel().compositions.empty()
			|| w.m_document->mutableModel().compositions.front().units.size() != 2
			|| w.m_document->mutableModel().compositions.front().units.front() != "creator-unit-a"
			|| w.m_document->mutableModel().compositions.front().units.back() != "creator-unit-b") {
			fail(QStringLiteral("composition membership did not retain both units in order"));
			return;
		}
		marker("E2E_CREATOR_ROLLING_STOCK_OK");
		next();
		return;
	}

	if (creatorAcceptance.phase == 4) {
		if (!w.m_document->isLoaded() || !w.m_serviceDock || !w.m_serviceListWidget || !w.m_addServiceButton
			|| !w.m_serviceIdEdit || !w.m_serviceOperatingCodeEdit || !w.m_serviceCompositionCombo
			|| !w.m_serviceRouteCombo || !w.m_addStopButton || !w.m_stopTableWidget) {
			fail(QStringLiteral("service creator controls are unavailable"));
			return;
		}
		w.m_serviceDock->show();
		w.m_serviceDock->raise();
		const bool noServicesBefore = w.m_followTrainCombo->currentText() == "No trains to follow" && !w.m_followAction->isEnabled();
		w.m_addServiceButton->click();
		process();
		// The first service changes why Follow is off: the case has trains to run but has not run.
		if (!noServicesBefore || w.m_followTrainCombo->currentText() != "Run the case to follow trains" || w.m_followAction->isEnabled()
			|| w.m_followStatusLabel->text() != "Run the case first, then you can follow a train.") {
			fail(QStringLiteral("Follow does not explain a case that has services and has not run"));
			return;
		}
		marker("E2E_FOLLOW_SERVICE_ADDED_OK");
		if (w.m_serviceListWidget->count() != 1
			|| !editLine(w.m_serviceIdEdit, QStringLiteral("creator-service"))
			|| !editLine(w.m_serviceOperatingCodeEdit, QStringLiteral("1701"))
			|| !choose(w.m_serviceCompositionCombo, QStringLiteral("creator-composition"))
			|| !choose(w.m_serviceRouteCombo, QStringLiteral("creator-route"))) {
			fail(QStringLiteral("service identity or typed references did not commit"));
			return;
		}
		w.m_serviceHasEntryTimeCheck->setChecked(true);
		if (!editLine(w.m_serviceEntryTimeSecondsEdit, QStringLiteral("60"))) {
			fail(QStringLiteral("service entry time did not commit"));
			return;
		}
		w.m_serviceHasRepeatCheck->setChecked(true);
		if (!editLine(w.m_serviceHeadwaySecondsEdit, QStringLiteral("300"))) {
			fail(QStringLiteral("service headway did not commit"));
			return;
		}
		w.m_serviceHasRepeatCountCheck->setChecked(true);
		if (!editLine(w.m_serviceRepeatCountEdit, QStringLiteral("3"))) {
			fail(QStringLiteral("service repeat count did not commit"));
			return;
		}
		w.m_servicePerformancePercentEdit->setValue(92.0);
		w.m_serviceHasMaximumSpeedCheck->setChecked(true);
		w.m_serviceMaximumSpeedKmhEdit->setValue(100.0);
		w.m_serviceHasOperatingCodeStepCheck->setChecked(true);
		if (!editLine(w.m_serviceOperatingCodeStepEdit, QStringLiteral("1"))) {
			fail(QStringLiteral("service operating-code increment did not commit"));
			return;
		}
		const auto configureStop = [&](const QString& station, const QString& platform,
									   double arrival, double departure, double dwell) {
			bool choices = true;
			const bool accepted = editStopDialog(-1, [&](QDialog* dialog) {
				choices = choose(dialog->findChild<QComboBox*>("stopEditorStationCombo"), station)
					&& choose(dialog->findChild<QComboBox*>("stopEditorPlatformCombo"), platform);
				dialog->findChild<QCheckBox*>("stopEditorArrivalPresent")->setChecked(true);
				dialog->findChild<QCheckBox*>("stopEditorDeparturePresent")->setChecked(true);
				dialog->findChild<QLineEdit*>("stopEditorArrivalEdit")->setText(QString::number(arrival));
				dialog->findChild<QLineEdit*>("stopEditorDepartureEdit")->setText(QString::number(departure));
				dialog->findChild<QLineEdit*>("stopEditorDwellEdit")->setText(QString::number(dwell));
			});
			return choices && accepted;
		};
		if (!configureStop(QStringLiteral("creator-station-a"), QStringLiteral("creator-platform-a"),
				180.0, 240.0, 60.0)) {
			fail(QStringLiteral("first service stop did not commit station/platform timetable"));
			return;
		}
		if (!configureStop(QStringLiteral("creator-station-b"), QStringLiteral("creator-platform-b"),
				480.0, 540.0, 60.0)) {
			fail(QStringLiteral("second service stop did not commit station/platform timetable"));
			return;
		}
		if (w.m_document->mutableModel().services.empty() || w.m_document->mutableModel().services.front().stops.size() != 2
			|| !w.m_document->mutableModel().services.front().hasRepeat
			|| !w.m_document->mutableModel().services.front().hasRepeatCount
			|| w.m_document->mutableModel().services.front().repeatCount != 3
			|| w.m_document->mutableModel().services.front().headwaySeconds != 300.0
			|| w.m_document->mutableModel().services.front().stops.front().stationId != "creator-station-a"
			|| w.m_document->mutableModel().services.front().stops.back().stationId != "creator-station-b"
			|| !w.m_document->mutableModel().services.front().stops.front().hasPlannedArrival
			|| !w.m_document->mutableModel().services.front().stops.front().hasPlannedDeparture
			|| w.m_document->mutableModel().services.front().stops.front().plannedArrivalSeconds != 180.0
			|| w.m_document->mutableModel().services.front().stops.front().plannedDepartureSeconds != 240.0
			|| w.m_document->mutableModel().services.front().stops.front().dwellSeconds != 60.0
			|| w.m_document->mutableModel().services.front().stops.back().plannedArrivalSeconds != 480.0
			|| w.m_document->mutableModel().services.front().stops.back().plannedDepartureSeconds != 540.0
			|| w.m_document->mutableModel().services.front().stops.back().dwellSeconds != 60.0) {
			const SceneService* service = w.m_document->mutableModel().services.empty()
				? nullptr
				: &w.m_document->mutableModel().services.front();
			fail(QStringLiteral("service repeat or ordered stop values were not retained: stops=%1 repeat=%2 count-flag=%3 count=%4 headway=%5 first=%6 last=%7 arrival=%8 departure=%9 dwell=%10")
					.arg(service ? static_cast<int>(service->stops.size()) : -1)
					.arg(service && service->hasRepeat)
					.arg(service && service->hasRepeatCount)
					.arg(service ? service->repeatCount : -1)
					.arg(service ? service->headwaySeconds : -1.0)
					.arg(service && !service->stops.empty() ? QString::fromStdString(service->stops.front().stationId) : QStringLiteral("-"))
					.arg(service && !service->stops.empty() ? QString::fromStdString(service->stops.back().stationId) : QStringLiteral("-"))
					.arg(service && !service->stops.empty() && service->stops.front().hasPlannedArrival)
					.arg(service && !service->stops.empty() && service->stops.front().hasPlannedDeparture)
					.arg(service && !service->stops.empty() ? service->stops.front().dwellSeconds : -1.0));
			return;
		}
		if (w.m_serviceOccurrenceTable->rowCount() != 3 || hasErrors(w.m_session->m_sceneDiagnostics)) {
			QString detail;
			for (const SceneDiagnostic& diagnostic : w.m_session->m_sceneDiagnostics)
				if (diagnostic.severity == SceneSeverity::Error) {
					detail = QString::fromStdString(toDisplayText(diagnostic));
					break;
				}
			fail(QStringLiteral("service occurrence preview or validation is not runnable: rows=%1 %2")
					.arg(w.m_serviceOccurrenceTable->rowCount())
					.arg(detail));
			return;
		}
		marker("E2E_CREATOR_SERVICE_OK");
		next();
		return;
	}

	if (creatorAcceptance.phase == 5) {
		if (!w.m_document->isLoaded() || !w.m_incidentDock || !w.m_scenarioListWidget
			|| !w.m_blankScenarioButton || !w.m_addIncidentButton || !w.m_addEntranceDelayButton
			|| !w.m_incidentHasReducedSpeedCheck || !w.m_incidentReducedSpeedKmhEdit) {
			fail(QStringLiteral("scenario creator controls are unavailable"));
			return;
		}
		w.m_incidentDock->show();
		w.m_incidentDock->raise();
		w.m_blankScenarioButton->click();
		process();
		if (!editLine(w.m_scenarioIdEdit, QStringLiteral("incident"))
			|| !editLine(w.m_scenarioNameEdit, QStringLiteral("Incident scenario"))
			|| !editLine(w.m_scenarioDescriptionEdit, QStringLiteral("signal and breakdown evidence"))) {
			fail(QStringLiteral("incident scenario metadata did not commit"));
			return;
		}
		w.m_addIncidentButton->click();
		process();
		if (!editLine(w.m_incidentIdEdit, QStringLiteral("creator-signal-failure"))
			|| !choose(w.m_incidentTargetCombo, QStringLiteral("creator-signal"))
			|| !editLine(w.m_incidentStartSecondsEdit, QStringLiteral("350"))
			|| !editLine(w.m_incidentEndSecondsEdit, QStringLiteral("500"))) {
			fail(QStringLiteral("signal failure incident did not bind through controls"));
			return;
		}
		w.m_addIncidentButton->click();
		process();
		w.m_incidentTypeCombo->setCurrentText(QStringLiteral("train_breakdown"));
		process();
		if (!editLine(w.m_incidentIdEdit, QStringLiteral("creator-breakdown"))
			|| !choose(w.m_incidentTargetCombo, QStringLiteral("creator-service"))
			|| !editLine(w.m_incidentStartSecondsEdit, QStringLiteral("60"))
			|| !editLine(w.m_incidentEndSecondsEdit, QStringLiteral("100"))) {
			fail(QStringLiteral("breakdown incident target or finite interval did not commit"));
			return;
		}
		w.m_incidentHasReducedSpeedCheck->setChecked(true);
		w.m_incidentReducedSpeedKmhEdit->setValue(10.0);
		QMetaObject::invokeMethod(w.m_incidentReducedSpeedKmhEdit, "editingFinished", Qt::DirectConnection);
		process();
		w.m_blankScenarioButton->click();
		process();
		if (!editLine(w.m_scenarioIdEdit, QStringLiteral("entrance"))
			|| !editLine(w.m_scenarioNameEdit, QStringLiteral("Entrance delay"))
			|| !editLine(w.m_scenarioDescriptionEdit, QStringLiteral("positive entrance delay at station A"))) {
			fail(QStringLiteral("entrance scenario metadata did not commit"));
			return;
		}
		w.m_addEntranceDelayButton->click();
		process();
		if (!choose(w.m_entranceDelayServiceCombo, QStringLiteral("creator-service"))) {
			fail(QStringLiteral("entrance delay service selector lacked authored service"));
			return;
		}
		w.m_entranceDelayOccurrenceEdit->setValue(2);
		QMetaObject::invokeMethod(w.m_entranceDelayOccurrenceEdit, "editingFinished", Qt::DirectConnection);
		process();
		if (!choose(w.m_entranceDelayStationCombo, QStringLiteral("creator-station-a"))) {
			fail(QStringLiteral("entrance delay station selector lacked station A"));
			return;
		}
		w.m_entranceDelaySecondsEdit->setValue(90.0);
		QMetaObject::invokeMethod(w.m_entranceDelaySecondsEdit, "editingFinished", Qt::DirectConnection);
		process();
		if (w.m_document->mutableModel().scenarios.size() != 3
			|| w.m_document->mutableModel().scenarios[1].incidents.size() != 2
			|| w.m_document->mutableModel().scenarios[2].entranceDelays.size() != 1
			|| w.m_document->mutableModel().scenarios[2].entranceDelays.front().occurrence != 2
			|| w.m_document->mutableModel().scenarios[2].entranceDelays.front().delaySeconds <= 0.0
			|| hasErrors(w.m_session->m_sceneDiagnostics)) {
			QString detail;
			for (const SceneDiagnostic& diagnostic : w.m_session->m_sceneDiagnostics)
				if (diagnostic.severity == SceneSeverity::Error) {
					detail = QString::fromStdString(toDisplayText(diagnostic));
					break;
				}
			fail(QStringLiteral("scenario library or validation did not retain all three scenarios: scenarios=%1 incident-count=%2 delay-count=%3 occurrence=%4 seconds=%5 %6")
					.arg(static_cast<int>(w.m_document->mutableModel().scenarios.size()))
					.arg(w.m_document->mutableModel().scenarios.size() > 1 ? static_cast<int>(w.m_document->mutableModel().scenarios[1].incidents.size()) : -1)
					.arg(w.m_document->mutableModel().scenarios.size() > 2 ? static_cast<int>(w.m_document->mutableModel().scenarios[2].entranceDelays.size()) : -1)
					.arg(w.m_document->mutableModel().scenarios.size() > 2 && !w.m_document->mutableModel().scenarios[2].entranceDelays.empty() ? w.m_document->mutableModel().scenarios[2].entranceDelays.front().occurrence : -1)
					.arg(w.m_document->mutableModel().scenarios.size() > 2 && !w.m_document->mutableModel().scenarios[2].entranceDelays.empty() ? w.m_document->mutableModel().scenarios[2].entranceDelays.front().delaySeconds : -1.0)
					.arg(detail));
			return;
		}
		marker("E2E_CREATOR_SCENARIOS_OK");
		next();
		return;
	}

	if (creatorAcceptance.phase == 6) {
		if (!w.m_document->isLoaded() || !w.m_passengerDock || !w.m_passengerListWidget
			|| !w.m_addPassengerButton || !w.m_addPassengerJourneyButton
			|| !w.m_addPassengerLegButton || !w.m_passengerTabs) {
			fail(QStringLiteral("passenger creator controls are unavailable"));
			return;
		}
		w.m_passengerDock->show();
		w.m_passengerDock->raise();
		w.m_addPassengerButton->click();
		process();
		if (w.m_passengerListWidget->count() != 1
			|| !editLine(w.m_passengerIdEdit, QStringLiteral("creator-passenger"))) {
			fail(QStringLiteral("passenger ID did not commit"));
			return;
		}
		w.m_addPassengerJourneyButton->click();
		process();
		if (!editLine(w.m_passengerJourneyIdEdit, QStringLiteral("creator-journey"))
			|| !editLine(w.m_passengerJourneyActivityEdit, QStringLiteral("creator commute"))
			|| !choose(w.m_passengerJourneyOriginCombo, QStringLiteral("creator-station-a"))
			|| !choose(w.m_passengerJourneyDestinationCombo, QStringLiteral("creator-station-b"))) {
			fail(QStringLiteral("journey station or metadata controls did not commit"));
			return;
		}
		const std::array<double, 4> windows = {120.0, 420.0, 300.0, 720.0};
		for (std::size_t index = 0; index < windows.size(); ++index)
			w.m_passengerJourneyWindowEdits[index]->setValue(windows[index]);
		process();
		w.m_passengerTabs->setCurrentIndex(1);
		w.m_addPassengerLegButton->click();
		process();
		if (!editLine(w.m_passengerLegIdEdit, QStringLiteral("creator-leg"))
			|| !choose(w.m_passengerLegOriginCombo, QStringLiteral("creator-station-a"))
			|| !choose(w.m_passengerLegDestinationCombo, QStringLiteral("creator-station-b"))
			|| !choose(w.m_passengerLegServiceCombo, QStringLiteral("creator-service"))) {
			fail(QStringLiteral("passenger leg public CRUD did not commit references"));
			return;
		}
		w.m_passengerLegOccurrenceEdit->setFocus(Qt::OtherFocusReason);
		w.m_passengerLegOccurrenceEdit->setValue(1);
		w.m_passengerLegOccurrenceEdit->clearFocus();
		process();
		if (w.m_document->mutableModel().passengers.size() != 1
			|| w.m_document->mutableModel().passengers.front().journeys.size() != 1
			|| w.m_document->mutableModel().passengers.front().journeys.front().legs.size() != 1
			|| w.m_document->mutableModel().passengers.front().journeys.front().legs.front().occurrence != 1
			|| hasErrors(w.m_session->m_sceneDiagnostics)) {
			fail(QStringLiteral("passenger journey/leg or validation did not retain authored values"));
			return;
		}
		marker("E2E_CREATOR_PASSENGER_OK");
		next();
		return;
	}

	if (creatorAcceptance.phase == 7) {
		const QString folder = emitPath(qEnvironmentVariable("QEGTRAIN_E2E_CREATOR_FOLDER"));
		if (!w.m_document->isLoaded() || folder.isEmpty() || !w.m_saveSceneAsFolderAction
			|| !w.m_newSceneAction || !w.m_trainUnitListWidget) {
			fail(QStringLiteral("folder persistence controls or target are unavailable"));
			return;
		}
		w.activateWindow();
		w.m_trainUnitDock->show();
		w.m_trainUnitDock->raise();
		process();
		w.m_trainUnitListWidget->setCurrentRow(0);
		process();
		QLineEdit* pendingMass = w.m_trainUnitPhysicalEdits[0]
			? w.m_trainUnitPhysicalEdits[0]->findChild<QLineEdit*>()
			: nullptr;
		if (!pendingMass) {
			fail(QStringLiteral("focused train-unit mass editor is unavailable"));
			return;
		}
		pendingMass->setFocus(Qt::OtherFocusReason);
		pendingMass->setText(QStringLiteral("91000"));
		process();
		if (!pendingMass->hasFocus()) {
			fail(QStringLiteral("focused train-unit mass editor did not retain focus before Save As"));
			return;
		}
		acceptFileDialog(folder, true);
		w.m_saveSceneAsFolderAction->trigger();
		process();
		if (QFileInfo(w.m_session->m_sceneDir).absoluteFilePath() != folder || w.m_session->m_sceneIsBundle
			|| w.m_document->isDirty() || !QFileInfo(QDir(folder).filePath("scene.json")).exists()) {
			fail(QStringLiteral("public Save Scene As Folder did not persist the focused physical value"));
			return;
		}
		w.m_newSceneAction->trigger();
		process();
		QAction* openFolder = w.findChild<QAction*>("actionOpenSceneFolder");
		if (!openFolder) {
			fail(QStringLiteral("public Open Scene Folder action is unavailable"));
			return;
		}
		acceptFileDialog(folder, true);
		acceptMessageBox(QMessageBox::Discard);
		openFolder->trigger();
		process();
		if (QFileInfo(w.m_session->m_sceneDir).absoluteFilePath() != folder || w.m_session->m_sceneIsBundle
			|| w.m_document->mutableModel().name != "creator_acceptance_case"
			|| w.m_document->mutableModel().tracks.size() != 2 || w.m_document->mutableModel().nodes.size() != 6
			|| w.m_document->mutableModel().arcs.size() != 4 || w.m_document->mutableModel().blocks.size() != 6
			|| w.m_document->mutableModel().connections.size() != 1 || w.m_document->mutableModel().stations.size() != 2
			|| w.m_document->mutableModel().trainUnits.size() != 2 || w.m_document->mutableModel().compositions.size() != 1
			|| w.m_document->mutableModel().services.size() != 1 || w.m_document->mutableModel().scenarios.size() != 3
			|| w.m_document->mutableModel().passengers.size() != 1
			|| w.m_document->mutableModel().trainUnits.front().physical.mass_of_traction_unit_kg != 91000.0
			|| w.m_document->mutableModel().passengers.front().id != "creator-passenger"
			|| w.m_document->mutableModel().passengers.front().journeys.size() != 1
			|| w.m_document->mutableModel().passengers.front().journeys.front().id != "creator-journey"
			|| hasErrors(w.m_session->m_sceneDiagnostics)) {
			fail(QStringLiteral("folder reopen did not retain complete creator rows"));
			return;
		}
		// Normal Open must construct semantic infrastructure before Run, without
		// preparing native backing objects or runtime observer collections.
		int previewArcs = 0, previewNodes = 0, previewConnections = 0;
		for (auto* item : w.scene->items()) {
			if (auto* arc = qgraphicsitem_cast<TrackLineItem*>(item)) {
				++previewArcs;
				if (arc->arc || arc->track != -1) {
					fail(QStringLiteral("preview arc has runtime backing state"));
					return;
				}
			}
			if (qgraphicsitem_cast<NodeItem*>(item) || qgraphicsitem_cast<StationNodeItem*>(item))
				++previewNodes;
			if (qgraphicsitem_cast<ConnectionItem*>(item))
				++previewConnections;
		}
		if (previewArcs != 4 || previewNodes != 6 || previewConnections != 1
			|| !w.m_presenter->allArcs.isEmpty() || !w.m_presenter->allSignals.isEmpty() || !w.m_presenter->allPlatforms.isEmpty()
			|| w.m_worker || w.m_document->isDirty() || !w.m_showingTrackPreview) {
			fail(QStringLiteral("normal Open lacks semantic preview infrastructure: arcs=%1 nodes=%2 connections=%3")
					.arg(previewArcs)
					.arg(previewNodes)
					.arg(previewConnections));
			return;
		}
		marker("E2E_CREATOR_PREVIEW_PRIMITIVES_OK");
		QString previewFailure;
		if (!checkPreviewInfrastructure(w, previewFailure)) {
			fail(previewFailure);
			return;
		}
		if (qEnvironmentVariableIsSet("QEGTRAIN_E2E_PREVIEW_ONLY")) {
			if (qEnvironmentVariableIsSet("QEGTRAIN_E2E_PREVIEW_PARITY")
				&& (!checkPreviewRuntimeParity(w, previewFailure)
					|| !checkPreviewRuntimeParity(w, previewFailure, true))) {
				fail(previewFailure);
				return;
			}
			QCoreApplication::exit(0);
			return;
		}
		marker("E2E_CREATOR_FOLDER_ROUNDTRIP_OK");
		next();
		return;
	}

	if (creatorAcceptance.phase == 8) {
		const QString bundle = emitPath(qEnvironmentVariable("QEGTRAIN_E2E_CREATOR_BUNDLE"));
		if (!w.m_document->isLoaded() || bundle.isEmpty() || !w.m_saveSceneAsAction || !w.m_newSceneAction) {
			fail(QStringLiteral("bundle persistence controls or target are unavailable"));
			return;
		}
		acceptFileDialog(bundle, false);
		w.m_saveSceneAsAction->trigger();
		process();
		if (QFileInfo(w.m_session->m_sceneDir).absoluteFilePath() != bundle || !w.m_session->m_sceneIsBundle
			|| w.m_document->isDirty() || !QFileInfo(bundle).exists()) {
			fail(QStringLiteral("public Save Case Study As bundle did not complete"));
			return;
		}
		const QString folder = emitPath(qEnvironmentVariable("QEGTRAIN_E2E_CREATOR_FOLDER"));
		const std::string originalName = w.m_document->mutableModel().name;
		auto openRecent = [&w](const QString& path) {
			for (QAction* action : w.m_session->recentScenesMenu()->actions()) {
				if (action->data().toString() == path) {
					action->trigger();
					return true;
				}
			}
			return false;
		};
		for (QMessageBox::StandardButton decision : {QMessageBox::Cancel,
				 QMessageBox::Discard, QMessageBox::Save}) {
			w.m_caseNameEdit->setFocus(Qt::OtherFocusReason);
			w.m_caseNameEdit->setText(QStringLiteral("pending case name"));
			acceptMessageBox(decision);
			if (!openRecent(folder)) {
				fail(QStringLiteral("pending-open test could not find recent folder"));
				return;
			}
			process();
			if (decision == QMessageBox::Cancel) {
				if (w.m_session->m_sceneDir != bundle || !w.m_document->isDirty()
					|| w.m_document->mutableModel().name != "pending case name") {
					fail(QStringLiteral("Cancel did not retain focused edits and current bundle"));
					return;
				}
			} else {
				if (w.m_session->m_sceneDir != folder || w.m_session->m_sceneIsBundle || w.m_document->isDirty()
					|| w.m_document->mutableModel().name != originalName) {
					fail(QStringLiteral("Save or Discard did not continue the pending folder open"));
					return;
				}
				if (!openRecent(bundle)
					|| w.m_document->mutableModel().name != (decision == QMessageBox::Save ? "pending case name" : originalName)) {
					fail(QStringLiteral("Save or Discard persisted the wrong outgoing bundle contents"));
					return;
				}
			}
		}
		if (!editLine(w.m_caseNameEdit, QString::fromStdString(originalName))) {
			fail(QStringLiteral("could not restore case name after pending-open checks"));
			return;
		}
		w.m_saveSceneAction->trigger();
		w.m_caseNameEdit->setText(QStringLiteral("chooser pending edit"));
		QTimer::singleShot(0, &w, [&w, folder]() {
			auto* chooser = w.findChild<QDialog*>(QStringLiteral("caseChooserDialog"));
			auto* list = chooser
				? chooser->findChild<QListWidget*>(QStringLiteral("caseChooserList"))
				: nullptr;
			if (!list)
				return;
			for (int row = 0; row < list->count(); ++row) {
				if (list->item(row)->data(Qt::UserRole).toString() == folder) {
					list->setCurrentRow(row);
					break;
				}
			}
			if (auto* open = chooser->findChild<QPushButton*>(QStringLiteral("caseChooserOpenButton")))
				open->click();
		});
		acceptMessageBox(QMessageBox::Cancel);
		w.m_openCaseAction->trigger();
		if (w.m_session->m_sceneDir != bundle || !w.m_document->isDirty()
			|| w.m_document->mutableModel().name != "chooser pending edit") {
			fail(QStringLiteral("chooser bypassed the pending-open cancellation boundary"));
			return;
		}
		if (!editLine(w.m_caseNameEdit, QString::fromStdString(originalName))) {
			fail(QStringLiteral("could not restore case name after chooser check"));
			return;
		}
		w.m_saveSceneAction->trigger();
		{
			QTemporaryDir invalidInput;
			const QString invalidBundle = invalidInput.filePath("invalid.egscene");
			QFile file(invalidBundle);
			if (!file.open(QIODevice::WriteOnly) || file.write("invalid scene") < 0) {
				fail(QStringLiteral("could not create invalid incoming bundle fixture"));
				return;
			}
			file.close();
			w.m_session->addRecentScene(invalidBundle);
			w.m_caseNameEdit->setText(QStringLiteral("retained after failed open"));
			acceptMessageBox(QMessageBox::Discard);
			if (!openRecent(invalidBundle)) {
				fail(QStringLiteral("invalid incoming bundle was not offered as a recent scene"));
				return;
			}
			if (w.m_session->m_sceneDir != bundle || !w.m_document->isDirty()
				|| w.m_document->mutableModel().name != "retained after failed open") {
				fail(QStringLiteral("failed incoming load erased current dirty case"));
				return;
			}
		}
		w.m_session->rebuildRecentScenesMenu();
		if (!editLine(w.m_caseNameEdit, QString::fromStdString(originalName))) {
			fail(QStringLiteral("could not restore case name after failed-load check"));
			return;
		}
		w.m_saveSceneAction->trigger();
		w.m_newSceneAction->trigger();
		process();
		const QString failedSave = QDir(emitPath(qEnvironmentVariable("QEGTRAIN_E2E_OUT")))
									   .filePath(QStringLiteral("failed-save.egscene"));
		if (!QDir().mkpath(failedSave)
			|| saveSceneBundle(w.m_document->mutableModel(), failedSave.toStdString()).success()) {
			fail(QStringLiteral("could not create failed-save fixture"));
			return;
		}
		w.m_session->m_sceneDir = failedSave;
		w.m_session->m_sceneIsBundle = true;
		w.m_caseNameEdit->setText(QStringLiteral("retained after failed save"));
		acceptMessageBox(QMessageBox::Save);
		if (!openRecent(bundle)) {
			fail(QStringLiteral("failed-save replacement was not offered as a recent scene"));
			return;
		}
		if (!w.m_document->isLoaded() || w.m_session->m_sceneDir != failedSave || !w.m_document->isDirty()
			|| w.m_document->mutableModel().name != "retained after failed save") {
			fail(QStringLiteral("failed save replaced the unsaved current case"));
			return;
		}
		w.m_session->m_sceneDir.clear();
		w.m_session->m_sceneIsBundle = false;
		w.m_session->m_sceneBundleVersion.reset();
		w.m_session->updateSceneWindowTitle();
		w.updateSceneActions();
		QTimer rejectSaveAs;
		rejectSaveAs.setInterval(10);
		QObject::connect(&rejectSaveAs, &QTimer::timeout, &w, []() {
			for (QWidget* widget : QApplication::topLevelWidgets()) {
				if (auto* dialog = qobject_cast<QFileDialog*>(widget))
					if (dialog->isVisible())
						dialog->reject();
			}
		});
		rejectSaveAs.start();
		acceptMessageBox(QMessageBox::Save);
		openRecent(bundle);
		rejectSaveAs.stop();
		if (!w.m_document->isLoaded() || !w.m_session->m_sceneDir.isEmpty() || !w.m_document->isDirty()
			|| w.m_document->mutableModel().name != "retained after failed save") {
			fail(QStringLiteral("cancelled Save As replaced the unsaved current case"));
			return;
		}
		acceptMessageBox(QMessageBox::Discard);
		if (!openRecent(bundle) || w.m_session->m_sceneDir != bundle || w.m_document->isDirty()) {
			fail(QStringLiteral("Discard did not continue opening from an unsaved new case"));
			return;
		}
		marker("E2E_CREATOR_PENDING_OPEN_OK");
		w.m_newSceneAction->trigger();
		process();
		QAction* openBundle = w.findChild<QAction*>("actionOpenCaseStudyBundle");
		if (!openBundle) {
			fail(QStringLiteral("public Open Case Study bundle action is unavailable"));
			return;
		}
		acceptFileDialog(bundle, false);
		acceptMessageBox(QMessageBox::Discard);
		openBundle->trigger();
		process();
		if (QFileInfo(w.m_session->m_sceneDir).absoluteFilePath() != bundle || !w.m_session->m_sceneIsBundle
			|| w.m_document->mutableModel().name != "creator_acceptance_case"
			|| w.m_document->mutableModel().blocks.size() != 6 || w.m_document->mutableModel().trainUnits.size() != 2
			|| w.m_document->mutableModel().compositions.size() != 1 || w.m_document->mutableModel().services.size() != 1
			|| w.m_document->mutableModel().scenarios.size() != 3 || w.m_document->mutableModel().passengers.size() != 1
			|| w.m_document->mutableModel().services.front().repeatCount != 3
			|| w.m_document->mutableModel().services.front().stops.size() != 2 || hasErrors(w.m_session->m_sceneDiagnostics)) {
			fail(QStringLiteral("bundle reopen did not retain complete creator IDs and values: path=%1 bundle=%2 name=%3 blocks=%4 units=%5 compositions=%6 services=%7 scenarios=%8 passengers=%9 repeat=%10 stops=%11 errors=%12")
					.arg(w.m_session->m_sceneDir)
					.arg(w.m_session->m_sceneIsBundle)
					.arg(QString::fromStdString(w.m_document->mutableModel().name))
					.arg(static_cast<int>(w.m_document->mutableModel().blocks.size()))
					.arg(static_cast<int>(w.m_document->mutableModel().trainUnits.size()))
					.arg(static_cast<int>(w.m_document->mutableModel().compositions.size()))
					.arg(static_cast<int>(w.m_document->mutableModel().services.size()))
					.arg(static_cast<int>(w.m_document->mutableModel().scenarios.size()))
					.arg(static_cast<int>(w.m_document->mutableModel().passengers.size()))
					.arg(w.m_document->mutableModel().services.empty() ? -1 : w.m_document->mutableModel().services.front().repeatCount)
					.arg(w.m_document->mutableModel().services.empty() ? -1 : static_cast<int>(w.m_document->mutableModel().services.front().stops.size()))
					.arg(hasErrors(w.m_session->m_sceneDiagnostics)));
			return;
		}
		if (QAction* detailsAction = w.findChild<QAction*>("actionAdvancedDetails"))
			detailsAction->setChecked(true);
		if (!w.m_loadedDataDock || !w.m_validation->tree()) {
			fail(QStringLiteral("Loaded Data review is unavailable after bundle reopen"));
			return;
		}
		if (!w.m_loadedDataDock->isVisible())
			w.m_loadedDataDock->toggleViewAction()->trigger();
		w.m_loadedDataDock->raise();
		process();
		QTreeWidgetItem* caseRoot = w.m_validation->tree()->topLevelItemCount() > 0
			? w.m_validation->tree()->topLevelItem(0)
			: nullptr;
		bool loadedDataOk = caseRoot && caseRoot->text(0) == "Case Study"
			&& QFileInfo(caseRoot->text(1)).absoluteFilePath() == bundle;
		for (const QString& label : {QStringLiteral("Infrastructure"), QStringLiteral("Scenarios"),
				 QStringLiteral("Passengers"), QStringLiteral("Canonical schema version"),
				 QStringLiteral("Bundle format version")}) {
			const QList<QTreeWidgetItem*> rows = w.m_validation->tree()->findItems(
				label, Qt::MatchExactly | Qt::MatchRecursive, 0);
			loadedDataOk = loadedDataOk && !rows.isEmpty()
				&& rows.front()->text(3) != "Invalid" && rows.front()->text(3) != "Failed";
		}
		if (!w.m_loadedDataDock->isVisible() || !loadedDataOk) {
			fail(QStringLiteral("Loaded Data did not expose the reopened bundle inputs"));
			return;
		}
		marker("E2E_CREATOR_BUNDLE_ROUNDTRIP_OK");
		w.m_speedSlider->setValue(0);
		w.m_runSceneAction->trigger();
		next();
		return;
	}

	if (creatorAcceptance.phase == 9) {
		if (!w.m_worker) {
			fail(QStringLiteral("replacement check did not retain a running worker"));
			return;
		}
		auto* followButton = w.findChild<QToolButton*>("actionFollowButton");
		if (!followButton) {
			fail(QStringLiteral("Follow control unavailable for running replacement"));
			return;
		}
		auto* accessible = QAccessible::queryAccessibleInterface(followButton);
		auto* accessibleActions = accessible ? accessible->actionInterface() : nullptr;
		if (!accessibleActions) {
			fail(QStringLiteral("Follow accessibility actions unavailable"));
			return;
		}
		accessibleActions->doAction(QAccessibleActionInterface::toggleAction());
		process();
		if (!followButton->isChecked() || !w.m_followAction->isChecked()
			|| w.m_followTrainIndex != w.m_followTrainCombo->currentData().toInt()) {
			fail(QStringLiteral("accessible Follow toggle did not activate the selected target"));
			return;
		}
		accessibleActions->doAction(QAccessibleActionInterface::toggleAction());
		process();
		if (followButton->isChecked() || w.m_followAction->isChecked() || w.m_followTrainIndex != -1) {
			fail(QStringLiteral("accessible Follow toggle did not clear the target"));
			return;
		}
		accessibleActions->doAction(QAccessibleActionInterface::toggleAction());
		w.setFollowTrain(-1);
		process();
		if (followButton->isChecked() || w.m_followAction->isChecked() || w.m_followTrainIndex != -1) {
			fail(QStringLiteral("queued accessible Follow toggle overrode an explicit clear"));
			return;
		}
		followButton->click();
		process();
		if (!followButton->isChecked() || !w.m_followAction->isChecked()
			|| w.m_followTrainIndex != w.m_followTrainCombo->currentData().toInt()) {
			fail(QStringLiteral("Follow click toggled the action more than once"));
			return;
		}
		accessibleActions->doAction(QAccessibleActionInterface::toggleAction());
		w.setFollowTrain(w.m_followTrainCombo->currentData().toInt());
		process();
		if (!followButton->isChecked() || !w.m_followAction->isChecked()
			|| w.m_followTrainIndex != w.m_followTrainCombo->currentData().toInt()) {
			fail(QStringLiteral("queued accessible Follow toggle overrode an explicit target"));
			return;
		}
		w.setFollowTrain(-1);
		if (followButton->isChecked()) {
			fail(QStringLiteral("clearing Follow left the toolbar control checked"));
			return;
		}
		w.clearSimulationWorker(true);
		const int completionTarget = w.m_followTrainCombo->currentData().toInt();
		w.setFollowTrain(completionTarget);
		if (completionTarget < 0 || w.m_followTrainIndex != completionTarget) {
			fail(QStringLiteral("completion ordering fixture has no followable train"));
			return;
		}
		w.setFollowTrain(-1);
		accessibleActions->doAction(QAccessibleActionInterface::toggleAction());
		w.onSimulationFinished();
		process();
		if (followButton->isChecked() || w.m_followAction->isChecked() || w.m_followTrainIndex != -1) {
			fail(QStringLiteral("queued accessible Follow toggle survived simulation completion"));
			return;
		}
		w.m_runSceneAction->trigger();
		if (!w.m_worker) {
			fail(QStringLiteral("completion ordering check could not restart the replacement run"));
			return;
		}
		followButton->click();
		w.ui->actionSimulationPause->trigger();
		for (QAction* action : w.m_session->recentScenesMenu()->actions()) {
			if (action->data().toString() == w.m_session->m_sceneDir) {
				action->trigger();
				break;
			}
		}
		// The reload waits for the paused run to stop. Until then the run and the scene are kept.
		if (!w.m_worker || !w.m_afterRunAction || w.ui->actionSimulationStop->isEnabled()
			|| w.statusBar()->currentMessage() != QStringLiteral("Stopping simulation...")) {
			fail(QStringLiteral("reloading during a run did not wait for the run to stop"));
			return;
		}
		QElapsedTimer reloadWait;
		reloadWait.start();
		while ((w.m_worker || w.m_afterRunAction) && reloadWait.elapsed() < 15000)
			QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
		process();
		if (w.m_worker || w.m_resultsAvailable || w.m_runtimeStatus != "Not built"
			|| w.m_followAction->isChecked() || followButton->isChecked() || w.m_followTrainIndex != -1
			|| w.progressBar->isVisible()
			|| w.ui->actionSimulationPause->text() != "Pause"
			|| !w.statusBar()->currentMessage().startsWith("Scene reloaded:")) {
			fail(QStringLiteral("stopped run completion changed the replacement scene"));
			return;
		}
		w.m_speedSlider->setValue(500);
		if (!w.m_document->isLoaded() || !w.m_runSceneAction || !w.m_serviceOccurrenceTable
			|| !w.m_scenarioListWidget || !w.m_setDelayBaselineButton || !w.m_compareDelayButton
			|| !w.m_delayFeedbackLabel) {
			fail(QStringLiteral("run and occurrence controls are unavailable"));
			return;
		}
		if (w.m_setDelayBaselineButton->isEnabled() || w.m_compareDelayButton->isEnabled()
			|| !w.m_delayFeedbackLabel->text().contains(QStringLiteral("No completed run"))) {
			fail(QStringLiteral("delay controls did not expose a disabled no-results reason"));
			return;
		}
		int baselineRow = -1;
		for (int row = 0; row < static_cast<int>(w.m_document->mutableModel().scenarios.size()); ++row)
			if (w.m_document->mutableModel().scenarios[static_cast<std::size_t>(row)].id == "baseline")
				baselineRow = row;
		if (baselineRow < 0) {
			fail(QStringLiteral("baseline scenario was not retained after bundle reopen"));
			return;
		}
		w.m_scenarioListWidget->setCurrentRow(baselineRow);
		process();
		if (w.m_serviceOccurrenceTable->rowCount() != 3) {
			fail(QStringLiteral("three service occurrences were not exposed after reopen"));
			return;
		}
		for (int row = 0; row < w.m_serviceOccurrenceTable->rowCount(); ++row) {
			if (auto* item = w.m_serviceOccurrenceTable->item(row, 0))
				item->setCheckState(row == 2 ? Qt::Unchecked : Qt::Checked);
		}
		process();
		creatorAcceptance.polls = 0;
		w.m_runSceneAction->trigger();
		const quint64 runningRevision = w.m_document->revision();
		const bool runningDirty = w.m_document->isDirty();
		w.commitPendingEditorValues();
		if (!w.m_worker || w.m_document->revision() != runningRevision || w.m_document->isDirty() != runningDirty) {
			fail(QStringLiteral("resolving pending edits during a run changed disabled scenario inputs"));
			return;
		}
		marker("E2E_CREATOR_BASELINE_RUN_STARTED");
		next();
		return;
	}

	if (creatorAcceptance.phase == 10) {
		if (w.m_worker || !w.m_resultsAvailable) {
			if (++creatorAcceptance.polls > 600) {
				fail(QStringLiteral("baseline simulation did not complete"));
				return;
			}
			pollAgain(w, 100);
			return;
		}
		creatorAcceptance.polls = 0;
		if (w.m_completedRun->provenance.appliedScenario != "baseline"
			|| w.m_completedRun->provenance.selectedOccurrences.size() != 2) {
			fail(QStringLiteral("baseline provenance did not retain scenario and selected occurrences 1+2"));
			return;
		}
		if (!w.tabifiedDockWidgets(w.m_loadedDataDock).contains(w.m_runResultsDock)) {
			fail(QStringLiteral("run results do not share the loaded-data dock area"));
			return;
		}
		QAction* timeDistance = w.m_runResultsDock->findChild<QAction*>("resultView_Time/distance");
		if (!timeDistance) {
			fail(QStringLiteral("public Time / distance result control is unavailable"));
			return;
		}
		timeDistance->trigger();
		process();
		for (QWidget* widget : QApplication::topLevelWidgets()) {
			if (auto* diagram = qobject_cast<DiagramWindow*>(widget))
				if (diagram->windowTitle() == QStringLiteral("Time vs Distance"))
					creatorAcceptance.baselineDiagram = diagram;
		}
		if (!creatorAcceptance.baselineDiagram) {
			fail(QStringLiteral("baseline result diagram did not open"));
			return;
		}
		// The title names the chart. The context line names the run.
		creatorAcceptance.baselineContext = chartContextLine(w.m_completedRun->provenance);
		const auto* baselineContext = creatorAcceptance.baselineDiagram->findChild<QLabel*>("diagramContext");
		if (!baselineContext || !baselineContext->isVisible() || baselineContext->text() != creatorAcceptance.baselineContext) {
			fail(QStringLiteral("baseline result diagram does not name its run in the context line: ")
				+ (baselineContext ? baselineContext->text() : QStringLiteral("no context label")));
			return;
		}
		creatorAcceptance.baselineCsv = creatorAcceptance.baselineDiagram->csvContent();
		if (creatorAcceptance.baselineCsv.empty()) {
			fail(QStringLiteral("baseline result diagram has nothing to export"));
			return;
		}
		w.m_setDelayBaselineButton->click();
		if (!w.m_delayBaseline || !w.m_runResultsSummaryLabel || !w.m_delayFeedbackLabel
			|| !w.m_runResultsSummaryLabel->text().contains(QStringLiteral("Delay baseline:"))
			|| !w.m_delayFeedbackLabel->text().startsWith(QStringLiteral("Baseline:"))) {
			fail(QStringLiteral("public delay baseline control did not freeze baseline"));
			return;
		}
		marker("E2E_CREATOR_BASELINE_RUN_OK");
		next();
		return;
	}

	if (creatorAcceptance.phase == 11) {
		int entranceRow = -1;
		for (int row = 0; row < static_cast<int>(w.m_document->mutableModel().scenarios.size()); ++row)
			if (w.m_document->mutableModel().scenarios[static_cast<std::size_t>(row)].id == "entrance")
				entranceRow = row;
		if (entranceRow < 0 || !w.m_scenarioListWidget || !w.m_runSceneAction) {
			fail(QStringLiteral("entrance scenario was not available for public run"));
			return;
		}
		w.m_scenarioListWidget->setCurrentRow(entranceRow);
		process();
		creatorAcceptance.polls = 0;
		w.m_runSceneAction->trigger();
		next();
		return;
	}

	if (creatorAcceptance.phase == 12) {
		if (w.m_worker || !w.m_resultsAvailable) {
			if (++creatorAcceptance.polls > 600) {
				fail(QStringLiteral("entrance-delay simulation did not complete"));
				return;
			}
			pollAgain(w, 100);
			return;
		}
		creatorAcceptance.polls = 0;
		if (w.m_completedRun->provenance.appliedScenario != "entrance") {
			fail(QStringLiteral("entrance run did not stage the entrance scenario"));
			return;
		}
		const auto stationADeparture = [](const std::vector<TimetableResultRow>& rows)
			-> const TimetableResultRow* {
			for (const TimetableResultRow& row : rows)
				if (row.serviceId == "creator-service" && row.occurrence == 2
					&& row.stationId == "Creator A")
					return &row;
			return nullptr;
		};
		const TimetableResultRow* baselineDeparture = w.m_delayBaseline
			? stationADeparture(w.m_delayBaseline->timetable)
			: nullptr;
		const TimetableResultRow* delayedDeparture = stationADeparture(w.m_completedRun->timetable);
		if (!baselineDeparture || !delayedDeparture
			|| !baselineDeparture->plannedDepartureSeconds.available
			|| !delayedDeparture->plannedDepartureSeconds.available
			|| !baselineDeparture->departureDelaySeconds.available
			|| !delayedDeparture->departureDelaySeconds.available
			|| delayedDeparture->plannedDepartureSeconds.value
					- baselineDeparture->plannedDepartureSeconds.value
				< 89.0
			|| std::abs(delayedDeparture->departureDelaySeconds.value
				   - baselineDeparture->departureDelaySeconds.value)
				< 1.0) {
			fail(QStringLiteral("entrance delay did not shift occurrence 2 station-A plan by 90 seconds and change its reported delay: baseline=%1/%2 delayed=%3/%4 rows=%5/%6")
					.arg(baselineDeparture && baselineDeparture->plannedDepartureSeconds.available
							? baselineDeparture->plannedDepartureSeconds.value
							: -1.0)
					.arg(baselineDeparture && baselineDeparture->departureDelaySeconds.available
							? baselineDeparture->departureDelaySeconds.value
							: -1.0)
					.arg(delayedDeparture && delayedDeparture->plannedDepartureSeconds.available
							? delayedDeparture->plannedDepartureSeconds.value
							: -1.0)
					.arg(delayedDeparture && delayedDeparture->departureDelaySeconds.available
							? delayedDeparture->departureDelaySeconds.value
							: -1.0)
					.arg(w.m_delayBaseline ? static_cast<int>(w.m_delayBaseline->timetable.size()) : -1)
					.arg(static_cast<int>(w.m_completedRun->timetable.size())));
			return;
		}
		// The values that the delay analysis reads carry the entrance delay and say which time of each call exists.
		const DelayRunSnapshot entranceSnapshot = w.completedDelaySnapshot();
		const TrainRunResult* delayedTrain = nullptr;
		for (const TrainRunResult& train : entranceSnapshot.run.trains)
			if (train.serviceId == "creator-service" && train.occurrence == 2)
				delayedTrain = &train;
		const TimetableResultRow* snapshotDeparture = stationADeparture(entranceSnapshot.timetable);
		if (!entranceSnapshot.hasEntranceDelays || entranceSnapshot.hasIncidents || !delayedTrain
			|| std::abs(delayedTrain->entranceDelaySeconds - 90.0) > 1e-9 || delayedTrain->entranceDelayStationId != "Creator A"
			|| !snapshotDeparture || snapshotDeparture->departureStatus != TimetableValueStatus::Available) {
			fail(QStringLiteral("the delay snapshot of the entrance run lacks the 90 second entrance delay of occurrence 2 or the status of its departure"));
			return;
		}
		// The comparison accepts a scenario with entrance delays and shows the entrance delay as the input delay.
		if (!w.m_delayFeedbackLabel || !w.m_compareDelayButton->isEnabled()
			|| !w.m_delayFeedbackLabel->text().contains(QStringLiteral("Compare delays shows"))) {
			fail(QStringLiteral("entrance-delay result did not enable the delay comparison"));
			return;
		}
		w.m_compareDelayButton->click();
		process();
		DelayAnalysisWindow* entranceWindow = newestDelayWindow();
		const auto* entranceTabs = entranceWindow ? entranceWindow->findChild<QTabWidget*>("delayAnalysisTabs") : nullptr;
		const auto* entranceInputs = entranceWindow ? entranceWindow->findChild<QTableWidget*>("delayBaselineOccurrences") : nullptr;
		const auto inputDelayOf = [entranceInputs](const QString& occurrence) {
			for (int row = 0; entranceInputs && row < entranceInputs->rowCount(); ++row)
				if (entranceInputs->item(row, 1)->text() == occurrence)
					return entranceInputs->item(row, 4)->text() + QStringLiteral(" at ") + entranceInputs->item(row, 5)->text();
			return QString();
		};
		if (!entranceTabs || entranceTabs->currentIndex() != 2 || !entranceInputs || !entranceInputs->isVisible()
			|| entranceInputs->rowCount() != 2 || inputDelayOf(QStringLiteral("2")) != QStringLiteral("90 at Creator A")
			|| inputDelayOf(QStringLiteral("1")) != QStringLiteral("0 at ")) {
			fail(QStringLiteral("the comparison of the entrance run did not show the entrance delay as the input delay: occurrence 1 '%1', occurrence 2 '%2'")
					.arg(inputDelayOf(QStringLiteral("1")), inputDelayOf(QStringLiteral("2"))));
			return;
		}
		entranceWindow->close();
		process();
		marker("E2E_CREATOR_ENTRANCE_RUN_OK");
		int incidentRow = -1;
		for (int row = 0; row < static_cast<int>(w.m_document->mutableModel().scenarios.size()); ++row)
			if (w.m_document->mutableModel().scenarios[static_cast<std::size_t>(row)].id == "incident")
				incidentRow = row;
		if (incidentRow < 0 || !w.m_scenarioListWidget || !w.m_runSceneAction) {
			fail(QStringLiteral("incident scenario was not available for final public run"));
			return;
		}
		w.m_scenarioListWidget->setCurrentRow(incidentRow);
		process();
		creatorAcceptance.polls = 0;
		w.m_runSceneAction->trigger();
		next();
		return;
	}

	if (creatorAcceptance.phase == 13) {
		if (w.m_worker || !w.m_resultsAvailable) {
			if (++creatorAcceptance.polls > 600) {
				fail(QStringLiteral("incident simulation did not complete"));
				return;
			}
			pollAgain(w, 100);
			return;
		}
		creatorAcceptance.polls = 0;
		bool directEvidence = false;
		for (const auto& train : w.m_completedRun->results.trains)
			if (!train.directIncidentIds.empty())
				directEvidence = true;
		if (w.m_completedRun->provenance.appliedScenario != "incident" || !directEvidence) {
			fail(QStringLiteral("final incident run lacked direct incident evidence"));
			return;
		}
		if (!w.m_compareDelayButton->isEnabled() || !w.m_delayFeedbackLabel
			|| !w.m_delayFeedbackLabel->text().contains(QStringLiteral("Compare delays shows"))) {
			fail(QStringLiteral("incident result did not expose an enabled delay comparison next step"));
			return;
		}
		// With the advanced details on, the summary says for how many of the trains the run recorded direct incident evidence.
		int trainsWithEvidence = 0;
		for (const auto& train : w.m_completedRun->results.trains)
			trainsWithEvidence += train.directIncidentIds.empty() ? 0 : 1;
		w.m_advancedDetailsAction->setChecked(true);
		const QString evidenceSummary = w.m_runResultsSummaryLabel->text();
		w.m_advancedDetailsAction->setChecked(false);
		if (!evidenceSummary.contains(QString("Direct incident evidence recorded for %1 of %2 train")
					.arg(trainsWithEvidence)
					.arg(static_cast<int>(w.m_completedRun->results.trains.size())))) {
			fail(QStringLiteral("the run summary does not say for how many trains direct incident evidence was recorded: ") + evidenceSummary);
			return;
		}
		marker("E2E_CREATOR_INCIDENT_RUN_OK");
		next();
		return;
	}

	if (creatorAcceptance.phase == 14) {
		const QString exportDir = emitPath(qEnvironmentVariable("QEGTRAIN_E2E_CREATOR_EXPORT_DIR"));
		if (!w.m_resultsAvailable || exportDir.isEmpty() || !w.m_runResultsDock) {
			fail(QStringLiteral("public result controls or export target are unavailable"));
			return;
		}
		QDir().mkpath(exportDir);
		auto path = [&](const char* name) { return QDir(exportDir).filePath(QString::fromLatin1(name)); };
		auto exportButton = [&](QWidget* window, const QString& text, const QString& target) {
			if (!window)
				return false;
			QPushButton* button = nullptr;
			for (QPushButton* candidate : window->findChildren<QPushButton*>())
				if (candidate->text() == text) {
					button = candidate;
					break;
				}
			if (!button)
				return false;
			acceptFileDialog(target, false);
			button->click();
			process();
			return true;
		};
		auto findDiagram = [&](const QString& titlePart) -> DiagramWindow* {
			for (QWidget* widget : QApplication::topLevelWidgets())
				if (auto* diagram = qobject_cast<DiagramWindow*>(widget))
					if (diagram->windowTitle().contains(titlePart))
						return diagram;
			return nullptr;
		};
		auto findResultAction = [&](const QString& text) -> QAction* {
			for (QAction* action : w.m_runResultsDock->findChildren<QAction*>())
				if (action->text() == text && action->objectName().startsWith("resultView_"))
					return action;
			return nullptr;
		};

		if (!creatorAcceptance.baselineDiagram
			|| !exportButton(creatorAcceptance.baselineDiagram, QStringLiteral("Export CSV..."),
				path("baseline_time_distance.csv"))) {
			fail(QStringLiteral("baseline diagram CSV export was not driven through its public button"));
			return;
		}
		// The baseline run is two runs behind. Its diagram still exports it, not the run the application holds now.
		if (creatorAcceptance.baselineDiagram->csvContent() != creatorAcceptance.baselineCsv
			|| creatorAcceptance.baselineCsv == trajectoryCsvFrom(*w.m_completedRun, trainIdsFrom(*w.m_completedRun))) {
			fail(QStringLiteral("the baseline diagram did not keep exporting the baseline run after later runs"));
			return;
		}
		const auto* baselineContext = creatorAcceptance.baselineDiagram->findChild<QLabel*>("diagramContext");
		const QString currentContext = chartContextLine(w.m_completedRun->provenance);
		if (!baselineContext || baselineContext->text() != creatorAcceptance.baselineContext
			|| currentContext == creatorAcceptance.baselineContext) {
			fail(QStringLiteral("the baseline diagram did not keep naming the baseline run after later runs: ")
				+ (baselineContext ? baselineContext->text() : QStringLiteral("no context label")));
			return;
		}
		QAction* speedButton = findResultAction(QStringLiteral("Speed / distance"));
		if (!speedButton) {
			fail(QStringLiteral("speed/distance result control is unavailable"));
			return;
		}
		speedButton->trigger();
		process();
		DiagramWindow* speedDiagram = findDiagram(QStringLiteral("Speed vs Distance"));
		if (!speedDiagram
			|| !exportButton(speedDiagram, QStringLiteral("Export CSV..."), path("trajectory.csv"))
			|| !exportButton(speedDiagram, QStringLiteral("Export PNG..."), path("trajectory.png"))) {
			fail(QStringLiteral("current trajectory/speed exports were not driven through the diagram"));
			return;
		}
		const auto* speedContext = speedDiagram->findChild<QLabel*>("diagramContext");
		if (!speedContext || !speedContext->isVisible() || speedContext->text() != currentContext) {
			fail(QStringLiteral("the speed diagram does not name the current run in its context line: ")
				+ (speedContext ? speedContext->text() : QStringLiteral("no context label")));
			return;
		}
		QAction* tractiveButton = findResultAction(QStringLiteral("Tractive effort / distance"));
		if (!tractiveButton) {
			fail(QStringLiteral("tractive-effort result control is unavailable"));
			return;
		}
		tractiveButton->trigger();
		process();
		DiagramWindow* tractiveDiagram = findDiagram(QStringLiteral("Simulated Tractive Effort vs Distance"));
		if (!tractiveDiagram
			|| !exportButton(tractiveDiagram, QStringLiteral("Export CSV..."), path("tractive_effort.csv"))
			|| !exportButton(tractiveDiagram, QStringLiteral("Export PNG..."), path("tractive_effort.png"))) {
			fail(QStringLiteral("simulated tractive-effort exports were not driven through the diagram"));
			return;
		}
		const auto* tractiveContext = tractiveDiagram->findChild<QLabel*>("diagramContext");
		if (!tractiveContext || !tractiveContext->isVisible() || tractiveContext->text() != currentContext) {
			fail(QStringLiteral("the tractive-effort diagram does not name the current run in its context line: ")
				+ (tractiveContext ? tractiveContext->text() : QStringLiteral("no context label")));
			return;
		}
		// A run with 300-character names: the title stays the name of the chart, the context line holds the run and stays
		// within three lines, and the chart title that a PNG shows still has the run in brackets.
		{
			const std::shared_ptr<const CompletedRunData> kept = w.m_completedRun;
			auto longRun = std::make_shared<CompletedRunData>(*kept);
			longRun->provenance.caseName = std::string(300, 'c');
			longRun->provenance.appliedScenario = std::string(300, 's');
			w.m_completedRun = longRun;
			const QString run = QString(300, QLatin1Char('c')) + QStringLiteral(" / ") + QString(300, QLatin1Char('s'));
			const char* const chartTitles[] = {"Speed vs Distance", "Speed vs Time", "Time vs Distance",
				"Simulated Tractive Effort vs Distance", "Arrival delay along journey (minutes)"};
			QString problem;
			for (int chart = 0; chart < 5 && problem.isEmpty(); ++chart) {
				if (chart < 4)
					w.buildPerTrainDiagram(chart);
				else
					w.showDelayDiagram();
				process();
				const auto windows = w.findChildren<DiagramWindow*>();
				DiagramWindow* window = windows.isEmpty() ? nullptr : windows.last();
				const auto* context = window ? window->findChild<QLabel*>("diagramContext") : nullptr;
				const auto* view = window ? window->findChild<QChartView*>() : nullptr;
				const QString title = QString::fromLatin1(chartTitles[chart]);
				if (!window || !context || !view)
					problem = title + QStringLiteral(" did not open");
				else if (window->windowTitle() != title)
					problem = title + QStringLiteral(" has the window title ") + window->windowTitle();
				else if (!context->isVisible() || context->text() != QStringLiteral("Run: ") + run
					|| !context->toolTip().contains(run))
					problem = title + QStringLiteral(" does not show the run in its context line");
				else if (context->height() > context->fontMetrics().height() * 3
					|| window->width() > window->screen()->availableGeometry().width() * 9 / 10)
					problem = title + QStringLiteral(" does not keep its size with a long run");
				else if (view->chart()->title() != title + QStringLiteral(" [") + run + QStringLiteral("]"))
					problem = title + QStringLiteral(" has the chart title ") + view->chart()->title();
				if (window)
					window->close();
			}
			w.m_completedRun = kept;
			process();
			if (!problem.isEmpty()) {
				fail(QStringLiteral("result chart with a long run identity: ") + problem);
				return;
			}
		}
		QAction* timetableButton = findResultAction(QStringLiteral("Timetable"));
		if (!timetableButton) {
			fail(QStringLiteral("timetable result control is unavailable"));
			return;
		}
		timetableButton->trigger();
		process();
		TimetableTableWindow* timetable = nullptr;
		for (QWidget* widget : QApplication::topLevelWidgets())
			if (auto* candidate = qobject_cast<TimetableTableWindow*>(widget))
				timetable = candidate;
		if (!timetable
			|| !exportButton(timetable, QStringLiteral("Export CSV..."), path("timetable.csv"))
			|| !exportButton(timetable, QStringLiteral("Export PNG..."), path("timetable.png"))) {
			fail(QStringLiteral("timetable exports were not driven through the public table"));
			return;
		}
		QAction* blockingButton = findResultAction(QStringLiteral("Blocking time"));
		if (!blockingButton) {
			fail(QStringLiteral("blocking-time result control is unavailable"));
			return;
		}
		acceptMessageBox();
		blockingButton->trigger();
		process();
		DiagramWindow* blocking = findDiagram(QStringLiteral("Blocking time:"));
		if (!blocking
			|| !exportButton(blocking, QStringLiteral("Export CSV..."), path("blocking_time.csv"))
			|| !exportButton(blocking, QStringLiteral("Export PNG..."), path("blocking_time.png"))) {
			fail(QString("blocking-time exports were not driven through the public diagram (window %1)")
					.arg(blocking ? "present" : "absent"));
			return;
		}
		QPushButton* summaryCsv = w.findChild<QPushButton*>("resultView_ExportCSV");
		QPushButton* summaryPng = w.findChild<QPushButton*>("resultView_ExportPNG");
		if (!summaryCsv || !summaryPng) {
			fail(QStringLiteral("run-summary result controls are unavailable"));
			return;
		}
		acceptFileDialog(path("run_summary.csv"), false);
		summaryCsv->click();
		process();
		acceptFileDialog(path("run_summary.png"), false);
		summaryPng->click();
		process();

		auto boundedResultDialog = [process](QDialog* dialog, QTableWidget* table,
									   QPushButton* secondary) {
			if (!dialog || !table || !secondary) return false;
			auto* scroll = dialog->findChild<QScrollArea*>(QStringLiteral("dialogBodyScroll"));
			auto* context = dialog->findChild<QLabel*>(QStringLiteral("dialogContext"));
			auto* heading = dialog->findChild<QLabel*>(QStringLiteral("dialogHeading"));
			auto* buttons = dialog->findChild<QDialogButtonBox*>();
			if (!scroll || !context || !heading || !buttons || !buttons->button(QDialogButtonBox::Close)
				|| !scroll->isAncestorOf(table) || !scroll->isAncestorOf(secondary)
				|| scroll->isAncestorOf(buttons) || !context->wordWrap()
				|| context->textFormat() != Qt::PlainText || heading->textFormat() != Qt::PlainText)
				return false;
			QScreen* screen = QGuiApplication::screenAt(dialog->mapToGlobal(QPoint(0, 0)));
			if (!screen) screen = QGuiApplication::primaryScreen();
			if (!screen) return false;
			const QRect available = screen->availableGeometry();
			const auto bounded = [dialog, &available]() {
				return dialog->width() <= available.width() * 9 / 10
					&& dialog->height() <= available.height() * 4 / 5;
			};
			if (!bounded()) {
				qWarning() << "Result dialog exceeds screen bounds:" << dialog->windowTitle()
						   << dialog->size() << available;
				return false;
			}
			const QString originalContext = context->text();
			const int rows = table->rowCount();
			const int columns = table->columnCount();
			const QString firstCell = rows && columns && table->item(0, 0)
				? table->item(0, 0)->text()
				: QString();
			const QFont originalFont = dialog->font();
			QFont largeFont = originalFont;
			if (largeFont.pointSizeF() > 0) largeFont.setPointSizeF(largeFont.pointSizeF() * 1.5);
			else largeFont.setPixelSize(qMax(18, largeFont.pixelSize() * 3 / 2));
			dialog->setFont(largeFont);
			context->setText(QStringLiteral("<case & scenario> ").repeated(400) + originalContext);
			// Modal timer callbacks can leave nested layout requests queued. Settle
			// both the content and viewport before checking the scroll range.
			QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
			process();
			QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
			const int scrollRange = scroll->verticalScrollBar()->maximum();
			scroll->verticalScrollBar()->setValue(scrollRange);
			const bool intact = bounded() && scrollRange > 0
				&& buttons->button(QDialogButtonBox::Close)->isVisible()
				&& buttons->geometry().bottom() <= dialog->height()
				&& scroll->geometry().bottom() <= buttons->geometry().top()
				&& table->rowCount() == rows && table->columnCount() == columns
				&& (firstCell.isEmpty() || (table->item(0, 0) && table->item(0, 0)->text() == firstCell));
			if (!intact)
				qWarning() << "Result dialog stress check failed:" << dialog->windowTitle()
						   << dialog->size() << available << "scroll range" << scrollRange
						   << "Close visible" << buttons->button(QDialogButtonBox::Close)->isVisible();
			context->setText(originalContext);
			dialog->setFont(originalFont);
			scroll->verticalScrollBar()->setValue(0);
			process();
			return intact;
		};
		QAction* capacityButton = findResultAction(QStringLiteral("Capacity"));
		if (!capacityButton) {
			fail(QStringLiteral("capacity result control is unavailable"));
			return;
		}
		acceptCapacityScope();
		capacityButton->trigger();
		process();
		QDialog* capacity = nullptr;
		for (QWidget* widget : QApplication::topLevelWidgets())
			if (auto* dialog = qobject_cast<QDialog*>(widget))
				if (!dialog->isModal() && dialog->windowTitle().contains(QStringLiteral("Capacity analysis")))
					capacity = dialog;
		QPushButton* capacityExport = nullptr;
		if (capacity)
			for (QPushButton* button : capacity->findChildren<QPushButton*>())
				if (button->text() == QStringLiteral("Export capacity CSV...")) capacityExport = button;
		auto* capacityTabs = capacity ? capacity->findChild<QTabWidget*>() : nullptr;
		auto* pairTable = capacityTabs ? qobject_cast<QTableWidget*>(capacityTabs->widget(0)) : nullptr;
		auto* compressionTable = capacityTabs && capacityTabs->count() > 1
			? qobject_cast<QTableWidget*>(capacityTabs->widget(1))
			: nullptr;
		auto* criticalTable = capacityTabs && capacityTabs->count() > 2
			? qobject_cast<QTableWidget*>(capacityTabs->widget(2))
			: nullptr;
		if (!capacity || !capacityTabs || capacityTabs->count() != 3 || !pairTable
			|| pairTable->columnCount() != 9 || pairTable->rowCount() == 0
			|| !compressionTable || compressionTable->columnCount() != 7
			|| !criticalTable || criticalTable->columnCount() != 5
			|| !boundedResultDialog(capacity, pairTable, capacityExport)
			|| !exportButton(capacity, QStringLiteral("Export capacity CSV..."), path("capacity_analysis.csv"))) {
			fail(QStringLiteral("capacity analysis did not open and export through public controls"));
			return;
		}
		const auto capacityLabels = capacity->findChildren<QLabel*>("resultContextText");
		const bool validCapacityPercentage = std::any_of(capacityLabels.cbegin(), capacityLabels.cend(),
			[](const QLabel* label) {
				return label->text().startsWith(QStringLiteral("Cycle / occupation time:"))
					&& label->text().contains(QStringLiteral("%."))
					&& !label->text().contains(QStringLiteral("%%"));
			});
		if (!validCapacityPercentage) {
			fail(QStringLiteral("capacity calculation details have an invalid percentage unit"));
			return;
		}
		const auto* capacityHeading = capacity->findChild<QLabel*>(QStringLiteral("dialogHeading"));
		if (!capacityHeading || !capacityHeading->text().startsWith(QStringLiteral("Selected train chain: cycle "))
			|| !capacityHeading->text().endsWith(QStringLiteral("% of period; not network capacity"))) {
			fail(QStringLiteral("capacity analysis does not say that it describes the selected train chain"));
			return;
		}
		QPushButton* compressedButton = nullptr;
		for (QPushButton* candidate : capacity->findChildren<QPushButton*>())
			if (candidate->text().contains(QStringLiteral("compressed blocking-time")))
				compressedButton = candidate;
		if (!compressedButton || !capacity->findChild<QScrollArea*>(QStringLiteral("dialogBodyScroll"))->isAncestorOf(compressedButton)) {
			fail(QStringLiteral("compressed blocking-time control is unavailable"));
			return;
		}
		compressedButton->click();
		process();
		DiagramWindow* compressed = findDiagram(QStringLiteral("Compressed blocking-time diagram"));
		if (!compressed
			|| !exportButton(compressed, QStringLiteral("Export CSV..."), path("capacity_compressed_blocking_time.csv"))
			|| !exportButton(compressed, QStringLiteral("Export PNG..."), path("capacity_compressed_blocking_time.png"))) {
			fail(QStringLiteral("compressed capacity diagram did not export"));
			return;
		}
		const auto* compressedView = compressed->findChild<QChartView*>();
		const auto* compressedSections = compressedView ? compressedView->chart()->findChild<QObject*>("blockSectionOverlay") : nullptr;
		if (!compressedSections || compressedSections->property("sectionCount").toInt() <= 0) {
			fail(QStringLiteral("compressed capacity diagram has no physical section layer"));
			return;
		}
		capacity->close();
		QAction* areaButton = findResultAction(QStringLiteral("Capacity by area"));
		if (!areaButton) {
			fail(QStringLiteral("capacity by area result control is unavailable"));
			return;
		}
		acceptAreaCapacityScope();
		areaButton->trigger();
		process();
		AreaCapacityWindow* area = nullptr;
		for (QWidget* widget : QApplication::topLevelWidgets())
			if (auto* window = dynamic_cast<AreaCapacityWindow*>(widget); window && window->isVisible())
				area = window;
		if (!area || area->isModal()
			|| !exportButton(area, QStringLiteral("Export area capacity CSV..."), path("area_capacity.csv"))) {
			fail(QStringLiteral("capacity by area did not open and export through public controls"));
			return;
		}
		area->close();
		QPushButton* compareButton = w.findChild<QPushButton*>("compareDelayButton");
		QAction* delayAnalysisResult = findResultAction(QStringLiteral("Delay analysis"));
		QAction* delayAnalysisMenu = nullptr;
		for (QAction* action : w.m_diagramsMenu->actions())
			if (action->text() == QStringLiteral("Delay analysis..."))
				delayAnalysisMenu = action;
		if (!compareButton || !delayAnalysisResult || !delayAnalysisMenu || !delayAnalysisMenu->isEnabled()) {
			fail(QStringLiteral("delay analysis result controls are unavailable"));
			return;
		}
		const auto delayChild = [](DelayAnalysisWindow* window, const char* name) {
			return window ? window->findChild<QWidget*>(name) : nullptr;
		};
		const auto delayTable = [&delayChild](DelayAnalysisWindow* window, const char* name) {
			return qobject_cast<QTableWidget*>(delayChild(window, name));
		};
		const auto delayLabelText = [&delayChild](DelayAnalysisWindow* window, const char* name) {
			const auto* label = qobject_cast<QLabel*>(delayChild(window, name));
			return label ? label->text() : QString();
		};
		const auto delayTab = [&delayChild](DelayAnalysisWindow* window) {
			const auto* tabs = qobject_cast<QTabWidget*>(delayChild(window, "delayAnalysisTabs"));
			return tabs ? tabs->currentIndex() : -1;
		};

		// The Diagrams menu and the result views open the window on its Calls tab, which exports delay_analysis.csv.
		delayAnalysisMenu->trigger();
		process();
		DelayAnalysisWindow* menuWindow = newestDelayWindow();
		if (!menuWindow || menuWindow->isModal() || delayTab(menuWindow) != 0) {
			fail(QStringLiteral("the Diagrams menu did not open the delay analysis on its Calls tab"));
			return;
		}
		menuWindow->close();
		process();
		delayAnalysisResult->trigger();
		process();
		DelayAnalysisWindow* callsWindow = newestDelayWindow();
		const QTableWidget* callsTable = delayTable(callsWindow, "delayCalls");
		if (!callsWindow || delayTab(callsWindow) != 0 || !callsTable || callsTable->rowCount() == 0
			|| callsWindow->suggestedFileName() != QStringLiteral("delay_analysis.csv")
			|| !exportButton(callsWindow, QStringLiteral("Export CSV..."), path("delay_analysis.csv"))) {
			fail(QStringLiteral("the delay analysis did not open on its Calls tab and export through public controls"));
			return;
		}
		callsWindow->close();
		process();

		// The Compare delays button opens it on the Against baseline tab. The tab exports delay_comparison.csv.
		const auto openComparison = [&]() -> DelayAnalysisWindow* {
			compareButton->click();
			process();
			DelayAnalysisWindow* window = newestDelayWindow();
			return delayTab(window) == 2 && window->suggestedFileName() == QStringLiteral("delay_comparison.csv") ? window : nullptr;
		};
		const auto delayEvidenceShown = [&delayTable](DelayAnalysisWindow* window, const QString& prefix) {
			const QTableWidget* occurrences = delayTable(window, "delayBaselineOccurrences");
			for (int row = 0; occurrences && row < occurrences->rowCount(); ++row)
				if (!occurrences->item(row, 6)->text().startsWith(prefix))
					return false;
			return occurrences && occurrences->rowCount() > 0;
		};

		DelayAnalysisWindow* incidentWindow = openComparison();
		const QTableWidget* incidentOccurrences = delayTable(incidentWindow, "delayBaselineOccurrences");
		bool incidentEvidenceShown = false;
		for (int row = 0; incidentOccurrences && row < incidentOccurrences->rowCount(); ++row)
			incidentEvidenceShown = incidentEvidenceShown || incidentOccurrences->item(row, 6)->text().startsWith(QStringLiteral("Yes:"));
		if (!incidentOccurrences || !incidentOccurrences->isVisible() || incidentOccurrences->rowCount() != 2 || !incidentEvidenceShown) {
			fail(QStringLiteral("Compare delays did not open the Against baseline tab with the incident run and its direct evidence"));
			return;
		}
		incidentWindow->close();
		process();

		// The comparison reads the completed run, so each case below swaps in an altered copy of it and restores it after.
		const std::shared_ptr<const CompletedRunData> incidentRun = w.m_completedRun;
		const auto alterRun = [&w, incidentRun](const std::function<void(CompletedRunData&)>& change) {
			auto altered = std::make_shared<CompletedRunData>(*incidentRun);
			change(*altered);
			w.m_completedRun = altered;
		};
		const auto restoreRun = [&w, incidentRun]() {
			w.m_completedRun = incidentRun;
			w.refreshRunResults();
		};
		const std::vector<TimetableResultRow> baselineTimetable = w.m_delayBaseline
			? w.m_delayBaseline->timetable
			: std::vector<TimetableResultRow>();
		bool injectedPositiveDelay = false;
		alterRun([&baselineTimetable, &injectedPositiveDelay](CompletedRunData& run) {
			run.timetable = baselineTimetable;
			for (TimetableResultRow& row : run.timetable) {
				if (row.simulatedArrivalSeconds.available) {
					row.simulatedArrivalSeconds.value += 15.0;
					injectedPositiveDelay = true;
				}
			}
		});
		if (!injectedPositiveDelay) {
			restoreRun();
			fail(QStringLiteral("creator timetable had no final arrival row for positive comparison presentation"));
			return;
		}
		w.refreshRunResults();
		DelayAnalysisWindow* laterWindow = openComparison();
		QTableWidget* laterOccurrences = delayTable(laterWindow, "delayBaselineOccurrences");
		QPushButton* laterExport = qobject_cast<QPushButton*>(delayChild(laterWindow, "delayAnalysisExport"));
		const auto* laterHeading = laterWindow ? laterWindow->findChild<QLabel*>(QStringLiteral("dialogHeading")) : nullptr;
		const auto* laterContext = laterWindow ? laterWindow->findChild<QLabel*>(QStringLiteral("dialogContext")) : nullptr;
		const QString laterTotals = delayLabelText(laterWindow, "delayBaselineTotals");
		const bool laterOk = laterOccurrences && laterOccurrences->rowCount() == 2 && laterHeading
			&& laterHeading->text().startsWith(QStringLiteral("Delay analysis")) && laterContext
			&& laterContext->text().contains("Run:") && laterContext->text().contains("Baseline:")
			&& laterTotals.contains(QStringLiteral("Positive additional delay: +"))
			&& boundedResultDialog(laterWindow, laterOccurrences, laterExport);
		if (!laterOk || !exportButton(laterWindow, QStringLiteral("Export CSV..."), path("delay_comparison.csv"))) {
			restoreRun();
			fail(QStringLiteral("a later run did not show its additional delay and export through the Against baseline tab: ") + laterTotals);
			return;
		}
		laterWindow->close();
		process();

		alterRun([&baselineTimetable](CompletedRunData& run) { run.timetable = baselineTimetable; });
		w.refreshRunResults();
		DelayAnalysisWindow* sameWindow = openComparison();
		const QString sameTotals = delayLabelText(sameWindow, "delayBaselineTotals");
		const QTableWidget* sameOccurrences = delayTable(sameWindow, "delayBaselineOccurrences");
		const bool sameOk = sameOccurrences && sameOccurrences->rowCount() == 2
			&& sameTotals.contains(QStringLiteral("Final arrival later: 0, same: 2, earlier: 0, not available: 0"))
			&& sameTotals.contains(QStringLiteral("Positive additional delay: 0 s"));
		if (sameWindow)
			sameWindow->close();
		process();
		restoreRun();
		if (!sameOk) {
			fail(QStringLiteral("a run equal to the baseline did not report zero additional delay: ") + sameTotals);
			return;
		}

		// A run without recorded direct evidence is still compared. Its evidence column says none was recorded.
		alterRun([](CompletedRunData& run) {
			for (TrainRunResult& train : run.results.trains)
				train.directIncidentIds.clear();
		});
		w.refreshRunResults();
		DelayAnalysisWindow* noEvidenceWindow = openComparison();
		const bool noEvidenceOk = delayEvidenceShown(noEvidenceWindow, QStringLiteral("None recorded"))
			&& delayLabelText(noEvidenceWindow, "delayBaselineNote").contains(QStringLiteral("not shown to be propagated"));
		if (noEvidenceWindow)
			noEvidenceWindow->close();
		process();
		restoreRun();
		if (!noEvidenceOk) {
			fail(QStringLiteral("a run without direct incident evidence was not compared with 'None recorded' evidence"));
			return;
		}

		// A baseline that does not fit is explained with each reason, and nothing is exported.
		alterRun([](CompletedRunData& run) { run.settings.horizonSteps += 1.0; });
		w.refreshRunResults();
		DelayAnalysisWindow* mismatchWindow = openComparison();
		const QTableWidget* reasons = delayTable(mismatchWindow, "delayBaselineDiagnostics");
		const QPushButton* mismatchExport = qobject_cast<QPushButton*>(delayChild(mismatchWindow, "delayAnalysisExport"));
		const bool mismatchOk = reasons && reasons->isVisible() && reasons->rowCount() == 1 && reasons->item(0, 0)->text() == QStringLiteral("duration_differs")
			&& !delayChild(mismatchWindow, "delayBaselineOccurrences")->isVisible() && mismatchExport && !mismatchExport->isEnabled();
		if (mismatchWindow)
			mismatchWindow->close();
		process();
		restoreRun();
		if (!mismatchOk) {
			fail(QStringLiteral("a baseline with another duration was not explained by its reason"));
			return;
		}
		marker("E2E_CREATOR_EXPORTS_OK");
		next();
		return;
	}

	if (creatorAcceptance.phase == 15) {
		if (!w.m_caseSettingsDock || !w.m_caseDescriptionEdit || !w.m_runResultsDock) {
			fail(QStringLiteral("stale-result invalidation controls are unavailable"));
			return;
		}
		w.m_caseSettingsDock->show();
		w.m_caseSettingsDock->raise();
		if (!editLine(w.m_caseDescriptionEdit,
				QStringLiteral("creator acceptance edited after incident exports"))) {
			fail(QStringLiteral("description editing did not commit through editingFinished"));
			return;
		}
		process();
		bool resultButtonsDisabled = true;
		for (QPushButton* button : w.findChildren<QPushButton*>())
			if (button->objectName().startsWith(QStringLiteral("resultView_")))
				resultButtonsDisabled = resultButtonsDisabled && !button->isEnabled();
		for (QAction* action : w.m_runResultsDock->findChildren<QAction*>())
			if (action->objectName().startsWith("resultView_"))
				resultButtonsDisabled = resultButtonsDisabled && !action->isEnabled();
		if (w.m_resultsAvailable || w.m_delayBaseline || w.m_runResultsDock->isVisible() || !resultButtonsDisabled
			|| !w.m_delayFeedbackLabel || !w.m_delayFeedbackLabel->text().contains(QStringLiteral("cleared"))) {
			fail(QStringLiteral("editing case description did not invalidate stale result views"));
			return;
		}
		acceptMessageBox(QMessageBox::Discard);
		if (!w.m_session->requestOpenScene(qEnvironmentVariable("QEGTRAIN_E2E_CREATOR_BUNDLE"))
			|| !w.m_delayBaselineStatus.isEmpty()
			|| !w.m_delayFeedbackLabel->text().contains(QStringLiteral("No completed run"))) {
			fail(QStringLiteral("opening a case retained the previous case's baseline-clear message"));
			return;
		}
		creatorAcceptance.finished = true;
		marker("E2E_CREATOR_ACCEPTANCE_OK");
		QCoreApplication::exit(0);
		return;
	}
}

// Adds a fixture to the open case, draws its preview, starts a run and compares the items of the prepared run with
// those of the preview: pens, z values and stacking order, and the nodes and stations. With measuredPresentation the
// fixture also has the presentation views of a measured case, and both views must show their sizes, positions and
// signal endpoints. It prints E2E_PREVIEW_PREPARED_RUNTIME_PARITY_OK and returns true, or sets failure and returns
// false.
bool MainWindowTestAccess::checkPreviewRuntimeParity(MainWindow& w, QString& failure, bool measuredPresentation) {
	const SceneModel originalFixture = w.m_document->mutableModel();
	// Only this optional, bounded Run fixture prepares native infrastructure.
	// Add a fourth authored point on a directed runnable chain, not a fallback.
	auto arc = std::find_if(w.m_document->mutableModel().arcs.begin(), w.m_document->mutableModel().arcs.end(),
		[](const SceneArc& value) { return value.id == "creator-main-arc-0"; });
	if (arc == w.m_document->mutableModel().arcs.end()) {
		failure = "parity fixture arc missing";
		return false;
	}
	const std::string oldEnd = arc->toNodeId;
	SceneArc second = *arc;
	arc->toNodeId = "parity-midpoint";
	second.id = "parity-second-arc";
	second.fromNodeId = "parity-midpoint";
	second.toNodeId = oldEnd;
	w.m_document->mutableModel().arcs.push_back(second);
	w.m_document->mutableModel().nodes.push_back({"parity-midpoint", "creator-main", 0.5, 0.125});
	w.m_document->mutableModel().stations.push_back({"parity-first-station", "First", false, 0,
		{{"parity-first-platform", {"creator-main-node-0"}}}});

	if (measuredPresentation) {
		// Paimpol-equivalent presentation fixture, still using the authored projection path.
		for (auto& node : w.m_document->mutableModel().nodes) node.yKm = 0.0;
		w.m_document->mutableModel().trackViews = {{"creator-main", 0, 0}, {"creator-yard", 1, 0}};
		w.m_document->mutableModel().stations[0].name = "Koge"; // Exercise the named presentation nudge too.
		w.m_document->mutableModel().stationViews = {
			{w.m_document->mutableModel().stations[0].id, 1.0, 0.0, {{0, 0.0}}, {}},
			{w.m_document->mutableModel().stations[1].id, 1.0, 0.02, {{0, 2.0}}, {}}};
	}
	const qreal expectedScale = 0.45;
	const auto checkPresentation = [&]() {
		const auto closeTo = [](qreal a, qreal b) { return qAbs(a - b) < 1e-6; };
		if (!closeTo(w.m_presenter->m_cachedTrackPreview.presentationScale, expectedScale)) return false;
		int squares = 0, dots = 0, heads = 0, platforms = 0;
		for (auto* item : w.scene->items()) {
			if (auto* square = qgraphicsitem_cast<StationNodeItem*>(item)) {
				if (!closeTo(square->rect().width(), w.m_presenter->station_node_size * expectedScale)) return false;
				++squares;
			} else if (auto* node = qgraphicsitem_cast<NodeItem*>(item)) {
				if (node->rect().width() != 0 && !closeTo(node->rect().width(), w.m_presenter->node_size * expectedScale)) return false;
				++dots;
			} else if (auto* head = qgraphicsitem_cast<SignalItem*>(item)) {
				if (!closeTo(head->rect().width(), w.m_presenter->node_size * expectedScale)) return false;
				const auto line = std::find_if(w.m_presenter->m_cachedTrackPreview.lines.begin(), w.m_presenter->m_cachedTrackPreview.lines.end(),
					[&](const auto& candidate) {
						return w.m_showingTrackPreview ? candidate.id == head->data(PreviewGraphics::TrackId).toString().toStdString()
													   : &candidate == w.m_presenter->cachedTrackLine(head->trackID);
					});
				if (line == w.m_presenter->m_cachedTrackPreview.lines.end()) return false;
				QPointF center, before, after, normal;
				if (!previewPointAtX(*line, head->X, line->displayOffset, center)
					|| !previewPointAtX(*line, head->X - 0.008, line->displayOffset, before)
					|| !previewPointAtX(*line, head->X + 0.008, line->displayOffset, after)
					|| !previewSignalNormal(*line, head->X, normal)) return false;
				const bool reversed = w.m_showingTrackPreview ? head->data(PreviewGraphics::Reversed).toBool() : head->reversedDirection;
				const QPointF expected = (reversed ? before : after)
					+ normal * (reversed ? -30.0 : 30.0) * expectedScale;
				if (QLineF(head->scenePos(), expected).length() > 1e-6) return false;
				// The +/- 0.008 km endpoints remain eight physical metres away.
				if (!closeTo(qAbs(after.x() - center.x()), 8.0)
					|| !closeTo(qAbs(before.x() - center.x()), 8.0)) return false;
				++heads;
			}
			if (auto* platform = qgraphicsitem_cast<PlatformItem*>(item)) {
				if (!closeTo(platform->rect().width(), 5 * w.m_presenter->station_node_size * expectedScale)
					|| !closeTo(platform->rect().height(), 0.9 * w.m_presenter->station_node_size * expectedScale)) return false;
				if (platform->textIcon && !closeTo(platform->textIcon->scale(), expectedScale)) return false;
				++platforms;
			}
			if (auto* track = qgraphicsitem_cast<TrackLineItem*>(item))
				if (!track->pen().isCosmetic()
					|| track->pen().width() != (track->pen().color() == kSelectionCueColor ? 4 : 2)) return false;
			if (auto* connection = qgraphicsitem_cast<ConnectionItem*>(item))
				if (!connection->pen().isCosmetic() || connection->pen().width() != 2) return false;
			if (auto* stroke = qgraphicsitem_cast<QGraphicsLineItem*>(item))
				if (stroke->pen().width() > 0 && !stroke->pen().isCosmetic()) return false;
		}
		if (!squares || !dots || !heads || (initial_variables.PAX_GUI && !platforms) || w.m_presenter->m_stationPictures.isEmpty()
			|| w.m_presenter->m_stationLabels.size() != w.m_presenter->m_stationOverlays.size()) return false;
		for (int i = 0; i < w.m_presenter->m_stationOverlays.size(); ++i) {
			const auto* overlay = w.m_presenter->m_stationOverlays[i];
			const auto* picture = w.m_presenter->m_stationPictures.value(w.m_presenter->m_stationOverlays[i], nullptr);
			const auto* label = qgraphicsitem_cast<QGraphicsTextItem*>(w.m_presenter->m_stationLabels[i]);
			const auto source = std::find_if(w.m_document->mutableModel().stations.begin(), w.m_document->mutableModel().stations.end(),
				[&](const auto& station) { return QString::fromStdString(station.name) == overlay->stationName(); });
			if (source == w.m_document->mutableModel().stations.end() || !picture || !label) return false;
			const QPointF textCenter = overlay->stableAnchor()
				+ stationDecorationOffset(w.m_document->mutableModel().stationViews, source->id, source->name) * expectedScale;
			// The scene size, without the enlargement that keeps the artwork readable at Fit.
			const auto sceneSizeRect = [](const QGraphicsItem* item) {
				// The item transform acts after the item scale, so remove it in scaled coordinates.
				const QTransform scaled = QTransform::fromScale(item->scale(), item->scale());
				const QTransform enlargement = scaled * item->transform() * scaled.inverted();
				return (enlargement.inverted() * item->sceneTransform()).mapRect(item->boundingRect());
			};
			const QRectF pictureRect = sceneSizeRect(picture);
			const QRectF labelRect = sceneSizeRect(label);
			if (!closeTo(pictureRect.width(), w.m_presenter->station_size * expectedScale)
				|| !closeTo(picture->scale(), expectedScale) || !closeTo(label->scale(), expectedScale)
				|| label->font().pixelSize() != w.m_presenter->station_size / 5
				|| QLineF(labelRect.center(), textCenter).length() > 1e-6
				|| QLineF(pictureRect.center(), textCenter - QPointF(0, w.m_presenter->station_size * expectedScale / 2)).length() > 1e-6)
				return false;
		}
		return true;
	};
	// The pens, the z values and the stacking order that the preview and a run share, in the view that 'mode' names.
	const auto checkInfrastructurePaint = [&](const char* mode) {
		const QString modeName = QString::fromLatin1(mode);
		const auto reject = [&](const QString& primitive) {
			failure = QStringLiteral("%1 %2").arg(modeName, primitive);
			return false;
		};
		int connectionCount = 0, trackCount = 0, headCount = 0;
		for (auto* item : w.scene->items(Qt::AscendingOrder)) {
			if (auto* connection = qgraphicsitem_cast<ConnectionItem*>(item)) {
				const QPen pen = connection->pen();
				if (pen.color() != QColor(Qt::white) || pen.width() != 2 || !pen.isCosmetic() || pen.capStyle() != Qt::SquareCap)
					return reject(QStringLiteral("connection pen is not white, 2 wide, cosmetic with a square cap"));
				if (!qFuzzyIsNull(connection->zValue()))
					return reject(QStringLiteral("connection z is not 0"));
				if (trackCount > 0)
					return reject(QStringLiteral("connection is drawn over a track line"));
				++connectionCount;
			} else if (auto* track = qgraphicsitem_cast<TrackLineItem*>(item)) {
				if (!qFuzzyIsNull(track->zValue()))
					return reject(QStringLiteral("track line z is not 0"));
				++trackCount;
			} else if (auto* head = qgraphicsitem_cast<SignalItem*>(item)) {
				const QPen pen = head->pen();
				if (pen.color() != QColor(Qt::black) || pen.width() != 0 || pen.style() != Qt::SolidLine)
					return reject(QStringLiteral("signal head pen is not a black solid hairline"));
				++headCount;
			}
		}
		std::fprintf(stdout, "\nE2E_INFRASTRUCTURE_PAINT_ITEMS mode=%s connections=%d tracks=%d heads=%d\n", mode, connectionCount, trackCount, headCount);
		std::fflush(stdout);
		if (!connectionCount)
			return reject(QStringLiteral("has no connection"));
		if (!trackCount)
			return reject(QStringLiteral("has no track line"));
		if (!headCount)
			return reject(QStringLiteral("has no signal head"));
		return true;
	};
	w.m_document->markChanged(SceneDocument::AllChanges);
	w.renderTrackPreview(w.m_document->mutableModel());
	if (!checkInfrastructurePaint("preview"))
		return false;
	if (measuredPresentation && !checkPresentation()) {
		failure = "preview measured presentation dimensions or physical signal endpoints mismatch";
		return false;
	}
	struct Primitive {
		int type;
		QRectF rect;
		QColor fill;
		QColor outline;
	};
	std::vector<Primitive> boundaries;
	for (auto* item : w.scene->items()) {
		if (auto* node = qgraphicsitem_cast<NodeItem*>(item))
			boundaries.push_back({item->type(), node->rect(), node->brush().color(), node->pen().color()});
		else if (auto* square = qgraphicsitem_cast<StationNodeItem*>(item))
			boundaries.push_back({item->type(), square->rect(), square->brush().color(), square->pen().color()});
	}
	const quint64 revision = w.m_document->revision();
	const auto held = [&w, revision]() {
		return w.resolvePreviewItem("arc", "creator-main-arc-0", QString(), false, revision);
	};
	w.showSceneContextMenu(held(), QPointF(), QPoint(), false);
	w.m_speedSlider->setValue(0);
	w.m_runSceneAction->trigger();
	if (!w.m_worker || w.m_showingTrackPreview || held() || w.m_sceneContextMenu
		|| !w.m_previewSelectedStationId.isEmpty() || !w.nodeIDText->text().isEmpty()) {
		failure = "preview-to-prepared-runtime did not clear canonical state/actions";
		return false;
	}
	if (!checkInfrastructurePaint("prepared run")) {
		w.clearSimulationWorker(true);
		return false;
	}
	if (measuredPresentation && !checkPresentation()) {
		failure = "prepared-runtime measured presentation mismatch";
		w.clearSimulationWorker(true);
		return false;
	}
	if (measuredPresentation) {
		// A diagnostic wagon in one physical section: presentation affects width, not length.
		GuiTrainState wagon;
		wagon.routeIndex = 0;
		wagon.wagonCount = 0;
		wagon.length = 100;
		wagon.routeAxisPosition = 800;
		wagon.wagonHeadPositions = {0.8};
		wagon.wagonTailPositions = {0.7};
		QPolygonF polygon;
		w.m_presenter->getTrainPolygon(&polygon, 0, wagon);
		if (polygon.size() != 4 || qAbs(polygon.boundingRect().width() - 100) > 1e-6
			|| qAbs(polygon.boundingRect().height() - 30 * expectedScale) > 1e-6
			|| wagon.length != 100 || wagon.wagonHeadPositions[0] != 0.8 || wagon.wagonTailPositions[0] != 0.7) {
			failure = "train presentation thickness changed physical wagon length/head/tail";
			w.clearSimulationWorker(true);
			return false;
		}
	}
	for (const auto& expected : boundaries) {
		bool found = false;
		for (auto* item : w.scene->items()) {
			QAbstractGraphicsShapeItem* shape = nullptr;
			QRectF rect;
			if (auto* node = qgraphicsitem_cast<NodeItem*>(item)) {
				if (!node->node || node->track < 0) continue;
				shape = node;
				rect = node->rect();
			} else if (auto* square = qgraphicsitem_cast<StationNodeItem*>(item)) {
				if (!square->node || square->track < 0) continue;
				shape = square;
				rect = square->rect();
			}
			if (shape && item->type() == expected.type
				&& QLineF(rect.center(), expected.rect.center()).length() < 1e-6
				&& rect.size() == expected.rect.size() && shape->brush().color() == expected.fill
				&& shape->pen().color() == expected.outline)
				found = true;
		}
		if (!found) {
			failure = QString("prepared-runtime boundary mismatch: type=%1 center=%2,%3 size=%4")
						  .arg(expected.type)
						  .arg(expected.rect.center().x())
						  .arg(expected.rect.center().y())
						  .arg(expected.rect.width());
			w.clearSimulationWorker(true);
			return false;
		}
	}
	for (auto* item : w.scene->items())
		if (item->data(PreviewGraphics::Kind).isValid()) {
			failure = "canonical preview role leaked into prepared runtime";
			w.clearSimulationWorker(true);
			return false;
		}
	w.clearSimulationWorker(true);
	w.m_document->mutableModel() = originalFixture;
	if (measuredPresentation)
		std::fprintf(stdout, "\nE2E_MEASURED_PRESENTATION_PARITY_OK\n");
	std::fprintf(stdout, "\nE2E_PREVIEW_PREPARED_RUNTIME_PARITY_OK\n");
	std::fflush(stdout);
	return true;
}

// Inspects the preview of the open case through the events of its items: the canonical endpoints of nodes, arcs and
// connections and the texts of the inspector. Then it checks a fixture with duplicate station names, hidden tracks
// and authored anchors, and that a rename, a delete and a replacement of an item leave no stale identity. It prints
// E2E_PREVIEW_IDENTITY_LIFETIME_OK and returns true, or sets failure and returns false.
bool MainWindowTestAccess::checkPreviewInfrastructure(MainWindow& w, QString& failure) {
	const auto require = [&](bool condition, const QString& message) {
		if (!condition) failure = message;
		return condition;
	};
	const auto find = [&](const QString& kind, const QString& id) {
		return w.resolvePreviewItem(kind, id, QString(), false, w.m_document->revision());
	};
	const auto click = [&](const QPointF& position) {
		QGraphicsSceneMouseEvent event(QEvent::GraphicsSceneMousePress);
		event.setButton(Qt::LeftButton);
		event.setButtons(Qt::LeftButton);
		event.setScenePos(position);
		event.setPos(w.networkView->mapFromScene(position));
		event.setWidget(w.networkView->viewport());
		w.scene->mousePressEvent(&event);
	};
	const auto clickArtwork = [&](QGraphicsPixmapItem* picture) {
		const QRectF bounds = picture->boundingRect();
		for (int y = 1; y < 20; ++y)
			for (int x = 1; x < 20; ++x) {
				const QPointF local(bounds.left() + bounds.width() * x / 20,
					bounds.top() + bounds.height() * y / 20);
				if (picture->shape().contains(local)) {
					click(picture->mapToScene(local));
					return;
				}
			}
	};
	const SceneModel saved = w.m_document->mutableModel();
	const bool savedDirty = w.m_document->isDirty();
	const quint64 initialRevision = w.m_document->revision();
	const QString editorSelection = w.m_infrastructurePanel->selectionId();
	// Event-driven checks on the actual public-Open result (nonnumeric IDs).
	for (const auto& line : w.m_presenter->m_cachedTrackPreview.lines) {
		for (const auto& point : line.points) {
			auto* item = find("node", QString::fromStdString(point.nodeId));
			if (!item) item = find("station-node", QString::fromStdString(point.nodeId));
			if (!require(item != nullptr, "missing canonical endpoint " + QString::fromStdString(point.nodeId))) return false;
			if (auto* node = qgraphicsitem_cast<NodeItem*>(item)) {
				if (!require(!node->node && node->track == -1 && node->brush().color() == QColor(Qt::lightGray),
						"ordinary preview node paint/backing state")) return false;
			} else if (auto* square = qgraphicsitem_cast<StationNodeItem*>(item)) {
				if (!require(!square->node && square->track == -1, "station preview has backing state")) return false;
			}
			if (!require(item->sceneBoundingRect().center() == QPointF(point.x, point.y + line.displayOffset),
					"preview endpoint coordinate differs from cached topology")) return false;
			// Detail-scale events avoid ambiguity from Fit padding/nearby artwork.
			w.networkView->resetTransform();
			w.networkView->scale(2, 2);
			click(QPointF(point.x, point.y + line.displayOffset));
			if (!require(w.nodeIDText->text() == QString::fromStdString(point.nodeId), "node event canonical ID")) return false;
		}
	}
	for (const auto& arc : saved.arcs) {
		auto* item = qgraphicsitem_cast<TrackLineItem*>(find("arc", QString::fromStdString(arc.id)));
		if (!require(item && item->pen().isCosmetic() && item->pen().color() == classifyTrackSpeed(arc.speedLimitMs).color,
				"preview speed-class arc")) return false;
		const auto from = std::find_if(saved.nodes.begin(), saved.nodes.end(), [&](const SceneNode& node) { return node.id == arc.fromNodeId; });
		const auto to = std::find_if(saved.nodes.begin(), saved.nodes.end(), [&](const SceneNode& node) { return node.id == arc.toNodeId; });
		const double lengthM = std::hypot(to->xKm - from->xKm, to->yKm - from->yKm) * 1000;
		click(item->line().pointAt(0.5));
		if (!require(w.arcIDText->text() == QString::fromStdString(arc.id)
					&& w.arcSpeedLimitText->text().toDouble() == arc.speedLimitMs
					&& std::fabs(w.arcLengthText->text().toDouble() - lengthM) < 1e-6
					&& w.arcGradientText->text().toDouble() == arc.gradientPercent
					&& w.arcCurvatureText->text().toDouble() == arc.curvatureRadiusM
					&& w.arcOperationalStateText->text() == "Unavailable before Run",
				"arc event canonical fields")) return false;
	}
	auto* connection = qgraphicsitem_cast<ConnectionItem*>(find("connection", "creator-switch"));
	if (!require(connection && !connection->connection && connection->pen().color() == QColor(Qt::white)
				&& connection->pen().width() == 2 && connection->pen().isCosmetic(),
			"preview connection paint/backing")) return false;
	click(connection->line().pointAt(0.5));
	if (!require(w.infoDockWidget->windowTitle().contains("creator-switch")
				&& w.connectionXFirstNodeText->text().toDouble() == 1000,
			"connection event canonical fields")) return false;
	if (!require(w.m_document->revision() == initialRevision && w.m_document->isDirty() == savedDirty
				&& w.m_infrastructurePanel->selectionId() == editorSelection && !w.m_worker && w.m_presenter->allArcs.isEmpty()
				&& w.m_presenter->allSignals.isEmpty() && w.m_presenter->allPlatforms.isEmpty(),
			"inspection mutated Open state")) return false;

	const bool unchangedNodes = w.m_document->mutableModel().nodes.size() == saved.nodes.size()
		&& std::equal(saved.nodes.begin(), saved.nodes.end(), w.m_document->mutableModel().nodes.begin(),
			[](const SceneNode& a, const SceneNode& b) {
				return std::tie(a.id, a.trackId, a.xKm, a.yKm) == std::tie(b.id, b.trackId, b.xKm, b.yKm);
			});
	const bool unchangedArcs = w.m_document->mutableModel().arcs.size() == saved.arcs.size()
		&& std::equal(saved.arcs.begin(), saved.arcs.end(), w.m_document->mutableModel().arcs.begin(),
			[](const SceneArc& a, const SceneArc& b) {
				return std::tie(a.id, a.trackId, a.fromNodeId, a.toNodeId, a.curvatureRadiusM, a.gradientPercent, a.speedLimitMs)
					== std::tie(b.id, b.trackId, b.fromNodeId, b.toNodeId, b.curvatureRadiusM, b.gradientPercent, b.speedLimitMs);
			});
	if (!require(unchangedNodes && unchangedArcs, "inspection changed authored geometry/units")) return false;

	// Bounded in-memory authoring fixture: duplicate names/memberships/endpoints,
	// hidden-track incidence, authored Z-before-A anchors, and malformed fallback.
	SceneModel fixture;
	fixture.tracks = {{"Z"}, {"A"}, {"hidden"}, {"fallback"}};
	fixture.nodes = {{"z0", "Z", 0, 0}, {"z1", "Z", 1, 0}, {"z2", "Z", 2, 0}, {"z3", "Z", 3, 0},
		{"a0", "A", 0, 1}, {"a1", "A", 1, 1}, {"a2", "A", 3, 1},
		{"hidden0", "hidden", 1, 2}, {"f0", "fallback", 4, 0}, {"f1", "fallback", 5, 0}, {"f2", "fallback", 6, 0}};
	fixture.arcs = {{"z-arc0", "Z", "z0", "z1", 0, 0, 60}, {"z-arc1", "Z", "z1", "z2", 0, 0, 40},
		{"z-arc2", "Z", "z2", "z3", 0, 0, 20}, {"a-arc0", "A", "a0", "a1", 0, 0, 20},
		{"a-arc1", "A", "a1", "a2", 0, 0, 20}, {"f-arc0", "fallback", "f0", "f1", 0, 0, 20}};
	fixture.blocks = {{"Z-section0", "Z", 1}, {"Z-section1", "Z", 2}, {"A-section0", "A", 1}, {"A-section1", "A", 2}};
	fixture.connections = {{"join-one", "z1", "a1"}, {"join-two", "z1", "a1"}, {"hidden-join", "z1", "hidden0"}};
	fixture.trackViews = {{"Z", 0, 0, true}, {"A", 1, 1, true}, {"hidden", 2, 2, false}};
	fixture.stations = {{"earlier", "Duplicate", false, 0, {{"platform-real", {"z1", "z0", "z2"}}}},
		{"later", "Duplicate", true, 1 + 5e-9, {}}, {"between", "Duplicate", true, 1.5, {}}};
	sceneSignals(fixture) = {{"authored-Z", "@Z-section1@"}, {"authored-A", "@A-section1@"}};
	w.m_document->mutableModel() = fixture;
	++w.m_document->m_revision;
	w.renderTrackPreview(w.m_document->mutableModel());
	const QRectF topologyBounds = w.m_presenter->m_previewFitBounds;
	auto* z0 = qgraphicsitem_cast<NodeItem*>(find("node", "z0"));
	auto* z1 = qgraphicsitem_cast<StationNodeItem*>(find("station-node", "z1"));
	auto* z3 = qgraphicsitem_cast<NodeItem*>(find("node", "z3"));
	if (!require(z0 && z0->rect().width() == 0 && z1 && z3 && z3->rect().width() == w.m_presenter->node_size,
			"first-station/four-node/terminal presentation")) return false;
	if (!require(z1->data(PreviewGraphics::StationId).toString() == "later"
				&& z1->data(PreviewGraphics::PlatformId).toString() == "platform-real"
				&& z1->brush().color() == classifyStation(true, 3).fill,
			"native last-wins fields/hidden and duplicate endpoint incidence")) return false;
	if (!require(find("station-node", "z2") && find("connection", "join-one") && find("connection", "join-two")
				&& find("track", "fallback"),
			"multiple platform nodes/duplicate connections/fallback track")) return false;
	w.networkView->resetTransform();
	w.networkView->scale(2, 2);
	click(z1->rect().center());
	if (!require(w.nodeStationNameText->text() == "Duplicate [later]" && w.nodeXText->text().toDouble() == 1000
				&& w.infoDockWidget->windowTitle().contains("platform-real"),
			QString("station square canonical inspection: name=%1 x=%2 title=%3")
				.arg(w.nodeStationNameText->text(), w.nodeXText->text(), w.infoDockWidget->windowTitle()))) return false;
	StationOverlayItem* earlier = nullptr;
	StationOverlayItem* between = nullptr;
	for (auto* overlay : w.m_presenter->m_stationOverlays) {
		if (overlay->data(PreviewGraphics::Id).toString() == "earlier") earlier = overlay;
		if (overlay->data(PreviewGraphics::Id).toString() == "between") between = overlay;
	}
	const auto& line = w.m_presenter->m_cachedTrackPreview.lines.front();
	QPointF expectedBetween;
	previewPointAtX(line, 1.5, line.displayOffset, expectedBetween);
	if (!require(earlier && between && between->stableAnchor() == expectedBetween,
			"position-only artwork uses authored Z-before-A track")) return false;
	auto* picture = w.m_presenter->m_stationPictures.value(earlier, nullptr);
	if (!require(picture && picture->parentItem() == z1, "artwork bound downward to authored square with different winning station")) return false;
	picture->setPos(picture->pos() + QPointF(150, -150));
	clickArtwork(picture);
	if (!require(w.nodeStationNameText->text() == "Duplicate [earlier]"
				&& w.m_previewSelectedStationId == "earlier",
			"displaced artwork owns canonical station identity")) return false;
	// Position-only artwork has a real station, not an invented node.
	auto* positionPicture = w.m_presenter->m_stationPictures.value(between, nullptr);
	if (!require(positionPicture && !positionPicture->parentItem(), "position-only artwork has no invented parent")) return false;
	positionPicture->setPos(positionPicture->pos() + QPointF(0, -300));
	clickArtwork(positionPicture);
	if (!require(w.nodeIDText->text() == "No node anchor" && w.nodeXText->text().toDouble() == 1500
				&& w.nodeStationNameText->text() == "Duplicate [between]",
			"position-only canonical inspection")) return false;
	const bool oldLayer = w.m_presenter->m_stationLayerVisible, oldNames = w.m_presenter->m_stationNamesVisible;
	w.m_presenter->m_stationLayerVisible = false;
	w.m_presenter->m_stationNamesVisible = false;
	for (auto* decoration : w.m_presenter->m_stationDecorations) decoration->setVisible(false);
	w.updateViewportOverlays();
	if (!require(z1->isVisible() && !picture->isVisible() && !w.m_presenter->m_stationLabels.front()->isVisible(),
			"station layers must not hide structural squares")) return false;
	w.m_presenter->m_stationLayerVisible = true;
	w.updateViewportOverlays();
	if (!require(!w.m_presenter->m_stationLabels.front()->isVisible() && z1->isVisible(), "independent name layer")) return false;
	w.m_presenter->m_stationLayerVisible = oldLayer;
	w.m_presenter->m_stationNamesVisible = oldNames;
	for (auto* decoration : w.m_presenter->m_stationDecorations) decoration->setVisible(oldLayer);
	w.updateViewportOverlays();
	click(qgraphicsitem_cast<TrackLineItem*>(find("track", "fallback"))->line().pointAt(0.5));
	if (!require(w.arcIDText->text() == "No authored arc (fallback)" && w.arcFirstNodeIDText->text().isEmpty()
				&& w.nodeStationNameText->text().isEmpty(),
			"fallback clears unrelated canonical fields")) return false;
	const quint64 fixtureRevision = w.m_document->revision();
	for (const auto& track : {QString("Z"), QString("A")}) {
		auto* head = w.resolvePreviewItem("signal", "@" + track + "-section1@", track, false, fixtureRevision);
		if (!require(head != nullptr, "same-chainage signal head missing")) return false;
		click(head->sceneBoundingRect().center());
		if (!require(w.signallingTrackIDText->text() == track && w.signallingXText->text().toDouble() == 1000
					&& w.signallingLengthSectionAheadText->text() == "Unavailable before Run"
					&& w.signallingGroupDetails->toPlainText().contains("authored-" + track),
				"exact canonical preview signal identity")) return false;
	}
	if (!require(w.m_presenter->m_previewFitBounds == topologyBounds && !w.m_worker && w.m_presenter->allArcs.isEmpty()
				&& w.m_presenter->allPlatforms.isEmpty() && w.m_presenter->allSignals.isEmpty(),
			"hit geometry/inspection mutated topology or runtime")) return false;
	// Held payload tests the same guard as menus without dereferencing a deleted action.
	const auto held = [&w, fixtureRevision]() {
		return w.resolvePreviewItem("arc", "z-arc0", QString(), false, fixtureRevision);
	};
	w.showSceneContextMenu(find("arc", "z-arc0"), QPointF(), QPoint(), false);
	w.renderTrackPreview(w.m_document->mutableModel());
	if (!require(!w.m_sceneContextMenu && !w.infoDockWidget->isVisible() && w.nodeIDText->text().isEmpty()
				&& w.m_previewSelectedStationId.isEmpty() && held(),
			"same-revision rebuild clears menu/inspection and safely resolves payload")) return false;
	w.m_document->mutableModel().arcs.front().id = "renamed";
	w.m_document->markChanged(SceneDocument::AllChanges);
	w.renderTrackPreview(w.m_document->mutableModel());
	if (!require(!held() && find("arc", "renamed"), "rename invalidates queued canonical identity")) return false;
	w.m_document->mutableModel().arcs.erase(w.m_document->mutableModel().arcs.begin());
	w.m_document->markChanged(SceneDocument::AllChanges);
	w.renderTrackPreview(w.m_document->mutableModel());
	if (!require(!find("arc", "renamed"), "delete removes canonical graphic")) return false;
	w.m_document->mutableModel() = fixture;
	++w.m_document->m_revision;
	w.renderTrackPreview(w.m_document->mutableModel());
	if (!require(find("arc", "z-arc0") && !held(), "replacement reusing IDs rejects old payload")) return false;
	// A platform darkens a station square only at the end of its line; an interior square is the ordinary grey
	// unless it joins three or more tracks.
	SceneModel lineEndFixture = fixture;
	lineEndFixture.stations.front().platforms.front().nodeIds.push_back("z3");
	w.m_document->mutableModel() = lineEndFixture;
	++w.m_document->m_revision;
	w.renderTrackPreview(w.m_document->mutableModel());
	const auto squareFill = [&](const QString& id) {
		const auto* square = qgraphicsitem_cast<StationNodeItem*>(find("station-node", id));
		return square ? square->brush().color() : QColor();
	};
	if (!require(squareFill("z1") == classifyStation(false, 3).fill && squareFill("z2") == classifyStation(false, 0).fill
				&& squareFill("z3") == classifyStation(true, 0).fill,
			"station square colour: interchange, interior platform node and line end")) return false;
	w.m_document->mutableModel() = saved;
	w.m_document->setDirty(savedDirty);
	++w.m_document->m_revision;
	w.renderTrackPreview(w.m_document->mutableModel());
	std::fprintf(stdout, "E2E_PREVIEW_IDENTITY_LIFETIME_OK\n");
	std::fflush(stdout);
	return true;
}
