#ifndef CASESETTINGSPANEL_H
#define CASESETTINGSPANEL_H

#include "editors/SceneEditorPanel.h"

#include <QString>

#include <optional>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;

/// The scene-level values: name, description, base time (HH:MM:SS), duration and buffer time in seconds, recovery time in
/// percent, the passenger settings and, in the group "Virtual coupling", the settings of levels 3 and 4. Opening the panel never
/// changes the model, and every edit takes the same path through commit().
class CaseSettingsPanel : public SceneEditorPanel {
	Q_OBJECT

public:
	/// Makes the fields, disabled until a loaded document is set.
	explicit CaseSettingsPanel(QWidget* parent = nullptr);
	/// Lets go of the fields first, so that a field that loses the focus later in the teardown edits nothing.
	~CaseSettingsPanel() override;

	void refresh() override;
	/// Commits the text fields and the number field with the focus.
	void commitPendingEditors() override;
	/// Takes the values of the fields into the document, when one differs from what the field shows of the model, as one
	/// change "Edit case settings". The fields call it when editing finishes. A stored number outside the range of its
	/// field, which the field shows at the nearest limit, stays as it is until the user edits it. A duration, buffer or
	/// recovery value that is set this way counts as given in the scene from then on. The list of services that couple is
	/// not taken here: a check in it is taken at once.
	void commit();

	/// Names the values that the command line (-h, -b and -c) replaces in every run of the session, in a line above the
	/// fields. A value without a number is not overridden, and no line shows when none is. The fields keep showing the
	/// values of the case.
	void setCommandLineOverrides(std::optional<double> durationSeconds, std::optional<double> bufferSeconds,
		std::optional<double> recoveryPercent);
	/// The line that names the command-line overrides; hidden while there are none.
	QLabel* overrideNotice() const { return m_overrideNotice; }

	/// The field of the scene name.
	QLineEdit* nameEdit() const { return m_nameEdit; }
	/// The field of the scene description.
	QLineEdit* descriptionEdit() const { return m_descriptionEdit; }
	/// The field of the base time, the clock time HH:MM:SS from which the run and the service, stop and incident times
	/// count. The text is stored as it is typed.
	QLineEdit* baseTimeEdit() const { return m_baseTimeEdit; }
	/// The field of the duration of the run in seconds, 0 or more.
	QDoubleSpinBox* durationSecondsEdit() const { return m_durationSecondsEdit; }
	/// The field of the buffer time in seconds, 0 or more.
	QDoubleSpinBox* bufferSecondsEdit() const { return m_bufferSecondsEdit; }
	/// The field of the recovery time in percent, 0 or more.
	QDoubleSpinBox* recoveryPercentEdit() const { return m_recoveryPercentEdit; }
	/// The field of the most transfers that a live passenger journey plans, 0 to 10.
	QSpinBox* maxTransfersEdit() const { return m_maxTransfersEdit; }
	/// The field of the walking time in seconds per platform of difference at a transfer.
	QSpinBox* walkingSecondsEdit() const { return m_walkingSecondsEdit; }
	/// The field of the delay in seconds up to which passengers plan on the planned times of a train, 0 to 3600.
	QSpinBox* lateThresholdEdit() const { return m_lateThresholdEdit; }
	/// The field of the virtual coupling distance in metres, 1 to 500.
	QDoubleSpinBox* couplingDistanceEdit() const { return m_couplingDistanceEdit; }
	/// The field of the virtual coupling speed tolerance in m/s, 0.01 to 10.
	QDoubleSpinBox* couplingSpeedToleranceEdit() const { return m_couplingSpeedToleranceEdit; }
	/// The field of the margin in metres of the ends of authority that trains report at levels 3 and 4, 1 to 500.
	QDoubleSpinBox* authorityMarginEdit() const { return m_authorityMarginEdit; }
	/// The services of the case, each checked when its trains may couple virtually; with none checked every train may couple.
	/// A listed id that names no service of the case follows them, checked, so that it can be unchecked.
	QListWidget* couplingServiceList() const { return m_couplingServiceList; }
	/// The line under the list of services that says which trains may couple.
	QLabel* couplingServicesNote() const { return m_couplingServicesNote; }
	/// The field of the arrival pattern of demand passengers, Even or Random.
	QComboBox* demandArrivalsCombo() const { return m_demandArrivalsCombo; }

Q_SIGNALS:
	/// The base time of the model has been set to this text. Emitted before the document is marked changed.
	void baseTimeChanged(const QString& baseTime);

protected:
	/// Shows the services of the case again in the list of services that couple when another panel has changed them.
	void documentChanged(SceneDocument::ChangeKinds kinds) override;

private:
	/// Takes the checked services of the list into the document as one change "Edit case settings", when they differ from
	/// the services that couple in the model in more than their order. Only a check in the list calls it: the list always
	/// shows the model, so commit() and commitPendingEditors() never take it, and an edit of another panel, such as the
	/// rename of a service, is not undone by the list.
	void commitCouplingServices();

	QLabel* m_overrideNotice = nullptr;
	QLineEdit* m_nameEdit = nullptr;
	QLineEdit* m_descriptionEdit = nullptr;
	QLineEdit* m_baseTimeEdit = nullptr;
	QDoubleSpinBox* m_durationSecondsEdit = nullptr;
	QDoubleSpinBox* m_bufferSecondsEdit = nullptr;
	QDoubleSpinBox* m_recoveryPercentEdit = nullptr;
	QSpinBox* m_maxTransfersEdit = nullptr;
	QSpinBox* m_walkingSecondsEdit = nullptr;
	QSpinBox* m_lateThresholdEdit = nullptr;
	QDoubleSpinBox* m_couplingDistanceEdit = nullptr;
	QDoubleSpinBox* m_couplingSpeedToleranceEdit = nullptr;
	QDoubleSpinBox* m_authorityMarginEdit = nullptr;
	QListWidget* m_couplingServiceList = nullptr;
	QLabel* m_couplingServicesNote = nullptr;
	QComboBox* m_demandArrivalsCombo = nullptr;
};

#endif // CASESETTINGSPANEL_H
