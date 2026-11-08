#include "widgets/ManualWindow.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHelpContentModel>
#include <QHelpEngine>
#include <QHelpSearchEngine>
#include <QImage>
#include <QMessageBox>
#include <QProcess>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QScreen>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextObjectInterface>
#include <QThread>
#include <QTimer>

#include <cmath>
#include <functional>
#include <iostream>

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

// The sizes at which the layout shows the pictures of a page.
static QVector<QSizeF> pictureSizes(QTextDocument* page) {
	QVector<QSizeF> sizes;
	QTextObjectInterface* pictures = page->documentLayout()->handlerForObject(QTextFormat::ImageObject);
	for (QTextBlock block = page->begin(); block.isValid(); block = block.next())
		for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it)
			if (it.fragment().charFormat().isImageFormat())
				sizes.append(pictures->intrinsicSize(page, it.fragment().position(), it.fragment().charFormat()));
	return sizes;
}

// The text of each message box that shows while `act` runs, and whether its text can be selected. The boxes are closed.
struct ShownMessages {
	QStringList texts;
	bool selectable = true;
};

static ShownMessages closeMessages(const std::function<void()>& act) {
	ShownMessages shown;
	QTimer timer;
	QObject::connect(&timer, &QTimer::timeout, [&shown] {
		if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
			shown.texts << box->text();
			shown.selectable = shown.selectable && box->textInteractionFlags().testFlag(Qt::TextSelectableByMouse);
			box->accept();
		}
	});
	timer.start(0);
	act();
	return shown;
}

// Builds a help file with a start page, the page of the section case-settings and a page of pictures, the way the build
// makes EGTRAIN.qch.
static QString buildHelpFile(const QString& folder) {
	const QString project = folder + QStringLiteral("/manual.qhp");
	QDir().mkpath(folder + QStringLiteral("/_images"));
	QImage wide(1200, 750, QImage::Format_RGB32);
	wide.fill(Qt::darkBlue);
	QImage narrow(100, 50, QImage::Format_RGB32);
	narrow.fill(Qt::darkGreen);
	const bool written = writeFile(folder + QStringLiteral("/index.html"), "<html><body><h1>Start</h1></body></html>")
		&& writeFile(folder + QStringLiteral("/guides/glossary.html"),
			"<html><body><h1>Glossary</h1><h2 id=\"case-settings\">Case settings</h2><p>The base time of a timetable.</p></body></html>")
		&& wide.save(folder + QStringLiteral("/_images/wide.png")) && narrow.save(folder + QStringLiteral("/_images/narrow.png"))
		&& writeFile(folder + QStringLiteral("/guides/pictures.html"),
			"<html><body><p><img src=\"../_images/wide.png\"/></p><ul><li><p><img src=\"../_images/wide.png\"/></p></li></ul>"
			"<p><img src=\"../_images/narrow.png\"/></p><h2 id=\"end\">End</h2>"
				+ QByteArray("<p>Text of the page.</p>").repeated(400) + "<p><img src=\"../_images/wide.png\"/></p></body></html>")
		&& writeFile(project,
			"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<QtHelpProject version=\"1.0\"><namespace>org.egtrain.test</namespace>"
			"<virtualFolder>doc</virtualFolder><filterSection><toc><section title=\"Manual\" ref=\"index.html\">"
			"<section title=\"Glossary\" ref=\"guides/glossary.html\"/></section></toc><keywords/>"
			"<files><file>index.html</file><file>guides/glossary.html</file><file>guides/pictures.html</file>"
			"<file>_images/wide.png</file><file>_images/narrow.png</file></files></filterSection></QtHelpProject>\n");
	const QString helpFile = folder + QStringLiteral("/manual.qch");
	QProcess generator;
	generator.start(QStringLiteral(EGTRAIN_QHELPGENERATOR), {project, QStringLiteral("-o"), helpFile});
	if (!written || !generator.waitForFinished(60000) || generator.exitCode() != 0) {
		std::cerr << generator.readAllStandardError().constData() << generator.readAllStandardOutput().constData();
		return {};
	}
	return helpFile;
}

// The manual window reads a help file that qhelpgenerator builds as the build makes EGTRAIN.qch: its title, sections,
// contents, search and pictures.
class TestManualWindow : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void titleNamesVersionAndFitsScreen();
	void sectionOpensItsPage();
	void contentsAndSearchReadHelpFile();
	void picturesFitPage();
	void widgetTakesSectionOfParent();
	void missingHelpFileOpensNothing();
	void browserFailureShowsAddress();

private:
	QTemporaryDir m_folder;
	QString m_helpFile;
};

void TestManualWindow::initTestCase() {
	m_helpFile = m_folder.isValid() ? buildHelpFile(m_folder.path()) : QString();
	QVERIFY2(!m_helpFile.isEmpty(), "qhelpgenerator did not build the test help file");
	QCoreApplication::setApplicationVersion(QStringLiteral("1.2.3"));
}

// The title names the version, and the window opens inside the screen area.
void TestManualWindow::titleNamesVersionAndFitsScreen() {
	ManualWindow window(m_helpFile);
	// The title names the version.
	QCOMPARE(window.windowTitle(), QStringLiteral("EGTRAIN 1.2.3 Manual"));
	QVERIFY2(window.screen()->availableGeometry().contains(window.geometry()), "the window is larger than the screen area");
}

void TestManualWindow::sectionOpensItsPage() {
	ManualWindow window(m_helpFile);
	auto* browser = window.findChild<QTextBrowser*>();
	// A section opens its page at its anchor.
	QVERIFY2(window.showSection(QStringLiteral("case-settings")), "the help file did not open");
	// case-settings opens its page.
	QCOMPARE(browser->source(), QUrl(QStringLiteral("qthelp://org.egtrain.test/doc/guides/glossary.html#case-settings")));
	QVERIFY2(browser->toPlainText().contains(QStringLiteral("The base time of a timetable.")), "the page text is missing");
	// A section whose page is not in the help file opens the start page.
	window.showSection(QStringLiteral("main-window"));
	QCOMPARE(browser->source(), QUrl(QStringLiteral("qthelp://org.egtrain.test/doc/index.html")));
}

// Contents and search read the help file.
void TestManualWindow::contentsAndSearchReadHelpFile() {
	ManualWindow window(m_helpFile);
	auto* engine = window.findChild<QHelpEngine*>();
	bool indexed = false;
	int hits = -1;
	QObject::connect(engine->searchEngine(), &QHelpSearchEngine::indexingFinished, [&indexed] { indexed = true; });
	QObject::connect(engine->searchEngine(), &QHelpSearchEngine::searchingFinished, [&hits](int count) { hits = count; });
	QVERIFY2(waitFor([engine] { return engine->contentModel()->rowCount() == 1; }), "the contents are empty");
	QVERIFY2(waitFor([&indexed] { return indexed; }), "the search index was not built");
	engine->searchEngine()->search(QStringLiteral("timetable"));
	// The search finds the page.
	QVERIFY(waitFor([&hits] { return hits >= 0; }));
	QCOMPARE(hits, 1);
}

// A picture wider than its line shows at the width of the line with its aspect ratio, also at the end of a long page and
// after the window narrows, and a narrower picture keeps its size. The page still opens at its anchor below the pictures.
// The scroll bar stays, as on macOS, so the page keeps its width when it loads.
void TestManualWindow::picturesFitPage() {
	ManualWindow window(m_helpFile);
	auto* browser = window.findChild<QTextBrowser*>();
	browser->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
	window.show();
	browser->setSource(QUrl(QStringLiteral("qthelp://org.egtrain.test/doc/guides/pictures.html#end")));
	const auto fits = [browser] {
		const QVector<QSizeF> sizes = pictureSizes(browser->document());
		const qreal room = browser->viewport()->width() - 2 * browser->document()->documentMargin();
		return browser->horizontalScrollBar()->maximum() == 0 && sizes.size() == 4 && sizes[0].width() == room
			&& std::abs(sizes[0].height() - room * 750 / 1200) <= 1 && sizes[1].width() < room && sizes[2] == QSizeF(100, 50)
			&& sizes[3] == sizes[0];
	};
	QVERIFY2(waitFor(fits), "a picture does not fit the page");
	// The page opens at its anchor.
	QCOMPARE(browser->cursorForPosition(QPoint(0, 4)).block().text(), QStringLiteral("End"));
	const int wideViewport = browser->viewport()->width();
	window.resize(window.width() - 200, window.height());
	QVERIFY2(waitFor([&] { return browser->viewport()->width() < wideViewport && fits(); }), "a picture does not fit the narrower page");
}

// A widget takes the section of its nearest parent that names one.
void TestManualWindow::widgetTakesSectionOfParent() {
	QWidget dock;
	dock.setProperty("manualSection", QStringLiteral("case-settings"));
	auto* field = new QWidget(&dock);
	// A child takes the section of its parent.
	QCOMPARE(ManualWindow::sectionOf(field), QStringLiteral("case-settings"));
}

// Without a help file the window shows nothing, so the application opens the published guides instead.
void TestManualWindow::missingHelpFileOpensNothing() {
	ManualWindow missing(m_folder.path() + QStringLiteral("/missing.qch"));
	QVERIFY2(!missing.showSection(QStringLiteral("case-settings")), "a missing help file opened");
}

// A link and the published manual that the browser cannot open show their address in a message whose text can be
// selected. The test lets QDesktopServices::openUrl fail on every platform, without a browser: the handler of https that
// it registers has no method of the given name, so the call to the handler fails.
void TestManualWindow::browserFailureShowsAddress() {
	QObject receiver;
	QDesktopServices::setUrlHandler(QStringLiteral("https"), &receiver, "missingMethod");
	const auto restore = qScopeGuard([] { QDesktopServices::unsetUrlHandler(QStringLiteral("https")); });
	const QRegularExpression noMethod(QStringLiteral("No such method"));

	ManualWindow window(m_helpFile);
	auto* browser = window.findChild<QTextBrowser*>();
	QVERIFY(browser);
	const QUrl link(QStringLiteral("https://example.org/egtrain-test-link"));
	QTest::ignoreMessage(QtWarningMsg, noMethod);
	const ShownMessages linkMessages = closeMessages([browser, &link] { emit browser->anchorClicked(link); });
	// a link that does not open shows its address in a message whose text can be selected
	QCOMPARE(linkMessages.texts,
		QStringList(QStringLiteral("Could not open the link in your browser. Address: ") + link.toString()));
	QVERIFY(linkMessages.selectable);

	// Without a help file next to the program, Help > Manual opens the published guides.
	const QDir programFolder(QCoreApplication::applicationDirPath());
	if (programFolder.exists(QStringLiteral("EGTRAIN.qch")) || programFolder.exists(QStringLiteral("../Resources/EGTRAIN.qch")))
		QSKIP("a help file is next to the test program");
	QWidget parent;
	QTest::ignoreMessage(QtWarningMsg, noMethod);
	const ShownMessages manualMessages = closeMessages([&parent] { ManualWindow::open(&parent, QStringLiteral("capacity")); });
	// the published manual that does not open shows its address in a message whose text can be selected
	QCOMPARE(manualMessages.texts,
		QStringList(QStringLiteral("Could not open the manual in your browser. Address: "
								   "https://egtrain.samuelbruin.com/latest/guides/capacity-analysis.html")));
	QVERIFY(manualMessages.selectable);
}

QTEST_MAIN(TestManualWindow)
#include "test_manualwindow.moc"
