#ifndef INFRASTRUCTUREFACET_H
#define INFRASTRUCTUREFACET_H

#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>

class InfrastructurePanel;

// One kind of entity that the infrastructure panel lists, such as the tracks. The panel owns the facet combo, the
// table and the selection. A facet knows the columns of the table, the rows that show the entities of the scene, and
// what an edited cell, an addition and a deletion do to the model of the panel's document. When it has changed the
// model it calls InfrastructurePanel::entityChanged, and when it refuses an edit InfrastructurePanel::refuseEdit.
// The panel calls a facet only while the scene is loaded and open for edits, except for fillRows.
class InfrastructureFacet {
public:
	InfrastructureFacet() = default;
	InfrastructureFacet(const InfrastructureFacet&) = delete;
	InfrastructureFacet& operator=(const InfrastructureFacet&) = delete;
	virtual ~InfrastructureFacet() = default;

	// The value that the facet combo stores for this facet, such as "tracks".
	virtual QString key() const = 0;
	// The headers of the table.
	virtual QStringList columns() const = 0;
	// Sets the rows of the table of the panel, one for each entity of the model. The columns are set and the cells are
	// empty.
	virtual void fillRows(InfrastructurePanel& panel) = 0;
	// The id that names the entity in `row` for the selection. The default is the text of the first cell.
	virtual QString selectionId(const QTableWidget& table, int row) const {
		const QTableWidgetItem* item = table.item(row, 0);
		return item ? item->text() : QString();
	}
	// The cell in `row` and `column` has been edited: takes its text into the model, or refuses it.
	virtual void commitCell(InfrastructurePanel& panel, int row, int column) = 0;
	// Adds an entity.
	virtual void add(InfrastructurePanel& panel) = 0;
	// Deletes the entity in the selected row after the user has confirmed it.
	virtual void remove(InfrastructurePanel& panel) = 0;
	// Takes the values of the cells that are being edited into the model, as if the user had left them.
	virtual void commitPendingEdits(InfrastructurePanel&) {}

protected:
	// Puts a text cell in the table. A cell that is not editable shows a value that follows from the others.
	static void setCell(QTableWidget& table, int row, int column, const QString& text, bool editable = true) {
		auto* item = new QTableWidgetItem(text);
		if (!editable)
			item->setFlags(item->flags() & ~Qt::ItemIsEditable);
		table.setItem(row, column, item);
	}
};

#endif // INFRASTRUCTUREFACET_H
