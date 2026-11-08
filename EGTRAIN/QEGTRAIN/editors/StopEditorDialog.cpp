#include "editors/StopEditorDialog.h"

#include "scene/StopInsertion.h"
#include "util/CsvWriter.h"
#include "util/TimeFormat.h"
#include "widgets/ChoiceComboBox.h"
#include "widgets/DialogLayout.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

namespace {

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

void showResolution(QLabel* label, SceneStopResolutionStatus status, const QString& text) {
	if (const auto severity = stopResolutionSeverity(status))
		DialogLayout::setStatus(label, *severity, text);
	else
		DialogLayout::setHelp(label, text);
}

// The route visits at which a new stop at `position` can be, and in `problem` why there are none.
SceneRouteTraversal insertionVisits(const SceneModel& model, const SceneService& service, std::size_t position, std::string& problem) {
	const auto window = sceneStopInsertionWindow(model, service, position);
	problem = window.ok ? std::string() : window.problem;
	if (window.ok && window.visits.visits.empty())
		problem = "The route has no station here between the stops before and after.";
	return window.ok ? window.visits : SceneRouteTraversal();
}

// How the stop at `position` resolves on the route once `stop` is added there or takes the place of the stop there.
SceneStopResolutionStatus draftResolution(const SceneModel& model, const SceneService& service, std::size_t position, bool adding, const SceneStop& stop) {
	SceneService candidate = service;
	if (adding)
		candidate.stops.insert(candidate.stops.begin() + position, stop);
	else
		candidate.stops[position] = stop;
	const auto resolutions = resolveSceneServiceStops(model, candidate, sceneServiceTraversal(model, candidate));
	return position < resolutions.size() ? resolutions[position].status : SceneStopResolutionStatus::UnresolvedRoute;
}

std::optional<double> plannedArrival(const SceneStop& stop) {
	return stop.hasPlannedArrival ? std::optional<double>(stop.plannedArrivalSeconds) : std::nullopt;
}

std::optional<double> plannedDeparture(const SceneStop& stop) {
	return stop.hasPlannedDeparture ? std::optional<double>(stop.plannedDepartureSeconds) : std::nullopt;
}

void setPlannedTimes(SceneStop& stop, const std::optional<double>& arrival, const std::optional<double>& departure) {
	stop.hasPlannedArrival = arrival.has_value();
	if (stop.hasPlannedArrival)
		stop.plannedArrivalSeconds = *arrival;
	stop.hasPlannedDeparture = departure.has_value();
	if (stop.hasPlannedDeparture)
		stop.plannedDepartureSeconds = *departure;
}

// Reads a planned time field into `value`, which is absent when the box is not checked or the field has no text. False
// when the text does not parse.
bool parsePlannedField(bool present, const QString& text, bool clockMode, long long baseOffsetSeconds, std::optional<double>& value) {
	if (!present || text.trimmed().isEmpty()) {
		value.reset();
		return true;
	}
	return parsePlannedTime(text.toStdString(), clockMode, baseOffsetSeconds, value);
}

QString plannedTimeProblem(bool clockMode, const QString& field) {
	return clockMode ? QString("%1 must be a time such as 08:15:00, or +1d 00:10:00 for the next day.").arg(field)
					 : QString("%1 must be seconds after the base time, such as 120.").arg(field);
}

} // namespace

namespace TimetableStops {

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
		case SceneStopResolutionStatus::Resolved: return {};
		case SceneStopResolutionStatus::AmbiguousPlatform: return "Choose a platform: the route reaches more than one here.";
		case SceneStopResolutionStatus::OffRouteContext: return "Not on the route. The train does not stop here.";
		case SceneStopResolutionStatus::OutOfOrder: return "The route passes this station before the previous stop.";
		case SceneStopResolutionStatus::InvalidPlatform: return "The route does not use this platform.";
		case SceneStopResolutionStatus::UnknownStation: return "This station does not exist.";
		case SceneStopResolutionStatus::UnresolvedRoute: return "The route cannot be followed. Fix the route first.";
	}
	return {};
}

QString baseTimeText(const SceneModel& model, bool clockMode) {
	return clockMode ? QString()
					 : QString("Seconds after %1").arg(QString::fromStdString(model.baseTime.empty() ? std::string("00:00:00") : model.baseTime));
}

} // namespace TimetableStops

StopDraftCheck validateStopDraft(const SceneModel& model, const SceneService& service, std::size_t position, bool adding,
	const SceneStop& draft, const StopDraftFields& fields) {
	StopDraftCheck check;
	const auto problem = [&check](const QString& message, StopDraftField field) {
		check.message = message;
		check.field = field;
		return check;
	};
	if (adding) {
		std::string windowProblem;
		insertionVisits(model, service, position, windowProblem);
		if (!windowProblem.empty())
			return problem(QString::fromStdString(windowProblem), StopDraftField::Position);
	}
	std::optional<double> arrival = plannedArrival(draft);
	std::optional<double> departure = plannedDeparture(draft);
	if (!parsePlannedField(fields.hasArrival, fields.arrivalText, fields.clockMode, fields.baseOffsetSeconds, arrival))
		return problem(plannedTimeProblem(fields.clockMode, "Planned arrival"), StopDraftField::PlannedArrival);
	if (!parsePlannedField(fields.hasDeparture, fields.departureText, fields.clockMode, fields.baseOffsetSeconds, departure))
		return problem(plannedTimeProblem(fields.clockMode, "Planned departure"), StopDraftField::PlannedDeparture);
	bool dwellOk = false;
	const double dwell = fields.dwellText.trimmed().toDouble(&dwellOk);
	if (!dwellOk || !std::isfinite(dwell) || dwell < 0.0)
		return problem("Minimum dwell must be 0 or more seconds.", StopDraftField::Dwell);
	if (draft.stationId.empty())
		return problem("Choose a station for this timetable stop.", StopDraftField::Station);
	const bool changedAssignment = adding || draft.stationId != service.stops[position].stationId
		|| draft.platformId != service.stops[position].platformId;
	if (changedAssignment && draftResolution(model, service, position, adding, draft) == SceneStopResolutionStatus::AmbiguousPlatform)
		return problem("Choose a platform: the route reaches more than one here.", StopDraftField::Platform);
	SceneStop stop = draft;
	setPlannedTimes(stop, arrival, departure);
	stop.dwellSeconds = dwell;
	if (adding) {
		SceneService inserted = service;
		const auto result = insertSceneStop(model, inserted, position, stop);
		if (!result.inserted)
			return problem(QString::fromStdString(result.error), StopDraftField::None);
	}
	check.ok = true;
	check.stop = std::move(stop);
	return check;
}

StopEditorDialog::StopEditorDialog(SceneModel model, SceneService service, std::size_t stopIndex, bool adding, bool clockMode, QWidget* parent)
	: QDialog(parent), m_model(std::move(model)), m_service(std::move(service)), m_stopIndex(stopIndex), m_adding(adding), m_clockMode(clockMode),
	  m_baseOffsetSeconds(TimetableStops::baseTimeToSeconds(m_model.baseTime)) {
	// A new stop is offered only the visits between its neighbours, and an edited stop those after the stop before it.
	if (m_adding)
		updateWindow();
	else
		m_traversal = sceneRemainingStopTraversal(m_model, m_service, m_stopIndex);
	if (!m_adding) {
		m_draft = m_service.stops[m_stopIndex];
	} else if (!m_traversal.visits.empty()) {
		m_draft.stationId = m_traversal.visits.front().stationId;
		std::vector<std::string> candidatePlatforms;
		for (const auto& visit : m_traversal.visits)
			if (visit.stationId == m_draft.stationId && !visit.platformId.empty()
				&& std::find(candidatePlatforms.begin(), candidatePlatforms.end(), visit.platformId)
					== candidatePlatforms.end())
				candidatePlatforms.push_back(visit.platformId);
		if (candidatePlatforms.size() == 1)
			m_draft.platformId = candidatePlatforms.front();
	}

	setObjectName("stopEditorDialog");
	setWindowTitle(m_adding ? "Add Timetable Stop" : "Edit Timetable Stop");
	auto* body = new QWidget;
	auto* layout = new QVBoxLayout(body);
	auto* form = new QFormLayout();
	// The messages of the form ask for no width. A form that keeps its fields at their size hint, as on macOS, would give
	// them none and leave an empty band in place of their text.
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	if (m_adding) {
		m_positionCombo = new ChoiceComboBox(this);
		m_positionCombo->setObjectName("stopEditorPositionCombo");
		m_positionCombo->setAccessibleName("Timetable stop position");
		m_positionCombo->addItem("At the start of the timetable", 0);
		for (std::size_t index = 0; index < m_service.stops.size(); ++index) {
			const SceneStop& stop = m_service.stops[index];
			QString label = QString("After stop %1: %2").arg(index + 1).arg(TimetableStops::stationDisplayName(m_model, stop.stationId));
			if (!stop.platformId.empty())
				label += QString(" [%1]").arg(QString::fromStdString(stop.platformId));
			m_positionCombo->addItem(label, static_cast<int>(index + 1));
		}
		m_positionCombo->setCurrentIndex(static_cast<int>(m_stopIndex));
		form->addRow("Insert position", m_positionCombo);
	}
	m_stationCombo = new ChoiceComboBox(this);
	m_stationCombo->setObjectName("stopEditorStationCombo");
	m_stationCombo->setAccessibleName("Timetable stop station");
	fillStationChoices();
	form->addRow("Station", m_stationCombo);
	m_platformCombo = new ChoiceComboBox(this);
	m_platformCombo->setObjectName("stopEditorPlatformCombo");
	m_platformCombo->setAccessibleName("Timetable stop platform");
	form->addRow("Compatible platform", m_platformCombo);
	m_eligibilityLabel = new QLabel(this);
	m_eligibilityLabel->setObjectName("stopEditorEligibilityLabel");
	form->addRow(QString(), m_eligibilityLabel);

	m_modeCombo = new ChoiceComboBox(this);
	m_modeCombo->setObjectName("stopEditorTimeModeCombo");
	m_modeCombo->addItem("Elapsed offsets (s)", false);
	m_modeCombo->addItem("Clock time", true);
	m_modeCombo->setCurrentIndex(m_clockMode ? 1 : 0);
	form->addRow("Planned time display", m_modeCombo);
	m_baseLabel = new QLabel(this);
	m_baseLabel->setObjectName("stopEditorBaseTimeLabel");
	DialogLayout::setHelp(m_baseLabel, TimetableStops::baseTimeText(m_model, m_clockMode));
	form->addRow(QString(), m_baseLabel);

	m_arrivalPresent = new QCheckBox("Planned arrival", this);
	m_arrivalPresent->setObjectName("stopEditorArrivalPresent");
	m_arrivalEdit = new QLineEdit(this);
	m_arrivalEdit->setObjectName("stopEditorArrivalEdit");
	m_arrivalEdit->setAccessibleName("Planned arrival time");
	form->addRow(m_arrivalPresent, m_arrivalEdit);
	m_departurePresent = new QCheckBox("Planned departure", this);
	m_departurePresent->setObjectName("stopEditorDeparturePresent");
	m_departureEdit = new QLineEdit(this);
	m_departureEdit->setObjectName("stopEditorDepartureEdit");
	m_departureEdit->setAccessibleName("Planned departure time");
	form->addRow(m_departurePresent, m_departureEdit);
	// The two boxes are labels of the form. With labels aligned to the right, as on macOS, boxes of different widths
	// would not line up, so both take the width of the wider one.
	const int presentWidth = std::max(m_arrivalPresent->sizeHint().width(), m_departurePresent->sizeHint().width());
	m_arrivalPresent->setMinimumWidth(presentWidth);
	m_departurePresent->setMinimumWidth(presentWidth);
	m_dwellEdit = new QLineEdit(this);
	m_dwellEdit->setObjectName("stopEditorDwellEdit");
	m_dwellEdit->setAccessibleName("Minimum dwell seconds");
	form->addRow("Minimum dwell (s)", m_dwellEdit);
	layout->addLayout(form);
	m_errorLabel = new QLabel(this);
	m_errorLabel->setObjectName("stopEditorErrorLabel");
	layout->addWidget(m_errorLabel);
	showError(QString());
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	buttons->setObjectName("stopEditorButtons");
	buttons->button(QDialogButtonBox::Ok)->setObjectName("stopEditorAcceptButton");
	buttons->button(QDialogButtonBox::Cancel)->setObjectName("stopEditorCancelButton");
	DialogLayout::install(*this, m_adding ? "Add Timetable Stop" : "Edit Timetable Stop", QString(), body, buttons);

	setTimeField(m_arrivalPresent, m_arrivalEdit, plannedArrival(m_draft));
	setTimeField(m_departurePresent, m_departureEdit, plannedDeparture(m_draft));
	m_dwellEdit->setText(QString::fromStdString(csv::formatDouble(m_draft.dwellSeconds)));
	{
		const QSignalBlocker blocker(m_stationCombo);
		const int index = m_stationCombo->findData(QString::fromStdString(m_draft.stationId));
		if (index >= 0)
			m_stationCombo->setCurrentIndex(index);
	}
	refreshPlatforms();
	updateEligibility();
	DialogLayout::fitWidthToContent(*this);
	if (m_positionCombo)
		connect(m_positionCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &StopEditorDialog::changePosition);
	connect(m_stationCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &StopEditorDialog::changeStation);
	connect(m_platformCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &StopEditorDialog::changePlatform);
	connect(m_arrivalPresent, &QCheckBox::toggled, this, [this](bool checked) {
		m_arrivalEdit->setEnabled(checked);
		if (!checked)
			m_arrivalEdit->clear();
	});
	connect(m_departurePresent, &QCheckBox::toggled, this, [this](bool checked) {
		m_departureEdit->setEnabled(checked);
		if (!checked)
			m_departureEdit->clear();
	});
	connect(m_modeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &StopEditorDialog::changeTimeMode);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(buttons, &QDialogButtonBox::accepted, this, &StopEditorDialog::acceptDraft);
}

void StopEditorDialog::updateWindow() {
	m_traversal = insertionVisits(m_model, m_service, m_stopIndex, m_windowProblem);
}

void StopEditorDialog::fillStationChoices() {
	const QSignalBlocker blocker(m_stationCombo);
	m_stationCombo->clear();
	const auto addStationChoice = [this](const std::string& stationId) {
		if (stationId.empty() || m_stationCombo->findData(QString::fromStdString(stationId)) >= 0)
			return;
		m_stationCombo->addItem(TimetableStops::stationDisplayName(m_model, stationId), QString::fromStdString(stationId));
	};
	for (const auto& visit : m_traversal.visits)
		addStationChoice(visit.stationId);
	if (!m_adding)
		addStationChoice(m_draft.stationId);
	if (m_stationCombo->count() == 0)
		m_stationCombo->addItem(m_adding ? "(no reachable station here)" : "(missing station)", QString());
	m_stationCombo->setEnabled(!m_adding || !m_traversal.visits.empty());
}

void StopEditorDialog::updateEligibility() {
	if (!m_windowProblem.empty()) {
		showResolution(m_eligibilityLabel, SceneStopResolutionStatus::UnresolvedRoute, QString::fromStdString(m_windowProblem));
		return;
	}
	const auto status = draftResolution(m_model, m_service, m_stopIndex, m_adding, m_draft);
	showResolution(m_eligibilityLabel, status, TimetableStops::stopResolutionText(status));
	const bool stationOnRoute = std::any_of(m_traversal.visits.begin(), m_traversal.visits.end(),
		[this](const auto& visit) { return visit.stationId == m_draft.stationId; });
	if (m_stationCombo->currentIndex() >= 0 && !stationOnRoute)
		m_stationCombo->setItemData(m_stationCombo->currentIndex(), m_eligibilityLabel->text(), Qt::ToolTipRole);
	if (m_invalidPlatformIndex >= 0 && m_platformCombo->currentIndex() == m_invalidPlatformIndex)
		m_platformCombo->setItemData(m_invalidPlatformIndex, m_eligibilityLabel->text(), Qt::ToolTipRole);
}

void StopEditorDialog::refreshPlatforms() {
	const std::string stationId = m_stationCombo->currentData().toString().toStdString();
	const QSignalBlocker blocker(m_platformCombo);
	m_platformCombo->clear();
	m_invalidPlatformIndex = -1;
	m_platformCombo->addItem(QStringLiteral("(no platform)"), QString());
	std::vector<std::string> choices;
	for (const auto& visit : m_traversal.visits)
		if (visit.stationId == stationId && !visit.platformId.empty()
			&& std::find(choices.begin(), choices.end(), visit.platformId) == choices.end())
			choices.push_back(visit.platformId);
	for (const auto& platform : choices)
		m_platformCombo->addItem(QString::fromStdString(platform), QString::fromStdString(platform));
	if (!m_draft.platformId.empty() && m_platformCombo->findData(QString::fromStdString(m_draft.platformId)) < 0) {
		m_invalidPlatformIndex = m_platformCombo->count();
		m_platformCombo->addItem(QString("%1 (not on the route)").arg(QString::fromStdString(m_draft.platformId)),
			QString::fromStdString(m_draft.platformId));
	}
	const int index = m_platformCombo->findData(QString::fromStdString(m_draft.platformId));
	m_platformCombo->setCurrentIndex(index < 0 ? 0 : index);
	m_platformCombo->setEnabled(!stationId.empty());
}

void StopEditorDialog::showError(const QString& text) {
	DialogLayout::setStatus(m_errorLabel, DialogLayout::Severity::Error, text);
	if (text.isEmpty())
		return;
	// The body of the dialog scrolls and the label is at its end, so the text is scrolled into view once the layout has made
	// room for it.
	QLabel* errorLabel = m_errorLabel;
	QTimer::singleShot(0, errorLabel, [errorLabel] {
		for (QWidget* ancestor = errorLabel->parentWidget(); ancestor; ancestor = ancestor->parentWidget())
			if (auto* scroll = qobject_cast<QScrollArea*>(ancestor)) {
				scroll->ensureWidgetVisible(errorLabel);
				return;
			}
	});
}

void StopEditorDialog::setTimeField(QCheckBox* present, QLineEdit* edit, const std::optional<double>& value) {
	const QSignalBlocker checkBlocker(present);
	present->setChecked(value.has_value());
	const QSignalBlocker editBlocker(edit);
	edit->setText(QString::fromStdString(formatPlannedTime(value, m_clockMode, m_baseOffsetSeconds)));
	edit->setEnabled(value.has_value());
}

void StopEditorDialog::changePosition(int index) {
	if (index < 0)
		return;
	m_stopIndex = static_cast<std::size_t>(m_positionCombo->itemData(index).toInt());
	updateWindow();
	fillStationChoices();
	int stationIndex = m_stationCombo->findData(QString::fromStdString(m_draft.stationId));
	if (stationIndex < 0)
		stationIndex = 0;
	const std::string stationId = m_stationCombo->itemData(stationIndex).toString().toStdString();
	const bool platformOffered = std::any_of(m_traversal.visits.begin(), m_traversal.visits.end(),
		[&](const auto& visit) { return visit.stationId == stationId && visit.platformId == m_draft.platformId; });
	if (stationId != m_draft.stationId || !platformOffered)
		m_draft.platformId.clear();
	m_draft.stationId = stationId;
	{
		const QSignalBlocker blocker(m_stationCombo);
		m_stationCombo->setCurrentIndex(stationIndex);
	}
	refreshPlatforms();
	updateEligibility();
	showError(QString());
}

void StopEditorDialog::changeStation(int index) {
	if (index < 0)
		return;
	const std::string stationId = m_stationCombo->itemData(index).toString().toStdString();
	if (stationId != m_draft.stationId)
		m_draft.platformId.clear();
	m_draft.stationId = stationId;
	refreshPlatforms();
	updateEligibility();
}

void StopEditorDialog::changePlatform(int index) {
	if (index >= 0)
		m_draft.platformId = m_platformCombo->itemData(index).toString().toStdString();
	updateEligibility();
}

void StopEditorDialog::changeTimeMode(int index) {
	if (index < 0)
		return;
	const bool nextClockMode = m_modeCombo->itemData(index).toBool();
	if (nextClockMode == m_clockMode)
		return;
	std::optional<double> arrival = plannedArrival(m_draft);
	std::optional<double> departure = plannedDeparture(m_draft);
	if (!parsePlannedField(m_arrivalPresent->isChecked(), m_arrivalEdit->text(), m_clockMode, m_baseOffsetSeconds, arrival)
		|| !parsePlannedField(m_departurePresent->isChecked(), m_departureEdit->text(), m_clockMode, m_baseOffsetSeconds, departure)) {
		const QSignalBlocker blocker(m_modeCombo);
		m_modeCombo->setCurrentIndex(m_clockMode ? 1 : 0);
		showError("Fix the planned time before switching the display.");
		return;
	}
	setPlannedTimes(m_draft, arrival, departure);
	m_clockMode = nextClockMode;
	DialogLayout::setHelp(m_baseLabel, TimetableStops::baseTimeText(m_model, m_clockMode));
	setTimeField(m_arrivalPresent, m_arrivalEdit, arrival);
	setTimeField(m_departurePresent, m_departureEdit, departure);
	showError(QString());
}

void StopEditorDialog::acceptDraft() {
	StopDraftFields fields;
	fields.hasArrival = m_arrivalPresent->isChecked();
	fields.arrivalText = m_arrivalEdit->text();
	fields.hasDeparture = m_departurePresent->isChecked();
	fields.departureText = m_departureEdit->text();
	fields.dwellText = m_dwellEdit->text();
	fields.clockMode = m_clockMode;
	fields.baseOffsetSeconds = m_baseOffsetSeconds;
	const StopDraftCheck check = validateStopDraft(m_model, m_service, m_stopIndex, m_adding, m_draft, fields);
	if (!check.ok) {
		showError(check.message);
		QWidget* field = nullptr;
		switch (check.field) {
			case StopDraftField::None: break;
			case StopDraftField::Position: field = m_positionCombo; break;
			case StopDraftField::PlannedArrival: field = m_arrivalEdit; break;
			case StopDraftField::PlannedDeparture: field = m_departureEdit; break;
			case StopDraftField::Dwell: field = m_dwellEdit; break;
			case StopDraftField::Station: field = m_stationCombo; break;
			case StopDraftField::Platform: field = m_platformCombo; break;
		}
		if (field)
			field->setFocus();
		return;
	}
	m_draft = check.stop;
	accept();
}
