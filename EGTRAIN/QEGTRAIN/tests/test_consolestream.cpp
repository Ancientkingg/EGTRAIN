#include "widgets/ConsoleWidget.h"

#include "util/Log.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QLoggingCategory>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using eglog::Category;
using eglog::Level;

Q_LOGGING_CATEGORY(testCategory, "test.cat")

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

class TestConsoleStream : public QObject {
	Q_OBJECT

private slots:
	void passesEachWriteOnce();
	void survivesReentrantOriginal();
	void keepsTextThatOriginalRejects();
	void deliversOnlyToInstalledInstance();
	void deliversToMessageCallback();
	void deliversWithoutMessageCallback();
	void prefixesMessageText();
	void logSinkFollowsDock();
	void qtMessagesReachCore();
	void installAppliesRules();
	void writesLogFile();
	void qtMessageInsideSinkSkipsCore();
	void dockShowsMessages();
	void dockPutsRunMessagesOnLines();
};

void TestConsoleStream::passesEachWriteOnce() {
	std::string callback;
	StreamCapture streams;
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

	// Callback receives each write exactly once.
	QCOMPARE(callback, "cout-bulk!cerr-bulk?cout-tailcerr-tail");
	// Cout original receives only cout text.
	QCOMPARE(streams.out.str(), "cout-bulk!cout-tail");
	// Cerr original receives only cerr text.
	QCOMPARE(streams.err.str(), "cerr-bulk?cerr-tail");
}

void TestConsoleStream::survivesReentrantOriginal() {
	ReentrantBuf reentrant;
	std::stringbuf reentrantCerr;
	std::string reentrantCallback;
	const StreamCapture streams;
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
	// Reentrant original buffer does not deadlock or lose callback text.
	QCOMPARE(reentrantCallback, "mirrorx");
	// Reentrant original receives cout text.
	QCOMPARE(reentrant.str(), "x");
	// Reentrant original may write through cerr.
	QCOMPARE(reentrantCerr.str(), "mirror");
}

void TestConsoleStream::keepsTextThatOriginalRejects() {
	RejectingBuf rejecting;
	std::stringbuf rejectingCerr;
	std::string rejectingCallback;
	const StreamCapture streams;
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
	// Callback keeps text rejected by original destination.
	QCOMPARE(rejectingCallback, "lost");
}

void TestConsoleStream::deliversOnlyToInstalledInstance() {
	const std::string text = "text\n";
	QVERIFY2(!ConsoleStreambuf::deliver(Category::AppUi, Level::Warning, text), "deliver without an installed instance returns false");
	StreamCapture streams;
	ConsoleStreambuf capture([](QString) {});
	capture.install();
	QVERIFY2(ConsoleStreambuf::deliver(Category::AppUi, Level::Warning, text), "deliver to an installed instance returns true");
	capture.restore();
	QVERIFY2(!ConsoleStreambuf::deliver(Category::AppUi, Level::Warning, text), "deliver after restore returns false");
}

void TestConsoleStream::deliversToMessageCallback() {
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

	// A message reaches the original cout once, as given.
	QCOMPARE(streams.out.str(), "abctext\n");
	QVERIFY2(streams.err.str().empty(), "a message does not reach the original cerr");
	QVERIFY2((messages == std::vector<Message>{{Category::AppUi, Level::Warning, "text\n"}}),
		"the message callback receives category, level and text once");
	QVERIFY2((events == std::vector<std::string>{"plain abc", "message text\n"}),
		"the pending partial line reaches the plain callback before the message, and the message does not");
}

void TestConsoleStream::deliversWithoutMessageCallback() {
	StreamCapture streams;
	std::string plain;
	ConsoleStreambuf capture([&plain](QString text) { plain += text.toStdString(); });
	capture.install();
	ConsoleStreambuf::deliver(Category::SimEvents, Level::Info, "text\n");
	capture.restore();
	// Without a message callback the original cout receives the text once.
	QCOMPARE(streams.out.str(), "text\n");
	// Without a message callback the plain callback receives the text.
	QCOMPARE(plain, "text\n");
}

void TestConsoleStream::prefixesMessageText() {
	// Level and category come first.
	QCOMPARE(consoleMessageText(Category::AppUi, Level::Warning, "x\n"), "warning app.ui: x\n");
	// A text without a newline gets the prefix.
	QCOMPARE(consoleMessageText(Category::SimEvents, Level::Debug, "x"), "debug sim.events: x");
	// Only the first line gets the prefix.
	QCOMPARE(consoleMessageText(Category::SimSetup, Level::Error, "a\nb\n"), "error sim.setup: a\nb\n");
	// The prefix follows leading line breaks.
	QCOMPARE(consoleMessageText(Category::SimSetup, Level::Info, "\n\nSelected\n"), "\n\ninfo sim.setup: Selected\n");
	// The prefix follows a carriage return.
	QCOMPARE(consoleMessageText(Category::SimSetup, Level::Info, "\rStep 1"), "\rinfo sim.setup: Step 1");
	// A text of line breaks is unchanged.
	QCOMPARE(consoleMessageText(Category::SimSetup, Level::Info, "\n"), "\n");
	QVERIFY2(consoleMessageText(Category::SimSetup, Level::Info, "").isEmpty(), "an empty text is unchanged");
}

void TestConsoleStream::logSinkFollowsDock() {
	{
		StreamCapture streams;
		consoleLogSink(Category::AppUi, Level::Warning, "ui\n");
		consoleLogSink(Category::SimSetup, Level::Info, "setup");
		// Without a dock app.ui goes to cerr, as given.
		QCOMPARE(streams.err.str(), "ui\n");
		// Without a dock every other category goes to cout, as given.
		QCOMPARE(streams.out.str(), "setup");
	}
	StreamCapture streams;
	std::vector<Message> messages;
	ConsoleStreambuf capture([](QString) {});
	capture.setMessageCallback([&messages](Category category, Level level, QString text) { messages.push_back({category, level, text.toStdString()}); });
	capture.install();
	consoleLogSink(Category::AppUi, Level::Warning, "ui\n");
	consoleLogSink(Category::SimSetup, Level::Info, "setup\n");
	capture.restore();
	// With a dock every category goes to the original cout, as given.
	QCOMPARE(streams.out.str(), "ui\nsetup\n");
	QVERIFY2(streams.err.str().empty(), "with a dock nothing goes to the original cerr");
	QVERIFY2((messages == std::vector<Message>{{Category::AppUi, Level::Warning, "ui\n"}, {Category::SimSetup, Level::Info, "setup\n"}}),
		"with a dock the messages reach the dock with their category and level");
}

void TestConsoleStream::qtMessagesReachCore() {
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
	QVERIFY2(messages == expected, "each Qt message reaches the core as app.ui with its level and the text of Qt's own handler");

	messages.clear();
	eglog::configure("app.ui.warning=false");
	qWarning("hidden");
	qCritical("shown");
	QVERIFY2((messages == std::vector<Message>{{Category::AppUi, Level::Error, "shown\n"}}), "EGTRAIN_LOG rules switch one level of app.ui off");

	// The handler is called directly, because Qt ends the program after a fatal message.
	messages.clear();
	eglog::configure("app.ui=false");
	qtMessageToLog(QtFatalMsg, QMessageLogContext(), QStringLiteral("fatal text"));
	QVERIFY2((messages == std::vector<Message>{{Category::AppUi, Level::Error, "fatal text\n"}}), "a fatal Qt message is never filtered");
}

void TestConsoleStream::installAppliesRules() {
	LoggingReset reset;
	StreamCapture streams;

	qputenv("EGTRAIN_LOG", "app.ui.warning=false");
	QVERIFY2(installConsoleLogging().empty(), "good rules give no problem");
	qWarning("hidden");
	qCritical("shown");
	// The rules of EGTRAIN_LOG filter the Qt messages, and the rest go to cerr.
	QCOMPARE(streams.err.str(), "shown\n");
	QVERIFY2(streams.out.str().empty(), "a Qt message does not reach cout without a dock");

	qputenv("EGTRAIN_LOG", "oops;app.ui.warning=false");
	const std::string problems = installConsoleLogging();
	QVERIFY2(problems.find("EGTRAIN_LOG has a malformed rule") != std::string::npos, "a malformed rule is reported");
	qWarning("hidden again");
	// The other rules still apply after a malformed rule.
	QCOMPARE(streams.err.str(), "shown\n");

	qunsetenv("EGTRAIN_LOG");
	QVERIFY2(installConsoleLogging().empty(), "an unset EGTRAIN_LOG gives no problem");
	qWarning("visible");
	// The defaults apply when EGTRAIN_LOG is unset.
	QCOMPARE(streams.err.str(), "shown\nvisible\n");
}

void TestConsoleStream::writesLogFile() {
	LoggingReset reset;
	StreamCapture streams;
	QTemporaryDir directory;
	const QString path = directory.filePath(QStringLiteral("log.txt"));

	qputenv("EGTRAIN_LOG_FILE", path.toLocal8Bit());
	QVERIFY2(installConsoleLogging().empty(), "a log file that can be opened gives no problem");
	qWarning("to file");
	EG_LOG(SimSetup, Info) << "\nSelected\n";
	eglog::configure("sim.setup=false");
	EG_LOG(SimSetup, Info) << "switched off\n";
	eglog::configure("");
	// The log file holds the enabled messages with level and category.
	QCOMPARE(readFile(path), "warning app.ui: to file\n\ninfo sim.setup: Selected\n");
	// The console streams hold the text as given.
	QCOMPARE(streams.err.str(), "to file\n");
	QCOMPARE(streams.out.str(), "\nSelected\n");

	installConsoleLogging();
	EG_LOG(SimSetup, Info) << "\rStep 1";
	EG_LOG(SimSetup, Info) << "\rStep 2\n";
	qCritical("again");
	// A later call appends to the log file, and each message starts on a line of its own.
	QCOMPARE(readFile(path), "warning app.ui: to file\n\ninfo sim.setup: Selected\n\rinfo sim.setup: Step 1\n\rinfo sim.setup: Step 2\nerror app.ui: again\n");
	const std::string written = readFile(path);

	qunsetenv("EGTRAIN_LOG_FILE");
	installConsoleLogging();
	qCritical("not in the file");
	// Without the variable the log file is closed.
	QCOMPARE(readFile(path), written);

	qputenv("EGTRAIN_LOG_FILE", directory.filePath(QStringLiteral("missing/log.txt")).toLocal8Bit());
	QVERIFY2(installConsoleLogging().find("EGTRAIN_LOG_FILE could not be opened") != std::string::npos, "a log file that cannot be opened is reported");
}

// A Qt message raised while the sink runs must not enter the core again, whose mutex is held.
void TestConsoleStream::qtMessageInsideSinkSkipsCore() {
	LoggingReset reset;
	StreamCapture streams;
	ConsoleStreambuf capture([](QString) {});
	capture.setMessageCallback([](Category, Level, QString) { qWarning("nested"); });
	capture.install();
	installConsoleLogging();
	EG_LOG(SimEvents, Info) << "outer\n";
	capture.restore();
	// The outer message reaches the original cout.
	QCOMPARE(streams.out.str(), "outer\n");
	// The nested message goes to cerr and does not wait for the core.
	QCOMPARE(streams.err.str(), "nested\n");
}

void TestConsoleStream::dockShowsMessages() {
	LoggingReset reset;
	StreamCapture streams;
	{
		ConsoleWidget dock;
		QPlainTextEdit* edit = dock.findChild<QPlainTextEdit*>();
		if (edit == nullptr)
			QFAIL("the dock holds a text edit");
		installConsoleLogging();

		qWarning("first");
		// The dock shows level and category of a Qt message.
		QCOMPARE(edit->toPlainText(), "warning app.ui: first\n");
		// The original cout holds the text once, as given.
		QCOMPARE(streams.out.str(), "first\n");
		QVERIFY(streams.err.str().empty());

		std::cout << "partial";
		qCritical("second");
		// A message starts on a line of its own.
		QCOMPARE(edit->toPlainText(), "warning app.ui: first\npartial\nerror app.ui: second\n");

		EG_LOG(SimSetup, Info) << "\nSelected\n";
		std::cout << "plain\n";
		// A message of any category is prefixed, plain text is not.
		QCOMPARE(edit->toPlainText(), "warning app.ui: first\npartial\nerror app.ui: second\n\ninfo sim.setup: Selected\nplain\n");

		std::thread([] { EG_LOG(SimEvents, Warning) << "from a thread\n"; }).join();
		QCoreApplication::processEvents();
		QVERIFY2(edit->toPlainText().endsWith("plain\nwarning sim.events: from a thread\n"), "a message from another thread reaches the dock");
	}
	QVERIFY2(!ConsoleStreambuf::deliver(Category::AppUi, Level::Warning, "gone\n"), "the destroyed dock is no longer delivered to");
}

// The messages of a run: progress lines that start with a carriage return, and the end line that starts with a line break.
// The dock shows each on a line of its own, with level and category in front, and no empty line before a progress line.
void TestConsoleStream::dockPutsRunMessagesOnLines() {
	LoggingReset reset;
	StreamCapture streams;
	ConsoleWidget dock;
	QPlainTextEdit* edit = dock.findChild<QPlainTextEdit*>();
	if (edit == nullptr)
		QFAIL("the dock holds a text edit");
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
	// Each progress line and the end line are on a line of their own, with no empty line before a progress line.
	QCOMPARE(edit->toPlainText(), expected);
}

int main(int argc, char** argv) {
	// Qt reads these once, at its first message.
	qunsetenv("QT_MESSAGE_PATTERN");
	qunsetenv("QT_LOGGING_RULES");
	qunsetenv("EGTRAIN_LOG");
	qunsetenv("EGTRAIN_LOG_FILE");
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication application(argc, argv);
	TestConsoleStream test;
	return QTest::qExec(&test, argc, argv);
}

#include "test_consolestream.moc"
