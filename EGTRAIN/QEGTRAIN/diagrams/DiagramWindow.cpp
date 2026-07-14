#include "diagrams/DiagramWindow.h"
#include "util/TimeFormat.h"

#include <QAbstractItemView>
#include <QColor>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSaveFile>
#include <QVariant>
#include <QVBoxLayout>
#include <QtCharts/QAbstractSeries>
#include <QtCharts/QLegend>
#include <QtCharts/QXYSeries>

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

	// Filter panel: search, quick actions, and one row per train.
	QLabel* filterTitle = new QLabel("Trains", this);
	m_search = new QLineEdit(this);
	m_search->setPlaceholderText("Search trains...");
	m_search->setClearButtonEnabled(true);
	connect(m_search, &QLineEdit::textChanged, this, &DiagramWindow::applyGroupFilter);

	QPushButton* allBtn = new QPushButton("All", this);
	QPushButton* noneBtn = new QPushButton("None", this);
	connect(allBtn, &QPushButton::clicked, this, &DiagramWindow::selectAllGroups);
	connect(noneBtn, &QPushButton::clicked, this, &DiagramWindow::selectNoGroups);
	QHBoxLayout* quickRow = new QHBoxLayout();
	quickRow->addWidget(allBtn);
	quickRow->addWidget(noneBtn);

	m_groupList = new QListWidget(this);
	m_groupList->setSelectionMode(QAbstractItemView::NoSelection);
	connect(m_groupList, &QListWidget::itemChanged, this, &DiagramWindow::onGroupItemChanged);

	QVBoxLayout* panelLayout = new QVBoxLayout();
	panelLayout->addWidget(filterTitle);
	panelLayout->addWidget(m_search);
	panelLayout->addLayout(quickRow);
	panelLayout->addWidget(m_groupList, 1);
	QWidget* panel = new QWidget(this);
	panel->setLayout(panelLayout);
	panel->setMinimumWidth(220);
	panel->setMaximumWidth(320);

	QHBoxLayout* topRow = new QHBoxLayout();
	topRow->addWidget(m_view, 1);
	topRow->addWidget(panel);

	// Bottom bar: export buttons, pinned readout, hover readout.
	QPushButton* exportPngBtn = new QPushButton("Export PNG...", this);
	connect(exportPngBtn, &QPushButton::clicked, this, &DiagramWindow::exportPng);
	m_csvButton = new QPushButton("Export CSV...", this);
	m_csvButton->setEnabled(false);
	connect(m_csvButton, &QPushButton::clicked, this, &DiagramWindow::exportCsv);
	QPushButton* clearPinBtn = new QPushButton("Clear selection", this);
	connect(clearPinBtn, &QPushButton::clicked, this, &DiagramWindow::clearPin);

	m_pinLabel = new QLabel("", this);
	m_readout = new QLabel("hover over the chart", this);

	QHBoxLayout* bar = new QHBoxLayout();
	bar->addWidget(exportPngBtn);
	bar->addWidget(m_csvButton);
	bar->addWidget(clearPinBtn);
	bar->addWidget(m_pinLabel);
	bar->addStretch();
	bar->addWidget(m_readout);

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->addLayout(topRow, 1);
	layout->addLayout(bar);
}

void DiagramWindow::setChart(QChart* chart) {
	m_view->setChart(chart);  // QChartView takes ownership
	// The train panel replaces the built-in legend, which collapses to "..."
	// once many trains are present.
	if (chart && chart->legend())
		chart->legend()->hide();
	rebuildFilterPanel();
}

void DiagramWindow::setTimeAxisX(bool on, long long startOffsetSeconds) {
	m_timeAxis = on;
	m_startOffset = startOffsetSeconds;
}

void DiagramWindow::setCsvProvider(std::function<std::string(const QStringList&)> provider,
								   const QString& suggestedFileName) {
	m_csvProvider = std::move(provider);
	m_csvSuggestedName = suggestedFileName;
	if (m_csvButton)
		m_csvButton->setEnabled(static_cast<bool>(m_csvProvider));
}

QString DiagramWindow::groupIdForSeries(QAbstractSeries* series) const {
	if (!series)
		return QString();
	const QVariant trainId = series->property("trainId");
	if (trainId.isValid() && !trainId.toString().isEmpty())
		return trainId.toString();
	return series->name();
}

void DiagramWindow::rebuildFilterPanel() {
	m_groups.clear();
	m_basePens.clear();
	if (m_groupList)
		m_groupList->clear();
	QChart* chart = m_view ? m_view->chart() : nullptr;
	if (!chart)
		return;

	QHash<QString, int> indexByTrain;
	for (QAbstractSeries* series : chart->series()) {
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

	if (!m_groupList)
		return;
	for (int i = 0; i < m_groups.size(); ++i) {
		SeriesGroup& group = m_groups[i];
		auto* item = new QListWidgetItem(group.trainId, m_groupList);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(Qt::Checked);
		item->setData(Qt::UserRole, group.trainId);
		// Show the train's line colour as a swatch so the panel reads as a legend.
		if (!group.members.isEmpty()) {
			if (auto* xy = qobject_cast<QXYSeries*>(group.members.first())) {
				QPixmap swatch(12, 12);
				swatch.fill(xy->pen().color());
				item->setIcon(QIcon(swatch));
			}
		}
		group.item = item;
	}
}

void DiagramWindow::onGroupItemChanged(QListWidgetItem* item) {
	if (!item)
		return;
	setGroupChecked(item->data(Qt::UserRole).toString(), item->checkState() == Qt::Checked);
}

void DiagramWindow::setGroupChecked(const QString& trainId, bool checked) {
	for (const SeriesGroup& group : m_groups) {
		if (group.trainId != trainId)
			continue;
		for (QAbstractSeries* series : group.members)
			series->setVisible(checked);
	}
	refreshEmphasis();
}

void DiagramWindow::applyGroupFilter() {
	const QString needle = m_search ? m_search->text().trimmed() : QString();
	for (const SeriesGroup& group : m_groups) {
		if (!group.item)
			continue;
		const bool match = needle.isEmpty() ||
			group.trainId.contains(needle, Qt::CaseInsensitive);
		group.item->setHidden(!match);
	}
}

void DiagramWindow::selectAllGroups() {
	for (const SeriesGroup& group : m_groups)
		if (group.item && !group.item->isHidden())
			group.item->setCheckState(Qt::Checked);
}

void DiagramWindow::selectNoGroups() {
	for (const SeriesGroup& group : m_groups)
		if (group.item && !group.item->isHidden())
			group.item->setCheckState(Qt::Unchecked);
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
	QStringList ids;
	for (const SeriesGroup& group : m_groups) {
		if (group.item && group.item->checkState() == Qt::Checked)
			ids.append(group.trainId);
	}
	return ids;
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
