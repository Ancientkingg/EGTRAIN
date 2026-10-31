#ifndef LISTFACET_H
#define LISTFACET_H

#include "editors/InfrastructurePanel.h"
#include "editors/infrastructure/InfrastructureFacet.h"
#include "scene/SceneIds.h"
#include "scene/SceneReferences.h"

#include <QString>
#include <QStringList>
#include <QTableWidget>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

/// A facet over one list of the model, whose items have an id and are listed in the order of the list. The column of the
/// id is the first. Renaming an item rewrites what refers to it, and an item that others use cannot be deleted
/// (sceneDirectDeleteConsumers). `Item` is the type of the items, which has a `std::string id`.
template <typename Item>
class ListFacet : public InfrastructureFacet {
public:
	/// The combo key given to the constructor.
	QString key() const override { return m_key; }
	/// The headers given to the constructor.
	QStringList columns() const override { return m_columns; }

	/// One row for each item, filled by fillRow().
	void fillRows(InfrastructurePanel& panel) override {
		const std::vector<Item>& items = panel.model().*m_list;
		QTableWidget& table = *panel.table();
		table.setRowCount(static_cast<int>(items.size()));
		for (int row = 0; row < table.rowCount(); ++row)
			fillRow(table, row, items[static_cast<std::size_t>(row)]);
	}

	/// Takes the trimmed text of the cell into the item. A new id must be unique in the list and not empty, and renames the
	/// item and what refers to it; another column goes to editField().
	void commitCell(InfrastructurePanel& panel, int row, int column) override {
		const std::vector<Item>& items = panel.model().*m_list;
		if (row >= static_cast<int>(items.size()))
			return;
		const QString value = panel.table()->item(row, column)->text().trimmed();
		const std::size_t index = static_cast<std::size_t>(row);
		if (column == 0) {
			const std::string oldId = items[index].id;
			const std::string newId = value.toStdString();
			if (oldId == newId)
				return;
			if (!idCanReplaceRow(items, row, newId)) {
				panel.refuseEdit(QStringLiteral("IDs must be unique and not empty"));
				return;
			}
			panel.setSelectionId(value);
			panel.edit(QStringLiteral("Rename %1").arg(m_noun), [this, index, &oldId, &newId](SceneModel& model) {
				(model.*m_list)[index].id = newId;
				rewriteReferences(model, oldId, newId);
			});
			return;
		}
		Item edited = items[index];
		const Edit edit = editField(edited, column, value);
		if (edit == Edit::Changed)
			panel.edit(QStringLiteral("Edit %1").arg(m_noun), [this, index, &edited](SceneModel& model) { (model.*m_list)[index] = edited; });
		else if (edit == Edit::Refused)
			panel.refuseEdit();
	}

	/// Appends newItem() with an id made unique from idBase(), and selects it.
	void add(InfrastructurePanel& panel) override {
		const std::string id = sceneUniqueInfrastructureId(panel.model(), idBase(), m_key.toStdString());
		Item item = newItem(panel.model(), id);
		panel.setSelectionId(QString::fromStdString(id));
		panel.edit(QStringLiteral("Add %1").arg(m_noun), [this, &item](SceneModel& model) { (model.*m_list).push_back(std::move(item)); });
	}

	/// Deletes the selected item when the user confirms it and nothing uses it. An item with an empty id is kept.
	void remove(InfrastructurePanel& panel) override {
		const std::vector<Item>& items = panel.model().*m_list;
		const int row = panel.table()->currentRow();
		if (row < 0 || row >= static_cast<int>(items.size()))
			return;
		const std::string id = items[static_cast<std::size_t>(row)].id;
		if (id.empty())
			return;
		if (!panel.confirmDelete(QString::fromStdString(id), sceneDirectDeleteConsumers(panel.model(), m_key.toStdString(), id)))
			return;
		panel.setSelectionId(QString());
		panel.edit(QStringLiteral("Delete %1").arg(m_noun), [this, row](SceneModel& model) {
			std::vector<Item>& list = model.*m_list;
			list.erase(list.begin() + row);
		});
	}

protected:
	/// `comboKey` is the value of the combo, `noun` names one entity in the label of an edit, `list` is the member of the
	/// model that holds the items and `headers` are the columns, the id first.
	ListFacet(const char* comboKey, const char* noun, std::vector<Item> SceneModel::* list, QStringList headers)
		: m_key(QString::fromLatin1(comboKey)), m_noun(QString::fromLatin1(noun)), m_list(list), m_columns(std::move(headers)) {}

	/// Puts the cells of the entity in `row`.
	virtual void fillRow(QTableWidget& table, int row, const Item& item) const = 0;
	/// Takes the trimmed text of a cell other than the id into the item.
	virtual Edit editField(Item& item, int column, const QString& value) const = 0;
	/// A new item with this id, as the model is now.
	virtual Item newItem(const SceneModel& model, const std::string& id) const = 0;
	/// The id that a new item has when no other item has it. The default is the noun.
	virtual std::string idBase() const { return m_noun.toStdString(); }
	/// Rewrites what refers to the item that was renamed from the second argument to the third. The default rewrites
	/// nothing.
	virtual void rewriteReferences(SceneModel&, const std::string&, const std::string&) const {}

private:
	QString m_key;
	QString m_noun;
	std::vector<Item> SceneModel::* m_list;
	QStringList m_columns;
};

#endif // LISTFACET_H
