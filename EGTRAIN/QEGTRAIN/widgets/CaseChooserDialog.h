#pragma once

#include <QDialog>
#include <QRect>
#include <QString>
#include <QStringList>

class QLabel;
class QListWidget;
class QMenu;
class QPushButton;

/// The key under which two spellings of one scene path are the same case: the canonical path of a scene that exists,
/// else the cleaned absolute path, and case-folded on Windows.
QString sceneListKey(const QString& path);

/// The label of a scene in a list of cases: the name in the manifest of a scene folder, else the file name without
/// .egscene, with underscores as spaces.
QString sceneDisplayName(const QString& path);

/// The "Open a Case" dialog: bundled and recent cases in one list, and a menu with
/// the other ways to open. It records what the person chose and changes nothing
/// else; the caller acts on outcome() after exec() returns.
class CaseChooserDialog : public QDialog {
public:
	/// What the person chose.
	enum class Outcome {
		Continue,	  ///< Continue, Cancel or the close button of the window.
		OpenSelected, ///< Open, or a double click on a case; chosenPath() names it.
		BrowseFolder, ///< "Open Scene Folder..." in the menu.
		ImportLegacy, ///< "Import Legacy Case..." in the menu.
		NewCase		  ///< "New Case Study..." in the menu.
	};

	/// What the dialog lists and where it opens.
	struct Options {
		/// The name of the loaded case, shown in the context line; empty when no case is loaded, and the Continue
		/// button is then called Cancel.
		QString currentCaseName;
		/// Folders searched for bundled scene folders and .egscene files. A name found in
		/// an earlier folder hides the same name in a later one.
		QStringList sceneRoots;
		/// Recent scene paths, listed in the order given. A path with the same sceneListKey as an earlier one is left out.
		QStringList recentPaths;
		/// The screen area that the dialog has to fit in. The current screen when empty.
		QRect availableGeometry;
	};

	/// Fills the list and selects its first case that can be opened.
	explicit CaseChooserDialog(const Options& options, QWidget* parent = nullptr);

	/// What the person chose; Continue until a choice is made.
	Outcome outcome() const { return m_outcome; }
	/// The scene path when outcome() is OpenSelected, otherwise empty.
	QString chosenPath() const { return m_chosenPath; }
	/// The path of the current row; empty for a heading or an unavailable case. The status label names the reason.
	QString selectedPath() const;

	/// The list of cases, for tests. The dialog owns it and the widgets below.
	QListWidget* list() const { return m_list; }
	/// The Open button, enabled while a case that can be opened is selected.
	QPushButton* openButton() const { return m_openButton; }
	/// The Continue button, or Cancel when no case is loaded.
	QPushButton* continueButton() const { return m_continueButton; }
	/// The menu of the "Other ways to open..." button.
	QMenu* otherMenu() const { return m_otherMenu; }

private:
	void choose(Outcome chosen, const QString& path = QString());

	QListWidget* m_list = nullptr;
	QLabel* m_status = nullptr;
	QPushButton* m_openButton = nullptr;
	QPushButton* m_continueButton = nullptr;
	QMenu* m_otherMenu = nullptr;
	Outcome m_outcome = Outcome::Continue;
	QString m_chosenPath;
};
