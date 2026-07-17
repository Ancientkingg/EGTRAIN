#include "diagrams/DiagramWindow.h"
#include "diagrams/TrainFilterButton.h"
#include "util/TimeFormat.h"

#include <QColor>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSaveFile>
#include <QVariant>
#include <QVBoxLayout>
#include <QtCharts/QAbstractSeries>
#include <QtCharts/QCategoryAxis>
#include <QtCharts/QLegend>
#include <QtCharts/QValueAxis>
#include <QtCharts/QXYSeries>

#include <cmath>

namespace {

// Reduced-opacity pen for lines that are not the pinned train.
QPen mutedPen(const QPen& base) {
	QPen pen = base;
	QColor color = pen.color();
	color.setAlpha(60);
	pen.setColor(color);
	return pen;
}

// Emphasised pen for the pinned train.
QPen emphasisedPen(const QPen& base) {
	QPen pen = base;
	QColor color = pen.color();
	color.setAlpha(255);
	pen.setColor(color);
	pen.setWidthF(base.widthF() + 1.5);
	return pen;
}

} // namespace

DiagramWindow::DiagramWindow(const QString& title, QWidget* parent)
	: QDialog(parent) {
	setModal(false);
	setWindowTitle(title);
	resize(1200, 720);

	m_view = new QChartView(this);
	m_view->setRenderHint(QPainter::Antialiasing);
	m_view->setMouseTracking(true);
	m_view->viewport()->setMouseTracking(true);
	m_view->viewport()->installEventFilter(this);
	// Drag a rectangle to zoom; filter state is unaffected by zoom or pan.
	m_view->setRubberBand(QChartView::RectangleRubberBand);

	// Top bar: train visibility dropdown, pin state, zoom reset, exports.
	m_trainsButton = new TrainFilterButton(this);
	connect(m_trainsButton, &TrainFilterButton::selectionChanged,
			this, &DiagramWindow::applyTrainVisibility);

	QPushButton* clearPinBtn = new QPushButton("Clear selection", this);
	connect(clearPinBtn, &QPushButton::clicked, this, &DiagramWindow::clearPin);
	m_pinLabel = new QLabel("", this);

	QPushButton* resetZoomBtn = new QPushButton("Reset zoom", this);
	resetZoomBtn->setToolTip("Undo rubber-band zoom (drag on the chart to zoom in)");
	connect(resetZoomBtn, &QPushButton::clicked, this, &DiagramWindow::resetZoom);

	m_csvButton = new QPushButton("Export CSV...", this);
	m_csvButton->setToolTip("Write the raw data of the visible trains to a CSV file");
	m_csvButton->setEnabled(false);
	connect(m_csvButton, &QPushButton::clicked, this, &DiagramWindow::exportCsv);

	QPushButton* exportPngBtn = new QPushButton("Export PNG...", this);
	exportPngBtn->setToolTip("Save the chart as an image");
	connect(exportPngBtn, &QPushButton::clicked, this, &DiagramWindow::exportPng);

	QHBoxLayout* topBar = new QHBoxLayout();
	topBar->addWidget(m_trainsButton);
	topBar->addWidget(clearPinBtn);
	topBar->addWidget(m_pinLabel);
	topBar->addStretch();
	topBar->addWidget(resetZoomBtn);
	topBar->addWidget(m_csvButton);
	topBar->addWidget(exportPngBtn);

	m_readout = new QLabel("Hover over the chart to read values; click a line to select its train", this);

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->addLayout(topBar);
	layout->addWidget(m_view, 1);
	layout->addWidget(m_readout);
}

void DiagramWindow::setChart(QChart* chart) {
	m_timeAxisApplied = false;
	if (chart)
		applyChartStyle(chart);
	m_view->setChart(chart);  // QChartView takes ownership
	// The train dropdown replaces the built-in legend, which collapses to "..."
	// once many trains are present.
	if (chart && chart->legend())
		chart->legend()->hide();
	rebuildFilterGroups();
	if (m_timeAxis)
		applyTimeAxis();
}

void DiagramWindow::setTimeAxisX(bool on, long long startOffsetSeconds) {
	m_timeAxis = on;
	m_startOffset = startOffsetSeconds;
	if (m_timeAxis)
		applyTimeAxis();
}

void DiagramWindow::setCsvProvider(std::function<std::string(const QStringList&)> provider,
								   const QString& suggestedFileName) {
	m_csvProvider = std::move(provider);
	m_csvSuggestedName = suggestedFileName;
	if (m_csvButton)
		m_csvButton->setEnabled(static_cast<bool>(m_csvProvider));
}

// Shared look for every diagram: quiet grid, readable title, line weight that
// survives PNG export.
void DiagramWindow::applyChartStyle(QChart* chart) {
	chart->setBackgroundRoundness(0);
	chart->setBackgroundBrush(palette().brush(QPalette::Base));
	chart->setMargins(QMargins(12, 8, 12, 8));

	QFont titleFont = chart->titleFont();
	titleFont.setPointSizeF(titleFont.pointSizeF() + 2.0);
	titleFont.setBold(true);
	chart->setTitleFont(titleFont);
	chart->setTitleBrush(palette().brush(QPalette::WindowText));

	const QPen gridPen(QColor(0, 0, 0, 30));
	const QBrush labelBrush = palette().brush(QPalette::WindowText);
	const auto axes = chart->axes();
	for (QAbstractAxis* axis : axes) {
		axis->setGridLinePen(gridPen);
		axis->setLabelsBrush(labelBrush);
		axis->setTitleBrush(labelBrush);
		axis->setLinePen(QPen(QColor(0, 0, 0, 90)));
	}

	const auto seriesList = chart->series();
	for (QAbstractSeries* series : seriesList) {
		if (auto* xy = qobject_cast<QXYSeries*>(series)) {
			QPen pen = xy->pen();
			if (pen.widthF() < 2.0) {
				pen.setWidthF(2.0);
				xy->setPen(pen);
			}
		}
	}
}

// Swap the raw-seconds X axis for one labelled HH:MM:SS at round intervals.
void DiagramWindow::applyTimeAxis() {
	QChart* chart = m_view ? m_view->chart() : nullptr;
	if (!chart || m_timeAxisApplied)
		return;
	const auto horizontal = chart->axes(Qt::Horizontal);
	if (horizontal.isEmpty())
		return;
	auto* seconds = qobject_cast<QValueAxis*>(horizontal.first());
	if (!seconds)
		return;
	const double min = seconds->min();
	const double max = seconds->max();
	const double span = max - min;
	if (!(span > 0.0))
		return;

	// Aim for four to eight round-interval labels.
	const double candidates[] = {30, 60, 120, 300, 600, 900, 1800, 3600, 7200, 14400, 21600, 43200};
	double step = candidates[sizeof(candidates) / sizeof(candidates[0]) - 1];
	for (double candidate : candidates) {
		if (span / candidate <= 8.0) {
			step = candidate;
			break;
		}
	}

	auto* clock = new QCategoryAxis(chart);
	clock->setLabelsPosition(QCategoryAxis::AxisLabelsPositionOnValue);
	clock->setMin(min);
	clock->setMax(max);
	clock->setTitleText("Time");
	clock->setGridLinePen(QPen(QColor(0, 0, 0, 30)));
	clock->setLabelsBrush(palette().brush(QPalette::WindowText));
	clock->setTitleBrush(palette().brush(QPalette::WindowText));
	for (double tick = std::ceil(min / step) * step; tick <= max + 1e-9; tick += step)
		clock->append(QString::fromStdString(formatSimTime(static_cast<long long>(tick), m_startOffset)), tick);

	const auto seriesList = chart->series();
	chart->removeAxis(seconds);
	seconds->deleteLater();
	chart->addAxis(clock, Qt::AlignBottom);
	for (QAbstractSeries* series : seriesList)
		series->attachAxis(clock);
	m_timeAxisApplied = true;
}

QString DiagramWindow::groupIdForSeries(QAbstractSeries* series) const {
	if (!series)
		return QString();
	const QVariant trainId = series->property("trainId");
	if (trainId.isValid() && !trainId.toString().isEmpty())
		return trainId.toString();
	return series->name();
}

void DiagramWindow::rebuildFilterGroups() {
	m_groups.clear();
	m_basePens.clear();
	QChart* chart = m_view ? m_view->chart() : nullptr;
	if (!chart) {
		if (m_trainsButton)
			m_trainsButton->setTrains({});
		return;
	}

	QHash<QString, int> indexByTrain;
	const auto seriesList = chart->series();
	for (QAbstractSeries* series : seriesList) {
		if (auto* xy = qobject_cast<QXYSeries*>(series))
			m_basePens.insert(series, xy->pen());

		const QString trainId = groupIdForSeries(series);
		int groupIndex;
		auto it = indexByTrain.find(trainId);
		if (it == indexByTrain.end()) {
			SeriesGroup group;
			group.trainId = trainId;
			m_groups.append(group);
			groupIndex = m_groups.size() - 1;
			indexByTrain.insert(trainId, groupIndex);
		} else {
			groupIndex = it.value();
		}
		m_groups[groupIndex].members.append(series);

		// Connect identification signals once per series.
		if (auto* xy = qobject_cast<QXYSeries*>(series)) {
			connect(xy, &QXYSeries::hovered, this, [this, series](const QPointF&, bool state) {
				m_hoverName = state ? series->name() : QString();
			});
			connect(xy, &QXYSeries::clicked, this, [this, trainId](const QPointF&) {
				pinTrain(trainId);
			});
		}
	}

	if (!m_trainsButton)
		return;
	QVector<QPair<QString, QColor>> trains;
	trains.reserve(m_groups.size());
	for (const SeriesGroup& group : m_groups) {
		QColor swatch;
		if (!group.members.isEmpty()) {
			if (auto* xy = qobject_cast<QXYSeries*>(group.members.first()))
				swatch = xy->pen().color();
		}
		trains.append({group.trainId, swatch});
	}
	m_trainsButton->setTrains(trains);
}

void DiagramWindow::applyTrainVisibility() {
	if (!m_trainsButton)
		return;
	for (const SeriesGroup& group : m_groups) {
		const bool visible = m_trainsButton->isTrainVisible(group.trainId);
		for (QAbstractSeries* series : group.members)
			series->setVisible(visible);
	}
	refreshEmphasis();
}

void DiagramWindow::pinTrain(const QString& trainId) {
	m_pinnedTrainId = trainId;
	if (m_pinLabel)
		m_pinLabel->setText(QString("Selected: %1").arg(trainId));
	refreshEmphasis();
	emit trainSelected(trainId);
}

void DiagramWindow::clearPin() {
	m_pinnedTrainId.clear();
	if (m_pinLabel)
		m_pinLabel->setText("");
	refreshEmphasis();
}

void DiagramWindow::refreshEmphasis() {
	for (const SeriesGroup& group : m_groups) {
		const bool pinnedActive = !m_pinnedTrainId.isEmpty();
		const bool isPinned = pinnedActive && group.trainId == m_pinnedTrainId;
		for (QAbstractSeries* series : group.members) {
			auto* xy = qobject_cast<QXYSeries*>(series);
			if (!xy)
				continue;
			const QPen base = m_basePens.value(series, xy->pen());
			if (!pinnedActive)
				xy->setPen(base);
			else if (isPinned)
				xy->setPen(emphasisedPen(base));
			else
				xy->setPen(mutedPen(base));
		}
	}
}

QStringList DiagramWindow::visibleTrainIds() const {
	if (m_trainsButton)
		return m_trainsButton->visibleTrainIds();
	return QStringList();
}

void DiagramWindow::resetZoom() {
	if (m_view && m_view->chart())
		m_view->chart()->zoomReset();
}

void DiagramWindow::exportPng() {
	QString path = QFileDialog::getSaveFileName(this, "Export Diagram", "diagram.png", "PNG Image (*.png)");
	if (path.isEmpty())
		return;  // cancelled: no file is written
	if (QFileInfo(path).suffix().compare("png", Qt::CaseInsensitive) != 0)
		path += ".png";
	QPixmap pix = m_view->grab();
	if (!pix.save(path, "PNG")) {
		QMessageBox::warning(this, "Export failed",
							 QString("Could not write the image to:\n%1").arg(path));
	}
}

void DiagramWindow::exportCsv() {
	if (!m_csvProvider)
		return;
	const std::string content = m_csvProvider(visibleTrainIds());
	if (content.empty()) {
		QMessageBox::information(this, "Nothing to export",
								 "There is no data to export for the visible trains.");
		return;
	}
	const QString suggested = m_csvSuggestedName.isEmpty() ? QString("export.csv") : m_csvSuggestedName;
	QString path = QFileDialog::getSaveFileName(this, "Export Data", suggested, "CSV File (*.csv)");
	if (path.isEmpty())
		return;  // cancelled: no file is written
	if (QFileInfo(path).suffix().compare("csv", Qt::CaseInsensitive) != 0)
		path += ".csv";
	// QSaveFile writes to a temporary file and renames on commit, so a failure or
	// cancellation never leaves a partial file in place.
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		QMessageBox::warning(this, "Export failed",
							 QString("Could not open the file for writing:\n%1").arg(path));
		return;
	}
	const QByteArray bytes = QByteArray::fromStdString(content);
	if (file.write(bytes) != bytes.size() || !file.commit()) {
		QMessageBox::warning(this, "Export failed",
							 QString("Could not write the data to:\n%1").arg(path));
	}
}

bool DiagramWindow::eventFilter(QObject* obj, QEvent* ev) {
	if (m_view && obj == m_view->viewport() && ev->type() == QEvent::MouseMove && m_view->chart()) {
		QMouseEvent* me = static_cast<QMouseEvent*>(ev);
		// map: viewport pos -> scene pos -> chart-local pos -> data value
		QPointF scenePos = m_view->mapToScene(me->pos());
		QPointF chartPos = m_view->chart()->mapFromScene(scenePos);
		QPointF val = m_view->chart()->mapToValue(chartPos);
		QString xText = m_timeAxis
			? QString::fromStdString(formatSimTime(static_cast<long long>(val.x()), m_startOffset))
			: QString::number(val.x(), 'f', 2);
		QString text = m_hoverName.isEmpty()
			? QString("x: %1   y: %2").arg(xText).arg(val.y(), 0, 'f', 2)
			: QString("%1   x: %2   y: %3").arg(m_hoverName).arg(xText).arg(val.y(), 0, 'f', 2);
		m_readout->setText(text);
	}
	return QDialog::eventFilter(obj, ev);
}
