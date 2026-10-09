#include "util/Log.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <utility>

namespace eglog {
namespace {

constexpr int kPairCount = kCategoryCount * kLevelCount;

// The bit of a pair is category * kLevelCount + level.
constexpr std::uint64_t pairBit(int category, int level) {
	return std::uint64_t{1} << (category * kLevelCount + level);
}

constexpr std::uint64_t kAllPairs = ~std::uint64_t{0} >> (64 - kPairCount);
constexpr std::uint64_t kDefaultMask = kAllPairs & ~pairBit(static_cast<int>(Category::SimStep), static_cast<int>(Level::Debug));

// Constant-initialised, so a message logged during static initialisation sees the defaults.
std::atomic<std::uint64_t> enabledMask{kDefaultMask};

// The sink and its mutex are one function-local static: a call during static initialisation
// constructs them on first use.
struct SinkState {
	std::mutex mutex;
	Sink sink;
};

SinkState& sinkState() {
	static SinkState state;
	return state;
}

const char* const kBlanks = " \t\r";

std::string trim(const std::string& text) {
	const std::size_t first = text.find_first_not_of(kBlanks);
	if (first == std::string::npos)
		return std::string();
	const std::size_t last = text.find_last_not_of(kBlanks);
	return text.substr(first, last - first + 1);
}

bool endsWith(const std::string& text, const std::string& suffix) {
	return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool startsWith(const std::string& text, const std::string& prefix) {
	return text.compare(0, prefix.size(), prefix) == 0;
}

// A name pattern is its text without the '*' at the start and at the end, and which ends had one.
struct NamePattern {
	std::string core;
	bool leadingStar = false;
	bool trailingStar = false;
};

// Returns false when a '*' is left inside the text. A run of '*' counts as one.
bool parseNamePattern(const std::string& text, NamePattern& pattern) {
	std::size_t first = 0;
	while (first < text.size() && text[first] == '*')
		++first;
	std::size_t last = text.size();
	while (last > first && text[last - 1] == '*')
		--last;
	pattern.leadingStar = first > 0;
	pattern.trailingStar = last < text.size();
	pattern.core = text.substr(first, last - first);
	return pattern.core.find('*') == std::string::npos;
}

bool matches(const NamePattern& pattern, const std::string& name) {
	if (pattern.leadingStar && pattern.trailingStar)
		return name.find(pattern.core) != std::string::npos;
	if (pattern.leadingStar)
		return endsWith(name, pattern.core);
	if (pattern.trailingStar)
		return startsWith(name, pattern.core);
	return name == pattern.core;
}

// Returns the index of the level named by text, or -1.
int levelIndex(const std::string& text) {
	for (int level = 0; level < kLevelCount; ++level) {
		if (text == levelName(static_cast<Level>(level)))
			return level;
	}
	return -1;
}

// Applies one rule to mask. Returns false when the rule is malformed; an empty rule is skipped.
bool applyRule(const std::string& rule, std::uint64_t& mask) {
	if (rule.empty())
		return true;
	const std::size_t equals = rule.find('=');
	if (equals == std::string::npos)
		return false;
	const std::string pattern = trim(rule.substr(0, equals));
	const std::string value = trim(rule.substr(equals + 1));
	if ((value != "true" && value != "false") || pattern.empty())
		return false;

	std::string name = pattern;
	int onlyLevel = -1;
	const std::size_t dot = pattern.rfind('.');
	if (dot != std::string::npos) {
		onlyLevel = levelIndex(pattern.substr(dot + 1));
		if (onlyLevel >= 0)
			name = pattern.substr(0, dot);
	}
	NamePattern namePattern;
	if (name.empty() || !parseNamePattern(name, namePattern))
		return false;

	for (int category = 0; category < kCategoryCount; ++category) {
		if (!matches(namePattern, categoryName(static_cast<Category>(category))))
			continue;
		for (int level = 0; level < kLevelCount; ++level) {
			if (onlyLevel >= 0 && level != onlyLevel)
				continue;
			if (value == "true")
				mask |= pairBit(category, level);
			else
				mask &= ~pairBit(category, level);
		}
	}
	return true;
}

} // namespace

const char* categoryName(Category category) {
	switch (category) {
		case Category::SimSetup:
			return "sim.setup";
		case Category::SimStep:
			return "sim.step";
		case Category::SimEvents:
			return "sim.events";
		case Category::SimPax:
			return "sim.pax";
		case Category::IoRailml:
			return "io.railml";
		case Category::IoZmq:
			return "io.zmq";
		case Category::AppRun:
			return "app.run";
		case Category::AppUi:
			return "app.ui";
		case Category::Update:
			return "update";
	}
	return "";
}

const char* levelName(Level level) {
	switch (level) {
		case Level::Debug:
			return "debug";
		case Level::Info:
			return "info";
		case Level::Warning:
			return "warning";
		case Level::Error:
			return "error";
	}
	return "";
}

bool enabled(Category category, Level level) {
	return (enabledMask.load(std::memory_order_relaxed) & pairBit(static_cast<int>(category), static_cast<int>(level))) != 0;
}

void setSink(Sink sink) {
	SinkState& state = sinkState();
	std::lock_guard<std::mutex> lock(state.mutex);
	state.sink = std::move(sink);
}

bool configure(const std::string& rules) {
	std::uint64_t mask = kDefaultMask;
	bool wellFormed = true;
	std::size_t start = 0;
	while (start <= rules.size()) {
		std::size_t end = rules.find_first_of(";\n", start);
		if (end == std::string::npos)
			end = rules.size();
		if (!applyRule(trim(rules.substr(start, end - start)), mask))
			wellFormed = false;
		start = end + 1;
	}
	enabledMask.store(mask, std::memory_order_relaxed);
	return wellFormed;
}

bool configureFromEnvironment() {
	const char* value = std::getenv("EGTRAIN_LOG");
	return configure(value ? value : "");
}

Line::Line(Category category, Level level) : m_category(category), m_level(level) {
}

Line::~Line() {
	const std::string text = m_stream.str();
	if (text.empty())
		return;
	SinkState& state = sinkState();
	std::lock_guard<std::mutex> lock(state.mutex);
	if (state.sink)
		state.sink(m_category, m_level, text);
	else
		std::cout << text;
}

} // namespace eglog
