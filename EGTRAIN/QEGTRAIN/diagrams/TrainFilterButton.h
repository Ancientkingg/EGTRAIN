#ifndef TRAINFILTERBUTTON_H
#define TRAINFILTERBUTTON_H

#include <QColor>
#include <QPair>
#include <QPointer>
#include <QStringList>
#include <QToolButton>
#include <QVector>

class QLineEdit;
class QListWidget;
class QListWidgetItem;

// Toolbar dropdown that lists every train with a checkbox, a search box, and
// All/None shortcuts. The button caption shows how many trains are visible.
// Emits selectionChanged() whenever any visibility flips.
class TrainFilterButton : public QToolButton {
	Q_OBJECT
public:
	explicit TrainFilterButton(QWidget* parent = nullptr);

	// Replaces the listed trains. The colour paints the legend swatch next to
	// each entry; pass an invalid colour for rows without one.
	void setTrains(const QVector<QPair<QString, QColor>>& trains);

	QStringList visibleTrainIds() const;
	bool isTrainVisible(const QString& trainId) const;

signals:
	void selectionChanged();

private slots:
	void applySearchFilter();
	void checkAllVisible(bool checked);

private:
	void updateCaption();

	QPointer<QLineEdit> m_search;
	QPointer<QListWidget> m_list;
	bool m_updating = false;
};

#endif // TRAINFILTERBUTTON_H
