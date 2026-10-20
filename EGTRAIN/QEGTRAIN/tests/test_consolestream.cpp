#include "widgets/ConsoleWidget.h"

#include "util/Log.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QLoggingCategory>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using eglog::Category;
using eglog::Level;

Q_LOGGING_CATEGORY(testCategory, "test.cat")

// Writes to the C stderr, because the cases replace the stream buffer of std::cerr.
static bool expect(bool condition, const char* message) {
	if (!condition)
		std::fprintf(stderr, "failed: %s\n", message);
	return condition;
}

class ReentrantBuf : public std::stringbuf {
protected:
	std::streamsize xsputn(const char* s, std::streamsize n) override {
		std::cerr << "mirror";
		return std::stringbuf::xsputn(s, n);
	}
};

class RejectingBuf : public std::streambuf {
protected:
	std::streamsize xsputn(const char*, std::streamsize) override { return 0; }
};

struct Message {
	Category category;
	Level level;
	std::string text;

	bool operator==(const Message& other) const {
		return category == other.category && level == other.level && text == other.text;
	}
};

// Replaces the stream buffers of std::cout and std::cerr by string buffers while it lives.
struct StreamCapture {
	std::stringbuf out;
	std::stringbuf err;
	std::streambuf* oldOut = std::cout.rdbuf(&out);
	std::streambuf* oldErr = std::cerr.rdbuf(&err);

	StreamCapture() = default;
	StreamCapture(const StreamCapture&) = delete;
	StreamCapture& operator=(const StreamCapture&) = delete;
	~StreamCapture() {
		std::cout.rdbuf(oldOut);
		std::cerr.rdbuf(oldErr);
		std::cout.clear();
		std::cerr.clear();
	}
};

// Puts back what installConsoleLogging and the cases change: the log file, the sink, the Qt
// message handler, the rules and the two variables.
struct LoggingReset {
	LoggingReset() = default;
	LoggingReset(const LoggingReset&) = delete;
	LoggingReset& operator=(const LoggingReset&) = delete;
	~LoggingReset() {
		qunsetenv("EGTRAIN_LOG");
		qunsetenv("EGTRAIN_LOG_FILE");
		installConsoleLogging();
		eglog::setSink({});
		qInstallMessageHandler(nullptr);
		eglog::configure("");
	}
};

static eglog::Sink collectInto(std::vector<Message>& messages) {
	return [&messages](Category category, Level level, const std::string& text) { messages.push_back({category, level, text}); };
}

static std::string readFile(const QString& path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return "<unreadable>";
	return file.readAll().toStdString();
}

static bool testDeliver() {
	const std::string text = "text\n";
	bool ok = expect(!ConsoleStreambuf::deliver(Category::AppUi, Level::Warning, text), "deliver without an installed instance returns false");
	StreamCapture streams;
	ConsoleStreambuf capture([](QString) {});
	capture.install();
	ok &= expect(ConsoleStreambuf::deliver(Category::AppUi, Level::Warning, text), "deliver to an installed instance returns true");
	capture.restore();
	ok &= expect(!ConsoleStreambuf::deliver(Category::AppUi, Level::Warning, text), "deliver after restore returns false");
	return ok;
}

static bool testDeliverWithMessageCallback() {
	StreamCapture streams;
	std::vector<std::string> events;
	std::vector<Message> messages;
	ConsoleStreambuf capture([&events](QString text) { events.push_back("plain " + text.toStdString()); });
	capture.setMessageCallback([&events, &messages](Category category, Level level, QString text) {
		events.push_back("message " + text.toStdString());
		messages.push_back({category, level, text.toStdString()});
	});
	capture.install();
	std::cout << "abc";
	ConsoleStreambuf::deliver(Category::AppUi, Level::Warning, "text\n");
	capture.restore();

	bool ok = expect(streams.out.str() == "abctext\n", "a message reaches the original cout once, as given");
	ok &= expect(streams.err.str().empty(), "a message does not reach the original cerr");
	ok &= expect((messages == std::vector<Message>{{Category::AppUi, Level::Warning, "text\n"}}),
		"the message callback receives category, level and text once");
	ok &= expect((events == std::vector<std::string>{"plain abc", "message text\n"}),
		"the pending partial line reaches the plain callback before the message, and the message does not");
	return ok;
}

static bool testDeliverWithoutMessageCallback() {
	StreamCapture streams;
	std::string plain;
	ConsoleStreambuf capture([&plain](QString text) { plain += text.toStdString(); });
	capture.install();
	ConsoleStreambuf::deliver(Category::SimEvents, Level::Info, "text\n");
	capture.restore();
	bool ok = expect(streams.out.str() == "text\n", "without a message callback the original cout receives the text once");
	ok &= expect(plain == "text\n", "without a message callback the plain callback receives the text");
	return ok;
}

static bool testMessageText() {
	bool ok = expect(consoleMessageText(Category::AppUi, Level::Warning, "x\n") == "warning app.ui: x\n", "level and category come first");
	ok &= expect(consoleMessageText(Category::SimEvents, Level::Debug, "x") == "debug sim.events: x", "a text without a newline gets the prefix");
	ok &= expect(consoleMessageText(Category::SimSetup, Level::Error, "a\nb\n") == "error sim.setup: a\nb\n", "only the first line gets the prefix");
	ok &= expect(consoleMessageText(Category::SimSetup, Level::Info, "\n\nSelected\n") == "\n\ninfo sim.setup: Selected\n",
		"the prefix follows leading line breaks");
	ok &= expect(consoleMessageText(Category::SimSetup, Level::Info, "\rStep 1") == "\rinfo sim.setup: Step 1", "the prefix follows a carriage return");
	ok &= expect(consoleMessageText(Category::SimSetup, Level::Info, "\n") == "\n", "a text of line breaks is unchanged");
	ok &= expect(consoleMessageText(Category::SimSetup, Level::Info, "").isEmpty(), "an empty text is unchanged");
	return ok;
}

static bool testLogSink() {
	bool ok = true;
	{
		StreamCapture streams;
		consoleLogSink(Category::AppUi, Level::Warning, "ui\n");
		consoleLogSink(Category::SimSetup, Level::Info, "setup");
		ok &= expect(streams.err.str() == "ui\n", "without a dock app.ui goes to cerr, as given");
		ok &= expect(streams.out.str() == "setup", "without a dock every other category goes to cout, as given");
	}
	StreamCapture streams;
	std::vector<Message> messages;
	ConsoleStreambuf capture([](QString) {});
	capture.setMessageCallback([&messages](Category category, Level level, QString text) { messages.push_back({category, level, text.toStdString()}); });
	capture.install();
	consoleLogSink(Category::AppUi, Level::Warning, "ui\n");
	consoleLogSink(Category::SimSetup, Level::Info, "setup\n");
	capture.restore();
	ok &= expect(streams.out.str() == "ui\nsetup\n", "with a dock every category goes to the original cout, as given");
	ok &= expect(streams.err.str().empty(), "with a dock nothing goes to the original cerr");
	ok &= expect((messages == std::vector<Message>{{Category::AppUi, Level::Warning, "ui\n"}, {Category::SimSetup, Level::Info, "setup\n"}}),
		"with a dock the messages reach the dock with their category and level");
	return ok;
}

static bool testQtMessageHandler() {
	LoggingReset reset;
	testCategory(); // Qt reads its rules when a category is first used, before the handler exists.
	qSetMessagePattern(QStringLiteral("%{if-category}%{category}: %{endif}%{message}"));
	std::vector<Message> messages;
	eglog::setSink(collectInto(messages));
	qInstallMessageHandler(qtMessageToLog);

	qWarning("warning text");
	qDebug("debug text");
	qInfo("info text");
	qCritical("critical text");
	qCWarning(testCategory, "category text");
	const std::vector<Message> expected = {
		{Category::AppUi, Level::Warning, "warning text\n"},
		{Category::AppUi, Level::Debug, "debug text\n"},
		{Category::AppUi, Level::Info, "info text\n"},
		{Category::AppUi, Level::Error, "critical text\n"},
		{Category::AppUi, Level::Warning, "test.cat: category text\n"},
	};
	bool ok = expect(messages == expected, "each Qt message reaches the core as app.ui with its level and the text of Qt's own handler");

	messages.clear();
	eglog::configure("app.ui.warning=false");
	qWarning("hidden");
	qCritical("shown");
	ok &= expect((messages == std::vector<Message>{{Category::AppUi, Level::Error, "shown\n"}}), "EGTRAIN_LOG rules switch one level of app.ui off");

	// The handler is called directly, because Qt ends the program after a fatal message.
	messages.clear();
	eglog::configure("app.ui=false");
	qtMessageToLog(QtFatalMsg, QMessageLogContext(), QStringLiteral("fatal text"));
	ok &= expect((messages == std::vector<Message>{{Category::AppUi, Level::Error, "fatal text\n"}}), "a fatal Qt message is never filtered");
	return ok;
}

static bool testInstallConsoleLogging() {
	LoggingReset reset;
	StreamCapture streams;

	qputenv("EGTRAIN_LOG", "app.ui.warning=false");
	bool ok = expect(installConsoleLogging().empty(), "good rules give no problem");
	qWarning("hidden");
	qCritical("shown");
	ok &= expect(streams.err.str() == "shown\n", "the rules of EGTRAIN_LOG filter the Qt messages, and the rest go to cerr");
	ok &= expect(streams.out.str().empty(), "a Qt message does not reach cout without a dock");

	qputenv("EGTRAIN_LOG", "oops;app.ui.warning=false");
	const std::string problems = installConsoleLogging();
	ok &= expect(problems.find("EGTRAIN_LOG has a malformed rule") != std::string::npos, "a malformed rule is reported");
	qWarning("hidden again");
	ok &= expect(streams.err.str() == "shown\n", "the other rules still apply after a malformed rule");

	qunsetenv("EGTRAIN_LOG");
	ok &= expect(installConsoleLogging().empty(), "an unset EGTRAIN_LOG gives no problem");
	qWarning("visible");
	ok &= expect(streams.err.str() == "shown\nvisible\n", "the defaults apply when EGTRAIN_LOG is unset");
	return ok;
}

static bool testLogFile() {
	LoggingReset reset;
	StreamCapture streams;
	QTemporaryDir directory;
	const QString path = directory.filePath(QStringLiteral("log.txt"));

	qputenv("EGTRAIN_LOG_FILE", path.toLocal8Bit());
	bool ok = expect(installConsoleLogging().empty(), "a log file that can be opened gives no problem");
	qWarning("to file");
	EG_LOG(SimSetup, Info) << "\nSelected\n";
	eglog::configure("sim.setup=false");
	EG_LOG(SimSetup, Info) << "switched off\n";
	eglog::configure("");
	ok &= expect(readFile(path) == "warning app.ui: to file\n\ninfo sim.setup: Selected\n", "the log file holds the enabled messages with level and category");
	ok &= expect(streams.err.str() == "to file\n" && streams.out.str() == "\nSelected\n", "the console streams hold the text as given");

	installConsoleLogging();
	EG_LOG(SimSetup, Info) << "\rStep 1";
	EG_LOG(SimSetup, Info) << "\rStep 2\n";
	qCritical("again");
	ok &= expect(readFile(path) == "warning app.ui: to file\n\ninfo sim.setup: Selected\n\rinfo sim.setup: Step 1\n\rinfo sim.setup: Step 2\nerror app.ui: again\n",
		"a later call appends to the log file, and each message starts on a line of its own");
	const std::string written = readFile(path);

	qunsetenv("EGTRAIN_LOG_FILE");
	installConsoleLogging();
	qCritical("not in the file");
	ok &= expect(readFile(path) == written, "without the variable the log file is closed");

	qputenv("EGTRAIN_LOG_FILE", directory.filePath(QStringLiteral("missing/log.txt")).toLocal8Bit());
	ok &= expect(installConsoleLogging().find("EGTRAIN_LOG_FILE could not be opened") != std::string::npos, "a log file that cannot be opened is reported");
	return ok;
}

// A Qt message raised while the sink runs must not enter the core again, whose mutex is held.
static bool testQtMessageInsideSink() {
	LoggingReset reset;
	StreamCapture streams;
	ConsoleStreambuf capture([](QString) {});
	capture.setMessageCallback([](Category, Level, QString) { qWarning("nested"); });
	capture.install();
	installConsoleLogging();
	EG_LOG(SimEvents, Info) << "outer\n";
	capture.restore();
	bool ok = expect(streams.out.str() == "outer\n", "the outer message reaches the original cout");
	ok &= expect(streams.err.str() == "nested\n", "the nested message goes to cerr and does not wait for the core");
	return ok;
}

static bool testDock() {
	LoggingReset reset;
	StreamCapture streams;
	bool ok = true;
	{
		ConsoleWidget dock;
		QPlainTextEdit* edit = dock.findChild<QPlainTextEdit*>();
		if (!expect(edit != nullptr, "the dock holds a text edit"))
			return false;
		installConsoleLogging();

		qWarning("first");
		ok &= expect(edit->toPlainText() == "warning app.ui: first\n", "the dock shows level and category of a Qt message");
		ok &= expect(streams.out.str() == "first\n" && streams.err.str().empty(), "the original cout holds the text once, as given");

		std::cout << "partial";
		qCritical("second");
		ok &= expect(edit->toPlainText() == "warning app.ui: first\npartial\nerror app.ui: second\n", "a message starts on a line of its own");

		EG_LOG(SimSetup, Info) << "\nSelected\n";
		std::cout << "plain\n";
		ok &= expect(edit->toPlainText() == "warning app.ui: first\npartial\nerror app.ui: second\n\ninfo sim.setup: Selected\nplain\n",
			"a message of any category is prefixed, plain text is not");

		std::thread([] { EG_LOG(SimEvents, Warning) << "from a thread\n"; }).join();
		QCoreApplication::processEvents();
		ok &= expect(edit->toPlainText().endsWith("plain\nwarning sim.events: from a thread\n"), "a message from another thread reaches the dock");
	}
	ok &= expect(!ConsoleStreambuf::deliver(Category::AppUi, Level::Warning, "gone\n"), "the destroyed dock is no longer delivered to");
	return ok;
}

// The messages of a run: progress lines that start with a carriage return, and the end line that starts with a line break.
// The dock shows each on a line of its own, with level and category in front, and no empty line before a progress line.
static bool testDockRunMessages() {
	LoggingReset reset;
	StreamCapture streams;
	ConsoleWidget dock;
	QPlainTextEdit* edit = dock.findChild<QPlainTextEdit*>();
	if (!expect(edit != nullptr, "the dock holds a text edit"))
		return false;
	installConsoleLogging();

	EG_LOG(AppRun, Info) << "\n\nSimulating Train Runs...\n\n";
	EG_LOG(AppRun, Info) << "\r Time of simulation is 0";
	EG_LOG(AppRun, Info) << "\r Time of simulation is 100";
	EG_LOG(AppRun, Info) << "\r Time of simulation is 200";
	EG_LOG(AppRun, Info) << "\n End of Simulation";
	const QString expected = QStringLiteral(
		"\n\ninfo app.run: Simulating Train Runs...\n\n"
		"info app.run:  Time of simulation is 0\n"
		"info app.run:  Time of simulation is 100\n"
		"info app.run:  Time of simulation is 200\n"
		"info app.run:  End of Simulation");
	return expect(edit->toPlainText() == expected, "each progress line and the end line are on a line of their own, with no empty line before a progress line");
}

int main(int argc, char** argv) {
	// Qt reads these once, at its first message.
	qunsetenv("QT_MESSAGE_PATTERN");
	qunsetenv("QT_LOGGING_RULES");
	qunsetenv("EGTRAIN_LOG");
	qunsetenv("EGTRAIN_LOG_FILE");
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication application(argc, argv);

	std::stringbuf coutOriginal;
	std::stringbuf cerrOriginal;
	std::string callback;

	std::streambuf* realCout = std::cout.rdbuf(&coutOriginal);
	std::streambuf* realCerr = std::cerr.rdbuf(&cerrOriginal);
	{
		ConsoleStreambuf capture([&callback](QString text) {
			callback += text.toStdString();
		});
		capture.install();

		std::cout << "cout-bulk";
		std::cout.put('!');
		std::cout.flush();
		std::cerr << "cerr-bulk";
		std::cerr.put('?');
		std::cerr.flush();
		std::cout << "cout-tail";
		std::cerr << "cerr-tail";
		capture.restore();
		capture.restore();
	}
	std::cout.rdbuf(realCout);
	std::cerr.rdbuf(realCerr);

	bool ok = true;
	ok &= expect(callback == "cout-bulk!cerr-bulk?cout-tailcerr-tail", "callback receives each write exactly once");
	ok &= expect(coutOriginal.str() == "cout-bulk!cout-tail", "cout original receives only cout text");
	ok &= expect(cerrOriginal.str() == "cerr-bulk?cerr-tail", "cerr original receives only cerr text");

	ReentrantBuf reentrant;
	std::stringbuf reentrantCerr;
	std::string reentrantCallback;
	std::cout.rdbuf(&reentrant);
	std::cerr.rdbuf(&reentrantCerr);
	{
		ConsoleStreambuf capture([&reentrantCallback](QString text) {
			reentrantCallback += text.toStdString();
		});
		capture.install();
		std::cout.write("x", 1);
		std::cout.flush();
		capture.restore();
	}
	ok &= expect(reentrantCallback == "mirrorx", "reentrant original buffer does not deadlock or lose callback text");
	ok &= expect(reentrant.str() == "x", "reentrant original receives cout text");
	ok &= expect(reentrantCerr.str() == "mirror", "reentrant original may write through cerr");

	RejectingBuf rejecting;
	std::stringbuf rejectingCerr;
	std::string rejectingCallback;
	std::cout.rdbuf(&rejecting);
	std::cerr.rdbuf(&rejectingCerr);
	{
		ConsoleStreambuf capture([&rejectingCallback](QString text) {
			rejectingCallback += text.toStdString();
		});
		capture.install();
		std::cout.write("lost", 4);
		capture.restore();
	}
	ok &= expect(rejectingCallback == "lost", "callback keeps text rejected by original destination");

	std::cout.rdbuf(realCout);
	std::cerr.rdbuf(realCerr);
	std::cout.clear();
	std::cerr.clear();

	ok &= testDeliver();
	ok &= testDeliverWithMessageCallback();
	ok &= testDeliverWithoutMessageCallback();
	ok &= testMessageText();
	ok &= testLogSink();
	ok &= testQtMessageHandler();
	ok &= testInstallConsoleLogging();
	ok &= testLogFile();
	ok &= testQtMessageInsideSink();
	ok &= testDock();
	ok &= testDockRunMessages();
	return ok ? 0 : 1;
}
