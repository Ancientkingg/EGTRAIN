#include "widgets/ManualWindow.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QHelpContentWidget>
#include <QHelpEngine>
#include <QHelpSearchEngine>
#include <QHelpSearchQueryWidget>
#include <QHelpSearchResultWidget>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTabWidget>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

namespace {

// The page of each section, relative to the root of the manual. The published guides have the same paths.
QString pageOf(const QString& section) {
	static const QHash<QString, QString> pages = {
		{QStringLiteral("main-window"), QStringLiteral("guides/scenes-and-application.html#review-what-loaded")},
		{QStringLiteral("case-settings"), QStringLiteral("guides/glossary.html#case-settings")},
		{QStringLiteral("timetable-editor"), QStringLiteral("guides/scenes-and-application.html#timetable-stops")},
		{QStringLiteral("run-results"), QStringLiteral("guides/getting-started.html#read-the-results")},
		{QStringLiteral("train-diagram"), QStringLiteral("guides/how-to.html#results-and-exports")},
		{QStringLiteral("timetable-results"), QStringLiteral("guides/delay-analysis.html#where-delays-are-calculated")},
		{QStringLiteral("delay-analysis"), QStringLiteral("guides/delay-analysis.html#the-delay-analysis-window")},
		{QStringLiteral("train-paths"), QStringLiteral("guides/scenes-and-application.html#timetable-graphs-and-train-paths")},
		{QStringLiteral("blocking-time"), QStringLiteral("guides/glossary.html#blocking-time")},
		{QStringLiteral("capacity"), QStringLiteral("guides/capacity-analysis.html")},
		{QStringLiteral("capacity-by-area"), QStringLiteral("guides/capacity-analysis.html#capacity-by-area")},
	};
	return pages.value(section, QStringLiteral("index.html"));
}

// Reads the pages, style sheets and images of the manual from the help file.
class HelpBrowser : public QTextBrowser {
public:
	HelpBrowser(QHelpEngine* engine, QWidget* parent) : QTextBrowser(parent), m_engine(engine) {}

	QVariant loadResource(int type, const QUrl& name) override {
		if (name.scheme() == QLatin1String("qthelp"))
			return m_engine->fileData(name);
		return QTextBrowser::loadResource(type, name);
	}

private:
	QHelpEngine* m_engine;
};

} // namespace

ManualWindow::ManualWindow(const QString& helpFile, QWidget* parent) : QWidget(parent, Qt::Window) {
	setObjectName(QStringLiteral("manualWindow"));
	setWindowTitle(QStringLiteral("EGTRAIN Manual"));
	// The collection file holds the registration and the search index. A new one for each window needs no cleanup
	// when the application or the help file changes.
	m_engine = new QHelpEngine(m_collectionDir.filePath(QStringLiteral("manual.qhc")), this);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	// Qt 6 opens a collection file read-only unless told otherwise, and this one is written here.
	m_engine->setReadOnly(false);
#endif
	if (m_collectionDir.isValid() && QFileInfo::exists(helpFile) && m_engine->setupData()
		&& m_engine->registerDocumentation(helpFile) && m_engine->setupData())
		m_namespace = QHelpEngineCore::namespaceName(helpFile);

	m_browser = new HelpBrowser(m_engine, this);
	m_browser->setOpenLinks(false);
	connect(m_browser, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
		if (url.scheme() == QLatin1String("qthelp"))
			m_browser->setSource(url);
		else
			QDesktopServices::openUrl(url);
	});

	QHelpSearchEngine* search = m_engine->searchEngine();
	auto* searchPage = new QWidget;
	auto* searchLayout = new QVBoxLayout(searchPage);
	searchLayout->addWidget(search->queryWidget());
	searchLayout->addWidget(search->resultWidget(), 1);
	connect(search->queryWidget(), &QHelpSearchQueryWidget::search, search, [search] { search->search(search->queryWidget()->searchInput()); });
	connect(search->resultWidget(), &QHelpSearchResultWidget::requestShowLink, this, [this](const QUrl& url) { m_browser->setSource(url); });
	QHelpContentWidget* contents = m_engine->contentWidget();
	connect(contents, &QHelpContentWidget::linkActivated, this, [this](const QUrl& url) { m_browser->setSource(url); });
	connect(m_engine->contentModel(), &QHelpContentModel::contentsCreated, contents, [contents] { contents->expandToDepth(0); });

	auto* tabs = new QTabWidget;
	tabs->addTab(contents, QStringLiteral("Contents"));
	tabs->addTab(searchPage, QStringLiteral("Search"));
	auto* splitter = new QSplitter(this);
	splitter->addWidget(tabs);
	splitter->addWidget(m_browser);
	splitter->setStretchFactor(1, 1);
	auto* layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(splitter);
	resize(1100, 750);

	if (!m_namespace.isEmpty())
		search->reindexDocumentation();
}

ManualWindow::~ManualWindow() {
	// The engine closes the collection file before m_collectionDir removes it, which Windows refuses for an open file.
	delete m_engine;
}

bool ManualWindow::showSection(const QString& section) {
	if (m_namespace.isEmpty())
		return false;
	const QString root = QStringLiteral("qthelp://%1/doc/").arg(m_namespace);
	QUrl url(root + pageOf(section));
	if (m_engine->fileData(url).isEmpty())
		url = QUrl(root + QStringLiteral("index.html"));
	m_browser->setSource(url);
	return true;
}

QString ManualWindow::sectionOf(const QWidget* widget) {
	for (; widget; widget = widget->parentWidget()) {
		const QVariant section = widget->property("manualSection");
		if (section.isValid())
			return section.toString();
	}
	return {};
}

void ManualWindow::open(QWidget* parent, const QString& section) {
	auto* window = parent->findChild<ManualWindow*>(QString(), Qt::FindDirectChildrenOnly);
	if (!window) {
		// The macOS app holds the manual in its resources, the other packages next to the program.
		const QDir dir(QCoreApplication::applicationDirPath());
		const QString name = QStringLiteral("EGTRAIN.qch");
		window = new ManualWindow(dir.exists(name) ? dir.filePath(name) : QDir::cleanPath(dir.filePath(QStringLiteral("../Resources/") + name)), parent);
	}
	if (!window->showSection(section)) {
		delete window;
		QDesktopServices::openUrl(QUrl(QStringLiteral("https://ancientkingg.github.io/EGTRAIN/latest/") + pageOf(section)));
		return;
	}
	window->setWindowState(window->windowState() & ~Qt::WindowMinimized);
	window->show();
	window->raise();
	window->activateWindow();
}
