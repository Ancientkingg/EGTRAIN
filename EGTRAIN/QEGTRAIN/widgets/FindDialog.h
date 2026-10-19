#pragma once

#include <QDialog>
#include <QStringList>

class QLineEdit;
class QListWidget;

/// A search field over the names of the items on the canvas. The list shows the names that contain every word of the
/// field, ignoring case; Up and Down choose one, and Enter or a double click hands it back and closes the dialog.
class FindDialog : public QDialog {
	Q_OBJECT

public:
	/// An empty dialog; setLabels gives it the names.
	explicit FindDialog(QWidget* parent = nullptr);

	/// The names to search; the field and the list start empty.
	void setLabels(const QStringList& labels);

signals:
	/// The name the user chose, emitted after the dialog is accepted.
	void chosen(const QString& label);

protected:
	/// Passes Up and Down from the field to the list, and chooses the current name on Enter in the list.
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void filter(const QString& text);
	void choose();

	QStringList m_labels;
	QLineEdit* m_field;
	QListWidget* m_results;
};
