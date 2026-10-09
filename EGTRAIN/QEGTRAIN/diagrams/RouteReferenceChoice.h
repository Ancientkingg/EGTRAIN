#ifndef ROUTEREFERENCECHOICE_H
#define ROUTEREFERENCECHOICE_H

#include "scene/SceneModel.h"

#include <QDialog>
#include <QString>
#include <QVector>

#include <string>
#include <utility>
#include <vector>

class QListWidget;

// One entry of the reference-route chooser. The runtime index identifies the
// choice; the label and tooltip are display text only.
struct RouteReferenceChoice {
	int runtimeIndex = -1;
	QString routeId;
	QString label;
	QString toolTip;
};

// Labels the used routes, given as (runtime index, route id) in display order, with
// the stations each passes in travel order: "route0 --> Gvc - Gdg - Ut". A long
// label is shortened in the middle and the tooltip keeps the full text.
QVector<RouteReferenceChoice> buildRouteReferenceChoices(const SceneModel& scene,
		const std::vector<std::pair<int, std::string>>& usedRoutes);

class RouteReferenceDialog : public QDialog {
	Q_OBJECT
public:
	// purpose completes "Reference route for <purpose>:".
	RouteReferenceDialog(const QVector<RouteReferenceChoice>& choices, const QString& purpose,
			QWidget* parent = nullptr);

	// Runtime index of the selected row, or -1 when nothing is selected.
	int selectedRuntimeIndex() const;

private:
	QListWidget* m_list = nullptr;
};

#endif // ROUTEREFERENCECHOICE_H
