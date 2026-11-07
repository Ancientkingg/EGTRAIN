#include "diagrams/RouteReferenceChoice.h"

#include "scene/SectionInventory.h"
#include "widgets/DialogLayout.h"
#include "widgets/PlainToolTip.h"

#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QListWidget>
#include <QPushButton>
#include <QScrollBar>
#include <QStringList>

#include <algorithm>

namespace {

constexpr int kMaxLabelChars = 160;

// Keeps the start and the end of text, so the route id and the last station stay visible.
QString elideMiddle(const QString& text, int maxChars) {
	if (text.size() <= maxChars)
		return text;
	const int kept = maxChars - 1;
	return text.left(kept - kept / 2) + QChar(0x2026) + text.right(kept / 2);
}

// The services of the scene that run on the route, in scene order: "service A" or "services A, B". Empty when there
// is none.
QString servicesOn(const SceneModel& scene, const std::string& routeId) {
	QStringList ids;
	for (const SceneService& service : scene.services)
		if (service.route == routeId)
			ids << QString::fromStdString(service.id);
	if (ids.isEmpty())
		return {};
	return QString("%1 %2").arg(ids.size() == 1 ? "service" : "services", ids.join(QStringLiteral(", ")));
}

} // namespace

QVector<RouteReferenceChoice> buildRouteReferenceChoices(const SceneModel& scene,
	const std::vector<std::pair<int, std::string>>& usedRoutes) {
	const SceneSectionInventory inventory = buildSceneSectionInventory(scene);
	QVector<RouteReferenceChoice> choices;
	// The tooltips without their route id. A tooltip holds its label in full, so two equal ones mark labels that read
	// the same.
	QStringList descriptions;
	for (const auto& used : usedRoutes) {
		RouteReferenceChoice choice;
		choice.runtimeIndex = used.first;
		choice.routeId = QString::fromStdString(used.second);
		const auto sceneRoute = std::find_if(scene.routes.begin(), scene.routes.end(),
			[&used](const SceneRoute& route) { return route.id == used.second; });
		const SceneRouteStations stations = sceneRoute == scene.routes.end()
			? SceneRouteStations()
			: sceneRouteStations(scene, *sceneRoute, inventory);
		if (!stations.resolved) {
			choice.label = QString("%1 (station order unavailable)").arg(choice.routeId);
			choice.toolTip = QString("%1: station order unavailable.").arg(choice.routeId);
		} else if (stations.stationIds.empty()) {
			choice.label = QString("%1 (no stations on this route)").arg(choice.routeId);
			choice.toolTip = choice.label;
		} else {
			QStringList names;
			for (const std::string& id : stations.stationIds)
				names << QString::fromStdString(id);
			const QString full = QString("%1 --> %2").arg(choice.routeId, names.join(QStringLiteral(" - ")));
			choice.label = elideMiddle(full, kMaxLabelChars);
			choice.toolTip = full;
		}
		descriptions << choice.toolTip.mid(choice.routeId.size());
		choices.append(choice);
	}
	// Labels that read the same apart from the route id, such as those of two routes over the same stations, name the
	// services of their route. Every tooltip names them.
	for (qsizetype i = 0; i < choices.size(); ++i) {
		const QString services = servicesOn(scene, usedRoutes[static_cast<std::size_t>(i)].second);
		if (services.isEmpty())
			continue;
		if (descriptions.count(descriptions[i]) > 1)
			choices[i].label += QString(" (%1)").arg(services);
		choices[i].toolTip += "\nUsed by " + services;
	}
	return choices;
}

RouteReferenceDialog::RouteReferenceDialog(const QVector<RouteReferenceChoice>& choices,
	const QString& purpose, QWidget* parent)
	: QDialog(parent) {
	setObjectName(QStringLiteral("routeReferenceDialog"));
	setWindowTitle(QStringLiteral("Reference route"));

	m_list = new QListWidget(this);
	m_list->setObjectName(QStringLiteral("routeReferenceList"));
	m_list->setSelectionMode(QAbstractItemView::SingleSelection);
	m_list->setWordWrap(true);
	m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_list->setAccessibleName(QStringLiteral("Reference route"));
	int widest = 0;
	const QFontMetrics metrics(m_list->font());
	for (const RouteReferenceChoice& choice : choices) {
		auto* item = new QListWidgetItem(choice.label, m_list);
		item->setData(Qt::UserRole, choice.runtimeIndex);
		item->setToolTip(plainToolTip(choice.toolTip));
		widest = std::max(widest, metrics.horizontalAdvance(choice.label));
	}
	if (m_list->count() > 0)
		m_list->setCurrentRow(0);
	m_list->setMinimumHeight(metrics.height() * 8);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	buttons->button(QDialogButtonBox::Ok)->setDefault(true);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(m_list, &QListWidget::itemDoubleClicked, this, &QDialog::accept);

	DialogLayout::install(*this, windowTitle(), QString("Reference route for %1:").arg(purpose),
		m_list, buttons);
	// Widen the dialog for the widest label, the list frame and a scroll bar, up to the
	// width limit that install set. Rows that still do not fit wrap.
	m_list->setMinimumWidth(widest + 2 * m_list->frameWidth()
		+ m_list->verticalScrollBar()->sizeHint().width() + metrics.height());
	DialogLayout::fitWidthToContent(*this);
	m_list->setMinimumWidth(0);
	m_list->setFocus();
}

int RouteReferenceDialog::selectedRuntimeIndex() const {
	const QListWidgetItem* item = m_list->currentItem();
	return item && item->isSelected() ? item->data(Qt::UserRole).toInt() : -1;
}
