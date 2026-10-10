#pragma once

// The scene model has a member named "signals", so this header includes no Qt header that
// defines that macro. Include scene/SceneModel.h before any Qt header that includes QObject.
#include <QDateTime>
#include <QString>
#include <QVector>
#include <memory>

struct SceneModel;
class QLockFile;

// Where a scene came from. A recovery copy never changes the file or folder named here.
enum class RecoverySourceKind {
	Untitled,
	Bundle,
	Directory,
};

// The source of a scene at the time its copy is written. A directory source is judged by its
// scene.json alone.
struct RecoverySource {
	RecoverySourceKind kind = RecoverySourceKind::Untitled;
	QString path; // Absolute path the scene was loaded from or last saved to; empty for Untitled.
	QString displayName;
	qint64 sizeBytes = -1; // Size of the bundle file, or of scene.json in a directory; -1 when unknown.
	QDateTime modified;	   // Modification time (UTC) of the same file; invalid when unknown.
};

// How the source compares with the copy.
enum class RecoverySourceStatus {
	NotApplicable, // Untitled scene.
	Unknown,	   // The recorded size or time is missing.
	Unchanged,
	Changed,	   // Size or modification time differ from the recorded values.
	NewerThanCopy, // Modified after the copy was written.
	Missing,
};

enum class RecoveryCandidateState {
	Openable,
	NewerRecordVersion, // The record has a layout this version does not know. Nothing is changed.
	OtherSceneVersion,	// The bundle is valid but not of the current scene version.
};

// A recovery copy that an application left behind.
struct RecoveryCandidate {
	QString instanceId;
	RecoveryCandidateState state = RecoveryCandidateState::Openable;
	QString bundlePath; // Empty only for NewerRecordVersion when no readable bundle is found.
	RecoverySource source;
	QDateTime writtenAt; // UTC.
	RecoverySourceStatus sourceStatus = RecoverySourceStatus::NotApplicable;
};

struct RecoveryResult {
	bool ok = false;
	// Empty when ok. One fixed sentence, optionally followed by a diagnostic code in brackets.
	// It holds no path and no scene content.
	QString error;
	QString bundlePath; // write and adopt: the bundle that now holds the copy.
};

struct RecoveryScan {
	QVector<RecoveryCandidate> candidates; // Newest first.
	int removedOverCap = 0;				   // Candidates removed because more than kMaxKeptInstances were left.
};

// Keeps recovery copies of a scene below a root folder that the caller names. Each running
// application (an instance) owns one folder <root>/<instance id>, holds a lock on it while the
// store is open and keeps at most one copy of the scene in it, as an ordinary scene bundle. A
// clean close removes the folder. A folder whose owner is gone is a candidate for recovery.
//
// The store writes only below its root, apart from the private copy that saveSceneBundle makes
// in the system temporary directory while it runs. It never touches the file or folder a scene
// came from. No function throws, prints or logs, and no error text holds a path.
//
// A store is used by one thread at a time and has no static state.
class RecoveryStore {
public:
	// At most this many candidates are kept; scan removes the oldest beyond it.
	static constexpr int kMaxKeptInstances = 20;

	explicit RecoveryStore(const QString& root);
	// Releases the lock and deletes nothing, so a missing close() never destroys a copy.
	~RecoveryStore();
	RecoveryStore(const RecoveryStore&) = delete;
	RecoveryStore& operator=(const RecoveryStore&) = delete;

	// Creates the instance folder with a new random id and locks it. Ok when already open.
	RecoveryResult open();
	bool isOpen() const;
	QString instanceId() const;

	// Writes the scene as a new generation next to the previous one, then records it and removes
	// the previous one. A failed write leaves the previous copy.
	RecoveryResult write(const SceneModel& scene, const RecoverySource& source);
	// Removes the copy and the record. The folder and the lock stay.
	RecoveryResult clear();
	// Removes the instance folder and releases the lock. Ok when already closed.
	RecoveryResult close();

	// Lists the copies that other applications left behind, newest first. A folder whose lock is
	// held belongs to a running application and is skipped. The scan removes what is not usable:
	// stray files of an interrupted write, older generations, folders without a complete bundle,
	// and candidates beyond kMaxKeptInstances.
	RecoveryScan scan();
	// Takes over the copy of an Openable candidate. It replaces the copy this store holds, as write
	// does, and the candidate folder is removed. Fails when the candidate is in use.
	RecoveryResult adopt(const RecoveryCandidate& candidate);
	// Removes the folder of a candidate in any state. Fails when the candidate is in use.
	RecoveryResult discard(const RecoveryCandidate& candidate);

private:
	QString m_root;
	QString m_id;
	std::unique_ptr<QLockFile> m_lock;
	int m_generation = 0; // Number of the newest generation this store wrote.
};

// Compares the source with the values recorded when the copy was written. Reads metadata only.
RecoverySourceStatus recoverySourceStatus(const RecoverySource& source, const QDateTime& writtenAt);
