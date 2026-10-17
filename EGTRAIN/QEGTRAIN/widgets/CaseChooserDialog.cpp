#include "widgets/CaseChooserDialog.h"
#include "widgets/DialogLayout.h"
#include "widgets/PlainToolTip.h"

#include <QAction>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

namespace {

// The manifest name of a scene folder, else the file name with underscores as spaces.
QString displayName(const QString& path) {
	const QFileInfo info(path);
	if (info.isDir()) {
		QFile manifest(QDir(path).filePath("scene.json"));
		if (manifest.open(QIODevice::ReadOnly)) {
			const QString name = QJsonDocument::fromJson(manifest.readAll()).object().value(QStringLiteral("name")).toString().trimmed();
			if (!name.isEmpty()) return name;
		}
	}
	QString fallback = info.isFile() ? info.completeBaseName() : info.fileName();
	return fallback.replace(QLatin1Char('_'), QLatin1Char(' '));
}

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

// Draws the rows as the list style does and asks for no width, so that a row takes the width of the list and elides its text
// instead of widening the list.
class NarrowRowDelegate : public QStyledItemDelegate {
public:
	using QStyledItemDelegate::QStyledItemDelegate;
	QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
		return QSize(0, QStyledItemDelegate::sizeHint(option, index).height());
	}
};

// The tooltip of a row: the whole name and the whole path, because the row itself may show the name elided. Both show as typed.
QString rowToolTip(const QString& name, const QString& path) {
	return plainToolTip(name + QLatin1Char('\n') + path);
}

void addGroup(QListWidget* list, const QString& title) {
	addLabelRow(list, title, true)->setData(Qt::UserRole, QString());
}

// Adds a row for the scene unless the same file is already listed.
bool addSceneItem(QListWidget* list, QSet<QString>& seen, const QString& path) {
	const QString canonical = QFileInfo(path).canonicalFilePath();
	if (canonical.isEmpty() || seen.contains(canonical))
		return false;
	seen.insert(canonical);
	const QString name = displayName(path);
	auto* item = new QListWidgetItem(name, list);
	item->setData(Qt::UserRole, path);
	item->setToolTip(rowToolTip(name, path));
	return true;
}

void fillList(QListWidget* list, const QStringList& sceneRoots, const QStringList& recentPaths) {
	QSet<QString> seen;
	QSet<QString> bundledNames;
	bool bundledHeader = false;
	for (const QString& root : sceneRoots) {
		QDir dir(root);
		if (!dir.exists()) continue;
		const auto entries = dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
		for (const QFileInfo& entry : entries) {
			if (!((entry.isDir() && QFileInfo(QDir(entry.absoluteFilePath()).filePath("scene.json")).exists())
					|| (entry.isFile() && entry.suffix().compare("egscene", Qt::CaseInsensitive) == 0)))
				continue;
			if (bundledNames.contains(entry.fileName().toCaseFolded())) {
				seen.insert(entry.canonicalFilePath());
				continue;
			}
			bundledNames.insert(entry.fileName().toCaseFolded());
			if (!bundledHeader) {
				addGroup(list, "Bundled cases");
				bundledHeader = true;
			}
			addSceneItem(list, seen, entry.absoluteFilePath());
		}
	}
	bool recentHeader = false;
	for (const QString& path : recentPaths) {
		const QFileInfo info(path);
		const bool valid = (info.isDir() && QFileInfo(QDir(path).filePath("scene.json")).exists())
			|| (info.isFile() && info.suffix().compare("egscene", Qt::CaseInsensitive) == 0);
		if (valid && (info.canonicalFilePath().isEmpty() || seen.contains(info.canonicalFilePath())))
			continue;
		if (!recentHeader) {
			addGroup(list, "Recent cases");
			recentHeader = true;
		}
		if (valid) {
			addSceneItem(list, seen, path);
		} else {
			const QString name = displayName(path);
			addLabelRow(list, QString("Unavailable: %1").arg(name), false)->setToolTip(rowToolTip(name, path));
		}
	}
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

	QPushButton* moreButton = new QPushButton("Other ways to open...", body);
	moreButton->setObjectName("caseChooserOtherButton");
	m_otherMenu = new QMenu(moreButton);
	QAction* newCase = m_otherMenu->addAction("New Case Study...");
	QAction* browseScene = m_otherMenu->addAction("Open Scene Folder...");
	QAction* legacy = m_otherMenu->addAction("Import Legacy Case...");
	legacy->setToolTip("Convert an original text-input folder and choose where to save the scene.");
	moreButton->setMenu(m_otherMenu);
	bodyLayout->addWidget(moreButton, 0, Qt::AlignLeft);

	auto* buttons = new QDialogButtonBox(this);
	m_continueButton = buttons->addButton(options.currentCaseName.isEmpty() ? "Cancel" : "Continue",
		QDialogButtonBox::RejectRole);
	m_openButton = buttons->addButton("Open", QDialogButtonBox::AcceptRole);
	m_openButton->setObjectName("caseChooserOpenButton");
	m_openButton->setEnabled(false);
	m_openButton->setDefault(true);

	connect(m_list, &QListWidget::itemSelectionChanged, this, [this]() {
		m_openButton->setEnabled(!selectedPath().isEmpty());
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
	connect(browseScene, &QAction::triggered, this, [this]() { choose(Outcome::BrowseFolder); });
	connect(legacy, &QAction::triggered, this, [this]() {
		if (QMessageBox::question(this, "Import Legacy Case",
				"Choose the original text-input folder, then a folder for the converted scene. Continue?",
				QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
			== QMessageBox::Yes)
			choose(Outcome::ImportLegacy);
	});
	DialogLayout::install(*this, "Open a Case",
		options.currentCaseName.isEmpty() ? QStringLiteral("Choose a case to open.")
										  : QString("Current case: %1. Choose another or continue.").arg(options.currentCaseName),
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
