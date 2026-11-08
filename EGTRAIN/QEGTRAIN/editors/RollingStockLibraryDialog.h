#pragma once

// SceneDocument.h first: it includes the scene model, which has a member named signals, with the signals keyword of Qt
// undefined, so RollingStockLibrary.h below finds the model included.
#include "editors/SceneDocument.h"
#include "scene/RollingStockLibrary.h"

#include <QDialog>

#include <vector>

class QCheckBox;
class QPushButton;
class QTreeWidget;

/// The rolling stock units of the bundled cases that come with the program, read from the resource
/// :/rolling_stock_library.json.
RollingStockLibrary bundledRollingStockLibrary();

/// The dialog of Add from Library: the units of the library under a heading that names the cases that hold them, each
/// with its id, maximum speed, length and number of traction rows, and a tick box that adds a composition of the unit as
/// well. It records the choice and changes nothing; the caller adds the unit after exec() returns.
class RollingStockLibraryDialog : public QDialog {
	Q_OBJECT

public:
	/// Lists `units` and selects the first of them.
	explicit RollingStockLibraryDialog(std::vector<RollingStockLibraryUnit> units, QWidget* parent = nullptr);

	/// The selected unit; null while a heading or nothing is selected.
	const RollingStockLibraryUnit* selectedUnit() const;
	/// Whether a composition with only the chosen unit is added as well. True until the tick box is cleared.
	bool addsComposition() const;

	/// The units: a heading row for each list of cases, followed by a row for each unit that those cases hold.
	QTreeWidget* unitTree() const { return m_tree; }
	/// The tick box "Also add a composition with only this unit".
	QCheckBox* compositionCheck() const { return m_compositionCheck; }
	/// The Add button, enabled while a unit is selected.
	QPushButton* addButton() const { return m_addButton; }

private:
	std::vector<RollingStockLibraryUnit> m_units;
	QTreeWidget* m_tree = nullptr;
	QCheckBox* m_compositionCheck = nullptr;
	QPushButton* m_addButton = nullptr;
};
