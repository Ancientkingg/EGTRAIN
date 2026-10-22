#ifndef SCENE_SECTION_LABELS_H
#define SCENE_SECTION_LABELS_H

#include <string>
#include <unordered_map>

// The labels have a header of their own, without the scene model: Qt defines `signals` as a macro, so a file that
// includes Qt before scene/SceneModel.h cannot include that. sceneSectionLabels in scene/SectionInventory.h makes them.

/// The labels of the sections of an inventory by runtime id and by block id. A label that two sections
/// share ends with the id in parentheses; label() returns the reference itself when it names no section.
struct SceneSectionLabels {
	std::unordered_map<std::string, std::string> byReference; ///< Label by every reference that resolve() accepts.
	/// The label of the section that the reference names, or the reference itself when it names none or
	/// more than one.
	std::string label(const std::string& reference) const;
};

#endif // SCENE_SECTION_LABELS_H
