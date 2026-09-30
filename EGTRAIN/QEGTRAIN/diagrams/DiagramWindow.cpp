#include "diagrams/DiagramWindow.h"
#include "diagrams/TrainFilterButton.h"
#include "util/TimeFormat.h"

#include <QApplication>
#include <QBuffer>
#include <QWheelEvent>
#include <QNativeGestureEvent>
#include <QGestureEvent>
#include <QPinchGesture>
#include <QShortcut>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QAreaSeries>
#include <algorithm>
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
#include <QScreen>
#include <QScrollArea>
#include <QVariant>
#include <QVBoxLayout>
#include <QtCharts/QAbstractSeries>
#include <QtCharts/QCategoryAxis>
#include <QtCharts/QLegend>
#include <QtCharts/QValueAxis>
#include <QtCharts/QXYSeries>

#include <cmath>
#include <limits>
namespace {

// Reduced-opacity pen for lines that are not the pinned train.
QPen mutedPen(const QPen& base) {
	QPen pen = base;
	QColor color = pen.color();
	color.setAlpha(60);
	pen.setColor(color);
	return pen;
}

bool writeArtifact(const QString& path, const std::string& bytes) {
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly))
		return false;
	const QByteArray data = QByteArray::fromStdString(bytes);
	return file.write(data) == data.size() && file.commit();
}

// Clip a stroke to the visible plot before hit-testing. The returned fractions
// still refer to its original endpoints, so inspection keeps the sample index.
bool clippedSegment(const QRectF& plot, const QPointF& a, const QPointF& b,
		double& first, double& last) {
	first = 0;
	last = 1;
	const QPointF d = b - a;
	const auto edge = [&](double p, double q) {
		if (p == 0) return q >= 0;
		const double t = q / p;
		if (p < 0) first = std::max(first, t);
		else last = std::min(last, t);
		return first <= last;
	};
	return edge(-d.x(), a.x() - plot.left()) && edge(d.x(), plot.right() - a.x())
		&& edge(-d.y(), a.y() - plot.top()) && edge(d.y(), plot.bottom() - a.y());
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
	setProperty("dialogPresentation", true);
	setWindowTitle(title);
	const QRect available = screen() ? screen()->availableGeometry() : QRect(0, 0, 1280, 800);
	setMaximumSize(available.width() * 9 / 10, available.height() * 4 / 5);
	resize(qMin(1200, maximumWidth()), qMin(720, maximumHeight()));

	m_view = new QChartView(this);
	m_view->setRenderHint(QPainter::Antialiasing);
	m_view->setMouseTracking(true);
	m_view->viewport()->setMouseTracking(true);
	m_view->viewport()->installEventFilter(this);
	m_view->viewport()->grabGesture(Qt::PinchGesture);
	m_view->setFocusPolicy(Qt::StrongFocus);
	m_tooltip = new QLabel(this);
	m_tooltip->setObjectName("diagramTooltip");
	m_tooltip->setTextFormat(Qt::PlainText);
	m_tooltip->setForegroundRole(QPalette::ToolTipText);
	m_tooltip->setBackgroundRole(QPalette::ToolTipBase);
	m_tooltip->setAutoFillBackground(true);
	m_tooltip->setMargin(6);
	m_tooltip->setFrameShape(QFrame::StyledPanel);
	m_tooltip->setAttribute(Qt::WA_TransparentForMouseEvents);
	m_tooltip->hide();
	installEventFilter(this);
	for (const auto key : {Qt::Key_Plus, Qt::Key_Equal, Qt::Key_Minus, Qt::Key_0, Qt::Key_Home,
		Qt::Key_Left, Qt::Key_Right, Qt::Key_Up, Qt::Key_Down}) {
		auto* shortcut = new QShortcut(QKeySequence(key), this);
		shortcut->setContext(Qt::WidgetWithChildrenShortcut);
		connect(shortcut, &QShortcut::activated, this, [this, key] {
			if (!m_view->hasFocus() && !m_view->viewport()->hasFocus()) return;
			if (key == Qt::Key_0 || key == Qt::Key_Home) { resetZoom(); return; }
			QPointF pan;
			if (key == Qt::Key_Left) pan.setX(40);
			if (key == Qt::Key_Right) pan.setX(-40);
			if (key == Qt::Key_Up) pan.setY(40);
			if (key == Qt::Key_Down) pan.setY(-40);
			navigate(pan.isNull() ? (key == Qt::Key_Minus ? 1 / 1.2 : 1.2) : 1,
				m_view->chart()->plotArea().center(), pan);
		});
	}
	// Drag a rectangle to zoom; filter state is unaffected by zoom or pan.
	m_view->setRubberBand(QChartView::RectangleRubberBand);

	// Top bar: train visibility dropdown, pin state, zoom reset, exports.
	m_trainsButton = new TrainFilterButton(this);
	connect(m_trainsButton, &TrainFilterButton::selectionChanged,
			this, &DiagramWindow::applyTrainVisibility);

	m_clearPinButton = new QPushButton("Clear selection", this);
	connect(m_clearPinButton, &QPushButton::clicked, this, &DiagramWindow::clearPin);
	m_pinLabel = new QLabel("", this);

	QPushButton* resetZoomBtn = new QPushButton("Reset zoom", this);
	resetZoomBtn->setToolTip("Restore full bounds; keep train filters and selection (Home or 0)");
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
	topBar->addWidget(m_clearPinButton);
	topBar->addWidget(m_pinLabel);
	topBar->addStretch();
	topBar->addWidget(resetZoomBtn);
	topBar->addWidget(m_csvButton);
	topBar->addWidget(exportPngBtn);

	m_readout = new QLabel("Hover to inspect; click to select. Drag to zoom; two-finger scroll to pan; "
		"pinch or Ctrl+wheel to zoom. +/- zoom, arrows pan, Home resets. Planned: dashed; actual: solid.", this);
	m_readout->setObjectName("diagramNavigationHelp");
	m_readout->setWordWrap(true);
	m_readout->setTextFormat(Qt::PlainText);
	m_contextLabel = new QLabel(this);
	m_contextLabel->setObjectName("diagramContext");
	m_contextLabel->setTextFormat(Qt::PlainText);
	m_contextLabel->setWordWrap(true);
	m_contextLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_contextLabel->setMaximumHeight(fontMetrics().height() * 3);
	m_contextLabel->hide();
	m_detailsButton = new QPushButton("Technical details", this);
	m_detailsButton->setObjectName("diagramDetailsButton");
	m_detailsButton->setCheckable(true);
	m_detailsButton->hide();
	m_detailsPanel = new QScrollArea(this);
	m_detailsPanel->setObjectName("diagramDetailsPanel");
	auto* detailsScroll = qobject_cast<QScrollArea*>(m_detailsPanel.data());
	detailsScroll->setWidgetResizable(true);
	detailsScroll->setMaximumHeight(120);
	m_detailsLabel = new QLabel;
	m_detailsLabel->setObjectName("diagramDetailsText");
	m_detailsLabel->setTextFormat(Qt::PlainText);
	m_detailsLabel->setWordWrap(true);
	m_detailsLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_detailsLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	detailsScroll->setWidget(m_detailsLabel);
	m_detailsPanel->hide();
	connect(m_detailsButton, &QPushButton::toggled, m_detailsPanel, &QWidget::setVisible);

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->setSpacing(8);
	layout->addLayout(topBar);
	layout->addWidget(m_contextLabel);
	layout->addWidget(m_detailsButton, 0, Qt::AlignLeft);
	layout->addWidget(m_detailsPanel);
	layout->addWidget(m_view, 1);
	layout->addWidget(m_readout);
}

void DiagramWindow::setPresentation(const QString& heading, const QString& context,
		const QString& technicalNotes) {
	setWindowTitle(heading);
	m_contextLabel->setMaximumHeight(m_contextLabel->fontMetrics().height() * 3);
	m_detailsPanel->setMaximumHeight(qMax(120, m_detailsLabel->fontMetrics().height() * 6));
	m_contextLabel->setText(context);
	m_contextLabel->setToolTip(context.toHtmlEscaped());
	m_contextLabel->setVisible(!context.isEmpty());
	m_detailsLabel->setText(technicalNotes);
	m_detailsButton->setVisible(!technicalNotes.isEmpty());
	if (technicalNotes.isEmpty()) m_detailsButton->setChecked(false);
}

void DiagramWindow::setRollingStockSubject(bool on) {
	m_trainsButton->setVisible(!on);
	m_clearPinButton->setVisible(!on);
	m_pinLabel->setVisible(!on);
	m_readout->setText(on
		? QStringLiteral("Input tractive effort by speed. Drag to zoom; two-finger scroll to pan; pinch or Ctrl+wheel to zoom. +/- zoom, arrows pan, Home resets.")
		: QStringLiteral("Hover to inspect; click to select. Drag to zoom; two-finger scroll to pan; pinch or Ctrl+wheel to zoom. +/- zoom, arrows pan, Home resets. Planned: dashed; actual: solid."));
}

void DiagramWindow::setChart(QChart* chart) {
	if (m_view && m_view->chart() == chart) return;
	clearTooltip();
	delete m_numericAxis.data(); // detached axis belongs to this window, not the old chart
	m_numericAxis = nullptr;
	m_fullBounds.clear();
	m_clockAxis = nullptr;
	if (chart)
		applyChartStyle(chart);
	QChart* previous = m_view->chart();
	if (previous != chart)
		m_view->setChart(chart);  // QChartView takes ownership of the new chart
	if (previous && previous != chart)
		delete previous; // setChart releases, rather than deletes, the old chart
	// The train dropdown replaces the built-in legend, which collapses to "..."
	// once many trains are present.
	if (chart && chart->legend())
		chart->legend()->hide();
	// Existing input-traction callers tag their windows before setting the chart.
	// Keep the explicit API for future callers and avoid a train-only filter here.
	if (property("inputTrainUnitId").isValid()) setRollingStockSubject(true);
	rebuildFilterGroups();
	if (chart)
		for (auto* axis : chart->axes())
			if (auto* value = qobject_cast<QValueAxis*>(axis))
				m_fullBounds.insert(axis, {value->min(), value->max()});
	applyTimeAxis();
}

void DiagramWindow::setTimeAxisX(bool on, long long startOffsetSeconds) {
	clearTooltip();
	m_timeOrientation = Qt::Horizontal;
	m_timeAxis = on;
	m_startOffset = startOffsetSeconds;
	applyTimeAxis();
}

void DiagramWindow::setTimeAxisY(bool on, long long startOffsetSeconds) {
	clearTooltip();
	m_timeOrientation = Qt::Vertical;
	m_timeAxis = on;
	m_startOffset = startOffsetSeconds;
	applyTimeAxis();
}

void DiagramWindow::setCsvProvider(std::function<std::string(const QStringList&)> provider,
								   const QString& suggestedFileName) {
	m_csvProvider = std::move(provider);
	m_csvSuggestedName = suggestedFileName;
	if (m_csvButton)
		m_csvButton->setEnabled(static_cast<bool>(m_csvProvider));
}

void DiagramWindow::setProvenanceWriter(
		std::function<bool(const QString&, const char*, const std::string&)> writer) {
	m_provenanceWriter = std::move(writer);
}

// Shared look for every diagram: quiet grid, readable title, line weight that
// survives PNG export.
void DiagramWindow::applyChartStyle(QChart* chart) {
	chart->setBackgroundRoundness(0);
	chart->setBackgroundBrush(palette().brush(QPalette::Base));
	chart->setMargins(QMargins(12, 8, 12, 8));

	QFont titleFont = font();
	titleFont.setPointSizeF(titleFont.pointSizeF() + 2.0);
	titleFont.setBold(true);
	chart->setTitleFont(titleFont);
	chart->setTitleBrush(palette().brush(QPalette::WindowText));

	QColor gridColor = palette().color(QPalette::WindowText);
	gridColor.setAlpha(30);
	const QPen gridPen(gridColor);
	const QBrush labelBrush = palette().brush(QPalette::WindowText);
	const auto axes = chart->axes();
	for (QAbstractAxis* axis : axes) {
		axis->setGridLinePen(gridPen);
		axis->setLabelsBrush(labelBrush);
		axis->setTitleBrush(labelBrush);
		QColor lineColor = palette().color(QPalette::WindowText);
		lineColor.setAlpha(90);
		axis->setLinePen(QPen(lineColor));
	}

	const auto seriesList = chart->series();
	for (QAbstractSeries* series : seriesList) {
		if (auto* xy = qobject_cast<QXYSeries*>(series)) {
			QPen pen = xy->pen();
			if (series->name().contains("planned", Qt::CaseInsensitive))
				pen.setStyle(Qt::DashLine);
			xy->setPen(pen);
			if (pen.widthF() < 2.0) {
				pen.setWidthF(2.0);
				xy->setPen(pen);
			}
		}
	}
}

// Keep numeric ranges and orientation when replacing time labels.
void DiagramWindow::applyTimeAxis() {
	QChart* chart = m_view ? m_view->chart() : nullptr;
	if (!chart) return;
	if (m_clockAxis) {
		auto* old = qobject_cast<QValueAxis*>(m_clockAxis.data());
		const auto alignment = old->alignment();
		const auto bounds = m_fullBounds.take(old);
		auto* numeric = m_numericAxis.data();
		if (!numeric) return;
		numeric->setRange(old->min(), old->max());
		numeric->setReverse(old->isReverse());
		QList<QAbstractSeries*> attached;
		for (auto* series : chart->series())
			if (series->attachedAxes().contains(old)) attached.append(series);
		chart->removeAxis(old);
		old->deleteLater();
		chart->addAxis(numeric, alignment);
		for (auto* series : attached) series->attachAxis(numeric);
		m_fullBounds.insert(numeric, bounds);
		m_numericAxis = nullptr;
		m_clockAxis = nullptr;
	}
	if (!m_timeAxis) return;
	const auto timeAxes = chart->axes(m_timeOrientation);
	if (timeAxes.isEmpty())
		return;
	auto* seconds = qobject_cast<QValueAxis*>(timeAxes.first());
	if (!seconds)
		return;
	const double min = seconds->min();
	const double max = seconds->max();
	const double span = max - min;
	if (!(span > 0.0))
		return;


	auto* clock = new QCategoryAxis(chart);
	clock->setLabelsPosition(QCategoryAxis::AxisLabelsPositionOnValue);
	clock->setStartValue(min - 1);
	clock->setReverse(seconds->isReverse());
	clock->setMin(min);
	clock->setMax(max);
	clock->setTitleText("Time");
	clock->setGridLinePen(seconds->gridLinePen());
	clock->setGridLineVisible(seconds->isGridLineVisible());
	clock->setMinorGridLineVisible(seconds->isMinorGridLineVisible());
	clock->setLinePen(seconds->linePen());
	clock->setLabelsFont(seconds->labelsFont());
	clock->setTitleFont(seconds->titleFont());
	clock->setLabelsBrush(palette().brush(QPalette::WindowText));
	clock->setTitleBrush(palette().brush(QPalette::WindowText));


	QList<QAbstractSeries*> seriesList;
	for (auto* series : chart->series())
		if (series->attachedAxes().contains(seconds)) seriesList.append(series);
	const auto alignment = seconds->alignment();
	const auto bounds = m_fullBounds.take(seconds);
	chart->removeAxis(seconds);
	seconds->setParent(this); // keep the original numeric ticks and grid for the roundtrip
	m_numericAxis = seconds;
	chart->addAxis(clock, alignment);
	m_fullBounds.insert(clock, bounds);
	for (QAbstractSeries* series : seriesList)
		series->attachAxis(clock);
	m_clockAxis = clock;
	const auto updateLabels = [this, clock](qreal low, qreal high) {
		for (const QString& label : clock->categoriesLabels()) clock->remove(label);
		const double raw = (high - low) / 6;
		const double power = std::pow(10.0, std::floor(std::log10(std::max(1.0, raw))));
		double interval = std::max(1.0, std::ceil(raw / power) * power);
		for (double candidate : {1., 5., 10., 30., 60., 120., 300., 600., 900., 1800., 3600., 7200., 14400., 21600., 43200.})
			if ((high - low) / candidate <= 8) { interval = candidate; break; }
		clock->setStartValue(low - interval);
		for (double tick = std::ceil(low / interval) * interval; tick <= high; tick += interval)
			clock->append(QString::fromStdString(formatSimTime(static_cast<long long>(tick), m_startOffset)), tick);
	};
	connect(clock, &QValueAxis::rangeChanged, this, updateLabels);
	updateLabels(min, max);
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
	m_baseBrushes.clear();
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
		if (auto* area = qobject_cast<QAreaSeries*>(series))
			m_baseBrushes.insert(series, area->brush());

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
	clearTooltip();
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
			if (!xy) {
				if (auto* area = qobject_cast<QAreaSeries*>(series)) {
					QBrush brush = m_baseBrushes.value(series, area->brush());
					if (pinnedActive && !isPinned) {
						QColor color = brush.color();
						color.setAlpha(std::min(color.alpha(), 18));
						brush.setColor(color);
					}
					area->setBrush(brush);
				}
				continue;
			}
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

void DiagramWindow::updateReadout(const QPointF& value, const QString& seriesName) {
	if (!m_tooltip || !m_view || !m_view->chart())
		return;

	const QChart* chart = m_view->chart();
	const auto horizontal = chart->axes(Qt::Horizontal);
	const auto vertical = chart->axes(Qt::Vertical);
	const QString xLabel = horizontal.isEmpty() || horizontal.first()->titleText().isEmpty()
		? QString("x") : horizontal.first()->titleText();
	const QString yLabel = vertical.isEmpty() || vertical.first()->titleText().isEmpty()
		? QString("y") : vertical.first()->titleText();
	const QString xText = m_timeAxis && m_timeOrientation == Qt::Horizontal
		? QString::fromStdString(formatSimTime(static_cast<long long>(value.x()), m_startOffset))
		: QString::number(value.x(), 'f', 2);
	const QString yText = m_timeAxis && m_timeOrientation == Qt::Vertical
		? QString::fromStdString(formatSimTime(static_cast<long long>(value.y()), m_startOffset))
		: QString::number(value.y(), 'f', 2);
	m_tooltip->setText(QString("%1\n%2: %3   %4: %5")
		.arg(seriesName).arg(xLabel).arg(xText).arg(yLabel).arg(yText));
}

QStringList DiagramWindow::visibleTrainIds() const {
	if (m_trainsButton)
		return m_trainsButton->visibleTrainIds();
	return QStringList();
}

void DiagramWindow::resetZoom() {
	clearTooltip();
	if (m_view && m_view->chart()) {
		m_view->chart()->zoomReset();
		for (auto it = m_fullBounds.cbegin(); it != m_fullBounds.cend(); ++it)
			if (auto* axis = qobject_cast<QValueAxis*>(it.key()))
				axis->setRange(it.value().first, it.value().second);
	}
}

void DiagramWindow::exportPng() {
	const auto operation = m_telemetryCapture ? m_telemetryCapture() : telemetry::OperationObservation();
	QString path = QFileDialog::getSaveFileName(this, "Export Diagram", "diagram.png", "PNG Image (*.png)");
	if (path.isEmpty())
		return;  // cancelled: no file is written
	if (QFileInfo(path).suffix().compare("png", Qt::CaseInsensitive) != 0)
		path += ".png";
	QPixmap pix = m_view->grab();
	QByteArray data;
	QBuffer buffer(&data);
	if (!buffer.open(QIODevice::WriteOnly) || !pix.save(&buffer, "PNG")) {
		operation.failure(telemetry::Operation::Export, telemetry::Error::InternalFailure);
		QMessageBox::warning(this, "Export failed",
							 QString("Could not write the image to:\n%1").arg(path));
		return;
	}
	const std::string bytes(data.constData(), static_cast<std::size_t>(data.size()));
	const bool written = m_provenanceWriter
		? m_provenanceWriter(path, "png", bytes)
		: writeArtifact(path, bytes);
	operation.exportFinished(telemetry::ExportKind::Png, written, true, telemetry::Error::IoFailure);
	if (!written)
		QMessageBox::warning(this, "Export failed",
			QString("Could not export the image and provenance to:\n%1").arg(path));
}

void DiagramWindow::exportCsv() {
	const auto operation = m_telemetryCapture ? m_telemetryCapture() : telemetry::OperationObservation();
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
	const bool written = m_provenanceWriter
		? m_provenanceWriter(path, "csv", content)
		: writeArtifact(path, content);
	operation.exportFinished(telemetry::ExportKind::Csv, written, true, telemetry::Error::IoFailure);
	if (!written)
		QMessageBox::warning(this, "Export failed",
			QString("Could not export the data and provenance to:\n%1").arg(path));
}

void DiagramWindow::clearTooltip() {
	if (m_tooltip) m_tooltip->hide();
}

QAbstractSeries* DiagramWindow::inspectAt(const QPoint& position) {
	clearTooltip();
	auto* chart = m_view->chart();
	const QPointF cursor = chart->mapFromScene(m_view->mapToScene(position));
	if (!chart->plotArea().contains(cursor)) return nullptr;
	QXYSeries* selected = nullptr;
	QPointF sample;
	int sampleIndex = -1;
	double best = 10;
	QXYSeries* filled = nullptr;
	QPointF filledSample;
	int filledIndex = -1;
	double filledVertexDistance = std::numeric_limits<double>::max();
	for (auto* series : chart->series()) {
		auto* xy = qobject_cast<QXYSeries*>(series);
		if (!xy || !xy->isVisible()) continue;
		const auto points = xy->pointsVector();
		bool inside = false;
		if (xy->property("inspectionFilled").toBool() && points.size() >= 4
				&& points.first() == points.last() && !qobject_cast<QScatterSeries*>(xy)) {
			QPolygonF polygon;
			for (const auto& point : points) polygon.append(chart->mapToPosition(point, xy));
			inside = polygon.containsPoint(cursor, Qt::OddEvenFill);
		}
		for (int i = 0; i < points.size(); ++i) {
			const QPointF point = chart->mapToPosition(points[i], xy);
			if (inside) {
				const double vertexDistance = QLineF(cursor, point).length();
				if (vertexDistance < filledVertexDistance) {
					filledVertexDistance = vertexDistance; filled = xy; filledSample = points[i]; filledIndex = i;
				}
				continue;
			}
			if (chart->plotArea().contains(point)) {
				const double distance = QLineF(cursor, point).length();
				if (distance < best) { best = distance; selected = xy; sample = points[i]; sampleIndex = i; }
			}
			if (i == 0 || qobject_cast<QScatterSeries*>(xy)) continue;
			const QPointF previous = chart->mapToPosition(points[i - 1], xy);
			double first, last;
			if (!clippedSegment(chart->plotArea(), previous, point, first, last)) continue;
			const QPointF delta = point - previous;
			const double length2 = QPointF::dotProduct(delta, delta);
			const double t = length2 > 0 ? std::clamp(QPointF::dotProduct(cursor - previous, delta) / length2, first, last) : first;
			const double distance = QLineF(cursor, previous + t * delta).length();
			if (distance < best) {
				best = distance;
				const int nearest = t < 0.5 ? i - 1 : i;
				selected = xy; sample = points[nearest]; sampleIndex = nearest;
			}
		}
	}
	if (!selected && filled) {
		selected = filled;
		sample = filledSample;
		sampleIndex = filledIndex;
	}
	if (!selected) return nullptr;
	QString identity = selected->name();
	if (!identity.contains(groupIdForSeries(selected))) identity.prepend(groupIdForSeries(selected) + " ");
	updateReadout(sample, identity + " (nearest sample)");
	const QStringList contexts = selected->property("inspectionPoints").toStringList();
	if (sampleIndex >= 0 && sampleIndex < contexts.size() && !contexts[sampleIndex].isEmpty())
		m_tooltip->setText(m_tooltip->text() + "\n" + contexts[sampleIndex]);
	const QString interval = selected->property("inspectionInterval").toString();
	if (!interval.isEmpty()) m_tooltip->setText(m_tooltip->text() + "\n" + interval);
	m_tooltip->setWordWrap(true);
	m_tooltip->setMaximumWidth(std::max(100, std::min(480, width() - 24)));
	m_tooltip->adjustSize();
	QPoint local = m_view->viewport()->mapTo(this, position) + QPoint(16, 20);
	if (local.x() + m_tooltip->width() > width()) local.setX(local.x() - m_tooltip->width() - 32);
	if (local.y() + m_tooltip->height() > height()) local.setY(local.y() - m_tooltip->height() - 40);
	local.setX(std::clamp(local.x(), 0, std::max(0, width() - m_tooltip->width())));
	local.setY(std::clamp(local.y(), 0, std::max(0, height() - m_tooltip->height())));
	m_tooltip->move(local);
	m_tooltip->show();
	m_tooltip->raise();
	return selected;
}

void DiagramWindow::navigate(double factor, const QPointF& position, const QPointF& pan) {
	clearTooltip();
	if (!std::isfinite(factor) || factor <= 0) return;
	const QRectF plot = m_view->chart()->plotArea();
	if (plot.width() <= 0 || plot.height() <= 0) return;
	QVector<QPair<QValueAxis*, QPair<double, double>>> destinations;
	for (auto it = m_fullBounds.cbegin(); it != m_fullBounds.cend(); ++it) {
		auto* axis = qobject_cast<QValueAxis*>(it.key());
		if (!axis) continue;
		const double full = it.value().second - it.value().first;
		if (!(full > 0)) continue;
		const bool horizontal = axis->orientation() == Qt::Horizontal;
		double fraction = horizontal ? (position.x() - plot.left()) / plot.width() : (plot.bottom() - position.y()) / plot.height();
		fraction = std::clamp(fraction, 0.0, 1.0);
		if (axis->isReverse()) fraction = 1 - fraction;
		const double span = axis->max() - axis->min();
		const double next = std::clamp(span / factor, full / 1000000, full);
		double shift = horizontal ? -pan.x() / plot.width() : pan.y() / plot.height();
		if (axis->isReverse()) shift = -shift;
		const double low = std::clamp(axis->min() + fraction * (span - next) + shift * span,
			it.value().first, it.value().second - next);
		destinations.append({axis, {low, low + next}});
	}
	// Attached axes can synchronize when any range changes. Capture all ranges
	// before the first mutation so each receives exactly one navigation step.
	for (const auto& destination : destinations)
		destination.first->setRange(destination.second.first, destination.second.second);
}

bool DiagramWindow::eventFilter(QObject* obj, QEvent* ev) {
	if (ev->type() == QEvent::Hide || ev->type() == QEvent::Close || ev->type() == QEvent::Leave)
		clearTooltip();
	if (!m_view || obj != m_view->viewport() || !m_view->chart())
		return QDialog::eventFilter(obj, ev);
	const auto chartPosition = [this](const QPoint& p) { return m_view->chart()->mapFromScene(m_view->mapToScene(p)); };
	if (ev->type() == QEvent::MouseMove) {
		auto* mouse = static_cast<QMouseEvent*>(ev);
		if (mouse->buttons() == Qt::NoButton) inspectAt(mouse->pos());
		else clearTooltip();
	} else if (ev->type() == QEvent::MouseButtonPress) {
		m_pressPosition = static_cast<QMouseEvent*>(ev)->pos();
		clearTooltip();
	} else if (ev->type() == QEvent::MouseButtonRelease) {
		auto* mouse = static_cast<QMouseEvent*>(ev);
		if (mouse->button() == Qt::LeftButton && (mouse->pos() - m_pressPosition).manhattanLength() < QApplication::startDragDistance())
			if (auto* series = inspectAt(mouse->pos())) pinTrain(groupIdForSeries(series));
	} else if (ev->type() == QEvent::Wheel) {
		auto* wheel = static_cast<QWheelEvent*>(ev);
		const QPoint pixels = wheel->pixelDelta();
		const QPoint angles = wheel->angleDelta();
		if (pixels.isNull() && angles.isNull()) { wheel->accept(); return true; }
		if (wheel->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)) {
			const double delta = pixels.isNull()
				? (angles.y() != 0 ? angles.y() : angles.x()) / 120.0
				: (pixels.y() != 0 ? pixels.y() : pixels.x()) / 100.0;
			navigate(std::exp(std::clamp(delta * 0.2, -2.0, 2.0)), chartPosition(wheel->position().toPoint()));
		} else {
			QPointF pan = pixels.isNull() ? QPointF(angles) / 3 : QPointF(pixels);
			if (wheel->modifiers() & Qt::ShiftModifier) pan = QPointF(pan.y(), pan.x());
			navigate(1, chartPosition(wheel->position().toPoint()), pan);
		}
		wheel->accept(); return true;
	} else if (ev->type() == QEvent::NativeGesture) {
		auto* gesture = static_cast<QNativeGestureEvent*>(ev);
		if (gesture->gestureType() == Qt::ZoomNativeGesture) {
			navigate(1 + gesture->value(), chartPosition(gesture->localPos().toPoint()));
			gesture->accept(); return true;
		}
	} else if (ev->type() == QEvent::Gesture) {
		auto* event = static_cast<QGestureEvent*>(ev);
		if (auto* pinch = static_cast<QPinchGesture*>(event->gesture(Qt::PinchGesture))) {
			if (pinch->changeFlags() & QPinchGesture::ScaleFactorChanged)
				navigate(pinch->scaleFactor(), chartPosition(pinch->centerPoint().toPoint()));
			event->accept(pinch); return true;
		}
	}
	return QDialog::eventFilter(obj, ev);
}
