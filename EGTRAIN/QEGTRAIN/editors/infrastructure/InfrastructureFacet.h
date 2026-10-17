#ifndef INFRASTRUCTUREFACET_H
#define INFRASTRUCTUREFACET_H

#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>

#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

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
	// The next five belong to buttons that only some facets have, and do nothing for the others. Inserts an entity
	// before the selected one.
	virtual void insert(InfrastructurePanel&) {}
	// Moves the selected entity down by `offset` rows, up when it is negative.
	virtual void move(InfrastructurePanel&, int /*offset*/) {}
	// The sections of the selected route: adds the section that the catalog shows, removes the selected section, and
	// moves it down by `offset` places, up when it is negative.
	virtual void addSection(InfrastructurePanel&) {}
	virtual void removeSection(InfrastructurePanel&) {}
	virtual void moveSection(InfrastructurePanel&, int /*offset*/) {}

protected:
	// What came of the text of an edited cell.
	enum class Edit {
		Unchanged,
		Changed,
		// The text is no value of the field. The table shows the model again.
		Refused,
		// The text is no id that the entity can have, because it is empty or another entity has it.
		UnsafeId,
	};

	// Puts a text cell in the table. A cell that is not editable shows a value that follows from the others.
	static void setCell(QTableWidget& table, int row, int column, const QString& text, bool editable = true) {
		auto* item = new QTableWidgetItem(text);
		if (!editable)
			item->setFlags(item->flags() & ~Qt::ItemIsEditable);
		table.setItem(row, column, item);
	}

	// The text of a number with enough digits to give the number back.
	static QString numberText(double value) {
		return QString::number(value, 'g', std::numeric_limits<double>::max_digits10);
	}

	static QString flagText(bool value) { return value ? QStringLiteral("true") : QStringLiteral("false"); }

	static Edit setText(const QString& text, std::string& target) {
		const std::string value = text.toStdString();
		if (target == value)
			return Edit::Unchanged;
		target = value;
		return Edit::Changed;
	}

	static Edit setNumber(const QString& text, double& target) {
		bool parsed = false;
		const double number = text.toDouble(&parsed);
		if (!parsed || !std::isfinite(number))
			return Edit::Refused;
		if (target == number)
			return Edit::Unchanged;
		target = number;
		return Edit::Changed;
	}

	static Edit setFlag(const QString& text, bool& target) {
		const QString lower = text.toLower();
		if (lower != QStringLiteral("true") && lower != QStringLiteral("false"))
			return Edit::Refused;
		const bool flag = lower == QStringLiteral("true");
		if (target == flag)
			return Edit::Unchanged;
		target = flag;
		return Edit::Changed;
	}

	// Whether the item in `row` can take `id`: it is not empty, and no other item has it.
	template <typename Item>
	static bool idCanReplaceRow(const std::vector<Item>& items, int row, const std::string& id) {
		if (id.empty())
			return false;
		for (int index = 0; index < static_cast<int>(items.size()); ++index)
			if (index != row && items[static_cast<std::size_t>(index)].id == id)
				return false;
		return true;
	}
};

#endif // INFRASTRUCTUREFACET_H
