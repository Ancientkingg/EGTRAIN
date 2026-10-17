#ifndef PASSENGERPANEL_H
#define PASSENGERPANEL_H

#include "editors/SceneEditorPanel.h"
#include "widgets/UserPrompts.h"

#include <QMetaObject>
#include <QPointer>
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

// The passengers of the scene, the journeys of the selected passenger and the ordered legs of the selected journey,
// and the import of the legacy passenger files. A passenger list, a journey list and a leg list edit the model of the
// document in place; nothing is copied.
class PassengerPanel : public SceneEditorPanel {
	Q_OBJECT

public:
	explicit PassengerPanel(QWidget* parent = nullptr);

	// Refreshes when the document is replaced.
	void setDocument(SceneDocument* document) override;
	// Shows the passengers of the document and keeps the selected rows where they still exist.
	void refresh() override;
	void commitPendingEditors() override;

	// The questions and errors of the panel go through these prompts, which the caller keeps alive. Without prompts
	// the panel shows message boxes.
	void setPrompts(UserPrompts* prompts);
	// The validation messages that the selection shows. The vector must outlive the panel or be replaced first; null
	// shows none. Call refresh() after the diagnostics have changed.
	void setDiagnostics(const std::vector<SceneDiagnostic>* diagnostics);
	// The time in seconds over which the occurrences of a service are counted. Without one the duration of the scene
	// settings is used.
	void setOccurrenceDuration(std::function<double()> duration);
	// Gives the folder with the legacy passenger files to Import; an empty path cancels. Without one Import asks with a
	// folder dialog.
	void setImportFolderChooser(std::function<QString()> chooser);
	// Empties the table with the result of the latest import.
	void clearImportResult();

	ScenePassenger* selectedPassenger();
	ScenePassengerJourney* selectedPassengerJourney();
	ScenePassengerLeg* selectedPassengerLeg();

	QListWidget* passengerList() const { return m_passengerList; }
	QLineEdit* passengerIdEdit() const { return m_passengerIdEdit; }
	QPushButton* addPassengerButton() const { return m_addPassengerButton; }
	QPushButton* deletePassengerButton() const { return m_deletePassengerButton; }
	QPushButton* importButton() const { return m_importButton; }
	QLabel* importResultHeading() const { return m_importResultHeading; }
	QTableWidget* importResultTable() const { return m_importResultTable; }
	QLabel* diagnosticLabel() const { return m_diagnosticLabel; }
	QTabWidget* tabs() const { return m_tabs; }
	QListWidget* journeyList() const { return m_journeyList; }
	QPushButton* addJourneyButton() const { return m_addJourneyButton; }
	QPushButton* deleteJourneyButton() const { return m_deleteJourneyButton; }
	QLineEdit* journeyIdEdit() const { return m_journeyIdEdit; }
	QLineEdit* journeyActivityEdit() const { return m_journeyActivityEdit; }
	QComboBox* journeyOriginCombo() const { return m_journeyOriginCombo; }
	QComboBox* journeyDestinationCombo() const { return m_journeyDestinationCombo; }
	const std::array<QDoubleSpinBox*, 4>& journeyWindowEdits() const { return m_journeyWindowEdits; }
	QListWidget* legList() const { return m_legList; }
	QPushButton* addLegButton() const { return m_addLegButton; }
	QPushButton* deleteLegButton() const { return m_deleteLegButton; }
	QPushButton* moveLegUpButton() const { return m_moveLegUpButton; }
	QPushButton* moveLegDownButton() const { return m_moveLegDownButton; }
	QLineEdit* legIdEdit() const { return m_legIdEdit; }
	QComboBox* legOriginCombo() const { return m_legOriginCombo; }
	QComboBox* legDestinationCombo() const { return m_legDestinationCombo; }
	QComboBox* legServiceCombo() const { return m_legServiceCombo; }
	QSpinBox* legOccurrenceEdit() const { return m_legOccurrenceEdit; }

Q_SIGNALS:
	// A message for the status bar, which it shows for `timeoutMs` milliseconds.
	void statusMessage(const QString& text, int timeoutMs);

private:
	bool canEdit() const;
	double occurrenceDuration() const;

	void updatePassengerDetail();
	void updateJourneyDetail();
	void updateLegDetail();

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

	QPointer<SceneDocument> m_document;
	QMetaObject::Connection m_replacedConnection;
	UserPrompts m_defaultPrompts;
	UserPrompts* m_prompts = &m_defaultPrompts;
	const std::vector<SceneDiagnostic>* m_diagnostics = nullptr;
	std::function<double()> m_occurrenceDuration;
	std::function<QString()> m_importFolderChooser;

	QListWidget* m_passengerList = nullptr;
	QPushButton* m_addPassengerButton = nullptr;
	QPushButton* m_deletePassengerButton = nullptr;
	QPushButton* m_importButton = nullptr;
	QLineEdit* m_passengerIdEdit = nullptr;
	QLabel* m_diagnosticLabel = nullptr;
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
