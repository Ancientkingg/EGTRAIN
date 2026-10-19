#ifndef EGTRAIN_LOG_H
#define EGTRAIN_LOG_H

#include <functional>
#include <ostream>
#include <sstream>
#include <string>

/// \file
/// The logging core: categorised console output without Qt, and the EG_LOG macro.

/// Categorised console output without Qt. A message has a category and a level. The rules given
/// to configure() switch each pair on or off, and the sink receives the text of every enabled
/// message. EG_LOG writes one message as a stream expression:
///
///     EG_LOG(SimSetup, Warning) << "Train " << name << " has no route\n";
///
/// The text reaches the sink as streamed: the caller writes the newline.
namespace eglog {

/// The area a message comes from. categoryName() gives the name that the rules of configure() use.
enum class Category {
	SimSetup,  ///< "sim.setup": the set-up of a run, and problems in the scene data or the model.
	SimStep,   ///< "sim.step": messages that a train writes once per step in the trajectory code.
	SimEvents, ///< "sim.events": coupling and following mode, arrivals and departures, and problems in the
			   ///< acceleration and braking calculation.
	SimPax,	   ///< "sim.pax": passengers.
	IoRailml,  ///< "io.railml": the messages of io/RttpXml.cpp.
	IoZmq,	   ///< "io.zmq": the ZeroMQ transports, which are the legacy sender of the -TSM and -RC channels
			   ///< and the live server.
	AppRun,	   ///< "app.run": the banners and the progress of a run.
	AppUi,	   ///< "app.ui": the messages of Qt (qDebug, qInfo, qWarning, qCritical, qFatal).
	Update,	   ///< "update": no product code logs in this category.
};
/// The severity of a message. levelName() gives the name that the rules of configure() use.
enum class Level {
	Debug,	 ///< "debug"
	Info,	 ///< "info"
	Warning, ///< "warning"
	Error,	 ///< "error"
};

/// Number of categories: Update is the last.
constexpr int kCategoryCount = static_cast<int>(Category::Update) + 1;
/// Number of levels: Error is the last.
constexpr int kLevelCount = static_cast<int>(Level::Error) + 1;
static_assert(kCategoryCount * kLevelCount <= 64, "the enabled state of all pairs is one 64-bit mask");

/// The rule name of \p category, given at each enumerator of Category.
const char* categoryName(Category category);

/// The rule name of \p level, given at each enumerator of Level.
const char* levelName(Level level);

/// Tells whether a message of the pair is delivered. Every pair is enabled by default except
/// sim.step at debug level.
bool enabled(Category category, Level level);

/// Receives the text of an enabled message. It is called with a mutex held, so the texts of
/// different threads do not interleave. A sink must not call the logging core or throw.
using Sink = std::function<void(Category, Level, const std::string& text)>;

/// Replaces the sink. An empty sink restores the default sink, which writes the text unchanged
/// to std::cout, for every category and level, without a prefix, a newline or a flush. It is
/// std::cout and not the C stdout, because the console dock replaces the stream buffer of
/// std::cout.
void setSink(Sink sink);

/// Sets the enabled pairs: the defaults, then the rules in order, and the later rule wins.
/// The rules use the syntax of QT_LOGGING_RULES, limited to this grammar:
///   - Rules are separated by ';' or a newline. Blanks (space, tab, carriage return) around a
///     rule, a name and a value are ignored. An empty rule is skipped.
///   - A rule is split at its first '=' and is `pattern=true` or `pattern=false`, in lower case.
///   - A pattern is a name pattern with an optional level suffix `.debug`, `.info`, `.warning`
///     or `.error`. The suffix is the text after the last '.' when it is a level name. Without
///     a suffix the rule applies to all four levels.
///   - A name pattern is `*`, a category name, or a name with a '*' at the start, at the end or
///     at both ends: `sim.*`, `*.run`, `*step*`. A '*' anywhere else is malformed.
///   - A pattern that matches no category is well formed and changes nothing.
///
/// A malformed rule is skipped and the others still apply. Returns false when any rule is
/// malformed. configure("") restores the defaults.
bool configure(const std::string& rules);

/// Calls configure() with the value of the environment variable EGTRAIN_LOG, or with an empty
/// string when it is not set. The caller reports a false result: this code never prints.
bool configureFromEnvironment();

/// Flushes std::cout and std::cerr, which the default sink and the sink of the application write
/// without a flush. A caller that ends the process without leaving main(), with std::abort(),
/// calls it after its last message.
void flush();

/// One message, which EG_LOG creates. It does not check enabled(): the macro does.
class Line {
public:
	/// Starts an empty message of the category and the level.
	Line(Category category, Level level);
	Line(const Line&) = delete;
	Line& operator=(const Line&) = delete;
	/// Passes the collected text to the sink unless it is empty. When an argument of the stream
	/// expression throws, the text streamed before it is still passed.
	~Line();

	/// The stream that collects the text of the message.
	std::ostream& stream() { return m_stream; }

private:
	Category m_category;
	Level m_level;
	std::ostringstream m_stream;
};

} // namespace eglog

/// The category and the level are enumerator names without a scope. A disabled message evaluates
/// none of its arguments. The `if ... else` form keeps an `else` after a statement that is the
/// body of an `if` paired with the `if` of the caller.
#define EG_LOG(category, level)                                                  \
	if (!::eglog::enabled(::eglog::Category::category, ::eglog::Level::level)) { \
	} else                                                                       \
		::eglog::Line(::eglog::Category::category, ::eglog::Level::level).stream()

#endif // EGTRAIN_LOG_H
