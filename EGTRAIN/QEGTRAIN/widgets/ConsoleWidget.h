#ifndef CONSOLEWIDGET_H
#define CONSOLEWIDGET_H

#include "util/Log.h"
#include <QDockWidget>
#include <QPlainTextEdit>
#include <QPointer>
#include <QMutex>
#include <QString>
#include <QTextCursor>
#include <QScrollBar>
#include <QtGlobal>
#include <functional>
#include <streambuf>
#include <iostream>
#include <memory>
#include <string>

// Thread-safe streambuf that captures stdout/stderr and forwards to a callback
class ConsoleStreambuf : public std::streambuf {
public:
	// Receives a message of the logging core with its category and level, in the order of the
	// text that reaches the plain callback.
	using MessageCallback = std::function<void(eglog::Category, eglog::Level, QString)>;

	explicit ConsoleStreambuf(std::function<void(QString)> callback);
	~ConsoleStreambuf() override;

	// Set before install().
	void setMessageCallback(MessageCallback callback);

	void install();
	void restore();

	// Hands a message of the logging core to the installed instance and returns true, or returns
	// false when none is installed. The text reaches the original std::cout exactly as given.
	// An instance with a message callback passes the message to it and not to the plain callback;
	// one without writes the text as std::cout would.
	static bool deliver(eglog::Category category, eglog::Level level, const std::string& text);

protected:
	int_type overflow(int_type c) override;
	std::streamsize xsputn(const char* s, std::streamsize n) override;
	int sync() override;

private:
	enum class Stream { Cout,
		Cerr };

	class ProxyStreambuf : public std::streambuf {
	public:
		ProxyStreambuf(ConsoleStreambuf* owner, Stream stream);

	protected:
		int_type overflow(int_type c) override;
		std::streamsize xsputn(const char* s, std::streamsize n) override;
		int sync() override;

	private:
		ConsoleStreambuf* m_owner;
		Stream m_stream;
	};

	// The installed instance, which deliver() writes to. Observes only. It is cleared before the
	// stream buffers are put back, and deliver() holds s_activeMutex while it writes.
	static ConsoleStreambuf* s_active;
	static QMutex s_activeMutex;

	std::function<void(QString)> m_callback;
	MessageCallback m_messageCallback;
	std::streambuf* m_oldCout = nullptr;
	std::streambuf* m_oldCerr = nullptr;
	std::string m_buffer;
	QMutex m_mutex{QMutex::Recursive};
	bool m_installed = false;
	ProxyStreambuf m_coutProxy;
	ProxyStreambuf m_cerrProxy;

	int_type overflow(Stream stream, int_type c);
	std::streamsize xsputn(Stream stream, const char* s, std::streamsize n);
	int sync(Stream stream);
	void flushBuffer();
	void writeMessage(eglog::Category category, eglog::Level level, const std::string& text);
};

// Dock widget that displays captured console output
class ConsoleWidget : public QDockWidget {
	Q_OBJECT
public:
	explicit ConsoleWidget(QWidget* parent = nullptr);
	~ConsoleWidget() override;

private:
	QPointer<QPlainTextEdit> m_textEdit;
	std::unique_ptr<ConsoleStreambuf> m_streambuf;

	void appendText(const QString& text);
	void appendMessage(eglog::Category category, eglog::Level level, const QString& text);
};

// The text of a message as the dock and the log file show it: the level and the category in
// front of the first character that is not a line break ("warning app.ui: text"). The rest of
// the text is unchanged, and a text of line breaks only is returned as it is.
QString consoleMessageText(eglog::Category category, eglog::Level level, const QString& text);

// The sink of the application. It passes the message to the installed ConsoleStreambuf, which
// writes the text to the original std::cout and shows it in the dock with level and category.
// Without one (a run without a window, or a window without the dock) it writes the text as given
// to std::cerr for app.ui and to std::cout for every other category. The same text, with level
// and category in front, is appended to the log file when installConsoleLogging opened one; the
// file, like the dock, starts a message on a line of its own.
void consoleLogSink(eglog::Category category, eglog::Level level, const std::string& text);

// The Qt message handler of the application: passes the message of Qt to the logging core as
// app.ui, at the level of its type, with the text Qt's own handler prints (qFormatLogMessage).
// EGTRAIN_LOG can switch a level of app.ui off, except for qFatal, which always passes and is
// flushed. Qt ends the program after the handler returns.
void qtMessageToLog(QtMsgType type, const QMessageLogContext& context, const QString& message);

// Installs consoleLogSink and qtMessageToLog, configures the logging core from EGTRAIN_LOG and
// opens the file named by EGTRAIN_LOG_FILE, or closes the file of an earlier call when the
// variable is unset. Call it once, before other threads log. Returns the lines to print on
// std::cerr for a malformed rule in EGTRAIN_LOG or a log file that cannot be opened, and an
// empty string when both are fine.
std::string installConsoleLogging();

#endif // CONSOLEWIDGET_H
