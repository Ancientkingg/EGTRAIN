#include "app/CrashNote.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <csignal>
#include <stdexcept>
#include <thread>

namespace {

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

} // namespace

class TestCrashNote : public QObject {
	Q_OBJECT

private slots:
	void crashLeavesOneNote_data();
	void crashLeavesOneNote();
	void reportedNotesKept();
};

void TestCrashNote::crashLeavesOneNote_data() {
	QTest::addColumn<QString>("kind");
	QTest::addColumn<QByteArray>("cause");
	QTest::addColumn<bool>("exception");
#ifdef Q_OS_WIN
	QTest::newRow("segv") << QStringLiteral("segv") << QByteArray("Exception: 0xC0000005") << false;
#else
	QTest::newRow("segv") << QStringLiteral("segv") << QByteArray("Signal: SIGSEGV") << false;
	QTest::newRow("throw-thread") << QStringLiteral("throw-thread") << QByteArray("Signal: SIGABRT") << true;
#endif
	QTest::newRow("throw") << QStringLiteral("throw") << QByteArray("Signal: SIGABRT") << true;
}

// A child of the kind leaves one note with the version, the cause and, on macOS and Linux, a stack, and the next take
// reports it once.
void TestCrashNote::crashLeavesOneNote() {
	QFETCH(QString, kind);
	QFETCH(QByteArray, cause);
	QFETCH(bool, exception);
	QTemporaryDir folder;
	QProcess child;
	child.setProcessChannelMode(QProcess::ForwardedChannels);
	child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--child"), kind, folder.path()});
	QVERIFY2(child.waitForFinished(120000), "the child did not end");
#ifdef Q_OS_WIN
	// abort() without its fast-fail report exits quietly with 3.
	// the child ended normally or quietly
	QCOMPARE_NE(child.exitCode(), 0);
	QCOMPARE_NE(child.exitCode(), 3);
#else
	QVERIFY2(child.exitStatus() == QProcess::CrashExit, "the child did not end by its signal");
#endif
	const QDir dir(folder.path());
	const QStringList notes = dir.entryList({QStringLiteral("crash-*.txt")}, QDir::Files);
	QVERIFY2(notes.size() == 1, QByteArray::number(notes.size()).append(" notes, not one").constData());
	QFile file(dir.filePath(notes.first()));
	QVERIFY2(file.open(QIODevice::ReadOnly), "the note cannot be read");
	const QByteArray text = file.readAll();
	file.close();
	QVERIFY2(text.startsWith("EGTRAIN 9.8.7-test (Qt "), ("no version header in\n" + text).constData());
	QVERIFY2(text.contains(cause), ("no " + cause + " in\n" + text).constData());
	if (exception)
		QVERIFY2(text.contains("Uncaught exception: injected crash\n"), ("no exception in\n" + text).constData());
#ifndef Q_OS_WIN
	const int stack = text.indexOf("Stack:\n");
	// no stack in the note
	QCOMPARE_GE(stack, 0);
	QCOMPARE_GT(text.mid(stack + 7).trimmed().size(), 0);
#endif

	const QFileInfo taken(takeCrashNote(folder.path()));
	// the note was not taken
	QCOMPARE(taken.fileName(), QStringLiteral("reported-") + notes.first());
	QVERIFY(taken.isFile());
	QVERIFY2(takeCrashNote(folder.path()).isEmpty(), "the note was taken twice");
}

void TestCrashNote::reportedNotesKept() {
	QTemporaryDir folder;
	const QDir dir(folder.path());
	for (int second = 0; second < 12; ++second) {
		QFile note(dir.filePath(QStringLiteral("reported-crash-20261019-1000%1-1.txt").arg(second, 2, 10, QLatin1Char('0'))));
		QVERIFY2(note.open(QIODevice::WriteOnly), "a reported note cannot be made");
	}
	QVERIFY2(takeCrashNote(folder.path()).isEmpty(), "a reported note was taken again");
	const QStringList kept = dir.entryList({QStringLiteral("reported-crash-*.txt")}, QDir::Files, QDir::Name);
	// the newest 10 reported notes are not the ones kept
	QCOMPARE(kept.size(), 10);
	QCOMPARE(kept.first(), QStringLiteral("reported-crash-20261019-100002-1.txt"));
}

int main(int argc, char** argv) {
	QCoreApplication application(argc, argv);
	// A child crashes before Qt Test runs, so the crash handler of Qt Test does not take its signal.
	const QStringList arguments = QCoreApplication::arguments();
	if (arguments.size() == 4 && arguments.at(1) == QLatin1String("--child"))
		return crashChild(arguments.at(2), arguments.at(3));
	TestCrashNote test;
	return QTest::qExec(&test, argc, argv);
}

#include "test_crashnote.moc"
