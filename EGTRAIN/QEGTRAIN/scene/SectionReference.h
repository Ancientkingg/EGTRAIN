#ifndef SECTIONREFERENCE_H
#define SECTIONREFERENCE_H

#include <optional>
#include <string>
#include <vector>

// A section reference names one block or several joined by '/', such as the two ends of a switch transition
// `@B0-1@-5/@B1-1@-5`.

/// The form of one part of a section reference.
enum class SectionReferenceKind {
	Block,	  ///< A block id, bare (`B0-1`) or between two '@' (`@B0-1@`).
	Endpoint, ///< A block id between two '@' and an offset (`@B0-1@-5`): one end of a switch transition.
	Other,	  ///< A block id with other text after its '@' (`B0-1@x`, `@B0-1@x`).
};

/// One part of a section reference: the text between two '/'.
struct SectionReferencePart {
	std::string blockId;									 ///< The block id, without the '@' around it.
	std::string offset;										 ///< The offset of an endpoint as written; empty otherwise.
	SectionReferenceKind kind = SectionReferenceKind::Block; ///< The form of the part.
};

/// Reads one part of a section reference. A bare block id ends at its first '@', and an id that starts with '@' at
/// the next '@'. An endpoint has an offset after that '@': a number with an optional sign and at most one '.',
/// optionally after a '-' that separates it from the id, as the runtime writes a coordinate (`@B0-1@-5`,
/// `@B0-1@--1.5`) and as the legacy importer has always read it (`@B0-1@5`). Returns nothing for an empty block id
/// and for a part that starts with '@' and has no second '@'.
std::optional<SectionReferencePart> parseSectionReferencePart(const std::string& part);
/// The parts of `reference` between its '/', in order, empty parts included.
std::vector<std::string> sectionReferenceParts(const std::string& reference);
/// True for a switch transition: two endpoints joined by one '/'.
bool isSwitchTransitionReference(const std::string& reference);
/// The id of a block section at run time: a block id between two '@'. A reference that is empty, starts with '@' or
/// joins several parts with '/' is its own runtime id.
std::string nativeRuntimeBlockId(const std::string& id);

#endif // SECTIONREFERENCE_H
