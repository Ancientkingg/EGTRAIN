#pragma once

#include <QDialog>
#include <QRect>
#include <QString>
#include <QStringList>

class QListWidget;
class QMenu;
class QPushButton;

// The "Open a Case" dialog: bundled and recent cases in one list, and a menu with
// the other ways to open. It records what the person chose and changes nothing
// else; the caller acts on outcome() after exec() returns.
class CaseChooserDialog : public QDialog {
public:
	enum class Outcome {
		Continue,
		OpenSelected,
		BrowseFolder,
		ImportLegacy,
		NewCase
	};

	struct Options {
		// Empty when no case is loaded.
		QString currentCaseName;
		// Folders searched for bundled scene folders and .egscene files. A name found in
		// an earlier folder hides the same name in a later one.
		QStringList sceneRoots;
		// Recent scene paths, listed in the order given.
		QStringList recentPaths;
		// The screen area that the dialog has to fit in. The current screen when empty.
		QRect availableGeometry;
	};

	explicit CaseChooserDialog(const Options& options, QWidget* parent = nullptr);

	Outcome outcome() const { return m_outcome; }
	// The scene path when outcome() is OpenSelected, otherwise empty.
	QString chosenPath() const { return m_chosenPath; }
	// The path of the current row; empty for a heading or an unavailable case.
	QString selectedPath() const;

	QListWidget* list() const { return m_list; }
	QPushButton* openButton() const { return m_openButton; }
	QPushButton* continueButton() const { return m_continueButton; }
	QMenu* otherMenu() const { return m_otherMenu; }

private:
	void choose(Outcome chosen, const QString& path = QString());

	QListWidget* m_list = nullptr;
	QPushButton* m_openButton = nullptr;
	QPushButton* m_continueButton = nullptr;
	QMenu* m_otherMenu = nullptr;
	Outcome m_outcome = Outcome::Continue;
	QString m_chosenPath;
};
