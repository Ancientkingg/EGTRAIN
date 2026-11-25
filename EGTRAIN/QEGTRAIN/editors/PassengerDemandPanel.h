#ifndef PASSENGERDEMANDPANEL_H
#define PASSENGERDEMANDPANEL_H

#include "editors/SceneEditorPanel.h"

#include <QColor>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>
#include <utility>
#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStyledItemDelegate;
class QTableWidget;
class QTableWidgetItem;
struct PassengerDemandTable;
struct SceneDiagnostic;

/// Formatted preview data built from a parsed demand day table, for the preview dialog and tests.
struct DemandDayPreview {
	/// One slice row of the preview table.
	struct SliceRow {
		QString start;			  ///< Start of the slice as HH:MM:SS.
		QString end;			  ///< End of the slice as HH:MM:SS.
		long long passengers = 0; ///< Passengers in the slice.
	};

	std::vector<SliceRow> slices; ///< Slices in time order.
	QStringList unknownStations;  ///< One summary line per skipped unknown station name.
	QString totalText;			  ///< Summary line with total passengers and slice count.
};

/// Builds the preview model shown before importing a demand day table.
DemandDayPreview buildDemandDayPreview(const PassengerDemandTable& table);

/// The range rules of the demand timeline, apart from any widget so that they can be tested and reused by the time fields.
/// Times are seconds since midnight, and a slice lies inside the day.
namespace DemandRanges {

/// A slice as the timeline sees it.
struct Bar {
	QString id;				   ///< Id of the slice.
	double startSeconds = 0.0; ///< Start of the period.
	double endSeconds = 0.0;   ///< End of the period.
};

/// The end of the day, the latest time a slice may reach.
inline constexpr double DayEnd = 86400.0;
/// The grid that a drag on the timeline snaps to: one minute.
inline constexpr double SnapSeconds = 60.0;

/// `seconds` rounded to the nearest whole minute.
double snapToMinute(double seconds);

/// The range for a new slice that is dragged from `anchor` to `other`: both ends are snapped to whole minutes and the range is
/// clamped to the free space around `anchor`, so it never overlaps a bar. Empty when `anchor` lies inside a bar or the
/// clamped range is shorter than one minute.
std::optional<std::pair<double, double>> newRange(const std::vector<Bar>& bars, double anchor, double other);

/// The range for a new slice without a drag: one hour (or less, where the space is short) in the first free space of
/// [from, to] that is at least one minute long, else in the first free space of the day. Empty when the day has none.
std::optional<std::pair<double, double>> firstFreeRange(const std::vector<Bar>& bars, double from, double to);

/// The period of the slice `id` when its start edge (`startEdge`) or its end edge is moved to `seconds`: the edge is clamped
/// to the neighbouring bars, to the day and to at least `minimumLength` from the other edge. `seconds` is not snapped. The
/// period is unchanged when the slice is not among `bars`.
std::pair<double, double> movedEdge(
	const std::vector<Bar>& bars, const QString& id, bool startEdge, double seconds, double minimumLength);

/// "HH:MM:SS" of a time of day; 24:00:00 for the end of the day.
QString clockText(double seconds);
/// "HH:MM" of a time of day.
QString shortClockText(double seconds);
/// Seconds since midnight of "HH:MM:SS" or "HH:MM", up to 24:00:00; empty when the text is not such a time.
std::optional<double> parseClock(const QString& text);

} // namespace DemandRanges

/// The timeline of the demand panel: the case horizon as a time axis with a bar for each demand slice. A click on a bar
/// selects it, a drag over empty space asks for a new slice over the dragged range, and a drag on an edge of the selected
/// bar asks to move that edge. The widget edits nothing itself: it emits what the user asked for, and the panel decides.
/// Slices outside the horizon are drawn as a stub at the edge of the axis.
class DemandTimeline : public QWidget {
	Q_OBJECT

public:
	/// An empty timeline over the whole day.
	explicit DemandTimeline(QWidget* parent = nullptr);

	/// The time axis runs from `startSeconds` to `endSeconds`; an empty range shows the whole day.
	void setHorizon(double startSeconds, double endSeconds);
	/// The slices to draw.
	void setBars(std::vector<DemandRanges::Bar> bars);
	/// Marks the slice with this id as selected; an unknown or empty id marks none.
	void setSelectedId(const QString& id);
	/// False draws the bars but takes no drag.
	void setEditable(bool editable);

	/// The slices that are drawn.
	const std::vector<DemandRanges::Bar>& bars() const { return m_bars; }
	/// The id of the selected slice; empty when none.
	const QString& selectedId() const { return m_selectedId; }
	/// The time at the left end of the axis, in seconds since midnight.
	double horizonStart() const { return m_horizonStart; }
	/// The time at the right end of the axis, in seconds since midnight.
	double horizonEnd() const { return m_horizonEnd; }
	/// The x coordinate of a time on the axis, which is not limited to the widget.
	double xOfSeconds(double seconds) const;
	/// The time at an x coordinate of the widget, limited to the horizon.
	double secondsAtX(double x) const;
	/// The rectangle of the bar of `id` as drawn, empty when there is none. A slice outside the horizon has a stub.
	QRect barRect(const QString& id) const;
	/// A y coordinate inside the area of the bars, where a mouse event reaches them.
	int barsY() const;

	/// A wide strip, one line of marks above the bars.
	QSize sizeHint() const override;
	/// The narrowest strip that still shows the marks.
	QSize minimumSizeHint() const override;

Q_SIGNALS:
	/// The user clicked the bar of this slice.
	void sliceClicked(const QString& id);
	/// The user dragged a new slice over empty space: a period snapped to whole minutes, clear of every bar.
	void sliceDragged(double startSeconds, double endSeconds);
	/// The user moved an edge of the selected slice to `seconds`, snapped to a whole minute and clamped to its neighbours.
	void edgeDragged(const QString& id, bool startEdge, double seconds);
	/// The user pressed Delete or Backspace on the timeline.
	void deleteRequested();

protected:
	/// Draws the axis, the bars and the drag in progress.
	void paintEvent(QPaintEvent* event) override;
	/// Selects a bar, or starts an edge drag or a drag for a new slice.
	void mousePressEvent(QMouseEvent* event) override;
	/// Follows a drag in progress.
	void mouseMoveEvent(QMouseEvent* event) override;
	/// Ends a drag and emits what it asked for.
	void mouseReleaseEvent(QMouseEvent* event) override;
	/// Delete and Backspace ask to delete the selected slice.
	void keyPressEvent(QKeyEvent* event) override;

private:
	enum class Drag { None,
		Create,
		StartEdge,
		EndEdge };

	QRect axisRect() const;
	const DemandRanges::Bar* barById(const QString& id) const;
	const DemandRanges::Bar* barAt(const QPoint& position) const;
	// The period that the drag in progress would give, or none while it gives nothing.
	std::optional<std::pair<double, double>> dragRange() const;
	void updateDescription();

	std::vector<DemandRanges::Bar> m_bars;
	QString m_selectedId;
	double m_horizonStart = 0.0;
	double m_horizonEnd = DemandRanges::DayEnd;
	bool m_editable = true;
	Drag m_drag = Drag::None;
	double m_dragAnchor = 0.0;
	double m_dragCurrent = 0.0;
};

/// The Demand view of the Passengers dock: the origin-destination demand of the case. A timeline shows the demand slices over
/// the case horizon, and a station by station matrix below it edits the passengers of the selected slice, with totals. A
/// slice can be added, deleted and given another period by the timeline or by its start and end fields, and its matrix can
/// be replaced from a CSV file and written to one. Every change is one edit of SceneDocument::Passengers, so one Undo step.
/// Demand belongs to the case and not to a passenger, so this panel stands beside PassengerPanel and does not share its
/// selection. Slices never overlap: an edit that would overlap is clamped to the neighbour.
class PassengerDemandPanel : public SceneEditorPanel {
	Q_OBJECT

public:
	/// Makes the widgets, disabled until a loaded document with stations is set.
	explicit PassengerDemandPanel(QWidget* parent = nullptr);

	/// Shows the slices of the document and keeps the selected slice where it still exists.
	void refresh() override;
	/// Takes the matrix cell and the time field that are being edited into the document.
	void commitPendingEditors() override;

	/// The validation messages that the selected slice shows. The vector must outlive the panel or be replaced first; null
	/// shows none. Call refresh() after the diagnostics have changed.
	void setDiagnostics(const std::vector<SceneDiagnostic>* diagnostics);
	/// Gives the file for Import CSV and Export CSV: `save` is true for Export, and `suggestedName` is then the file name to
	/// propose, the id of the selected slice with .csv. An empty path cancels. Without a chooser both cancel.
	void setCsvFileChooser(std::function<QString(bool save, const QString& suggestedName)> chooser);

	/// Adds a slice over [startSeconds, endSeconds], clamped to the free space around the start, and selects it. False, with
	/// a message in the diagnostic label, when the period leaves no room or the document refuses the edit.
	bool addSlice(double startSeconds, double endSeconds);
	/// Adds a one hour slice in the first free space of the horizon.
	bool addSliceInFreeSpace();
	/// Deletes the selected slice with its cells. False when none is selected or the document refuses.
	bool deleteSelectedSlice();
	/// Selects the slice with this id; false when there is none.
	bool selectSlice(const QString& id);
	/// Replaces the cells of the selected slice by the matrix in CSV `text`. On a problem nothing changes and the diagnostic
	/// label lists the errors; the result is false.
	bool importCsvText(const QString& text);
	/// Reads the file and passes its text to importCsvText(). False, with a message, when the file cannot be read.
	bool importCsvFile(const QString& path);
	/// The selected slice as CSV text in the form that importCsvText() reads; empty when none is selected.
	QString csvText() const;
	/// Writes csvText() to the file. False, with a message, when none is selected or the file cannot be written.
	bool exportCsvFile(const QString& path);
	/// Parses a day demand table from CSV `text`, shows a preview dialog and, when accepted, replaces all demand slices
	/// of the case in one Undo step. On a problem or cancellation nothing changes and the result is false.
	bool importDayText(const QString& text);
	/// Reads the file and passes its text to importDayText(). False, with a message, when the file cannot be read.
	bool importDayFile(const QString& path);
	/// All demand slices of the case as a long-format CSV table in the form that importDayText() reads.
	QString dayTableText() const;
	/// Writes dayTableText() to the file. False, with a message, when there are no slices or the file cannot be written.
	/// After writing, a warning names the slices and cells that the table left out and the times it rounded.
	bool exportDayFile(const QString& path);

	/// The selected slice, or null when none is selected or no scene is loaded.
	const ScenePassengerDemandSlice* selectedSlice() const;

	/// The timeline of the slices.
	DemandTimeline* timeline() const { return m_timeline; }
	/// The slices by period, to select one by keyboard.
	QComboBox* sliceCombo() const { return m_sliceCombo; }
	/// The button that adds a slice in the first free space of the horizon.
	QPushButton* addButton() const { return m_addButton; }
	/// The button that deletes the selected slice.
	QPushButton* deleteButton() const { return m_deleteButton; }
	/// The field of the start of the selected slice, "HH:MM:SS".
	QLineEdit* startEdit() const { return m_startEdit; }
	/// The field of the end of the selected slice, "HH:MM:SS".
	QLineEdit* endEdit() const { return m_endEdit; }
	/// The matrix of the selected slice: a row and a column for each station, then a Total row and a Total column. The cells
	/// on the diagonal and the totals cannot be edited.
	QTableWidget* matrixTable() const { return m_matrix; }
	/// The text of the slice total, below the matrix.
	QLabel* totalLabel() const { return m_totalLabel; }
	/// The button that replaces the matrix of the selected slice from a CSV file.
	QPushButton* importButton() const { return m_importButton; }
	/// The button that writes the matrix of the selected slice to a CSV file.
	QPushButton* exportButton() const { return m_exportButton; }
	/// The button that replaces all demand slices of the case from a long-format CSV table.
	QPushButton* importDayButton() const { return m_importDayButton; }
	/// The button that writes all demand slices of the case to a long-format CSV table.
	QPushButton* exportDayButton() const { return m_exportDayButton; }
	/// The errors for the selected slice, or the help text when there is nothing to report.
	QLabel* diagnosticLabel() const { return m_diagnosticLabel; }
	/// The widget that holds every control. It is disabled while no scene with stations is loaded or a run uses the scene.
	QWidget* body() const { return m_body; }

protected:
	/// Shows the slices again when the passengers or the stations change.
	void documentChanged(SceneDocument::ChangeKinds kinds) override;

private:
	// True when the scene can be edited and has stations, the only case in which the panel changes anything.
	bool canEditDemand() const;
	// The index of the selected slice in the model, or -1.
	int selectedIndex() const;
	// The bars of the slices of the model.
	std::vector<DemandRanges::Bar> modelBars() const;
	// The horizon as a period in seconds since midnight, limited to the day.
	std::pair<double, double> horizon() const;
	void fillMatrix(const ScenePassengerDemandSlice* slice);
	void updateDiagnostics(const ScenePassengerDemandSlice* slice);
	// Shows `text` as an error, or as a warning when `warning` is true, in the diagnostic label until the next refresh.
	void showNotice(const QString& text, bool warning = false);
	// Applies `mutate` to the selected slice in one edit with this undo label. False when none is selected or the document
	// refuses.
	bool editSelected(const QString& label, const std::function<void(ScenePassengerDemandSlice&)>& mutate);
	// Moves an edge of the selected slice, clamped to its neighbours, in one edit.
	bool moveEdge(bool startEdge, double seconds, double minimumLength);
	void commitTime(bool startEdge);
	void commitCell(QTableWidgetItem* item);
	void importCsv();
	void exportCsv();
	void importDay();
	void exportDay();
	QString chooseCsvFile(bool save, const QString& suggestedName = QString());

	const std::vector<SceneDiagnostic>* m_diagnostics = nullptr;
	std::function<QString(bool, const QString&)> m_csvFileChooser;
	QString m_selectedId;
	QString m_notice;
	/// True when m_notice is a warning rather than an error.
	bool m_noticeWarning = false;
	bool m_refreshing = false;
	/// True once fillMatrix() has described the table below, so that the next fill can leave the structure alone.
	bool m_filledLayoutValid = false;
	/// The header labels of the last fill, in order, with "Total" at the end.
	QStringList m_filledLabels;
	/// Whether the cells were editable at the last fill.
	bool m_filledEditable = false;
	/// The colour of the hatched diagonal at the last fill.
	QRgb m_filledHatchColor = 0;
	/// The colour of the totals at the last fill.
	QRgb m_filledTotalColor = 0;

	QWidget* m_body = nullptr;
	DemandTimeline* m_timeline = nullptr;
	QComboBox* m_sliceCombo = nullptr;
	QPushButton* m_addButton = nullptr;
	QPushButton* m_deleteButton = nullptr;
	QLineEdit* m_startEdit = nullptr;
	QLineEdit* m_endEdit = nullptr;
	QTableWidget* m_matrix = nullptr;
	QStyledItemDelegate* m_countDelegate = nullptr;
	QLabel* m_totalLabel = nullptr;
	QPushButton* m_importButton = nullptr;
	QPushButton* m_exportButton = nullptr;
	QPushButton* m_importDayButton = nullptr;
	QPushButton* m_exportDayButton = nullptr;
	QLabel* m_diagnosticLabel = nullptr;
	QLabel* m_horizonLabel = nullptr;
};

#endif // PASSENGERDEMANDPANEL_H
