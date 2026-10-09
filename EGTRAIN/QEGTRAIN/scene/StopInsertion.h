#ifndef SCENE_STOP_INSERTION_H
#define SCENE_STOP_INSERTION_H

#include "scene/SceneModel.h"
#include "scene/SectionInventory.h"

#include <cstddef>
#include <limits>
#include <string>

// The traversal of the route a service runs on, or an unresolved traversal
// when the service has no route in the model.
SceneRouteTraversal sceneServiceTraversal(const SceneModel& model, const SceneService& service);

// The route visits after the last stop that resolves before `stopIndex`. A stop
// before `stopIndex` whose status is neither Resolved nor OffRouteContext leaves
// no visits.
SceneRouteTraversal sceneRemainingStopTraversal(const SceneModel& model, const SceneService& service,
		std::size_t stopIndex);

// The route visits at which a new stop can be inserted at one position of a
// timetable.
struct SceneStopInsertionWindow {
	static constexpr std::size_t kNoStop = std::numeric_limits<std::size_t>::max();

	// False when no window can be computed. A true value with no visits is a
	// window that offers nothing.
	bool ok = false;
	// Zero-based index of the stop that prevents the window, or kNoStop when
	// another cause applies (an unresolved route, a position past the end).
	std::size_t blockingStop = kNoStop;
	// Text for the user when `ok` is false.
	std::string problem;
	// Only the visits strictly between the neighbouring stops. The stations
	// of these visits, and the platforms of one station among them, are the
	// choices for the new stop.
	SceneRouteTraversal visits;
};

// `insertIndex` 0 inserts before the first stop and `stops.size()` after the
// last. The lower bound is the visit after the last resolved stop before
// `insertIndex`; the upper bound is the visit of the first resolved stop at or
// after it, resolved over the whole current timetable. Stops that resolve as
// OffRouteContext do not bound the window.
SceneStopInsertionWindow sceneStopInsertionWindow(const SceneModel& model,
		const SceneService& service, std::size_t insertIndex);

struct SceneStopInsertionResult {
	bool inserted = false;
	// Text for the user when nothing was inserted.
	std::string error;
};

// Inserts `stop` at `insertIndex` only when the new stop resolves and every stop
// that resolved before still resolves. Otherwise `service` is left unchanged
// and the result names the stops concerned. No field of an existing stop is
// changed and the planned times of `stop` are kept as given.
//
// The matching of stops to route visits is sequential, and two stops with the
// same station and platform inside one window bind to its visits in order.
SceneStopInsertionResult insertSceneStop(const SceneModel& model, SceneService& service,
		std::size_t insertIndex, const SceneStop& stop);

#endif // SCENE_STOP_INSERTION_H
