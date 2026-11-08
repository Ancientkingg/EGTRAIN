#include "scene/SectionReference.h"

#include <cctype>

namespace {

// True for the offset of an endpoint: an optional '-' that separates it, then an optional sign, then digits with at
// most one '.'.
bool isOffset(const std::string& text) {
	std::size_t i = 0;
	if (i < text.size() && text[i] == '-')
		++i;
	if (i < text.size() && (text[i] == '+' || text[i] == '-'))
		++i;
	bool sawDigit = false;
	bool sawDot = false;
	for (; i < text.size(); ++i) {
		if (std::isdigit(static_cast<unsigned char>(text[i])) != 0)
			sawDigit = true;
		else if (text[i] == '.' && !sawDot)
			sawDot = true;
		else
			return false;
	}
	return sawDigit;
}

} // namespace

std::optional<SectionReferencePart> parseSectionReferencePart(const std::string& part) {
	if (part.empty())
		return std::nullopt;
	SectionReferencePart result;
	const bool wrapped = part.front() == '@';
	const std::size_t idEnd = part.find('@', wrapped ? 1 : 0);
	if (wrapped && idEnd == std::string::npos)
		return std::nullopt;
	const std::size_t idBegin = wrapped ? 1 : 0;
	result.blockId = part.substr(idBegin, idEnd == std::string::npos ? std::string::npos : idEnd - idBegin);
	if (result.blockId.empty())
		return std::nullopt;
	if (idEnd == std::string::npos || (wrapped && idEnd + 1 == part.size()))
		return result;
	const std::string rest = part.substr(idEnd + 1);
	if (wrapped && isOffset(rest)) {
		result.offset = rest;
		result.kind = SectionReferenceKind::Endpoint;
	} else {
		result.kind = SectionReferenceKind::Other;
	}
	return result;
}

std::vector<std::string> sectionReferenceParts(const std::string& reference) {
	std::vector<std::string> parts;
	std::size_t begin = 0;
	while (begin <= reference.size()) {
		const std::size_t slash = reference.find('/', begin);
		parts.push_back(reference.substr(begin, slash == std::string::npos ? std::string::npos : slash - begin));
		if (slash == std::string::npos)
			break;
		begin = slash + 1;
	}
	return parts;
}

bool isSwitchTransitionReference(const std::string& reference) {
	const std::vector<std::string> parts = sectionReferenceParts(reference);
	if (parts.size() != 2)
		return false;
	for (const std::string& part : parts) {
		const std::optional<SectionReferencePart> parsed = parseSectionReferencePart(part);
		if (!parsed || parsed->kind != SectionReferenceKind::Endpoint)
			return false;
	}
	return true;
}

std::string nativeRuntimeBlockId(const std::string& id) {
	if (id.empty() || id.front() == '@' || id.find('/') != std::string::npos)
		return id;
	return "@" + id + "@";
}
