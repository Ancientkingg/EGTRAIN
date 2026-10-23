#include "diagrams/TrainKeyList.h"

#include "diagrams/DiagramKey.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {
constexpr int kIdRole = Qt::UserRole;		  // the id of the train of a row; empty for a group
constexpr int kOrderRole = Qt::UserRole + 1;  // the place of the train or the service in the order setTrains gave
constexpr int kColourRole = Qt::UserRole + 2; // the colour of the sample of a row
constexpr int kSearchAbove = 12;			  // the number of trains above which the search shows
constexpr int kAllNoneAbove = 6;			  // the number of trains above which All and None show
constexpr int kGroupAbove = 6;				  // the number of trains above which services group them
constexpr int kMaxGroups = 6;				  // the number of services up to which they group the trains
constexpr int kVisibleRows = 12;			  // the rows the list shows before it scrolls
// The highlight of the selected train, the colour of a selected row of a view without the focus in egtrain.qss.
const QColor kSelectedRow(0xd8, 0xe1, 0xe5);

// A line sample in `colour`, as the Key draws a line; transparent for an invalid colour, so that the ids stay in line.
QIcon lineSample(const QColor& colour) {
	constexpr qreal ratio = 2;
	QPixmap pixmap(QSize(static_cast<int>(kKeySampleWidth), static_cast<int>(kKeySampleHeight)) * ratio);
	pixmap.setDevicePixelRatio(ratio);
	pixmap.fill(Qt::transparent);
	if (colour.isValid()) {
		QPainter painter(&pixmap);
		DiagramKeyEntry entry;
		entry.pen = QPen(colour, 2.5, Qt::SolidLine, Qt::RoundCap);
		paintKeySample(painter, QRectF(0, 0, kKeySampleWidth, kKeySampleHeight), entry);
	}
	return QIcon(pixmap);
}

// The rows of the list. They take no selection of their own, and the arrow keys still move between them in a style that
// moves the current row only with the selection, as that of macOS does: a key asks for a command that selects nothing.
class TrainTree : public QTreeWidget {
public:
	using QTreeWidget::QTreeWidget;

protected:
	QItemSelectionModel::SelectionFlags selectionCommand(const QModelIndex& index, const QEvent* event) const override {
		const QItemSelectionModel::SelectionFlags command = QTreeWidget::selectionCommand(index, event);
		const bool key = event && event->type() == QEvent::KeyPress;
		return key && command == QItemSelectionModel::NoUpdate ? QItemSelectionModel::SelectionFlags(QItemSelectionModel::Rows) : command;
	}
	// A style sheet that draws the rows leaves their background out, so the band of the selected train is drawn here,
	// across the whole row.
	void drawRow(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
		if (const QBrush band = index.data(Qt::BackgroundRole).value<QBrush>(); band.style() != Qt::NoBrush)
			painter->fillRect(option.rect, band);
		QTreeWidget::drawRow(painter, option, index);
	}
};

bool isGroup(const QTreeWidgetItem* item) {
	return item->data(0, kIdRole).toString().isEmpty();
}

// A group shows the colour that its shown trains share, else, while none is shown, the colour that all its trains share,
// else no line sample.
void showSharedColour(QTreeWidgetItem* group) {
	QList<QColor> colours;
	for (const bool shownOnly : {true, false}) {
		for (int i = 0; i < group->childCount(); ++i)
			if (!shownOnly || group->child(i)->checkState(0) == Qt::Checked) colours << group->child(i)->data(0, kColourRole).value<QColor>();
		if (!colours.isEmpty()) break;
	}
	const bool shared = !colours.isEmpty() && std::all_of(colours.cbegin(), colours.cend(), [&colours](const QColor& colour) {
		return colour == colours.first();
	});
	const QColor colour = shared ? colours.first() : QColor();
	if (!group->icon(0).isNull() && group->data(0, kColourRole).value<QColor>() == colour) return;
	group->setData(0, kColourRole, colour);
	group->setIcon(0, lineSample(colour));
}

// The row and its group, if it has one, are not hidden by the search.
bool searchShows(const QTreeWidgetItem* item) {
	return !item->isHidden() && (!item->parent() || !item->parent()->isHidden());
}
} // namespace

TrainKeyList::TrainKeyList(QWidget* parent)
	: QWidget(parent) {
	setObjectName("trainKeyList");
	m_count = new QLabel(this);
	m_count->setObjectName("trainKeyCount");
	m_count->setTextFormat(Qt::PlainText);

	m_searchRow = new QWidget(this);
	auto* searchLayout = new QHBoxLayout(m_searchRow);
	searchLayout->setContentsMargins(0, 0, 0, 0);
	searchLayout->setSpacing(4);
	m_search = new QLineEdit(m_searchRow);
	m_search->setObjectName("trainKeySearch");
	m_search->setPlaceholderText("Search trains");
	m_search->setAccessibleName("Search trains");
	m_search->setClearButtonEnabled(true);
	m_search->installEventFilter(this);
	connect(m_search, &QLineEdit::textChanged, this, [this] { applySearch(); });
	m_all = new QPushButton("All", m_searchRow);
	m_all->setObjectName("trainKeyAll");
	m_all->setAccessibleName("Show listed trains");
	m_all->setToolTip("Show the trains of the rows in the list");
	connect(m_all, &QPushButton::clicked, this, [this] { setSearchedVisible(true); });
	m_none = new QPushButton("None", m_searchRow);
	m_none->setObjectName("trainKeyNone");
	m_none->setAccessibleName("Hide listed trains");
	m_none->setToolTip("Hide the trains of the rows in the list");
	connect(m_none, &QPushButton::clicked, this, [this] { setSearchedVisible(false); });
	searchLayout->addWidget(m_search, 1);
	searchLayout->addWidget(m_all);
	searchLayout->addWidget(m_none);
	m_searchRow->hide();

	m_tree = new TrainTree(this);
	m_tree->setObjectName("trainKeyTree");
	m_tree->setAccessibleName("Trains");
	m_tree->setColumnCount(2);
	m_tree->setHeaderHidden(true);
	m_tree->header()->setStretchLastSection(false);
	m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	// The rows are not selected: the highlight belongs to the train that the chart selects.
	m_tree->setSelectionMode(QAbstractItemView::NoSelection);
	m_tree->setUniformRowHeights(true);
	m_tree->setIconSize(QSize(static_cast<int>(kKeySampleWidth), static_cast<int>(kKeySampleHeight)));
	m_tree->setFrameShape(QFrame::NoFrame);
	m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_tree->setTextElideMode(Qt::ElideRight);
	m_tree->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	m_tree->viewport()->installEventFilter(this);
	m_tree->installEventFilter(this);
	connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
		if (column == 0) itemChanged(item);
	});
	connect(m_tree, &QTreeWidget::itemExpanded, this, [this] { fitHeight(); });
	connect(m_tree, &QTreeWidget::itemCollapsed, this, [this] { fitHeight(); });

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(6);
	layout->addWidget(m_searchRow);
	layout->addWidget(m_tree);
	setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	updateCount();
	fitHeight();
}

void TrainKeyList::setTrains(const QVector<Train>& trains) {
	QHash<QString, Qt::CheckState> states;
	for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) states.insert(it.key(), it.value()->checkState(0));
	QSet<QString> expanded;
	for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
		if (isGroup(m_tree->topLevelItem(i)) && m_tree->topLevelItem(i)->isExpanded()) expanded.insert(m_tree->topLevelItem(i)->text(0));

	m_updating = true;
	m_tree->clear();
	m_items.clear();
	m_order.clear();
	m_colours.clear();
	m_services.clear();
	QStringList services;
	QHash<QString, QVector<const Train*>> byService;
	for (const Train& train : trains) {
		if (train.id.isEmpty() || m_colours.contains(train.id)) continue;
		m_order << train.id;
		m_colours.insert(train.id, train.colour);
		// A train without a known service is a service of its own.
		const QString service = train.service.isEmpty() ? train.id : train.service;
		m_services.insert(train.id, service);
		if (!byService.contains(service)) services << service;
		byService[service] << &train;
	}
	m_grouped = m_order.size() > kGroupAbove && services.size() <= kMaxGroups;
	const auto addTrain = [&](const Train& train, QTreeWidgetItem* group) {
		auto* item = group ? new QTreeWidgetItem(group) : new QTreeWidgetItem(m_tree);
		item->setText(0, train.id);
		item->setToolTip(0, train.id);
		item->setData(0, kIdRole, train.id);
		item->setData(0, kOrderRole, m_order.indexOf(train.id));
		item->setData(0, kColourRole, train.colour);
		item->setIcon(0, lineSample(train.colour));
		item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
		item->setCheckState(0, states.value(train.id, Qt::Checked));
		m_items.insert(train.id, item);
	};
	if (m_grouped) {
		for (int index = 0; index < services.size(); ++index) {
			const QString& service = services[index];
			const QVector<const Train*>& members = byService[service];
			auto* group = new QTreeWidgetItem(m_tree);
			group->setText(0, service);
			group->setToolTip(0, QString("%1, %2 trains").arg(service).arg(members.size()));
			group->setData(0, kOrderRole, index);
			group->setText(1, QString::number(members.size()));
			group->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
			group->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
			for (const Train* member : members) addTrain(*member, group);
			updateGroupState(group);
			group->setExpanded(expanded.contains(service));
		}
	} else {
		for (const Train& train : trains)
			if (!train.id.isEmpty() && !m_items.contains(train.id)) addTrain(train, nullptr);
	}
	m_tree->setRootIsDecorated(m_grouped);
	m_tree->setColumnHidden(1, !m_grouped);
	if (!m_items.contains(m_selected)) m_selected.clear();
	if (QTreeWidgetItem* selected = m_items.value(m_selected)) {
		selected->setBackground(0, kSelectedRow);
		selected->setBackground(1, kSelectedRow);
		if (selected->parent()) selected->parent()->setExpanded(true);
	}
	m_updating = false;
	const bool searchable = m_order.size() > kSearchAbove;
	m_searchRow->setVisible(m_order.size() > kAllNoneAbove);
	m_search->setVisible(searchable);
	// A search that the list no longer offers hides no row.
	if (!searchable) m_search->clear();
	sortRows();
	updateCount();
}

void TrainKeyList::setColours(const QHash<QString, QColor>& colours) {
	// A new icon changes the row, which the list must not take for a click on its check box.
	const bool updating = m_updating;
	m_updating = true;
	for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) {
		const QColor colour = colours.value(it.key());
		m_colours.insert(it.key(), colour);
		if (it.value()->data(0, kColourRole).value<QColor>() == colour) continue;
		it.value()->setData(0, kColourRole, colour);
		it.value()->setIcon(0, lineSample(colour));
	}
	for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
		if (isGroup(m_tree->topLevelItem(i))) showSharedColour(m_tree->topLevelItem(i));
	m_updating = updating;
}

QList<QWidget*> TrainKeyList::focusChain() const {
	return {m_search, m_all, m_none, m_tree};
}

QStringList TrainKeyList::trainIds() const {
	return m_order;
}

QStringList TrainKeyList::visibleTrainIds() const {
	QStringList ids;
	for (const QString& id : m_order)
		if (m_items.value(id)->checkState(0) == Qt::Checked) ids << id;
	return ids;
}

bool TrainKeyList::isTrainVisible(const QString& trainId) const {
	const QTreeWidgetItem* item = m_items.value(trainId);
	return !item || item->checkState(0) == Qt::Checked;
}

void TrainKeyList::setTrainVisible(const QString& trainId, bool visible) {
	QTreeWidgetItem* item = m_items.value(trainId);
	const Qt::CheckState state = visible ? Qt::Checked : Qt::Unchecked;
	if (!item || item->checkState(0) == state) return;
	// The row reports the change itself.
	item->setCheckState(0, state);
}

void TrainKeyList::setSearchedVisible(bool visible) {
	m_updating = true;
	for (QTreeWidgetItem* item : std::as_const(m_items))
		if (searchShows(item)) item->setCheckState(0, visible ? Qt::Checked : Qt::Unchecked);
	for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
		if (isGroup(m_tree->topLevelItem(i))) updateGroupState(m_tree->topLevelItem(i));
	m_updating = false;
	updateCount();
	sortSoon();
	emit visibilityChanged();
}

void TrainKeyList::setSelectedTrain(const QString& trainId) {
	// A new band changes the row, which the list must not take for a click on its check box.
	const bool updating = m_updating;
	m_updating = true;
	if (QTreeWidgetItem* old = m_items.value(m_selected)) {
		old->setBackground(0, QBrush());
		old->setBackground(1, QBrush());
	}
	QTreeWidgetItem* item = m_items.value(trainId);
	m_selected = item ? trainId : QString();
	if (item) {
		item->setBackground(0, kSelectedRow);
		item->setBackground(1, kSelectedRow);
		if (item->parent()) item->parent()->setExpanded(true);
	}
	m_updating = updating;
	sortSoon();
	if (item) m_tree->scrollToItem(item);
}

QVector<QPair<QString, QColor>> TrainKeyList::shownRows() const {
	QVector<QPair<QString, QColor>> rows;
	if (!m_grouped) {
		for (const QString& id : m_order)
			if (m_items.value(id)->checkState(0) == Qt::Checked) rows.append({id, m_colours.value(id)});
		return rows;
	}
	QList<QTreeWidgetItem*> groups;
	for (int i = 0; i < m_tree->topLevelItemCount(); ++i) groups << m_tree->topLevelItem(i);
	std::sort(groups.begin(), groups.end(),
		[](const QTreeWidgetItem* a, const QTreeWidgetItem* b) { return a->data(0, kOrderRole).toInt() < b->data(0, kOrderRole).toInt(); });
	for (const QTreeWidgetItem* group : groups) {
		QVector<QPair<QString, QColor>> shown;
		for (int i = 0; i < group->childCount(); ++i)
			if (const QTreeWidgetItem* child = group->child(i); child->checkState(0) == Qt::Checked)
				shown.append({child->data(0, kIdRole).toString(), child->data(0, kColourRole).value<QColor>()});
		if (shown.isEmpty()) continue;
		// A group whose shown trains differ in colour lists them, so that the figure still keys each line.
		const QColor colour = shown.first().second;
		if (std::all_of(shown.cbegin(), shown.cend(), [&colour](const auto& row) { return row.second == colour; }))
			rows.append({group->text(0), colour});
		else
			rows += shown;
	}
	return rows;
}

bool TrainKeyList::eventFilter(QObject* watched, QEvent* event) {
	// Escape clears a search before it reaches the window, which it would close.
	if (watched == m_search && event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape
		&& !m_search->text().isEmpty()) {
		m_search->clear();
		return true;
	}
	if (m_tree && watched == m_tree->viewport()) {
		if (event->type() == QEvent::Enter) {
			m_hovered = true;
		} else if (event->type() == QEvent::Leave || event->type() == QEvent::Hide) {
			m_hovered = false;
			if (m_sortPending && !m_tree->hasFocus()) sortRows();
		}
	} else if (m_tree && watched == m_tree && event->type() == QEvent::FocusOut) {
		if (m_sortPending && !m_hovered) sortRows();
	}
	return QWidget::eventFilter(watched, event);
}

// A click or Space on a group row shows or hides the trains of the group that the search shows, and the group then
// shows whether all, some or none of its trains are shown.
void TrainKeyList::itemChanged(QTreeWidgetItem* item) {
	if (m_updating) return;
	m_updating = true;
	if (isGroup(item)) {
		const Qt::CheckState state = item->checkState(0) == Qt::Unchecked ? Qt::Unchecked : Qt::Checked;
		for (int i = 0; i < item->childCount(); ++i)
			if (!item->child(i)->isHidden()) item->child(i)->setCheckState(0, state);
		updateGroupState(item);
	} else if (item->parent()) {
		updateGroupState(item->parent());
	}
	m_updating = false;
	updateCount();
	sortSoon();
	emit visibilityChanged();
}

// A group row and its trains are left out when neither the service nor any of its trains holds the text of the search.
// A search that finds a group or trains of it opens the group, and the group closes again when the search is cleared.
// The check states stay as they are.
void TrainKeyList::applySearch() {
	const QString needle = m_search->text().trimmed();
	for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
		QTreeWidgetItem* row = m_tree->topLevelItem(i);
		const bool rowMatches = needle.isEmpty() || row->text(0).contains(needle, Qt::CaseInsensitive);
		if (!isGroup(row)) {
			row->setHidden(!rowMatches);
			continue;
		}
		bool anyTrain = false;
		for (int child = 0; child < row->childCount(); ++child) {
			const bool matches = rowMatches || row->child(child)->text(0).contains(needle, Qt::CaseInsensitive);
			row->child(child)->setHidden(!matches);
			anyTrain = anyTrain || matches;
		}
		row->setHidden(!anyTrain);
		if (needle.isEmpty()) {
			if (m_openedBySearch.remove(row->text(0))) row->setExpanded(false);
		} else if (anyTrain && !row->isExpanded()) {
			m_openedBySearch.insert(row->text(0));
			row->setExpanded(true);
		}
		const bool updating = m_updating;
		m_updating = true;
		updateGroupState(row);
		m_updating = updating;
	}
	fitHeight();
}

// The selected train first, then the shown trains, then the hidden ones, each in the order setTrains gave them; a group
// by its first train in that order.
int TrainKeyList::rank(const QTreeWidgetItem* item) const {
	if (!isGroup(item)) {
		if (item->data(0, kIdRole).toString() == m_selected) return 0;
		return item->checkState(0) == Qt::Checked ? 1 : 2;
	}
	int best = 2;
	for (int i = 0; i < item->childCount(); ++i) best = std::min(best, rank(item->child(i)));
	return best;
}

void TrainKeyList::sortRows() {
	m_sortPending = false;
	const auto before = [this](const QTreeWidgetItem* a, const QTreeWidgetItem* b) {
		const int rankA = rank(a), rankB = rank(b);
		return rankA != rankB ? rankA < rankB : a->data(0, kOrderRole).toInt() < b->data(0, kOrderRole).toInt();
	};
	QTreeWidgetItem* current = m_tree->currentItem();
	m_updating = true;
	// The children of each group, then the rows at the top; a list in order is left alone, so the view keeps its state.
	const auto sortChildren = [&](QTreeWidgetItem* parent) {
		QList<QTreeWidgetItem*> children;
		for (int i = 0; i < parent->childCount(); ++i) children << parent->child(i);
		if (std::is_sorted(children.begin(), children.end(), before)) return;
		std::stable_sort(children.begin(), children.end(), before);
		parent->takeChildren();
		parent->addChildren(children);
	};
	QList<QTreeWidgetItem*> rows;
	for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
		rows << m_tree->topLevelItem(i);
		if (isGroup(rows.last())) sortChildren(rows.last());
	}
	if (!std::is_sorted(rows.begin(), rows.end(), before)) {
		QSet<const QTreeWidgetItem*> expanded;
		for (const QTreeWidgetItem* row : rows)
			if (row->isExpanded()) expanded.insert(row);
		std::stable_sort(rows.begin(), rows.end(), before);
		while (m_tree->topLevelItemCount() > 0) m_tree->takeTopLevelItem(0);
		m_tree->addTopLevelItems(rows);
		for (QTreeWidgetItem* row : rows) row->setExpanded(expanded.contains(row));
	}
	m_updating = false;
	if (current) m_tree->setCurrentItem(current, 0, QItemSelectionModel::NoUpdate);
	// A row that left the view and came back is no longer hidden by the search.
	applySearch();
}

// Sorts now, or once the pointer and the focus have left the list, so that a row does not move from under them.
void TrainKeyList::sortSoon() {
	if (m_hovered || m_tree->hasFocus())
		m_sortPending = true;
	else
		sortRows();
}

// A group counts its trains that the search shows, as a click on it acts on those, else all of them. Its line sample
// follows the trains it shows.
void TrainKeyList::updateGroupState(QTreeWidgetItem* group) {
	int counted = 0, shown = 0;
	for (const bool searched : {true, false}) {
		for (int i = 0; i < group->childCount(); ++i) {
			if (searched && group->child(i)->isHidden()) continue;
			++counted;
			shown += group->child(i)->checkState(0) == Qt::Checked ? 1 : 0;
		}
		if (counted > 0) break;
	}
	Qt::CheckState state = Qt::PartiallyChecked;
	if (shown == 0) state = Qt::Unchecked;
	if (shown == counted) state = Qt::Checked;
	group->setCheckState(0, state);
	showSharedColour(group);
}

void TrainKeyList::updateCount() {
	m_count->setText(m_order.isEmpty() ? QString() : QString("%1 of %2").arg(visibleTrainIds().size()).arg(m_order.size()));
}

// The list is as tall as its rows up to kVisibleRows, and scrolls beyond that.
void TrainKeyList::fitHeight() {
	int rows = 0;
	for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
		const QTreeWidgetItem* row = m_tree->topLevelItem(i);
		if (row->isHidden()) continue;
		++rows;
		if (row->isExpanded())
			for (int child = 0; child < row->childCount(); ++child) rows += row->child(child)->isHidden() ? 0 : 1;
	}
	const int rowHeight = m_tree->topLevelItemCount() > 0 ? m_tree->sizeHintForRow(0) : 0;
	const int height = std::clamp(rows, 1, kVisibleRows) * std::max(rowHeight, fontMetrics().height() + 8);
	m_tree->setFixedHeight(height + 2 * m_tree->frameWidth());
}
