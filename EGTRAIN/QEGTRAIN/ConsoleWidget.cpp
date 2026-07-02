#include "ConsoleWidget.h"
#include <QApplication>
#include <QThread>

// ---- ConsoleStreambuf ----

ConsoleStreambuf::ConsoleStreambuf(std::function<void(QString)> callback)
    : m_callback(std::move(callback))
{
}

ConsoleStreambuf::~ConsoleStreambuf() {
    restore();
}

void ConsoleStreambuf::install() {
    if (m_installed) return;
    m_oldCout = std::cout.rdbuf(this);
    m_oldCerr = std::cerr.rdbuf(this);
    m_installed = true;
}

void ConsoleStreambuf::restore() {
    if (!m_installed) return;
    if (m_oldCout) std::cout.rdbuf(m_oldCout);
    if (m_oldCerr) std::cerr.rdbuf(m_oldCerr);
    m_installed = false;
    flushBuffer();
}

ConsoleStreambuf::int_type ConsoleStreambuf::overflow(int_type c) {
    QMutexLocker lock(&m_mutex);
    if (c != EOF) {
        m_buffer += static_cast<char>(c);
        if (c == '\n') {
            lock.unlock();
            flushBuffer();
        }
    }
    return c;
}

std::streamsize ConsoleStreambuf::xsputn(const char* s, std::streamsize n) {
    if (n <= 0) return n;
    QMutexLocker lock(&m_mutex);
    m_buffer.append(s, static_cast<std::size_t>(n));
    auto pos = m_buffer.rfind('\n');
    if (pos != std::string::npos) {
        std::string to_flush = m_buffer.substr(0, pos + 1);
        m_buffer = m_buffer.substr(pos + 1);
        lock.unlock();
        if (m_callback) {
            m_callback(QString::fromStdString(to_flush));
        }
    }
    return n;
}

int ConsoleStreambuf::sync() {
    flushBuffer();
    return 0;
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
    m_textEdit->setFont(QFont("Menlo", 10));
    m_textEdit->setStyleSheet(
        "QPlainTextEdit { background-color: #1e1e1e; color: #d4d4d4; }"
    );
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
