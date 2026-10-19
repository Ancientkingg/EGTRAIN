#include "app/CrashNote.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

#include <csignal>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

bool expect(bool condition, const std::string& message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

// Installs the crash handler with a note in folder and ends the process in the way that kind names.
int crashChild(const QString& kind, const QString& folder) {
	QCoreApplication::setApplicationVersion(QStringLiteral("9.8.7-test"));
	installCrashHandler(folder);
	if (kind == QLatin1String("segv")) {
#ifdef Q_OS_WIN
		volatile int* volatile nowhere = nullptr;
		*nowhere = 1;
#else
		std::raise(SIGSEGV);
#endif
	} else if (kind == QLatin1String("throw")) {
		throw std::runtime_error("injected crash");
	} else if (kind == QLatin1String("throw-thread")) {
		std::thread([] { throw std::runtime_error("injected crash"); }).join();
	}
	return 0;
}

// A child of the kind leaves one note with the version, the cause and, on macOS and Linux, a stack, and the next take
// reports it once.
bool checkCrash(const char* kind, const char* cause, bool exception) {
	const std::string name = kind;
	QTemporaryDir folder;
	QProcess child;
	child.setProcessChannelMode(QProcess::ForwardedChannels);
	child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--child"), QString::fromLatin1(kind), folder.path()});
	bool ok = expect(child.waitForFinished(120000), name + ": the child did not end");
#ifdef Q_OS_WIN
	ok &= expect(child.exitCode() != 0, name + ": the child ended normally");
#else
	ok &= expect(child.exitStatus() == QProcess::CrashExit, name + ": the child did not end by its signal");
#endif
	const QDir dir(folder.path());
	const QStringList notes = dir.entryList({QStringLiteral("crash-*.txt")}, QDir::Files);
	if (!expect(notes.size() == 1, name + ": " + std::to_string(notes.size()) + " notes, not one"))
		return false;
	QFile file(dir.filePath(notes.first()));
	ok &= expect(file.open(QIODevice::ReadOnly), name + ": the note cannot be read");
	const QByteArray text = file.readAll();
	file.close();
	ok &= expect(text.startsWith("EGTRAIN 9.8.7-test (Qt "), name + ": no version header in\n" + text.toStdString());
	ok &= expect(text.contains(cause), name + ": no " + cause + " in\n" + text.toStdString());
	if (exception)
		ok &= expect(text.contains("Uncaught exception: injected crash\n"), name + ": no exception in\n" + text.toStdString());
#ifndef Q_OS_WIN
	const int stack = text.indexOf("Stack:\n");
	ok &= expect(stack >= 0 && text.mid(stack + 7).trimmed().size() > 0, name + ": no stack in\n" + text.toStdString());
#endif

	const QFileInfo taken(takeCrashNote(folder.path()));
	ok &= expect(taken.fileName() == QStringLiteral("reported-") + notes.first() && taken.isFile(), name + ": the note was not taken");
	ok &= expect(takeCrashNote(folder.path()).isEmpty(), name + ": the note was taken twice");
	return ok;
}

bool checkReportedNotesKept() {
	QTemporaryDir folder;
	const QDir dir(folder.path());
	for (int second = 0; second < 12; ++second) {
		QFile note(dir.filePath(QStringLiteral("reported-crash-20261019-1000%1-1.txt").arg(second, 2, 10, QLatin1Char('0'))));
		if (!expect(note.open(QIODevice::WriteOnly), "a reported note cannot be made"))
			return false;
	}
	bool ok = expect(takeCrashNote(folder.path()).isEmpty(), "a reported note was taken again");
	const QStringList kept = dir.entryList({QStringLiteral("reported-crash-*.txt")}, QDir::Files, QDir::Name);
	ok &= expect(kept.size() == 10 && kept.first() == QStringLiteral("reported-crash-20261019-100002-1.txt"),
		"the newest 10 reported notes are not the ones kept");
	return ok;
}

} // namespace

int main(int argc, char** argv) {
	QCoreApplication application(argc, argv);
	const QStringList arguments = QCoreApplication::arguments();
	if (arguments.size() == 4 && arguments.at(1) == QLatin1String("--child"))
		return crashChild(arguments.at(2), arguments.at(3));

	bool ok = true;
#ifdef Q_OS_WIN
	ok &= checkCrash("segv", "Exception: 0xC0000005", false);
#else
	ok &= checkCrash("segv", "Signal: SIGSEGV", false);
	ok &= checkCrash("throw-thread", "Signal: SIGABRT", true);
#endif
	ok &= checkCrash("throw", "Signal: SIGABRT", true);
	ok &= checkReportedNotesKept();
	std::cout << (ok ? "test_crashnote: PASS\n" : "test_crashnote: FAIL\n");
	return ok ? 0 : 1;
}
