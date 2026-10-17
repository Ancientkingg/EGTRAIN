#ifndef SCENEEDITORPANEL_H
#define SCENEEDITORPANEL_H

#include "editors/SceneDocument.h"

#include <QListWidget>
#include <QString>
#include <QWidget>

// The row of the entry that reads `text` in a list that was filled again, or -1 when no entry does. The entry at `row` is
// preferred when it reads `text`, so that entries that share their text keep their place. A panel uses it to keep the
// selected entry selected when the document is replaced by one that lists the entry at another row.
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

// The widget of one scene editor dock. A panel shows the model of its SceneDocument, edits it there and calls
// SceneDocument::markChanged. The window creates the dock and puts the panel in it.
class SceneEditorPanel : public QWidget {
	Q_OBJECT

public:
	explicit SceneEditorPanel(QWidget* parent = nullptr) : QWidget(parent) {}

	// The panel works on this document from now on; null detaches it.
	virtual void setDocument(SceneDocument* document) = 0;
	// Shows the current state of the document.
	virtual void refresh() = 0;
	// Takes the text of a field that is being edited into the document, as if the user had left the field.
	virtual void commitPendingEditors() = 0;

Q_SIGNALS:
	// The panel has changed the document.
	void changed();
};

#endif // SCENEEDITORPANEL_H
