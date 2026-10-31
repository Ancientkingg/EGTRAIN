#ifndef INFRASTRUCTUREFACET_H
#define INFRASTRUCTUREFACET_H

#include <QFlags>
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

/// One kind of entity that the infrastructure panel lists, such as the tracks. The panel owns the facet combo, the
/// table, the selection and the facets. A facet knows the columns of the table, the rows that show the entities of the
/// scene, and what an edited cell, an addition and a deletion do to the model of the panel's document. It changes the
/// model only through InfrastructurePanel::edit or InfrastructurePanel::editInPlace, which refuse while the document is
/// not open for edits, and when it refuses an edit it calls InfrastructurePanel::refuseEdit. The panel calls key(),
/// capabilities(), columns(), fillRows(), selectionId() and commitPendingEdits() at any time, and commitCell(), add(),
/// remove(), insert(), move(), addSection(), removeSection() and moveSection() only while the scene is loaded and open
/// for edits. A widget that a facet puts in the table can still change while the scene is closed for edits.
class InfrastructureFacet {
public:
	/// Makes the base of a facet, which holds nothing.
	InfrastructureFacet() = default;
	/// A facet is not copied; the panel owns one of each kind.
	InfrastructureFacet(const InfrastructureFacet&) = delete;
	/// A facet is not copied; the panel owns one of each kind.
	InfrastructureFacet& operator=(const InfrastructureFacet&) = delete;
	/// The panel deletes its facets through this base.
	virtual ~InfrastructureFacet() = default;

	/// The controls next to the table that a facet uses besides the Add and Delete buttons.
	enum Capability {
		/// The filter on a track above the table. Add then needs a track of the scene chosen in it.
		TrackFilter = 0x1,
		/// The Insert, Move Up and Move Down buttons, which call insert() and move().
		InsertAndMove = 0x2,
		/// The sections of the selected route below the table, whose buttons call addSection(), removeSection() and
		/// moveSection().
		RouteSections = 0x4,
	};
	// A combination of Capability values.
	Q_DECLARE_FLAGS(Capabilities, Capability)

	/// The value that the facet combo stores for this facet, such as "tracks".
	virtual QString key() const = 0;
	/// The controls that the panel shows for this facet. The default is none.
	virtual Capabilities capabilities() const { return {}; }
	/// The headers of the table.
	virtual QStringList columns() const = 0;
	/// Sets the rows of the table of the panel, one for each entity of the model. The columns are set and the cells are
	/// empty when it is called.
	virtual void fillRows(InfrastructurePanel& panel) = 0;
	/// The id that names the entity in `row` for the selection. The default is the text of the first cell, or an empty
	/// string when the row has no first cell.
	virtual QString selectionId(const QTableWidget& table, int row) const {
		const QTableWidgetItem* item = table.item(row, 0);
		return item ? item->text() : QString();
	}
	/// The cell in `row` and `column` has been edited: takes its text into the model, or refuses it.
	virtual void commitCell(InfrastructurePanel& panel, int row, int column) = 0;
	/// Adds an entity.
	virtual void add(InfrastructurePanel& panel) = 0;
	/// Deletes the entity in the selected row after the user has confirmed it.
	virtual void remove(InfrastructurePanel& panel) = 0;
	/// Takes the values of the cells that are being edited into the model, as if the user had left them. The default does
	/// nothing.
	virtual void commitPendingEdits(InfrastructurePanel&) {}
	// The next five belong to the buttons of InsertAndMove and RouteSections. The panel calls them only for a facet with
	// that capability, and they do nothing by default.
	/// Inserts an entity before the selected one.
	virtual void insert(InfrastructurePanel&) {}
	/// Moves the selected entity down by `offset` rows, up when it is negative.
	virtual void move(InfrastructurePanel&, int /*offset*/) {}
	/// Adds the section that the catalog shows to the selected route.
	virtual void addSection(InfrastructurePanel&) {}
	/// Removes the selected section from the selected route.
	virtual void removeSection(InfrastructurePanel&) {}
	/// Moves the selected section of the selected route down by `offset` places, up when it is negative.
	virtual void moveSection(InfrastructurePanel&, int /*offset*/) {}

protected:
	/// What came of the text of an edited cell.
	enum class Edit {
		/// The text gives the value that the model holds.
		Unchanged,
		/// The model holds the new value.
		Changed,
		/// The text is no value of the field. The table shows the model again.
		Refused,
		/// The text is no id that the entity can have, because it is empty or another entity has it.
		UnsafeId,
	};

	/// Puts a text cell in the table, which owns it. A cell that is not editable shows a value that follows from the others.
	static void setCell(QTableWidget& table, int row, int column, const QString& text, bool editable = true) {
		auto* item = new QTableWidgetItem(text);
		if (!editable)
			item->setFlags(item->flags() & ~Qt::ItemIsEditable);
		table.setItem(row, column, item);
	}

	/// The text of a number with enough digits to give the number back.
	static QString numberText(double value) {
		return QString::number(value, 'g', std::numeric_limits<double>::max_digits10);
	}

	/// The text of a flag: "true" or "false".
	static QString flagText(bool value) { return value ? QStringLiteral("true") : QStringLiteral("false"); }

	/// Stores `text` in `target`.
	static Edit setText(const QString& text, std::string& target) {
		const std::string value = text.toStdString();
		if (target == value)
			return Edit::Unchanged;
		target = value;
		return Edit::Changed;
	}

	/// Stores the number that `text` reads in `target`. Text that is no finite number is refused.
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

	/// Stores the flag that `text` reads, "true" or "false" in any case, in `target`. Other text is refused.
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

	/// Whether the item in `row` can take `id`: it is not empty, and no other item has it.
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

Q_DECLARE_OPERATORS_FOR_FLAGS(InfrastructureFacet::Capabilities)

#endif // INFRASTRUCTUREFACET_H
