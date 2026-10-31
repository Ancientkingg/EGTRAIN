#pragma once

// SceneDocument.h first: it includes the scene model, which has a member named signals, with the signals keyword of Qt
// undefined, so SectionInventory.h below finds the model included.
#include "editors/SceneDocument.h"
#include "scene/SectionInventory.h"

#include <QDialog>
#include <QString>

#include <cstddef>
#include <optional>
#include <string>

class ChoiceComboBox;
class QCheckBox;
class QLabel;
class QLineEdit;
class QWidget;

/// What the timetable table and the stop dialog both show of a scene.
namespace TimetableStops {
/// Seconds since midnight of an "hh:mm:ss" base time; 0 when the text is empty or not a time of day.
long long baseTimeToSeconds(const std::string& hhmmss);
/// The name of a station, or its id when it has no name. A station that the scene does not have shows its id, and an empty
/// id shows "(missing station)".
QString stationDisplayName(const SceneModel& model, const std::string& stationId);
/// What is wrong with a stop on its route; empty for a stop that is on it.
QString stopResolutionText(SceneStopResolutionStatus status);
/// The line that names the base time while planned times show as seconds after it; empty for clock times.
QString baseTimeText(const SceneModel& model, bool clockMode);
} // namespace TimetableStops

/// The field of the stop dialog that a problem of validateStopDraft() belongs to.
enum class StopDraftField {
	None,			  ///< No field: the stop does not fit the timetable as a whole.
	Position,		  ///< The insert position, which offers no station.
	PlannedArrival,	  ///< The planned arrival.
	PlannedDeparture, ///< The planned departure.
	Dwell,			  ///< The minimum dwell.
	Station,		  ///< The station.
	Platform		  ///< The platform.
};

/// What the fields of the stop dialog hold besides the station and the platform of the draft.
struct StopDraftFields {
	bool hasArrival = false;		 ///< The box of the planned arrival is checked.
	QString arrivalText;			 ///< The text of the planned arrival.
	bool hasDeparture = false;		 ///< The box of the planned departure is checked.
	QString departureText;			 ///< The text of the planned departure.
	QString dwellText;				 ///< The text of the minimum dwell, in seconds.
	bool clockMode = false;			 ///< The planned times are clock times rather than seconds after the base time.
	long long baseOffsetSeconds = 0; ///< The base time in seconds since midnight.
};

/// What validateStopDraft() found: the finished stop, or the first problem with its message and the field to focus.
struct StopDraftCheck {
	bool ok = false;							 ///< True when the stop can be taken.
	SceneStop stop;								 ///< The draft with the planned times and the dwell of the fields, when ok.
	QString message;							 ///< The message of the problem, when not ok. A refused insertion can leave it empty.
	StopDraftField field = StopDraftField::None; ///< The field of the problem, when not ok.
};

/// Checks a stop of the stop dialog in this order: an added stop at a `position` that offers no station, the planned
/// arrival and departure, which must parse when their box is checked and have text, the dwell, a finite number of 0 or
/// more, the station, and a platform that is ambiguous on the route when the stop is added or its station or platform
/// differs from the stop at `position` of `service`. An added stop is then inserted with insertSceneStop() into a copy of
/// `service`, and a refusal is the last problem. `draft` holds the station and the platform, and its planned times are
/// kept where the text of a field still shows them. `service` is not changed.
StopDraftCheck validateStopDraft(const SceneModel& model, const SceneService& service, std::size_t position, bool adding,
	const SceneStop& draft, const StopDraftFields& fields);

/// The dialog that adds a stop to a timetable or edits one: the insert position when it adds, the station, the platform,
/// the planned time display, the planned arrival and departure and the minimum dwell. It works on its own copies of the
/// scene and of the service, which it does not change, and holds nothing of a document. OK takes the stop only when
/// validateStopDraft() finds no problem; otherwise the dialog shows the problem and focuses its field. The station and
/// the platform offered are those that the route reaches there, and a line under them says how the stop resolves.
class StopEditorDialog : public QDialog {
	Q_OBJECT

public:
	/// The dialog of the stop at `stopIndex` of `service`, or, when `adding`, of a new stop at the position `stopIndex`, which
	/// can be from 0 to the number of stops. `service` is a service of `model`. `clockMode` shows the planned times as clock
	/// times at first.
	StopEditorDialog(SceneModel model, SceneService service, std::size_t stopIndex, bool adding, bool clockMode, QWidget* parent = nullptr);

	/// The stop with the fields of the dialog. After the dialog is accepted it is the stop that validateStopDraft() returned.
	const SceneStop& stop() const { return m_draft; }
	/// The position of the stop in the timetable: the one chosen when the dialog adds a stop, else the row of the stop.
	std::size_t position() const { return m_stopIndex; }

private:
	// The route visits offered at the position of a new stop, and why there are none.
	void updateWindow();
	void fillStationChoices();
	// Shows how the stop resolves on its route, and puts that on the station or platform that is not on the route.
	void updateEligibility();
	void refreshPlatforms();
	// An empty text hides the error label.
	void showError(const QString& text);
	void setTimeField(QCheckBox* present, QLineEdit* edit, const std::optional<double>& value);
	void changePosition(int index);
	void changeStation(int index);
	void changePlatform(int index);
	void changeTimeMode(int index);
	void acceptDraft();

	SceneModel m_model;
	SceneService m_service;
	std::size_t m_stopIndex = 0;
	bool m_adding = false;
	// Whether the fields show the planned times as clock times.
	bool m_clockMode = false;
	long long m_baseOffsetSeconds = 0;
	SceneStop m_draft;
	SceneRouteTraversal m_traversal;
	std::string m_windowProblem;
	// The entry of a platform that is not on the route, or -1.
	int m_invalidPlatformIndex = -1;
	ChoiceComboBox* m_positionCombo = nullptr;
	ChoiceComboBox* m_stationCombo = nullptr;
	ChoiceComboBox* m_platformCombo = nullptr;
	QLabel* m_eligibilityLabel = nullptr;
	ChoiceComboBox* m_modeCombo = nullptr;
	QLabel* m_baseLabel = nullptr;
	QCheckBox* m_arrivalPresent = nullptr;
	QLineEdit* m_arrivalEdit = nullptr;
	QCheckBox* m_departurePresent = nullptr;
	QLineEdit* m_departureEdit = nullptr;
	QLineEdit* m_dwellEdit = nullptr;
	QLabel* m_errorLabel = nullptr;
};
