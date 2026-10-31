#ifndef RESULTFRAME_H
#define RESULTFRAME_H

#include <QList>
#include <QPointer>
#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>
#include <optional>
#include <string>

class QChart;
class QDialog;
class QDialogButtonBox;
class QEvent;
class QFrame;
class QHBoxLayout;
class QImage;
class QLabel;
class QPalette;
class QResizeEvent;
class QShowEvent;
class QSplitter;
class QTableView;
class QToolButton;
class QVBoxLayout;
struct RunProvenance;
namespace telemetry {
enum class ExportKind;
class OperationObservation;
} // namespace telemetry

/// The side panel of a result window: titled sections in a column that scrolls when it is taller than the window,
/// with a rule between the sections that are shown, and a cursor readout below the column.
class ResultPanel : public QWidget {
	Q_OBJECT
public:
	/// An empty panel with its readout.
	explicit ResultPanel(QWidget* parent = nullptr);
	/// Adds a section below the others: a heading with `title` and, at its right, `headingWidget` when given, and
	/// `content` below the heading. A section without a title and without a heading widget has no heading. The panel
	/// owns both widgets. Returns the section, which setSectionVisible takes.
	QWidget* addSection(const QString& title, QWidget* content, QWidget* headingWidget = nullptr);
	/// Shows or hides a section of the panel, with its rule.
	void setSectionVisible(QWidget* section, bool visible);
	/// The plain-text label at the bottom of the panel, below the column of sections; empty until a window sets it.
	QLabel* readout() const { return m_readout; }
	/// Makes the panel at least as tall as its sections, so that it does not scroll them itself. A panel in a body that
	/// scrolls as a whole, such as the body of a dialog of DialogLayout, needs it, so that the body scrolls to the sections.
	void keepSectionsWhole();
	/// The minimum size of the layout; after keepSectionsWhole, at least the height of the sections and the readout.
	QSize minimumSizeHint() const override;

protected:
	/// Measures the panel again when the sections change their size after keepSectionsWhole.
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void updateRules();

	struct Section {
		QWidget* widget;
		QFrame* rule; // above the section, shown when an earlier section is shown too
	};
	QVector<Section> m_sections;
	QWidget* m_body = nullptr;
	QVBoxLayout* m_column = nullptr;
	QLabel* m_readout = nullptr;
	bool m_sectionsWhole = false;
};

/// A key figure in the panel of a result window: a large value over a small caption, such as "471.5 s" over "Cycle".
/// Both are cut at the right in a narrow panel; the tooltip has them whole.
class ResultFigure : public QWidget {
	Q_OBJECT
public:
	/// An empty figure with the object name `name`.
	explicit ResultFigure(const QString& name, QWidget* parent = nullptr);
	/// Shows `value` over `caption`. The tooltip has the value and the caption, then `toolTip` when given.
	void setFigure(const QString& value, const QString& caption, const QString& toolTip = QString());
	/// The value, such as "471.5 s".
	QString value() const;
	/// The caption under the value, such as "Cycle".
	QString caption() const;

protected:
	/// Keeps the value larger than the font of the figure when that font changes.
	void changeEvent(QEvent* event) override;

private:
	void scaleValue();

	QLabel* m_value = nullptr;
	QLabel* m_caption = nullptr;
};

/// The frame of a result window. A splitter holds the main widget, with a header row above it, and the panel beside
/// both. The header shows the context of the window in one line at the left and tool buttons at the right, the last of
/// which shows and hides the panel. The panel is 260 px wide, or 200 px while the window is narrower than 1000 px, until
/// the user drags the splitter, which stops at 200 px; the main widget takes the rest, and the splitter cannot collapse
/// either of them.
class ResultFrame : public QWidget {
	Q_OBJECT
public:
	/// A frame around `main`, which it owns. The context label is named `name` + "Context" and the panel button `name` +
	/// "PanelToggle". The names are given at once, since the style sheet keeps the rules that matched the first name of a
	/// widget.
	ResultFrame(QWidget* main, const QString& name, QWidget* parent = nullptr);
	/// Shows `text` in the header in one line, elided at the right. The tooltip has the whole text, and `toolTip` on the
	/// lines below it when given. An empty text hides the context.
	void setContext(const QString& text, const QString& toolTip = QString());
	/// The label of the context, a plain-text label whose text() is the whole text.
	QLabel* contextLabel() const;
	/// Adds a tool button at the right of the header, before the panel button. Without its icon, which the diagram
	/// library cannot load when the application resources are not linked, the button shows `text`, which is also its
	/// accessible name.
	QToolButton* addHeaderButton(const QString& objectName, const QString& text, const QString& iconPath, const QString& toolTip);
	/// Adds `widget` between the header and the main widget, such as a warning.
	void addAboveMain(QWidget* widget);
	/// The panel at the right of the splitter.
	ResultPanel* panel() const { return m_panel; }
	/// The checkable header button that shows and hides the panel.
	QToolButton* panelButton() const { return m_panelButton; }
	/// Lays out `dialog` with DialogLayout::install, with the frame as its body from edge to edge and `buttons` as the
	/// footer, which keeps the margins of the dialog. The heading becomes the window title. The panel keeps its sections
	/// whole, so that the body scrolls when the window is shorter than the panel.
	void installInDialog(QDialog& dialog, const QString& heading, QDialogButtonBox* buttons);

protected:
	/// Fits the width of the panel to the new width of the window.
	void resizeEvent(QResizeEvent* event) override;
	/// Fits the width of the panel to the width the window opens with.
	void showEvent(QShowEvent* event) override;

private:
	void fitPanel();

	QFrame* m_header = nullptr;
	QHBoxLayout* m_headerLayout = nullptr;
	QVBoxLayout* m_mainColumn = nullptr;
	QLabel* m_context = nullptr;
	QSplitter* m_splitter = nullptr;
	ResultPanel* m_panel = nullptr;
	QToolButton* m_panelButton = nullptr;
	bool m_userSized = false; // the user dragged the splitter, so the panel keeps the width the user gave it
};

/// Raises the window of a widget and makes it active again when the guard ends, so that a result window stays in front
/// after the save dialog of an export and the messages that follow it, saved or cancelled. On macOS the save dialog runs
/// application modal, and when it closes AppKit gives the key window to the main window. A result window is a dialog of
/// the main window, which Qt makes a panel that cannot become the main window of AppKit, so it would drop behind.
class KeepInFront {
public:
	/// Keeps the window of `widget` in front once the guard ends; a widget that is gone by then is left alone.
	explicit KeepInFront(QWidget* widget);
	~KeepInFront();
	KeepInFront(const KeepInFront&) = delete;
	KeepInFront& operator=(const KeepInFront&) = delete;
	KeepInFront(KeepInFront&&) = delete;
	KeepInFront& operator=(KeepInFront&&) = delete;

private:
	QPointer<QWidget> m_widget;
};

/// The look of the charts of the result windows: square corners, the base colour of `palette` behind the plot, margins of
/// 12 px at the sides and 8 px above and below, a quiet grid and axis lines, and the text colour for the labels and the
/// titles of the axes.
void styleResultChart(QChart* chart, const QPalette& palette);

/// Writes the content of an export to `path` as a file of the kind `suffix` names, "csv" or "png", and returns whether
/// it did, such as writeRunArtifactWithProvenance.
using ExportWriter = std::function<bool(const QString& path, const char* suffix, const std::string& bytes)>;

/// Exports a CSV file or a PNG image, as `kind` says, through the save dialog of `parent`: titled `title`, starting at
/// `suggestedName` in the folder of dialog_folders::suggestedPath, with a filter for the kind. A path without the suffix
/// of the kind gets it. After the dialog `bytes` gives the content, or nothing when it cannot be made, which reports an
/// internal failure of the export. `write` writes the content, or else it is written atomically without provenance;
/// empty content is not written. A failed write reports a failed export. Either failure shows "Export failed" with the
/// path as the person reads it. The window of `parent` stays in front (KeepInFront). Returns the path that was written,
/// or an empty string when the dialog was cancelled or nothing was written.
QString saveExportInteractive(QWidget* parent, telemetry::ExportKind kind, const QString& title, const QString& suggestedName,
	const telemetry::OperationObservation& operation, const std::function<std::optional<std::string>()>& bytes,
	const ExportWriter& write = {});

/// The content of a PNG export of `image`, for saveExportInteractive; nothing when the image cannot be encoded.
std::optional<std::string> pngBytes(const QImage& image);

/// The whole table as an image: every row and column at its natural size, with no scroll bars.
QImage fullTableImage(const QTableView& table);

/// The writer of the exports of a run: writeRunArtifactWithProvenance with `provenance`, which it keeps.
ExportWriter runProvenanceWriter(RunProvenance provenance);

/// The run of a result window named by its case and scenario, for its header and for messages: "case / scenario", one of
/// the two when the other is empty, or "(none)" when both are.
QString runContextText(const RunProvenance& provenance);

/// The main widget of a table window, for ResultFrame: `widgets` in a column on white, inside the margins of the main area,
/// the last one taking the height that is left, such as a status above tabs. Takes the widgets.
QWidget* resultTableArea(const QList<QWidget*>& widgets);

#endif // RESULTFRAME_H
