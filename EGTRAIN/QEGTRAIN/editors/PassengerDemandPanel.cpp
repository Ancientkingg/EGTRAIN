#include "editors/PassengerDemandPanel.h"

#include "scene/PassengerDemandCsv.h"
#include "scene/SceneDiagnostic.h"
#include "scene/SceneModel.h"
#include "util/TimeFormat.h"
#include "widgets/DialogLayout.h"
#include "widgets/DiagnosticLines.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>

namespace DemandRanges {

double snapToMinute(double seconds) {
	return std::round(seconds / SnapSeconds) * SnapSeconds;
}

std::optional<std::pair<double, double>> newRange(const std::vector<Bar>& bars, double anchor, double other) {
	const double from = std::clamp(snapToMinute(anchor), 0.0, DayEnd);
	const double to = std::clamp(snapToMinute(other), 0.0, DayEnd);
	double gapStart = 0.0;
	double gapEnd = DayEnd;
	for (const Bar& bar : bars) {
		if (bar.startSeconds < from && from < bar.endSeconds)
			return std::nullopt;
		if (bar.endSeconds <= from)
			gapStart = std::max(gapStart, bar.endSeconds);
		else if (bar.startSeconds >= from)
			gapEnd = std::min(gapEnd, bar.startSeconds);
	}
	const double low = std::clamp(std::min(from, to), gapStart, gapEnd);
	const double high = std::clamp(std::max(from, to), gapStart, gapEnd);
	if (high - low < SnapSeconds)
		return std::nullopt;
	return std::make_pair(low, high);
}

namespace {

// The first free space of [from, to] that is at least one minute long, as a range of one hour or less.
std::optional<std::pair<double, double>> firstGap(std::vector<Bar> bars, double from, double to) {
	std::sort(bars.begin(), bars.end(), [](const Bar& a, const Bar& b) { return a.startSeconds < b.startSeconds; });
	double cursor = std::ceil(from / SnapSeconds) * SnapSeconds;
	const auto gapAt = [&cursor](double gapEnd) -> std::optional<std::pair<double, double>> {
		if (gapEnd - cursor < SnapSeconds)
			return std::nullopt;
		return std::make_pair(cursor, std::min(cursor + 3600.0, gapEnd));
	};
	for (const Bar& bar : bars) {
		if (bar.endSeconds <= cursor)
			continue;
		if (const auto gap = gapAt(std::min(bar.startSeconds, to)))
			return gap;
		cursor = std::max(cursor, std::ceil(bar.endSeconds / SnapSeconds) * SnapSeconds);
	}
	return gapAt(to);
}

} // namespace

std::optional<std::pair<double, double>> firstFreeRange(const std::vector<Bar>& bars, double from, double to) {
	if (const auto inHorizon = firstGap(bars, std::max(0.0, from), std::min(DayEnd, to)))
		return inHorizon;
	return firstGap(bars, 0.0, DayEnd);
}

std::pair<double, double> movedEdge(
	const std::vector<Bar>& bars, const QString& id, bool startEdge, double seconds, double minimumLength) {
	const auto self = std::find_if(bars.begin(), bars.end(), [&id](const Bar& bar) { return bar.id == id; });
	if (self == bars.end())
		return {0.0, 0.0};
	double low = 0.0;
	double high = DayEnd;
	for (const Bar& bar : bars) {
		if (&bar == &*self)
			continue;
		if (bar.endSeconds <= self->startSeconds)
			low = std::max(low, bar.endSeconds);
		else if (bar.startSeconds >= self->endSeconds)
			high = std::min(high, bar.startSeconds);
	}
	std::pair<double, double> period{self->startSeconds, self->endSeconds};
	if (startEdge) {
		const double limit = self->endSeconds - minimumLength;
		if (low <= limit)
			period.first = std::clamp(seconds, low, limit);
	} else {
		const double limit = self->startSeconds + minimumLength;
		if (limit <= high)
			period.second = std::clamp(seconds, limit, high);
	}
	return period;
}

QString clockText(double seconds) {
	return QString::fromStdString(formatClockSeconds(std::lround(seconds)));
}

QString shortClockText(double seconds) {
	return clockText(seconds).left(5);
}

std::optional<double> parseClock(const QString& text) {
	static const QRegularExpression pattern(QStringLiteral("^(\\d{1,2}):(\\d{2})(?::(\\d{2}))?$"));
	const QRegularExpressionMatch match = pattern.match(text.trimmed());
	if (!match.hasMatch())
		return std::nullopt;
	const int hours = match.captured(1).toInt();
	const int minutes = match.captured(2).toInt();
	const int seconds = match.captured(3).toInt();
	if (minutes > 59 || seconds > 59)
		return std::nullopt;
	const int total = hours * 3600 + minutes * 60 + seconds;
	if (total > static_cast<int>(DayEnd))
		return std::nullopt;
	return static_cast<double>(total);
}

} // namespace DemandRanges

// ---- DemandTimeline ----

namespace {

constexpr int AxisMargin = 8;
constexpr int StubWidth = 8;
constexpr int EdgeGrab = 4;
constexpr int BarHeight = 26;
constexpr int MinimumDragPixels = 3;

} // namespace

DemandTimeline::DemandTimeline(QWidget* parent) : QWidget(parent) {
	setObjectName("demandTimeline");
	setAccessibleName("Demand timeline");
	setFocusPolicy(Qt::StrongFocus);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	updateDescription();
}

void DemandTimeline::setHorizon(double startSeconds, double endSeconds) {
	if (endSeconds <= startSeconds) {
		startSeconds = 0.0;
		endSeconds = DemandRanges::DayEnd;
	}
	m_horizonStart = startSeconds;
	m_horizonEnd = endSeconds;
	update();
}

void DemandTimeline::setBars(std::vector<DemandRanges::Bar> bars) {
	m_bars = std::move(bars);
	if (!barById(m_selectedId))
		m_selectedId.clear();
	updateDescription();
	update();
}

void DemandTimeline::setSelectedId(const QString& id) {
	m_selectedId = barById(id) ? id : QString();
	update();
}

void DemandTimeline::setEditable(bool editable) {
	m_editable = editable;
	m_drag = Drag::None;
	update();
}

QRect DemandTimeline::axisRect() const {
	return QRect(AxisMargin, 0, std::max(1, width() - 2 * AxisMargin), height());
}

int DemandTimeline::barsY() const {
	return fontMetrics().height() + 4 + BarHeight / 2;
}

double DemandTimeline::xOfSeconds(double seconds) const {
	const QRect axis = axisRect();
	return axis.left() + (seconds - m_horizonStart) / (m_horizonEnd - m_horizonStart) * axis.width();
}

double DemandTimeline::secondsAtX(double x) const {
	const QRect axis = axisRect();
	const double fraction = (x - axis.left()) / axis.width();
	return std::clamp(m_horizonStart + fraction * (m_horizonEnd - m_horizonStart), m_horizonStart, m_horizonEnd);
}

const DemandRanges::Bar* DemandTimeline::barById(const QString& id) const {
	if (id.isEmpty())
		return nullptr;
	for (const DemandRanges::Bar& bar : m_bars) {
		if (bar.id == id)
			return &bar;
	}
	return nullptr;
}

QRect DemandTimeline::barRect(const QString& id) const {
	const DemandRanges::Bar* bar = barById(id);
	if (!bar)
		return {};
	const QRect axis = axisRect();
	const int top = fontMetrics().height() + 4;
	if (bar->endSeconds <= m_horizonStart)
		return QRect(axis.left(), top, StubWidth, BarHeight);
	if (bar->startSeconds >= m_horizonEnd)
		return QRect(axis.right() - StubWidth + 1, top, StubWidth, BarHeight);
	const int left = static_cast<int>(std::lround(xOfSeconds(std::max(bar->startSeconds, m_horizonStart))));
	const int right = static_cast<int>(std::lround(xOfSeconds(std::min(bar->endSeconds, m_horizonEnd))));
	return QRect(left, top, std::max(3, right - left), BarHeight);
}

const DemandRanges::Bar* DemandTimeline::barAt(const QPoint& position) const {
	for (auto it = m_bars.rbegin(); it != m_bars.rend(); ++it) {
		if (barRect(it->id).contains(position))
			return &*it;
	}
	return nullptr;
}

QSize DemandTimeline::sizeHint() const {
	return QSize(fontMetrics().averageCharWidth() * 60, fontMetrics().height() + BarHeight + 12);
}

QSize DemandTimeline::minimumSizeHint() const {
	return QSize(fontMetrics().averageCharWidth() * 24, fontMetrics().height() + BarHeight + 12);
}

std::optional<std::pair<double, double>> DemandTimeline::dragRange() const {
	switch (m_drag) {
		case Drag::Create:
			return DemandRanges::newRange(m_bars, m_dragAnchor, m_dragCurrent);
		case Drag::StartEdge:
		case Drag::EndEdge:
			return DemandRanges::movedEdge(m_bars, m_selectedId, m_drag == Drag::StartEdge,
				DemandRanges::snapToMinute(m_dragCurrent), DemandRanges::SnapSeconds);
		case Drag::None:
			break;
	}
	return std::nullopt;
}

void DemandTimeline::updateDescription() {
	QStringList lines;
	for (const DemandRanges::Bar& bar : m_bars)
		lines << QStringLiteral("%1 %2 to %3")
					 .arg(bar.id, DemandRanges::shortClockText(bar.startSeconds), DemandRanges::shortClockText(bar.endSeconds));
	setAccessibleDescription(lines.isEmpty() ? QStringLiteral("No demand slices") : lines.join(QStringLiteral("; ")));
}

void DemandTimeline::paintEvent(QPaintEvent*) {
	QPainter painter(this);
	if (!isEnabled())
		painter.setOpacity(0.5);
	const QRect axis = axisRect();
	const QFontMetrics metrics = fontMetrics();
	const int rulerHeight = metrics.height() + 4;
	const QRect track(axis.left(), rulerHeight, axis.width(), BarHeight);
	painter.fillRect(track, palette().color(QPalette::Base));
	painter.setPen(palette().color(QPalette::Mid));
	painter.drawRect(track.adjusted(0, 0, -1, -1));

	// Marks as fine as the width allows.
	const double span = m_horizonEnd - m_horizonStart;
	const double pixelsPerSecond = axis.width() / span;
	double step = 21600.0;
	for (const double candidate : {900.0, 1800.0, 3600.0, 7200.0, 10800.0, 21600.0}) {
		if (candidate * pixelsPerSecond >= 56.0) {
			step = candidate;
			break;
		}
	}
	painter.setPen(palette().color(QPalette::Text));
	const long long firstMark = static_cast<long long>(std::ceil(m_horizonStart / step));
	const long long lastMark = static_cast<long long>(std::floor((m_horizonEnd + 1e-6) / step));
	for (long long index = firstMark; index <= lastMark; ++index) {
		const double mark = static_cast<double>(index) * step;
		const int x = static_cast<int>(std::lround(xOfSeconds(mark)));
		painter.drawLine(x, rulerHeight - 3, x, rulerHeight);
		const QString label = DemandRanges::shortClockText(std::fmod(mark, DemandRanges::DayEnd));
		const int labelWidth = metrics.horizontalAdvance(label);
		const int labelLeft = std::clamp(x - labelWidth / 2, 0, std::max(0, width() - labelWidth));
		painter.drawText(labelLeft, metrics.ascent(), label);
	}

	for (const DemandRanges::Bar& bar : m_bars) {
		const QRect rect = barRect(bar.id);
		const bool selected = bar.id == m_selectedId;
		QColor fill = palette().color(QPalette::Highlight);
		if (!selected)
			fill.setAlpha(110);
		painter.fillRect(rect, fill);
		painter.setPen(selected ? palette().color(QPalette::HighlightedText) : palette().color(QPalette::Text));
		painter.drawRect(rect.adjusted(0, 0, -1, -1));
		const bool before = bar.endSeconds <= m_horizonStart;
		const bool after = bar.startSeconds >= m_horizonEnd;
		if (before || after) {
			painter.drawText(rect, Qt::AlignCenter, before ? QStringLiteral("<") : QStringLiteral(">"));
			continue;
		}
		const QString label = QStringLiteral("%1-%2").arg(
			DemandRanges::shortClockText(bar.startSeconds), DemandRanges::shortClockText(bar.endSeconds));
		painter.drawText(rect.adjusted(3, 0, -3, 0), Qt::AlignCenter, metrics.elidedText(label, Qt::ElideRight, rect.width() - 6));
	}

	if (m_drag != Drag::None) {
		if (const auto preview = dragRange()) {
			const int left = static_cast<int>(std::lround(xOfSeconds(preview->first)));
			const int right = static_cast<int>(std::lround(xOfSeconds(preview->second)));
			painter.setPen(QPen(palette().color(QPalette::Highlight), 2, Qt::DashLine));
			painter.setBrush(Qt::NoBrush);
			painter.drawRect(QRect(left, rulerHeight, std::max(1, right - left), BarHeight));
		}
	}
	if (hasFocus()) {
		painter.setPen(QPen(palette().color(QPalette::Highlight), 1, Qt::DotLine));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(rect().adjusted(0, 0, -1, -1));
	}
}

void DemandTimeline::mousePressEvent(QMouseEvent* event) {
	if (event->button() != Qt::LeftButton || !isEnabled()) {
		QWidget::mousePressEvent(event);
		return;
	}
	setFocus(Qt::MouseFocusReason);
	const QPoint position = event->position().toPoint();
	m_drag = Drag::None;
	if (const DemandRanges::Bar* selected = barById(m_selectedId); selected && m_editable) {
		const QRect rect = barRect(selected->id);
		if (rect.top() <= position.y() && position.y() <= rect.bottom()) {
			if (selected->startSeconds >= m_horizonStart && std::abs(position.x() - rect.left()) <= EdgeGrab)
				m_drag = Drag::StartEdge;
			else if (selected->endSeconds <= m_horizonEnd && std::abs(position.x() - rect.right() - 1) <= EdgeGrab)
				m_drag = Drag::EndEdge;
		}
	}
	if (m_drag == Drag::None) {
		if (const DemandRanges::Bar* bar = barAt(position)) {
			Q_EMIT sliceClicked(bar->id);
			return;
		}
		if (!m_editable)
			return;
		m_drag = Drag::Create;
	}
	m_dragAnchor = secondsAtX(position.x());
	m_dragCurrent = m_dragAnchor;
	update();
}

void DemandTimeline::mouseMoveEvent(QMouseEvent* event) {
	if (m_drag == Drag::None)
		return;
	m_dragCurrent = secondsAtX(event->position().x());
	update();
}

void DemandTimeline::mouseReleaseEvent(QMouseEvent* event) {
	if (event->button() != Qt::LeftButton || m_drag == Drag::None)
		return;
	const Drag drag = m_drag;
	m_dragCurrent = secondsAtX(event->position().x());
	const bool moved = std::abs(xOfSeconds(m_dragCurrent) - xOfSeconds(m_dragAnchor)) >= MinimumDragPixels;
	const auto range = moved ? dragRange() : std::nullopt;
	m_drag = Drag::None;
	update();
	if (!range)
		return;
	if (drag == Drag::Create)
		Q_EMIT sliceDragged(range->first, range->second);
	else
		Q_EMIT edgeDragged(m_selectedId, drag == Drag::StartEdge, drag == Drag::StartEdge ? range->first : range->second);
}

void DemandTimeline::keyPressEvent(QKeyEvent* event) {
	if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && m_editable && !m_selectedId.isEmpty()) {
		Q_EMIT deleteRequested();
		return;
	}
	QWidget::keyPressEvent(event);
}

// ---- PassengerDemandPanel ----

namespace {

// The name of a station as the matrix shows it.
QString stationLabel(const SceneStation& station) {
	return QString::fromStdString(sceneStationLabel(station));
}

long long totalOf(const ScenePassengerDemandSlice& slice) {
	long long total = 0;
	for (const ScenePassengerDemandCell& cell : slice.cells)
		total += cell.count;
	return total;
}

QString countLabel(long long count, const char* singular, const char* plural) {
	return QStringLiteral("%1 %2").arg(count).arg(QString::fromLatin1(count == 1 ? singular : plural));
}

bool showDayImportPreviewDialog(QWidget* parent, const DemandDayPreview& preview) {
	QDialog dialog(parent);
	dialog.setObjectName("demandImportDayDialog");
	dialog.setProperty("dialogPresentation", true);

	auto* body = new QWidget;
	auto* bodyLayout = new QVBoxLayout(body);
	bodyLayout->setContentsMargins(0, 0, 0, 0);

	auto* slicesTable = new QTableWidget(static_cast<int>(preview.slices.size()), 3, body);
	slicesTable->setObjectName("demandPreviewSlicesTable");
	slicesTable->setAccessibleName("Demand slices to import");
	slicesTable->setHorizontalHeaderLabels({"Start", "End", "Passengers"});
	slicesTable->verticalHeader()->hide();
	slicesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
	slicesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
	slicesTable->setSelectionMode(QAbstractItemView::SingleSelection);
	slicesTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	slicesTable->horizontalHeader()->setStretchLastSection(true);
	for (std::size_t idx = 0; idx < preview.slices.size(); ++idx) {
		const DemandDayPreview::SliceRow& row = preview.slices[idx];
		const int rowIndex = static_cast<int>(idx);
		slicesTable->setItem(rowIndex, 0, new QTableWidgetItem(row.start));
		slicesTable->setItem(rowIndex, 1, new QTableWidgetItem(row.end));
		auto* countItem = new QTableWidgetItem(QString::number(row.passengers));
		countItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
		slicesTable->setItem(rowIndex, 2, countItem);
	}
	const int visibleRows = std::clamp(static_cast<int>(preview.slices.size()), 1, 8);
	slicesTable->setMinimumHeight(slicesTable->horizontalHeader()->sizeHint().height()
		+ slicesTable->fontMetrics().lineSpacing() * (visibleRows + 1) + 2 * slicesTable->frameWidth());
	bodyLayout->addWidget(slicesTable, 1);

	auto* totalLabel = new QLabel(preview.totalText, body);
	totalLabel->setObjectName("demandPreviewTotalLabel");
	totalLabel->setTextFormat(Qt::PlainText);
	totalLabel->setWordWrap(true);
	bodyLayout->addWidget(totalLabel);

	if (!preview.unknownStations.isEmpty()) {
		auto* unknownLabel = new QLabel(body);
		unknownLabel->setObjectName("demandPreviewUnknownLabel");
		DialogLayout::setStatus(unknownLabel, DialogLayout::Severity::Warning,
			QStringLiteral("Unknown stations in the file were skipped (%1):")
				.arg(countLabel(preview.unknownStations.size(), "name", "names")));
		bodyLayout->addWidget(unknownLabel);

		auto* unknownList = new QListWidget(body);
		unknownList->setObjectName("demandPreviewUnknownList");
		unknownList->setAccessibleName("Skipped unknown station names");
		unknownList->addItems(preview.unknownStations);
		const int unknownRows = std::clamp(static_cast<int>(preview.unknownStations.size()), 1, 6);
		unknownList->setMinimumHeight(unknownList->fontMetrics().lineSpacing() * (unknownRows + 1)
			+ 2 * unknownList->frameWidth());
		bodyLayout->addWidget(unknownList);
	}

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	if (QPushButton* okButton = buttons->button(QDialogButtonBox::Ok)) {
		okButton->setObjectName("demandPreviewOkButton");
		okButton->setDefault(true);
	}
	if (QPushButton* cancelButton = buttons->button(QDialogButtonBox::Cancel))
		cancelButton->setObjectName("demandPreviewCancelButton");
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

	DialogLayout::install(dialog, QStringLiteral("Import Demand Day"), preview.replaceWarning, body, buttons);
	DialogLayout::fitWidthToContent(dialog);
	return dialog.exec() == QDialog::Accepted;
}

// The editor of a count: a whole number of 0 or more, or nothing.
class CountDelegate : public QStyledItemDelegate {
public:
	using QStyledItemDelegate::QStyledItemDelegate;

	QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex&) const override {
		auto* editor = new QLineEdit(parent);
		// Empty text is acceptable, so that an emptied cell can be committed; the count is checked when it is committed.
		editor->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9]{0,7}")), editor));
		editor->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
		editor->setAccessibleName("Passengers");
		return editor;
	}

	// The macOS style ignores the background of an item once the style sheet styles view items, so paint it here.
	void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
		const QVariant background = index.data(Qt::BackgroundRole);
		if (background.canConvert<QBrush>() && background.value<QBrush>().style() != Qt::NoBrush)
			painter->fillRect(option.rect, background.value<QBrush>());
		QStyledItemDelegate::paint(painter, option, index);
	}
};

} // namespace

DemandDayPreview buildDemandDayPreview(const PassengerDemandTable& table) {
	DemandDayPreview preview;
	preview.slices.reserve(table.slices.size());
	for (const ScenePassengerDemandSlice& slice : table.slices) {
		preview.slices.push_back({DemandRanges::clockText(slice.startSeconds),
			DemandRanges::clockText(slice.endSeconds), totalOf(slice)});
	}
	for (const PassengerDemandUnknownStation& unknown : table.unknownStations) {
		preview.unknownStations << QStringLiteral("%1: %2, %3 skipped")
									   .arg(QString::fromStdString(unknown.name),
										   countLabel(unknown.rows, "row", "rows"),
										   countLabel(unknown.passengers, "passenger", "passengers"));
	}
	preview.totalText = QStringLiteral("Total: %1 across %2.")
							.arg(countLabel(table.totalPassengers, "passenger", "passengers"),
								countLabel(static_cast<long long>(table.slices.size()), "slice", "slices"));
	preview.replaceWarning = QStringLiteral(
		"Importing this table replaces all demand slices of the case. Undo restores the previous slices.");
	return preview;
}

PassengerDemandPanel::PassengerDemandPanel(QWidget* parent) : SceneEditorPanel(parent) {
	setObjectName("demandPanel");
	auto* panelLayout = new QVBoxLayout(this);
	m_diagnosticLabel = new QLabel(this);
	m_diagnosticLabel->setObjectName("demandDiagnosticLabel");
	m_diagnosticLabel->setAccessibleName("Demand messages");
	panelLayout->addWidget(m_diagnosticLabel);
	m_body = new QWidget(this);
	m_body->setObjectName("demandBody");
	panelLayout->addWidget(m_body, 1);
	auto* layout = new QVBoxLayout(m_body);
	layout->setContentsMargins(0, 0, 0, 0);

	m_horizonLabel = new QLabel(m_body);
	m_horizonLabel->setObjectName("demandHorizonLabel");
	m_horizonLabel->setAccessibleName("Case horizon");
	layout->addWidget(m_horizonLabel);
	m_timeline = new DemandTimeline(m_body);
	m_timeline->setToolTip("Drag over empty space to add a slice. Click a slice to select it, drag its edge to move the edge, "
						   "press Delete to remove it.");
	layout->addWidget(m_timeline);

	auto* sliceRow = new QHBoxLayout();
	m_sliceCombo = new QComboBox(m_body);
	m_sliceCombo->setObjectName("demandSliceCombo");
	m_sliceCombo->setAccessibleName("Demand slice");
	m_sliceCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_sliceCombo->setMinimumContentsLength(18);
	sliceRow->addWidget(m_sliceCombo, 1);
	m_addButton = new QPushButton("Add slice", m_body);
	m_addButton->setObjectName("demandAddButton");
	m_addButton->setAccessibleName("Add demand slice");
	sliceRow->addWidget(m_addButton);
	m_deleteButton = new QPushButton("Delete", m_body);
	m_deleteButton->setObjectName("demandDeleteButton");
	m_deleteButton->setAccessibleName("Delete demand slice");
	sliceRow->addWidget(m_deleteButton);
	layout->addLayout(sliceRow);

	auto* timeRow = new QHBoxLayout();
	timeRow->addWidget(new QLabel("Start", m_body));
	m_startEdit = new QLineEdit(m_body);
	m_startEdit->setObjectName("demandStartEdit");
	m_startEdit->setAccessibleName("Demand slice start");
	m_startEdit->setPlaceholderText("HH:MM:SS");
	m_startEdit->setMaximumWidth(fontMetrics().averageCharWidth() * 14);
	timeRow->addWidget(m_startEdit);
	timeRow->addWidget(new QLabel("End", m_body));
	m_endEdit = new QLineEdit(m_body);
	m_endEdit->setObjectName("demandEndEdit");
	m_endEdit->setAccessibleName("Demand slice end");
	m_endEdit->setPlaceholderText("HH:MM:SS");
	m_endEdit->setMaximumWidth(fontMetrics().averageCharWidth() * 14);
	timeRow->addWidget(m_endEdit);
	timeRow->addStretch();
	layout->addLayout(timeRow);

	m_matrix = new QTableWidget(m_body);
	m_matrix->setObjectName("demandMatrixTable");
	m_matrix->setAccessibleName("Demand matrix, origins in rows and destinations in columns");
	m_matrix->setMinimumHeight(fontMetrics().lineSpacing() * 8);
	m_matrix->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	m_matrix->setSelectionMode(QAbstractItemView::SingleSelection);
	m_matrix->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_matrix->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_countDelegate = new CountDelegate(this);
	m_matrix->setItemDelegate(m_countDelegate);
	layout->addWidget(m_matrix, 1);
	m_totalLabel = new QLabel(m_body);
	m_totalLabel->setObjectName("demandTotalLabel");
	m_totalLabel->setAccessibleName("Slice total");
	layout->addWidget(m_totalLabel);

	auto* csvRow = new QHBoxLayout();
	m_importButton = new QPushButton("Import CSV...", m_body);
	m_importButton->setObjectName("demandImportButton");
	m_importButton->setAccessibleName("Import demand CSV");
	m_importButton->setToolTip("Replace the passengers of the selected slice by the matrix in a CSV file.");
	csvRow->addWidget(m_importButton);
	m_exportButton = new QPushButton("Export CSV...", m_body);
	m_exportButton->setObjectName("demandExportButton");
	m_exportButton->setAccessibleName("Export demand CSV");
	m_exportButton->setToolTip("Write the matrix of the selected slice to a CSV file.");
	csvRow->addWidget(m_exportButton);
	m_importDayButton = new QPushButton("Import day...", m_body);
	m_importDayButton->setObjectName("demandImportDayButton");
	m_importDayButton->setAccessibleName("Import demand day");
	m_importDayButton->setToolTip("Replace all demand slices of the case from a long-format CSV table.");
	csvRow->addWidget(m_importDayButton);
	m_exportDayButton = new QPushButton("Export day...", m_body);
	m_exportDayButton->setObjectName("demandExportDayButton");
	m_exportDayButton->setAccessibleName("Export demand day");
	m_exportDayButton->setToolTip("Write all demand slices of the case to a long-format CSV table.");
	csvRow->addWidget(m_exportDayButton);
	csvRow->addStretch();
	layout->addLayout(csvRow);

	connect(m_timeline, &DemandTimeline::sliceClicked, this, &PassengerDemandPanel::selectSlice);
	connect(m_timeline, &DemandTimeline::sliceDragged, this, [this](double start, double end) { addSlice(start, end); });
	connect(m_timeline, &DemandTimeline::edgeDragged, this, [this](const QString& id, bool startEdge, double seconds) {
		if (id == m_selectedId)
			moveEdge(startEdge, seconds, DemandRanges::SnapSeconds);
	});
	connect(m_timeline, &DemandTimeline::deleteRequested, this, [this] { deleteSelectedSlice(); });
	connect(m_sliceCombo, &QComboBox::activated, this, [this](int index) { selectSlice(m_sliceCombo->itemData(index).toString()); });
	connect(m_addButton, &QPushButton::clicked, this, [this] { addSliceInFreeSpace(); });
	connect(m_deleteButton, &QPushButton::clicked, this, [this] { deleteSelectedSlice(); });
	connect(m_startEdit, &QLineEdit::editingFinished, this, [this] { commitTime(true); });
	connect(m_endEdit, &QLineEdit::editingFinished, this, [this] { commitTime(false); });
	connect(m_matrix, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
		if (!m_refreshing)
			commitCell(item);
	});
	connect(m_importButton, &QPushButton::clicked, this, [this] { importCsv(); });
	connect(m_exportButton, &QPushButton::clicked, this, [this] { exportCsv(); });
	connect(m_importDayButton, &QPushButton::clicked, this, [this] { importDay(); });
	connect(m_exportDayButton, &QPushButton::clicked, this, [this] { exportDay(); });
	PassengerDemandPanel::refresh();
}

void PassengerDemandPanel::setDiagnostics(const std::vector<SceneDiagnostic>* diagnostics) {
	m_diagnostics = diagnostics;
}

void PassengerDemandPanel::setCsvFileChooser(std::function<QString(bool, const QString&)> chooser) {
	m_csvFileChooser = std::move(chooser);
}

bool PassengerDemandPanel::canEditDemand() const {
	return canEdit() && !m_document->model().stations.empty();
}

void PassengerDemandPanel::documentChanged(SceneDocument::ChangeKinds kinds) {
	// The horizon comes from the case settings and the matrix from the stations. The panel edits only the demand, so these
	// are edits of other panels.
	if (kinds & (SceneDocument::CaseSettings | SceneDocument::Infrastructure))
		takePendingText();
	if (kinds & (SceneDocument::CaseSettings | SceneDocument::Infrastructure | SceneDocument::Passengers))
		refresh();
}

int PassengerDemandPanel::selectedIndex() const {
	if (!hasScene() || m_selectedId.isEmpty())
		return -1;
	const auto& slices = m_document->model().passengerDemand;
	for (std::size_t index = 0; index < slices.size(); ++index) {
		if (QString::fromStdString(slices[index].id) == m_selectedId)
			return static_cast<int>(index);
	}
	return -1;
}

const ScenePassengerDemandSlice* PassengerDemandPanel::selectedSlice() const {
	const int index = selectedIndex();
	return index < 0 ? nullptr : &m_document->model().passengerDemand[static_cast<std::size_t>(index)];
}

std::vector<DemandRanges::Bar> PassengerDemandPanel::modelBars() const {
	std::vector<DemandRanges::Bar> bars;
	if (!hasScene())
		return bars;
	for (const ScenePassengerDemandSlice& slice : m_document->model().passengerDemand)
		bars.push_back({QString::fromStdString(slice.id), slice.startSeconds, slice.endSeconds});
	return bars;
}

std::pair<double, double> PassengerDemandPanel::horizon() const {
	if (!hasScene())
		return {0.0, DemandRanges::DayEnd};
	const double start = sceneBaseTimeSeconds(m_document->model().baseTime).value_or(0);
	const double duration = occurrenceDuration();
	const double end = std::min(DemandRanges::DayEnd, start + duration);
	if (duration <= 0.0 || end <= start)
		return {0.0, DemandRanges::DayEnd};
	return {start, end};
}

void PassengerDemandPanel::refresh() {
	const bool loaded = hasScene();
	const bool hasStations = loaded && !m_document->model().stations.empty();
	const bool enabled = canEdit() && hasStations;
	const QSignalBlocker comboBlocker(m_sliceCombo);
	const bool wasRefreshing = m_refreshing;
	m_refreshing = true;

	m_body->setEnabled(enabled);
	const std::vector<DemandRanges::Bar> bars = modelBars();
	if (selectedIndex() < 0)
		m_selectedId = bars.empty() ? QString() : bars.front().id;
	const ScenePassengerDemandSlice* slice = selectedSlice();

	const auto [horizonStart, horizonEnd] = horizon();
	m_horizonLabel->setText(QStringLiteral("Case horizon %1 to %2")
			.arg(DemandRanges::shortClockText(horizonStart), DemandRanges::shortClockText(horizonEnd)));
	m_timeline->setHorizon(horizonStart, horizonEnd);
	m_timeline->setBars(bars);
	m_timeline->setSelectedId(m_selectedId);
	m_timeline->setEditable(enabled);

	m_sliceCombo->clear();
	for (const DemandRanges::Bar& bar : bars) {
		QString text = QStringLiteral("%1  %2-%3")
						   .arg(bar.id, DemandRanges::shortClockText(bar.startSeconds), DemandRanges::shortClockText(bar.endSeconds));
		if (bar.endSeconds <= horizonStart || bar.startSeconds >= horizonEnd)
			text += QStringLiteral(" (outside the horizon)");
		m_sliceCombo->addItem(text, bar.id);
	}
	if (bars.empty())
		m_sliceCombo->addItem("No demand slices", QString());
	m_sliceCombo->setCurrentIndex(std::max(0, m_sliceCombo->findData(m_selectedId)));

	m_startEdit->setText(slice ? DemandRanges::clockText(slice->startSeconds) : QString());
	m_endEdit->setText(slice ? DemandRanges::clockText(slice->endSeconds) : QString());
	m_startEdit->setEnabled(enabled && slice);
	m_endEdit->setEnabled(enabled && slice);
	m_sliceCombo->setEnabled(enabled && !bars.empty());
	m_addButton->setEnabled(enabled);
	m_deleteButton->setEnabled(enabled && slice);
	m_importButton->setEnabled(enabled && slice);
	m_exportButton->setEnabled(enabled && slice);
	m_importDayButton->setEnabled(enabled);
	m_exportDayButton->setEnabled(enabled && !bars.empty());

	fillMatrix(slice);
	updateDiagnostics(slice);
	m_refreshing = wasRefreshing;
}

void PassengerDemandPanel::fillMatrix(const ScenePassengerDemandSlice* slice) {
	const std::vector<SceneStation> noStations;
	const std::vector<SceneStation>& stations = hasScene() ? m_document->model().stations : noStations;
	const int count = static_cast<int>(stations.size());
	const bool editable = slice && canEditDemand();
	QStringList labels;
	for (const SceneStation& station : stations)
		labels << stationLabel(station);
	labels << "Total";
	const QRgb hatchColor = palette().color(QPalette::Mid).rgba();
	const QRgb totalColor = palette().color(QPalette::AlternateBase).rgba();
	// What decides the structure of the table. While it is the same as at the last fill, only texts can differ.
	const bool sameLayout = m_filledLayoutValid && m_filledLabels == labels && m_filledEditable == editable
		&& m_filledHatchColor == hatchColor && m_filledTotalColor == totalColor && m_matrix->rowCount() == count + 1
		&& m_matrix->columnCount() == count + 1;
	// A header that sizes its sections to the contents measures all of them again at each change of an item while the
	// table is on show. The headers therefore keep their sizes from the first change of a fill and measure once at its end.
	bool headersHeld = false;
	const auto holdHeaders = [this, &headersHeld] {
		if (headersHeld)
			return;
		headersHeld = true;
		m_matrix->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
		m_matrix->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
	};
	if (!sameLayout) {
		holdHeaders();
		m_matrix->setRowCount(count + 1);
		m_matrix->setColumnCount(count + 1);
		m_matrix->setHorizontalHeaderLabels(labels);
		m_matrix->setVerticalHeaderLabels(labels);
	}

	std::vector<long long> counts(static_cast<std::size_t>(count) * static_cast<std::size_t>(count), 0);
	if (slice) {
		// The last station with an id wins, as when the stations are searched in order.
		std::unordered_map<std::string_view, int> indexOfStation;
		indexOfStation.reserve(stations.size());
		for (int index = 0; index < count; ++index)
			indexOfStation[stations[static_cast<std::size_t>(index)].id] = index;
		const auto indexOf = [&indexOfStation](const std::string& id) {
			const auto found = indexOfStation.find(id);
			return found == indexOfStation.end() ? -1 : found->second;
		};
		for (const ScenePassengerDemandCell& cell : slice->cells) {
			const int row = indexOf(cell.originStationId);
			const int column = indexOf(cell.destinationStationId);
			if (row >= 0 && column >= 0 && row != column)
				counts[static_cast<std::size_t>(row * count + column)] += cell.count;
		}
	}
	std::vector<long long> columnTotals(static_cast<std::size_t>(count), 0);
	long long grandTotal = 0;
	for (int row = 0; row <= count; ++row) {
		long long rowTotal = 0;
		for (int column = 0; column <= count; ++column) {
			QTableWidgetItem* item = m_matrix->item(row, column);
			// A new item has none of the attributes yet, so it is set up in full like every item of a changed layout.
			const bool setUp = !sameLayout || !item;
			if (!item) {
				item = new QTableWidgetItem();
				m_matrix->setItem(row, column, item);
			}
			if (setUp)
				item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
			if (row < count && column < count && row != column) {
				const long long value = counts[static_cast<std::size_t>(row * count + column)];
				rowTotal += value;
				columnTotals[static_cast<std::size_t>(column)] += value;
				const QString text = value > 0 ? QString::number(value) : QString();
				if (item->text() != text) {
					holdHeaders();
					item->setText(text);
				}
				if (!setUp)
					continue;
				item->setFlags(editable ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable) : Qt::NoItemFlags);
				item->setBackground(QBrush());
				item->setData(Qt::FontRole, QVariant());
				const QString description = QStringLiteral("%1 to %2")
												.arg(stationLabel(stations[static_cast<std::size_t>(row)]),
													stationLabel(stations[static_cast<std::size_t>(column)]));
				item->setToolTip(description);
				item->setData(Qt::AccessibleDescriptionRole, description);
			} else if (row < count && column < count) {
				if (!setUp)
					continue;
				item->setText(QString());
				item->setFlags(Qt::NoItemFlags);
				item->setData(Qt::FontRole, QVariant());
				// Hatched, so that it differs from an empty cell in every style and colour scheme.
				item->setBackground(QBrush(palette().color(QPalette::Mid), Qt::BDiagPattern));
				item->setToolTip(QStringLiteral("An origin and a destination must differ."));
				item->setData(Qt::AccessibleDescriptionRole, QStringLiteral("Same station, not used"));
			} else {
				long long value = 0;
				if (column == count && row < count) {
					value = rowTotal;
					grandTotal += rowTotal;
				} else if (row == count && column < count) {
					value = columnTotals[static_cast<std::size_t>(column)];
				} else {
					value = grandTotal;
				}
				const QString text = QString::number(value);
				if (item->text() != text) {
					holdHeaders();
					item->setText(text);
				}
				if (!setUp)
					continue;
				QString description;
				if (column == count && row < count)
					description = QStringLiteral("Total from %1").arg(stationLabel(stations[static_cast<std::size_t>(row)]));
				else if (row == count && column < count)
					description = QStringLiteral("Total to %1").arg(stationLabel(stations[static_cast<std::size_t>(column)]));
				else
					description = QStringLiteral("Slice total");
				item->setFlags(Qt::ItemIsEnabled);
				item->setBackground(palette().color(QPalette::AlternateBase));
				QFont font = item->font();
				font.setBold(true);
				item->setFont(font);
				item->setToolTip(description);
				item->setData(Qt::AccessibleDescriptionRole, description);
			}
		}
	}
	if (headersHeld) {
		m_matrix->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
		m_matrix->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	}
	m_filledLayoutValid = true;
	m_filledLabels = labels;
	m_filledEditable = editable;
	m_filledHatchColor = hatchColor;
	m_filledTotalColor = totalColor;
	m_totalLabel->setText(slice ? QStringLiteral("Slice total: %1 passengers").arg(grandTotal) : QStringLiteral("No slice selected"));
}

void PassengerDemandPanel::updateDiagnostics(const ScenePassengerDemandSlice* slice) {
	if (!m_notice.isEmpty()) {
		DialogLayout::setStatus(m_diagnosticLabel, DialogLayout::Severity::Error, m_notice);
		m_diagnosticLabel->setToolTip(QString());
		m_notice.clear();
		return;
	}
	if (!hasScene()) {
		DialogLayout::setHelp(m_diagnosticLabel, QStringLiteral("Open a case to edit its passenger demand."));
		return;
	}
	if (m_document->model().stations.empty()) {
		DialogLayout::setHelp(m_diagnosticLabel, QStringLiteral("This case has no stations, so it has no passenger demand to edit."));
		return;
	}
	DiagnosticLines lines;
	if (m_diagnostics) {
		for (const SceneDiagnostic& diagnostic : *m_diagnostics) {
			const bool ofSlice = diagnostic.itemType == "demand_slice"
				&& (diagnostic.itemId.empty() || (slice && diagnostic.itemId == slice->id));
			if (!ofSlice)
				continue;
			lines.add(diagnostic.severity, QString::fromStdString(diagnostic.message));
		}
	}
	if (lines.isEmpty()) {
		DialogLayout::setHelp(m_diagnosticLabel,
			slice ? QStringLiteral("An empty cell or 0 means no passengers.")
				  : QStringLiteral("Drag on the timeline or use Add slice to add a demand slice."));
		m_diagnosticLabel->setToolTip(QString());
	} else if (!lines.errors.isEmpty()) {
		DialogLayout::setStatus(m_diagnosticLabel, DialogLayout::Severity::Error, DiagnosticLines::capped(lines.errors));
		m_diagnosticLabel->setToolTip(lines.errors.join(QLatin1Char('\n')));
	} else {
		DialogLayout::setStatus(m_diagnosticLabel,
			lines.hasWarning ? DialogLayout::Severity::Warning : DialogLayout::Severity::Neutral, DiagnosticLines::capped(lines.notes));
		m_diagnosticLabel->setToolTip(lines.notes.join(QLatin1Char('\n')));
	}
}

void PassengerDemandPanel::showNotice(const QString& text) {
	m_notice = text;
	refresh();
}

bool PassengerDemandPanel::editSelected(
	const QString& label, const std::function<void(ScenePassengerDemandSlice&)>& mutate) {
	if (!canEditDemand() || selectedIndex() < 0)
		return false;
	const std::string id = m_selectedId.toStdString();
	if (!m_document->edit(SceneDocument::Passengers, label, [&id, &mutate](SceneModel& model) {
			for (ScenePassengerDemandSlice& slice : model.passengerDemand) {
				if (slice.id == id) {
					mutate(slice);
					return;
				}
			}
		}))
		return false;
	Q_EMIT changed();
	return true;
}

bool PassengerDemandPanel::selectSlice(const QString& id) {
	if (!hasScene())
		return false;
	const auto& slices = m_document->model().passengerDemand;
	const std::string wanted = id.toStdString();
	if (std::none_of(slices.begin(), slices.end(), [&wanted](const ScenePassengerDemandSlice& slice) { return slice.id == wanted; }))
		return false;
	m_selectedId = id;
	refresh();
	return true;
}

bool PassengerDemandPanel::addSlice(double startSeconds, double endSeconds) {
	if (!canEditDemand())
		return false;
	const auto range = DemandRanges::newRange(modelBars(), startSeconds, endSeconds);
	if (!range) {
		showNotice(QStringLiteral("There is no room for a slice there: slices may not overlap and must last a minute or more."));
		return false;
	}
	std::set<std::string> used;
	for (const ScenePassengerDemandSlice& slice : m_document->model().passengerDemand)
		used.insert(slice.id);
	int number = 1;
	while (used.count("slice-" + std::to_string(number)))
		++number;
	ScenePassengerDemandSlice added;
	added.id = "slice-" + std::to_string(number);
	added.startSeconds = range->first;
	added.endSeconds = range->second;
	// Selected before the edit, so that the refresh that the edit causes shows the new slice.
	const QString previousId = m_selectedId;
	m_selectedId = QString::fromStdString(added.id);
	if (!m_document->edit(SceneDocument::Passengers, "Add demand slice",
			[&added](SceneModel& model) { model.passengerDemand.push_back(added); })) {
		m_selectedId = previousId;
		refresh();
		return false;
	}
	Q_EMIT changed();
	return true;
}

bool PassengerDemandPanel::addSliceInFreeSpace() {
	const auto [from, to] = horizon();
	const auto range = DemandRanges::firstFreeRange(modelBars(), from, to);
	if (!range) {
		showNotice(QStringLiteral("The day has no free minute left for another slice."));
		return false;
	}
	return addSlice(range->first, range->second);
}

bool PassengerDemandPanel::deleteSelectedSlice() {
	if (!canEditDemand() || selectedIndex() < 0)
		return false;
	const std::string id = m_selectedId.toStdString();
	const QString name = m_selectedId;
	m_selectedId.clear();
	if (!m_document->edit(SceneDocument::Passengers, "Delete demand slice", [&id](SceneModel& model) {
			auto& slices = model.passengerDemand;
			slices.erase(std::remove_if(slices.begin(), slices.end(),
							 [&id](const ScenePassengerDemandSlice& slice) { return slice.id == id; }),
				slices.end());
		})) {
		m_selectedId = name;
		refresh();
		return false;
	}
	reportDeleted(QStringLiteral("demand slice '%1'").arg(name));
	Q_EMIT changed();
	return true;
}

bool PassengerDemandPanel::moveEdge(bool startEdge, double seconds, double minimumLength) {
	const ScenePassengerDemandSlice* slice = selectedSlice();
	if (!slice || !canEditDemand())
		return false;
	const auto period = DemandRanges::movedEdge(modelBars(), m_selectedId, startEdge, seconds, minimumLength);
	const double reached = startEdge ? period.first : period.second;
	const bool clamped = std::abs(reached - seconds) > 0.5;
	if (period.first == slice->startSeconds && period.second == slice->endSeconds) {
		if (clamped)
			Q_EMIT statusMessage(QStringLiteral("The period stays within the neighbouring slices and the day."), 6000);
		refresh();
		return false;
	}
	const bool edited = editSelected("Edit demand period", [period](ScenePassengerDemandSlice& target) {
		target.startSeconds = period.first;
		target.endSeconds = period.second;
	});
	if (edited && clamped)
		Q_EMIT statusMessage(QStringLiteral("The period was limited to the neighbouring slices and the day."), 6000);
	return edited;
}

void PassengerDemandPanel::commitTime(bool startEdge) {
	if (m_refreshing || !selectedSlice())
		return;
	const QLineEdit* field = startEdge ? m_startEdit : m_endEdit;
	const auto seconds = DemandRanges::parseClock(field->text());
	if (!seconds) {
		showNotice(QStringLiteral("Enter the time as HH:MM:SS, from 00:00:00 to 24:00:00."));
		return;
	}
	moveEdge(startEdge, *seconds, DemandRanges::SnapSeconds);
}

void PassengerDemandPanel::commitCell(QTableWidgetItem* item) {
	const ScenePassengerDemandSlice* slice = selectedSlice();
	const auto& stations = m_document->model().stations;
	const int row = item->row();
	const int column = item->column();
	const int count = static_cast<int>(stations.size());
	if (!slice || row >= count || column >= count || row == column) {
		refresh();
		return;
	}
	const QString text = item->text().trimmed();
	bool ok = true;
	const long long value = text.isEmpty() ? 0 : text.toLongLong(&ok);
	if (!ok || value < 0) {
		showNotice(QStringLiteral("Enter a whole number of passengers, 0 or more."));
		return;
	}
	const std::string origin = stations[static_cast<std::size_t>(row)].id;
	const std::string destination = stations[static_cast<std::size_t>(column)].id;
	// The matrix shows the sum of the cells of a pair, so that sum is the count that is being replaced.
	long long previous = 0;
	int matching = 0;
	for (const ScenePassengerDemandCell& cell : slice->cells) {
		if (cell.originStationId == origin && cell.destinationStationId == destination) {
			previous += cell.count;
			++matching;
		}
	}
	if (value == previous && matching <= 1) {
		refresh();
		return;
	}
	long long all = 0;
	for (const ScenePassengerDemandSlice& other : m_document->model().passengerDemand)
		all += totalOf(other);
	// A scene that is already above the limit can still be edited downwards.
	if (value > kMaxScenePassengerDemand || (value > previous && all - previous + value > kMaxScenePassengerDemand)) {
		showNotice(QStringLiteral("The slices may hold %1 passengers together.").arg(kMaxScenePassengerDemand));
		return;
	}
	const int newCount = static_cast<int>(value);
	const bool edited = editSelected("Edit demand", [&origin, &destination, newCount](ScenePassengerDemandSlice& target) {
		auto& cells = target.cells;
		const auto isPair = [&](const ScenePassengerDemandCell& cell) {
			return cell.originStationId == origin && cell.destinationStationId == destination;
		};
		const auto found = std::find_if(cells.begin(), cells.end(), isPair);
		// One cell with the new count stands where the first cell of the pair was, and the others are removed.
		const auto position = found - cells.begin();
		cells.erase(std::remove_if(cells.begin(), cells.end(), isPair), cells.end());
		if (newCount > 0)
			cells.insert(cells.begin() + position, ScenePassengerDemandCell{origin, destination, newCount});
	});
	if (!edited)
		refresh();
}

void PassengerDemandPanel::commitPendingEditors() {
	// A matrix cell that is open for typing is committed through its delegate, as when the user leaves it.
	for (QLineEdit* editor : m_matrix->viewport()->findChildren<QLineEdit*>(QString(), Qt::FindDirectChildrenOnly)) {
		Q_EMIT m_countDelegate->commitData(editor);
		Q_EMIT m_countDelegate->closeEditor(editor, QAbstractItemDelegate::NoHint);
	}
	if (m_startEdit->isModified())
		commitTime(true);
	if (m_endEdit->isModified())
		commitTime(false);
}

bool PassengerDemandPanel::importCsvText(const QString& text) {
	const ScenePassengerDemandSlice* slice = selectedSlice();
	if (!slice || !canEditDemand())
		return false;
	const PassengerDemandCsv csv = readPassengerDemandCsv(text.toStdString(), m_document->model().stations);
	if (!csv.ok()) {
		QStringList lines;
		for (const std::string& error : csv.errors)
			lines << QString::fromStdString(error);
		showNotice(QStringLiteral("The CSV was not imported.\n") + DiagnosticLines::capped(lines));
		return false;
	}
	long long all = 0;
	for (const ScenePassengerDemandSlice& other : m_document->model().passengerDemand)
		all += totalOf(other);
	long long imported = 0;
	for (const ScenePassengerDemandCell& cell : csv.cells)
		imported += cell.count;
	if (imported > totalOf(*slice) && all - totalOf(*slice) + imported > kMaxScenePassengerDemand) {
		showNotice(QStringLiteral("The CSV was not imported: the slices may hold %1 passengers together.")
				.arg(kMaxScenePassengerDemand));
		return false;
	}
	const std::vector<ScenePassengerDemandCell> cells = csv.cells;
	if (!editSelected("Import demand CSV", [&cells](ScenePassengerDemandSlice& target) { target.cells = cells; }))
		return false;
	QString message = QStringLiteral("Imported %1 passengers into the demand slice.").arg(imported);
	const double sliceSeconds = slice->endSeconds - slice->startSeconds;
	if (csv.periodSeconds > 0.0 && std::abs(csv.periodSeconds - sliceSeconds) > 0.5)
		message += QStringLiteral(" The file is for a period of %1 and the slice lasts %2; the counts were not scaled.")
					   .arg(DemandRanges::clockText(csv.periodSeconds), DemandRanges::clockText(sliceSeconds));
	Q_EMIT statusMessage(message, 6000);
	return true;
}

bool PassengerDemandPanel::importCsvFile(const QString& path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		showNotice(QStringLiteral("Cannot read %1: %2").arg(QDir::toNativeSeparators(path), file.errorString()));
		return false;
	}
	return importCsvText(QString::fromUtf8(file.readAll()));
}

QString PassengerDemandPanel::csvText() const {
	const ScenePassengerDemandSlice* slice = selectedSlice();
	if (!slice)
		return {};
	return QString::fromStdString(writePassengerDemandCsv(*slice, m_document->model().stations));
}

bool PassengerDemandPanel::exportCsvFile(const QString& path) {
	if (!selectedSlice()) {
		showNotice(QStringLiteral("Select a demand slice to export."));
		return false;
	}
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(csvText().toUtf8()) < 0 || !file.commit()) {
		showNotice(QStringLiteral("Cannot write %1: %2").arg(QDir::toNativeSeparators(path), file.errorString()));
		return false;
	}
	Q_EMIT statusMessage(QStringLiteral("Wrote demand slice %1 to %2").arg(m_selectedId, QDir::toNativeSeparators(path)), 6000);
	return true;
}

QString PassengerDemandPanel::chooseCsvFile(bool save, const QString& suggestedName) {
	if (!m_csvFileChooser)
		return {};
	const QString proposed = save ? (suggestedName.isEmpty() ? m_selectedId + QStringLiteral(".csv") : suggestedName) : QString();
	return m_csvFileChooser(save, proposed);
}

void PassengerDemandPanel::importCsv() {
	if (const QString path = chooseCsvFile(false); !path.isEmpty())
		importCsvFile(path);
}

void PassengerDemandPanel::exportCsv() {
	if (const QString path = chooseCsvFile(true); !path.isEmpty())
		exportCsvFile(path);
}

bool PassengerDemandPanel::importDayText(const QString& text) {
	if (!canEditDemand())
		return false;
	const PassengerDemandTable table = readPassengerDemandTable(text.toStdString(), m_document->model().stations);
	if (!table.ok()) {
		QStringList lines;
		for (const std::string& error : table.errors)
			lines << QString::fromStdString(error);
		showNotice(QStringLiteral("The demand table was not imported.\n") + DiagnosticLines::capped(lines));
		return false;
	}
	const DemandDayPreview preview = buildDemandDayPreview(table);
	if (!showDayImportPreviewDialog(this, preview))
		return false;
	if (!canEditDemand())
		return false;
	const QString previousId = m_selectedId;
	m_selectedId = table.slices.empty() ? QString() : QString::fromStdString(table.slices.front().id);
	const std::vector<ScenePassengerDemandSlice> imported = table.slices;
	if (!m_document->edit(SceneDocument::Passengers, "Import demand day",
			[&imported](SceneModel& model) { model.passengerDemand = imported; })) {
		m_selectedId = previousId;
		refresh();
		return false;
	}
	Q_EMIT changed();
	QString message = QStringLiteral("Imported %1 across %2.")
						  .arg(countLabel(table.totalPassengers, "passenger", "passengers"),
							  countLabel(static_cast<long long>(table.slices.size()), "demand slice", "demand slices"));
	if (!table.unknownStations.empty()) {
		message += QStringLiteral(" Skipped %1.")
					   .arg(countLabel(static_cast<long long>(table.unknownStations.size()), "unknown station", "unknown stations"));
	}
	Q_EMIT statusMessage(message, 6000);
	return true;
}

bool PassengerDemandPanel::importDayFile(const QString& path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		showNotice(QStringLiteral("Cannot read %1: %2").arg(QDir::toNativeSeparators(path), file.errorString()));
		return false;
	}
	return importDayText(QString::fromUtf8(file.readAll()));
}

QString PassengerDemandPanel::dayTableText() const {
	if (!hasScene())
		return {};
	return QString::fromStdString(
		writePassengerDemandTable(m_document->model().passengerDemand, m_document->model().stations));
}

bool PassengerDemandPanel::exportDayFile(const QString& path) {
	if (!hasScene() || m_document->model().passengerDemand.empty()) {
		showNotice(QStringLiteral("There are no demand slices to export."));
		return false;
	}
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(dayTableText().toUtf8()) < 0 || !file.commit()) {
		showNotice(QStringLiteral("Cannot write %1: %2").arg(QDir::toNativeSeparators(path), file.errorString()));
		return false;
	}
	Q_EMIT statusMessage(
		QStringLiteral("Wrote %1 to %2")
			.arg(countLabel(static_cast<long long>(m_document->model().passengerDemand.size()), "demand slice", "demand slices"),
				QDir::toNativeSeparators(path)),
		6000);
	return true;
}

void PassengerDemandPanel::importDay() {
	if (const QString path = chooseCsvFile(false); !path.isEmpty())
		importDayFile(path);
}

void PassengerDemandPanel::exportDay() {
	if (const QString path = chooseCsvFile(true, QStringLiteral("demand.csv")); !path.isEmpty())
		exportDayFile(path);
}
