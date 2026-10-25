#include "editors/TrainUnitPanel.h"

#include "editors/EditorHelpers.h"
#include "scene/SceneFields.h"
#include "scene/SceneIds.h"
#include "scene/SceneReferences.h"
#include "widgets/CompactDoubleSpinBox.h"
#include "widgets/DialogLayout.h"
#include "widgets/DisplayPath.h"
#include "widgets/EditorLayout.h"

#include <QApplication>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

namespace {

static_assert(static_cast<std::size_t>(TrainUnitPanel::PhysicalFieldCount) == kSceneTrainPhysicalFields.size(),
	"one range for each physical field");

// Edits a number of the traction table in a CompactDoubleSpinBox with the range of its column. A cell shows its number as
// such a field shows it without the focus.
class TractionCellDelegate : public QStyledItemDelegate {
public:
	using QStyledItemDelegate::QStyledItemDelegate;

	// The number that the editor of a cell in the column holds for a value: the value rounded to the decimals of the editor,
	// as QDoubleSpinBox rounds it, then put at the nearest limit of the range. A cell that holds this number gets the same
	// number back from an editor that closes unchanged.
	static double editorValue(int column, double value) {
		const SceneValueRange& range = kSceneTractionColumns[static_cast<std::size_t>(column)].range;
		return std::clamp(QString::number(value, 'f', kDecimals).toDouble(), range.min, range.max);
	}

	QString displayText(const QVariant& value, const QLocale&) const override {
		return CompactDoubleSpinBox::compactText(value.toDouble());
	}

	QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex& index) const override {
		auto* edit = new CompactDoubleSpinBox(parent);
		const SceneValueRange& range = kSceneTractionColumns[static_cast<std::size_t>(index.column())].range;
		edit->setRange(range.min, range.max);
		edit->setDecimals(kDecimals);
		edit->setSingleStep(0.1);
		// The field shows its full text once it has the focus. Selected then, the number gives way to what is typed.
		const auto selectWhenFocused = [edit](QWidget*, QWidget* now) {
			if (now == edit)
				edit->selectAll();
		};
		connect(qApp, &QApplication::focusChanged, edit, selectWhenFocused, Qt::SingleShotConnection);
		return edit;
	}

	// An open editor is at least as wide as it asks to be, so that a narrow column does not hide its number, and stays in
	// the table. It covers the cells beside it.
	void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex&) const override {
		QRect rect = option.rect;
		rect.setWidth(std::max(rect.width(), editor->sizeHint().width()));
		const int overflow = rect.right() - (editor->parentWidget()->width() - 1);
		if (overflow > 0)
			rect.translate(-std::min(overflow, std::max(rect.left(), 0)), 0);
		editor->setGeometry(rect);
	}

private:
	static constexpr int kDecimals = std::numeric_limits<double>::max_digits10;
};

// The path as the person reads it on this system, with a zero-width space after each separator and underscore so
// that a label can wrap it. The characters are not drawn.
QString breakablePath(const QString& path) {
	QString shown = EditorLayout::breakableName(display_path::format(path));
	shown.replace(display_path::separator(), QString(display_path::separator()) + QChar(0x200B));
	return shown;
}

} // namespace

TrainUnitPanel::TrainUnitPanel(QWidget* parent) : SceneEditorPanel(parent) {
	setObjectName("trainUnitPanel");
	QVBoxLayout* layout = new QVBoxLayout(this);

	// top: the list of units and the buttons that manage it, in one row. The title of the dock names the list, which
	// shows about three rows, so that the fields below keep the height of a short dock.
	QWidget* listPane = new QWidget(this);
	QVBoxLayout* listLayout = new QVBoxLayout(listPane);
	listLayout->setContentsMargins(0, 0, 0, 0);
	m_listWidget = new QListWidget(listPane);
	m_listWidget->setObjectName("trainUnitList");
	m_listWidget->setAccessibleName("Rolling stock units");
	m_listWidget->setFixedHeight(m_listWidget->fontMetrics().lineSpacing() * 3 + 2 * m_listWidget->frameWidth());
	listLayout->addWidget(m_listWidget);
	QGridLayout* buttonLayout = new QGridLayout();
	m_addButton = new QPushButton("Add Unit", listPane);
	m_addButton->setObjectName("addTrainUnitButton");
	m_duplicateButton = new QPushButton("Duplicate", listPane);
	m_duplicateButton->setObjectName("duplicateTrainUnitButton");
	m_deleteButton = new QPushButton("Delete", listPane);
	m_deleteButton->setObjectName("deleteTrainUnitButton");
	buttonLayout->addWidget(m_addButton, 0, 0);
	buttonLayout->addWidget(m_duplicateButton, 0, 1);
	buttonLayout->addWidget(m_deleteButton, 0, 2);
	listLayout->addLayout(buttonLayout);
	layout->addWidget(listPane);

	// below: the id of the selected unit above its traction and physical values
	QWidget* detailPane = new QWidget(this);
	QVBoxLayout* detailLayout = new QVBoxLayout(detailPane);
	detailLayout->setContentsMargins(0, 0, 0, 0);
	const auto addHeading = [](QWidget* page, QVBoxLayout* pageLayout, const QString& title) {
		QLabel* heading = EditorLayout::sectionHeading(title, page);
		heading->setObjectName("rollingStockSectionHeading");
		pageLayout->addWidget(heading);
	};
	// Adds the buttons and the status of one source link to a page. `name` is part of the object names.
	const auto addSourceLink = [](QWidget* page, QVBoxLayout* pageLayout, const QString& linkText, const QString& name,
								   QPushButton*& link, QPushButton*& unlink, QPushButton*& retry, QLabel*& status) {
		QGridLayout* actions = new QGridLayout();
		link = new QPushButton(linkText, page);
		link->setObjectName("linkTrainUnit" + name + "Button");
		unlink = new QPushButton("Unlink", page);
		unlink->setObjectName("unlinkTrainUnit" + name + "Button");
		retry = new QPushButton("Retry", page);
		retry->setObjectName("retryTrainUnit" + name + "Button");
		actions->addWidget(link, 0, 0, 1, 2);
		actions->addWidget(unlink, 1, 0);
		actions->addWidget(retry, 1, 1);
		pageLayout->addLayout(actions);
		status = EditorLayout::wrappedLabel("Unlinked", page);
		status->setObjectName("trainUnit" + name + "StatusLabel");
		pageLayout->addWidget(status);
	};
	addHeading(detailPane, detailLayout, "ID");
	m_idEdit = new QLineEdit(detailPane);
	m_idEdit->setObjectName("trainUnitIdEdit");
	detailLayout->addWidget(m_idEdit);
	m_emptyStateLabel = EditorLayout::emptyStateLabel(detailPane);
	m_emptyStateLabel->setObjectName("trainUnitEmptyStateLabel");
	detailLayout->addWidget(m_emptyStateLabel);

	m_tabs = new QTabWidget(detailPane);
	m_tabs->setObjectName("trainUnitTabs");

	QWidget* tractionPage = new QWidget(m_tabs);
	QVBoxLayout* tractionLayout = new QVBoxLayout(tractionPage);
	tractionLayout->addWidget(new QLabel("Traction file", tractionPage));
	m_sourceTractionEdit = new QLineEdit(tractionPage);
	m_sourceTractionEdit->setObjectName("trainUnitSourceTractionEdit");
	m_sourceTractionEdit->setPlaceholderText("Optional");
	tractionLayout->addWidget(m_sourceTractionEdit);
	addSourceLink(tractionPage, tractionLayout, "Link File...", "SourceTraction", m_linkTractionButton,
		m_unlinkTractionButton, m_retryTractionButton, m_sourceTractionStatusLabel);

	tractionLayout->addWidget(new QLabel("Traction curve", tractionPage));
	m_tractionTable = new QTableWidget(0, 5, tractionPage);
	m_tractionTable->setObjectName("trainUnitTractionTable");
	m_tractionTable->setHorizontalHeaderLabels(QStringList()
		<< "Lower speed (m/s)" << "Upper speed (m/s)" << "C0" << "C1" << "C2");
	m_tractionTable->setItemDelegate(new TractionCellDelegate(m_tractionTable));
	// A number changes in an editor that a double click, the edit key or typing opens. Without one the arrow keys move the
	// current cell, which is the selected one, and Tab leaves the table.
	m_tractionTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
	m_tractionTable->setTabKeyNavigation(false);
	m_tractionTable->setSelectionBehavior(QAbstractItemView::SelectItems);
	m_tractionTable->setSelectionMode(QAbstractItemView::SingleSelection);
	// size columns to their headers so "Lower speed (m/s)" is never clipped
	m_tractionTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_tractionTable->horizontalHeader()->setStretchLastSection(true);
	m_tractionTable->setMinimumHeight(m_tractionTable->fontMetrics().lineSpacing() * 8);
	tractionLayout->addWidget(m_tractionTable, 1);
	QGridLayout* tractionButtonLayout = new QGridLayout();
	m_addTractionRowButton = new QPushButton("Add Row", tractionPage);
	m_addTractionRowButton->setObjectName("addTrainUnitTractionRowButton");
	m_removeTractionRowButton = new QPushButton("Delete Row", tractionPage);
	m_removeTractionRowButton->setObjectName("removeTrainUnitTractionRowButton");
	m_plotButton = new QPushButton("Plot Traction Curve", tractionPage);
	m_plotButton->setObjectName("plotTrainUnitTractionButton");
	tractionButtonLayout->addWidget(m_addTractionRowButton, 0, 0);
	tractionButtonLayout->addWidget(m_removeTractionRowButton, 0, 1);
	tractionButtonLayout->addWidget(m_plotButton, 1, 0, 1, 2);
	tractionLayout->addLayout(tractionButtonLayout);
	m_tabs->addTab(tractionPage, "Traction");

	QWidget* physicalPage = new QWidget(m_tabs);
	QVBoxLayout* physicalPageLayout = new QVBoxLayout(physicalPage);
	QFormLayout* physicalLayout = new QFormLayout();
	physicalLayout->setRowWrapPolicy(QFormLayout::WrapLongRows);
	physicalLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	const char* physicalLabels[PhysicalFieldCount] = {
		"Traction-unit mass (kg)", "Wagon mass (kg)", "Wagon count",
		"Maximum speed (m/s)", "Maximum deceleration (m/s²)", "Frontal area (m²)",
		"Resistance coefficient", "Jerk (m/s³)", "Length (m)"};
	for (int i = 0; i < PhysicalFieldCount; ++i) {
		auto* edit = new CompactDoubleSpinBox(physicalPage);
		edit->setObjectName(QString("trainUnitPhysicalEdit%1").arg(i));
		const SceneValueRange& range = kSceneTrainPhysicalFields[static_cast<std::size_t>(i)].range;
		edit->setRange(range.min, range.max);
		edit->setDecimals(std::numeric_limits<double>::max_digits10);
		edit->setSingleStep(i == 2 ? 1.0 : 0.1);
		edit->setKeyboardTracking(false);
		m_physicalEdits[static_cast<std::size_t>(i)] = edit;
		physicalLayout->addRow(new QLabel(physicalLabels[i], physicalPage), edit);
	}
	physicalPageLayout->addLayout(physicalLayout);
	physicalPageLayout->addWidget(new QLabel("Parameter file", physicalPage));
	m_sourceDataEdit = new QLineEdit(physicalPage);
	m_sourceDataEdit->setObjectName("trainUnitSourceDataEdit");
	m_sourceDataEdit->setPlaceholderText("Optional");
	physicalPageLayout->addWidget(m_sourceDataEdit);
	addSourceLink(physicalPage, physicalPageLayout, "Link File...", "SourceData", m_linkDataButton,
		m_unlinkDataButton, m_retryDataButton, m_sourceDataStatusLabel);
	physicalPageLayout->addStretch(1);
	m_tabs->addTab(physicalPage, "Physical");

	detailLayout->addWidget(m_tabs, 1);
	layout->addWidget(detailPane, 1);

	connect(m_listWidget, &QListWidget::currentRowChanged, this, [this](int) { updateDetail(); });
	connect(m_idEdit, &QLineEdit::editingFinished, this, &TrainUnitPanel::commitIdEdit);
	connect(m_sourceDataEdit, &QLineEdit::editingFinished, this, &TrainUnitPanel::commitSources);
	connect(m_sourceTractionEdit, &QLineEdit::editingFinished, this, &TrainUnitPanel::commitSources);
	for (bool traction : {false, true}) {
		connect(linkButton(traction), &QPushButton::clicked, this, [this, traction] { emit sourceLinkRequested(traction); });
		connect(unlinkButton(traction), &QPushButton::clicked, this, [this, traction] { emit sourceUnlinkRequested(traction); });
		connect(retryButton(traction), &QPushButton::clicked, this, [this, traction] { emit sourceRetryRequested(traction); });
	}
	for (int i = 0; i < PhysicalFieldCount; ++i) {
		connect(m_physicalEdits[static_cast<std::size_t>(i)], QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
			[this, i](double) { commitPhysical(i); });
	}
	connect(m_addButton, &QPushButton::clicked, this, &TrainUnitPanel::addUnit);
	connect(m_duplicateButton, &QPushButton::clicked, this, &TrainUnitPanel::duplicateUnit);
	connect(m_deleteButton, &QPushButton::clicked, this, &TrainUnitPanel::deleteUnit);
	connect(m_addTractionRowButton, &QPushButton::clicked, this, &TrainUnitPanel::addTractionRow);
	connect(m_removeTractionRowButton, &QPushButton::clicked, this, &TrainUnitPanel::removeTractionRow);
	connect(m_plotButton, &QPushButton::clicked, this, [this] {
		if (const SceneTrainUnit* unit = selectedUnit())
			emit tractionPlotRequested(QString::fromStdString(unit->id));
	});
	connect(m_tractionTable, &QTableWidget::currentCellChanged, this, [this](int, int, int, int) {
		m_removeTractionRowButton->setEnabled(m_tractionTable->currentRow() >= 0);
	});
	connect(m_tractionTable, &QTableWidget::cellChanged, this, [this](int row, int column) {
		commitTractionCell(row, column, m_tractionTable->item(row, column)->data(Qt::EditRole).toDouble());
	});
	refresh();
}

void TrainUnitPanel::setDocument(SceneDocument* document) {
	QObject::disconnect(m_replacedConnection);
	m_document = document;
	if (m_document)
		m_replacedConnection = connect(m_document, &SceneDocument::replaced, this, &TrainUnitPanel::refresh);
	refresh();
}

void TrainUnitPanel::setPrompts(UserPrompts* prompts) {
	m_prompts = prompts ? prompts : &m_ownPrompts;
}

bool TrainUnitPanel::hasScene() const {
	return m_document && m_document->isLoaded();
}

int TrainUnitPanel::selectedRow() const {
	const int row = m_listWidget->currentRow();
	if (!hasScene() || row < 0 || row >= static_cast<int>(m_document->model().trainUnits.size()))
		return -1;
	return row;
}

const SceneTrainUnit* TrainUnitPanel::selectedUnit() const {
	const int row = selectedRow();
	return row < 0 ? nullptr : &m_document->model().trainUnits[static_cast<std::size_t>(row)];
}

void TrainUnitPanel::refresh() {
	const bool loaded = hasScene();
	{
		const int previousRow = m_listWidget->currentRow();
		const QString previousId = m_listWidget->currentItem() ? m_listWidget->currentItem()->text() : QString();
		const QSignalBlocker blocker(m_listWidget);
		m_listWidget->clear();
		if (loaded) {
			for (const auto& unit : m_document->model().trainUnits)
				m_listWidget->addItem(QString::fromStdString(unit.id));
		}
		const int rowCount = m_listWidget->count();
		int rowToSelect = -1;
		if (rowCount > 0)
			rowToSelect = previousRow < 0 ? 0 : std::min(previousRow, rowCount - 1);
		if (const int sameUnit = rowOfEntry(m_listWidget, previousId, previousRow); sameUnit >= 0)
			rowToSelect = sameUnit;
		m_listWidget->setCurrentRow(rowToSelect);
		m_listWidget->setEnabled(loaded);
	}
	m_addButton->setEnabled(loaded);
	updateDetail();
}

void TrainUnitPanel::updateDetail() {
	const SceneTrainUnit* unit = selectedUnit();
	const bool hasSelection = unit != nullptr;
	m_tabs->setVisible(hasSelection);
	DialogLayout::setHelp(m_emptyStateLabel,
		hasScene() && !hasSelection ? QStringLiteral("Select a rolling stock unit or use Add Unit.") : QString());
	{
		const QSignalBlocker blocker(m_idEdit);
		m_idEdit->setText(unit ? QString::fromStdString(unit->id) : QString());
		m_idEdit->setEnabled(hasSelection);
	}
	const double values[PhysicalFieldCount] = {
		unit ? unit->physical.mass_of_traction_unit_kg : 0.0,
		unit ? unit->physical.mass_of_a_wagon_kg : 0.0,
		unit ? unit->physical.number_of_wagons : 0.0,
		unit ? unit->physical.max_speed_ms : 0.0,
		unit ? unit->physical.max_deceleration_ms2 : 0.0,
		unit ? unit->physical.frontal_area_m2 : 0.0,
		unit ? unit->physical.resistance_coefficient : 0.0,
		unit ? unit->physical.jerk_ms3 : 0.0,
		unit ? unit->physical.length_m : 0.0};
	for (std::size_t index = 0; index < m_physicalEdits.size(); ++index) {
		const QSignalBlocker blocker(m_physicalEdits[index]);
		m_physicalEdits[index]->setValue(values[index]);
		m_physicalEdits[index]->setEnabled(hasSelection);
	}
	{
		const QSignalBlocker blocker(m_sourceDataEdit);
		m_sourceDataEdit->setText(unit ? QString::fromStdString(unit->sourceDataFile) : QString());
		m_sourceDataEdit->setEnabled(hasSelection);
	}
	{
		const QSignalBlocker blocker(m_sourceTractionEdit);
		m_sourceTractionEdit->setText(unit ? QString::fromStdString(unit->sourceTractionFile) : QString());
		m_sourceTractionEdit->setEnabled(hasSelection);
	}
	emit detailsShown();
	m_plotButton->setEnabled(unit && !unit->tractionCurve.empty());
	m_duplicateButton->setEnabled(hasSelection);
	m_deleteButton->setEnabled(hasSelection);
	m_addTractionRowButton->setEnabled(hasSelection);
	m_removeTractionRowButton->setEnabled(hasSelection && m_tractionTable->currentRow() >= 0);
	refreshTractionTable();
}

void TrainUnitPanel::refreshTractionTable() {
	const SceneTrainUnit* unit = selectedUnit();
	const int previousRow = m_tractionTable->currentRow();
	const QSignalBlocker tableBlocker(m_tractionTable);
	m_tractionTable->setRowCount(0);
	if (!unit) {
		m_tractionTable->setEnabled(false);
		return;
	}
	const auto& curve = unit->tractionCurve;
	for (int row = 0; row < static_cast<int>(curve.size()); ++row) {
		m_tractionTable->insertRow(row);
		for (int column = 0; column < 5; ++column) {
			// The cell holds the stored value as its editor holds it, so a value outside the range shows at the nearest limit.
			auto* item = new QTableWidgetItem();
			item->setData(Qt::EditRole,
				TractionCellDelegate::editorValue(column, curve[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)]));
			m_tractionTable->setItem(row, column, item);
		}
	}
	m_tractionTable->setEnabled(true);
	if (!curve.empty())
		m_tractionTable->setCurrentCell(std::min(previousRow < 0 ? 0 : previousRow, static_cast<int>(curve.size()) - 1), 0);
	m_removeTractionRowButton->setEnabled(m_tractionTable->currentRow() >= 0);
}

void TrainUnitPanel::setSourceState(bool traction, const QString& linkedPath, const QString& status) {
	const bool hasSelection = selectedUnit() != nullptr;
	const bool linked = hasSelection && !linkedPath.isEmpty();
	linkButton(traction)->setEnabled(hasSelection);
	unlinkButton(traction)->setEnabled(linked);
	retryButton(traction)->setEnabled(linked);
	QLabel* statusLabel = sourceStatusLabel(traction);
	if (!hasSelection) {
		statusLabel->setText(QStringLiteral("No unit selected"));
		statusLabel->setToolTip(QString());
	} else if (!linked) {
		statusLabel->setText(QStringLiteral("Unlinked"));
		statusLabel->setToolTip(QString());
	} else if (!QFileInfo(linkedPath).exists()) {
		// A file that is gone is missing whatever the last answer to its changes was, also after Keep Edits.
		statusLabel->setText(QStringLiteral("File not found: %1").arg(breakablePath(linkedPath)));
		statusLabel->setToolTip(display_path::format(linkedPath));
	} else {
		statusLabel->setText(status.isEmpty() ? QStringLiteral("Linked: %1").arg(QFileInfo(linkedPath).fileName()) : status);
		statusLabel->setToolTip(display_path::format(linkedPath));
	}
}

bool TrainUnitPanel::selectUnit(const QString& id) {
	const QList<QListWidgetItem*> matches = m_listWidget->findItems(id, Qt::MatchExactly);
	if (matches.isEmpty())
		return false;
	m_listWidget->setCurrentItem(matches.front());
	return true;
}

void TrainUnitPanel::commitPendingEditors() {
	commitIdEdit();
	for (std::size_t index = 0; index < m_physicalEdits.size(); ++index) {
		QDoubleSpinBox* edit = m_physicalEdits[index];
		if (hasEditorFocus(edit)) {
			edit->interpretText();
			commitPhysical(static_cast<int>(index));
		}
	}
	// The open editor of the traction table, which is at the current cell, commits as Return commits it.
	const QModelIndex current = m_tractionTable->currentIndex();
	if (QWidget* editor = m_tractionTable->indexWidget(current))
		m_tractionTable->itemDelegate()->setModelData(editor, m_tractionTable->model(), current);
	commitSources();
}

void TrainUnitPanel::addUnit() {
	if (!hasScene())
		return;
	SceneModel& model = m_document->mutableModel();
	SceneTrainUnit unit;
	unit.id = sceneUniqueTrainUnitId(model, "new_train_unit");
	model.trainUnits.push_back(unit);

	m_document->markChanged(SceneDocument::TrainUnits, "Add rolling stock unit");
	refresh();
	emit trainUnitsChanged();
	emit changed();

	m_listWidget->setCurrentRow(static_cast<int>(model.trainUnits.size()) - 1);
}

void TrainUnitPanel::duplicateUnit() {
	const int row = selectedRow();
	if (row < 0)
		return;
	SceneModel& model = m_document->mutableModel();
	SceneTrainUnit duplicate = model.trainUnits[static_cast<std::size_t>(row)];
	duplicate.id = sceneUniqueTrainUnitId(model, duplicate.id + "_copy");
	model.trainUnits.insert(model.trainUnits.begin() + row + 1, duplicate);

	m_document->markChanged(SceneDocument::TrainUnits, "Duplicate rolling stock unit");
	refresh();
	emit trainUnitsChanged();
	emit changed();

	m_listWidget->setCurrentRow(row + 1);
}

void TrainUnitPanel::deleteUnit() {
	const int row = selectedRow();
	if (row < 0)
		return;
	SceneModel& model = m_document->mutableModel();
	const std::string id = model.trainUnits[static_cast<std::size_t>(row)].id;
	QStringList consumers;
	for (const std::string& consumer : sceneDirectDeleteConsumers(model, "train_unit", id))
		consumers << QString::fromStdString(consumer);
	if (!consumers.isEmpty()) {
		m_prompts->showError(this, "Cannot Delete Rolling Stock Unit",
			QString("Rolling stock unit '%1' is used by:\n• %2\nRemove it from those compositions first.")
				.arg(QString::fromStdString(id), consumers.join("\n• ")),
			true);
		return;
	}
	if (!m_prompts->confirm(this, "Delete Rolling Stock Unit",
			QString("Delete rolling stock unit '%1'?").arg(QString::fromStdString(id))))
		return;

	model.trainUnits.erase(model.trainUnits.begin() + row);

	m_document->markChanged(SceneDocument::TrainUnits, "Delete rolling stock unit");
	emit unitDeleted(QString::fromStdString(id));
	refresh();
	emit trainUnitsChanged();
	emit changed();
}

void TrainUnitPanel::commitIdEdit() {
	const int row = selectedRow();
	if (row < 0)
		return;
	SceneModel& model = m_document->mutableModel();
	SceneTrainUnit& unit = model.trainUnits[static_cast<std::size_t>(row)];
	const std::string oldId = unit.id;
	const std::string newId = m_idEdit->text().trimmed().toStdString();
	if (newId.empty()) {
		const QSignalBlocker blocker(m_idEdit);
		m_idEdit->setText(QString::fromStdString(oldId));
		return;
	}
	if (newId == oldId)
		return;
	for (int index = 0; index < static_cast<int>(model.trainUnits.size()); ++index) {
		if (index != row && model.trainUnits[static_cast<std::size_t>(index)].id == newId) {
			const QSignalBlocker blocker(m_idEdit);
			m_idEdit->setText(QString::fromStdString(oldId));
			return;
		}
	}

	unit.id = newId;
	sceneRewriteTrainUnitReferences(model, oldId, newId);
	if (QListWidgetItem* item = m_listWidget->item(row)) {
		const QSignalBlocker blocker(m_listWidget);
		item->setText(QString::fromStdString(newId));
	}

	m_document->markChanged(SceneDocument::TrainUnits | SceneDocument::Compositions, "Rename rolling stock unit");
	emit unitRenamed(QString::fromStdString(oldId), QString::fromStdString(newId));
	emit trainUnitsChanged();
	emit changed();
}

void TrainUnitPanel::commitSources() {
	const int row = selectedRow();
	if (row < 0)
		return;
	SceneTrainUnit& unit = m_document->mutableModel().trainUnits[static_cast<std::size_t>(row)];
	const std::string sourceDataFile = m_sourceDataEdit->text().toStdString();
	const std::string sourceTractionFile = m_sourceTractionEdit->text().toStdString();
	if (unit.sourceDataFile == sourceDataFile && unit.sourceTractionFile == sourceTractionFile)
		return;
	unit.sourceDataFile = sourceDataFile;
	unit.sourceTractionFile = sourceTractionFile;

	m_document->markChanged(SceneDocument::TrainUnits, "Edit rolling stock unit sources");
	emit sourceReferencesChanged();
	emit changed();
}

void TrainUnitPanel::commitPhysical(int fieldIndex) {
	const int row = selectedRow();
	if (row < 0 || fieldIndex < 0 || fieldIndex >= PhysicalFieldCount)
		return;
	SceneTrainUnit& unit = m_document->mutableModel().trainUnits[static_cast<std::size_t>(row)];
	SceneTrainPhysical& physical = unit.physical;
	double* values[PhysicalFieldCount] = {
		&physical.mass_of_traction_unit_kg, &physical.mass_of_a_wagon_kg, &physical.number_of_wagons,
		&physical.max_speed_ms, &physical.max_deceleration_ms2, &physical.frontal_area_m2,
		&physical.resistance_coefficient, &physical.jerk_ms3, &physical.length_m};
	const double value = m_physicalEdits[static_cast<std::size_t>(fieldIndex)]->value();
	// A stored value outside the range is shown at the nearest limit. Showing it is not an edit.
	const SceneValueRange& range = kSceneTrainPhysicalFields[static_cast<std::size_t>(fieldIndex)].range;
	if (std::clamp(*values[fieldIndex], range.min, range.max) == value && unit.hasPhysical)
		return;
	*values[fieldIndex] = value;
	unit.hasPhysical = true;

	m_document->markChanged(SceneDocument::TrainUnits, "Edit rolling stock unit");
	emit changed();
}

void TrainUnitPanel::addTractionRow() {
	const int unitRow = selectedRow();
	if (unitRow < 0)
		return;
	std::vector<std::array<double, 5>>& curve = m_document->mutableModel().trainUnits[static_cast<std::size_t>(unitRow)].tractionCurve;
	std::array<double, 5> row = {{0.0, 1.0, 0.0, 0.0, 0.0}};
	if (!curve.empty()) {
		row[0] = curve.back()[1];
		row[1] = row[0] + 1.0;
	}
	curve.push_back(row);

	m_document->markChanged(SceneDocument::TrainUnits, "Add traction row");
	emit changed();
	refreshTractionTable();
	m_tractionTable->setCurrentCell(m_tractionTable->rowCount() - 1, 0);
}

void TrainUnitPanel::removeTractionRow() {
	const int unitRow = selectedRow();
	const int curveRow = m_tractionTable->currentRow();
	if (unitRow < 0)
		return;
	std::vector<std::array<double, 5>>& curve = m_document->mutableModel().trainUnits[static_cast<std::size_t>(unitRow)].tractionCurve;
	if (curveRow < 0 || curveRow >= static_cast<int>(curve.size()))
		return;
	curve.erase(curve.begin() + curveRow);

	m_document->markChanged(SceneDocument::TrainUnits, "Delete traction row");
	emit changed();
	updateDetail();
}

void TrainUnitPanel::commitTractionCell(int row, int column, double value) {
	const int unitRow = selectedRow();
	if (unitRow < 0 || row < 0 || column < 0 || column >= 5)
		return;
	std::vector<std::array<double, 5>>& curve = m_document->mutableModel().trainUnits[static_cast<std::size_t>(unitRow)].tractionCurve;
	if (row >= static_cast<int>(curve.size()))
		return;
	const SceneValueRange& range = kSceneTractionColumns[static_cast<std::size_t>(column)].range;
	if (std::clamp(curve[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)], range.min, range.max) == value)
		return;
	curve[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)] = value;

	m_document->markChanged(SceneDocument::TrainUnits, "Edit traction curve");
	emit changed();
}
