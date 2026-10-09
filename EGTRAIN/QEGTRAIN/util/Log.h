#ifndef EGTRAIN_LOG_H
#define EGTRAIN_LOG_H

#include <functional>
#include <ostream>
#include <sstream>
#include <string>

// Categorised console output without Qt. A message has a category and a level. The rules given
// to configure() switch each pair on or off, and the sink receives the text of every enabled
// message. EG_LOG writes one message as a stream expression:
//
//   EG_LOG(SimSetup, Warning) << "Train " << name << " has no route\n";
//
// The text reaches the sink as streamed: the caller writes the newline.
namespace eglog {

enum class Category {
	SimSetup,
	SimStep,
	SimEvents,
	SimPax,
	IoRailml,
	IoZmq,
	AppRun,
	AppUi,
	Update,
};
enum class Level {
	Debug,
	Info,
	Warning,
	Error,
};

// Update is the last category and Error the last level. One 64-bit mask holds the state of
// every pair.
constexpr int kCategoryCount = static_cast<int>(Category::Update) + 1;
constexpr int kLevelCount = static_cast<int>(Level::Error) + 1;
static_assert(kCategoryCount * kLevelCount <= 64, "the enabled state of all pairs is one 64-bit mask");

// "sim.setup", "sim.step", "sim.events", "sim.pax", "io.railml", "io.zmq", "app.run", "app.ui"
// and "update".
const char* categoryName(Category category);

// "debug", "info", "warning" and "error".
const char* levelName(Level level);

// Tells whether a message of the pair is delivered. Every pair is enabled by default except
// sim.step at debug level.
bool enabled(Category category, Level level);

// Receives the text of an enabled message. It is called with a mutex held, so the texts of
// different threads do not interleave. A sink must not call the logging core or throw.
using Sink = std::function<void(Category, Level, const std::string& text)>;

// Replaces the sink. An empty sink restores the default sink, which writes the text unchanged
// to std::cout, for every category and level, without a prefix, a newline or a flush. It is
// std::cout and not the C stdout, because the console dock replaces the stream buffer of
// std::cout.
void setSink(Sink sink);

// Sets the enabled pairs: the defaults, then the rules in order, and the later rule wins.
// The rules use the syntax of QT_LOGGING_RULES, limited to this grammar:
//   - Rules are separated by ';' or a newline. Blanks (space, tab, carriage return) around a
//     rule, a name and a value are ignored. An empty rule is skipped.
//   - A rule is split at its first '=' and is `pattern=true` or `pattern=false`, in lower case.
//   - A pattern is a name pattern with an optional level suffix `.debug`, `.info`, `.warning`
//     or `.error`. The suffix is the text after the last '.' when it is a level name. Without
//     a suffix the rule applies to all four levels.
//   - A name pattern is `*`, a category name, or a name with a '*' at the start, at the end or
//     at both ends: `sim.*`, `*.run`, `*step*`. A '*' anywhere else is malformed.
//   - A pattern that matches no category is well formed and changes nothing.
// A malformed rule is skipped and the others still apply. Returns false when any rule is
// malformed. configure("") restores the defaults.
bool configure(const std::string& rules);

// Calls configure() with the value of the environment variable EGTRAIN_LOG, or with an empty
// string when it is not set. The caller reports a false result: this code never prints.
bool configureFromEnvironment();

// One message. The destructor passes the collected text to the sink unless it is empty.
class Line {
public:
	Line(Category category, Level level);
	Line(const Line&) = delete;
	Line& operator=(const Line&) = delete;
	~Line();

	std::ostream& stream() { return m_stream; }

private:
	Category m_category;
	Level m_level;
	std::ostringstream m_stream;
};

} // namespace eglog

// The category and the level are enumerator names without a scope. A disabled message evaluates
// none of its arguments. The `if ... else` form keeps an `else` after a statement that is the
// body of an `if` paired with the `if` of the caller.
#define EG_LOG(category, level)                                                  \
	if (!::eglog::enabled(::eglog::Category::category, ::eglog::Level::level)) { \
	} else                                                                       \
		::eglog::Line(::eglog::Category::category, ::eglog::Level::level).stream()

#endif // EGTRAIN_LOG_H
