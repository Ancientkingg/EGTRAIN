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

// A facet over one list of the model, whose items have an id and are listed in the order of the list. The column of the
// id is the first. Renaming an item rewrites what refers to it, and an item that others use cannot be deleted
// (sceneDirectDeleteConsumers).
template <typename Item>
class ListFacet : public InfrastructureFacet {
public:
	QString key() const override { return m_key; }
	QStringList columns() const override { return m_columns; }

	void fillRows(InfrastructurePanel& panel) override {
		const std::vector<Item>& items = panel.model().*m_list;
		QTableWidget& table = *panel.table();
		table.setRowCount(static_cast<int>(items.size()));
		for (int row = 0; row < table.rowCount(); ++row)
			fillRow(table, row, items[static_cast<std::size_t>(row)]);
	}

	void commitCell(InfrastructurePanel& panel, int row, int column) override {
		std::vector<Item>& items = panel.mutableModel().*m_list;
		if (row >= static_cast<int>(items.size()))
			return;
		const QString value = panel.table()->item(row, column)->text().trimmed();
		Item& item = items[static_cast<std::size_t>(row)];
		Edit edit = Edit::Unchanged;
		if (column == 0) {
			const std::string newId = value.toStdString();
			if (item.id != newId) {
				if (idCanReplaceRow(items, row, newId)) {
					const std::string oldId = item.id;
					item.id = newId;
					rewriteReferences(panel.mutableModel(), oldId, newId);
					panel.setSelectionId(value);
					edit = Edit::Changed;
				} else {
					edit = Edit::UnsafeId;
				}
			}
		} else {
			edit = editField(item, column, value);
		}
		if (edit == Edit::Changed)
			panel.entityChanged((column == 0 ? QStringLiteral("Rename %1") : QStringLiteral("Edit %1")).arg(m_noun));
		else if (edit == Edit::UnsafeId)
			panel.refuseEdit(QStringLiteral("Infrastructure IDs must be non-empty and unique"));
		else if (edit == Edit::Refused)
			panel.refuseEdit();
	}

	void add(InfrastructurePanel& panel) override {
		SceneModel& model = panel.mutableModel();
		const std::string id = sceneUniqueInfrastructureId(model, idBase(), m_key.toStdString());
		(model.*m_list).push_back(newItem(model, id));
		panel.setSelectionId(QString::fromStdString(id));
		panel.entityChanged(QStringLiteral("Add %1").arg(m_noun));
	}

	void remove(InfrastructurePanel& panel) override {
		std::vector<Item>& items = panel.mutableModel().*m_list;
		const int row = panel.table()->currentRow();
		if (row < 0 || row >= static_cast<int>(items.size()))
			return;
		const std::string id = items[static_cast<std::size_t>(row)].id;
		if (id.empty())
			return;
		if (!panel.confirmDelete(QString::fromStdString(id), sceneDirectDeleteConsumers(panel.model(), m_key.toStdString(), id)))
			return;
		items.erase(items.begin() + row);
		panel.setSelectionId(QString());
		panel.entityChanged(QStringLiteral("Delete %1").arg(m_noun));
	}

protected:
	// `comboKey` is the value of the combo, `noun` names one entity in the label of an edit.
	ListFacet(const char* comboKey, const char* noun, std::vector<Item> SceneModel::* list, QStringList headers)
		: m_key(QString::fromLatin1(comboKey)), m_noun(QString::fromLatin1(noun)), m_list(list), m_columns(std::move(headers)) {}

	// Puts the cells of the entity in `row`.
	virtual void fillRow(QTableWidget& table, int row, const Item& item) const = 0;
	// Takes the text of a cell other than the id into the item.
	virtual Edit editField(Item& item, int column, const QString& value) const = 0;
	// A new item with this id, as the model is now.
	virtual Item newItem(const SceneModel& model, const std::string& id) const = 0;
	// The id that a new item has when no other item has it. The default is the noun.
	virtual std::string idBase() const { return m_noun.toStdString(); }
	// What refers to the item that was renamed.
	virtual void rewriteReferences(SceneModel&, const std::string&, const std::string&) const {}

private:
	QString m_key;
	QString m_noun;
	std::vector<Item> SceneModel::* m_list;
	QStringList m_columns;
};

#endif // LISTFACET_H
