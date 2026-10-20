#ifndef CASESETTINGSPANEL_H
#define CASESETTINGSPANEL_H

#include "editors/SceneEditorPanel.h"

#include <QMetaObject>
#include <QPointer>
#include <QString>

class QDoubleSpinBox;
class QLineEdit;

// The six scene-level values: name, description, base time, duration, buffer time and recovery time. Opening the
// panel never changes the model, and every edit takes the same path through commit().
class CaseSettingsPanel : public SceneEditorPanel {
	Q_OBJECT

public:
	explicit CaseSettingsPanel(QWidget* parent = nullptr);

	// Refreshes when the document is replaced.
	void setDocument(SceneDocument* document) override;
	void refresh() override;
	void commitPendingEditors() override;
	// Takes the values of the fields into the document. The fields call it when editing finishes.
	void commit();

	QLineEdit* nameEdit() const { return m_nameEdit; }
	QLineEdit* descriptionEdit() const { return m_descriptionEdit; }
	QLineEdit* baseTimeEdit() const { return m_baseTimeEdit; }
	QDoubleSpinBox* durationSecondsEdit() const { return m_durationSecondsEdit; }
	QDoubleSpinBox* bufferSecondsEdit() const { return m_bufferSecondsEdit; }
	QDoubleSpinBox* recoveryPercentEdit() const { return m_recoveryPercentEdit; }

Q_SIGNALS:
	// The base time of the model has been set to this text. Emitted before the document is marked changed.
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
