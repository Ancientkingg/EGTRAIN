#include "editors/TimetableStopsPanel.h"

#include "scene/SectionInventory.h"
#include "scene/StopInsertion.h"
#include "util/CsvWriter.h"
#include "util/TimeFormat.h"
#include "widgets/ChoiceComboBox.h"
#include "widgets/DialogLayout.h"
#include "widgets/EditorLayout.h"

#include <QAbstractItemDelegate>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace {

// The number of digits that a platform, a dwell or a planned time is assumed to need at most, and the number that the
// station column keeps at least. Longer values are elided.
constexpr int kTypicalValueChars = 10;
constexpr int kMinimumStationChars = 6;

// What the table shows for a platform or a planned time that a stop does not have: an em dash.
QString noValueText() {
	return QStringLiteral("\u2014");
}

// Seconds since midnight of an "hh:mm:ss" base time; 0 when the text is empty or not a time of day.
long long baseTimeToSeconds(const std::string& hhmmss) {
	if (hhmmss.empty())
		return 0;
	int h = 0, m = 0, s = 0;
	char extra = 0;
	if (std::sscanf(hhmmss.c_str(), "%d:%d:%d%c", &h, &m, &s, &extra) != 3)
		return 0;
	if (h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59)
		return 0;
	return h * 3600 + m * 60 + s;
}

QString stationDisplayName(const SceneModel& model, const std::string& stationId) {
	for (const auto& station : model.stations) {
		if (station.id == stationId)
			return station.name.empty()
				? QString::fromStdString(station.id)
				: QString::fromStdString(station.name);
	}
	return stationId.empty() ? QStringLiteral("(missing station)") : QString::fromStdString(stationId);
}

QString stopResolutionText(SceneStopResolutionStatus status) {
	switch (status) {
		case SceneStopResolutionStatus::Resolved: return "Reachable ordered route visit";
		case SceneStopResolutionStatus::AmbiguousPlatform: return "Choose an explicit reachable platform";
		case SceneStopResolutionStatus::OffRouteContext: return "Off-route schedule context; no simulated stop";
		case SceneStopResolutionStatus::OutOfOrder: return "Invalid: stop is before or at an already used route visit";
		case SceneStopResolutionStatus::InvalidPlatform: return "Invalid: platform is not reachable on this route";
		case SceneStopResolutionStatus::UnknownStation: return "Invalid: station is missing";
		case SceneStopResolutionStatus::UnresolvedRoute: return "Invalid: route traversal is unresolved";
	}
	return {};
}

// How the stop dialog shows a resolution: as help when the stop is on the route, as a warning when the platform or the
// route context needs a look, and as an error when the stop has no place on the route.
std::optional<DialogLayout::Severity> stopResolutionSeverity(SceneStopResolutionStatus status) {
	switch (status) {
		case SceneStopResolutionStatus::Resolved: return std::nullopt;
		case SceneStopResolutionStatus::AmbiguousPlatform:
		case SceneStopResolutionStatus::OffRouteContext:
		case SceneStopResolutionStatus::InvalidPlatform: return DialogLayout::Severity::Warning;
		case SceneStopResolutionStatus::OutOfOrder:
		case SceneStopResolutionStatus::UnknownStation:
		case SceneStopResolutionStatus::UnresolvedRoute: return DialogLayout::Severity::Error;
	}
	return DialogLayout::Severity::Error;
}

} // namespace

TimetableStopsPanel::TimetableStopsPanel(QWidget* parent) : SceneEditorPanel(parent) {
	setObjectName("timetableStopsPanel");
	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	QHBoxLayout* timeModeLayout = new QHBoxLayout();
	timeModeLayout->addWidget(new QLabel("Planned time display", this));
	m_timeModeCombo = new QComboBox(this);
	m_timeModeCombo->setObjectName("timetableTimeModeCombo");
	m_timeModeCombo->addItem("Elapsed offsets (s)", false);
	m_timeModeCombo->addItem("Clock time", true);
	timeModeLayout->addWidget(m_timeModeCombo);
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
	m_table->setTabKeyNavigation(true);
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
	buttonLayout->addWidget(m_addButton, 0, 0);
	buttonLayout->addWidget(m_insertAfterButton, 0, 1, 1, 2);
	buttonLayout->addWidget(m_removeButton, 1, 0);
	buttonLayout->addWidget(m_moveUpButton, 1, 1);
	buttonLayout->addWidget(m_moveDownButton, 1, 2);
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

void TimetableStopsPanel::setDocument(SceneDocument* document) {
	QObject::disconnect(m_replacedConnection);
	m_document = document;
	if (m_document)
		m_replacedConnection = connect(m_document, &SceneDocument::replaced, this, &TimetableStopsPanel::refresh);
	refresh();
}

void TimetableStopsPanel::setServiceId(const QString& serviceId) {
	m_serviceId = serviceId.toStdString();
	refresh();
}

bool TimetableStopsPanel::hasScene() const {
	return m_document && m_document->isLoaded();
}

bool TimetableStopsPanel::canEdit() const {
	return hasScene() && m_document->isEditable();
}

int TimetableStopsPanel::serviceRow() const {
	if (!hasScene() || m_serviceId.empty())
		return -1;
	const auto& services = m_document->model().services;
	const auto found = std::find_if(services.begin(), services.end(), [this](const SceneService& service) { return service.id == m_serviceId; });
	return found == services.end() ? -1 : static_cast<int>(found - services.begin());
}

void TimetableStopsPanel::refresh() {
	const int serviceIndex = serviceRow();
	const bool hasService = serviceIndex >= 0;
	const bool editorAvailable = hasService && canEdit();
	const int previousRow = m_table->currentRow();
	const SceneModel noScene;
	const SceneModel& model = m_document ? m_document->model() : noScene;
	const long long baseOffset = baseTimeToSeconds(model.baseTime);
	m_timeBaseLabel->setText(hasService
			? QString("Case base %1; %2")
				  .arg(QString::fromStdString(model.baseTime.empty() ? std::string("00:00:00") : model.baseTime))
				  .arg(m_clockMode ? QStringLiteral("clock times include explicit day offsets")
								   : QStringLiteral("planned times are elapsed seconds from this base"))
			: QString());
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
				const QString station = stationDisplayName(model, stop.stationId);
				const QString platform = stop.platformId.empty() ? noValueText() : QString::fromStdString(stop.platformId);
				const QString arrival = stop.hasPlannedArrival
					? QString::fromStdString(formatPlannedTime(stop.plannedArrivalSeconds, m_clockMode, baseOffset))
					: noValueText();
				const QString departure = stop.hasPlannedDeparture
					? QString::fromStdString(formatPlannedTime(stop.plannedDepartureSeconds, m_clockMode, baseOffset))
					: noValueText();
				const std::array<QString, 5> values = {station, platform, QString::fromStdString(csv::formatDouble(stop.dwellSeconds)), arrival, departure};
				const QString reason = row < static_cast<int>(resolutions.size())
					? stopResolutionText(resolutions[static_cast<std::size_t>(row)].status)
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
	const int room = std::max(available - minimumStation, 0);
	// While the kept columns fit, only the others are shortened.
	const bool keepClockTimes = wanted > room && kept < room;
	int used = 0;
	for (int column = 1; column < 5; ++column) {
		int& width = widths[static_cast<std::size_t>(column)];
		if (keepClockTimes) {
			if (!isClockTime(column))
				width = std::max(width * (room - kept) / (wanted - kept), minimum);
		} else if (wanted > room) {
			width = std::max(width * room / wanted, minimum);
		}
		used += width;
	}
	widths[0] = std::max(available - used, minimumStation);
	for (int column = 0; column < 5; ++column)
		m_table->setColumnWidth(column, widths[static_cast<std::size_t>(column)]);
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
	const int serviceIndex = serviceRow();
	if (!canEdit() || serviceIndex < 0)
		return;
	SceneModel& model = m_document->mutableModel();
	SceneService& service = model.services[static_cast<std::size_t>(serviceIndex)];
	const bool append = row < 0;
	if (!append && row >= static_cast<int>(service.stops.size()))
		return;
	// The position of a new stop in the timetable, or the row of the stop being edited.
	std::size_t stopIndex = !append ? static_cast<std::size_t>(row)
		: insertIndex < 0			? service.stops.size()
									: std::min(static_cast<std::size_t>(insertIndex), service.stops.size());
	// The route visits offered for the stop at `stopIndex`. A new stop is offered only
	// the visits between its neighbours; `windowProblem` says why there are none.
	SceneRouteTraversal traversal;
	std::string windowProblem;
	const auto updateWindow = [&]() {
		const auto window = sceneStopInsertionWindow(model, service, stopIndex);
		traversal = window.ok ? window.visits : SceneRouteTraversal();
		windowProblem = window.ok ? std::string() : window.problem;
		if (window.ok && traversal.visits.empty())
			windowProblem = "No station visit is available at this position between the neighbouring stops.";
	};
	if (append)
		updateWindow();
	else
		traversal = sceneRemainingStopTraversal(model, service, stopIndex);

	SceneStop draft = append ? SceneStop() : service.stops[stopIndex];
	if (append && !traversal.visits.empty()) {
		draft.stationId = traversal.visits.front().stationId;
		std::vector<std::string> candidatePlatforms;
		for (const auto& visit : traversal.visits)
			if (visit.stationId == draft.stationId && !visit.platformId.empty()
				&& std::find(candidatePlatforms.begin(), candidatePlatforms.end(), visit.platformId)
					== candidatePlatforms.end())
				candidatePlatforms.push_back(visit.platformId);
		if (candidatePlatforms.size() == 1)
			draft.platformId = candidatePlatforms.front();
	}

	QDialog dialog(this);
	dialog.setObjectName("stopEditorDialog");
	dialog.setWindowTitle(append ? "Add timetable stop" : "Edit timetable stop");
	auto* body = new QWidget;
	auto* layout = new QVBoxLayout(body);
	auto* form = new QFormLayout();
	ChoiceComboBox* positionCombo = nullptr;
	if (append) {
		positionCombo = new ChoiceComboBox(&dialog);
		positionCombo->setObjectName("stopEditorPositionCombo");
		positionCombo->setAccessibleName("Timetable stop position");
		positionCombo->addItem("At the start of the timetable", 0);
		for (std::size_t index = 0; index < service.stops.size(); ++index) {
			const SceneStop& stop = service.stops[index];
			QString label = QString("After stop %1: %2").arg(index + 1).arg(stationDisplayName(model, stop.stationId));
			if (!stop.platformId.empty())
				label += QString(" [%1]").arg(QString::fromStdString(stop.platformId));
			positionCombo->addItem(label, static_cast<int>(index + 1));
		}
		positionCombo->setCurrentIndex(static_cast<int>(stopIndex));
		form->addRow("Insert position", positionCombo);
	}
	auto* stationCombo = new ChoiceComboBox(&dialog);
	stationCombo->setObjectName("stopEditorStationCombo");
	stationCombo->setAccessibleName("Timetable stop station");
	const auto fillStationChoices = [&]() {
		const QSignalBlocker blocker(stationCombo);
		stationCombo->clear();
		const auto addStationChoice = [&](const std::string& stationId) {
			if (stationId.empty() || stationCombo->findData(QString::fromStdString(stationId)) >= 0)
				return;
			stationCombo->addItem(stationDisplayName(model, stationId), QString::fromStdString(stationId));
		};
		for (const auto& visit : traversal.visits)
			addStationChoice(visit.stationId);
		if (!append)
			addStationChoice(draft.stationId);
		if (stationCombo->count() == 0)
			stationCombo->addItem(append ? "(no reachable station here)" : "(missing station)", QString());
		stationCombo->setEnabled(!append || !traversal.visits.empty());
	};
	fillStationChoices();
	form->addRow("Station", stationCombo);
	auto* platformCombo = new ChoiceComboBox(&dialog);
	platformCombo->setObjectName("stopEditorPlatformCombo");
	platformCombo->setAccessibleName("Timetable stop platform");
	form->addRow("Compatible platform", platformCombo);
	auto* eligibilityLabel = new QLabel(&dialog);
	eligibilityLabel->setObjectName("stopEditorEligibilityLabel");
	form->addRow(QString(), eligibilityLabel);
	const auto showEligibility = [eligibilityLabel](SceneStopResolutionStatus status, const QString& text) {
		if (const auto severity = stopResolutionSeverity(status))
			DialogLayout::setStatus(eligibilityLabel, *severity, text);
		else
			DialogLayout::setHelp(eligibilityLabel, text);
	};

	auto* modeCombo = new ChoiceComboBox(&dialog);
	modeCombo->setObjectName("stopEditorTimeModeCombo");
	modeCombo->addItem("Elapsed offsets (s)", false);
	modeCombo->addItem("Clock time", true);
	modeCombo->setCurrentIndex(m_clockMode ? 1 : 0);
	form->addRow("Planned time display", modeCombo);
	auto* baseLabel = new QLabel(&dialog);
	baseLabel->setObjectName("stopEditorBaseTimeLabel");
	DialogLayout::setHelp(baseLabel,
		QString("Case base time: %1").arg(QString::fromStdString(model.baseTime.empty() ? std::string("00:00:00") : model.baseTime)));
	form->addRow(QString(), baseLabel);

	auto* arrivalPresent = new QCheckBox("Planned arrival", &dialog);
	arrivalPresent->setObjectName("stopEditorArrivalPresent");
	auto* arrivalEdit = new QLineEdit(&dialog);
	arrivalEdit->setObjectName("stopEditorArrivalEdit");
	arrivalEdit->setPlaceholderText("blank = absent");
	arrivalEdit->setAccessibleName("Planned arrival time");
	auto* arrivalLayout = new QHBoxLayout();
	arrivalLayout->addWidget(arrivalPresent);
	arrivalLayout->addWidget(arrivalEdit, 1);
	form->addRow("Arrival", arrivalLayout);
	auto* departurePresent = new QCheckBox("Planned departure", &dialog);
	departurePresent->setObjectName("stopEditorDeparturePresent");
	auto* departureEdit = new QLineEdit(&dialog);
	departureEdit->setObjectName("stopEditorDepartureEdit");
	departureEdit->setPlaceholderText("blank = absent");
	departureEdit->setAccessibleName("Planned departure time");
	auto* departureLayout = new QHBoxLayout();
	departureLayout->addWidget(departurePresent);
	departureLayout->addWidget(departureEdit, 1);
	form->addRow("Departure", departureLayout);
	auto* dwellEdit = new QLineEdit(&dialog);
	dwellEdit->setObjectName("stopEditorDwellEdit");
	dwellEdit->setAccessibleName("Minimum dwell seconds");
	form->addRow("Minimum dwell (s)", dwellEdit);
	layout->addLayout(form);
	auto* errorLabel = new QLabel(&dialog);
	errorLabel->setObjectName("stopEditorErrorLabel");
	layout->addWidget(errorLabel);
	// An empty text hides the label. A text is scrolled into view once the layout has made room for it, because the body
	// of the dialog scrolls and the label is at its end.
	const auto showError = [errorLabel](const QString& text) {
		DialogLayout::setStatus(errorLabel, DialogLayout::Severity::Error, text);
		if (text.isEmpty())
			return;
		QTimer::singleShot(0, errorLabel, [errorLabel] {
			for (QWidget* ancestor = errorLabel->parentWidget(); ancestor; ancestor = ancestor->parentWidget())
				if (auto* scroll = qobject_cast<QScrollArea*>(ancestor)) {
					scroll->ensureWidgetVisible(errorLabel);
					return;
				}
		});
	};
	showError(QString());
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	buttons->setObjectName("stopEditorButtons");
	buttons->button(QDialogButtonBox::Ok)->setObjectName("stopEditorAcceptButton");
	buttons->button(QDialogButtonBox::Cancel)->setObjectName("stopEditorCancelButton");
	DialogLayout::install(dialog, append ? "Add timetable stop" : "Edit timetable stop",
		"Set the station, compatible platform and planned times.", body, buttons);

	const long long baseOffsetSeconds = baseTimeToSeconds(model.baseTime);
	bool dialogClockMode = m_clockMode;
	const auto optionalArrival = [&]() -> std::optional<double> {
		return draft.hasPlannedArrival ? std::optional<double>(draft.plannedArrivalSeconds) : std::nullopt;
	};
	const auto optionalDeparture = [&]() -> std::optional<double> {
		return draft.hasPlannedDeparture ? std::optional<double>(draft.plannedDepartureSeconds) : std::nullopt;
	};
	const auto setTimeField = [&](QCheckBox* present, QLineEdit* edit, const std::optional<double>& value) {
		const QSignalBlocker checkBlocker(present);
		present->setChecked(value.has_value());
		const QSignalBlocker editBlocker(edit);
		edit->setText(QString::fromStdString(formatPlannedTime(value, dialogClockMode, baseOffsetSeconds)));
		edit->setEnabled(value.has_value());
	};
	setTimeField(arrivalPresent, arrivalEdit, optionalArrival());
	setTimeField(departurePresent, departureEdit, optionalDeparture());
	dwellEdit->setText(QString::fromStdString(csv::formatDouble(draft.dwellSeconds)));

	int invalidPlatformIndex = -1;
	const auto stationOnRoute = [&](const std::string& stationId) {
		return std::any_of(traversal.visits.begin(), traversal.visits.end(),
			[&](const auto& visit) { return visit.stationId == stationId; });
	};
	const auto updateEligibility = [&]() {
		if (!windowProblem.empty()) {
			showEligibility(SceneStopResolutionStatus::UnresolvedRoute, QString::fromStdString(windowProblem));
			return SceneStopResolutionStatus::UnresolvedRoute;
		}
		SceneService candidate = service;
		if (append)
			candidate.stops.insert(candidate.stops.begin() + stopIndex, draft);
		else
			candidate.stops[stopIndex] = draft;
		const auto resolutions = resolveSceneServiceStops(model, candidate,
			sceneServiceTraversal(model, candidate));
		const auto status = stopIndex < resolutions.size() ? resolutions[stopIndex].status
														   : SceneStopResolutionStatus::UnresolvedRoute;
		showEligibility(status, stopResolutionText(status));
		if (stationCombo->currentIndex() >= 0 && !stationOnRoute(draft.stationId))
			stationCombo->setItemData(stationCombo->currentIndex(), eligibilityLabel->text(), Qt::ToolTipRole);
		if (invalidPlatformIndex >= 0 && platformCombo->currentIndex() == invalidPlatformIndex)
			platformCombo->setItemData(invalidPlatformIndex, eligibilityLabel->text(), Qt::ToolTipRole);
		return status;
	};
	const auto refreshPlatforms = [&]() {
		const std::string stationId = stationCombo->currentData().toString().toStdString();
		const QSignalBlocker blocker(platformCombo);
		platformCombo->clear();
		invalidPlatformIndex = -1;
		platformCombo->addItem(QStringLiteral("(no platform)"), QString());
		std::vector<std::string> choices;
		for (const auto& visit : traversal.visits)
			if (visit.stationId == stationId && !visit.platformId.empty()
				&& std::find(choices.begin(), choices.end(), visit.platformId) == choices.end())
				choices.push_back(visit.platformId);
		for (const auto& platform : choices)
			platformCombo->addItem(QString::fromStdString(platform), QString::fromStdString(platform));
		if (!draft.platformId.empty() && platformCombo->findData(QString::fromStdString(draft.platformId)) < 0) {
			invalidPlatformIndex = platformCombo->count();
			platformCombo->addItem(QString("Invalid: %1").arg(QString::fromStdString(draft.platformId)),
				QString::fromStdString(draft.platformId));
		}
		int index = platformCombo->findData(QString::fromStdString(draft.platformId));
		platformCombo->setCurrentIndex(index < 0 ? 0 : index);
		platformCombo->setEnabled(!stationId.empty());
	};
	{
		const QSignalBlocker blocker(stationCombo);
		const int index = stationCombo->findData(QString::fromStdString(draft.stationId));
		if (index >= 0)
			stationCombo->setCurrentIndex(index);
	}
	refreshPlatforms();
	updateEligibility();
	DialogLayout::fitWidthToContent(dialog);
	if (positionCombo) {
		connect(positionCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int index) {
			if (index < 0)
				return;
			stopIndex = static_cast<std::size_t>(positionCombo->itemData(index).toInt());
			updateWindow();
			fillStationChoices();
			int stationIndex = stationCombo->findData(QString::fromStdString(draft.stationId));
			if (stationIndex < 0)
				stationIndex = 0;
			const std::string stationId = stationCombo->itemData(stationIndex).toString().toStdString();
			const bool platformOffered = std::any_of(traversal.visits.begin(), traversal.visits.end(),
				[&](const auto& visit) { return visit.stationId == stationId && visit.platformId == draft.platformId; });
			if (stationId != draft.stationId || !platformOffered)
				draft.platformId.clear();
			draft.stationId = stationId;
			{
				const QSignalBlocker blocker(stationCombo);
				stationCombo->setCurrentIndex(stationIndex);
			}
			refreshPlatforms();
			updateEligibility();
			showError(QString());
		});
	}
	connect(stationCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int index) {
		if (index < 0)
			return;
		const std::string stationId = stationCombo->itemData(index).toString().toStdString();
		if (stationId != draft.stationId)
			draft.platformId.clear();
		draft.stationId = stationId;
		refreshPlatforms();
		updateEligibility();
	});
	connect(platformCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int index) {
		if (index >= 0)
			draft.platformId = platformCombo->itemData(index).toString().toStdString();
		updateEligibility();
	});
	connect(arrivalPresent, &QCheckBox::toggled, &dialog, [&](bool checked) {
		arrivalEdit->setEnabled(checked);
		if (!checked)
			arrivalEdit->clear();
	});
	connect(departurePresent, &QCheckBox::toggled, &dialog, [&](bool checked) {
		departureEdit->setEnabled(checked);
		if (!checked)
			departureEdit->clear();
	});
	const auto parseField = [&](QCheckBox* present, QLineEdit* edit, bool clock,
								std::optional<double>& value) {
		if (!present->isChecked() || edit->text().trimmed().isEmpty()) {
			value.reset();
			return true;
		}
		return parsePlannedTime(edit->text().toStdString(), clock, baseOffsetSeconds, value);
	};
	const auto showParseError = [&](QLineEdit* edit, const QString& field) {
		showError(QString("%1 is invalid. Enter a non-negative elapsed value or HH:MM:SS[.fraction] with an explicit +Nd day prefix.").arg(field));
		edit->setFocus();
	};
	connect(modeCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int index) {
		if (index < 0)
			return;
		const bool nextClockMode = modeCombo->itemData(index).toBool();
		if (nextClockMode == dialogClockMode)
			return;
		std::optional<double> arrival = optionalArrival();
		std::optional<double> departure = optionalDeparture();
		if (!parseField(arrivalPresent, arrivalEdit, dialogClockMode, arrival)
			|| !parseField(departurePresent, departureEdit, dialogClockMode, departure)) {
			const QSignalBlocker blocker(modeCombo);
			modeCombo->setCurrentIndex(dialogClockMode ? 1 : 0);
			showError("Finish the invalid planned time before changing its display mode.");
			return;
		}
		draft.hasPlannedArrival = arrival.has_value();
		if (draft.hasPlannedArrival)
			draft.plannedArrivalSeconds = *arrival;
		draft.hasPlannedDeparture = departure.has_value();
		if (draft.hasPlannedDeparture)
			draft.plannedDepartureSeconds = *departure;
		dialogClockMode = nextClockMode;
		setTimeField(arrivalPresent, arrivalEdit, arrival);
		setTimeField(departurePresent, departureEdit, departure);
		showError(QString());
	});
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
		if (!windowProblem.empty()) {
			showError(QString::fromStdString(windowProblem));
			if (positionCombo)
				positionCombo->setFocus();
			return;
		}
		std::optional<double> arrival = optionalArrival();
		std::optional<double> departure = optionalDeparture();
		if (!parseField(arrivalPresent, arrivalEdit, dialogClockMode, arrival)) {
			showParseError(arrivalEdit, "Planned arrival");
			return;
		}
		if (!parseField(departurePresent, departureEdit, dialogClockMode, departure)) {
			showParseError(departureEdit, "Planned departure");
			return;
		}
		bool dwellOk = false;
		const double dwell = dwellEdit->text().trimmed().toDouble(&dwellOk);
		if (!dwellOk || !std::isfinite(dwell) || dwell < 0.0) {
			showError("Minimum dwell must be a finite non-negative number of seconds.");
			dwellEdit->setFocus();
			return;
		}
		if (draft.stationId.empty()) {
			showError("Choose a station for this timetable stop.");
			stationCombo->setFocus();
			return;
		}
		const bool changedAssignment = append || draft.stationId != service.stops[stopIndex].stationId
			|| draft.platformId != service.stops[stopIndex].platformId;
		if (changedAssignment && updateEligibility() == SceneStopResolutionStatus::AmbiguousPlatform) {
			showError("Choose a compatible platform: more than one is reachable at this station.");
			platformCombo->setFocus();
			return;
		}
		draft.hasPlannedArrival = arrival.has_value();
		if (draft.hasPlannedArrival)
			draft.plannedArrivalSeconds = *arrival;
		draft.hasPlannedDeparture = departure.has_value();
		if (draft.hasPlannedDeparture)
			draft.plannedDepartureSeconds = *departure;
		draft.dwellSeconds = dwell;
		if (append) {
			const auto result = insertSceneStop(model, service, stopIndex, draft);
			if (!result.inserted) {
				showError(QString::fromStdString(result.error));
				return;
			}
		}
		dialog.accept();
	});

	if (dialog.exec() != QDialog::Accepted)
		return;
	if (!append)
		service.stops[stopIndex] = draft;
	m_document->markChanged(SceneDocument::Services, append ? "Add timetable stop" : "Edit timetable stop");
	refresh();
	m_table->setCurrentCell(static_cast<int>(stopIndex), 0);
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
	QMessageBox::information(this, "No reachable stop",
		"No station visit is available between any of the current stops. Check the route and stop order before adding a stop.");
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
	auto& stops = m_document->mutableModel().services[static_cast<std::size_t>(serviceIndex)].stops;
	if (stopRow < 0 || stopRow >= static_cast<int>(stops.size()))
		return;
	stops.erase(stops.begin() + stopRow);
	m_document->markChanged(SceneDocument::Services, "Remove timetable stop");
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
	auto& stops = m_document->mutableModel().services[static_cast<std::size_t>(serviceIndex)].stops;
	if (stopRow <= 0 || stopRow >= static_cast<int>(stops.size()))
		return;
	std::swap(stops[static_cast<std::size_t>(stopRow)], stops[static_cast<std::size_t>(stopRow - 1)]);
	m_document->markChanged(SceneDocument::Services, "Move timetable stop up");
	refresh();
	m_table->setCurrentCell(stopRow - 1, 0);
	emit changed();
}

void TimetableStopsPanel::moveStopDown() {
	const int serviceIndex = serviceRow();
	const int stopRow = m_table->currentRow();
	if (!canEdit() || serviceIndex < 0)
		return;
	auto& stops = m_document->mutableModel().services[static_cast<std::size_t>(serviceIndex)].stops;
	if (stopRow < 0 || stopRow + 1 >= static_cast<int>(stops.size()))
		return;
	std::swap(stops[static_cast<std::size_t>(stopRow)], stops[static_cast<std::size_t>(stopRow + 1)]);
	m_document->markChanged(SceneDocument::Services, "Move timetable stop down");
	refresh();
	m_table->setCurrentCell(stopRow + 1, 0);
	emit changed();
}
