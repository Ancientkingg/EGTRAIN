#ifndef CASESETTINGSPANEL_H
#define CASESETTINGSPANEL_H

#include "editors/SceneEditorPanel.h"

#include <QMetaObject>
#include <QPointer>
#include <QString>

class QDoubleSpinBox;
class QLineEdit;

/// The six scene-level values: name, description, base time (HH:MM:SS), duration and buffer time in seconds and recovery
/// time in percent. Opening the panel never changes the model, and every edit takes the same path through commit().
class CaseSettingsPanel : public SceneEditorPanel {
	Q_OBJECT

public:
	/// Makes the fields, disabled until a loaded document is set.
	explicit CaseSettingsPanel(QWidget* parent = nullptr);
	/// Lets go of the fields first, so that a field that loses the focus later in the teardown edits nothing.
	~CaseSettingsPanel() override;

	/// Refreshes when the document is replaced.
	void setDocument(SceneDocument* document) override;
	void refresh() override;
	/// Commits the text fields and the number field with the focus.
	void commitPendingEditors() override;
	/// Takes the values of the fields into the document, when one differs from what the field shows of the model, as one
	/// change "Edit case settings". The fields call it when editing finishes. A stored number outside the range of its
	/// field, which the field shows at the nearest limit, stays as it is until the user edits it. A duration, buffer or
	/// recovery value that is set this way counts as given in the scene from then on.
	void commit();

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

Q_SIGNALS:
	/// The base time of the model has been set to this text. Emitted before the document is marked changed.
	void baseTimeChanged(const QString& baseTime);

private:
	QPointer<SceneDocument> m_document;
	QMetaObject::Connection m_replacedConnection;
	QLineEdit* m_nameEdit = nullptr;
	QLineEdit* m_descriptionEdit = nullptr;
	QLineEdit* m_baseTimeEdit = nullptr;
	QDoubleSpinBox* m_durationSecondsEdit = nullptr;
	QDoubleSpinBox* m_bufferSecondsEdit = nullptr;
	QDoubleSpinBox* m_recoveryPercentEdit = nullptr;
};

#endif // CASESETTINGSPANEL_H
