#include "editors/CompositionPanel.h"

#include "diagrams/TractionCurve.h"
#include "scene/SceneIds.h"
#include "scene/SceneReferences.h"
#include "widgets/DialogLayout.h"
#include "widgets/EditorLayout.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QVBoxLayout>

#include <string>
#include <utility>
#include <vector>

CompositionPanel::CompositionPanel(QWidget* parent) : SceneEditorPanel(parent) {
	setObjectName("compositionPanel");
	QHBoxLayout* layout = new QHBoxLayout(this);

	// left: the list of compositions and the buttons that manage it
	QWidget* listPane = new QWidget(this);
	QVBoxLayout* listLayout = new QVBoxLayout(listPane);
	listLayout->addWidget(new QLabel("Compositions", listPane));
	m_listWidget = new QListWidget(listPane);
	m_listWidget->setObjectName("compositionList");
	listLayout->addWidget(m_listWidget);
	QGridLayout* buttonLayout = new QGridLayout();
	m_addButton = new QPushButton("Add Composition", listPane);
	m_addButton->setObjectName("addCompositionButton");
	m_duplicateButton = new QPushButton("Duplicate", listPane);
	m_duplicateButton->setObjectName("duplicateCompositionButton");
	m_deleteButton = new QPushButton("Delete", listPane);
	m_deleteButton->setObjectName("deleteCompositionButton");
	buttonLayout->addWidget(m_addButton, 0, 0);
	buttonLayout->addWidget(m_duplicateButton, 0, 1);
	buttonLayout->addWidget(m_deleteButton, 1, 0);
	listLayout->addLayout(buttonLayout);
	listPane->setMaximumWidth(listPane->fontMetrics().averageCharWidth() * 32);
	layout->addWidget(listPane);

	// right: the selected composition's id and its ordered unit membership
	QWidget* detailPane = new QWidget(this);
	QVBoxLayout* detailLayout = new QVBoxLayout(detailPane);
	m_emptyStateLabel = EditorLayout::emptyStateLabel(detailPane);
	m_emptyStateLabel->setObjectName("compositionEmptyStateLabel");
	detailLayout->addWidget(m_emptyStateLabel);
	detailLayout->addWidget(new QLabel("Composition Id", detailPane));
	m_idEdit = new QLineEdit(detailPane);
	m_idEdit->setObjectName("compositionIdEdit");
	detailLayout->addWidget(m_idEdit);
	detailLayout->addWidget(new QLabel("Rolling stock units (in order)", detailPane));
	m_unitsEmptyStateLabel = EditorLayout::emptyStateLabel(detailPane);
	m_unitsEmptyStateLabel->setObjectName("compositionUnitsEmptyStateLabel");
	detailLayout->addWidget(m_unitsEmptyStateLabel);
	m_unitsListWidget = new QListWidget(detailPane);
	m_unitsListWidget->setObjectName("compositionUnitsList");
	detailLayout->addWidget(m_unitsListWidget);
	QGridLayout* unitButtonLayout = new QGridLayout();
	m_addUnitButton = new QPushButton("Add Unit", detailPane);
	m_addUnitButton->setObjectName("addCompositionUnitButton");
	m_removeUnitButton = new QPushButton("Remove Unit", detailPane);
	m_removeUnitButton->setObjectName("removeCompositionUnitButton");
	m_moveUnitUpButton = new QPushButton("Move Up", detailPane);
	m_moveUnitUpButton->setObjectName("moveCompositionUnitUpButton");
	m_moveUnitDownButton = new QPushButton("Move Down", detailPane);
	m_moveUnitDownButton->setObjectName("moveCompositionUnitDownButton");
	unitButtonLayout->addWidget(m_addUnitButton, 0, 0);
	unitButtonLayout->addWidget(m_removeUnitButton, 0, 1);
	unitButtonLayout->addWidget(m_moveUnitUpButton, 1, 0);
	unitButtonLayout->addWidget(m_moveUnitDownButton, 1, 1);
	detailLayout->addLayout(unitButtonLayout);

	// selected unit: its original sources and the tractive-effort plot
	detailLayout->addWidget(new QLabel("Original parameter source", detailPane));
	m_unitSourceDataLabel = EditorLayout::wrappedLabel(QString(), detailPane);
	m_unitSourceDataLabel->setObjectName("compositionUnitSourceDataLabel");
	m_unitSourceDataLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	detailLayout->addWidget(m_unitSourceDataLabel);
	detailLayout->addWidget(new QLabel("Original tractive-effort source", detailPane));
	m_unitSourceTractionLabel = EditorLayout::wrappedLabel(QString(), detailPane);
	m_unitSourceTractionLabel->setObjectName("compositionUnitSourceTractionLabel");
	m_unitSourceTractionLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	detailLayout->addWidget(m_unitSourceTractionLabel);
	m_plotTractionButton = new QPushButton("Plot input traction characteristic", detailPane);
	m_plotTractionButton->setObjectName("plotCompositionUnitTractionButton");
	detailLayout->addWidget(m_plotTractionButton);
	m_unitWarningLabel = new QLabel(detailPane);
	m_unitWarningLabel->setObjectName("compositionUnitWarningLabel");
	detailLayout->addWidget(m_unitWarningLabel);
	layout->addWidget(detailPane);

	connect(m_listWidget, &QListWidget::currentRowChanged, this, [this](int) { updateDetail(); });
	connect(m_unitsListWidget, &QListWidget::currentRowChanged, this, [this](int) { refreshUnitDetails(); });
	connect(m_idEdit, &QLineEdit::editingFinished, this, &CompositionPanel::commitIdEdit);
	connect(m_addButton, &QPushButton::clicked, this, &CompositionPanel::addComposition);
	connect(m_duplicateButton, &QPushButton::clicked, this, &CompositionPanel::duplicateComposition);
	connect(m_deleteButton, &QPushButton::clicked, this, &CompositionPanel::deleteComposition);
	connect(m_addUnitButton, &QPushButton::clicked, this, &CompositionPanel::addUnit);
	connect(m_removeUnitButton, &QPushButton::clicked, this, &CompositionPanel::removeUnit);
	connect(m_moveUnitUpButton, &QPushButton::clicked, this, &CompositionPanel::moveUnitUp);
	connect(m_moveUnitDownButton, &QPushButton::clicked, this, &CompositionPanel::moveUnitDown);
	connect(m_plotTractionButton, &QPushButton::clicked, this, &CompositionPanel::plotSelectedUnitTraction);
	CompositionPanel::refresh();
}

void CompositionPanel::setDocument(SceneDocument* document) {
	QObject::disconnect(m_replacedConnection);
	m_document = document;
	if (m_document)
		m_replacedConnection = connect(m_document, &SceneDocument::replaced, this, &CompositionPanel::refresh);
	refresh();
}

void CompositionPanel::setPrompts(UserPrompts* prompts) {
	m_prompts = prompts ? prompts : &m_ownPrompts;
}

bool CompositionPanel::hasScene() const {
	return m_document && m_document->isLoaded();
}

int CompositionPanel::selectedRow() const {
	const int row = m_listWidget->currentRow();
	if (!hasScene() || row < 0 || row >= static_cast<int>(m_document->model().compositions.size()))
		return -1;
	return row;
}

void CompositionPanel::refresh() {
	const bool loaded = hasScene();
	{
		const int previousRow = m_listWidget->currentRow();
		const QSignalBlocker blocker(m_listWidget);
		m_listWidget->clear();
		if (loaded) {
			for (const auto& composition : m_document->model().compositions)
				m_listWidget->addItem(QString::fromStdString(composition.id));
		}
		const int rowCount = m_listWidget->count();
		int rowToSelect = -1;
		if (rowCount > 0) {
			if (previousRow < 0)
				rowToSelect = 0;
			else if (previousRow >= rowCount)
				rowToSelect = rowCount - 1; // keep selection near a deleted last row
			else
				rowToSelect = previousRow;
		}
		m_listWidget->setCurrentRow(rowToSelect);
		m_listWidget->setEnabled(loaded);
	}
	m_addButton->setEnabled(loaded);
	updateDetail();
}

void CompositionPanel::updateDetail() {
	const int row = selectedRow();
	const bool hasSelection = row >= 0;
	DialogLayout::setHelp(m_emptyStateLabel,
		hasScene() && !hasSelection ? QStringLiteral("Select a composition or use Add Composition.") : QString());
	{
		const QSignalBlocker blocker(m_idEdit);
		m_idEdit->setText(hasSelection ? QString::fromStdString(m_document->model().compositions[static_cast<std::size_t>(row)].id)
									   : QString());
		m_idEdit->setEnabled(hasSelection);
	}
	{
		const QSignalBlocker blocker(m_unitsListWidget);
		m_unitsListWidget->clear();
		if (hasSelection) {
			for (const auto& unitId : m_document->model().compositions[static_cast<std::size_t>(row)].units)
				m_unitsListWidget->addItem(QString::fromStdString(unitId));
		}
		m_unitsListWidget->setEnabled(hasSelection);
	}
	QString unitsEmptyState;
	if (hasSelection && m_unitsListWidget->count() == 0) {
		unitsEmptyState = m_document->model().trainUnits.empty()
			? QStringLiteral("This composition has no units. Add a rolling stock unit first.")
			: QStringLiteral("This composition has no units. Use Add Unit.");
	}
	DialogLayout::setHelp(m_unitsEmptyStateLabel, unitsEmptyState);
	m_duplicateButton->setEnabled(hasSelection);
	m_deleteButton->setEnabled(hasSelection);
	m_addUnitButton->setEnabled(hasSelection && !m_document->model().trainUnits.empty());
	refreshUnitDetails();
}

void CompositionPanel::refreshUnitDetails() {
	const int unitRow = m_unitsListWidget->currentRow();
	const int unitCount = m_unitsListWidget->count();
	const bool hasUnitSelection = unitRow >= 0;

	m_removeUnitButton->setEnabled(hasUnitSelection);
	m_moveUnitUpButton->setEnabled(hasUnitSelection && unitRow > 0);
	m_moveUnitDownButton->setEnabled(hasUnitSelection && unitRow < unitCount - 1);

	// resolve the selected unit and surface its source files, association state,
	// and whether a traction curve can be plotted
	const SceneTrainUnit* unit = nullptr;
	if (hasScene() && hasUnitSelection && m_unitsListWidget->item(unitRow))
		unit = sceneTrainUnitById(m_document->model(), m_unitsListWidget->item(unitRow)->text().toStdString());

	const auto sourceText = [](const std::string& value) {
		return value.empty() ? QStringLiteral("(none)") : QString::fromStdString(value);
	};
	m_unitSourceDataLabel->setText(unit ? sourceText(unit->sourceDataFile) : QStringLiteral("(none)"));
	m_unitSourceTractionLabel->setText(unit ? sourceText(unit->sourceTractionFile) : QStringLiteral("(none)"));

	m_plotTractionButton->setEnabled(unit && !unit->tractionCurve.empty());
	QString warning;
	if (hasUnitSelection)
		warning = QString::fromStdString(tractionAssociationWarning(unit != nullptr, unit && !unit->tractionCurve.empty()));
	DialogLayout::setStatus(m_unitWarningLabel, unit ? DialogLayout::Severity::Warning : DialogLayout::Severity::Error, warning);
}

void CompositionPanel::plotSelectedUnitTraction() {
	const int unitRow = m_unitsListWidget->currentRow();
	if (!hasScene() || unitRow < 0 || !m_unitsListWidget->item(unitRow))
		return;
	const std::string unitId = m_unitsListWidget->item(unitRow)->text().toStdString();
	const SceneTrainUnit* unit = sceneTrainUnitById(m_document->model(), unitId);
	if (!unit || unit->tractionCurve.empty()) {
		QMessageBox::information(this, "No Traction Data", "The selected unit has no traction curve to plot.");
		return;
	}
	emit tractionPlotRequested(QString::fromStdString(unitId));
}

void CompositionPanel::addComposition() {
	if (!hasScene())
		return;
	SceneModel& model = m_document->mutableModel();
	SceneComposition composition;
	composition.id = sceneUniqueCompositionId(model, "new_composition");
	model.compositions.push_back(composition);

	m_document->markChanged(SceneDocument::Compositions, "Add composition");
	refresh();
	emit compositionIdsChanged();
	emit changed();

	m_listWidget->setCurrentRow(static_cast<int>(model.compositions.size()) - 1);
}

void CompositionPanel::duplicateComposition() {
	const int row = selectedRow();
	if (row < 0)
		return;
	SceneModel& model = m_document->mutableModel();
	SceneComposition duplicate = model.compositions[static_cast<std::size_t>(row)];
	duplicate.id = sceneUniqueCompositionId(model, duplicate.id + "_copy");
	model.compositions.insert(model.compositions.begin() + row + 1, duplicate);

	m_document->markChanged(SceneDocument::Compositions, "Duplicate composition");
	refresh();
	emit compositionIdsChanged();
	emit changed();

	m_listWidget->setCurrentRow(row + 1);
}

void CompositionPanel::deleteComposition() {
	const int row = selectedRow();
	if (row < 0)
		return;
	SceneModel& model = m_document->mutableModel();
	const std::string id = model.compositions[static_cast<std::size_t>(row)].id;
	QStringList consumers;
	for (const std::string& consumer : sceneDirectDeleteConsumers(model, "composition", id))
		consumers << QString::fromStdString(consumer);
	if (!consumers.isEmpty()) {
		m_prompts->showError(this, "Cannot Delete Composition",
			QString("Cannot delete composition '%1' because it is still referenced by:\n• %2\nReassign those services first.")
				.arg(QString::fromStdString(id), consumers.join("\n• ")),
			true);
		return;
	}
	if (!m_prompts->confirm(this, "Delete Composition", QString("Delete composition '%1'?").arg(QString::fromStdString(id))))
		return;

	model.compositions.erase(model.compositions.begin() + row);

	m_document->markChanged(SceneDocument::Compositions, "Delete composition");
	refresh();
	emit compositionIdsChanged();
	emit changed();
}

void CompositionPanel::commitIdEdit() {
	const int row = selectedRow();
	if (row < 0)
		return;
	SceneModel& model = m_document->mutableModel();
	SceneComposition& composition = model.compositions[static_cast<std::size_t>(row)];

	const std::string oldId = composition.id;
	const std::string newId = m_idEdit->text().trimmed().toStdString();
	if (newId == oldId)
		return;
	const bool duplicate = newId.empty() || sceneUniqueCompositionId(model, newId) != newId;
	if (duplicate) {
		m_idEdit->setText(QString::fromStdString(oldId));
		return;
	}

	composition.id = newId;
	sceneRewriteCompositionReferences(model, oldId, newId);

	// update the list row label in place instead of rebuilding the panel, so a
	// focus-out that lands on another control keeps that pending click intact
	if (QListWidgetItem* item = m_listWidget->item(row)) {
		const QSignalBlocker blocker(m_listWidget);
		item->setText(QString::fromStdString(newId));
	}

	m_document->markChanged(SceneDocument::Compositions | SceneDocument::Services, "Rename composition");
	emit compositionIdsChanged();
	emit changed();
}

void CompositionPanel::addUnit() {
	const int row = selectedRow();
	if (row < 0)
		return;
	SceneModel& model = m_document->mutableModel();
	if (model.trainUnits.empty()) {
		QMessageBox::information(this, "Add Rolling Stock Unit", "This scene has no rolling stock units defined.");
		return;
	}

	QStringList unitIds;
	for (const auto& unit : model.trainUnits)
		unitIds << QString::fromStdString(unit.id);
	const QString chosen = m_prompts->chooseItem(this, "Add Rolling Stock Unit", "Rolling stock unit:", unitIds);
	if (chosen.isEmpty())
		return;

	model.compositions[static_cast<std::size_t>(row)].units.push_back(chosen.toStdString());

	m_document->markChanged(SceneDocument::Compositions, "Add unit to composition");
	refresh();
	emit changed();

	m_unitsListWidget->setCurrentRow(m_unitsListWidget->count() - 1);
}

void CompositionPanel::removeUnit() {
	const int row = selectedRow();
	if (row < 0)
		return;
	std::vector<std::string>& units = m_document->mutableModel().compositions[static_cast<std::size_t>(row)].units;
	const int unitRow = m_unitsListWidget->currentRow();
	if (unitRow < 0 || unitRow >= static_cast<int>(units.size()))
		return;

	units.erase(units.begin() + unitRow);

	m_document->markChanged(SceneDocument::Compositions, "Remove unit from composition");
	refresh();
	emit changed();

	const int remaining = m_unitsListWidget->count();
	if (remaining > 0)
		m_unitsListWidget->setCurrentRow(unitRow < remaining ? unitRow : remaining - 1);
}

void CompositionPanel::moveUnitUp() {
	const int row = selectedRow();
	if (row < 0)
		return;
	std::vector<std::string>& units = m_document->mutableModel().compositions[static_cast<std::size_t>(row)].units;
	const int unitRow = m_unitsListWidget->currentRow();
	if (unitRow <= 0 || unitRow >= static_cast<int>(units.size()))
		return;

	std::swap(units[static_cast<std::size_t>(unitRow)], units[static_cast<std::size_t>(unitRow) - 1]);

	m_document->markChanged(SceneDocument::Compositions, "Move composition unit up");
	refresh();
	emit changed();

	m_unitsListWidget->setCurrentRow(unitRow - 1);
}

void CompositionPanel::moveUnitDown() {
	const int row = selectedRow();
	if (row < 0)
		return;
	std::vector<std::string>& units = m_document->mutableModel().compositions[static_cast<std::size_t>(row)].units;
	const int unitRow = m_unitsListWidget->currentRow();
	if (unitRow < 0 || unitRow + 1 >= static_cast<int>(units.size()))
		return;

	std::swap(units[static_cast<std::size_t>(unitRow)], units[static_cast<std::size_t>(unitRow) + 1]);

	m_document->markChanged(SceneDocument::Compositions, "Move composition unit down");
	refresh();
	emit changed();

	m_unitsListWidget->setCurrentRow(unitRow + 1);
}
