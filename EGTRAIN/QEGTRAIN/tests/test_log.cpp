#include "util/Log.h"

#include <QString>
#include <QTest>

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace eglog;

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

// A stream buffer that counts how often it is synchronised.
class SyncCounter : public std::stringbuf {
public:
	int syncs = 0;

protected:
	int sync() override {
		++syncs;
		return std::stringbuf::sync();
	}
};

// The else belongs to the if of the caller, whether or not the message is enabled.
static bool otherBranchTaken(bool flag) {
	bool other = false;
	if (flag)
		EG_LOG(AppRun, Info) << "x";
	else
		other = true;
	return other;
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

class TestLog : public QObject {
	Q_OBJECT

private slots:
	void cleanup();
	void countsCategoriesAndLevels();
	void namesCategories_data();
	void namesCategories();
	void namesLevels_data();
	void namesLevels();
	void disablesOnlySimStepDebugByDefault();
	void appliesSingleRules();
	void appliesPatterns();
	void laterRuleWins();
	void acceptsSeparatorsAndBlanks();
	void rejectsMalformedRule_data();
	void rejectsMalformedRule();
	void emptyRulesRestoreDefaults();
	void deliversEnabledMessages();
	void evaluatesArgumentsOnlyWhenEnabled();
	void keepsElseWithCaller();
	void replacesSink();
	void flushesBothStreams();
	void readsRulesFromEnvironment();
};

// Every slot starts with the default rules and sink.
void TestLog::cleanup() {
	reset();
}

void TestLog::countsCategoriesAndLevels() {
	// Nine categories and four levels.
	QCOMPARE(kCategoryCount, kCategories);
	QCOMPARE(kLevelCount, kLevels);
}

void TestLog::namesCategories_data() {
	QTest::addColumn<int>("category");
	const char* const categories[] = {"sim.setup", "sim.step", "sim.events", "sim.pax", "io.railml", "io.zmq", "app.run", "app.ui", "update"};
	for (int category = 0; category < kCategories; ++category)
		QTest::newRow(categories[category]) << category;
}

void TestLog::namesCategories() {
	QFETCH(int, category);
	QCOMPARE(categoryName(static_cast<Category>(category)), QTest::currentDataTag());
}

void TestLog::namesLevels_data() {
	QTest::addColumn<int>("level");
	const char* const levels[] = {"debug", "info", "warning", "error"};
	for (int level = 0; level < kLevels; ++level)
		QTest::newRow(levels[level]) << level;
}

void TestLog::namesLevels() {
	QFETCH(int, level);
	QCOMPARE(levelName(static_cast<Level>(level)), QTest::currentDataTag());
}

void TestLog::disablesOnlySimStepDebugByDefault() {
	QVERIFY2(configure(""), "empty rules are well formed");
	QVERIFY2(disabledExactly(defaultDisabled), "only sim.step at debug level is disabled by default");
}

void TestLog::appliesSingleRules() {
	QVERIFY2(configure("sim.step=true"), "sim.step=true is well formed");
	QVERIFY2(disabledExactly([](int, int) { return false; }), "sim.step=true enables sim.step at debug level");

	QVERIFY2(configure("sim.setup.warning=false"), "a level rule is well formed");
	QVERIFY2(disabledExactly([](int category, int level) {
		return (category == ordinal(Category::SimSetup) && level == ordinal(Level::Warning)) || defaultDisabled(category, level);
	}),
		"sim.setup.warning=false disables that pair only");

	QVERIFY2(configure("*.debug=false"), "*.debug=false is well formed");
	QVERIFY2(disabledExactly([](int, int level) { return level == ordinal(Level::Debug); }), "*.debug=false disables debug for every category");

	QVERIFY2(configure("io.*.warning=false"), "a prefix and a level are well formed");
	QVERIFY2(disabledExactly([](int category, int level) {
		return ((category == ordinal(Category::IoRailml) || category == ordinal(Category::IoZmq)) && level == ordinal(Level::Warning))
			|| defaultDisabled(category, level);
	}),
		"io.*.warning=false disables warning for the io categories");
}

void TestLog::appliesPatterns() {
	QVERIFY2(configure("sim.*=false"), "sim.*=false is well formed");
	QVERIFY2(disabledExactly([](int category, int) { return category <= ordinal(Category::SimPax); }),
		"sim.* disables the four sim categories only");

	QVERIFY2(configure("*.run=false"), "*.run=false is well formed");
	QVERIFY2(disabledExactly([](int category, int level) { return category == ordinal(Category::AppRun) || defaultDisabled(category, level); }),
		"*.run disables app.run only");

	QVERIFY2(configure("*step*=false"), "*step*=false is well formed");
	QVERIFY2(disabledExactly([](int category, int) { return category == ordinal(Category::SimStep); }), "*step* disables sim.step only");

	QVERIFY2(configure("update=false"), "a category name is well formed");
	QVERIFY2(disabledExactly([](int category, int level) { return category == ordinal(Category::Update) || defaultDisabled(category, level); }),
		"a category name disables that category only");

	QVERIFY2(configure("*=false"), "*=false is well formed");
	QVERIFY2(disabledExactly([](int, int) { return true; }), "* disables every pair");
	QVERIFY2(configure("**=false"), "**=false is well formed");
	QVERIFY2(disabledExactly([](int, int) { return true; }), "** counts as *");

	QVERIFY2(configure("nothing.here=false;*.nothing=false"), "a pattern that matches no category is well formed");
	QVERIFY2(disabledExactly(defaultDisabled), "a pattern that matches no category changes nothing");
}

void TestLog::laterRuleWins() {
	QVERIFY2(configure("sim.step=false;sim.step=true"), "two rules are well formed");
	QVERIFY2(disabledExactly([](int, int) { return false; }), "the later true wins");
	QVERIFY2(configure("sim.step=true;sim.step=false"), "two reversed rules are well formed");
	QVERIFY2(disabledExactly([](int category, int) { return category == ordinal(Category::SimStep); }), "the later false wins");
	QVERIFY2(configure("*=false;app.run.error=true"), "a general and a narrow rule are well formed");
	QVERIFY2(disabledExactly([](int category, int level) { return !(category == ordinal(Category::AppRun) && level == ordinal(Level::Error)); }),
		"a narrow rule after a general one wins");
}

void TestLog::acceptsSeparatorsAndBlanks() {
	QVERIFY2(configure("  sim.setup.info = false ; \n\t io.zmq=false \r\n;;sim.pax.warning=false;"), "separators and blanks are accepted");
	QVERIFY2(disabledExactly([](int category, int level) {
		return (category == ordinal(Category::SimSetup) && level == ordinal(Level::Info)) || category == ordinal(Category::IoZmq)
			|| (category == ordinal(Category::SimPax) && level == ordinal(Level::Warning)) || defaultDisabled(category, level);
	}),
		"semicolons, newlines and blanks separate the rules");
}

void TestLog::rejectsMalformedRule_data() {
	QTest::addColumn<QString>("malformed");
	for (const char* malformed : {"sim.step", "sim.step=maybe", "=true", ".debug=true", "si*m=true", "sim.step=TRUE", "sim.step=",
			 "sim.*x=true", "*sim*x*=true"})
		QTest::newRow(malformed) << QString::fromLatin1(malformed);
}

void TestLog::rejectsMalformedRule() {
	QFETCH(QString, malformed);
	const std::string rule = malformed.toStdString();
	QVERIFY2(!configure(rule), "a malformed rule returns false");
	QVERIFY2(disabledExactly(defaultDisabled), "a malformed rule changes nothing");
	QVERIFY2(!configure("sim.setup.error=false;" + rule), "a malformed rule after a good one returns false");
	QVERIFY2(!pairEnabled(ordinal(Category::SimSetup), ordinal(Level::Error)), "the good rule before a malformed one is applied");
	QVERIFY2(!configure(rule + "\nsim.pax.error=false"), "a malformed rule before a good one returns false");
	QVERIFY2(!pairEnabled(ordinal(Category::SimPax), ordinal(Level::Error)), "the good rule after a malformed one is applied");
}

void TestLog::emptyRulesRestoreDefaults() {
	configure("*=false");
	QVERIFY2(configure(""), "empty rules are well formed after other rules");
	QVERIFY2(disabledExactly(defaultDisabled), "empty rules restore the defaults");
	configure("sim.step");
	QVERIFY2(configure(""), "empty rules are well formed after a malformed rule");
	QVERIFY2(disabledExactly(defaultDisabled), "empty rules restore the defaults after a malformed rule");
}

void TestLog::deliversEnabledMessages() {
	std::vector<Message> messages;
	setSink(collectInto(messages));
	EG_LOG(SimSetup, Warning) << "n=" << 42 << " x=" << 1.5 << " s=" << std::string("abc") << "\nnext" << std::endl;
	// One message is delivered.
	QCOMPARE(messages.size(), std::size_t(1));
	// Category and level are delivered.
	QVERIFY(messages[0].category == Category::SimSetup);
	QVERIFY(messages[0].level == Level::Warning);
	// The text is delivered as streamed.
	QCOMPARE(messages[0].text, "n=42 x=1.5 s=abc\nnext\n");

	messages.clear();
	EG_LOG(SimStep, Debug) << "per step";
	QVERIFY2(messages.empty(), "a message of a disabled pair is not delivered");
	EG_LOG(SimSetup, Error);
	QVERIFY2(messages.empty(), "an empty message is not delivered");
	configure("sim.step=true;sim.setup.warning=false");
	EG_LOG(SimStep, Debug) << "per step";
	EG_LOG(SimSetup, Warning) << "dropped";
	// The rules decide what is delivered.
	QCOMPARE(messages.size(), std::size_t(1));
	QCOMPARE(messages[0].text, "per step");
}

void TestLog::evaluatesArgumentsOnlyWhenEnabled() {
	std::vector<Message> messages;
	setSink(collectInto(messages));
	int evaluated = 0;
	auto next = [&evaluated]() { return ++evaluated; };
	configure("sim.setup=false");
	EG_LOG(SimSetup, Warning) << next();
	// A disabled message evaluates no argument.
	QCOMPARE(evaluated, 0);
	QVERIFY(messages.empty());
	configure("");
	EG_LOG(SimSetup, Warning) << next();
	// An enabled message evaluates its arguments once.
	QCOMPARE(evaluated, 1);
	QCOMPARE(messages.size(), std::size_t(1));
	QCOMPARE(messages[0].text, "1");
}

void TestLog::keepsElseWithCaller() {
	std::vector<Message> messages;
	setSink(collectInto(messages));
	// The message is written when the condition holds.
	QVERIFY(!otherBranchTaken(true));
	QCOMPARE(messages.size(), std::size_t(1));
	messages.clear();
	// The else branch is taken when the condition fails.
	QVERIFY(otherBranchTaken(false));
	QVERIFY(messages.empty());
	configure("app.run=false");
	// A disabled message does not take the else branch.
	QVERIFY(!otherBranchTaken(true));
	QVERIFY(messages.empty());
	QVERIFY2(otherBranchTaken(false), "the else branch is taken for a disabled message");
}

void TestLog::replacesSink() {
	std::vector<Message> first;
	std::vector<Message> second;
	setSink(collectInto(first));
	EG_LOG(AppRun, Info) << "one";
	setSink(collectInto(second));
	EG_LOG(AppRun, Info) << "two";
	// The first sink gets the message before the swap.
	QCOMPARE(first.size(), std::size_t(1));
	QCOMPARE(first[0].text, "one");
	// The second sink gets the message after the swap.
	QCOMPARE(second.size(), std::size_t(1));
	QCOMPARE(second[0].text, "two");

	setSink({});
	std::ostringstream captured;
	std::streambuf* original = std::cout.rdbuf(captured.rdbuf());
	EG_LOG(AppRun, Error) << "a" << 1;
	EG_LOG(Update, Debug) << "b\n";
	EG_LOG(SimStep, Debug) << "dropped";
	std::cout.rdbuf(original);
	// The default sink writes exactly the text to std::cout.
	QCOMPARE(captured.str(), "a1b\n");
	// A replaced sink gets nothing.
	QCOMPARE(first.size(), std::size_t(1));
	QCOMPARE(second.size(), std::size_t(1));
}

void TestLog::flushesBothStreams() {
	SyncCounter out;
	SyncCounter err;
	std::streambuf* originalOut = std::cout.rdbuf(&out);
	std::streambuf* originalErr = std::cerr.rdbuf(&err);
	flush();
	std::cout.rdbuf(originalOut);
	std::cerr.rdbuf(originalErr);
	// Flush synchronises std::cout and std::cerr.
	QCOMPARE_GT(out.syncs, 0);
	QCOMPARE_GT(err.syncs, 0);
}

void TestLog::readsRulesFromEnvironment() {
	setLogVariable("sim.step=true;io.zmq.warning=false");
	QVERIFY2(configureFromEnvironment(), "EGTRAIN_LOG with good rules is well formed");
	QVERIFY2(disabledExactly([](int category, int level) { return category == ordinal(Category::IoZmq) && level == ordinal(Level::Warning); }),
		"EGTRAIN_LOG sets the rules");

	setLogVariable("sim.step");
	QVERIFY2(!configureFromEnvironment(), "EGTRAIN_LOG with a malformed rule returns false");

	configure("*=false");
	setLogVariable("");
	QVERIFY2(configureFromEnvironment(), "an empty EGTRAIN_LOG is well formed");
	QVERIFY2(disabledExactly(defaultDisabled), "an empty EGTRAIN_LOG restores the defaults");

	configure("*=false");
	unsetLogVariable();
	QVERIFY2(configureFromEnvironment(), "an unset EGTRAIN_LOG is well formed");
	QVERIFY2(disabledExactly(defaultDisabled), "an unset EGTRAIN_LOG restores the defaults");
}

QTEST_APPLESS_MAIN(TestLog)
#include "test_log.moc"
