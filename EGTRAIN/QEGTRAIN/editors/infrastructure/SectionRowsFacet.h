#ifndef SECTIONROWSFACET_H
#define SECTIONROWSFACET_H

#include "editors/InfrastructurePanel.h"
#include "editors/infrastructure/InfrastructureFacet.h"
#include "scene/SectionInventory.h"

#include <QComboBox>
#include <QObject>
#include <QSignalBlocker>
#include <QString>
#include <QStringList>
#include <QTableWidget>

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// A facet over one list of the model whose items have no id and name sections instead, such as the dependencies between
// blocks. A section is chosen in a combo of the sections of the scene, which writes the model as soon as another
// section is chosen. The other cells are text. A new item is empty, and a row is named by its first section when it is
// deleted. Nothing else refers to these items.
template <typename Item>
class SectionRowsFacet : public InfrastructureFacet {
public:
	QString key() const override { return m_key; }
	QStringList columns() const override { return m_columns; }

	void fillRows(InfrastructurePanel& panel) override {
		const std::vector<Item>& items = panel.model().*m_list;
		panel.table()->setRowCount(static_cast<int>(items.size()));
		const SceneSectionInventory inventory = buildSceneSectionInventory(panel.model());
		for (int row = 0; row < panel.table()->rowCount(); ++row)
			fillRow(panel, inventory, row, items[static_cast<std::size_t>(row)]);
	}

	void commitCell(InfrastructurePanel& panel, int row, int column) override {
		std::vector<Item>& items = panel.mutableModel().*m_list;
		if (row >= static_cast<int>(items.size()))
			return;
		const QString value = panel.table()->item(row, column)->text().trimmed();
		const Edit edit = editField(items[static_cast<std::size_t>(row)], column, value);
		if (edit == Edit::Changed)
			panel.entityChanged(editLabel());
		else if (edit == Edit::Refused)
			panel.refuseEdit();
	}

	void add(InfrastructurePanel& panel) override {
		std::vector<Item>& items = panel.mutableModel().*m_list;
		items.push_back(Item());
		const int newRow = static_cast<int>(items.size()) - 1;
		panel.setSelectionId(QString());
		panel.entityChanged(QStringLiteral("Add %1").arg(m_noun));
		if (newRow < panel.table()->rowCount())
			panel.table()->setCurrentCell(newRow, 0);
	}

	void remove(InfrastructurePanel& panel) override {
		std::vector<Item>& items = panel.mutableModel().*m_list;
		const int row = panel.table()->currentRow();
		if (row < 0 || row >= static_cast<int>(items.size()))
			return;
		const std::string title = rowTitle(items[static_cast<std::size_t>(row)]);
		const QString name = title.empty() ? QStringLiteral("incomplete row") : QString::fromStdString(title);
		if (!panel.confirmDelete(name, {}))
			return;
		items.erase(items.begin() + row);
		panel.setSelectionId(QString());
		panel.entityChanged(QStringLiteral("Delete %1").arg(m_noun));
	}

protected:
	// What a combo does to an item when another section is chosen: stores it, and tells whether that changed the item.
	using Assign = std::function<bool(Item&, const std::string&)>;

	// `comboKey` is the value of the combo, and `noun` names one item.
	SectionRowsFacet(const char* comboKey, const char* noun, std::vector<Item> SceneModel::* list, QStringList headers)
		: m_key(QString::fromLatin1(comboKey)), m_noun(QString::fromLatin1(noun)), m_list(list), m_columns(std::move(headers)) {}

	// Puts the cells of the item in `row`.
	virtual void fillRow(InfrastructurePanel& panel, const SceneSectionInventory& inventory, int row, const Item& item) const = 0;
	// The section that names the item in the question before it is deleted; empty when the item has none yet.
	virtual std::string rowTitle(const Item& item) const = 0;
	// Takes the text of a cell that is no combo into the item. The default has no such cell.
	virtual Edit editField(Item&, int, const QString&) const { return Edit::Unchanged; }

	// Puts a combo of the sections in `column` of `row`. `current` is the section that it shows at first.
	QComboBox* addSectionCombo(InfrastructurePanel& panel, const SceneSectionInventory& inventory, int row, int column,
		const std::string& current, const char* objectName, Assign assign, bool allowNone = false,
		const QString& noneLabel = QString()) const {
		QTableWidget& table = *panel.table();
		auto* combo = new QComboBox(&table);
		combo->setObjectName(QString::fromLatin1(objectName));
		{
			const QSignalBlocker blocker(combo);
			InfrastructurePanel::populateSectionCombo(combo, inventory, current, allowNone, noneLabel);
		}
		QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), &panel,
			[&panel, list = m_list, label = editLabel(), row, combo, assign = std::move(assign)](int) {
				std::vector<Item>& items = panel.mutableModel().*list;
				if (row < 0 || row >= static_cast<int>(items.size()))
					return;
				if (assign(items[static_cast<std::size_t>(row)], combo->currentData().toString().toStdString()))
					panel.entityEdited(label);
			});
		table.setCellWidget(row, column, combo);
		return combo;
	}

	// An Assign that stores the section in a member of the item.
	static Assign storeIn(std::string Item::* member) {
		return [member](Item& item, const std::string& section) {
			if (item.*member == section)
				return false;
			item.*member = section;
			return true;
		};
	}

private:
	QString editLabel() const { return QStringLiteral("Edit %1").arg(m_noun); }

	QString m_key;
	QString m_noun;
	std::vector<Item> SceneModel::* m_list;
	QStringList m_columns;
};

#endif // SECTIONROWSFACET_H
