#ifndef TRAINKEYLIST_H
#define TRAINKEYLIST_H

#include <QColor>
#include <QHash>
#include <QList>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

/// The Trains section of a diagram window: the train filter and the colour key in one list. Each row has a check box
/// that shows or hides the train, a line sample in its colour and its id. More than six trains in six services or fewer
/// are grouped by service, with a group row whose check box shows and hides the trains of the group and is partly
/// checked while some of them are shown, and whose line sample has the colour that its shown trains share, or while
/// none is shown the colour that all its trains share; other lists are flat. The selected train comes first and is
/// highlighted, then the shown trains, then the hidden ones, and groups follow the same order; while the pointer is
/// over the list or the list has the focus the rows keep their places, and they move once it leaves. Above 6 trains All
/// and None show or hide the trains of the rows in the list, and above 12 a search field beside them narrows the rows;
/// a group then counts and acts on its trains that the search shows, and Escape in the field clears it. The count of
/// shown trains, such as "2 of 2", is a label that the panel puts on the heading row of the section.
class TrainKeyList : public QWidget {
	Q_OBJECT
public:
	/// One train of the list.
	struct Train {
		QString id;		 ///< The id of the train, which is also the group id of its series
		QColor colour;	 ///< The colour of its line sample; invalid for none
		QString service; ///< The service the train runs; empty when unknown, which makes the train a service of its own
	};
	/// An empty list.
	explicit TrainKeyList(QWidget* parent = nullptr);
	/// Lists `trains` in this order before the sorting. A train that the list already has keeps its check state, a new
	/// one is shown, and the selected train stays selected while it is listed. Changes no check state, so emits nothing.
	void setTrains(const QVector<Train>& trains);
	/// Gives the listed trains the colours of their line samples, by id, and keeps the rows, their order, their check
	/// states and the open groups as they are; a train that `colours` leaves out has no line sample. Emits nothing.
	void setColours(const QHash<QString, QColor>& colours);
	/// The ids of the listed trains, in the order setTrains gave them.
	QStringList trainIds() const;
	/// The ids of the shown trains, in the order setTrains gave them.
	QStringList visibleTrainIds() const;
	/// The service that groups the train: the one setTrains gave, else the id of the train; empty for a train the list
	/// does not have.
	QString serviceOf(const QString& trainId) const { return m_services.value(trainId); }
	/// Whether the train is shown; true for a train the list does not have.
	bool isTrainVisible(const QString& trainId) const;
	/// Shows or hides one train and emits visibilityChanged when that changes its state.
	void setTrainVisible(const QString& trainId, bool visible);
	/// Shows or hides the trains of the rows that the search shows, as All and None do, and emits visibilityChanged once.
	void setSearchedVisible(bool visible);
	/// Highlights the row of the train and brings it first; an empty id removes the highlight. Emits nothing.
	void setSelectedTrain(const QString& trainId);
	/// True while the trains are grouped by service.
	bool isGrouped() const { return m_grouped; }
	/// The rows of the shown trains as an exported figure lists them: in a grouped list each service with a shown train,
	/// with the colour that its shown trains share, or, where they differ, each of them; else each shown train with its
	/// colour.
	QVector<QPair<QString, QColor>> shownRows() const;
	/// The label with the count of shown trains, "2 of 2", for the heading row of the section.
	QLabel* countLabel() const { return m_count; }
	/// The tree of the rows.
	QTreeWidget* tree() const { return m_tree; }
	/// The search field, shown above 12 trains. All and None show above 6.
	QLineEdit* searchField() const { return m_search; }
	/// The widgets of the list that take the focus, in the order Tab goes through them: the search field, All, None and
	/// the rows. Tab passes over those that are hidden.
	QList<QWidget*> focusChain() const;

signals:
	/// A check box of the list showed or hid trains.
	void visibilityChanged();

protected:
	/// Keeps the rows in place while the pointer is over the list or the list has the focus, and sorts them once both
	/// have left. Escape in a search field with text clears it.
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void itemChanged(QTreeWidgetItem* item);
	void applySearch();
	void sortRows();
	void sortSoon();
	void updateGroupState(QTreeWidgetItem* group);
	void updateCount();
	void fitHeight();
	int rank(const QTreeWidgetItem* item) const;

	// Children of the list, which live as long as it does.
	QLineEdit* m_search = nullptr;
	QPushButton* m_all = nullptr;
	QPushButton* m_none = nullptr;
	QWidget* m_searchRow = nullptr;
	QTreeWidget* m_tree = nullptr;
	QLabel* m_count = nullptr;
	QStringList m_order;					  // the ids in the order setTrains gave them
	QHash<QString, QTreeWidgetItem*> m_items; // the row of each train
	QHash<QString, QColor> m_colours;		  // the colour of each train
	QHash<QString, QString> m_services;		  // the service of each train
	QString m_selected;						  // the id of the selected train
	QSet<QString> m_openedBySearch;			  // the groups that the search opened, which close when it is cleared
	bool m_grouped = false;
	bool m_updating = false;	// the list changes check states itself
	bool m_hovered = false;		// the pointer is over the rows
	bool m_sortPending = false; // the rows wait to be sorted until the pointer and the focus leave
};

#endif // TRAINKEYLIST_H
