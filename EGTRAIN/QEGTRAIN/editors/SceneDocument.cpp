#include "editors/SceneDocument.h"

#include <utility>

SceneDocument::SceneDocument(QObject* parent) : QObject(parent) {}

void SceneDocument::replace(SceneModel newModel, bool dirty) {
	m_model = std::move(newModel);
	m_loaded = true;
	++m_revision;
	setDirty(dirty);
	emit replaced();
}

void SceneDocument::markChanged(ChangeKinds kinds, const QString& label) {
	++m_revision;
	m_lastChangeLabel = label;
	setDirty(true);
	emit changed(kinds);
}

void SceneDocument::setDirty(bool dirty) {
	if (m_dirty == dirty)
		return;
	m_dirty = dirty;
	emit dirtyChanged(m_dirty);
}

void SceneDocument::setEditable(bool editable) {
	if (m_editable == editable)
		return;
	m_editable = editable;
	emit editableChanged(m_editable);
}
