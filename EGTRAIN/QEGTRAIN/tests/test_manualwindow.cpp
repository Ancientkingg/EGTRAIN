#include "widgets/ManualWindow.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHelpContentModel>
#include <QHelpEngine>
#include <QHelpSearchEngine>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QThread>

#include <functional>
#include <iostream>

static bool check(bool condition, const char* message) {
	if (!condition)
		std::cerr << message << '\n';
	return condition;
}

static bool writeFile(const QString& path, const QByteArray& text) {
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(text) == text.size();
}

static bool waitFor(const std::function<bool()>& done) {
	QElapsedTimer timer;
	timer.start();
	while (!done() && timer.elapsed() < 30000) {
		QCoreApplication::processEvents();
		QThread::msleep(10);
	}
	return done();
}

// Builds a help file with a start page and the page of the section case-settings, the way the build makes EGTRAIN.qch.
static QString buildHelpFile(const QString& folder) {
	const QString project = folder + QStringLiteral("/manual.qhp");
	const bool written = writeFile(folder + QStringLiteral("/index.html"), "<html><body><h1>Start</h1></body></html>")
		&& writeFile(folder + QStringLiteral("/guides/glossary.html"),
			"<html><body><h1>Glossary</h1><h2 id=\"case-settings\">Case settings</h2><p>The base time of a timetable.</p></body></html>")
		&& writeFile(project,
			"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<QtHelpProject version=\"1.0\"><namespace>org.egtrain.test</namespace>"
			"<virtualFolder>doc</virtualFolder><filterSection><toc><section title=\"Manual\" ref=\"index.html\">"
			"<section title=\"Glossary\" ref=\"guides/glossary.html\"/></section></toc><keywords/>"
			"<files><file>index.html</file><file>guides/glossary.html</file></files></filterSection></QtHelpProject>\n");
	const QString helpFile = folder + QStringLiteral("/manual.qch");
	QProcess generator;
	generator.start(QStringLiteral(EGTRAIN_QHELPGENERATOR), {project, QStringLiteral("-o"), helpFile});
	if (!written || !generator.waitForFinished(60000) || generator.exitCode() != 0) {
		std::cerr << generator.readAllStandardError().constData() << generator.readAllStandardOutput().constData();
		return {};
	}
	return helpFile;
}

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	QTemporaryDir folder;
	const QString helpFile = folder.isValid() ? buildHelpFile(folder.path()) : QString();
	if (!check(!helpFile.isEmpty(), "qhelpgenerator did not build the test help file"))
		return 1;

	bool ok = true;
	ManualWindow window(helpFile);
	auto* engine = window.findChild<QHelpEngine*>();
	auto* browser = window.findChild<QTextBrowser*>();
	bool indexed = false;
	int hits = -1;
	QObject::connect(engine->searchEngine(), &QHelpSearchEngine::indexingFinished, [&indexed] { indexed = true; });
	QObject::connect(engine->searchEngine(), &QHelpSearchEngine::searchingFinished, [&hits](int count) { hits = count; });

	// A section opens its page at its anchor.
	ok &= check(window.showSection(QStringLiteral("case-settings")), "the help file did not open");
	ok &= check(browser->source() == QUrl(QStringLiteral("qthelp://org.egtrain.test/doc/guides/glossary.html#case-settings")),
		"case-settings did not open its page");
	ok &= check(browser->toPlainText().contains(QStringLiteral("The base time of a timetable.")), "the page text is missing");
	// A section without a page opens the start page.
	window.showSection(QStringLiteral("no-such-section"));
	ok &= check(browser->source() == QUrl(QStringLiteral("qthelp://org.egtrain.test/doc/index.html")), "an unknown section did not open the start page");
	// So does a section whose page is not in the help file.
	window.showSection(QStringLiteral("main-window"));
	ok &= check(browser->source() == QUrl(QStringLiteral("qthelp://org.egtrain.test/doc/index.html")), "a missing page did not open the start page");

	// Contents and search read the help file.
	ok &= check(waitFor([engine] { return engine->contentModel()->rowCount() == 1; }), "the contents are empty");
	ok &= check(waitFor([&indexed] { return indexed; }), "the search index was not built");
	engine->searchEngine()->search(QStringLiteral("timetable"));
	ok &= check(waitFor([&hits] { return hits >= 0; }) && hits == 1, "the search did not find the page");

	// A widget takes the section of its nearest parent that names one.
	QWidget dock;
	dock.setProperty("manualSection", QStringLiteral("case-settings"));
	auto* field = new QWidget(&dock);
	ok &= check(ManualWindow::sectionOf(field) == QStringLiteral("case-settings"), "a child did not take the section of its parent");

	// Without a help file the window shows nothing, so the application opens the published guides instead.
	ManualWindow missing(folder.path() + QStringLiteral("/missing.qch"));
	ok &= check(!missing.showSection(QStringLiteral("case-settings")), "a missing help file opened");

	if (ok)
		std::cout << "test_manualwindow passed\n";
	return ok ? 0 : 1;
}
