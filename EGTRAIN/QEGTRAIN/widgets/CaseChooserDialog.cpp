#include "widgets/CaseChooserDialog.h"
#include "widgets/DialogLayout.h"
#include "widgets/DisplayPath.h"
#include "widgets/NarrowRowDelegate.h"
#include "widgets/PlainToolTip.h"

#include <QAction>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSet>
#include <QVBoxLayout>

QString sceneListKey(const QString& path) {
	const QFileInfo info(path);
	QString key = info.canonicalFilePath();
	if (key.isEmpty())
		key = QDir::cleanPath(info.absoluteFilePath());
#ifdef Q_OS_WIN
	key = key.toCaseFolded();
#endif
	return key;
}

QString sceneDisplayName(const QString& rawPath) {
	const QString path = QDir::cleanPath(rawPath);
	const QFileInfo info(path);
	if (info.isDir()) {
		QFile manifest(QDir(path).filePath("scene.json"));
		if (manifest.open(QIODevice::ReadOnly)) {
			const QString name = QJsonDocument::fromJson(manifest.readAll()).object().value(QStringLiteral("name")).toString().trimmed();
			if (!name.isEmpty()) return name;
		}
	}
	const bool bundle = info.suffix().compare(QLatin1String("egscene"), Qt::CaseInsensitive) == 0;
	QString fallback = bundle ? info.completeBaseName() : info.fileName();
	// A case that is no longer on disk keeps its file name as it was, so that the person can recognise it.
	if (!info.exists())
		return fallback;
	return fallback.replace(QLatin1Char('_'), QLatin1Char(' '));
}

QStringList sceneListLabels(const QStringList& paths) {
	QStringList labels;
	QHash<QString, int> nameCount;
	for (const QString& path : paths) {
		labels << sceneDisplayName(path);
		++nameCount[labels.last()];
	}
	// A name that several paths have is followed by the parent path, shortened in the middle when it is long.
	QHash<QString, int> labelCount;
	for (qsizetype i = 0; i < paths.size(); ++i) {
		if (nameCount.value(labels.at(i)) > 1) {
			QString parent = display_path::format(QFileInfo(paths.at(i)).dir().path());
			if (parent.length() > 44)
				parent = parent.left(20) + "..." + parent.right(20);
			labels[i] = QString("%1 (%2)").arg(labels.at(i), parent);
		}
		++labelCount[labels.at(i)];
	}
	// The file name tells them apart when the parent path is the same too.
	for (qsizetype i = 0; i < paths.size(); ++i) {
		if (labelCount.value(labels.at(i)) > 1) {
			labels[i].chop(1);
			labels[i] += display_path::format("/" + QFileInfo(paths.at(i)).fileName()) + ")";
		}
	}
	return labels;
}

namespace {

// Holds the reason that a row cannot be opened; the row has no scene path.
constexpr int kReasonRole = Qt::UserRole + 1;

// A row that is only a label: it cannot be selected, reached with the arrow keys or opened. A heading is bold and
// any other label is italic. The font sets that one attribute and takes the rest from the list, so a larger system
// font still applies.
QListWidgetItem* addLabelRow(QListWidget* list, const QString& text, bool heading) {
	auto* item = new QListWidgetItem(text, list);
	item->setFlags(Qt::NoItemFlags);
	QFont font;
	if (heading)
		font.setBold(true);
	else
		font.setItalic(true);
	item->setFont(font);
	return item;
}

// The tooltip of a row: the whole label and the whole path, because the row itself may show the label elided. The path shows
// with the separators of this system.
QString rowToolTip(const QString& label, const QString& path) {
	return plainToolTip(label + QLatin1Char('\n') + display_path::format(path));
}

// Adds the heading and a row for each path, unless there is no path. A path that holds no case is listed as unavailable.
void addGroup(QListWidget* list, const QString& title, const QStringList& paths, const QStringList& labels) {
	if (paths.isEmpty())
		return;
	addLabelRow(list, title, true)->setData(Qt::UserRole, QString());
	for (qsizetype i = 0; i < paths.size(); ++i) {
		const QString& path = paths.at(i);
		const QString& label = labels.at(i);
		const QFileInfo info(path);
		if ((info.isDir() && QFileInfo(QDir(path).filePath("scene.json")).exists())
			|| (info.isFile() && info.suffix().compare("egscene", Qt::CaseInsensitive) == 0)) {
			auto* item = new QListWidgetItem(label, list);
			item->setData(Qt::UserRole, path);
			item->setToolTip(rowToolTip(label, path));
		} else {
			QListWidgetItem* item = addLabelRow(list, QString("Unavailable: %1").arg(label), false);
			// The row can be selected, so that its reason shows, but it has no path to open.
			item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
			item->setToolTip(rowToolTip(label, path));
			item->setData(kReasonRole, QStringLiteral("This case was moved or deleted."));
		}
	}
}

// Adds the path unless the same file is already listed.
void addPath(QStringList& paths, QSet<QString>& seen, const QString& path) {
	const QString key = sceneListKey(path);
	if (seen.contains(key))
		return;
	seen.insert(key);
	paths << path;
}

void fillList(QListWidget* list, const QStringList& sceneRoots, const QStringList& recentPaths) {
	QSet<QString> seen;
	QSet<QString> bundledNames;
	QStringList bundled;
	for (const QString& root : sceneRoots) {
		QDir dir(root);
		if (!dir.exists()) continue;
		const auto entries = dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
		for (const QFileInfo& entry : entries) {
			if (!((entry.isDir() && QFileInfo(QDir(entry.absoluteFilePath()).filePath("scene.json")).exists())
					|| (entry.isFile() && entry.suffix().compare("egscene", Qt::CaseInsensitive) == 0)))
				continue;
			if (bundledNames.contains(entry.fileName().toCaseFolded())) {
				seen.insert(sceneListKey(entry.absoluteFilePath()));
				continue;
			}
			bundledNames.insert(entry.fileName().toCaseFolded());
			addPath(bundled, seen, entry.absoluteFilePath());
		}
	}
	QStringList recent;
	for (const QString& path : recentPaths)
		addPath(recent, seen, path);
	// One set of labels for both groups, so that two cases of one name read differently wherever they are listed.
	const QStringList labels = sceneListLabels(bundled + recent);
	addGroup(list, "Bundled cases", bundled, labels.mid(0, bundled.size()));
	addGroup(list, "Recent cases", recent, labels.mid(bundled.size()));
	if (list->count() == 0) {
		const QString hint = QStringLiteral("No cases found. Use Other ways to open... below.");
		// The row can be elided in a narrow dialog, so the tooltip holds the whole sentence.
		addLabelRow(list, hint, false)->setToolTip(hint);
	}
}

} // namespace

CaseChooserDialog::CaseChooserDialog(const Options& options, QWidget* parent) : QDialog(parent) {
	setWindowTitle("Open a Case");
	setObjectName("caseChooserDialog");
	// Set before the footer buttons exist, because a button is styled as soon as it is built and a style that was cached
	// before the property would miss the rules that name it. DialogLayout::install sets it again.
	setProperty("dialogPresentation", true);
	auto* body = new QWidget;
	auto* bodyLayout = new QVBoxLayout(body);
	m_list = new QListWidget(body);
	m_list->setObjectName("caseChooserList");
	// A name wider than the list ends in an ellipsis in the middle, so the list never needs a horizontal scroll bar.
	m_list->setTextElideMode(Qt::ElideMiddle);
	m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_list->setItemDelegate(new NarrowRowDelegate(m_list));
	fillList(m_list, options.sceneRoots, options.recentPaths);
	bodyLayout->addWidget(m_list, 1);
	m_status = new QLabel(body);
	m_status->setObjectName("caseChooserStatus");
	DialogLayout::setStatus(m_status, DialogLayout::Severity::Warning, QString());
	bodyLayout->addWidget(m_status);

	QPushButton* moreButton = new QPushButton("Other ways to open...", body);
	moreButton->setObjectName("caseChooserOtherButton");
	// The button opens a menu, so it does not take the default while it has the focus: Return still opens the selected case
	// and Space opens the menu.
	moreButton->setAutoDefault(false);
	m_otherMenu = new QMenu(moreButton);
	QAction* newCase = m_otherMenu->addAction("New Case...");
	QAction* browseFile = m_otherMenu->addAction("Open Case File...");
	QAction* browseScene = m_otherMenu->addAction("Open Case Folder...");
	QAction* legacy = m_otherMenu->addAction("Import Legacy Case...");
	legacy->setToolTip("Convert a case from the original text files");
	moreButton->setMenu(m_otherMenu);
	bodyLayout->addWidget(moreButton, 0, Qt::AlignLeft);

	auto* buttons = new QDialogButtonBox(this);
	m_continueButton = buttons->addButton(options.currentCaseName.isEmpty() ? "Cancel" : "Continue",
		QDialogButtonBox::RejectRole);
	m_openButton = buttons->addButton("Open", QDialogButtonBox::AcceptRole);
	m_openButton->setObjectName("caseChooserOpenButton");
	m_openButton->setEnabled(false);
	m_openButton->setDefault(true);

	connect(m_list, &QListWidget::currentItemChanged, this, [this]() {
		m_openButton->setEnabled(!selectedPath().isEmpty());
		const QListWidgetItem* item = m_list->currentItem();
		DialogLayout::setStatus(m_status, DialogLayout::Severity::Warning, item ? item->data(kReasonRole).toString() : QString());
	});
	connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
		if (item && (item->flags() & Qt::ItemIsEnabled) && !item->data(Qt::UserRole).toString().isEmpty())
			choose(Outcome::OpenSelected, item->data(Qt::UserRole).toString());
	});
	connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
		if (!selectedPath().isEmpty())
			choose(Outcome::OpenSelected, selectedPath());
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(newCase, &QAction::triggered, this, [this]() { choose(Outcome::NewCase); });
	connect(browseFile, &QAction::triggered, this, [this]() { choose(Outcome::BrowseFile); });
	connect(browseScene, &QAction::triggered, this, [this]() { choose(Outcome::BrowseFolder); });
	connect(legacy, &QAction::triggered, this, [this]() { choose(Outcome::ImportLegacy); });
	DialogLayout::install(*this, "Open a Case",
		options.currentCaseName.isEmpty() ? QString() : QString("Current case: %1").arg(options.currentCaseName),
		body, buttons, options.availableGeometry);
	for (int row = 0; row < m_list->count(); ++row) {
		if ((m_list->item(row)->flags() & Qt::ItemIsEnabled)
			&& !m_list->item(row)->data(Qt::UserRole).toString().isEmpty()) {
			m_list->setCurrentRow(row);
			break;
		}
	}
}

QString CaseChooserDialog::selectedPath() const {
	const QListWidgetItem* item = m_list->currentItem();
	return item && (item->flags() & Qt::ItemIsEnabled)
		? item->data(Qt::UserRole).toString()
		: QString();
}

void CaseChooserDialog::choose(Outcome chosen, const QString& path) {
	m_outcome = chosen;
	m_chosenPath = path;
	accept();
}
