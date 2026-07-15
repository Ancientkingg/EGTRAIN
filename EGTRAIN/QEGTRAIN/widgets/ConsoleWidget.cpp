#include "widgets/ConsoleWidget.h"
#include <QApplication>
#include <QFontDatabase>
#include <QThread>

// ---- ConsoleStreambuf ----

ConsoleStreambuf::ConsoleStreambuf(std::function<void(QString)> callback)
    : m_callback(std::move(callback)),
      m_coutProxy(this, Stream::Cout),
      m_cerrProxy(this, Stream::Cerr)
{
}

ConsoleStreambuf::~ConsoleStreambuf() {
    restore();
}

void ConsoleStreambuf::install() {
    if (m_installed) return;
    m_oldCout = std::cout.rdbuf(&m_coutProxy);
    m_oldCerr = std::cerr.rdbuf(&m_cerrProxy);
    m_installed = true;
}

void ConsoleStreambuf::restore() {
    if (!m_installed) return;
    if (m_oldCout) std::cout.rdbuf(m_oldCout);
    if (m_oldCerr) std::cerr.rdbuf(m_oldCerr);
    m_installed = false;
    flushBuffer();
}

ConsoleStreambuf::ProxyStreambuf::ProxyStreambuf(ConsoleStreambuf* owner, Stream stream)
    : m_owner(owner), m_stream(stream)
{
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

// ---- ConsoleWidget ----

ConsoleWidget::ConsoleWidget(QWidget* parent)
    : QDockWidget("Console Log", parent)
{
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

    m_streambuf = std::make_unique<ConsoleStreambuf>([this](const QString& text) {
        if (QThread::currentThread() == qApp->thread()) {
            appendText(text);
        } else {
            QMetaObject::invokeMethod(this, [this, text]() {
                appendText(text);
            }, Qt::QueuedConnection);
        }
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
