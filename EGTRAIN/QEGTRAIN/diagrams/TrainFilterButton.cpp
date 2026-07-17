#include "diagrams/TrainFilterButton.h"

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QIcon>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidgetAction>

TrainFilterButton::TrainFilterButton(QWidget* parent)
	: QToolButton(parent) {
	setPopupMode(QToolButton::InstantPopup);
	setToolButtonStyle(Qt::ToolButtonTextOnly);
	setToolTip("Choose which trains are visible");

	auto* menu = new QMenu(this);
	auto* panel = new QWidget(menu);
	auto* layout = new QVBoxLayout(panel);
	layout->setContentsMargins(8, 8, 8, 8);

	m_search = new QLineEdit(panel);
	m_search->setPlaceholderText("Search trains...");
	m_search->setClearButtonEnabled(true);
	connect(m_search, &QLineEdit::textChanged, this, &TrainFilterButton::applySearchFilter);
	layout->addWidget(m_search);

	auto* allButton = new QPushButton("All", panel);
	auto* noneButton = new QPushButton("None", panel);
	connect(allButton, &QPushButton::clicked, this, [this]() { checkAllVisible(true); });
	connect(noneButton, &QPushButton::clicked, this, [this]() { checkAllVisible(false); });
	auto* quickRow = new QHBoxLayout();
	quickRow->addWidget(allButton);
	quickRow->addWidget(noneButton);
	layout->addLayout(quickRow);

	m_list = new QListWidget(panel);
	m_list->setSelectionMode(QAbstractItemView::NoSelection);
	m_list->setMinimumSize(240, 320);
	connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem*) {
		if (m_updating)
			return;
		updateCaption();
		emit selectionChanged();
	});
	layout->addWidget(m_list, 1);

	auto* action = new QWidgetAction(menu);
	action->setDefaultWidget(panel);
	menu->addAction(action);
	setMenu(menu);
	updateCaption();
}

void TrainFilterButton::setTrains(const QVector<QPair<QString, QColor>>& trains) {
	m_updating = true;
	m_list->clear();
	for (const auto& train : trains) {
		auto* item = new QListWidgetItem(train.first, m_list);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(Qt::Checked);
		item->setData(Qt::UserRole, train.first);
		if (train.second.isValid()) {
			QPixmap swatch(12, 12);
			swatch.fill(train.second);
			item->setIcon(QIcon(swatch));
		}
	}
	m_updating = false;
	updateCaption();
}

QStringList TrainFilterButton::visibleTrainIds() const {
	QStringList ids;
	for (int i = 0; i < m_list->count(); ++i) {
		QListWidgetItem* item = m_list->item(i);
		if (item->checkState() == Qt::Checked)
			ids.append(item->data(Qt::UserRole).toString());
	}
	return ids;
}

bool TrainFilterButton::isTrainVisible(const QString& trainId) const {
	for (int i = 0; i < m_list->count(); ++i) {
		QListWidgetItem* item = m_list->item(i);
		if (item->data(Qt::UserRole).toString() == trainId)
			return item->checkState() == Qt::Checked;
	}
	return true;
}

void TrainFilterButton::applySearchFilter() {
	const QString needle = m_search->text().trimmed();
	for (int i = 0; i < m_list->count(); ++i) {
		QListWidgetItem* item = m_list->item(i);
		item->setHidden(!needle.isEmpty()
						&& !item->text().contains(needle, Qt::CaseInsensitive));
	}
}

// All/None act on the rows the search currently shows, so a filtered "None"
// hides one service group without touching the rest.
void TrainFilterButton::checkAllVisible(bool checked) {
	m_updating = true;
	for (int i = 0; i < m_list->count(); ++i) {
		QListWidgetItem* item = m_list->item(i);
		if (!item->isHidden())
			item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
	}
	m_updating = false;
	updateCaption();
	emit selectionChanged();
}

void TrainFilterButton::updateCaption() {
	const int total = m_list ? m_list->count() : 0;
	if (total == 0) {
		setText("Trains");
		return;
	}
	setText(QString("Trains (%1/%2)").arg(visibleTrainIds().size()).arg(total));
}
