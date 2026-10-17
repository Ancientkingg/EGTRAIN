#ifndef SCENEDOCUMENT_H
#define SCENEDOCUMENT_H

#include <QFlags>
#include <QObject>
#include <QString>
#include <QtGlobal>

// SceneModel has a member named signals, which the Qt keyword hides.
#ifdef signals
#define EGTRAIN_RESTORE_SIGNALS_KEYWORD
#undef signals
#endif
#include "scene/SceneModel.h"
#ifdef EGTRAIN_RESTORE_SIGNALS_KEYWORD
#define signals Q_SIGNALS
#undef EGTRAIN_RESTORE_SIGNALS_KEYWORD
#endif

class MainWindow;

// Copies `snapshot` over `live`, except for the parts of the scene that the application derives from the files on disk and
// from checking the scene, which an edit does not author: the loaded data, the source files and the version that saved the
// scene. Those keep their values in `live`.
void assignAuthoredScene(SceneModel& live, const SceneModel& snapshot);

// The scene that the editors work on: the model, whether it is loaded, edited since it was saved and open for edits, and
// how often it has been changed. Editors change the model in place through mutableModel() and then call markChanged().
class SceneDocument : public QObject {
	Q_OBJECT

public:
	// The parts of the model that an edit can touch.
	enum Change {
		CaseSettings = 1 << 0,
		Infrastructure = 1 << 1,
		TrainUnits = 1 << 2,
		Compositions = 1 << 3,
		Services = 1 << 4,
		Scenarios = 1 << 5,
		Passengers = 1 << 6,
		AllChanges = CaseSettings | Infrastructure | TrainUnits | Compositions | Services | Scenarios | Passengers
	};
	Q_DECLARE_FLAGS(ChangeKinds, Change)

	explicit SceneDocument(QObject* parent = nullptr);

	const SceneModel& model() const { return m_model; }
	SceneModel& mutableModel() { return m_model; }

	// False until the first replace().
	bool isLoaded() const { return m_loaded; }
	// True when the model has changed since it was opened or saved.
	bool isDirty() const { return m_dirty; }
	// False while a run uses the model.
	bool isEditable() const { return m_editable; }
	// Grows with every replace(), restore() and markChanged(), never otherwise.
	quint64 revision() const { return m_revision; }
	// The label of the latest markChanged().
	const QString& lastChangeLabel() const { return m_lastChangeLabel; }

	// Takes another model: a scene that is opened or created. `dirty` tells whether it differs from what is saved.
	void replace(SceneModel newModel, bool dirty);
	// Puts back an earlier state of the scene, as Undo and Redo do. The authored parts of the model become those of
	// `snapshot` (see assignAuthoredScene), and `dirty` tells whether that state differs from what is saved. It is not an
	// edit: changed() is not emitted, so the editors have to be refreshed by the caller.
	void restore(const SceneModel& snapshot, bool dirty);
	// Records that an editor has changed `kinds` of the model, and what the change was called.
	void markChanged(ChangeKinds kinds, const QString& label = {});
	void markSaved() { setDirty(false); }
	void setDirty(bool dirty);
	void setEditable(bool editable);

Q_SIGNALS:
	// Emitted once for each markChanged(), after the revision and the dirty state are updated.
	void changed(SceneDocument::ChangeKinds kinds);
	// Emitted by replace(), after the whole state is set.
	void replaced();
	void dirtyChanged(bool dirty);
	void editableChanged(bool editable);

private:
	// MainWindow holds references to the state until its editors have moved out.
	friend class MainWindow;

	SceneModel m_model;
	bool m_loaded = false;
	bool m_dirty = false;
	bool m_editable = true;
	quint64 m_revision = 0;
	QString m_lastChangeLabel;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(SceneDocument::ChangeKinds)

#endif // SCENEDOCUMENT_H
