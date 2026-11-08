#include "editors/InfrastructurePanel.h"

#include "editors/CountText.h"
#include "editors/infrastructure/InfrastructureGuidance.h"
#include "scene/SectionInventory.h"
#include "widgets/EditorLayout.h"
#include "widgets/FindDialog.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QCompleter>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSplitter>
#include <QStyle>
#include <QStyleOption>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cstddef>
#include <set>
#include <string>

namespace {

struct FacetEntry {
	const char* key;
	const char* title;
	/// The name of one row, in sentence case and in Title Case.
	const char* noun;
	const char* titleNoun;
};

// The facets in the order of the combo.
constexpr FacetEntry kFacets[] = {
	{"tracks", "Tracks", "Track", "Track"},
	{"nodes", "Nodes", "Node", "Node"},
	{"arcs", "Arcs", "Arc", "Arc"},
	{"blocks", "Blocks", "Block", "Block"},
	{"connections", "Connections", "Connection", "Connection"},
	{"stations", "Stations", "Station", "Station"},
	{"platforms", "Platforms", "Platform", "Platform"},
	{"signals", "Signals", "Signal", "Signal"},
	{"signalling_areas", "Signalling areas", "Signalling area", "Signalling Area"},
	{"routes", "Routes", "Route", "Route"},
	{"block_dependencies", "Block dependencies", "Block dependency", "Block Dependency"},
	{"single_track_restrictions", "Single-track restrictions", "Single-track restriction", "Single-Track Restriction"},
	{"station_boundaries", "Station boundaries", "Station boundary", "Station Boundary"},
};

const FacetEntry* facetEntry(const QString& key) {
	for (const FacetEntry& entry : kFacets)
		if (key == QLatin1String(entry.key))
			return &entry;
	return nullptr;
}

const SceneModel& emptyScene() {
	static const SceneModel scene;
	return scene;
}

// A chainage in kilometres with three decimals and a full stop in every locale.
QString kilometres(double value) {
	return QString::number(value, 'f', 3);
}

// Names as a sentence lists them: "A", "A and B", "A, B and C".
QString sentenceList(const QStringList& names) {
	if (names.size() <= 1)
		return names.value(0);
	return names.mid(0, names.size() - 1).join(QStringLiteral(", ")) + QStringLiteral(" and ") + names.last();
}

// The item with the id, or null.
template <typename Item>
const Item* withId(const std::vector<Item>& items, const std::string& id) {
	const auto found = std::find_if(items.begin(), items.end(), [&id](const Item& item) { return item.id == id; });
	return found == items.end() ? nullptr : &*found;
}

// The item in the row that the id names by its number, or null.
template <typename Item>
const Item* inRow(const std::vector<Item>& items, const QString& id) {
	bool ok = false;
	const int row = id.toInt(&ok);
	return ok && row >= 0 && row < static_cast<int>(items.size()) ? &items[static_cast<std::size_t>(row)] : nullptr;
}

// What the line under the table says the canvas marks of the entity `id` of the facet `facet`: the id of the entity, and
// the tracks, the kilometres, the nodes and the sections that the table shows of it, such as "route2: 124 sections on B3,
// B0, B4, B5 and B2". A section is named by its label, as in the combos of the table. Empty without an id and for an
// entity that is not in the model.
QString markText(const SceneModel& scene, const QString& facet, const QString& id) {
	if (id.isEmpty())
		return {};
	const std::string key = id.toStdString();
	const auto nodeOf = [&scene](const std::string& nodeId) { return withId(scene.nodes, nodeId); };
	const auto trackName = [](const std::string& trackId) {
		return trackId.empty() ? QStringLiteral("no track") : QString::fromStdString(trackId);
	};
	SceneSectionInventory inventory;
	SceneSectionLabels labels;
	const auto buildSections = [&]() {
		inventory = buildSceneSectionInventory(scene);
		labels = sceneSectionLabels(inventory);
	};
	// The label of the section that a reference names, the reference with "(missing)" when it names none.
	const auto section = [&inventory, &labels](const std::string& reference) {
		if (inventory.resolve(reference))
			return QString::fromStdString(labels.label(reference));
		return QStringLiteral("%1 (missing)").arg(QString::fromStdString(reference));
	};

	if (facet == QLatin1String("tracks")) {
		bool found = false;
		double low = 0.0;
		double high = 0.0;
		for (const SceneNode& node : scene.nodes) {
			if (node.trackId != key)
				continue;
			low = found ? std::min(low, node.xKm) : node.xKm;
			high = found ? std::max(high, node.xKm) : node.xKm;
			found = true;
		}
		if (!withId(scene.tracks, key))
			return {};
		return found ? QStringLiteral("%1: the whole track, %2 to %3 km").arg(id, kilometres(low), kilometres(high))
					 : QStringLiteral("%1: the whole track").arg(id);
	}
	if (facet == QLatin1String("nodes")) {
		const SceneNode* node = nodeOf(key);
		return node ? QStringLiteral("%1: node on %2 at %3 km").arg(id, trackName(node->trackId), kilometres(node->xKm)) : QString();
	}
	if (facet == QLatin1String("arcs")) {
		const SceneArc* arc = withId(scene.arcs, key);
		if (!arc)
			return {};
		const SceneNode* from = nodeOf(arc->fromNodeId);
		const SceneNode* to = nodeOf(arc->toNodeId);
		if (!from || !to)
			return QStringLiteral("%1: arc on %2").arg(id, trackName(arc->trackId));
		return QStringLiteral("%1: arc on %2 from %3 to %4 km")
			.arg(id, trackName(arc->trackId), kilometres(from->xKm), kilometres(to->xKm));
	}
	if (facet == QLatin1String("connections")) {
		const SceneConnection* connection = withId(scene.connections, key);
		if (!connection)
			return {};
		const SceneNode* from = nodeOf(connection->fromNodeId);
		const SceneNode* to = nodeOf(connection->toNodeId);
		if (!from || !to)
			return QStringLiteral("%1: connection").arg(id);
		return QStringLiteral("%1: from %2 at %3 km to %4 at %5 km")
			.arg(id, trackName(from->trackId), kilometres(from->xKm), trackName(to->trackId), kilometres(to->xKm));
	}
	if (facet == QLatin1String("blocks")) {
		if (!withId(scene.blocks, key))
			return {};
		buildSections();
		const std::vector<std::size_t>* positions = inventory.sections.withSourceBlock(key);
		if (!positions)
			return QStringLiteral("%1: no section").arg(id);
		QStringList names;
		for (const std::size_t position : *positions)
			names << InfrastructurePanel::sectionLabel(labels, inventory.sections[position]);
		return QStringLiteral("%1: section %2").arg(id, sentenceList(names));
	}
	if (facet == QLatin1String("stations")) {
		const SceneStation* station = withId(scene.stations, key);
		if (!station)
			return {};
		QStringList nodes;
		QStringList tracks;
		const SceneNode* first = nullptr;
		for (const ScenePlatform& platform : station->platforms)
			for (const std::string& nodeId : platform.nodeIds) {
				const SceneNode* node = nodeOf(nodeId);
				if (!node || nodes.contains(QString::fromStdString(nodeId)))
					continue;
				nodes << QString::fromStdString(nodeId);
				if (!tracks.contains(trackName(node->trackId)))
					tracks << trackName(node->trackId);
				first = first ? first : node;
			}
		if (nodes.size() == 1)
			return QStringLiteral("%1: station mark and platform node on %2 at %3 km")
				.arg(id, trackName(first->trackId), kilometres(first->xKm));
		if (nodes.size() > 1)
			return QStringLiteral("%1: station mark and %2 platform nodes on %3")
				.arg(id, QString::number(nodes.size()), sentenceList(tracks));
		if (station->hasPosition)
			return QStringLiteral("%1: station mark at %2 km").arg(id, kilometres(station->positionKm));
		return QStringLiteral("%1: station mark").arg(id);
	}
	if (facet == QLatin1String("platforms")) {
		const qsizetype separator = id.indexOf(QLatin1Char('\n'));
		const SceneStation* station = separator < 0 ? nullptr : withId(scene.stations, id.left(separator).toStdString());
		const ScenePlatform* platform = station ? withId(station->platforms, id.mid(separator + 1).toStdString()) : nullptr;
		if (!platform)
			return {};
		const QString name = QString::fromStdString(platform->id);
		QStringList nodes;
		for (const std::string& nodeId : platform->nodeIds)
			nodes << QString::fromStdString(nodeId);
		if (nodes.isEmpty())
			return QStringLiteral("%1: no node").arg(name);
		if (const SceneNode* node = nodes.size() == 1 ? nodeOf(platform->nodeIds.front()) : nullptr)
			return QStringLiteral("%1: node %2 at %3 km").arg(name, nodes.front(), kilometres(node->xKm));
		return QStringLiteral("%1: %2 %3").arg(name, nodes.size() == 1 ? QStringLiteral("node") : QStringLiteral("nodes"), sentenceList(nodes));
	}
	if (facet == QLatin1String("signals")) {
		const SceneSignal* signal = withId(sceneSignals(scene), key);
		if (!signal)
			return {};
		if (signal->protectedSection.empty())
			return QStringLiteral("%1: protects no section").arg(id);
		buildSections();
		return QStringLiteral("%1: protects %2").arg(id, section(signal->protectedSection));
	}
	if (facet == QLatin1String("signalling_areas")) {
		const SceneSignallingArea* area = withId(scene.signallingAreas, key);
		if (!area)
			return {};
		return QStringLiteral("%1: %2 to %3 km on %4")
			.arg(id, kilometres(area->startKm), kilometres(area->endKm),
				area->trackId.empty() ? QStringLiteral("every track") : QString::fromStdString(area->trackId));
	}
	if (facet == QLatin1String("routes")) {
		const SceneRoute* route = withId(scene.routes, key);
		if (!route)
			return {};
		buildSections();
		QStringList tracks;
		int missing = 0;
		for (const std::string& reference : route->blocks) {
			const SceneSectionDescriptor* resolved = inventory.resolve(reference);
			if (!resolved) {
				++missing;
				continue;
			}
			for (const std::string* trackId : {&resolved->firstTrackId, &resolved->secondTrackId})
				if (!trackId->empty() && !tracks.contains(QString::fromStdString(*trackId)))
					tracks << QString::fromStdString(*trackId);
		}
		const qsizetype count = static_cast<qsizetype>(route->blocks.size());
		QString text = QStringLiteral("%1: %2 %3")
						   .arg(id, QString::number(count), count == 1 ? QStringLiteral("section") : QStringLiteral("sections"));
		if (!tracks.isEmpty())
			text += QStringLiteral(" on ") + sentenceList(tracks);
		if (missing > 0)
			text += missing == 1 ? QStringLiteral("; 1 is missing") : QStringLiteral("; %1 are missing").arg(missing);
		return text;
	}
	if (facet == QLatin1String("single_track_restrictions")) {
		const SceneSingleTrackRestriction* restriction = inRow(scene.singleTrackRestrictions, id);
		if (!restriction)
			return {};
		buildSections();
		const SceneSectionDescriptor* start = inventory.resolve(restriction->startBlock);
		const SceneSectionDescriptor* end = inventory.resolve(restriction->endBlock);
		QString text;
		if (start && end && !start->connectionDerived && !end->connectionDerived && start->firstTrackId == end->firstTrackId)
			text = QStringLiteral("%1 %2 to %3 km")
					   .arg(trackName(start->firstTrackId), kilometres(std::min(start->startKm, end->startKm)),
						   kilometres(std::max(start->endKm, end->endKm)));
		else
			text = QStringLiteral("%1 to %2").arg(section(restriction->startBlock), section(restriction->endBlock));
		QStringList protectedEnds;
		for (const std::string* reference : {&restriction->protectedStartBlock, &restriction->protectedEndBlock})
			if (!reference->empty())
				protectedEnds << section(*reference);
		// The label of a switch section has a comma, so each end has its own "at".
		if (!protectedEnds.isEmpty())
			text += QStringLiteral(", protected at ") + protectedEnds.join(QStringLiteral(" and at "));
		return QStringLiteral("Single-track restriction: ") + text;
	}
	if (facet == QLatin1String("station_boundaries")) {
		const SceneStationBoundary* boundary = inRow(scene.stationBoundaries, id);
		if (!boundary)
			return {};
		buildSections();
		const QString entrance = boundary->entranceBlock.empty() ? QStringLiteral("none") : section(boundary->entranceBlock);
		return QStringLiteral("Station boundary: entrance %1, %2")
			.arg(entrance, boundary->hasExitBlock ? QStringLiteral("exit ") + section(boundary->exitBlock) : QStringLiteral("no exit"));
	}
	if (facet == QLatin1String("block_dependencies")) {
		const SceneBlockDependency* dependency = inRow(scene.blockDependencies, id);
		if (!dependency)
			return {};
		buildSections();
		const auto named = [&section](const std::string& reference) {
			return reference.empty() ? QStringLiteral("none") : section(reference);
		};
		return QStringLiteral("Block dependency: %1 depends on %2").arg(named(dependency->block), named(dependency->dependsOn));
	}
	return {};
}

// The row that an item of the track preview stands for (see InfrastructurePanel::selectCanvasItem).
InfrastructurePanel::RowTarget canvasRow(const SceneModel& scene, const QString& kind, const QString& id, const QString& platform) {
	const std::string key = id.toStdString();
	if (kind == QLatin1String("station-node") && !platform.isEmpty()) {
		const std::string platformId = platform.toStdString();
		for (const SceneStation& station : scene.stations)
			for (const ScenePlatform& candidate : station.platforms)
				if (candidate.id == platformId
					&& std::find(candidate.nodeIds.begin(), candidate.nodeIds.end(), key) != candidate.nodeIds.end())
					return {QStringLiteral("platforms"), QString::fromStdString(station.id) + QLatin1Char('\n') + platform, {}};
	}
	if ((kind == QLatin1String("node") || kind == QLatin1String("station-node")) && withId(scene.nodes, key))
		return {QStringLiteral("nodes"), id, {}};
	if (kind == QLatin1String("track") && withId(scene.tracks, key))
		return {QStringLiteral("tracks"), id, {}};
	if (kind == QLatin1String("arc") && withId(scene.arcs, key))
		return {QStringLiteral("arcs"), id, {}};
	if (kind == QLatin1String("connection") && withId(scene.connections, key))
		return {QStringLiteral("connections"), id, {}};
	if (kind == QLatin1String("station") && withId(scene.stations, key))
		return {QStringLiteral("stations"), id, {}};
	if (kind == QLatin1String("signal")) {
		const SceneSectionInventory inventory = buildSceneSectionInventory(scene);
		const SceneSectionDescriptor* section = inventory.resolve(key);
		if (const SceneBlock* block = section ? withId(scene.blocks, section->sourceBlockId) : nullptr)
			return {QStringLiteral("blocks"), QString::fromStdString(block->id), QString::fromStdString(block->trackId)};
	}
	return {};
}

// The editor of a cell that refers to other entities. It ignores the mouse wheel, so the wheel scrolls the table behind it,
// and the keys up and down without Alt or Ctrl, so they move to another row as from a text cell instead of choosing
// another entity.
class ReferenceComboBox : public QComboBox {
public:
	using QComboBox::QComboBox;

protected:
	void wheelEvent(QWheelEvent* event) override { event->ignore(); }
	void keyPressEvent(QKeyEvent* event) override {
		const int key = event->key();
		const bool rowKey = key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_PageUp || key == Qt::Key_PageDown;
		if (rowKey && !(event->modifiers() & (Qt::AltModifier | Qt::ControlModifier)))
			event->ignore();
		else
			QComboBox::keyPressEvent(event);
	}
};

} // namespace

/// The delegate of the table. It knows the editor that it has open on a cell. The widgets that facets put in cells with
/// setCellWidget are not its editors.
class InfrastructurePanel::CellDelegate : public QStyledItemDelegate {
public:
	/// The delegate of the table of `panel`; `parent` owns it.
	CellDelegate(InfrastructurePanel& panel, QObject* parent) : QStyledItemDelegate(parent), m_panel(panel) {}

	/// Makes the editor of a cell and keeps it as the open editor. A cell that refers to other entities
	/// (InfrastructureFacet::choices) gets a combo of their labels, in which the user can also type, and which completes
	/// what is typed from any part of a label, ignoring case. Choosing an entity in its list ends the edit, as Return does.
	/// Another cell gets the editor of QStyledItemDelegate.
	QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
		const InfrastructureFacet* facet = m_panel.currentFacet();
		const std::vector<InfrastructureFacet::Choice> choices = facet ? facet->choices(m_panel, index.row(), index.column()) : std::vector<InfrastructureFacet::Choice>();
		m_editor = choices.empty() ? QStyledItemDelegate::createEditor(parent, option, index) : referenceEditor(parent, index, choices);
		return m_editor;
	}

	/// A combo of choices shows the item of the id in the cell, or the id itself when it offers no item for it.
	void setEditorData(QWidget* editor, const QModelIndex& index) const override {
		// The default editors of the text cells are line edits, so the only combo is one of choices.
		auto* combo = qobject_cast<QComboBox*>(editor);
		if (!combo) {
			QStyledItemDelegate::setEditorData(editor, index);
			return;
		}
		const QString id = index.data(Qt::EditRole).toString();
		combo->setCurrentIndex(combo->findData(id));
		if (combo->currentIndex() < 0)
			combo->setEditText(id);
		// All of the text is selected, with the cursor at its start, so that a narrow cell shows the id.
		const int length = static_cast<int>(combo->currentText().size());
		combo->lineEdit()->setSelection(length, -length);
	}

	/// A combo of choices is as wide as the label of the entity in the cell, also where the cell is narrower; it then covers
	/// the cells to its right. The view also places the widgets that facets put in cells through this function; they keep the
	/// size of their cell.
	void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
		QStyledItemDelegate::updateEditorGeometry(editor, option, index);
		auto* combo = editor == m_editor ? qobject_cast<QComboBox*>(editor) : nullptr;
		if (!combo)
			return;
		const QString id = index.data(Qt::EditRole).toString();
		const int item = combo->findData(id);
		const QString label = item < 0 ? id : combo->itemText(item);
		QStyleOptionComboBox box;
		box.initFrom(combo);
		box.editable = true;
		const QFontMetrics metrics(combo->font());
		const QSize text(metrics.horizontalAdvance(label) + metrics.averageCharWidth(), combo->height());
		const int width = combo->style()->sizeFromContents(QStyle::CT_ComboBox, &box, text, combo).width();
		combo->resize(std::max(combo->width(), width), combo->height());
	}

	/// A combo of choices gives the cell the id of the item whose label or id it shows, also in another case, or else the
	/// text as it is typed.
	void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override {
		auto* combo = qobject_cast<QComboBox*>(editor);
		if (!combo) {
			QStyledItemDelegate::setModelData(editor, model, index);
			return;
		}
		const QString text = combo->currentText().trimmed();
		int item = combo->findText(text, Qt::MatchExactly | Qt::MatchCaseSensitive);
		if (item < 0)
			item = combo->findText(text, Qt::MatchFixedString);
		if (item < 0)
			item = combo->findData(text, Qt::UserRole, Qt::MatchFixedString);
		model->setData(index, item < 0 ? text : combo->itemData(item).toString(), Qt::EditRole);
	}

	/// Forgets the open editor when the view lets it go, and destroys it as QStyledItemDelegate does.
	void destroyEditor(QWidget* editor, const QModelIndex& index) const override {
		if (editor == m_editor)
			m_editor = nullptr;
		QStyledItemDelegate::destroyEditor(editor, index);
	}

	/// Takes the text of the open editor into its cell and closes the editor, as leaving it does: the view writes the text
	/// with setModelData(), and the table tells itemChanged() when it differs. Does nothing without an open editor.
	void commitOpenEditor() {
		QWidget* editor = m_editor;
		if (!editor)
			return;
		emit commitData(editor);
		// A commit that fills the table again has closed the editor already.
		if (editor == m_editor)
			emit closeEditor(editor);
	}

private:
	// The combo of the choices of the cell in `index`, with each id as the data of its label.
	QComboBox* referenceEditor(QWidget* parent, const QModelIndex& index, const std::vector<InfrastructureFacet::Choice>& choices) const {
		auto* combo = new ReferenceComboBox(parent);
		combo->setObjectName(QStringLiteral("infrastructureReferenceEditor"));
		if (const QTableWidgetItem* header = m_panel.table()->horizontalHeaderItem(index.column()))
			combo->setAccessibleName(header->text());
		combo->setEditable(true);
		combo->setInsertPolicy(QComboBox::NoInsert);
		const QFontMetrics metrics(combo->font());
		int widest = 0;
		for (const InfrastructureFacet::Choice& choice : choices) {
			combo->addItem(choice.label, choice.id);
			widest = std::max(widest, metrics.horizontalAdvance(choice.label));
		}
		QCompleter* completer = combo->completer();
		completer->setCompletionMode(QCompleter::PopupCompletion);
		completer->setFilterMode(Qt::MatchContains);
		completer->setCaseSensitivity(Qt::CaseInsensitive);
		// The lists show their labels in full, also under a narrow column.
		const int listWidth = widest + combo->style()->pixelMetric(QStyle::PM_ScrollBarExtent) + 4 * metrics.averageCharWidth();
		combo->view()->setMinimumWidth(listWidth);
		completer->popup()->setMinimumWidth(listWidth);
		// The edit ends after the combo has handled the choice.
		auto* self = const_cast<CellDelegate*>(this);
		const auto commitChoice = [self, editor = QPointer<QWidget>(combo)] {
			if (editor && editor == self->m_editor)
				self->commitOpenEditor();
		};
		QObject::connect(combo, &QComboBox::activated, self, commitChoice, Qt::QueuedConnection);
		QObject::connect(completer, QOverload<const QModelIndex&>::of(&QCompleter::activated), self, commitChoice, Qt::QueuedConnection);
		return combo;
	}

	InfrastructurePanel& m_panel;
	mutable QPointer<QWidget> m_editor;
};

InfrastructurePanel::InfrastructurePanel(QWidget* parent) : SceneEditorPanel(parent) {
	setObjectName("infrastructurePanel");
	QVBoxLayout* layout = new QVBoxLayout(this);
	QHBoxLayout* facetRow = new QHBoxLayout();
	m_facetCombo = new QComboBox(this);
	m_facetCombo->setObjectName("infrastructureFacetCombo");
	m_facetCombo->setAccessibleName("Infrastructure type");
	for (const FacetEntry& entry : kFacets)
		m_facetCombo->addItem(QString::fromLatin1(entry.title), QString::fromLatin1(entry.key));
	facetRow->addWidget(m_facetCombo, 1);
	layout->addLayout(facetRow);

	m_blockTrackFilterCombo = new QComboBox(this);
	m_blockTrackFilterCombo->setObjectName("blockTrackFilterCombo");
	m_blockTrackFilterCombo->setAccessibleName("Block track filter");
	m_blockTrackFilterCombo->setToolTip("Track to show. Add puts new blocks on it.");
	layout->addWidget(m_blockTrackFilterCombo);

	// The filters of the rows and their count share one row, which leaves the table its height in a low dock.
	QHBoxLayout* filterRow = new QHBoxLayout();
	m_trackFilterCombo = new QComboBox(this);
	m_trackFilterCombo->setObjectName("infrastructureTrackFilterCombo");
	m_trackFilterCombo->setAccessibleName("Track filter");
	m_trackFilterCombo->setToolTip("Track whose rows to show.");
	// As wide as "All tracks" whatever the ids of the tracks, so that the panel keeps its width.
	m_trackFilterCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_trackFilterCombo->setMinimumContentsLength(10);
	filterRow->addWidget(m_trackFilterCombo);
	m_filterEdit = new QLineEdit(this);
	m_filterEdit->setObjectName("infrastructureFilterEdit");
	m_filterEdit->setAccessibleName("Filter rows");
	m_filterEdit->setPlaceholderText("Filter rows");
	m_filterEdit->setToolTip("Shows the rows that hold every word in their cells.");
	m_filterEdit->setClearButtonEnabled(true);
	filterRow->addWidget(m_filterEdit, 1);
	m_countLabel = new QLabel(this);
	m_countLabel->setObjectName("infrastructureCountLabel");
	m_countLabel->setAccessibleName("Rows shown");
	// A long count is cut short in a narrow dock rather than make the panel wider.
	m_countLabel->setMinimumWidth(1);
	m_countLabel->setVisible(false);
	filterRow->addWidget(m_countLabel);
	layout->addLayout(filterRow);

	m_guidanceLabel = new QLabel(this);
	m_guidanceLabel->setObjectName("infrastructureGuidance");
	m_guidanceLabel->setWordWrap(true);
	m_guidanceLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	layout->addWidget(m_guidanceLabel);

	// The table with its buttons, and below it the sections of a route. The user can resize the two.
	auto* splitter = new QSplitter(Qt::Vertical, this);
	splitter->setObjectName("infrastructureSplitter");
	splitter->setChildrenCollapsible(false);
	layout->addWidget(splitter, 1);

	auto* tablePane = new QWidget(splitter);
	tablePane->setObjectName("infrastructureTablePane");
	QVBoxLayout* tableLayout = new QVBoxLayout(tablePane);
	tableLayout->setContentsMargins(0, 0, 0, 0);
	m_table = new QTableWidget(tablePane);
	m_table->setObjectName("infrastructureTable");
	m_table->setAccessibleName("Infrastructure fields");
	m_table->setEditTriggers(QAbstractItemView::AllEditTriggers);
	m_cellDelegate = new CellDelegate(*this, m_table);
	m_table->setItemDelegate(m_cellDelegate);
	// Tab leaves the table and the arrow keys move between the cells.
	m_table->setTabKeyNavigation(false);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::SingleSelection);
	m_table->horizontalHeader()->setStretchLastSection(true);
	tableLayout->addWidget(m_table, 1);
	m_markLabel = new QLabel(tablePane);
	m_markLabel->setObjectName("infrastructureMarkLabel");
	m_markLabel->setAccessibleName("Marked on the canvas");
	m_markLabel->setWordWrap(true);
	m_markLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_markLabel->setVisible(false);
	tableLayout->addWidget(m_markLabel);

	QHBoxLayout* actions = new QHBoxLayout();
	m_addButton = new QPushButton("Add", tablePane);
	m_addButton->setObjectName("infrastructureAddButton");
	m_deleteButton = new QPushButton("Delete", tablePane);
	m_deleteButton->setObjectName("infrastructureDeleteButton");
	m_insertBlockButton = new QPushButton("Insert", tablePane);
	m_insertBlockButton->setObjectName("blockInsertButton");
	m_moveBlockUpButton = new QPushButton("Move Up", tablePane);
	m_moveBlockUpButton->setObjectName("blockMoveUpButton");
	m_moveBlockDownButton = new QPushButton("Move Down", tablePane);
	m_moveBlockDownButton->setObjectName("blockMoveDownButton");
	actions->addWidget(m_addButton);
	actions->addWidget(m_deleteButton);
	actions->addWidget(m_insertBlockButton);
	actions->addWidget(m_moveBlockUpButton);
	actions->addWidget(m_moveBlockDownButton);
	actions->addStretch();
	tableLayout->addLayout(actions);
	// The task actions of the chosen kind have a row of their own, so that no kind is wider than the first row makes it.
	m_taskActionRow = new QWidget(tablePane);
	m_taskActionRow->setObjectName("infrastructureTaskRow");
	auto* taskLayout = new QHBoxLayout(m_taskActionRow);
	taskLayout->setContentsMargins(0, 0, 0, 0);
	m_taskActionRow->setVisible(false);
	tableLayout->addWidget(m_taskActionRow);
	// The buttons of blocks keep their room while another kind hides them. They make the widest controls of any kind, so
	// the panel is as wide for every kind, and the dock and the canvas beside it keep their width when the kind changes.
	for (QPushButton* button : {m_insertBlockButton, m_moveBlockUpButton, m_moveBlockDownButton}) {
		QSizePolicy policy = button->sizePolicy();
		policy.setRetainSizeWhenHidden(true);
		button->setSizePolicy(policy);
	}

	m_routeSectionDetailWidget = new QWidget(splitter);
	m_routeSectionDetailWidget->setObjectName("routeSectionDetailWidget");
	QVBoxLayout* routeSectionDetailLayout = new QVBoxLayout(m_routeSectionDetailWidget);
	routeSectionDetailLayout->setContentsMargins(0, 0, 0, 0);
	QLabel* routeSectionHeading = EditorLayout::sectionHeading("Route sections", m_routeSectionDetailWidget);
	routeSectionHeading->setObjectName("routeSectionHeading");
	routeSectionDetailLayout->addWidget(routeSectionHeading);
	QHBoxLayout* routeSectionCatalogLayout = new QHBoxLayout();
	routeSectionCatalogLayout->addWidget(new QLabel("Section catalog", m_routeSectionDetailWidget));
	m_routeSectionCatalogCombo = new QComboBox(m_routeSectionDetailWidget);
	m_routeSectionCatalogCombo->setObjectName("routeSectionCatalogCombo");
	m_routeSectionCatalogCombo->setAccessibleName("Route section catalog");
	routeSectionCatalogLayout->addWidget(m_routeSectionCatalogCombo, 1);
	m_addRouteSectionButton = new QPushButton("Add Section", m_routeSectionDetailWidget);
	m_addRouteSectionButton->setObjectName("routeAddSectionButton");
	routeSectionCatalogLayout->addWidget(m_addRouteSectionButton);
	routeSectionDetailLayout->addLayout(routeSectionCatalogLayout);
	m_routeSectionListWidget = new QListWidget(m_routeSectionDetailWidget);
	m_routeSectionListWidget->setObjectName("routeSectionList");
	m_routeSectionListWidget->setAccessibleName("Ordered route sections");
	routeSectionDetailLayout->addWidget(m_routeSectionListWidget, 1);
	QHBoxLayout* routeSectionActions = new QHBoxLayout();
	m_removeRouteSectionButton = new QPushButton("Remove", m_routeSectionDetailWidget);
	m_removeRouteSectionButton->setObjectName("routeRemoveSectionButton");
	m_moveRouteSectionUpButton = new QPushButton("Move Up", m_routeSectionDetailWidget);
	m_moveRouteSectionUpButton->setObjectName("routeMoveUpButton");
	m_moveRouteSectionDownButton = new QPushButton("Move Down", m_routeSectionDetailWidget);
	m_moveRouteSectionDownButton->setObjectName("routeMoveDownButton");
	routeSectionActions->addWidget(m_removeRouteSectionButton);
	routeSectionActions->addWidget(m_moveRouteSectionUpButton);
	routeSectionActions->addWidget(m_moveRouteSectionDownButton);
	routeSectionActions->addStretch();
	routeSectionDetailLayout->addLayout(routeSectionActions);
	splitter->setStretchFactor(0, 1);
	splitter->setStretchFactor(1, 1);

	connect(m_facetCombo, &QComboBox::currentTextChanged, this, [this](const QString&) {
		m_selectionId.clear();
		refreshTable();
	});
	connect(m_blockTrackFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
		if (currentFacetHas(InfrastructureFacet::TrackFilter)) {
			m_selectionId.clear();
			refreshTable();
		}
	});
	connect(m_trackFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
		if (currentFacetHas(InfrastructureFacet::TrackRowFilter))
			filterRows();
	});
	connect(m_filterEdit, &QLineEdit::textChanged, this, &InfrastructurePanel::filterRows);
	connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
		if (item)
			commitCell(item->row(), item->column());
	});
	connect(m_table, &QTableWidget::itemSelectionChanged, this, &InfrastructurePanel::updateSelection);
	connect(m_addButton, &QPushButton::clicked, this, &InfrastructurePanel::addEntity);
	connect(m_deleteButton, &QPushButton::clicked, this, &InfrastructurePanel::deleteEntity);
	connect(m_insertBlockButton, &QPushButton::clicked, this, &InfrastructurePanel::insertEntity);
	connect(m_moveBlockUpButton, &QPushButton::clicked, this, [this] { moveEntity(-1); });
	connect(m_moveBlockDownButton, &QPushButton::clicked, this, [this] { moveEntity(1); });
	connect(m_addRouteSectionButton, &QPushButton::clicked, this, &InfrastructurePanel::addSection);
	connect(m_removeRouteSectionButton, &QPushButton::clicked, this, &InfrastructurePanel::removeSection);
	connect(m_moveRouteSectionUpButton, &QPushButton::clicked, this, [this] { moveSection(-1); });
	connect(m_moveRouteSectionDownButton, &QPushButton::clicked, this, [this] { moveSection(1); });
	connect(m_routeSectionListWidget, &QListWidget::itemSelectionChanged, this, [this] {
		refreshRouteSectionPanel();
		refreshMarkLabel();
		emit selectionChanged(facetKey(), m_selectionId);
	});
	InfrastructurePanel::refresh(false);
}

void InfrastructurePanel::setFacet(std::unique_ptr<InfrastructureFacet> facet) {
	if (!facet)
		return;
	const QString key = facet->key();
	m_facets[key] = std::move(facet);
}

InfrastructureFacet* InfrastructurePanel::facet(const QString& key) const {
	const auto found = m_facets.find(key);
	return found == m_facets.end() ? nullptr : found->second.get();
}

InfrastructureFacet* InfrastructurePanel::currentFacet() const {
	return facet(facetKey());
}

bool InfrastructurePanel::currentFacetHas(InfrastructureFacet::Capability capability) const {
	const InfrastructureFacet* current = currentFacet();
	return current && current->capabilities().testFlag(capability);
}

QStringList InfrastructurePanel::facetKeys() {
	QStringList keys;
	for (const FacetEntry& entry : kFacets)
		keys << QString::fromLatin1(entry.key);
	return keys;
}

QString InfrastructurePanel::facetKey() const {
	return m_facetCombo->currentData().toString();
}

QString InfrastructurePanel::facetTitle() const {
	return m_facetCombo->currentText();
}

const SceneModel& InfrastructurePanel::model() const {
	return m_document ? m_document->model() : emptyScene();
}


void InfrastructurePanel::showStatus(const QString& text, int timeoutMs) {
	emit statusMessage(text, timeoutMs);
}

void InfrastructurePanel::refresh() {
	refresh(false);
}

void InfrastructurePanel::refresh(bool resetSelection) {
	const bool editable = canEdit();
	m_facetCombo->setEnabled(editable);
	m_addButton->setEnabled(editable);
	refreshTable(resetSelection);
}

void InfrastructurePanel::commitPendingEditors() {
	// The window answers changed() by taking the text of every panel, also while the panel tells an edit of its own. The
	// table then still shows the rows from before the edit, and the cell of the open editor can stand for another entity.
	if (!m_editing)
		m_cellDelegate->commitOpenEditor();
	if (InfrastructureFacet* current = currentFacet())
		current->commitPendingEdits(*this);
}

void InfrastructurePanel::refreshTable(bool resetSelection) {
	InfrastructureFacet* current = currentFacet();
	const QString key = facetKey();
	const int previousRow = resetSelection ? -1 : m_table->currentRow();
	const int previousColumn = resetSelection ? -1 : m_table->currentColumn();
	const QString previousId = resetSelection ? QString() : m_selectionId;
	const InfrastructureFacet::Capabilities capabilities = current ? current->capabilities() : InfrastructureFacet::Capabilities();
	if (capabilities.testFlag(InfrastructureFacet::TrackFilter))
		refreshBlockTrackFilter();
	if (capabilities.testFlag(InfrastructureFacet::TrackRowFilter))
		refreshTrackFilter();
	m_blockRowModelIndices.clear();
	const QStringList headers = current ? current->columns() : QStringList();

	const QSignalBlocker blocker(m_table);
	// Clearing the table hides a field in a cell that has the focus, and the focus goes to the table. The table would
	// then open an editor on its first cell and lose track of it, so no editor opens while the rows are filled again.
	// Restoring the current cell below would open one too.
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_table->clearContents();
	m_table->setColumnCount(static_cast<int>(headers.size()));
	m_table->setHorizontalHeaderLabels(headers);
	if (current)
		current->fillRows(*this);
	else
		m_table->setRowCount(0);
	// The table shows two rows also in a low dock, which then scrolls the panel instead.
	// A row that a filter hid before the refresh has no height, so the default height is the least.
	const int rowHeight = std::max(m_table->rowCount() > 0 ? m_table->rowHeight(0) : 0, m_table->verticalHeader()->defaultSectionSize());
	m_table->setMinimumHeight(m_table->horizontalHeader()->sizeHint().height() + 2 * rowHeight + 2 * m_table->frameWidth());
	const bool editable = canEdit();
	m_table->setEnabled(editable);

	const bool empty = m_table->rowCount() == 0;
	InfrastructureGuidanceState state;
	state.loaded = hasScene();
	state.editable = editable;
	if (state.loaded) {
		state.tracks = static_cast<int>(model().tracks.size());
		state.nodes = static_cast<int>(model().nodes.size());
		state.stations = static_cast<int>(model().stations.size());
	}
	const QString guidance = infrastructureGuidanceText(key, empty, state);
	m_guidanceLabel->setText(guidance);
	m_guidanceLabel->setVisible(!guidance.isEmpty());

	// The table measures only the rows it shows, so the rows that the filters hid are shown while the columns are sized,
	// and a filter does not narrow them.
	for (int row = 0; row < m_table->rowCount(); ++row)
		m_table->setRowHidden(row, false);
	m_table->resizeColumnsToContents();
	applyFilters();
	int rowToSelect = -1;
	if (current) {
		for (int row = 0; row < m_table->rowCount(); ++row) {
			if (current->selectionId(*m_table, row) == previousId) {
				rowToSelect = row;
				break;
			}
		}
	}
	// An entity that the filters hide is not selected, unless the row of a canvas item is being selected: then the filters
	// that hide it are cleared.
	const bool selectionHidden = rowToSelect >= 0 && m_table->isRowHidden(rowToSelect);
	if (selectionHidden && m_revealSelection)
		revealRow(rowToSelect);
	else if (selectionHidden)
		rowToSelect = -1;
	// The entity that is found again in its row keeps the column that was current, if the facet still has it. A new or
	// moved row starts at the first column.
	const bool sameRow = !previousId.isEmpty() && rowToSelect >= 0 && rowToSelect == previousRow;
	const int columnToSelect = sameRow && previousColumn >= 0 && previousColumn < m_table->columnCount() ? previousColumn : 0;
	if (!resetSelection && !selectionHidden && rowToSelect < 0 && previousRow >= 0 && m_table->rowCount() > 0) {
		// The shown row at the same position, or else the one before it.
		const int position = std::min(previousRow, m_table->rowCount() - 1);
		for (int row = position; row < m_table->rowCount() && rowToSelect < 0; ++row)
			rowToSelect = m_table->isRowHidden(row) ? -1 : row;
		for (int row = position - 1; row >= 0 && rowToSelect < 0; --row)
			rowToSelect = m_table->isRowHidden(row) ? -1 : row;
	}
	m_table->setCurrentCell(rowToSelect, rowToSelect < 0 ? -1 : columnToSelect);
	m_table->setEditTriggers(QAbstractItemView::AllEditTriggers);
	updateSelection();

	const bool trackFilter = capabilities.testFlag(InfrastructureFacet::TrackFilter);
	const bool insertAndMove = capabilities.testFlag(InfrastructureFacet::InsertAndMove);
	m_blockTrackFilterCombo->setVisible(trackFilter);
	m_trackFilterCombo->setVisible(capabilities.testFlag(InfrastructureFacet::TrackRowFilter));
	m_trackFilterCombo->setEnabled(editable);
	m_filterEdit->setEnabled(editable);
	m_insertBlockButton->setVisible(insertAndMove);
	m_moveBlockUpButton->setVisible(insertAndMove);
	m_moveBlockDownButton->setVisible(insertAndMove);
	bool canAdd = editable;
	if (trackFilter) {
		const std::string selectedTrack = m_blockTrackFilterCombo->currentData().toString().toStdString();
		const std::vector<SceneTrack>& tracks = model().tracks;
		canAdd = canAdd && !selectedTrack.empty()
			&& std::any_of(tracks.begin(), tracks.end(),
				[&selectedTrack](const SceneTrack& track) { return track.id == selectedTrack; });
	}
	m_addButton->setEnabled(canAdd);
	refreshTaskButtons();
}

void InfrastructurePanel::refreshTaskButtons() {
	// The facets are set after the panel is built, so the buttons are made here, when the kind has changed.
	const InfrastructureFacet* current = currentFacet();
	if (current != m_taskFacet) {
		auto* layout = qobject_cast<QHBoxLayout*>(m_taskActionRow->layout());
		for (QPushButton* button : m_taskButtons)
			delete button;
		m_taskButtons.clear();
		while (layout->count() > 0)
			delete layout->takeAt(0);
		if (current) {
			const std::vector<InfrastructureFacet::TaskAction> actions = current->taskActions();
			for (int index = 0; index < static_cast<int>(actions.size()); ++index) {
				const InfrastructureFacet::TaskAction& action = actions[static_cast<std::size_t>(index)];
				auto* button = new QPushButton(action.text, m_taskActionRow);
				button->setObjectName(action.objectName);
				button->setToolTip(action.toolTip);
				connect(button, &QPushButton::clicked, this, [this, index] { runTaskAction(index); });
				layout->addWidget(button);
				m_taskButtons.push_back(button);
			}
			layout->addStretch();
		}
		m_taskFacet = current;
	}
	m_taskActionRow->setVisible(!m_taskButtons.empty());
	const bool enabled = canEdit() && m_table->currentRow() >= 0;
	for (QPushButton* button : m_taskButtons)
		button->setEnabled(enabled);
}

void InfrastructurePanel::updateSelection() {
	const int row = m_table->currentRow();
	const QString key = facetKey();
	const InfrastructureFacet* current = currentFacet();
	m_selectionId = row >= 0 && current ? current->selectionId(*m_table, row) : QString();
	const bool hasRow = canEdit() && row >= 0;
	m_deleteButton->setEnabled(hasRow);
	for (QPushButton* button : m_taskButtons)
		button->setEnabled(hasRow);
	const bool insertAndMove = currentFacetHas(InfrastructureFacet::InsertAndMove);
	m_insertBlockButton->setEnabled(hasRow && insertAndMove);
	QString deleteHint;
	if (!hasScene())
		deleteHint = QStringLiteral("Open a case to delete infrastructure.");
	else if (!canEdit())
		deleteHint = QStringLiteral("Stop the run to edit.");
	else if (row < 0)
		deleteHint = QStringLiteral("Select a row to delete.");
	else
		deleteHint = QStringLiteral("Delete the selected row.");
	m_deleteButton->setToolTip(deleteHint);
	refreshMoveButtons();
	refreshRouteSectionPanel();
	refreshMarkLabel();
	emit selectionChanged(key, m_selectionId);
}

bool InfrastructurePanel::selectCanvasItem(const QString& kind, const QString& id, const QString& platform) {
	return selectRow(
		[&](const SceneModel& scene) { return canvasRow(scene, kind, id, platform); });
}

bool InfrastructurePanel::selectEntity(const QString& kind, const QString& id) {
	return selectRow([&](const SceneModel& scene) {
		const std::string key = id.toStdString();
		// A diagnostic can name a signalling area with a space.
		const QString name = QString(kind).replace(QLatin1Char(' '), QLatin1Char('_'));
		if (name == QLatin1String("block")) {
			const SceneBlock* block = withId(scene.blocks, key);
			return block ? RowTarget{QStringLiteral("blocks"), id, QString::fromStdString(block->trackId)} : RowTarget();
		}
		if (name == QLatin1String("route") && withId(scene.routes, key))
			return RowTarget{QStringLiteral("routes"), id, {}};
		if (name == QLatin1String("signal") && withId(sceneSignals(scene), key))
			return RowTarget{QStringLiteral("signals"), id, {}};
		if (name == QLatin1String("signalling_area") && withId(scene.signallingAreas, key))
			return RowTarget{QStringLiteral("signalling_areas"), id, {}};
		if (name == QLatin1String("signal") || name == QLatin1String("block") || name == QLatin1String("route")
			|| name == QLatin1String("signalling_area"))
			return RowTarget();
		return canvasRow(scene, kind, id, QString());
	});
}

bool InfrastructurePanel::selectRow(const std::function<RowTarget(const SceneModel&)>& find) {
	if (!canEdit() || find(model()).facet.isEmpty())
		return false;
	commitPendingEditors();
	// Taking the cells is an edit when their text changed, and the edit can rename or remove the entity.
	const RowTarget target = find(model());
	InfrastructureFacet* targetFacet = facet(target.facet);
	if (!canEdit() || !targetFacet)
		return false;
	bool listed = facetKey() == target.facet;
	if (!listed) {
		const QSignalBlocker blocker(m_facetCombo);
		m_facetCombo->setCurrentIndex(m_facetCombo->findData(target.facet));
	}
	if (targetFacet->capabilities().testFlag(InfrastructureFacet::TrackFilter)) {
		// The filter shows the track of the block, so that the table lists its row.
		refreshBlockTrackFilter();
		const int track = m_blockTrackFilterCombo->findData(target.track);
		listed = listed && track == m_blockTrackFilterCombo->currentIndex();
		const QSignalBlocker blocker(m_blockTrackFilterCombo);
		m_blockTrackFilterCombo->setCurrentIndex(track);
	}
	if (listed) {
		// Choosing a row of the table tells selectionChanged, as a click on the row does, and opens no editor on it.
		for (int row = 0; row < m_table->rowCount(); ++row) {
			if (targetFacet->selectionId(*m_table, row) != target.id)
				continue;
			revealRow(row);
			m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
			m_table->setCurrentCell(row, std::max(m_table->currentColumn(), 0));
			m_table->setEditTriggers(QAbstractItemView::AllEditTriggers);
			break;
		}
	} else {
		// Filling the table again finds the row of the selection, clears the filters that hide it, and tells
		// selectionChanged once.
		m_selectionId = target.id;
		const QScopedValueRollback<bool> reveal(m_revealSelection, true);
		refreshTable();
	}
	const int row = m_table->currentRow();
	if (row < 0 || m_selectionId != target.id)
		return false;
	m_table->scrollTo(m_table->model()->index(row, 0));
	return true;
}

int InfrastructurePanel::selectedRouteSection() const {
	if (!currentFacetHas(InfrastructureFacet::RouteSections) || m_table->currentRow() < 0)
		return -1;
	return m_routeSectionListWidget->currentRow();
}

void InfrastructurePanel::setMarkShown(bool shown) {
	if (shown == m_markShown)
		return;
	m_markShown = shown;
	refreshMarkLabel();
}

void InfrastructurePanel::refreshMarkLabel() {
	const QString text = canEdit() && m_markShown ? markText(model(), facetKey(), m_selectionId) : QString();
	m_markLabel->setText(text);
	m_markLabel->setVisible(!text.isEmpty());
}

void InfrastructurePanel::refreshTrackFilter() {
	const QString previousTrack = m_trackFilterCombo->currentData().toString();
	const QSignalBlocker blocker(m_trackFilterCombo);
	m_trackFilterCombo->clear();
	m_trackFilterCombo->addItem(QStringLiteral("All tracks"), QString());
	for (const SceneTrack& track : model().tracks)
		m_trackFilterCombo->addItem(QString::fromStdString(track.id), QString::fromStdString(track.id));
	m_trackFilterCombo->setCurrentIndex(std::max(m_trackFilterCombo->findData(previousTrack), 0));
}

void InfrastructurePanel::refreshMoveButtons() {
	if (!currentFacetHas(InfrastructureFacet::InsertAndMove))
		return;
	const int row = m_table->currentRow();
	const int rows = static_cast<int>(m_blockRowModelIndices.size());
	const bool movable = canEdit() && row >= 0 && row < rows && !m_rowsHidden;
	m_moveBlockUpButton->setEnabled(movable && row > 0);
	m_moveBlockDownButton->setEnabled(movable && row + 1 < rows);
	const QString hint = m_rowsHidden ? QStringLiteral("Clear the filter to move a row.") : QString();
	m_moveBlockUpButton->setToolTip(hint);
	m_moveBlockDownButton->setToolTip(hint);
}

bool InfrastructurePanel::matchesText(int row) const {
	const QString text = m_filterEdit->text();
	if (text.trimmed().isEmpty())
		return true;
	QStringList cells;
	for (int column = 0; column < m_table->columnCount(); ++column) {
		if (const QTableWidgetItem* item = m_table->item(row, column))
			cells << item->text();
		const QWidget* widget = m_table->cellWidget(row, column);
		if (const auto* combo = qobject_cast<const QComboBox*>(widget))
			cells << combo->currentText();
		else if (const auto* field = qobject_cast<const QAbstractSpinBox*>(widget))
			cells << field->text();
	}
	return FindDialog::matches(text, cells);
}

bool InfrastructurePanel::matchesTrack(int row) const {
	const QString track = m_trackFilterCombo->currentData().toString();
	const InfrastructureFacet* current = currentFacet();
	return track.isEmpty() || !current || !current->capabilities().testFlag(InfrastructureFacet::TrackRowFilter)
		|| current->rowOnTrack(*this, row, track.toStdString());
}

void InfrastructurePanel::applyFilters() {
	const int rows = m_table->rowCount();
	int shown = 0;
	for (int row = 0; row < rows; ++row) {
		const bool show = matchesTrack(row) && matchesText(row);
		m_table->setRowHidden(row, !show);
		shown += show ? 1 : 0;
	}
	m_rowsHidden = shown < rows;
	const FacetEntry* entry = facetEntry(facetKey());
	QString count;
	if (entry && currentFacet() && rows > 0) {
		count = countText(rows, QString::fromLatin1(entry->noun).toLower(), QString::fromLatin1(entry->title).toLower());
		if (m_rowsHidden)
			count = QStringLiteral("%1 of %2").arg(shown).arg(count);
	}
	m_countLabel->setText(count);
	m_countLabel->setVisible(!count.isEmpty());
	refreshMoveButtons();
}

void InfrastructurePanel::filterRows() {
	applyFilters();
	const int row = m_table->currentRow();
	if (row >= 0 && m_table->isRowHidden(row))
		m_table->setCurrentCell(-1, -1);
}

void InfrastructurePanel::revealRow(int row) {
	// Each change of a filter applies the filters again.
	if (!matchesText(row))
		m_filterEdit->clear();
	if (!matchesTrack(row))
		m_trackFilterCombo->setCurrentIndex(0);
}

void InfrastructurePanel::clearFilters() {
	m_filterEdit->clear();
	if (currentFacetHas(InfrastructureFacet::TrackRowFilter))
		m_trackFilterCombo->setCurrentIndex(0);
}

void InfrastructurePanel::refreshBlockTrackFilter() {
	const QString previousTrack = m_blockTrackFilterCombo->currentData().toString();
	const QSignalBlocker blocker(m_blockTrackFilterCombo);
	m_blockTrackFilterCombo->clear();
	const SceneModel& scene = model();
	std::set<std::string> knownTracks;
	for (const SceneTrack& track : scene.tracks) {
		m_blockTrackFilterCombo->addItem(QString::fromStdString(track.id), QString::fromStdString(track.id));
		knownTracks.insert(track.id);
	}
	std::set<std::string> invalidTracks;
	for (const SceneBlock& block : scene.blocks)
		if (knownTracks.count(block.trackId) == 0)
			invalidTracks.insert(block.trackId);
	for (const std::string& trackId : invalidTracks) {
		const QString value = QString::fromStdString(trackId);
		m_blockTrackFilterCombo->addItem(trackId.empty()
				? QStringLiteral("(none)")
				: QStringLiteral("%1 (missing)").arg(value),
			value);
	}
	int selected = m_blockTrackFilterCombo->findData(previousTrack);
	if (selected < 0 && m_blockTrackFilterCombo->count() > 0)
		selected = 0;
	m_blockTrackFilterCombo->setCurrentIndex(selected);
	m_blockTrackFilterCombo->setVisible(currentFacetHas(InfrastructureFacet::TrackFilter));
	m_blockTrackFilterCombo->setEnabled(canEdit() && m_blockTrackFilterCombo->count() > 0);
}

void InfrastructurePanel::refreshRouteSectionPanel() {
	if (!currentFacetHas(InfrastructureFacet::RouteSections)) {
		m_routeSectionDetailWidget->setVisible(false);
		return;
	}
	m_routeSectionDetailWidget->setVisible(true);
	const SceneModel& scene = model();
	const bool editable = canEdit();
	const int routeRow = m_table->currentRow();
	const bool hasRoute = routeRow >= 0 && routeRow < static_cast<int>(scene.routes.size());
	const SceneSectionInventory inventory = buildSceneSectionInventory(scene);
	const SceneSectionLabels labels = sceneSectionLabels(inventory);
	const QString previousCatalog = m_routeSectionCatalogCombo->currentData().toString();
	const int previousListRow = m_routeSectionListWidget->currentRow();
	{
		const QSignalBlocker blocker(m_routeSectionCatalogCombo);
		m_routeSectionCatalogCombo->clear();
		for (const SceneSectionDescriptor& section : inventory.sections) {
			m_routeSectionCatalogCombo->addItem(sectionLabel(labels, section), QString::fromStdString(section.id));
			m_routeSectionCatalogCombo->setItemData(m_routeSectionCatalogCombo->count() - 1, QString::fromStdString(section.id),
				Qt::ToolTipRole);
		}
		int selected = m_routeSectionCatalogCombo->findData(previousCatalog);
		if (selected < 0 && m_routeSectionCatalogCombo->count() > 0)
			selected = 0;
		m_routeSectionCatalogCombo->setCurrentIndex(selected);
		m_routeSectionCatalogCombo->setEnabled(editable && hasRoute && selected >= 0);
	}
	{
		const QSignalBlocker blocker(m_routeSectionListWidget);
		m_routeSectionListWidget->clear();
		if (hasRoute) {
			const SceneRoute& route = scene.routes[static_cast<std::size_t>(routeRow)];
			for (const std::string& raw : route.blocks) {
				auto* item = new QListWidgetItem(m_routeSectionListWidget);
				item->setData(Qt::UserRole, QString::fromStdString(raw));
				item->setToolTip(QString::fromStdString(raw));
				if (const SceneSectionDescriptor* exact = inventory.exact(raw)) {
					item->setText(sectionLabel(labels, *exact));
				} else if (const SceneSectionDescriptor* resolved = inventory.resolve(raw)) {
					item->setText(QStringLiteral("%1 (legacy: %2)").arg(sectionLabel(labels, *resolved), QString::fromStdString(raw)));
				} else {
					item->setText(QStringLiteral("%1 (missing)").arg(QString::fromStdString(raw)));
				}
			}
		}
		restoreListSelection(m_routeSectionListWidget, QString(), previousListRow);
	}
	m_routeSectionListWidget->setEnabled(editable && hasRoute);
	const int selectedRow = m_routeSectionListWidget->currentRow();
	m_addRouteSectionButton->setEnabled(editable && hasRoute && m_routeSectionCatalogCombo->currentIndex() >= 0);
	m_removeRouteSectionButton->setEnabled(editable && hasRoute && selectedRow >= 0);
	m_moveRouteSectionUpButton->setEnabled(editable && hasRoute && selectedRow > 0);
	m_moveRouteSectionDownButton->setEnabled(editable && hasRoute && selectedRow >= 0
		&& selectedRow + 1 < m_routeSectionListWidget->count());
}

void InfrastructurePanel::commitCell(int row, int column) {
	if (!canEdit() || row < 0 || row >= m_table->rowCount() || !m_table->item(row, column))
		return;
	if (InfrastructureFacet* current = currentFacet())
		current->commitCell(*this, row, column);
}

void InfrastructurePanel::addEntity() {
	if (!canEdit())
		return;
	if (InfrastructureFacet* current = currentFacet()) {
		clearFilters();
		current->add(*this);
	}
}

void InfrastructurePanel::deleteEntity() {
	if (!canEdit() || m_table->currentRow() < 0)
		return;
	if (InfrastructureFacet* current = currentFacet())
		current->remove(*this);
}

void InfrastructurePanel::runTaskAction(int index) {
	commitPendingEditors();
	if (!canEdit() || m_table->currentRow() < 0 || index < 0 || index >= static_cast<int>(m_taskButtons.size()))
		return;
	if (InfrastructureFacet* current = currentFacet())
		current->runTaskAction(*this, index);
}

void InfrastructurePanel::insertEntity() {
	if (canEdit() && currentFacetHas(InfrastructureFacet::InsertAndMove)) {
		clearFilters();
		currentFacet()->insert(*this);
	}
}

void InfrastructurePanel::moveEntity(int offset) {
	if (canEdit() && currentFacetHas(InfrastructureFacet::InsertAndMove) && !m_rowsHidden)
		currentFacet()->move(*this, offset);
}

void InfrastructurePanel::addSection() {
	if (canEdit() && currentFacetHas(InfrastructureFacet::RouteSections))
		currentFacet()->addSection(*this);
}

void InfrastructurePanel::removeSection() {
	if (canEdit() && currentFacetHas(InfrastructureFacet::RouteSections))
		currentFacet()->removeSection(*this);
}

void InfrastructurePanel::moveSection(int offset) {
	if (canEdit() && currentFacetHas(InfrastructureFacet::RouteSections))
		currentFacet()->moveSection(*this, offset);
}

bool InfrastructurePanel::edit(const QString& label, const std::function<void(SceneModel&)>& apply) {
	if (!editInPlace(label, apply))
		return false;
	refreshTable();
	return true;
}

bool InfrastructurePanel::editInPlace(const QString& label, const std::function<void(SceneModel&)>& apply) {
	const QScopedValueRollback<bool> editing(m_editing, true);
	if (!m_document || !m_document->edit(SceneDocument::Infrastructure, label, apply))
		return false;
	emit changed();
	return true;
}

void InfrastructurePanel::refuseEdit(const QString& message) {
	if (!message.isEmpty())
		showStatus(message);
	refreshTable();
}

bool InfrastructurePanel::deleteRow(const QString& id, const std::vector<std::string>& consumers, const QString& label,
	const std::function<void(SceneModel&)>& apply) {
	const FacetEntry* entry = facetEntry(facetKey());
	const QString noun = entry ? QString::fromLatin1(entry->noun) : QStringLiteral("Row");
	const QString titleNoun = entry ? QString::fromLatin1(entry->titleNoun) : QStringLiteral("Row");
	if (!consumers.empty()) {
		// A box that lists every consumer of a track or a node can be taller than the screen.
		constexpr std::size_t kMaxListedConsumers = 10;
		QStringList lines;
		for (std::size_t index = 0; index < consumers.size() && index < kMaxListedConsumers; ++index)
			lines << QString::fromStdString(consumers[index]);
		QString list = lines.join("\n• ");
		if (consumers.size() > kMaxListedConsumers)
			list += QString("\nand %1 more").arg(static_cast<int>(consumers.size() - kMaxListedConsumers));
		m_prompts->showError(this, QStringLiteral("Cannot Delete %1").arg(titleNoun),
			QString("%1 '%2' is used by:\n• %3\nRemove or change these first.").arg(noun, id, list), true);
		return false;
	}
	setSelectionId(QString());
	if (!edit(label, apply))
		return false;
	reportDeleted(id.isEmpty() ? QStringLiteral("an incomplete %1").arg(noun.toLower())
							   : QStringLiteral("%1 '%2'").arg(noun.toLower(), id));
	return true;
}

void InfrastructurePanel::populateSectionCombo(QComboBox* combo, const SceneSectionInventory& inventory,
	const std::string& current, bool allowNone, const QString& noneLabel) {
	if (!combo)
		return;
	combo->clear();
	if (allowNone)
		combo->addItem(noneLabel, QString());
	const SceneSectionLabels labels = sceneSectionLabels(inventory);
	for (const SceneSectionDescriptor& section : inventory.sections) {
		combo->addItem(sectionLabel(labels, section), QString::fromStdString(section.id));
		combo->setItemData(combo->count() - 1, QString::fromStdString(section.id), Qt::ToolTipRole);
	}

	int selected = combo->findData(QString::fromStdString(current));
	if (selected < 0 && !current.empty()) {
		if (const SceneSectionDescriptor* resolved = inventory.resolve(current)) {
			combo->addItem(QStringLiteral("%1 (legacy: %2)").arg(sectionLabel(labels, *resolved), QString::fromStdString(current)),
				QString::fromStdString(current));
		} else {
			combo->addItem(QStringLiteral("%1 (missing)").arg(QString::fromStdString(current)), QString::fromStdString(current));
		}
		selected = combo->count() - 1;
	} else if (selected < 0 && current.empty() && !allowNone) {
		combo->addItem(QStringLiteral("(none)"), QString());
		selected = combo->count() - 1;
	}
	combo->setCurrentIndex(selected >= 0 ? selected : (combo->count() > 0 ? 0 : -1));
}

QString InfrastructurePanel::sectionLabel(const SceneSectionLabels& labels, const SceneSectionDescriptor& section) {
	return QString::fromStdString(labels.label(section.id));
}
