#ifndef RESULTFRAME_H
#define RESULTFRAME_H

#include <QString>
#include <QVector>
#include <QWidget>

class QFrame;
class QHBoxLayout;
class QLabel;
class QResizeEvent;
class QShowEvent;
class QSplitter;
class QToolButton;
class QVBoxLayout;

/// The side panel of a result window: titled sections in a column that scrolls when it is taller than the window,
/// with a rule between the sections that are shown, and a cursor readout below the column.
class ResultPanel : public QWidget {
	Q_OBJECT
public:
	explicit ResultPanel(QWidget* parent = nullptr);
	/// Adds a section below the others: a heading with `title` and, at its right, `headingWidget` when given, and
	/// `content` below the heading. The panel owns both widgets. Returns the section, which setSectionVisible takes.
	QWidget* addSection(const QString& title, QWidget* content, QWidget* headingWidget = nullptr);
	/// Shows or hides a section of the panel, with its rule.
	void setSectionVisible(QWidget* section, bool visible);
	/// The plain-text label at the bottom of the panel, below the column of sections; empty until a window sets it.
	QLabel* readout() const { return m_readout; }

private:
	void updateRules();

	struct Section {
		QWidget* widget;
		QFrame* rule; // above the section, shown when an earlier section is shown too
	};
	QVector<Section> m_sections;
	QVBoxLayout* m_column = nullptr;
	QLabel* m_readout = nullptr;
};

/// The frame of a result window. A splitter holds the main widget, with a header row above it, and the panel beside
/// both. The header shows the context of the window in one line at the left and tool buttons at the right, the last of
/// which shows and hides the panel. The panel is 260 px wide, or 200 px while the window is narrower than 1000 px, until
/// the user drags the splitter; the main widget takes the rest, and the splitter cannot collapse either of them.
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
	/// The panel and its button.
	ResultPanel* panel() const { return m_panel; }
	QToolButton* panelButton() const { return m_panelButton; }

protected:
	void resizeEvent(QResizeEvent* event) override;
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

#endif // RESULTFRAME_H
