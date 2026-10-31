#ifndef PASSENGERPANEL_H
#define PASSENGERPANEL_H

#include "editors/SceneEditorPanel.h"

#include <QString>

#include <array>
#include <functional>
#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTableWidget;
struct SceneDiagnostic;

/// The passengers of the scene, the journeys of the selected passenger and the ordered legs of the selected journey,
/// and the import of the legacy passenger files. A passenger list, a journey list and a leg list edit the model of the
/// document in place; nothing is copied. The planned departure and arrival windows of a journey are in seconds since
/// midnight, not from the base time. A leg names one occurrence of a service, counted from 1.
class PassengerPanel : public SceneEditorPanel {
	Q_OBJECT

public:
	/// Makes the widgets, empty until a loaded document is set.
	explicit PassengerPanel(QWidget* parent = nullptr);

	/// Shows the passengers of the document and keeps the selected rows where they still exist.
	void refresh() override;
	/// Takes the ids, the activity and the numbers that are being edited into the document.
	void commitPendingEditors() override;

	/// The validation messages that the selection shows. The vector must outlive the panel or be replaced first; null
	/// shows none. Call refresh() after the diagnostics have changed.
	void setDiagnostics(const std::vector<SceneDiagnostic>* diagnostics);
	/// Gives the folder with the legacy passenger files to Import; an empty path cancels. Without one Import asks with a
	/// folder dialog.
	void setImportFolderChooser(std::function<QString()> chooser);
	/// Empties the table with the result of the latest import.
	void clearImportResult();

	/// The selected passenger, or null when none is selected or no scene is loaded.
	const ScenePassenger* selectedPassenger() const;
	/// The selected journey of the selected passenger, or null.
	const ScenePassengerJourney* selectedPassengerJourney() const;
	/// The selected leg of the selected journey, or null.
	const ScenePassengerLeg* selectedPassengerLeg() const;

	/// The passengers, by id.
	QListWidget* passengerList() const { return m_passengerList; }
	/// The field of the id of the selected passenger. An empty or used id is refused.
	QLineEdit* passengerIdEdit() const { return m_passengerIdEdit; }
	/// The button that adds a passenger without journeys.
	QPushButton* addPassengerButton() const { return m_addPassengerButton; }
	/// The button that deletes the selected passenger and its journeys after the user has confirmed it.
	QPushButton* deletePassengerButton() const { return m_deletePassengerButton; }
	/// The button that appends the passengers of a legacy passenger folder. A passenger whose id, or the id of one of its
	/// journeys or legs, is already used is left out.
	QPushButton* importButton() const { return m_importButton; }
	/// The heading of the import result table.
	QLabel* importResultHeading() const { return m_importResultHeading; }
	/// The rows of the latest import: source, row, passenger, status and detail.
	QTableWidget* importResultTable() const { return m_importResultTable; }
	/// The errors for the selection, or the help text when there is nothing to report.
	QLabel* diagnosticLabel() const { return m_diagnosticLabel; }
	/// The warnings and notes for the selection.
	QLabel* warningLabel() const { return m_warningLabel; }
	/// The Journeys and Legs pages of the selected passenger, hidden while none is selected.
	QTabWidget* tabs() const { return m_tabs; }
	/// The journeys of the selected passenger, by id and activity.
	QListWidget* journeyList() const { return m_journeyList; }
	/// The button that adds a journey to the selected passenger.
	QPushButton* addJourneyButton() const { return m_addJourneyButton; }
	/// The button that deletes the selected journey and its legs after the user has confirmed it.
	QPushButton* deleteJourneyButton() const { return m_deleteJourneyButton; }
	/// The field of the id of the selected journey, unique in the scene.
	QLineEdit* journeyIdEdit() const { return m_journeyIdEdit; }
	/// The field of the activity of the selected journey; it may be empty.
	QLineEdit* journeyActivityEdit() const { return m_journeyActivityEdit; }
	/// The origin station of the selected journey, with the station id as the data.
	QComboBox* journeyOriginCombo() const { return m_journeyOriginCombo; }
	/// The destination station of the selected journey, with the station id as the data.
	QComboBox* journeyDestinationCombo() const { return m_journeyDestinationCombo; }
	/// The planned departure start and end and arrival start and end of the selected journey, in this order, in seconds
	/// since midnight.
	const std::array<QDoubleSpinBox*, 4>& journeyWindowEdits() const { return m_journeyWindowEdits; }
	/// The legs of the selected journey in order, by id.
	QListWidget* legList() const { return m_legList; }
	/// The button that appends a leg to the selected journey.
	QPushButton* addLegButton() const { return m_addLegButton; }
	/// The button that deletes the selected leg after the user has confirmed it.
	QPushButton* deleteLegButton() const { return m_deleteLegButton; }
	/// The button that moves the selected leg one place earlier.
	QPushButton* moveLegUpButton() const { return m_moveLegUpButton; }
	/// The button that moves the selected leg one place later.
	QPushButton* moveLegDownButton() const { return m_moveLegDownButton; }
	/// The field of the id of the selected leg, unique in the scene.
	QLineEdit* legIdEdit() const { return m_legIdEdit; }
	/// The station where the selected leg boards, with the station id as the data.
	QComboBox* legOriginCombo() const { return m_legOriginCombo; }
	/// The station where the selected leg alights, with the station id as the data.
	QComboBox* legDestinationCombo() const { return m_legDestinationCombo; }
	/// The service of the selected leg, with the service id as the data.
	QComboBox* legServiceCombo() const { return m_legServiceCombo; }
	/// The occurrence of the service of the selected leg, counted from 1. The field also takes 0, which validation
	/// reports as an error.
	QSpinBox* legOccurrenceEdit() const { return m_legOccurrenceEdit; }

Q_SIGNALS:
	/// A message for the status bar, which it shows for `timeoutMs` milliseconds.
	void statusMessage(const QString& text, int timeoutMs);

protected:
	/// Shows the passengers again when the case settings, the infrastructure or the services change.
	void documentChanged(SceneDocument::ChangeKinds kinds) override;

private:
	// The row of the selected passenger, or -1 when the list has no selection.
	int selectedPassengerRow() const;

	void updatePassengerDetail();
	void updateJourneyDetail();
	void updateLegDetail();
	// Applies `mutate` to the selected passenger, journey or leg, which there must be, in one edit of the document. False
	// when the document refuses the edit.
	bool editPassenger(const QString& label, const std::function<void(ScenePassenger&)>& mutate);
	bool editJourney(const QString& label, const std::function<void(ScenePassengerJourney&)>& mutate);
	bool editLeg(const QString& label, const std::function<void(ScenePassengerLeg&)>& mutate);

	void addPassenger();
	void deletePassenger();
	void addJourney();
	void deleteJourney();
	void addLeg();
	void deleteLeg();
	void moveLeg(int offset);
	void importPassengers();

	void commitPassengerId();
	void commitJourneyId();
	void commitJourneyActivity();
	void commitJourneyStation(bool origin);
	void commitJourneyWindow(int index);
	void commitLegId();
	void commitLegStation(bool origin);
	void commitLegService();
	void commitLegOccurrence();

	const std::vector<SceneDiagnostic>* m_diagnostics = nullptr;
	std::function<QString()> m_importFolderChooser;

	QListWidget* m_passengerList = nullptr;
	QPushButton* m_addPassengerButton = nullptr;
	QPushButton* m_deletePassengerButton = nullptr;
	QPushButton* m_importButton = nullptr;
	QLineEdit* m_passengerIdEdit = nullptr;
	QLabel* m_diagnosticLabel = nullptr;
	QLabel* m_warningLabel = nullptr;
	QTabWidget* m_tabs = nullptr;
	QListWidget* m_journeyList = nullptr;
	QPushButton* m_addJourneyButton = nullptr;
	QPushButton* m_deleteJourneyButton = nullptr;
	QLineEdit* m_journeyIdEdit = nullptr;
	QLineEdit* m_journeyActivityEdit = nullptr;
	QComboBox* m_journeyOriginCombo = nullptr;
	QComboBox* m_journeyDestinationCombo = nullptr;
	std::array<QDoubleSpinBox*, 4> m_journeyWindowEdits{};
	QListWidget* m_legList = nullptr;
	QPushButton* m_addLegButton = nullptr;
	QPushButton* m_deleteLegButton = nullptr;
	QPushButton* m_moveLegUpButton = nullptr;
	QPushButton* m_moveLegDownButton = nullptr;
	QLineEdit* m_legIdEdit = nullptr;
	QComboBox* m_legOriginCombo = nullptr;
	QComboBox* m_legDestinationCombo = nullptr;
	QComboBox* m_legServiceCombo = nullptr;
	QSpinBox* m_legOccurrenceEdit = nullptr;
	QLabel* m_importResultHeading = nullptr;
	QTableWidget* m_importResultTable = nullptr;
};

#endif // PASSENGERPANEL_H
