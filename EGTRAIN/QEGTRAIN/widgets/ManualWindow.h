#pragma once

#include <QTemporaryDir>
#include <QWidget>

class QHelpEngine;
class QTextBrowser;

/// The user manual: the pages of docs/guides built into the Qt help file EGTRAIN.qch, shown with its contents and search.
/// A window or widget names its section of the manual in the property manualSection, which sectionOf() reads.
class ManualWindow : public QWidget {
	Q_OBJECT
public:
	/// Opens the help file helpFile. The window shows nothing when the file cannot be read.
	explicit ManualWindow(const QString& helpFile, QWidget* parent = nullptr);
	~ManualWindow() override;

	/// Shows the page of a section, or the start page for a section without one. False when the help file could not be read.
	bool showSection(const QString& section);

	/// The property manualSection of widget or of its nearest parent that has it.
	static QString sectionOf(const QWidget* widget);

	/// Shows the section in the manual window of parent, which opens on first use. Without a readable help file next to the
	/// application, the web browser opens the section in the published guides instead.
	static void open(QWidget* parent, const QString& section);

private:
	QTemporaryDir m_collectionDir;
	QHelpEngine* m_engine = nullptr;
	QTextBrowser* m_browser = nullptr;
	QString m_namespace;
};
