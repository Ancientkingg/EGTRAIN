#pragma once

#include <QDialog>
#include <QStringList>

class QLineEdit;
class QListWidget;

// A search field over the names of the items on the canvas. The list shows the names that contain every word of the
// field; Up and Down choose one, and Enter or a double click hands it back and closes the dialog.
class FindDialog : public QDialog {
	Q_OBJECT

public:
	explicit FindDialog(QWidget* parent = nullptr);

	// The names to search; the field and the list start empty.
	void setLabels(const QStringList& labels);

signals:
	void chosen(const QString& label);

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void filter(const QString& text);
	void choose();

	QStringList m_labels;
	QLineEdit* m_field;
	QListWidget* m_results;
};
