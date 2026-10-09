#include "util/Log.h"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace eglog;

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

struct Message {
	Category category;
	Level level;
	std::string text;
};

// A sink that collects into a vector.
static Sink collectInto(std::vector<Message>& messages) {
	return [&messages](Category category, Level level, const std::string& text) { messages.push_back({category, level, text}); };
}

// The test counts the categories and levels itself, so that a wrong constant in the header is noticed.
static const int kCategories = 9;
static const int kLevels = 4;

static int ordinal(Category category) {
	return static_cast<int>(category);
}

static int ordinal(Level level) {
	return static_cast<int>(level);
}

static bool pairEnabled(int category, int level) {
	return enabled(static_cast<Category>(category), static_cast<Level>(level));
}

static bool defaultDisabled(int category, int level) {
	return category == ordinal(Category::SimStep) && level == ordinal(Level::Debug);
}

// True when the pairs for which expectedDisabled(category, level) holds are disabled, and no other.
template <typename Predicate>
static bool disabledExactly(Predicate expectedDisabled) {
	for (int category = 0; category < kCategories; ++category) {
		for (int level = 0; level < kLevels; ++level) {
			if (pairEnabled(category, level) == expectedDisabled(category, level))
				return false;
		}
	}
	return true;
}

static void reset() {
	configure("");
	setSink({});
}

static bool testNames() {
	const char* const categories[] = {"sim.setup", "sim.step", "sim.events", "sim.pax", "io.railml", "io.zmq", "app.run", "app.ui", "update"};
	const char* const levels[] = {"debug", "info", "warning", "error"};
	bool ok = expect(kCategoryCount == kCategories && kLevelCount == kLevels, "nine categories and four levels");
	for (int category = 0; category < kCategories; ++category)
		ok &= expect(std::string(categoryName(static_cast<Category>(category))) == categories[category], "category name");
	for (int level = 0; level < kLevels; ++level)
		ok &= expect(std::string(levelName(static_cast<Level>(level))) == levels[level], "level name");
	return ok;
}

static bool testDefaults() {
	bool ok = expect(configure(""), "empty rules are well formed");
	ok &= expect(disabledExactly(defaultDisabled), "only sim.step at debug level is disabled by default");
	reset();
	return ok;
}

static bool testSingleRules() {
	bool ok = expect(configure("sim.step=true"), "sim.step=true is well formed");
	ok &= expect(disabledExactly([](int, int) { return false; }), "sim.step=true enables sim.step at debug level");

	ok &= expect(configure("sim.setup.warning=false"), "a level rule is well formed");
	ok &= expect(disabledExactly([](int category, int level) {
		return (category == ordinal(Category::SimSetup) && level == ordinal(Level::Warning)) || defaultDisabled(category, level);
	}),
		"sim.setup.warning=false disables that pair only");

	ok &= expect(configure("*.debug=false"), "*.debug=false is well formed");
	ok &= expect(disabledExactly([](int, int level) { return level == ordinal(Level::Debug); }), "*.debug=false disables debug for every category");

	ok &= expect(configure("io.*.warning=false"), "a prefix and a level are well formed");
	ok &= expect(disabledExactly([](int category, int level) {
		return ((category == ordinal(Category::IoRailml) || category == ordinal(Category::IoZmq)) && level == ordinal(Level::Warning))
			|| defaultDisabled(category, level);
	}),
		"io.*.warning=false disables warning for the io categories");
	reset();
	return ok;
}

static bool testPatterns() {
	bool ok = expect(configure("sim.*=false"), "sim.*=false is well formed");
	ok &= expect(disabledExactly([](int category, int) { return category <= ordinal(Category::SimPax); }),
		"sim.* disables the four sim categories only");

	ok &= expect(configure("*.run=false"), "*.run=false is well formed");
	ok &= expect(disabledExactly([](int category, int level) { return category == ordinal(Category::AppRun) || defaultDisabled(category, level); }),
		"*.run disables app.run only");

	ok &= expect(configure("*step*=false"), "*step*=false is well formed");
	ok &= expect(disabledExactly([](int category, int) { return category == ordinal(Category::SimStep); }), "*step* disables sim.step only");

	ok &= expect(configure("update=false"), "a category name is well formed");
	ok &= expect(disabledExactly([](int category, int level) { return category == ordinal(Category::Update) || defaultDisabled(category, level); }),
		"a category name disables that category only");

	ok &= expect(configure("*=false"), "*=false is well formed");
	ok &= expect(disabledExactly([](int, int) { return true; }), "* disables every pair");
	ok &= expect(configure("**=false"), "**=false is well formed");
	ok &= expect(disabledExactly([](int, int) { return true; }), "** counts as *");

	ok &= expect(configure("nothing.here=false;*.nothing=false"), "a pattern that matches no category is well formed");
	ok &= expect(disabledExactly(defaultDisabled), "a pattern that matches no category changes nothing");
	reset();
	return ok;
}

static bool testRuleOrder() {
	bool ok = expect(configure("sim.step=false;sim.step=true"), "two rules are well formed");
	ok &= expect(disabledExactly([](int, int) { return false; }), "the later true wins");
	ok &= expect(configure("sim.step=true;sim.step=false"), "two reversed rules are well formed");
	ok &= expect(disabledExactly([](int category, int) { return category == ordinal(Category::SimStep); }), "the later false wins");
	ok &= expect(configure("*=false;app.run.error=true"), "a general and a narrow rule are well formed");
	ok &= expect(disabledExactly([](int category, int level) { return !(category == ordinal(Category::AppRun) && level == ordinal(Level::Error)); }),
		"a narrow rule after a general one wins");
	reset();
	return ok;
}

static bool testSeparators() {
	bool ok = expect(configure("  sim.setup.info = false ; \n\t io.zmq=false \r\n;;sim.pax.warning=false;"), "separators and blanks are accepted");
	ok &= expect(disabledExactly([](int category, int level) {
		return (category == ordinal(Category::SimSetup) && level == ordinal(Level::Info)) || category == ordinal(Category::IoZmq)
			|| (category == ordinal(Category::SimPax) && level == ordinal(Level::Warning)) || defaultDisabled(category, level);
	}),
		"semicolons, newlines and blanks separate the rules");
	reset();
	return ok;
}

static bool testMalformedRules() {
	bool ok = true;
	const char* const malformedRules[] = {
		"sim.step",
		"sim.step=maybe",
		"=true",
		".debug=true",
		"si*m=true",
		"sim.step=TRUE",
		"sim.step=",
		"sim.*x=true",
		"*sim*x*=true",
	};
	for (const char* malformed : malformedRules) {
		ok &= expect(!configure(malformed), "a malformed rule returns false");
		ok &= expect(disabledExactly(defaultDisabled), "a malformed rule changes nothing");
		ok &= expect(!configure(std::string("sim.setup.error=false;") + malformed), "a malformed rule after a good one returns false");
		ok &= expect(!pairEnabled(ordinal(Category::SimSetup), ordinal(Level::Error)), "the good rule before a malformed one is applied");
		ok &= expect(!configure(std::string(malformed) + "\nsim.pax.error=false"), "a malformed rule before a good one returns false");
		ok &= expect(!pairEnabled(ordinal(Category::SimPax), ordinal(Level::Error)), "the good rule after a malformed one is applied");
	}
	reset();
	return ok;
}

static bool testRestoreDefaults() {
	configure("*=false");
	bool ok = expect(configure(""), "empty rules are well formed after other rules");
	ok &= expect(disabledExactly(defaultDisabled), "empty rules restore the defaults");
	configure("sim.step");
	ok &= expect(configure(""), "empty rules are well formed after a malformed rule");
	ok &= expect(disabledExactly(defaultDisabled), "empty rules restore the defaults after a malformed rule");
	reset();
	return ok;
}

static bool testDelivery() {
	std::vector<Message> messages;
	setSink(collectInto(messages));
	EG_LOG(SimSetup, Warning) << "n=" << 42 << " x=" << 1.5 << " s=" << std::string("abc") << "\nnext" << std::endl;
	bool ok = expect(messages.size() == 1, "one message is delivered");
	if (ok) {
		ok &= expect(messages[0].category == Category::SimSetup && messages[0].level == Level::Warning, "category and level are delivered");
		ok &= expect(messages[0].text == "n=42 x=1.5 s=abc\nnext\n", "the text is delivered as streamed");
	}

	messages.clear();
	EG_LOG(SimStep, Debug) << "per step";
	ok &= expect(messages.empty(), "a message of a disabled pair is not delivered");
	EG_LOG(SimSetup, Error);
	ok &= expect(messages.empty(), "an empty message is not delivered");
	configure("sim.step=true;sim.setup.warning=false");
	EG_LOG(SimStep, Debug) << "per step";
	EG_LOG(SimSetup, Warning) << "dropped";
	ok &= expect(messages.size() == 1 && messages[0].text == "per step", "the rules decide what is delivered");
	reset();
	return ok;
}

static bool testLazyEvaluation() {
	std::vector<Message> messages;
	setSink(collectInto(messages));
	int evaluated = 0;
	auto next = [&evaluated]() { return ++evaluated; };
	configure("sim.setup=false");
	EG_LOG(SimSetup, Warning) << next();
	bool ok = expect(evaluated == 0 && messages.empty(), "a disabled message evaluates no argument");
	configure("");
	EG_LOG(SimSetup, Warning) << next();
	ok &= expect(evaluated == 1 && messages.size() == 1 && messages[0].text == "1", "an enabled message evaluates its arguments once");
	reset();
	return ok;
}

// The else belongs to the if of the caller, whether or not the message is enabled.
static bool otherBranchTaken(bool flag) {
	bool other = false;
	if (flag)
		EG_LOG(AppRun, Info) << "x";
	else
		other = true;
	return other;
}

static bool testElseBinding() {
	std::vector<Message> messages;
	setSink(collectInto(messages));
	bool ok = expect(!otherBranchTaken(true) && messages.size() == 1, "the message is written when the condition holds");
	messages.clear();
	ok &= expect(otherBranchTaken(false) && messages.empty(), "the else branch is taken when the condition fails");
	configure("app.run=false");
	ok &= expect(!otherBranchTaken(true) && messages.empty(), "a disabled message does not take the else branch");
	ok &= expect(otherBranchTaken(false), "the else branch is taken for a disabled message");
	reset();
	return ok;
}

static bool testSink() {
	std::vector<Message> first;
	std::vector<Message> second;
	setSink(collectInto(first));
	EG_LOG(AppRun, Info) << "one";
	setSink(collectInto(second));
	EG_LOG(AppRun, Info) << "two";
	bool ok = expect(first.size() == 1 && first[0].text == "one", "the first sink gets the message before the swap");
	ok &= expect(second.size() == 1 && second[0].text == "two", "the second sink gets the message after the swap");

	setSink({});
	std::ostringstream captured;
	std::streambuf* original = std::cout.rdbuf(captured.rdbuf());
	EG_LOG(AppRun, Error) << "a" << 1;
	EG_LOG(Update, Debug) << "b\n";
	EG_LOG(SimStep, Debug) << "dropped";
	std::cout.rdbuf(original);
	ok &= expect(captured.str() == "a1b\n", "the default sink writes exactly the text to std::cout");
	ok &= expect(first.size() == 1 && second.size() == 1, "a replaced sink gets nothing");
	reset();
	return ok;
}

static void setLogVariable(const char* value) {
#ifdef _WIN32
	_putenv_s("EGTRAIN_LOG", value);
#else
	setenv("EGTRAIN_LOG", value, 1);
#endif
}

static void unsetLogVariable() {
#ifdef _WIN32
	_putenv_s("EGTRAIN_LOG", "");
#else
	unsetenv("EGTRAIN_LOG");
#endif
}

static bool testEnvironment() {
	setLogVariable("sim.step=true;io.zmq.warning=false");
	bool ok = expect(configureFromEnvironment(), "EGTRAIN_LOG with good rules is well formed");
	ok &= expect(disabledExactly([](int category, int level) { return category == ordinal(Category::IoZmq) && level == ordinal(Level::Warning); }),
		"EGTRAIN_LOG sets the rules");

	setLogVariable("sim.step");
	ok &= expect(!configureFromEnvironment(), "EGTRAIN_LOG with a malformed rule returns false");

	configure("*=false");
	setLogVariable("");
	ok &= expect(configureFromEnvironment(), "an empty EGTRAIN_LOG is well formed");
	ok &= expect(disabledExactly(defaultDisabled), "an empty EGTRAIN_LOG restores the defaults");

	configure("*=false");
	unsetLogVariable();
	ok &= expect(configureFromEnvironment(), "an unset EGTRAIN_LOG is well formed");
	ok &= expect(disabledExactly(defaultDisabled), "an unset EGTRAIN_LOG restores the defaults");
	reset();
	return ok;
}

int main() {
	bool ok = true;
	ok &= testNames();
	ok &= testDefaults();
	ok &= testSingleRules();
	ok &= testPatterns();
	ok &= testRuleOrder();
	ok &= testSeparators();
	ok &= testMalformedRules();
	ok &= testRestoreDefaults();
	ok &= testDelivery();
	ok &= testLazyEvaluation();
	ok &= testElseBinding();
	ok &= testSink();
	ok &= testEnvironment();
	return ok ? 0 : 1;
}
