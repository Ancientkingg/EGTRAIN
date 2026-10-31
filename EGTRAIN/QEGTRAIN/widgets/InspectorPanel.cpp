#include "widgets/InspectorPanel.h"

#ifdef signals
#define EGTRAIN_RESTORE_SIGNALS_KEYWORD
#undef signals
#endif
#include "scene/SceneModel.h"
#ifdef EGTRAIN_RESTORE_SIGNALS_KEYWORD
#define signals Q_SIGNALS
#undef EGTRAIN_RESTORE_SIGNALS_KEYWORD
#endif

#include "simulation/GuiSimulationSnapshot.h"
#include "graphics/NetworkPresenter.h"
#include "graphics/NetworkScene.h"
#include "graphics/items/ConnectionItem.h"
#include "graphics/items/NodeItem.h"
#include "graphics/items/PassengerItem.h"
#include "graphics/items/SignalItem.h"
#include "graphics/items/StationNodeItem.h"
#include "graphics/items/StationOverlayItem.h"
#include "graphics/items/TrackLineItem.h"
#include "graphics/items/TrainBodyItem.h"
#include "graphics/items/TrainItemGroup.h"
#include "scene/RuntimeLimits.h"
#include "scene/TrackPreview.h"
#include "widgets/InfoDockWidget.h"
#include "widgets/PlainToolTip.h"

#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStringList>
#include <QTextOption>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

namespace {

// The tracks of a run and the connections at one of its nodes that the arrays of the run hold.
constexpr int kMaxTracks = static_cast<int>(RuntimeLimits::kNativeMaxTracks);
constexpr int kMaxNodeConnections = static_cast<int>(RuntimeLimits::kNativeMaxNodeConnections);

// Shows text in the field, and in its tooltip the text with tip, such as the id behind a label, under it. The other
// fields get their text as tooltip from the textChanged hook of the constructor; this sets the tooltip after that.
void setTextWithTip(QLineEdit* field, const QString& text, const QString& tip) {
	field->setText(text);
	field->setToolTip(plainToolTip(tip.isEmpty() ? text : text + QLatin1Char('\n') + tip));
}

// A section by its label, with its id in the tooltip; "None" without one.
void setSectionText(QLineEdit* field, const std::string& id, const std::string& label) {
	if (id.empty())
		setTextWithTip(field, QStringLiteral("None"), QString());
	else
		setTextWithTip(field, QString::fromStdString(label.empty() ? id : label), QStringLiteral("Section ID: %1").arg(QString::fromStdString(id)));
}

// A track line of the run by its id in the scene, or by its number when the run has no id for it.
QString trackName(const NetworkPresenter& presenter, int track) {
	const std::string id = presenter.sceneTrackId(track);
	return id.empty() ? QString::number(track) : QString::fromStdString(id);
}

// Track lines of the run by their ids in the scene, with their runtime numbers in the tooltip.
void setTrackText(QLineEdit* field, const NetworkPresenter& presenter, const std::vector<int>& tracks) {
	if (tracks.empty()) {
		setTextWithTip(field, QString(), QString());
		return;
	}
	QStringList names;
	QStringList numbers;
	for (const int track : tracks) {
		names << trackName(presenter, track);
		numbers << QString::number(track);
	}
	setTextWithTip(field, names.join(QStringLiteral(", ")),
		(tracks.size() == 1 ? QStringLiteral("Track number: %1") : QStringLiteral("Track numbers: %1")).arg(numbers.join(QStringLiteral(", "))));
}

} // namespace

InspectorPanel::InspectorPanel(QMainWindow* window, QGraphicsScene* scene, NetworkPresenter& presenter, Host& host)
	: QObject(window), m_scene(scene), m_presenter(&presenter), m_host(host) {
	// main widget
	auto* infoWidget = new QWidget();

	// Arc info widget
	m_arcInfoWidget = new QWidget();
	m_arcIDText = new QLineEdit(m_arcInfoWidget);
	m_arcFirstNodeIDText = new QLineEdit(m_arcInfoWidget);
	m_arcSecondNodeIDText = new QLineEdit(m_arcInfoWidget);
	m_arcTrackIDText = new QLineEdit(m_arcInfoWidget);
	m_arcLengthText = new QLineEdit(m_arcInfoWidget);
	m_arcCurvatureText = new QLineEdit(m_arcInfoWidget);
	m_arcGradientText = new QLineEdit(m_arcInfoWidget);
	m_arcSpeedLimitText = new QLineEdit(m_arcInfoWidget);
	m_arcOperationalStateText = new QLineEdit(m_arcInfoWidget);
	m_arcOperationalStateText->setObjectName("arcOperationalStateText");
	m_arcConnectedSignalsText = new QLineEdit(m_arcInfoWidget);
	m_arcConnectedSignalsText->setObjectName("arcConnectedSignalsText");
	auto* arcFormLayout = new QFormLayout();
	arcFormLayout->addRow("Arc", m_arcIDText);
	arcFormLayout->addRow("From node", m_arcFirstNodeIDText);
	arcFormLayout->addRow("To node", m_arcSecondNodeIDText);
	arcFormLayout->addRow("Track", m_arcTrackIDText);
	arcFormLayout->addRow("Length (m)", m_arcLengthText);
	arcFormLayout->addRow("Curve radius (m)", m_arcCurvatureText);
	arcFormLayout->addRow("Gradient (rise/length)", m_arcGradientText);
	arcFormLayout->addRow("Speed limit (m/s)", m_arcSpeedLimitText);
	arcFormLayout->addRow("Operational state", m_arcOperationalStateText);
	arcFormLayout->addRow("Connected signals", m_arcConnectedSignalsText);
	m_arcInfoWidget->setLayout(arcFormLayout);

	// Node info widget
	m_nodeInfoWidget = new QWidget();
	m_nodeIDText = new QLineEdit(m_nodeInfoWidget);
	m_nodeTrackIDText = new QLineEdit(m_nodeInfoWidget);
	m_nodeXText = new QLineEdit(m_nodeInfoWidget);
	m_nodeYText = new QLineEdit(m_nodeInfoWidget);
	m_nodeStationNameText = new QLineEdit(m_nodeInfoWidget);
	m_nodeStationNameText->setObjectName("nodeStationNameText");
	m_nodeRegionText = new QLineEdit(m_nodeInfoWidget);
	m_nodeRegionText->setObjectName("nodeRegionText");
	m_nodeConnectedTracksText = new QLineEdit(m_nodeInfoWidget);
	m_nodeConnectedTracksText->setObjectName("nodeConnectedTracksText");
	m_nodeSignalledText = new QLineEdit(m_nodeInfoWidget);
	m_nodeSignalledText->setObjectName("nodeSignalledText");
	auto* nodeFormLayout = new QFormLayout();
	nodeFormLayout->addRow("Node", m_nodeIDText);
	nodeFormLayout->addRow("Track", m_nodeTrackIDText);
	nodeFormLayout->addRow("X (m)", m_nodeXText);
	nodeFormLayout->addRow("Y (m)", m_nodeYText);
	nodeFormLayout->addRow("Name", m_nodeStationNameText);
	nodeFormLayout->addRow("Region", m_nodeRegionText);
	nodeFormLayout->addRow("Connected tracks", m_nodeConnectedTracksText);
	nodeFormLayout->addRow("Signalled", m_nodeSignalledText);
	m_nodeInfoWidget->setLayout(nodeFormLayout);

	// connection info widget
	m_connectionInfoWidget = new QWidget();
	m_connectionFirstTrackIDText = new QLineEdit(m_connectionInfoWidget);
	m_connectionSecondTrackIDText = new QLineEdit(m_connectionInfoWidget);
	m_connectionXFirstNodeText = new QLineEdit(m_connectionInfoWidget);
	m_connectionXSecondNodeText = new QLineEdit(m_connectionInfoWidget);
	auto* connectionFormLayout = new QFormLayout();
	connectionFormLayout->addRow("First track", m_connectionFirstTrackIDText);
	connectionFormLayout->addRow("Second track", m_connectionSecondTrackIDText);
	connectionFormLayout->addRow("First node X (m)", m_connectionXFirstNodeText);
	connectionFormLayout->addRow("Second node X (m)", m_connectionXSecondNodeText);
	m_connectionInfoWidget->setLayout(connectionFormLayout);

	// signalling info widget
	m_signallingInfoWidget = new QWidget();
	m_signallingTrackIDText = new QLineEdit(m_signallingInfoWidget);
	m_signallingXText = new QLineEdit(m_signallingInfoWidget);
	m_signallingIDSectionAheadText = new QLineEdit(m_signallingInfoWidget);
	m_signallingLengthSectionAheadText = new QLineEdit(m_signallingInfoWidget);
	m_signallingAspectText = new QLineEdit(m_signallingInfoWidget);
	m_signallingAspectText->setObjectName("signallingAspectText");
	m_signallingProtectedSectionText = new QLineEdit(m_signallingInfoWidget);
	m_signallingProtectedSectionText->setObjectName("signallingProtectedSectionText");
	m_signallingNextTrackText = new QLineEdit(m_signallingInfoWidget);
	m_signallingNextTrackText->setObjectName("signallingNextTrackText");
	m_signallingGroupDetails = new QPlainTextEdit(m_signallingInfoWidget);
	m_signallingGroupDetails->setObjectName("signallingGroupDetails");
	m_signallingGroupDetails->setReadOnly(true);
	m_signallingGroupDetails->setMaximumHeight(120);
	m_signallingGroupDetails->setLineWrapMode(QPlainTextEdit::WidgetWidth);
	m_signallingGroupDetails->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
	auto* signallingFormLayout = new QFormLayout();
	signallingFormLayout->addRow("Track", m_signallingTrackIDText);
	signallingFormLayout->addRow("X (m)", m_signallingXText);
	signallingFormLayout->addRow("Block section", m_signallingIDSectionAheadText);
	signallingFormLayout->addRow("Block section length (m)", m_signallingLengthSectionAheadText);
	signallingFormLayout->addRow("Aspect", m_signallingAspectText);
	signallingFormLayout->addRow("Protected section", m_signallingProtectedSectionText);
	signallingFormLayout->addRow("Next track", m_signallingNextTrackText);
	// The label has its own row and the box spans the form, so that a narrow dock leaves the box its width.
	auto* signallingGroupLabel = new QLabel("Signals at location", m_signallingInfoWidget);
	signallingGroupLabel->setBuddy(m_signallingGroupDetails);
	signallingFormLayout->addRow(signallingGroupLabel);
	signallingFormLayout->addRow(m_signallingGroupDetails);
	m_signallingInfoWidget->setLayout(signallingFormLayout);

	// train info widget
	m_trainInfoWidget = new QWidget();
	m_trainNameText = new QLineEdit(m_trainInfoWidget);
	m_trainNameText->setObjectName("trainNameText");
	m_trainIDText = new QLineEdit(m_trainInfoWidget);
	m_trainTypeText = new QLineEdit(m_trainInfoWidget);
	m_trainLengthText = new QLineEdit(m_trainInfoWidget);
	m_trainWagonsText = new QLineEdit(m_trainInfoWidget);
	auto* trainFormLayout = new QFormLayout();
	trainFormLayout->addRow("Train", m_trainNameText);
	trainFormLayout->addRow("ID", m_trainIDText);
	trainFormLayout->addRow("Type", m_trainTypeText);
	trainFormLayout->addRow("Length (m)", m_trainLengthText);
	trainFormLayout->addRow("Wagons", m_trainWagonsText);
	m_trainInfoWidget->setLayout(trainFormLayout);

	// The button under the form of a signal or a train that adds an incident for it.
	m_incidentButton = new QPushButton(infoWidget);
	m_incidentButton->setObjectName("inspectorIncidentButton");
	connect(m_incidentButton, &QPushButton::clicked, this, &InspectorPanel::incidentRequested);

	// main layout
	auto* infoWidgetMainLayout = new QVBoxLayout();
	infoWidgetMainLayout->addWidget(m_arcInfoWidget);
	infoWidgetMainLayout->addWidget(m_nodeInfoWidget);
	infoWidgetMainLayout->addWidget(m_connectionInfoWidget);
	infoWidgetMainLayout->addWidget(m_signallingInfoWidget);
	infoWidgetMainLayout->addWidget(m_trainInfoWidget);
	infoWidgetMainLayout->addWidget(m_incidentButton, 0, Qt::AlignLeft);
	infoWidgetMainLayout->addStretch();
	infoWidget->setLayout(infoWidgetMainLayout);
	for (QFormLayout* form : {arcFormLayout, nodeFormLayout, connectionFormLayout,
			 signallingFormLayout, trainFormLayout})
		form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	for (auto* label : infoWidget->findChildren<QLabel*>())
		label->setWordWrap(true);
	for (auto* edit : infoWidget->findChildren<QLineEdit*>()) {
		edit->setReadOnly(true);
		connect(edit, &QLineEdit::textChanged, edit, [edit](const QString& text) {
			edit->setToolTip(plainToolTip(text));
			edit->setCursorPosition(0);
		});
	}

	// dock widget
	m_infoDockWidget = new InfoDockWidget("Details");
	auto* infoScroll = new QScrollArea(m_infoDockWidget);
	infoScroll->setWidgetResizable(true);
	infoScroll->setFrameShape(QFrame::NoFrame);
	infoScroll->setMinimumSize(300, 0);
	// Tab reaches the fields inside, and the area scrolls to show them.
	infoScroll->setFocusPolicy(Qt::NoFocus);
	infoScroll->setWidget(infoWidget);
	m_infoDockWidget->setWidget(infoScroll);
	m_infoDockWidget->setAllowedAreas(Qt::RightDockWidgetArea);
	window->addDockWidget(Qt::RightDockWidgetArea, m_infoDockWidget);

	// hide widgets
	m_infoDockWidget->hide();
	m_arcInfoWidget->hide();
	m_nodeInfoWidget->hide();
	m_connectionInfoWidget->hide();
	m_signallingInfoWidget->hide();
	m_trainInfoWidget->hide();
	m_incidentButton->hide();

	connect(m_infoDockWidget, &InfoDockWidget::closed, this, &InspectorPanel::handleCloseInfoDockWidget);
}

bool InspectorPanel::selectsStationOverlay(const StationOverlayItem* overlay) const {
	if (m_host.showingTrackPreview())
		return !m_previewSelectedStationId.isEmpty() && overlay->data(PreviewGraphics::Id).toString() == m_previewSelectedStationId;
	const bool selectedByIdentity = m_hasSelectedStationIdentity && overlay->hasSourceIdentity()
		&& overlay->matchesSourceIdentity(m_selectedStationNodeId, m_selectedStationTrack);
	const bool selectedByLegacyName = !m_hasSelectedStationIdentity && !overlay->hasSourceIdentity()
		&& !m_selectedStationName.isEmpty() && overlay->stationName() == m_selectedStationName;
	return selectedByIdentity || selectedByLegacyName;
}

void InspectorPanel::clearSelectedStation() {
	m_selectedStationName.clear();
	m_hasSelectedStationIdentity = false;
	m_selectedStationNodeId = 0.0;
	m_selectedStationTrack = -1;
}

void InspectorPanel::clearSelectedTrain() {
	m_selectedTrainIndex = -1;
}

void InspectorPanel::refreshInspectedSignal() {
	if (!m_signallingInfoWidget || !m_signallingInfoWidget->isVisible() || !m_signallingGroupDetails)
		return;
	if (const SignalItem* signal = m_presenter->inspectedSignal()) {
		const QString details = signal->toolTip();
		if (m_signallingGroupDetails->toPlainText() != details) {
			const int scroll = m_signallingGroupDetails->verticalScrollBar()->value();
			m_signallingGroupDetails->setPlainText(details);
			m_signallingGroupDetails->verticalScrollBar()->setValue(scroll);
		}
		m_signallingAspectText->setText(signalAspectLabel(signal));
	}
}

// hides all widgets from the dock widget
// removes highlight from last clicked item
void InspectorPanel::handleCloseInfoDockWidget() {
	m_previewSelectedStationId.clear();
	m_previewSelectionKind.clear();
	m_previewSelectionId.clear();
	m_presenter->setInspectedSignal(nullptr);
	// Only a preview item marks the fields that need a run.
	for (auto* field : m_infoDockWidget->findChildren<QLineEdit*>())
		field->setPlaceholderText(QString());
	m_selectedStationName.clear();
	m_hasSelectedStationIdentity = false;
	m_selectedStationNodeId = 0.0;
	m_selectedStationTrack = -1;
	m_selectedTrainIndex = -1;
	for (auto* overlay : m_presenter->stationOverlays())
		if (overlay)
			overlay->setSelected(false);

	// hide all widgets
	m_arcInfoWidget->hide();
	m_nodeInfoWidget->hide();
	m_connectionInfoWidget->hide();
	m_signallingInfoWidget->hide();
	m_trainInfoWidget->hide();
	m_incidentButton->hide();
	m_presenter->removeTrainPaxInfoIcon();
	m_presenter->clearSelectedTrain(); // Explicitly dismissed or replaced train inspection.
	m_presenter->removePaxInfoIcon();

	// remove highlight
	m_presenter->clearHighlight();
	m_host.inspectionChanged();
}

void InspectorPanel::showForm(QWidget* form, const QString& title, QGraphicsItem* highlighted) {
	for (QWidget* other : {m_arcInfoWidget, m_nodeInfoWidget, m_connectionInfoWidget, m_signallingInfoWidget, m_trainInfoWidget})
		if (other != form)
			other->hide();
	if (form) {
		m_infoDockWidget->setWindowTitle(title);
		m_infoDockWidget->show();
		form->show();
	}
	if (highlighted)
		m_presenter->setHighlight(highlighted);
	else
		m_presenter->clearHighlight();
}

void InspectorPanel::showIncidentAction(const QString& text, const QString& blocked) {
	m_incidentButton->setText(text);
	m_incidentButton->setAccessibleName(text);
	m_incidentButton->setEnabled(blocked.isEmpty());
	m_incidentButton->setToolTip(blocked);
	m_incidentButton->setVisible(!text.isEmpty());
}

void InspectorPanel::clearPreviewInspection() {
	m_host.closeSceneContextMenu();
	if (m_host.showingTrackPreview()) {
		handleCloseInfoDockWidget();
		m_infoDockWidget->hide();
		for (auto* field : m_infoDockWidget->findChildren<QLineEdit*>())
			field->clear();
		m_signallingGroupDetails->clear();
	}
}

QGraphicsItem* InspectorPanel::resolvePreviewItem(const QString& kind, const QString& id,
	const QString& track, bool reversed, quint64 revision) const {
	if (!m_host.showingTrackPreview() || !m_host.loadedScene() || revision != m_host.loadedSceneRevision() || !m_scene)
		return nullptr;
	for (auto* item : m_scene->items()) {
		if (item->data(PreviewGraphics::Kind).toString() != kind
			|| item->data(PreviewGraphics::Id).toString() != id)
			continue;
		if (kind == "signal" && (item->data(PreviewGraphics::TrackId).toString() != track || item->data(PreviewGraphics::Reversed).toBool() != reversed))
			continue;
		return item;
	}
	return nullptr;
}

void InspectorPanel::displayPreviewInfo(QGraphicsItem* item) {
	const SceneModel* loaded = m_host.loadedScene();
	if (!item || item->scene() != m_scene || !m_host.showingTrackPreview() || !loaded)
		return;
	const SceneModel& model = *loaded;
	const QString kind = item->data(PreviewGraphics::Kind).toString();
	const std::string id = item->data(PreviewGraphics::Id).toString().toStdString();
	const auto text = [](const std::string& value) { return QString::fromStdString(value); };
	const auto number = [](double value) { return QString::number(value, 'g', 12); };
	// A field that only a run fills stays empty, with a placeholder that says so.
	const auto afterRun = [](QLineEdit* field) {
		field->clear();
		field->setPlaceholderText(QStringLiteral("After a run"));
	};
	// Resolve the values of the case at interaction; no pointers survive an edit/rebuild.
	clearPreviewInspection();
	QString title;
	QWidget* form = nullptr;
	if (kind == "node" || kind == "station-node" || kind == "station") {
		const SceneNode* node = nullptr;
		const SceneStation* station = nullptr;
		for (const auto& candidate : model.nodes)
			if (kind != "station" && candidate.id == id)
				node = &candidate;
		const auto memberships = previewStationMemberships(model);
		const auto membership = memberships.find(id);
		const std::string stationId = kind == "station" ? id
			: membership == memberships.end()			? std::string()
														: membership->second.stationId;
		for (const auto& candidate : model.stations)
			if (candidate.id == stationId)
				station = &candidate;
		if (kind == "station") {
			if (!station)
				return;
			for (const auto& anchor : m_presenter->trackPreview().stations)
				if (anchor.id == id)
					for (const auto& candidate : model.nodes)
						if (!anchor.nodeId.empty() && candidate.id == anchor.nodeId)
							node = &candidate;
		} else if (!node) {
			return;
		}
		m_nodeIDText->setText(node ? text(node->id) : QStringLiteral("None"));
		if (node)
			m_nodeXText->setText(number(node->xKm * 1000));
		else if (station && station->hasPosition)
			m_nodeXText->setText(number(station->positionKm * 1000));
		else
			afterRun(m_nodeXText);
		if (node)
			m_nodeYText->setText(number(node->yKm * 1000));
		else
			afterRun(m_nodeYText);
		m_nodeTrackIDText->setText(node ? text(node->trackId) : QStringLiteral("None"));
		m_nodeStationNameText->setText(station
				? text(station->name.empty() ? station->id : station->name) + " [" + text(station->id) + "]"
				: QString());
		afterRun(m_nodeRegionText);
		afterRun(m_nodeSignalledText);
		QStringList connectedTracks;
		if (node) {
			connectedTracks << text(node->trackId);
			for (const auto& connection : model.connections) {
				const std::string other = connection.fromNodeId == node->id ? connection.toNodeId
					: connection.toNodeId == node->id						? connection.fromNodeId
																			: std::string();
				for (const auto& candidate : model.nodes)
					if (candidate.id == other && !connectedTracks.contains(text(candidate.trackId)))
						connectedTracks << text(candidate.trackId);
			}
		}
		m_nodeConnectedTracksText->setText(connectedTracks.join(", "));
		if (station)
			m_previewSelectedStationId = text(station->id);
		title = kind == "station" ? "Station " + text(id) : "Node " + text(id);
		if (kind != "station" && membership != memberships.end() && !membership->second.platformId.empty())
			title += ", platform " + text(membership->second.platformId);
		form = m_nodeInfoWidget;
	} else if (kind == "arc" || kind == "track") {
		if (kind == "arc") {
			const auto arc = std::find_if(model.arcs.begin(), model.arcs.end(),
				[&](const SceneArc& value) { return value.id == id; });
			if (arc == model.arcs.end())
				return;
			m_arcIDText->setText(text(arc->id));
			m_arcFirstNodeIDText->setText(text(arc->fromNodeId));
			m_arcSecondNodeIDText->setText(text(arc->toNodeId));
			m_arcTrackIDText->setText(text(arc->trackId));
			const SceneNode* from = nullptr;
			const SceneNode* to = nullptr;
			for (const auto& node : model.nodes) {
				if (node.id == arc->fromNodeId) from = &node;
				if (node.id == arc->toNodeId) to = &node;
			}
			if (from && to)
				m_arcLengthText->setText(number(std::hypot(to->xKm - from->xKm, to->yKm - from->yKm) * 1000));
			else
				afterRun(m_arcLengthText);
			m_arcCurvatureText->setText(number(arc->curvatureRadiusM));
			m_arcGradientText->setText(number(arc->gradientPercent));
			m_arcSpeedLimitText->setText(number(arc->speedLimitMs));
		} else {
			if (std::none_of(model.tracks.begin(), model.tracks.end(),
					[&](const SceneTrack& track) { return track.id == id; }))
				return;
			m_arcIDText->setText("None");
			m_arcTrackIDText->setText(text(id));
		}
		afterRun(m_arcOperationalStateText);
		afterRun(m_arcConnectedSignalsText);
		title = kind == "arc" ? "Arc " + text(id) : "Track " + text(id);
		form = m_arcInfoWidget;
	} else if (kind == "connection") {
		const auto connection = std::find_if(model.connections.begin(), model.connections.end(),
			[&](const SceneConnection& value) { return value.id == id; });
		if (connection == model.connections.end())
			return;
		for (const auto& node : model.nodes) {
			if (node.id == connection->fromNodeId) {
				m_connectionFirstTrackIDText->setText(text(node.trackId));
				m_connectionXFirstNodeText->setText(number(node.xKm * 1000));
			}
			if (node.id == connection->toNodeId) {
				m_connectionSecondTrackIDText->setText(text(node.trackId));
				m_connectionXSecondNodeText->setText(number(node.xKm * 1000));
			}
		}
		title = "Connection " + text(id);
		form = m_connectionInfoWidget;
	} else if (kind == "signal") {
		const std::string track = item->data(PreviewGraphics::TrackId).toString().toStdString();
		const TrackPreviewSignal* head = m_presenter->previewSignal(track, id);
		if (!head)
			return;
		m_signallingTrackIDText->setText(text(track));
		m_signallingXText->setText(number(head->rawX * 1000));
		afterRun(m_signallingAspectText);
		setSectionText(m_signallingProtectedSectionText, id, head->sectionLabel);
		afterRun(m_signallingNextTrackText);
		setSectionText(m_signallingIDSectionAheadText, id, head->sectionLabel);
		afterRun(m_signallingLengthSectionAheadText);
		QStringList ids;
		for (const auto& signal : sceneSignals(model))
			if (signal.protectedSection == id)
				ids << text(signal.id);
		QString details = QStringLiteral("Track %1, section %2, %3")
							  .arg(text(track), text(id), item->data(PreviewGraphics::Reversed).toBool() ? "reverse" : "forward");
		if (!ids.isEmpty())
			details += QStringLiteral("\nSignals: %1").arg(ids.join(", "));
		m_signallingGroupDetails->setPlainText(details);
		title = "Signal " + text(head->label);
		form = m_signallingInfoWidget;
	} else {
		return;
	}
	// Show the form before the dock so the dock opens at the form's width.
	form->show();
	showForm(form, title, item);
	m_previewSelectionKind = kind;
	m_previewSelectionId = text(id);
	m_host.inspectionChanged();
	// Do not recolor boundary dots/squares or neutral signal heads on inspection.
}

// shows Node info on dock widget
void InspectorPanel::displayNodeInfo(NodeItem* nodeItem) {
	if (!nodeItem || !nodeItem->node)
		return;
	handleCloseInfoDockWidget();

	// update Node info displayed on widget
	m_nodeIDText->setText(QString::fromStdString(toStringPrecision(nodeItem->node->ID, 0)));
	m_nodeXText->setText(QString::fromStdString(toStringPrecision(1000 * nodeItem->node->X, 2))); // km to m
	m_nodeYText->setText(QString::fromStdString(toStringPrecision(1000 * nodeItem->node->Y, 2))); // km to m
	setTrackText(m_nodeTrackIDText, *m_presenter, {nodeItem->track});
	m_nodeStationNameText->clear();
	m_nodeRegionText->clear();
	m_nodeConnectedTracksText->clear();
	m_nodeSignalledText->clear();

	showForm(m_nodeInfoWidget, QStringLiteral("Node %1").arg(m_nodeIDText->text()), nodeItem);
}

// shows station Node info on dock widget
void InspectorPanel::displayStationNodeInfo(StationNodeItem* stationItem) {
	if (!stationItem || !stationItem->node)
		return;
	handleCloseInfoDockWidget();
	m_selectedStationName = QString::fromStdString(stationItem->node->stationName);
	m_hasSelectedStationIdentity = true;
	m_selectedStationNodeId = stationItem->node->ID;
	m_selectedStationTrack = stationItem->track;
	for (auto* overlay : m_presenter->stationOverlays())
		if (overlay)
			overlay->setSelected(overlay->hasSourceIdentity()
					? overlay->matchesSourceIdentity(m_selectedStationNodeId, m_selectedStationTrack)
					: !m_hasSelectedStationIdentity && overlay->stationName() == m_selectedStationName);
	m_host.inspectionChanged();

	// update Node info displayed on widget
	m_nodeIDText->setText(QString::fromStdString(toStringPrecision(stationItem->node->ID, 0)));
	m_nodeXText->setText(QString::fromStdString(toStringPrecision(1000 * stationItem->node->X, 2))); // km to m
	m_nodeYText->setText(QString::fromStdString(toStringPrecision(1000 * stationItem->node->Y, 2))); // km to m
	setTrackText(m_nodeTrackIDText, *m_presenter, {stationItem->track});
	m_nodeStationNameText->setText(QString::fromStdString(stationItem->node->stationName));
	if (stationItem->track >= 0 && stationItem->track < kMaxTracks)
		m_nodeRegionText->setText(QString::number(m_host.regionOfTrack(stationItem->track)));
	else
		m_nodeRegionText->setText("None");
	std::vector<int> connectedTracks;
	if (stationItem->track >= 0 && stationItem->track < kMaxTracks)
		connectedTracks.push_back(stationItem->track);
	const int connectionCount = std::max(0, std::min(stationItem->node->numConnections, kMaxNodeConnections));
	for (int i = 0; i < connectionCount; ++i) {
		const int connectedTrack = stationItem->node->connectIdBlockSet[i];
		if (connectedTrack >= 0 && connectedTrack < kMaxTracks
			&& std::find(connectedTracks.begin(), connectedTracks.end(), connectedTrack) == connectedTracks.end())
			connectedTracks.push_back(connectedTrack);
	}
	setTrackText(m_nodeConnectedTracksText, *m_presenter, connectedTracks);
	m_nodeSignalledText->setText(stationItem->node->isSignalled ? "Yes" : "No");

	showForm(m_nodeInfoWidget, QStringLiteral("Station %1").arg(m_nodeStationNameText->text()).trimmed(), stationItem);
}

// shows Arc info on dock widget
void InspectorPanel::displayArcInfo(TrackLineItem* line) {
	if (!line || !line->arc)
		return;
	handleCloseInfoDockWidget();

	// update Arc info displayed on widget
	m_arcIDText->setText(QString::fromStdString(toStringPrecision(line->arc->ID, 0)));
	m_arcFirstNodeIDText->setText(QString::fromStdString(toStringPrecision(line->arc->startNode.ID, 0)));
	m_arcSecondNodeIDText->setText(QString::fromStdString(toStringPrecision(line->arc->endNode.ID, 0)));
	setTrackText(m_arcTrackIDText, *m_presenter, {line->track});
	m_arcLengthText->setText(QString::fromStdString(toStringPrecision(line->arc->length, 2)));
	m_arcCurvatureText->setText(QString::fromStdString(toStringPrecision(line->arc->curvature, 2)));
	m_arcGradientText->setText(QString::fromStdString(toStringPrecision(line->arc->gradient, 3)));
	m_arcSpeedLimitText->setText(QString::fromStdString(toStringPrecision(line->arc->speedLimit, 2)));
	const auto operationalStateName = [](TrackOperationalState state) {
		switch (state) {
			case TrackOperationalState::Prepared:
				return QStringLiteral("Permissive signalling");
			case TrackOperationalState::Occupied:
				return QStringLiteral("Occupied");
			case TrackOperationalState::Blocked:
				return QStringLiteral("Blocked");
			case TrackOperationalState::Free:
			default:
				return QStringLiteral("Free");
		}
	};
	m_arcOperationalStateText->setText(operationalStateName(line->operationalState()));
	// The sections by their labels, joined by semicolons because a switch label holds a comma, and their ids.
	QStringList sectionIds;
	QStringList sectionLabels;
	const double arcStart = std::min(line->arc->startNode.X, line->arc->endNode.X);
	const double arcEnd = std::max(line->arc->startNode.X, line->arc->endNode.X);
	const auto appendSection = [&sectionIds, &sectionLabels](const std::string& sectionId, const std::string& label) {
		if (sectionId.empty() || sectionIds.contains(QString::fromStdString(sectionId)))
			return;
		sectionIds << QString::fromStdString(sectionId);
		sectionLabels << QString::fromStdString(label.empty() ? sectionId : label);
	};
	const std::string startSection = line->arc->startNode.tdsbId;
	const std::string endSection = line->arc->endNode.tdsbId;
	const auto sectionTouchesArc = [&startSection, &endSection](const std::string& sectionId) {
		return !sectionId.empty() && (sectionId == startSection || sectionId == endSection);
	};
	for (auto* candidate : m_presenter->runSignals()) {
		if (!candidate || candidate->trackID != line->track)
			continue;
		const bool positionMatches = candidate->X >= arcStart - 1e-9 && candidate->X <= arcEnd + 1e-9;
		const bool sectionMatches = sectionTouchesArc(candidate->sectionAheadId)
			|| sectionTouchesArc(candidate->sectionBehindId);
		if (!positionMatches && !sectionMatches)
			continue;
		appendSection(candidate->sectionAheadId, candidate->sectionAheadLabel);
		appendSection(candidate->sectionBehindId, candidate->sectionBehindLabel);
	}
	if (sectionIds.isEmpty())
		setTextWithTip(m_arcConnectedSignalsText, QStringLiteral("None"), QString());
	else
		setTextWithTip(m_arcConnectedSignalsText, sectionLabels.join(QStringLiteral(", ")),
			(sectionIds.size() == 1 ? QStringLiteral("Section ID: %1") : QStringLiteral("Section IDs: %1")).arg(sectionIds.join(QStringLiteral(", "))));

	showForm(m_arcInfoWidget, QStringLiteral("Arc %1").arg(m_arcIDText->text()), line);
}

// shows connection info on dock widget
void InspectorPanel::displayConnectionInfo(ConnectionItem* line) {
	if (!line || !line->connection)
		return;
	handleCloseInfoDockWidget();

	// update connection info displayed on widget
	setTrackText(m_connectionFirstTrackIDText, *m_presenter, {line->connection->idFirstTrackLine});
	setTrackText(m_connectionSecondTrackIDText, *m_presenter, {line->connection->idSecondTrackLine});
	m_connectionXFirstNodeText->setText(QString::fromStdString(toStringPrecision(1000 * line->connection->xFirstNode, 2)));	  // km to m
	m_connectionXSecondNodeText->setText(QString::fromStdString(toStringPrecision(1000 * line->connection->xSecondNode, 2))); // km to m

	showForm(m_connectionInfoWidget, QStringLiteral("Connection"), line);
}

// shows signalling info on dock widget
void InspectorPanel::displaySignallingInfo(SignalItem* signal) {
	if (!signal)
		return;
	handleCloseInfoDockWidget();
	m_presenter->setInspectedSignal(signal);

	// update signalling info displayed on widget
	setTrackText(m_signallingTrackIDText, *m_presenter, {signal->trackID});
	m_signallingXText->setText(QString::fromStdString(toStringPrecision(1000 * signal->X, 2))); // km to m
	m_signallingAspectText->setText(signalAspectLabel(signal));
	m_signallingGroupDetails->setPlainText(signal->toolTip());
	const bool hasAhead = !signal->sectionAheadId.empty();
	const int nextTrack = signal->sectionAheadTrackId >= 0
		? signal->sectionAheadTrackId
		: signal->sectionBehindTrackId;
	setSectionText(m_signallingProtectedSectionText, hasAhead ? signal->sectionAheadId : signal->sectionBehindId,
		hasAhead ? signal->sectionAheadLabel : signal->sectionBehindLabel);
	if (nextTrack < 0)
		setTextWithTip(m_signallingNextTrackText, QStringLiteral("None"), QString());
	else
		setTrackText(m_signallingNextTrackText, *m_presenter, {nextTrack});

	// check if section ahead exists
	setSectionText(m_signallingIDSectionAheadText, signal->sectionAheadId, signal->sectionAheadLabel);
	m_signallingLengthSectionAheadText->setText(hasAhead
			? QString::fromStdString(toStringPrecision(1000 * signal->sectionAheadLength, 2)) // km to m
			: QStringLiteral("None"));

	showForm(m_signallingInfoWidget, QStringLiteral("Signal"), signal);
	m_host.signalInspected();
}

void InspectorPanel::displayTrainDetails(TrainBodyItem* trainItem, bool changeFollowMode) {
	if (!trainItem)
		return;
	// update train info displayed on widget
	TrainItemGroup* groupItem = qgraphicsitem_cast<TrainItemGroup*>(trainItem->parentItem());
	if (!groupItem)
		return;
	handleCloseInfoDockWidget();
	m_selectedTrainIndex = trainItem->index;
	m_trainNameText->setText(QString::fromStdString(followTrainName(groupItem->trainDescription, groupItem->index)));
	m_trainIDText->setText(QString::fromStdString(toStringPrecision(groupItem->trainId, 0)));
	m_trainTypeText->setText(QString::fromStdString(groupItem->trainType));
	m_trainLengthText->setText(QString::fromStdString(toStringPrecision(groupItem->trainLength, 0)));
	m_trainWagonsText->setText(QString::fromStdString(toStringPrecision(groupItem->wagonCount, 0)));

	// show pax info only while its layer is enabled
	if (m_host.passengerGui() && m_presenter->passengerLayerVisible())
		m_presenter->paintTrainPassengerInfo(groupItem);

	showForm(m_trainInfoWidget, QStringLiteral("Train %1").arg(m_trainNameText->text()), nullptr);
	if (changeFollowMode)
		m_host.followTrain(trainItem->index);
	m_host.inspectionChanged();

	// effect on entire train
	m_presenter->setHighlight(groupItem);
}

// shows train info on dock widget
void InspectorPanel::displayTrainInfo(TrainBodyItem* trainItem) {
	displayTrainDetails(trainItem, true);
}

// show text icon on top of passenger icon
void InspectorPanel::displayPassengerInfo(PassengerItem* paxItem) {
	if (!paxItem)
		return;
	handleCloseInfoDockWidget();
	m_infoDockWidget->hide();

	// show pax info
	m_presenter->paintPassengerInfoIcon(paxItem, m_host.passengerState(paxItem->passengerId), m_host.replayActive());

	showForm(nullptr, QString(), paxItem);
}

void InspectorPanel::handleDisableHighlight() {
	handleCloseInfoDockWidget();
	if (m_infoDockWidget)
		m_infoDockWidget->hide();
}

// double to string with precision
std::string InspectorPanel::toStringPrecision(double value, int precision) {
	std::ostringstream out;
	out.precision(precision);
	out << std::fixed << value;
	return out.str();
}
