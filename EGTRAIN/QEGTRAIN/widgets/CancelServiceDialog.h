#ifndef CANCELSERVICEDIALOG_H
#define CANCELSERVICEDIALOG_H

#include <QDialog>
#include <QStringList>

class QComboBox;
class QDialogButtonBox;
class QRadioButton;

/// Asks how a train is cancelled during a run: completely from now, or from one of the stations at which it still calls. A
/// train that runs and is cancelled completely ends its run at its next stop; one that has not entered does not enter. The
/// choice of a station is offered only when the train has stops left.
class CancelServiceDialog : public QDialog {
	Q_OBJECT

public:
	/// A dialog for the train, named as the window names it, with the stations that it still calls at, by their names in the
	/// run and in the order of its timetable; a station that it calls at twice is offered once. Complete is chosen.
	CancelServiceDialog(const QString& trainName, const QStringList& stopsAhead, QWidget* parent = nullptr);

	/// The chosen station, or an empty string for a complete cancellation.
	QString fromStation() const;

	/// Chooses a complete cancellation from now.
	QRadioButton* completeButton() const { return m_completeButton; }
	/// Chooses a cancellation from the station of the combo box.
	QRadioButton* fromStationButton() const { return m_fromStationButton; }
	/// The stations at which the train still calls; enabled while fromStationButton is chosen.
	QComboBox* stationCombo() const { return m_stationCombo; }
	/// The OK and Cancel buttons.
	QDialogButtonBox* buttons() const { return m_buttons; }

private:
	QRadioButton* m_completeButton = nullptr;
	QRadioButton* m_fromStationButton = nullptr;
	QComboBox* m_stationCombo = nullptr;
	QDialogButtonBox* m_buttons = nullptr;
};

#endif // CANCELSERVICEDIALOG_H
