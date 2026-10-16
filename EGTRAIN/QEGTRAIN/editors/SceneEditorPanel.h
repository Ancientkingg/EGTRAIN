#ifndef SCENEEDITORPANEL_H
#define SCENEEDITORPANEL_H

#include "editors/SceneDocument.h"

#include <QWidget>

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
