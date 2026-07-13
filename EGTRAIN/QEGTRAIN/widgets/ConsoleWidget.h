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
	enum class Stream { Cout, Cerr };

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

	std::function<void(QString)> m_callback;
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
