// RunController.h brings in SceneModel.h in the way that the member named signals needs, before the other Qt headers do.
#include "app/RunController.h"
#include "app/RunResultsDock.h"
#include "app/DialogFolders.h"
#include "app/MainWindowSupport.h"
#include "app/PathMessages.h"
#include "app/ResultViews.h"
#include "diagrams/ResultFrame.h"
#include "diagrams/RunResults.h"
#include "util/TimeFormat.h"
#include "widgets/DialogLayout.h"
#include "widgets/EditorLayout.h"
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <memory>
#include <string>

namespace {

// The height of the table with its header, the given number of rows and its horizontal scroll bar.
int tableHeight(const QTableWidget& table, int rows) {
	return table.horizontalHeader()->sizeHint().height() + rows * table.verticalHeader()->defaultSectionSize()
		+ table.horizontalScrollBar()->sizeHint().height() + 2 * table.frameWidth();
}

} // namespace

RunResultsDock::RunResultsDock(QMainWindow* window, RunController* runController, ResultViews* views, Host& host)
	: m_window(window), m_runController(runController), m_host(host) {
	m_dock = new QDockWidget("Run Results", m_window);
	m_dock->setObjectName("runResultsDock");
	m_dock->setAllowedAreas(Qt::AllDockWidgetAreas);
	m_table = new QTableWidget(m_dock);
	m_table->setObjectName("runResultsTable");
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setColumnCount(11);
	m_table->setHorizontalHeaderLabels({"Train", "Operating code", "Performance (%)", "Maximum speed (km/h)", "Start time (hh:mm:ss)", "End time (hh:mm:ss)", "Travel time (h:mm:ss)",
		"Energy (kWh)", "Energy with regen (kWh)",
		"Substation (kWh)", "Substation with regen (kWh)"});
	// The energy headers are short; the tooltip of each gives the full name.
	const QStringList energyNames{"Energy consumed (kWh)", "Energy consumed with regenerative braking (kWh)", "Substation request (kWh)",
		"Substation request with regenerative braking (kWh)"};
	for (int index = 0; index < energyNames.size(); ++index)
		m_table->horizontalHeaderItem(7 + index)->setToolTip(energyNames[index]);
	m_table->setAlternatingRowColors(true);
	m_table->horizontalHeader()->setStretchLastSection(true);
	// Tab leaves the table for the buttons after it; the arrow keys move between the cells.
	m_table->setTabKeyNavigation(false);

	QWidget* container = new QWidget(m_dock);
	QVBoxLayout* containerLayout = new QVBoxLayout(container);
	containerLayout->setContentsMargins(0, 0, 0, 0);
	m_summaryLabel = EditorLayout::wrappedLabel(QString(), container);
	m_summaryLabel->setObjectName("runResultsContextLabel");
	containerLayout->addWidget(m_summaryLabel);
	QHBoxLayout* resultViews = new QHBoxLayout();
	auto* diagramsButton = new QToolButton(container);
	diagramsButton->setText("Open result view...");
	diagramsButton->setObjectName("runResultsViewsButton");
	diagramsButton->setPopupMode(QToolButton::InstantPopup);
	// The text size of the push buttons beside it; macOS gives a tool button a smaller font.
	diagramsButton->setFont(QApplication::font());
	QMenu* resultMenu = new QMenu(diagramsButton);
	diagramsButton->setMenu(resultMenu);
	for (const ResultViewEntry& view : kResultViewEntries) {
		if (view.startsGroup)
			resultMenu->addSeparator();
		QAction* action = resultMenu->addAction(view.name);
		action->setObjectName(QString("resultView_%1").arg(view.name).remove(' '));
		QObject::connect(action, &QAction::triggered, views, view.open);
	}
	resultViews->addWidget(diagramsButton);
	QPushButton* exportCsvBtn = new QPushButton("Export CSV...", container);
	exportCsvBtn->setObjectName("resultView_ExportCSV");
	exportCsvBtn->setToolTip("Write travel time and energy per train to a CSV file");
	QObject::connect(exportCsvBtn, &QPushButton::clicked, m_dock, [this]() {
		const auto operation = m_host.captureTelemetryOperation();
		const std::shared_ptr<const CompletedRunData> run = m_runController->completedRun();
		saveCsvInteractive(
			m_window, "run_summary.csv", operation,
			[run] { return run ? buildRunSummaryCsv(run->results) : std::string(); },
			[run](const QString& path, const std::string& bytes) {
				return writeRunArtifactWithProvenance(path.toStdString(), "csv", bytes, run ? run->provenance : RunProvenance());
			});
	});
	QPushButton* exportPngBtn = new QPushButton("Export PNG...", container);
	exportPngBtn->setObjectName("resultView_ExportPNG");
	exportPngBtn->setToolTip("Save the table as an image");
	QObject::connect(exportPngBtn, &QPushButton::clicked, m_dock, [this]() {
		const auto operation = m_host.captureTelemetryOperation();
		QString path = QFileDialog::getSaveFileName(m_window, "Export Table", dialog_folders::suggestedPath("run_summary.png"), "PNG Image (*.png)");
		if (path.isEmpty())
			return;
		if (QFileInfo(path).suffix().compare("png", Qt::CaseInsensitive) != 0)
			path += ".png";
		QByteArray data;
		QBuffer buffer(&data);
		if (!buffer.open(QIODevice::WriteOnly) || !fullTableImage(*m_table).save(&buffer, "PNG")) {
			operation.failure(telemetry::Operation::Export, telemetry::Error::InternalFailure);
			QMessageBox::warning(m_window, "Export failed", path_message::writeFailed("save the image to", path));
			return;
		}
		const std::string bytes(data.constData(), static_cast<std::size_t>(data.size()));
		const bool written = writeRunArtifactWithProvenance(path.toStdString(), "png", bytes,
			m_runController->completedRun() ? m_runController->completedRun()->provenance : RunProvenance());
		operation.exportFinished(telemetry::ExportKind::Png, written, true, telemetry::Error::IoFailure);
		if (!written)
			QMessageBox::warning(m_window, "Export failed", path_message::writeFailed("save the image to", path));
	});
	resultViews->addWidget(exportCsvBtn);
	resultViews->addWidget(exportPngBtn);
	resultViews->addStretch();
	containerLayout->addLayout(resultViews);
	// The table takes the height that the other lines and buttons leave, at least its header and one row. A lower dock scrolls
	// its whole content, so the dock leaves the canvas its room; the delay comparison below the table keeps the table in view.
	// As a height-for-width item without a layout the table asks for its minimum height next to the wrapped labels, so the
	// scrolled content has room for both.
	QSizePolicy tablePolicy = m_table->sizePolicy();
	tablePolicy.setHeightForWidth(true);
	m_table->setSizePolicy(tablePolicy);
	m_table->setMinimumHeight(tableHeight(*m_table, 1));
	containerLayout->addWidget(m_table, 1);
	// Tab reaches the buttons above the table before the table.
	QWidget::setTabOrder(exportPngBtn, m_table);
	QHBoxLayout* delayRow = new QHBoxLayout();
	auto* delayCaption = new QLabel(container);
	delayCaption->setObjectName("delayComparisonCaption");
	DialogLayout::setHelp(delayCaption, "Delay comparison");
	containerLayout->addWidget(delayCaption);
	m_delayFeedbackLabel = EditorLayout::wrappedLabel(QString(), container);
	m_delayFeedbackLabel->setObjectName("delayFeedbackLabel");
	containerLayout->addWidget(m_delayFeedbackLabel);
	m_setDelayBaselineButton = new QPushButton("Set delay baseline", container);
	m_setDelayBaselineButton->setObjectName("setDelayBaselineButton");
	m_setDelayBaselineButton->setToolTip("Use this run as the delay baseline");
	QObject::connect(m_setDelayBaselineButton, &QPushButton::clicked, m_runController, &RunController::setDelayBaseline);
	delayRow->addWidget(m_setDelayBaselineButton);
	m_compareDelayButton = new QPushButton("Compare delays", container);
	m_compareDelayButton->setObjectName("compareDelayButton");
	m_compareDelayButton->setToolTip("Compare this run with the delay baseline");
	QObject::connect(m_compareDelayButton, &QPushButton::clicked, views, &ResultViews::showDelayComparison);
	delayRow->addWidget(m_compareDelayButton);
	delayRow->addStretch();
	containerLayout->addLayout(delayRow);
	auto* scroll = new EditorLayout::VerticalScrollArea(m_dock);
	scroll->setFocusPolicy(Qt::NoFocus);
	scroll->setWidget(container);
	m_dock->setWidget(scroll);
	m_window->addDockWidget(Qt::BottomDockWidgetArea, m_dock);
	m_dock->hide();
}

void RunResultsDock::refresh() {
	if (!m_runController->completedRun() || m_runController->completedRun()->results.trains.empty())
		return;
	const std::shared_ptr<const CompletedRunData> run = m_runController->completedRun();
	refreshSummary();
	m_dock->setWindowTitle(QStringLiteral("Run Results"));

	const RunResults& results = run->results;
	// The headers and their tooltips stay as the constructor set them.
	m_table->clearContents();
	m_table->setRowCount(static_cast<int>(results.trains.size()) + 1);

	// Start and end as clock times, travel time as a duration, energy with one
	// decimal; a dash marks values the run did not produce.
	const auto clockText = [&run](const RunResultValue& value) {
		return value.available
			? QString::fromStdString(formatSimTime(static_cast<long long>(value.value), run->settings.startOffsetSeconds))
			: QStringLiteral("-");
	};
	const auto runDurationText = [](const RunResultValue& value) {
		return value.available ? durationText(static_cast<long long>(value.value)) : QStringLiteral("-");
	};
	const auto energyText = [](const RunResultValue& value) {
		return value.available ? QString::number(value.value, 'f', 1) : QStringLiteral("-");
	};
	for (int row = 0; row < static_cast<int>(results.trains.size()); ++row) {
		const TrainRunResult& result = results.trains[static_cast<std::size_t>(row)];
		m_table->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(result.trainId)));
		m_table->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(result.operatingCode)));
		m_table->setItem(row, 2,
			new QTableWidgetItem(QString::number(result.performancePercent, 'f', 1)));
		m_table->setItem(row, 3,
			new QTableWidgetItem(QString::number(result.appliedMaximumSpeedKmh, 'f', 1)));
		m_table->setItem(row, 4, new QTableWidgetItem(clockText(result.startSeconds)));
		m_table->setItem(row, 5, new QTableWidgetItem(clockText(result.endSeconds)));
		m_table->setItem(row, 6, new QTableWidgetItem(runDurationText(result.travelSeconds)));
		m_table->setItem(row, 7, new QTableWidgetItem(energyText(result.energyConsumedKWh)));
		m_table->setItem(row, 8, new QTableWidgetItem(energyText(result.energyWithRegenKWh)));
		m_table->setItem(row, 9, new QTableWidgetItem(energyText(result.substationKWh)));
		m_table->setItem(row, 10, new QTableWidgetItem(energyText(result.substationWithRegenKWh)));
	}

	const int totalRow = static_cast<int>(results.trains.size());
	m_table->setItem(totalRow, 0, new QTableWidgetItem(QStringLiteral("Network total")));
	m_table->setItem(totalRow, 1, new QTableWidgetItem(QStringLiteral("-")));
	m_table->setItem(totalRow, 2, new QTableWidgetItem(QStringLiteral("-")));
	m_table->setItem(totalRow, 3, new QTableWidgetItem(QStringLiteral("-")));
	m_table->setItem(totalRow, 4, new QTableWidgetItem(clockText(results.networkStartSeconds)));
	m_table->setItem(totalRow, 5, new QTableWidgetItem(clockText(results.networkEndSeconds)));
	m_table->setItem(totalRow, 6, new QTableWidgetItem(runDurationText(results.networkTravelSeconds)));
	m_table->setItem(totalRow, 7, new QTableWidgetItem(energyText(results.energyConsumedKWh)));
	m_table->setItem(totalRow, 8, new QTableWidgetItem(energyText(results.energyWithRegenKWh)));
	m_table->setItem(totalRow, 9, new QTableWidgetItem(energyText(results.substationKWh)));
	m_table->setItem(totalRow, 10, new QTableWidgetItem(energyText(results.substationWithRegenKWh)));
	// Names stay left-aligned; numbers and times are right-aligned, as in every table of results.
	for (int row = 0; row <= totalRow; ++row)
		for (int column = 2; column < m_table->columnCount(); ++column)
			m_table->item(row, column)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
	m_table->resizeColumnsToContents();
	const bool openingResults = !m_dock->isVisible();
	m_dock->show();
	m_dock->raise();
	if (openingResults && !m_dock->isFloating() && m_window->dockWidgetArea(m_dock) == Qt::BottomDockWidgetArea) {
		m_window->resizeDocks({m_dock}, {m_window->height() / 3}, Qt::Vertical);
		// Higher than a third when the table would show fewer than its header and three rows, but at most half of the height
		// that the dock shares with the network view above it, so that a small window keeps a usable canvas.
		m_window->layout()->activate();
		const auto* scroll = static_cast<const QScrollArea*>(m_dock->widget());
		const int besideTable = m_dock->height() - scroll->viewport()->height() + scroll->widget()->height() - m_table->height();
		const auto* canvas = m_window->centralWidget()->findChild<QGraphicsView*>(QString(), Qt::FindDirectChildrenOnly);
		const int half = (m_dock->height() + canvas->height()) / 2;
		m_window->resizeDocks({m_dock}, {std::min(std::max(m_dock->height(), besideTable + tableHeight(*m_table, 3)), half)}, Qt::Vertical);
	}
	emit refreshed();
}

void RunResultsDock::refreshSummary() {
	if (!m_runController->completedRun() || m_runController->completedRun()->results.trains.empty())
		return;
	int destinationTerminationCount = 0;
	for (const TrainRunResult& result : m_runController->completedRun()->results.trains) {
		if (result.destinationTerminated)
			++destinationTerminationCount;
	}
	QString summary = QString("Run: %1\nOccurrences: %2\nStatus: Completed")
						  .arg(completedRunContext(m_runController->completedRun()->provenance),
							  QString::fromStdString(runOccurrencesText(m_runController->lastRunSelectedOccurrences(),
								  m_runController->lastRunTotalOccurrences(), m_runController->lastRunSelectedInPeriodOccurrences())));
	if (m_host.advancedDetailsEnabled()) {
		summary += QString("\n%1\nDestination terminations: %2")
					   .arg(QString::fromStdString(directIncidentEvidenceText(m_runController->completedRun()->results)))
					   .arg(destinationTerminationCount);
	}
	if (const auto& baseline = m_runController->delayBaseline())
		summary += QString("\nDelay baseline: %1").arg(completedRunContext(baseline->provenance));
	m_summaryLabel->setText(summary);
}

QString RunResultsDock::durationText(long long seconds) {
	return QString("%1:%2:%3").arg(seconds / 3600).arg((seconds % 3600) / 60, 2, 10, QChar('0')).arg(seconds % 60, 2, 10, QChar('0'));
}

void RunResultsDock::clear(const QString& summary, const QString& title) {
	m_table->setRowCount(0);
	m_summaryLabel->setText(summary);
	m_dock->setWindowTitle(title);
	m_dock->hide();
}

void RunResultsDock::updateAvailability(bool resultsAvailable) {
	if (auto* viewsButton = m_dock->findChild<QToolButton*>("runResultsViewsButton"))
		viewsButton->setEnabled(resultsAvailable);
	for (QPushButton* button : m_dock->findChildren<QPushButton*>()) {
		if (button->objectName().startsWith("resultView_"))
			button->setEnabled(resultsAvailable);
	}
	for (QAction* action : m_dock->findChildren<QAction*>())
		if (action->objectName().startsWith("resultView_"))
			action->setEnabled(resultsAvailable);
	const RunController::DelayBaselineState delay = m_runController->delayBaselineState(resultsAvailable);
	m_setDelayBaselineButton->setEnabled(delay.canSet);
	m_setDelayBaselineButton->setToolTip(delay.canSet
			? QStringLiteral("Use this run as the delay baseline")
			: QStringLiteral("Needs a completed run without incidents or entrance delays"));
	m_compareDelayButton->setEnabled(delay.canCompare);
	m_compareDelayButton->setToolTip(delay.canCompare
			? QStringLiteral("Compare this run with the delay baseline")
			: QStringLiteral("Needs a baseline and a run with incidents or entrance delays"));
	m_delayFeedbackLabel->setText(delay.feedback);
}
