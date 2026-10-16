#include "widgets/CaseChooserDialog.h"
#include "widgets/DialogLayout.h"

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

void addGroup(QListWidget* list, const QString& title) {
	auto* item = new QListWidgetItem(title, list);
	item->setFlags(Qt::NoItemFlags);
	item->setData(Qt::UserRole, QString());
}

// Adds a row for the scene unless the same file is already listed.
bool addSceneItem(QListWidget* list, QSet<QString>& seen, const QString& path) {
	const QString canonical = QFileInfo(path).canonicalFilePath();
	if (canonical.isEmpty() || seen.contains(canonical))
		return false;
	seen.insert(canonical);
	auto* item = new QListWidgetItem(displayName(path), list);
	item->setData(Qt::UserRole, path);
	item->setToolTip(path.toHtmlEscaped());
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
			auto* missing = new QListWidgetItem(QString("Unavailable: %1").arg(displayName(path)), list);
			missing->setFlags(Qt::NoItemFlags);
			missing->setToolTip(path.toHtmlEscaped());
		}
	}
	if (!recentHeader) {
		addGroup(list, "Recent cases");
		addGroup(list, "No other recent cases to show");
	}
}

} // namespace

CaseChooserDialog::CaseChooserDialog(const Options& options, QWidget* parent) : QDialog(parent) {
	setWindowTitle("Open a Case");
	setObjectName("caseChooserDialog");
	auto* body = new QWidget;
	auto* bodyLayout = new QVBoxLayout(body);
	m_list = new QListWidget(body);
	m_list->setObjectName("caseChooserList");
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
		body, buttons);
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
