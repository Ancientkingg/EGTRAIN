#ifndef TIMETABLESTOPSPANEL_H
#define TIMETABLESTOPSPANEL_H

#include "editors/SceneEditorPanel.h"

#include <QMetaObject>
#include <QPointer>
#include <QString>

#include <string>

class QComboBox;
class QEvent;
class QLabel;
class QPushButton;
class QTableWidget;

/// The timetable of one service: its stops in order, with the station, the platform, the dwell and the planned
/// times, and the buttons that add, remove and move them. A stop is added or edited in a dialog on a copy and enters
/// the service only when the stop still fits the route. The service is the one whose id the panel is given. The five
/// columns fill the width of the table down to their minimum widths: the platform and the times take what their
/// header and a typical value need and the station takes the rest. A table narrower than that shortens the columns,
/// and long text is elided. The minimum dwell is in seconds, and the planned arrival and departure are seconds from
/// the base time, shown either as such or as clock times; a planned time can be absent.
class TimetableStopsPanel : public SceneEditorPanel {
	Q_OBJECT

public:
	/// Makes the widgets, empty until a loaded document and a service are set.
	explicit TimetableStopsPanel(QWidget* parent = nullptr);

	/// Refreshes when the document is replaced.
	void setDocument(SceneDocument* document) override;
	/// Shows the stops of the service with this id, or none when the id is empty or no service has it.
	void setServiceId(const QString& serviceId);
	/// Lists the stops of the service again.
	void refresh() override;
	/// The table has no field in which text is being typed.
	void commitPendingEditors() override {}

	/// Opens the dialog for a new stop at the last position where the route reaches a station, or tells the user that
	/// there is none.
	void addStop();
	/// Opens the dialog for a new stop after the selected one.
	void insertStopAfterSelected();
	/// Removes the selected stop.
	void removeStop();
	/// Moves the selected stop one place earlier.
	void moveStopUp();
	/// Moves the selected stop one place later.
	void moveStopDown();
	/// Opens the dialog of the stop in this row of the table. A negative row adds a stop at `insertIndex`, or at the end
	/// when that is negative.
	void editStop(int row, int insertIndex = -1);

	/// The stops: station, stop platform, minimum dwell in seconds, planned arrival and planned departure.
	QTableWidget* table() const { return m_table; }
	/// The button of addStop().
	QPushButton* addButton() const { return m_addButton; }
	/// The button of insertStopAfterSelected().
	QPushButton* insertAfterButton() const { return m_insertAfterButton; }
	/// The button of removeStop().
	QPushButton* removeButton() const { return m_removeButton; }
	/// The button of moveStopUp().
	QPushButton* moveUpButton() const { return m_moveUpButton; }
	/// The button of moveStopDown().
	QPushButton* moveDownButton() const { return m_moveDownButton; }
	/// Whether the planned times are shown as elapsed seconds from the base time or as clock times.
	QComboBox* timeModeCombo() const { return m_timeModeCombo; }
	/// The base time that the planned times count from.
	QLabel* timeBaseLabel() const { return m_timeBaseLabel; }
	/// The message above the table when there is no service or it has no stops, hidden otherwise.
	QLabel* emptyStateLabel() const { return m_emptyLabel; }

Q_SIGNALS:
	/// The table shows the stops again, so what depends on the timetable of the services is out of date.
	void timetableRefreshed();

private:
	// Fits the columns again when the table is resized or its font changes.
	bool eventFilter(QObject* watched, QEvent* event) override;
	bool hasScene() const;
	// False while a run uses the scene.
	bool canEdit() const;
	// The position of the shown service in the model, or -1.
	int serviceRow() const;
	void updateActions();
	// Gives the columns widths that add up to the width of the table, without a horizontal scroll bar.
	void fitColumns();

	QPointer<SceneDocument> m_document;
	QMetaObject::Connection m_replacedConnection;
	std::string m_serviceId;
	// Whether the planned times are shown as clock times rather than as elapsed seconds.
	bool m_clockMode = false;
	QComboBox* m_timeModeCombo = nullptr;
	QLabel* m_timeBaseLabel = nullptr;
	QLabel* m_emptyLabel = nullptr;
	QTableWidget* m_table = nullptr;
	QPushButton* m_addButton = nullptr;
	QPushButton* m_insertAfterButton = nullptr;
	QPushButton* m_removeButton = nullptr;
	QPushButton* m_moveUpButton = nullptr;
	QPushButton* m_moveDownButton = nullptr;
};

#endif // TIMETABLESTOPSPANEL_H
