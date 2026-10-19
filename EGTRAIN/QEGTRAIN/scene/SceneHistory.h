#ifndef SCENE_SCENE_HISTORY_H
#define SCENE_SCENE_HISTORY_H

// The scene model has a member named "signals", so this header includes no Qt header. Include
// it before any Qt header that defines that macro, or guard the include as MainWindow.h does.
#include "scene/SceneModel.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

/// The retained history is bounded by the number of undo steps and by the estimated memory of its
/// snapshots. The estimate is a bound for the history, not a measurement of the process.
struct SceneHistoryLimits {
	std::size_t maxSteps = 100;				  ///< Most undo steps kept.
	std::size_t maxBytes = 128 * 1024 * 1024; ///< Most estimated bytes of all snapshots.
};

/// An estimate of the memory that a copy of the scene holds: its structs, the elements of its
/// vectors and sets, and the text of the strings that do not fit in the string object. It
/// covers every container member of SceneModel, so a new container member needs a line in
/// estimateSceneModelBytes. The value depends on the standard library.
std::size_t estimateSceneModelBytes(const SceneModel& scene);

/// One state of the scene. The label names the edit that produced the state from the one before;
/// the root has none.
struct SceneHistoryEntry {
	std::shared_ptr<const SceneModel> scene; ///< The scene in this state, shared and immutable.
	std::string label;						 ///< Name of the edit that produced it; empty for the root.
	std::size_t bytes = 0;					 ///< estimateSceneModelBytes of the scene, in bytes.
	/// Scenarios that differ from the saved scene, as of this state.
	std::set<std::string> modifiedScenarioIds;
};

/// Undo and Redo as a position in a list of whole-scene snapshots. The entry at the position is
/// the live scene. Recording an edit appends a copy and drops the entries after the position, so
/// an edit after Undo abandons the Redo branch. When either limit is exceeded, the oldest
/// entries are dropped; the entry at the position never is.
///
/// One position can be marked as saved. The scene is clean exactly when the position is the saved
/// one. Dropping the saved entry, by an edit after undoing past it or by a limit, leaves no saved
/// position, and the scene stays dirty until the next save.
///
/// A history always has a root. A new history holds an empty scene as its root until reset. The
/// class has no Qt dependency and is used by one thread at a time; the scenes that it returns
/// are immutable and may be read from other threads.
class SceneHistory {
public:
	/// A history with an empty scene as its root and these limits.
	explicit SceneHistory(SceneHistoryLimits limits = {});

	/// Starts over with `root` as the only entry. `saved` marks it as the saved state, as for a
	/// scene that was just opened; a new scene or a recovered one is not saved.
	void reset(const SceneModel& root, bool saved = false);

	/// Appends a copy of the scene after an edit. The label is derived from what the edit added
	/// or removed. `modifiedScenarioIds` is the set that the live scene has after the edit.
	void record(const SceneModel& after, std::set<std::string> modifiedScenarioIds = {});

	bool canUndo() const; ///< True when there is an entry before the position.
	bool canRedo() const; ///< True when there is an entry after the position.
	/// The label of the edit that Undo reverts; empty when there is none.
	std::string undoLabel() const;
	/// The label of the edit that Redo repeats; empty when there is none.
	std::string redoLabel() const;

	/// Moves the position one entry back and returns the entry that is now live, or null when there
	/// is no such entry. The pointer is valid until the history is changed.
	const SceneHistoryEntry* undo();
	/// Moves the position one entry forward and returns the entry that is now live, or null when there
	/// is no such entry. The pointer is valid until the history is changed.
	const SceneHistoryEntry* redo();

	/// The live scene.
	std::shared_ptr<const SceneModel> head() const;

	/// Marks the live state as saved and clears its modified scenarios.
	void markSaved();
	bool isAtSavedPoint() const; ///< True when the position is the saved one.

	std::size_t undoCount() const; ///< Number of entries before the position.
	std::size_t redoCount() const; ///< Number of entries after the position.
	std::size_t bytes() const;	   ///< The estimate of all entries, in bytes.

	const SceneHistoryLimits& limits() const { return m_limits; } ///< The limits of this history.

private:
	static SceneHistoryEntry makeEntry(const SceneModel& scene, std::string label);
	void dropOldest();

	SceneHistoryLimits m_limits;
	std::vector<SceneHistoryEntry> m_entries;
	std::size_t m_position = 0;
	std::optional<std::size_t> m_saved;
	std::size_t m_bytes = 0;
};

#endif // SCENE_SCENE_HISTORY_H
