#pragma once

#include <QDialog>

/// The "About EGTRAIN" dialog: the version, the authors and links to their pages and to the project.
class AboutDialog : public QDialog {
public:
	/// Takes the icon from parent, or from the application when there is no parent.
	explicit AboutDialog(QWidget* parent = nullptr);
};
