#ifndef SCENEEDITORPANEL_H
#define SCENEEDITORPANEL_H

#include "editors/SceneDocument.h"
// After SceneDocument.h, which includes the scene model the way RunSelection.h needs it.
#include "app/RunSelection.h"
#include "widgets/UserPrompts.h"

#include <QListWidget>
#include <QMetaObject>
#include <QPointer>
#include <QString>
#include <QWidget>

#include <algorithm>
#include <functional>
#include <utility>

/// The row of the entry that reads `text` in a list that was filled again, or -1 when no entry does or `text` is empty. The
/// entry at `row` is preferred when it reads `text`, so that entries that share their text keep their place. A panel uses it
/// to keep the selected entry selected when the document is replaced by one that lists the entry at another row.
inline int rowOfEntry(const QListWidget* list, const QString& text, int row) {
	if (text.isEmpty())
		return -1;
	if (row >= 0 && row < list->count() && list->item(row)->text() == text)
		return row;
	for (int index = 0; index < list->count(); ++index) {
		if (list->item(index)->text() == text)
			return index;
	}
	return -1;
}

/// Selects an entry of a list that was filled again: the one that reads `text` (see rowOfEntry), or else the entry at `row`,
/// which was selected before. That is the last entry when the list has become shorter, and the first when none was
/// selected. An empty `text` selects by the row alone, for a list whose text is not the identity of its entries.
inline void restoreListSelection(QListWidget* list, const QString& text, int row) {
	const int count = list->count();
	const int sameEntry = rowOfEntry(list, text, row);
	if (sameEntry >= 0)
		list->setCurrentRow(sameEntry);
	else
		list->setCurrentRow(count == 0 ? -1 : (row < 0 ? 0 : std::min(row, count - 1)));
}

/// The widget of one scene editor dock. A panel shows the model of its SceneDocument, edits it there in place, calls
/// SceneDocument::markChanged and then emits changed(). The window creates the dock and puts the panel in it. The panel
/// does not own the document. The base keeps what every panel has: the document with its signals, the prompts, the run
/// selection and the time over which occurrences are counted. A panel that does not need one of them does not use it.
class SceneEditorPanel : public QWidget {
	Q_OBJECT

public:
	/// A panel without a document.
	explicit SceneEditorPanel(QWidget* parent = nullptr) : QWidget(parent) {}

	/// The panel works on this document from now on and shows it; null detaches it. From then on documentReplaced() and
	/// documentChanged() follow the document.
	void setDocument(SceneDocument* document) {
		QObject::disconnect(m_replacedConnection);
		QObject::disconnect(m_changedConnection);
		m_document = document;
		if (m_document) {
			m_replacedConnection = connect(m_document, &SceneDocument::replaced, this, &SceneEditorPanel::documentReplaced);
			m_changedConnection = connect(m_document, &SceneDocument::changed, this, &SceneEditorPanel::documentChanged);
		}
		refresh();
	}
	/// The messages and questions of the panel go through these prompts. The panel uses its own until it is given others;
	/// null brings its own back. The prompts must outlive the panel.
	void setPrompts(UserPrompts* prompts) { m_prompts = prompts ? prompts : &m_ownPrompts; }
	/// The selected scenario, the scenarios that are edited and the occurrences that the next run leaves out are kept here.
	/// The panel uses its own until it is given another; null brings its own back. The selection must outlive the panel.
	void setRunSelection(RunSelection* selection) { m_runSelection = selection ? selection : &m_ownRunSelection; }
	/// Gives the time in seconds over which the occurrences of a service are counted. Without one the duration of the
	/// scene settings is used.
	void setOccurrenceDuration(std::function<double()> duration) { m_occurrenceDuration = std::move(duration); }
	/// Shows the current state of the document.
	virtual void refresh() = 0;
	/// Takes the text of a field that is being edited into the document, as if the user had left the field.
	virtual void commitPendingEditors() = 0;

	/// True when a scene is loaded.
	bool hasScene() const { return m_document && m_document->isLoaded(); }
	/// False while no scene is loaded or a run uses it.
	bool canEdit() const { return hasScene() && m_document->isEditable(); }

Q_SIGNALS:
	/// The panel has changed the document. Emitted after SceneDocument::changed().
	void changed();

protected:
	/// The document has been replaced. The panel shows it again.
	virtual void documentReplaced() { refresh(); }
	/// `kinds` of the document have changed. A panel that shows them goes along here.
	virtual void documentChanged([[maybe_unused]] SceneDocument::ChangeKinds kinds) {}
	/// The time in seconds over which the occurrences of a service are counted. The panel must have a document.
	double occurrenceDuration() const {
		return m_occurrenceDuration ? m_occurrenceDuration() : m_document->model().settings.durationSeconds;
	}

	/// The document that the panel works on; null without one.
	QPointer<SceneDocument> m_document;
	/// The prompts of setPrompts().
	UserPrompts* m_prompts = &m_ownPrompts;
	/// The selection of setRunSelection().
	RunSelection* m_runSelection = &m_ownRunSelection;

private:
	QMetaObject::Connection m_replacedConnection;
	QMetaObject::Connection m_changedConnection;
	UserPrompts m_ownPrompts;
	RunSelection m_ownRunSelection;
	std::function<double()> m_occurrenceDuration;
};

#endif // SCENEEDITORPANEL_H
