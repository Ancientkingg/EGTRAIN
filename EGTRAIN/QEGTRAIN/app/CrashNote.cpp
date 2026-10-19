#include "app/CrashNote.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSysInfo>

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fcntl.h>

#ifdef Q_OS_WIN
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <execinfo.h>
#include <unistd.h>
#endif

namespace {

constexpr int kKeptReportedNotes = 10;

// The path and the first lines of the note of this process. They are made at install time, because a handler must not
// allocate: it may run on a corrupted heap.
#ifdef Q_OS_WIN
wchar_t g_notePath[1024];
#else
char g_notePath[4096];
#endif
char g_header[2048];
volatile std::sig_atomic_t g_headerWritten = 0;
#ifdef Q_OS_WIN
// The filter of the C++ runtime, which turns an uncaught C++ exception into std::terminate.
LPTOP_LEVEL_EXCEPTION_FILTER g_previousFilter = nullptr;
#endif

// The handlers below do not allocate. backtrace is not on the POSIX list of calls that are safe in a signal handler, so a
// corrupted heap can leave a partial note; onTerminate also reads the current exception.
int openNote() {
#ifdef Q_OS_WIN
	int fd = -1;
	return _wsopen_s(&fd, g_notePath, _O_WRONLY | _O_CREAT | _O_APPEND | _O_BINARY, _SH_DENYNO, _S_IREAD | _S_IWRITE) == 0 ? fd : -1;
#else
	return open(g_notePath, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
#endif
}

void closeNote(int fd) {
#ifdef Q_OS_WIN
	_close(fd);
#else
	close(fd);
#endif
}

// A note that cannot be written is lost, and the process ends as before.
void writeText(int fd, const char* text) {
#ifdef Q_OS_WIN
	_write(fd, text, static_cast<unsigned int>(std::strlen(text)));
#else
	(void)!write(fd, text, std::strlen(text));
#endif
}

void writeHeader(int fd) {
	if (g_headerWritten)
		return;
	g_headerWritten = 1;
	writeText(fd, g_header);
}

const char* signalName(int number) {
	switch (number) {
		case SIGSEGV:
			return "SIGSEGV";
		case SIGILL:
			return "SIGILL";
		case SIGFPE:
			return "SIGFPE";
		case SIGABRT:
			return "SIGABRT";
#ifdef SIGBUS
		case SIGBUS:
			return "SIGBUS";
#endif
		default:
			return "signal";
	}
}

void onSignal(int number) {
	const int fd = openNote();
	if (fd >= 0) {
		writeHeader(fd);
		writeText(fd, "Signal: ");
		writeText(fd, signalName(number));
		writeText(fd, "\n");
#ifndef Q_OS_WIN
		writeText(fd, "Stack:\n");
		void* frames[64];
		backtrace_symbols_fd(frames, backtrace(frames, 64), fd);
#endif
		closeNote(fd);
	}
	// The handler is reset as it is called, so the signal now ends the process as it would without the handler. On Windows
	// that would exit quietly with code 3; returning lets abort() go on to the fast-fail report that the system records.
#ifndef Q_OS_WIN
	std::raise(number);
#endif
}

// Writes the exception and aborts, and the handler of SIGABRT adds the stack.
[[noreturn]] void onTerminate() {
	const int fd = openNote();
	if (fd >= 0) {
		writeHeader(fd);
		if (const std::exception_ptr current = std::current_exception()) {
			writeText(fd, "Uncaught exception: ");
			try {
				std::rethrow_exception(current);
			} catch (const std::exception& error) {
				writeText(fd, error.what());
			} catch (...) {
				writeText(fd, "unknown");
			}
			writeText(fd, "\n");
		} else {
			writeText(fd, "Terminated without an exception\n");
		}
		closeNote(fd);
	}
	std::abort();
}

#ifdef Q_OS_WIN
void writeHex(int fd, unsigned long value) {
	char digits[16];
	int position = static_cast<int>(sizeof digits) - 1;
	digits[position] = '\0';
	do {
		digits[--position] = "0123456789ABCDEF"[value % 16];
		value /= 16;
	} while (value != 0 && position > 0);
	writeText(fd, digits + position);
}

// Passes the exception on to the filter it replaced, and so to the system, after the note is written.
LONG WINAPI onException(EXCEPTION_POINTERS* info) {
	const int fd = openNote();
	if (fd >= 0) {
		writeHeader(fd);
		writeText(fd, "Exception: 0x");
		writeHex(fd, static_cast<unsigned long>(info->ExceptionRecord->ExceptionCode));
		writeText(fd, "\n");
		closeNote(fd);
	}
	return g_previousFilter ? g_previousFilter(info) : EXCEPTION_CONTINUE_SEARCH;
}
#endif

} // namespace

void installCrashHandler(const QString& folder) {
	if (folder.isEmpty() || !QDir().mkpath(folder))
		return;
	const QDateTime started = QDateTime::currentDateTimeUtc();
	const QString path = QDir(folder).absoluteFilePath(QStringLiteral("crash-%1-%2.txt")
			.arg(started.toString(QStringLiteral("yyyyMMdd-HHmmss")))
			.arg(QCoreApplication::applicationPid()));
#ifdef Q_OS_WIN
	const QString native = QDir::toNativeSeparators(path);
	if (native.size() >= static_cast<int>(sizeof g_notePath / sizeof g_notePath[0]))
		return;
	g_notePath[native.toWCharArray(g_notePath)] = L'\0';
#else
	const QByteArray native = QFile::encodeName(path);
	if (native.size() >= static_cast<int>(sizeof g_notePath))
		return;
	std::memcpy(g_notePath, native.constData(), static_cast<size_t>(native.size()) + 1);
#endif
	const QByteArray header = QStringLiteral("EGTRAIN %1 (Qt %2, %3 %4)\nStarted: %5\nExecutable: %6\n")
								  .arg(QCoreApplication::applicationVersion(), QString::fromLatin1(qVersion()),
									  QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture(),
									  started.toString(Qt::ISODate), QDir::toNativeSeparators(QCoreApplication::applicationFilePath()))
								  .toUtf8();
	qstrncpy(g_header, header.constData(), sizeof g_header);

	std::set_terminate(onTerminate);
#ifdef Q_OS_WIN
	g_previousFilter = SetUnhandledExceptionFilter(onException);
	std::signal(SIGABRT, onSignal);
#else
	// backtrace loads a library the first time; the handler must not.
	void* frame = nullptr;
	backtrace(&frame, 1);
	// A stack of its own lets the handler run after a stack overflow on the main thread.
	static char alternateStack[64 * 1024];
	stack_t stack{};
	stack.ss_sp = alternateStack;
	stack.ss_size = sizeof alternateStack;
	sigaltstack(&stack, nullptr);
	struct sigaction action{};
	action.sa_handler = onSignal;
	sigemptyset(&action.sa_mask);
	action.sa_flags = SA_ONSTACK | SA_RESETHAND | SA_NODEFER;
	for (const int number : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT})
		sigaction(number, &action, nullptr);
#endif
}

QString takeCrashNote(const QString& folder) {
	if (folder.isEmpty())
		return {};
	const QDir dir(folder);
	QString taken;
	for (const QString& name : dir.entryList({QStringLiteral("crash-*.txt")}, QDir::Files, QDir::Name)) {
		const QString reported = dir.filePath(QStringLiteral("reported-") + name);
		if (QFile::rename(dir.filePath(name), reported))
			taken = reported;
	}
	const QStringList kept = dir.entryList({QStringLiteral("reported-crash-*.txt")}, QDir::Files, QDir::Name);
	for (int index = 0; index < kept.size() - kKeptReportedNotes; ++index)
		QFile::remove(dir.filePath(kept.at(index)));
	return taken;
}
