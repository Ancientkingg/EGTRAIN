#include "editors/InfrastructurePanel.h"

#include "editors/infrastructure/InfrastructureGuidance.h"
#include "scene/SectionInventory.h"
#include "widgets/EditorLayout.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QVBoxLayout>

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

} // namespace

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
	// Tab leaves the table and the arrow keys move between the cells.
	m_table->setTabKeyNavigation(false);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::SingleSelection);
	m_table->horizontalHeader()->setStretchLastSection(true);
	tableLayout->addWidget(m_table, 1);

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
		if (facetKey() == QStringLiteral("blocks")) {
			m_selectionId.clear();
			refreshTable();
		}
	});
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
	connect(m_routeSectionListWidget, &QListWidget::itemSelectionChanged, this, [this] { refreshRouteSectionPanel(); });
	InfrastructurePanel::refresh(false);
}

void InfrastructurePanel::setDocument(SceneDocument* document) {
	m_document = document;
	refresh(false);
}

void InfrastructurePanel::setPrompts(UserPrompts* prompts) {
	m_prompts = prompts ? prompts : &m_ownPrompts;
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

SceneModel& InfrastructurePanel::mutableModel() {
	return m_document->mutableModel();
}

bool InfrastructurePanel::hasScene() const {
	return m_document && m_document->isLoaded();
}

bool InfrastructurePanel::canEdit() const {
	return hasScene() && m_document->isEditable();
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
	if (InfrastructureFacet* current = currentFacet())
		current->commitPendingEdits(*this);
}

void InfrastructurePanel::refreshTable(bool resetSelection) {
	InfrastructureFacet* current = currentFacet();
	const QString key = facetKey();
	const int previousRow = resetSelection ? -1 : m_table->currentRow();
	const int previousColumn = resetSelection ? -1 : m_table->currentColumn();
	const QString previousId = resetSelection ? QString() : m_selectionId;
	if (key == QStringLiteral("blocks"))
		refreshBlockTrackFilter();
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

	m_table->resizeColumnsToContents();
	int rowToSelect = -1;
	if (current) {
		for (int row = 0; row < m_table->rowCount(); ++row) {
			if (current->selectionId(*m_table, row) == previousId) {
				rowToSelect = row;
				break;
			}
		}
	}
	// The entity that is found again in its row keeps the column that was current, if the facet still has it. A new or
	// moved row starts at the first column.
	const bool sameRow = !previousId.isEmpty() && rowToSelect >= 0 && rowToSelect == previousRow;
	const int columnToSelect = sameRow && previousColumn >= 0 && previousColumn < m_table->columnCount() ? previousColumn : 0;
	if (!resetSelection && rowToSelect < 0 && previousRow >= 0 && m_table->rowCount() > 0)
		rowToSelect = std::min(previousRow, m_table->rowCount() - 1);
	m_table->setCurrentCell(rowToSelect, rowToSelect < 0 ? -1 : columnToSelect);
	m_table->setEditTriggers(QAbstractItemView::AllEditTriggers);
	updateSelection();

	const bool isBlocks = key == QStringLiteral("blocks");
	m_blockTrackFilterCombo->setVisible(isBlocks);
	m_insertBlockButton->setVisible(isBlocks);
	m_moveBlockUpButton->setVisible(isBlocks);
	m_moveBlockDownButton->setVisible(isBlocks);
	bool canAdd = editable;
	if (isBlocks) {
		const std::string selectedTrack = m_blockTrackFilterCombo->currentData().toString().toStdString();
		const std::vector<SceneTrack>& tracks = model().tracks;
		canAdd = canAdd && !selectedTrack.empty()
			&& std::any_of(tracks.begin(), tracks.end(),
				[&selectedTrack](const SceneTrack& track) { return track.id == selectedTrack; });
	}
	m_addButton->setEnabled(canAdd);
}

void InfrastructurePanel::updateSelection() {
	const int row = m_table->currentRow();
	const QString key = facetKey();
	const InfrastructureFacet* current = currentFacet();
	m_selectionId = row >= 0 && current ? current->selectionId(*m_table, row) : QString();
	emit selectionChanged(key, m_selectionId);
	const bool hasRow = canEdit() && row >= 0;
	m_deleteButton->setEnabled(hasRow);
	m_insertBlockButton->setEnabled(hasRow && key == QStringLiteral("blocks"));
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
	if (key == QStringLiteral("blocks")) {
		const int blockRows = static_cast<int>(m_blockRowModelIndices.size());
		const bool editable = canEdit() && row >= 0 && row < blockRows;
		m_moveBlockUpButton->setEnabled(editable && row > 0);
		m_moveBlockDownButton->setEnabled(editable && row + 1 < blockRows);
	}
	refreshRouteSectionPanel();
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
	m_blockTrackFilterCombo->setVisible(facetKey() == QStringLiteral("blocks"));
	m_blockTrackFilterCombo->setEnabled(canEdit() && m_blockTrackFilterCombo->count() > 0);
}

void InfrastructurePanel::refreshRouteSectionPanel() {
	if (facetKey() != QStringLiteral("routes")) {
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
		const int row = m_routeSectionListWidget->count() == 0
			? -1
			: std::min(std::max(previousListRow, 0), m_routeSectionListWidget->count() - 1);
		m_routeSectionListWidget->setCurrentRow(row);
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
	if (InfrastructureFacet* current = currentFacet())
		current->add(*this);
}

void InfrastructurePanel::deleteEntity() {
	if (!canEdit() || m_table->currentRow() < 0)
		return;
	if (InfrastructureFacet* current = currentFacet())
		current->remove(*this);
}

void InfrastructurePanel::insertEntity() {
	if (!canEdit())
		return;
	if (InfrastructureFacet* current = currentFacet())
		current->insert(*this);
}

void InfrastructurePanel::moveEntity(int offset) {
	if (!canEdit())
		return;
	if (InfrastructureFacet* current = currentFacet())
		current->move(*this, offset);
}

void InfrastructurePanel::addSection() {
	if (!canEdit())
		return;
	if (InfrastructureFacet* current = currentFacet())
		current->addSection(*this);
}

void InfrastructurePanel::removeSection() {
	if (!canEdit())
		return;
	if (InfrastructureFacet* current = currentFacet())
		current->removeSection(*this);
}

void InfrastructurePanel::moveSection(int offset) {
	if (!canEdit())
		return;
	if (InfrastructureFacet* current = currentFacet())
		current->moveSection(*this, offset);
}

void InfrastructurePanel::entityChanged(const QString& label) {
	entityEdited(label);
	emit entitiesChanged(facetKey());
	refreshTable();
}

void InfrastructurePanel::entityEdited(const QString& label) {
	m_document->markChanged(SceneDocument::Infrastructure, label);
	emit changed();
}

void InfrastructurePanel::refuseEdit(const QString& message) {
	if (!message.isEmpty())
		showStatus(message);
	refreshTable();
}

bool InfrastructurePanel::confirmDelete(const QString& id, const std::vector<std::string>& consumers) {
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
	return m_prompts->confirm(this, QStringLiteral("Delete %1").arg(titleNoun),
		QString("Delete %1 '%2'?").arg(noun.toLower(), id));
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
