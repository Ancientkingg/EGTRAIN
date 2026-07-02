#ifndef CONSOLEWIDGET_H
#define CONSOLEWIDGET_H

#include <QDockWidget>
#include <QPlainTextEdit>
#include <QPointer>
#include <QMutex>
#include <QString>
#include <QTextCursor>
#include <QScrollBar>
#include <streambuf>
#include <iostream>
#include <memory>

// Thread-safe streambuf that captures stdout/stderr and forwards to a callback
class ConsoleStreambuf : public std::streambuf {
public:
    explicit ConsoleStreambuf(std::function<void(QString)> callback);
    ~ConsoleStreambuf();

    void install();
    void restore();

protected:
    int_type overflow(int_type c) override;
    std::streamsize xsputn(const char* s, std::streamsize n) override;
    int sync() override;

private:
    std::function<void(QString)> m_callback;
    std::streambuf* m_oldCout = nullptr;
    std::streambuf* m_oldCerr = nullptr;
    std::string m_buffer;
    QMutex m_mutex;
    bool m_installed = false;

    void flushBuffer();
};

// Dock widget that displays captured console output
class ConsoleWidget : public QDockWidget {
    Q_OBJECT
public:
    explicit ConsoleWidget(QWidget* parent = nullptr);
    ~ConsoleWidget();

private:
    QPointer<QPlainTextEdit> m_textEdit;
    std::unique_ptr<ConsoleStreambuf> m_streambuf;

    void appendText(const QString& text);
};

#endif // CONSOLEWIDGET_H
