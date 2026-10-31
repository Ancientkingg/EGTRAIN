#include "editors/TimetableStopsPanel.h"

#include "editors/StopEditorDialog.h"
#include "scene/SceneWriter.h"
#include "scene/SectionInventory.h"
#include "scene/StopInsertion.h"
#include "util/CsvWriter.h"
#include "util/TimeFormat.h"
#include "widgets/DialogLayout.h"
#include "widgets/EditorLayout.h"

#include <QAbstractItemDelegate>
#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>

namespace {

// The number of digits that a platform, a dwell or a planned time is assumed to need at most, and the number that the
// station column keeps at least. Longer values are elided.
constexpr int kTypicalValueChars = 10;
constexpr int kMinimumStationChars = 6;

// What the table shows for a platform or a planned time that a stop does not have.
QString noValueText() {
	return QStringLiteral("-");
}

// The position of the first service with this id in the model, or -1.
int serviceIndexOf(const SceneModel& model, const std::string& serviceId) {
	const auto& services = model.services;
	const auto found = std::find_if(services.begin(), services.end(), [&serviceId](const SceneService& service) { return service.id == serviceId; });
	return found == services.end() ? -1 : static_cast<int>(found - services.begin());
}

// Whether two services are the same in all that the scene saves of them.
bool sameService(const SceneService& first, const SceneService& second) {
	SceneModel one;
	one.services = {first};
	SceneModel other;
	other.services = {second};
	return encodeServices(one) == encodeServices(other);
}

} // namespace

TimetableStopsPanel::TimetableStopsPanel(QWidget* parent) : SceneEditorPanel(parent) {
	setObjectName("timetableStopsPanel");
	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	// In a narrow dock the time mode goes below its label.
	auto* timeModeLayout = new QFormLayout();
	timeModeLayout->setRowWrapPolicy(QFormLayout::WrapLongRows);
	m_timeModeCombo = new QComboBox(this);
	m_timeModeCombo->setObjectName("timetableTimeModeCombo");
	m_timeModeCombo->addItem("Elapsed offsets (s)", false);
	m_timeModeCombo->addItem("Clock time", true);
	timeModeLayout->addRow("Planned time display", m_timeModeCombo);
	m_timeBaseLabel = new QLabel(this);
	m_timeBaseLabel->setObjectName("timetableTimeBaseLabel");
	m_timeBaseLabel->setWordWrap(true);
	layout->addLayout(timeModeLayout);
	layout->addWidget(m_timeBaseLabel);
	m_emptyLabel = EditorLayout::emptyStateLabel(this);
	m_emptyLabel->setObjectName("timetableEmptyStateLabel");
	layout->addWidget(m_emptyLabel);
	m_table = new QTableWidget(this);
	m_table->setObjectName("timetableStopTable");
	m_table->setColumnCount(5);
	m_table->setAccessibleName("Timetable stops");
	// The headers have two lines, so that the five columns fit the width of the dock.
	m_table->setHorizontalHeaderLabels({"Station", "Stop\nplatform", "Minimum\ndwell (s)", "Planned\narrival", "Planned\ndeparture"});
	// A narrow column cuts its header, so each header carries its whole text as a tooltip.
	for (int column = 0; column < m_table->columnCount(); ++column) {
		QTableWidgetItem* header = m_table->horizontalHeaderItem(column);
		header->setToolTip(header->text().replace(QLatin1Char('\n'), QLatin1Char(' ')));
	}
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::SingleSelection);
	m_table->setAlternatingRowColors(true);
	m_table->setTabKeyNavigation(false);
	m_table->setWordWrap(false);
	m_table->setTextElideMode(Qt::ElideRight);
	m_table->verticalHeader()->setVisible(false);
	// fitColumns sets the widths, so that the station takes the width that the other columns leave.
	m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
	m_table->viewport()->installEventFilter(this);
	m_table->horizontalHeader()->installEventFilter(this);
	layout->addWidget(m_table);
	QGridLayout* buttonLayout = new QGridLayout();
	m_addButton = new QPushButton("Add Stop", this);
	m_addButton->setObjectName("addTimetableStopButton");
	m_insertAfterButton = new QPushButton("Insert After Selected", this);
	m_insertAfterButton->setObjectName("insertTimetableStopAfterButton");
	m_removeButton = new QPushButton("Remove Stop", this);
	m_removeButton->setObjectName("removeTimetableStopButton");
	m_moveUpButton = new QPushButton("Move Up", this);
	m_moveUpButton->setObjectName("moveTimetableStopUpButton");
	m_moveDownButton = new QPushButton("Move Down", this);
	m_moveDownButton->setObjectName("moveTimetableStopDownButton");
	// Two columns, so that the buttons fit a narrow dock.
	buttonLayout->addWidget(m_addButton, 0, 0);
	buttonLayout->addWidget(m_removeButton, 0, 1);
	buttonLayout->addWidget(m_insertAfterButton, 1, 0, 1, 2);
	buttonLayout->addWidget(m_moveUpButton, 2, 0);
	buttonLayout->addWidget(m_moveDownButton, 2, 1);
	QWidget::setTabOrder(m_addButton, m_removeButton);
	layout->addLayout(buttonLayout);

	connect(m_table, &QTableWidget::cellClicked, this, [this](int row, int) { editStop(row); });
	for (const auto key : {Qt::Key_Return, Qt::Key_Enter}) {
		auto* shortcut = new QShortcut(QKeySequence(key), m_table);
		shortcut->setContext(Qt::WidgetShortcut);
		connect(shortcut, &QShortcut::activated, this, [this]() {
			if (m_table->currentRow() >= 0)
				editStop(m_table->currentRow());
		});
	}
	connect(m_table, &QTableWidget::currentCellChanged, this, &TimetableStopsPanel::updateActions);
	connect(m_addButton, &QPushButton::clicked, this, &TimetableStopsPanel::addStop);
	connect(m_insertAfterButton, &QPushButton::clicked, this, &TimetableStopsPanel::insertStopAfterSelected);
	connect(m_removeButton, &QPushButton::clicked, this, &TimetableStopsPanel::removeStop);
	connect(m_moveUpButton, &QPushButton::clicked, this, &TimetableStopsPanel::moveStopUp);
	connect(m_moveDownButton, &QPushButton::clicked, this, &TimetableStopsPanel::moveStopDown);
	connect(m_timeModeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
		if (index < 0)
			return;
		m_clockMode = m_timeModeCombo->itemData(index).toBool();
		refresh();
	});
	TimetableStopsPanel::refresh();
}

void TimetableStopsPanel::documentChanged(SceneDocument::ChangeKinds kinds) {
	// The base time sets the planned times, and the infrastructure decides where a stop is on its route.
	if (kinds & (SceneDocument::CaseSettings | SceneDocument::Infrastructure))
		refresh();
}

void TimetableStopsPanel::setServiceId(const QString& serviceId) {
	m_serviceId = serviceId.toStdString();
	refresh();
}

int TimetableStopsPanel::serviceRow() const {
	if (!hasScene() || m_serviceId.empty())
		return -1;
	return serviceIndexOf(m_document->model(), m_serviceId);
}

void TimetableStopsPanel::refresh() {
	const int serviceIndex = serviceRow();
	const bool hasService = serviceIndex >= 0;
	const bool editorAvailable = hasService && canEdit();
	const int previousRow = m_table->currentRow();
	const SceneModel noScene;
	const SceneModel& model = m_document ? m_document->model() : noScene;
	const long long baseOffset = TimetableStops::baseTimeToSeconds(model.baseTime);
	m_timeBaseLabel->setText(hasService ? TimetableStops::baseTimeText(model, m_clockMode) : QString());
	m_timeBaseLabel->setVisible(!m_timeBaseLabel->text().isEmpty());
	m_timeModeCombo->setEnabled(editorAvailable);
	const int stopCount = hasService ? static_cast<int>(model.services[static_cast<std::size_t>(serviceIndex)].stops.size()) : 0;
	QString emptyText;
	if (!hasScene())
		emptyText = QStringLiteral("Open a case to edit services.");
	else if (!model.services.empty() && !hasService)
		emptyText = QStringLiteral("Select a service to edit its timetable.");
	else if (!hasService)
		emptyText = canEdit() ? QStringLiteral("No services. Use Add Service.") : QStringLiteral("No services.");
	else if (stopCount == 0)
		emptyText = editorAvailable ? QStringLiteral("No stops. Use Add Stop.") : QStringLiteral("No stops.");
	DialogLayout::setHelp(m_emptyLabel, emptyText);
	{
		const QSignalBlocker blocker(m_table);
		m_table->clearContents();
		m_table->setRowCount(stopCount);
		if (hasService) {
			const SceneService& service = model.services[static_cast<std::size_t>(serviceIndex)];
			const std::vector<SceneStopResolution> resolutions = resolveSceneServiceStops(model, service, sceneServiceTraversal(model, service));
			for (int row = 0; row < static_cast<int>(service.stops.size()); ++row) {
				const SceneStop& stop = service.stops[static_cast<std::size_t>(row)];
				const QString station = TimetableStops::stationDisplayName(model, stop.stationId);
				const QString platform = stop.platformId.empty() ? noValueText() : QString::fromStdString(stop.platformId);
				const QString arrival = stop.hasPlannedArrival
					? QString::fromStdString(formatPlannedTime(stop.plannedArrivalSeconds, m_clockMode, baseOffset))
					: noValueText();
				const QString departure = stop.hasPlannedDeparture
					? QString::fromStdString(formatPlannedTime(stop.plannedDepartureSeconds, m_clockMode, baseOffset))
					: noValueText();
				const std::array<QString, 5> values = {station, platform, QString::fromStdString(csv::formatDouble(stop.dwellSeconds)), arrival, departure};
				const QString reason = row < static_cast<int>(resolutions.size())
					? TimetableStops::stopResolutionText(resolutions[static_cast<std::size_t>(row)].status)
					: QString();
				for (int column = 0; column < 5; ++column) {
					auto* item = new QTableWidgetItem(values[static_cast<std::size_t>(column)]);
					item->setData(Qt::UserRole, QString::fromStdString(stop.stationId));
					item->setData(Qt::UserRole + 1, QString::fromStdString(stop.platformId));
					item->setData(Qt::UserRole + 2, row);
					const QString text = column == 0 ? QString("Station %1 [%2]").arg(station, QString::fromStdString(stop.stationId))
													 : values[static_cast<std::size_t>(column)];
					item->setToolTip(reason.isEmpty() ? text : reason + QLatin1Char('\n') + text);
					m_table->setItem(row, column, item);
				}
			}
		}
		fitColumns();
		m_table->setEnabled(editorAvailable);
		if (m_table->rowCount() > 0)
			m_table->setCurrentCell(std::clamp(previousRow < 0 ? 0 : previousRow, 0, m_table->rowCount() - 1), 0);
		else
			m_table->clearSelection();
	}
	updateActions();
	emit timetableRefreshed();
}

bool TimetableStopsPanel::eventFilter(QObject* watched, QEvent* event) {
	if ((watched == m_table->viewport() && event->type() == QEvent::Resize) || (watched == m_table->horizontalHeader() && event->type() == QEvent::FontChange))
		fitColumns();
	return SceneEditorPanel::eventFilter(watched, event);
}

void TimetableStopsPanel::fitColumns() {
	const QHeaderView* header = m_table->horizontalHeader();
	const QFontMetrics metrics(m_table->font());
	// The space that a cell keeps around its text.
	const int margin = 2 * (m_table->style()->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, m_table) + 1);
	QStyleOptionViewItem option;
	option.initFrom(m_table);
	option.widget = m_table;
	option.font = m_table->font();
	option.fontMetrics = metrics;
	const int minimum = header->minimumSectionSize();
	const int typicalValue = metrics.horizontalAdvance(QString(kTypicalValueChars, QLatin1Char('0'))) + margin;
	const int minimumStation = std::max(minimum, metrics.horizontalAdvance(QString(kMinimumStationChars, QLatin1Char('0'))) + margin);
	std::array<int, 5> widths{};
	int wanted = 0;
	// A clock time is hard to read when it is elided, so its column is as wide as the longest time, with any day prefix,
	// and the other columns give up room first.
	const auto isClockTime = [this](int column) { return m_clockMode && column >= 3; };
	int kept = 0;
	for (int column = 1; column < 5; ++column) {
		const bool clockTime = isClockTime(column);
		int widest = 0;
		for (int row = 0; row < m_table->rowCount(); ++row) {
			// The delegate reports what its style needs to draw the text whole, which can be more than the advance of the text.
			const QModelIndex cell = m_table->model()->index(row, column);
			widest = std::max(widest, m_table->itemDelegate()->sizeHint(option, cell).width());
		}
		int& width = widths[static_cast<std::size_t>(column)];
		width = std::max(header->sectionSizeHint(column), clockTime ? widest : std::min(widest, typicalValue));
		wanted += width;
		if (clockTime)
			kept += width;
	}
	const int available = m_table->viewport()->width();
	// The station keeps its whole header where the other columns can give up the room for it and the clock times stay whole,
	// and six digits otherwise.
	std::array<int, 5> fitted{};
	for (const int station : {std::max(minimumStation, header->sectionSizeHint(0)), minimumStation}) {
		const int room = std::max(available - station, 0);
		// While the kept columns fit, only the others are shortened.
		const bool keepClockTimes = wanted > room && kept < room;
		int used = 0;
		fitted = widths;
		for (int column = 1; column < 5; ++column) {
			int& width = fitted[static_cast<std::size_t>(column)];
			if (keepClockTimes) {
				if (!isClockTime(column))
					width = std::max(width * (room - kept) / (wanted - kept), minimum);
			} else if (wanted > room) {
				width = std::max(width * room / wanted, minimum);
			}
			used += width;
		}
		fitted[0] = std::max(available - used, station);
		if (used <= room && kept < room)
			break;
	}
	for (int column = 0; column < 5; ++column)
		m_table->setColumnWidth(column, fitted[static_cast<std::size_t>(column)]);
}

void TimetableStopsPanel::updateActions() {
	const bool editorAvailable = serviceRow() >= 0 && canEdit();
	const int selectedRow = m_table->currentRow();
	const bool hasSelection = editorAvailable && selectedRow >= 0 && selectedRow < m_table->rowCount();
	m_addButton->setEnabled(editorAvailable);
	m_insertAfterButton->setEnabled(hasSelection);
	m_removeButton->setEnabled(hasSelection);
	m_moveUpButton->setEnabled(hasSelection && selectedRow > 0);
	m_moveDownButton->setEnabled(hasSelection && selectedRow + 1 < m_table->rowCount());
}

void TimetableStopsPanel::editStop(int row, int insertIndex) {
	if (!canEdit())
		return;
	// On macOS a button does not take the focus, so a field of another panel can still hold typed text when Add Stop is
	// clicked. The window takes that text now, before the copy. It can rename the service, which is looked up after it.
	emit commitPendingEditsRequested();
	const int serviceIndex = serviceRow();
	if (serviceIndex < 0)
		return;
	const SceneModel& model = m_document->model();
	// The service as the dialog opens. The dialog works on copies, and its stop enters the service only while the service
	// is still the same.
	const SceneService opened = model.services[static_cast<std::size_t>(serviceIndex)];
	const bool append = row < 0;
	if (!append && row >= static_cast<int>(opened.stops.size()))
		return;
	// The position of a new stop in the timetable, or the row of the stop being edited.
	const std::size_t stopIndex = !append ? static_cast<std::size_t>(row)
		: insertIndex < 0				  ? opened.stops.size()
										  : std::min(static_cast<std::size_t>(insertIndex), opened.stops.size());
	StopEditorDialog dialog(model, opened, stopIndex, append, m_clockMode, this);
	if (dialog.exec() != QDialog::Accepted)
		return;
	// The scene may have been replaced, closed for edits or changed while the dialog was open. The stop was checked against
	// the service as it was, so it is applied only to that service, as the first service with its id.
	if (!canEdit())
		return;
	const int liveIndex = serviceIndexOf(m_document->model(), opened.id);
	if (liveIndex < 0 || !sameService(m_document->model().services[static_cast<std::size_t>(liveIndex)], opened))
		return;
	SceneService next = m_document->model().services[static_cast<std::size_t>(liveIndex)];
	if (!append)
		next.stops[dialog.position()] = dialog.stop();
	else if (!insertSceneStop(m_document->model(), next, dialog.position(), dialog.stop()).inserted)
		return;
	if (!m_document->edit(SceneDocument::Services, append ? "Add timetable stop" : "Edit timetable stop",
			[liveIndex, &next](SceneModel& edited) { edited.services[static_cast<std::size_t>(liveIndex)] = std::move(next); }))
		return;
	refresh();
	m_table->setCurrentCell(static_cast<int>(dialog.position()), 0);
	emit changed();
}


void TimetableStopsPanel::addStop() {
	const int serviceIndex = serviceRow();
	if (!canEdit() || serviceIndex < 0)
		return;
	const SceneModel& model = m_document->model();
	const SceneService& service = model.services[static_cast<std::size_t>(serviceIndex)];
	for (std::size_t position = service.stops.size() + 1; position-- > 0;) {
		const auto window = sceneStopInsertionWindow(model, service, position);
		if (window.ok && !window.visits.visits.empty()) {
			editStop(-1, static_cast<int>(position));
			return;
		}
	}
	m_prompts->showInfo(this, "No Reachable Stop", "No station on the route is left to stop at. Check the route and the stop order.");
}

void TimetableStopsPanel::insertStopAfterSelected() {
	const int row = m_table->currentRow();
	if (row >= 0 && row < m_table->rowCount())
		editStop(-1, row + 1);
}

void TimetableStopsPanel::removeStop() {
	const int serviceIndex = serviceRow();
	const int stopRow = m_table->currentRow();
	if (!canEdit() || serviceIndex < 0)
		return;
	if (stopRow < 0 || stopRow >= static_cast<int>(m_document->model().services[static_cast<std::size_t>(serviceIndex)].stops.size()))
		return;
	if (!m_document->edit(SceneDocument::Services, "Remove timetable stop", [serviceIndex, stopRow](SceneModel& model) {
			auto& stops = model.services[static_cast<std::size_t>(serviceIndex)].stops;
			stops.erase(stops.begin() + stopRow);
		}))
		return;
	refresh();
	if (m_table->rowCount() > 0)
		m_table->setCurrentCell(std::min(stopRow, m_table->rowCount() - 1), 0);
	emit changed();
}

void TimetableStopsPanel::moveStopUp() {
	const int serviceIndex = serviceRow();
	const int stopRow = m_table->currentRow();
	if (!canEdit() || serviceIndex < 0)
		return;
	if (stopRow <= 0 || stopRow >= static_cast<int>(m_document->model().services[static_cast<std::size_t>(serviceIndex)].stops.size()))
		return;
	if (!m_document->edit(SceneDocument::Services, "Move timetable stop up", [serviceIndex, stopRow](SceneModel& model) {
			auto& stops = model.services[static_cast<std::size_t>(serviceIndex)].stops;
			std::swap(stops[static_cast<std::size_t>(stopRow)], stops[static_cast<std::size_t>(stopRow - 1)]);
		}))
		return;
	refresh();
	m_table->setCurrentCell(stopRow - 1, 0);
	emit changed();
}

void TimetableStopsPanel::moveStopDown() {
	const int serviceIndex = serviceRow();
	const int stopRow = m_table->currentRow();
	if (!canEdit() || serviceIndex < 0)
		return;
	if (stopRow < 0 || stopRow + 1 >= static_cast<int>(m_document->model().services[static_cast<std::size_t>(serviceIndex)].stops.size()))
		return;
	if (!m_document->edit(SceneDocument::Services, "Move timetable stop down", [serviceIndex, stopRow](SceneModel& model) {
			auto& stops = model.services[static_cast<std::size_t>(serviceIndex)].stops;
			std::swap(stops[static_cast<std::size_t>(stopRow)], stops[static_cast<std::size_t>(stopRow + 1)]);
		}))
		return;
	refresh();
	m_table->setCurrentCell(stopRow + 1, 0);
	emit changed();
}
