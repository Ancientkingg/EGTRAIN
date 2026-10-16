#include "widgets/ConsoleWidget.h"
#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QThread>

// ---- ConsoleStreambuf ----

ConsoleStreambuf* ConsoleStreambuf::s_active = nullptr;
QMutex ConsoleStreambuf::s_activeMutex;

ConsoleStreambuf::ConsoleStreambuf(std::function<void(QString)> callback)
	: m_callback(std::move(callback)),
	  m_coutProxy(this, Stream::Cout),
	  m_cerrProxy(this, Stream::Cerr) {
}

ConsoleStreambuf::~ConsoleStreambuf() {
	restore();
}

void ConsoleStreambuf::setMessageCallback(MessageCallback callback) {
	m_messageCallback = std::move(callback);
}

void ConsoleStreambuf::install() {
	if (m_installed) return;
	m_oldCout = std::cout.rdbuf(&m_coutProxy);
	m_oldCerr = std::cerr.rdbuf(&m_cerrProxy);
	m_installed = true;
	QMutexLocker lock(&s_activeMutex);
	s_active = this;
}

void ConsoleStreambuf::restore() {
	if (!m_installed) return;
	{
		QMutexLocker lock(&s_activeMutex);
		if (s_active == this) s_active = nullptr;
	}
	if (m_oldCout) std::cout.rdbuf(m_oldCout);
	if (m_oldCerr) std::cerr.rdbuf(m_oldCerr);
	m_installed = false;
	flushBuffer();
}

ConsoleStreambuf::ProxyStreambuf::ProxyStreambuf(ConsoleStreambuf* owner, Stream stream)
	: m_owner(owner), m_stream(stream) {
}

ConsoleStreambuf::int_type ConsoleStreambuf::ProxyStreambuf::overflow(int_type c) {
	return m_owner->overflow(m_stream, c);
}

std::streamsize ConsoleStreambuf::ProxyStreambuf::xsputn(const char* s, std::streamsize n) {
	return m_owner->xsputn(m_stream, s, n);
}

int ConsoleStreambuf::ProxyStreambuf::sync() {
	return m_owner->sync(m_stream);
}

ConsoleStreambuf::int_type ConsoleStreambuf::overflow(int_type c) {
	return overflow(Stream::Cout, c);
}

ConsoleStreambuf::int_type ConsoleStreambuf::overflow(Stream stream, int_type c) {
	std::streambuf* original = stream == Stream::Cout ? m_oldCout : m_oldCerr;
	bool forwarded = true;

	QMutexLocker lock(&m_mutex);
	if (c != EOF) {
		if (original && traits_type::eq_int_type(original->sputc(c), traits_type::eof()))
			forwarded = false;
		m_buffer += static_cast<char>(c);
		if (c == '\n') {
			lock.unlock();
			flushBuffer();
		}
	}
	return forwarded ? c : traits_type::eof();
}

std::streamsize ConsoleStreambuf::xsputn(const char* s, std::streamsize n) {
	return xsputn(Stream::Cout, s, n);
}

std::streamsize ConsoleStreambuf::xsputn(Stream stream, const char* s, std::streamsize n) {
	if (n <= 0) return n;

	std::streambuf* original = stream == Stream::Cout ? m_oldCout : m_oldCerr;
	std::streamsize forwarded = n;
	std::string to_flush;
	QMutexLocker lock(&m_mutex);
	if (original)
		forwarded = original->sputn(s, n);
	m_buffer.append(s, static_cast<std::size_t>(n));
	auto pos = m_buffer.rfind('\n');
	if (pos != std::string::npos) {
		to_flush = m_buffer.substr(0, pos + 1);
		m_buffer = m_buffer.substr(pos + 1);
	}
	lock.unlock();
	if (!to_flush.empty() && m_callback)
		m_callback(QString::fromStdString(to_flush));
	return forwarded;
}

int ConsoleStreambuf::sync() {
	return sync(Stream::Cout);
}

int ConsoleStreambuf::sync(Stream stream) {
	std::streambuf* original = stream == Stream::Cout ? m_oldCout : m_oldCerr;
	int result = 0;
	if (original) {
		QMutexLocker lock(&m_mutex);
		result = original->pubsync();
	}
	flushBuffer();
	return result;
}

void ConsoleStreambuf::flushBuffer() {
	QMutexLocker lock(&m_mutex);
	if (m_buffer.empty() || !m_callback) return;
	QString text = QString::fromStdString(m_buffer);
	m_buffer.clear();
	lock.unlock();
	m_callback(text);
}

bool ConsoleStreambuf::deliver(eglog::Category category, eglog::Level level, const std::string& text) {
	// Held for the whole delivery, so that restore() does not return while one is running.
	QMutexLocker lock(&s_activeMutex);
	if (!s_active) return false;
	s_active->writeMessage(category, level, text);
	return true;
}

void ConsoleStreambuf::writeMessage(eglog::Category category, eglog::Level level, const std::string& text) {
	const std::streamsize size = static_cast<std::streamsize>(text.size());
	if (!m_messageCallback) {
		xsputn(Stream::Cout, text.data(), size);
		return;
	}
	{
		QMutexLocker lock(&m_mutex);
		if (m_oldCout)
			m_oldCout->sputn(text.data(), size);
	}
	// Text of an earlier partial line comes before the message in the dock.
	flushBuffer();
	m_messageCallback(category, level, QString::fromStdString(text));
}

// ---- ConsoleWidget ----

ConsoleWidget::ConsoleWidget(QWidget* parent)
	: QDockWidget("Console Log", parent) {
	setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea | Qt::RightDockWidgetArea);
	setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
	setMinimumHeight(100);
	setObjectName("ConsoleLogDock"); // QDockWidget needs objectName for state saving

	m_textEdit = new QPlainTextEdit(this);
	m_textEdit->setReadOnly(true);
	m_textEdit->setMaximumBlockCount(10000);
	m_textEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
	m_textEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	setWidget(m_textEdit);

	// Both callbacks run on the GUI thread: directly when the caller is that thread, queued otherwise.
	const auto onGuiThread = [this](std::function<void()> call) {
		if (QThread::currentThread() == qApp->thread()) {
			call();
		} else {
			QMetaObject::invokeMethod(this, std::move(call), Qt::QueuedConnection);
		}
	};
	m_streambuf = std::make_unique<ConsoleStreambuf>([this, onGuiThread](const QString& text) {
		onGuiThread([this, text]() { appendText(text); });
	});
	m_streambuf->setMessageCallback([this, onGuiThread](eglog::Category category, eglog::Level level, const QString& text) {
		onGuiThread([this, category, level, text]() { appendMessage(category, level, text); });
	});
	m_streambuf->install();
}

ConsoleWidget::~ConsoleWidget() {
	m_streambuf->restore();
}

void ConsoleWidget::appendText(const QString& text) {
	m_textEdit->moveCursor(QTextCursor::End);
	m_textEdit->insertPlainText(text);
	m_textEdit->verticalScrollBar()->setValue(m_textEdit->verticalScrollBar()->maximum());
}

void ConsoleWidget::appendMessage(eglog::Category category, eglog::Level level, const QString& text) {
	m_textEdit->moveCursor(QTextCursor::End);
	// A message starts on a line of its own, unless its text starts with the line break itself.
	if (!m_textEdit->textCursor().atBlockStart() && !text.startsWith(QLatin1Char('\n')))
		m_textEdit->insertPlainText(QStringLiteral("\n"));
	m_textEdit->insertPlainText(consoleMessageText(category, level, text));
	m_textEdit->verticalScrollBar()->setValue(m_textEdit->verticalScrollBar()->maximum());
}

// ---- Application logging ----

QString consoleMessageText(eglog::Category category, eglog::Level level, const QString& text) {
	int start = 0;
	while (start < text.size() && (text[start] == QLatin1Char('\n') || text[start] == QLatin1Char('\r')))
		++start;
	if (start == text.size()) return text;
	const QString prefix = QStringLiteral("%1 %2: ").arg(QString::fromLatin1(eglog::levelName(level)), QString::fromLatin1(eglog::categoryName(category)));
	return text.left(start) + prefix + text.mid(start);
}

namespace {

// The file of EGTRAIN_LOG_FILE and whether its last byte ends a line. The sink writes it with the
// mutex of the logging core held.
QFile& logFile() {
	static QFile file;
	return file;
}
bool logFileAtLineStart = true;

eglog::Level qtMessageLevel(QtMsgType type) {
	switch (type) {
		case QtDebugMsg:
			return eglog::Level::Debug;
		case QtInfoMsg:
			return eglog::Level::Info;
		case QtWarningMsg:
			return eglog::Level::Warning;
		case QtCriticalMsg:
		case QtFatalMsg:
			return eglog::Level::Error;
	}
	return eglog::Level::Error;
}

// True while this thread runs consoleLogSink. A Qt message raised there must not enter the
// logging core again, whose mutex this thread holds.
thread_local bool insideSink = false;

} // namespace

void consoleLogSink(eglog::Category category, eglog::Level level, const std::string& text) {
	insideSink = true;
	if (logFile().isOpen()) {
		// Like the dock, the file starts a message on a line of its own, unless its text starts with the line break itself.
		QByteArray line = consoleMessageText(category, level, QString::fromStdString(text)).toUtf8();
		if (!logFileAtLineStart && !line.startsWith('\n'))
			line.prepend('\n');
		logFile().write(line);
		logFile().flush();
		logFileAtLineStart = line.endsWith('\n');
	}
	if (!ConsoleStreambuf::deliver(category, level, text)) {
		// Qt's own handler writes the messages of Qt to std::cerr, the default sink of the logging
		// core writes everything else to std::cout.
		if (category == eglog::Category::AppUi)
			std::cerr << text;
		else
			std::cout << text;
	}
	insideSink = false;
}

void qtMessageToLog(QtMsgType type, const QMessageLogContext& context, const QString& message) {
	const eglog::Level level = qtMessageLevel(type);
	const bool fatal = type == QtFatalMsg;
	if (!fatal && !eglog::enabled(eglog::Category::AppUi, level)) return;

	// The text Qt's own handler prints: the message, with "category: " in front when it has one.
	const QString formatted = qFormatLogMessage(type, context, message);
	if (!formatted.isNull()) {
		const std::string text = formatted.toStdString() + '\n';
		if (insideSink)
			std::cerr << text;
		else
			eglog::Line(eglog::Category::AppUi, level).stream() << text;
	}
	// Qt ends the program when this function returns, and the text of a fatal message must be out by then.
	if (fatal) {
		std::cout.flush();
		std::cerr.flush();
	}
}

std::string installConsoleLogging() {
	std::string problems;
	logFile().close();
	const QByteArray path = qgetenv("EGTRAIN_LOG_FILE");
	if (!path.isEmpty()) {
		logFile().setFileName(QString::fromLocal8Bit(path));
		logFileAtLineStart = true;
		if (!logFile().open(QIODevice::WriteOnly | QIODevice::Append))
			problems += "WARNING: EGTRAIN_LOG_FILE could not be opened (" + logFile().errorString().toStdString() + "). No log file is written.\n";
	}
	eglog::setSink(consoleLogSink);
	qInstallMessageHandler(qtMessageToLog);
	if (!eglog::configureFromEnvironment())
		problems += "WARNING: EGTRAIN_LOG has a malformed rule. It was skipped and the other rules apply. A rule is <pattern>=true or <pattern>=false, and rules are separated by ';'.\n";
	return problems;
}
