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

#include "app/GuiSimulationSnapshot.h"
#include "graphics/NetworkPresenter.h"
#include "graphics/NetworkScene.h"
#include "graphics/items/ConnectionItem.h"
#include "graphics/items/HighlightEffect.h"
#include "graphics/items/NodeItem.h"
#include "graphics/items/PassengerItem.h"
#include "graphics/items/SignalItem.h"
#include "graphics/items/StationNodeItem.h"
#include "graphics/items/StationOverlayItem.h"
#include "graphics/items/TrackLineItem.h"
#include "graphics/items/TrainBodyItem.h"
#include "graphics/items/TrainItemGroup.h"
#include "scene/TrackPreview.h"
#include "widgets/InfoDockWidget.h"
#include "widgets/PlainToolTip.h"

#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QStringList>
#include <QTextOption>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <sstream>

InspectorPanel::InspectorPanel(QMainWindow* window, QGraphicsScene* scene, NetworkPresenter& presenter, Host& host)
	: QObject(window), m_scene(scene), m_presenter(&presenter), m_host(host) {
	// main widget
	auto* infoWidget = new QWidget();

	// Arc info widget
	arcInfoWidget = new QWidget();
	arcIDText = new QLineEdit(arcInfoWidget);
	arcFirstNodeIDText = new QLineEdit(arcInfoWidget);
	arcSecondNodeIDText = new QLineEdit(arcInfoWidget);
	arcTrackIDText = new QLineEdit(arcInfoWidget);
	arcLengthText = new QLineEdit(arcInfoWidget);
	arcCurvatureText = new QLineEdit(arcInfoWidget);
	arcGradientText = new QLineEdit(arcInfoWidget);
	arcSpeedLimitText = new QLineEdit(arcInfoWidget);
	arcOperationalStateText = new QLineEdit(arcInfoWidget);
	arcOperationalStateText->setObjectName("arcOperationalStateText");
	arcConnectedSignalsText = new QLineEdit(arcInfoWidget);
	arcConnectedSignalsText->setObjectName("arcConnectedSignalsText");
	auto* arcFormLayout = new QFormLayout();
	arcFormLayout->addRow("Arc ID", arcIDText);
	arcFormLayout->addRow("First Node ID", arcFirstNodeIDText);
	arcFormLayout->addRow("Second Node ID", arcSecondNodeIDText);
	arcFormLayout->addRow("Track ID", arcTrackIDText);
	arcFormLayout->addRow("Length (m)", arcLengthText);
	arcFormLayout->addRow("Curvature (m)", arcCurvatureText);
	arcFormLayout->addRow("Gradient (rise/length)", arcGradientText);
	arcFormLayout->addRow("Speed Limit (m/s)", arcSpeedLimitText);
	arcFormLayout->addRow("Operational state", arcOperationalStateText);
	arcFormLayout->addRow("Connected signals", arcConnectedSignalsText);
	arcInfoWidget->setLayout(arcFormLayout);

	// Node info widget
	nodeInfoWidget = new QWidget();
	nodeIDText = new QLineEdit(nodeInfoWidget);
	nodeTrackIDText = new QLineEdit(nodeInfoWidget);
	nodeXText = new QLineEdit(nodeInfoWidget);
	nodeYText = new QLineEdit(nodeInfoWidget);
	nodeStationNameText = new QLineEdit(nodeInfoWidget);
	nodeStationNameText->setObjectName("nodeStationNameText");
	nodeRegionText = new QLineEdit(nodeInfoWidget);
	nodeRegionText->setObjectName("nodeRegionText");
	nodeConnectedTracksText = new QLineEdit(nodeInfoWidget);
	nodeConnectedTracksText->setObjectName("nodeConnectedTracksText");
	nodeSignalledText = new QLineEdit(nodeInfoWidget);
	nodeSignalledText->setObjectName("nodeSignalledText");
	auto* nodeFormLayout = new QFormLayout();
	nodeFormLayout->addRow("Node ID", nodeIDText);
	nodeFormLayout->addRow("Track ID", nodeTrackIDText);
	nodeFormLayout->addRow("X (m)", nodeXText);
	nodeFormLayout->addRow("Y (m)", nodeYText);
	nodeFormLayout->addRow("Name", nodeStationNameText);
	nodeFormLayout->addRow("Region", nodeRegionText);
	nodeFormLayout->addRow("Connected tracks", nodeConnectedTracksText);
	nodeFormLayout->addRow("Signalled", nodeSignalledText);
	nodeInfoWidget->setLayout(nodeFormLayout);

	// connection info widget
	connectionInfoWidget = new QWidget();
	connectionFirstTrackIDText = new QLineEdit(connectionInfoWidget);
	connectionSecondTrackIDText = new QLineEdit(connectionInfoWidget);
	connectionXFirstNodeText = new QLineEdit(connectionInfoWidget);
	connectionXSecondNodeText = new QLineEdit(connectionInfoWidget);
	auto* connectionFormLayout = new QFormLayout();
	connectionFormLayout->addRow("First Track ID", connectionFirstTrackIDText);
	connectionFormLayout->addRow("Second Track ID", connectionSecondTrackIDText);
	connectionFormLayout->addRow("X First Node (m)", connectionXFirstNodeText);
	connectionFormLayout->addRow("X Second Node (m)", connectionXSecondNodeText);
	connectionInfoWidget->setLayout(connectionFormLayout);

	// signalling info widget
	signallingInfoWidget = new QWidget();
	signallingTrackIDText = new QLineEdit(signallingInfoWidget);
	signallingXText = new QLineEdit(signallingInfoWidget);
	signallingIDSectionAheadText = new QLineEdit(signallingInfoWidget);
	signallingLengthSectionAheadText = new QLineEdit(signallingInfoWidget);
	signallingAspectText = new QLineEdit(signallingInfoWidget);
	signallingAspectText->setObjectName("signallingAspectText");
	signallingProtectedSectionText = new QLineEdit(signallingInfoWidget);
	signallingProtectedSectionText->setObjectName("signallingProtectedSectionText");
	signallingNextTrackText = new QLineEdit(signallingInfoWidget);
	signallingNextTrackText->setObjectName("signallingNextTrackText");
	signallingGroupDetails = new QPlainTextEdit(signallingInfoWidget);
	signallingGroupDetails->setObjectName("signallingGroupDetails");
	signallingGroupDetails->setReadOnly(true);
	signallingGroupDetails->setMaximumHeight(120);
	signallingGroupDetails->setLineWrapMode(QPlainTextEdit::WidgetWidth);
	signallingGroupDetails->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
	auto* signallingFormLayout = new QFormLayout();
	signallingFormLayout->addRow("Track ID", signallingTrackIDText);
	signallingFormLayout->addRow("X (m)", signallingXText);
	signallingFormLayout->addRow("ID of Block Section", signallingIDSectionAheadText);
	signallingFormLayout->addRow("Length of Block Section (m)", signallingLengthSectionAheadText);
	signallingFormLayout->addRow("Aspect", signallingAspectText);
	signallingFormLayout->addRow("Protected section", signallingProtectedSectionText);
	signallingFormLayout->addRow("Next track", signallingNextTrackText);
	// The label has its own row and the box spans the form, so that a narrow dock leaves the box its width.
	auto* signallingGroupLabel = new QLabel("Signals at location", signallingInfoWidget);
	signallingGroupLabel->setBuddy(signallingGroupDetails);
	signallingFormLayout->addRow(signallingGroupLabel);
	signallingFormLayout->addRow(signallingGroupDetails);
	signallingInfoWidget->setLayout(signallingFormLayout);

	// train info widget
	trainInfoWidget = new QWidget();
	trainNameText = new QLineEdit(trainInfoWidget);
	trainNameText->setObjectName("trainNameText");
	trainIDText = new QLineEdit(trainInfoWidget);
	trainTypeText = new QLineEdit(trainInfoWidget);
	trainLengthText = new QLineEdit(trainInfoWidget);
	trainWagonsText = new QLineEdit(trainInfoWidget);
	auto* trainFormLayout = new QFormLayout();
	trainFormLayout->addRow("Train", trainNameText);
	trainFormLayout->addRow("ID", trainIDText);
	trainFormLayout->addRow("Type", trainTypeText);
	trainFormLayout->addRow("Length (m)", trainLengthText);
	trainFormLayout->addRow("Number of wagons", trainWagonsText);
	trainInfoWidget->setLayout(trainFormLayout);

	// main layout
	auto* infoWidgetMainLayout = new QVBoxLayout();
	infoWidgetMainLayout->addWidget(arcInfoWidget);
	infoWidgetMainLayout->addWidget(nodeInfoWidget);
	infoWidgetMainLayout->addWidget(connectionInfoWidget);
	infoWidgetMainLayout->addWidget(signallingInfoWidget);
	infoWidgetMainLayout->addWidget(trainInfoWidget);
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
	infoDockWidget = new InfoDockWidget("Simulation Info");
	auto* infoScroll = new QScrollArea(infoDockWidget);
	infoScroll->setWidgetResizable(true);
	infoScroll->setFrameShape(QFrame::NoFrame);
	infoScroll->setMinimumSize(300, 0);
	// Tab reaches the fields inside, and the area scrolls to show them.
	infoScroll->setFocusPolicy(Qt::NoFocus);
	infoScroll->setWidget(infoWidget);
	infoDockWidget->setWidget(infoScroll);
	infoDockWidget->setAllowedAreas(Qt::RightDockWidgetArea);
	window->addDockWidget(Qt::RightDockWidgetArea, infoDockWidget);

	// hide widgets
	infoDockWidget->hide();
	arcInfoWidget->hide();
	nodeInfoWidget->hide();
	connectionInfoWidget->hide();
	signallingInfoWidget->hide();
	trainInfoWidget->hide();

	connect(infoDockWidget, &InfoDockWidget::closed, this, &InspectorPanel::handleCloseInfoDockWidget);
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
	if (signallingInfoWidget && signallingInfoWidget->isVisible() && signallingGroupDetails
		&& m_presenter->m_inspectedSignal && m_presenter->m_signalDecorations.contains(m_presenter->m_inspectedSignal)) {
		const QString details = m_presenter->m_inspectedSignal->toolTip();
		if (signallingGroupDetails->toPlainText() != details) {
			const int scroll = signallingGroupDetails->verticalScrollBar()->value();
			signallingGroupDetails->setPlainText(details);
			signallingGroupDetails->verticalScrollBar()->setValue(scroll);
		}
		signallingAspectText->setText(signalAspectLabel(m_presenter->m_inspectedSignal));
	}
}

// hides all widgets from the dock widget
// removes highlight from last clicked item
void InspectorPanel::handleCloseInfoDockWidget() {
	m_previewSelectedStationId.clear();
	m_presenter->m_inspectedSignal = nullptr;
	m_selectedStationName.clear();
	m_hasSelectedStationIdentity = false;
	m_selectedStationNodeId = 0.0;
	m_selectedStationTrack = -1;
	m_selectedTrainIndex = -1;
	for (auto* overlay : m_presenter->m_stationOverlays)
		if (overlay)
			overlay->setSelected(false);

	// hide all widgets
	arcInfoWidget->hide();
	nodeInfoWidget->hide();
	connectionInfoWidget->hide();
	signallingInfoWidget->hide();
	trainInfoWidget->hide();
	m_presenter->removeTrainPaxInfoIcon();
	m_presenter->trainPaxItem = nullptr; // Explicitly dismissed or replaced train inspection.
	m_presenter->removePaxInfoIcon();

	// remove highlight
	if (m_presenter->effect) {
		delete m_presenter->effect;
		m_presenter->effect = nullptr;
	}
	m_host.inspectionChanged();
}

void InspectorPanel::clearPreviewInspection() {
	m_host.closeSceneContextMenu();
	if (m_host.showingTrackPreview()) {
		handleCloseInfoDockWidget();
		infoDockWidget->hide();
		for (auto* field : {nodeIDText, nodeXText, nodeYText, nodeTrackIDText,
				 nodeStationNameText, nodeRegionText, nodeConnectedTracksText, nodeSignalledText,
				 arcIDText, arcFirstNodeIDText, arcSecondNodeIDText, arcTrackIDText,
				 arcLengthText, arcCurvatureText, arcGradientText, arcSpeedLimitText,
				 arcOperationalStateText, arcConnectedSignalsText, connectionFirstTrackIDText,
				 connectionSecondTrackIDText, connectionXFirstNodeText, connectionXSecondNodeText,
				 signallingTrackIDText, signallingXText, signallingAspectText,
				 signallingProtectedSectionText, signallingNextTrackText,
				 signallingIDSectionAheadText, signallingLengthSectionAheadText})
			field->clear();
		signallingGroupDetails->clear();
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
	const QString unavailable = QStringLiteral("Unavailable before Run");
	// Resolve canonical values at interaction; no pointers survive an edit/rebuild.
	clearPreviewInspection();
	QString title;
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
			for (const auto& anchor : m_presenter->m_cachedTrackPreview.stations)
				if (anchor.id == id)
					for (const auto& candidate : model.nodes)
						if (!anchor.nodeId.empty() && candidate.id == anchor.nodeId)
							node = &candidate;
		} else if (!node) {
			return;
		}
		nodeIDText->setText(node ? text(node->id) : QStringLiteral("No node anchor"));
		nodeXText->setText(node					  ? number(node->xKm * 1000)
				: station && station->hasPosition ? number(station->positionKm * 1000)
												  : unavailable);
		nodeYText->setText(node ? number(node->yKm * 1000) : unavailable);
		nodeTrackIDText->setText(node ? text(node->trackId) : QStringLiteral("No node anchor"));
		nodeStationNameText->setText(station
				? text(station->name.empty() ? station->id : station->name) + " [" + text(station->id) + "]"
				: QString());
		nodeRegionText->setText(unavailable);
		nodeSignalledText->setText(unavailable);
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
		nodeConnectedTracksText->setText(connectedTracks.join(", "));
		if (station)
			m_previewSelectedStationId = text(station->id);
		title = kind == "station" ? "Station " + text(id) : "Node " + text(id);
		if (kind != "station" && membership != memberships.end() && !membership->second.platformId.empty())
			title += " / platform " + text(membership->second.platformId);
		nodeInfoWidget->show();
	} else if (kind == "arc" || kind == "track") {
		if (kind == "arc") {
			const auto arc = std::find_if(model.arcs.begin(), model.arcs.end(),
				[&](const SceneArc& value) { return value.id == id; });
			if (arc == model.arcs.end())
				return;
			arcIDText->setText(text(arc->id));
			arcFirstNodeIDText->setText(text(arc->fromNodeId));
			arcSecondNodeIDText->setText(text(arc->toNodeId));
			arcTrackIDText->setText(text(arc->trackId));
			const SceneNode* from = nullptr;
			const SceneNode* to = nullptr;
			for (const auto& node : model.nodes) {
				if (node.id == arc->fromNodeId) from = &node;
				if (node.id == arc->toNodeId) to = &node;
			}
			arcLengthText->setText(from && to
					? number(std::hypot(to->xKm - from->xKm, to->yKm - from->yKm) * 1000)
					: unavailable);
			arcCurvatureText->setText(number(arc->curvatureRadiusM));
			arcGradientText->setText(number(arc->gradientPercent));
			arcSpeedLimitText->setText(number(arc->speedLimitMs));
		} else {
			if (std::none_of(model.tracks.begin(), model.tracks.end(),
					[&](const SceneTrack& track) { return track.id == id; }))
				return;
			arcIDText->setText("No authored arc (fallback)");
			arcTrackIDText->setText(text(id));
		}
		arcOperationalStateText->setText(unavailable);
		arcConnectedSignalsText->setText(unavailable);
		title = kind == "arc" ? "Arc " + text(id) : "Track " + text(id);
		arcInfoWidget->show();
	} else if (kind == "connection") {
		const auto connection = std::find_if(model.connections.begin(), model.connections.end(),
			[&](const SceneConnection& value) { return value.id == id; });
		if (connection == model.connections.end())
			return;
		for (const auto& node : model.nodes) {
			if (node.id == connection->fromNodeId) {
				connectionFirstTrackIDText->setText(text(node.trackId));
				connectionXFirstNodeText->setText(number(node.xKm * 1000));
			}
			if (node.id == connection->toNodeId) {
				connectionSecondTrackIDText->setText(text(node.trackId));
				connectionXSecondNodeText->setText(number(node.xKm * 1000));
			}
		}
		title = "Connection " + text(id);
		connectionInfoWidget->show();
	} else if (kind == "signal") {
		const std::string track = item->data(PreviewGraphics::TrackId).toString().toStdString();
		const auto head = std::find_if(m_presenter->m_cachedTrackPreview.previewSignals.begin(), m_presenter->m_cachedTrackPreview.previewSignals.end(),
			[&](const TrackPreviewSignal& signal) { return signal.trackId == track && signal.sectionId == id; });
		if (head == m_presenter->m_cachedTrackPreview.previewSignals.end())
			return;
		signallingTrackIDText->setText(text(track));
		signallingXText->setText(number(head->rawX * 1000));
		signallingAspectText->setText(unavailable);
		signallingProtectedSectionText->setText(text(id));
		signallingNextTrackText->setText(unavailable);
		signallingIDSectionAheadText->setText(text(id));
		signallingLengthSectionAheadText->setText(unavailable);
		QStringList ids;
		for (const auto& signal : sceneSignals(model))
			if (signal.protectedSection == id)
				ids << text(signal.id);
		signallingGroupDetails->setPlainText(QStringLiteral("Track %1; section %2; %3; authored signal IDs: %4")
				.arg(text(track), text(id), item->data(PreviewGraphics::Reversed).toBool() ? "reverse" : "forward",
					ids.isEmpty() ? "None" : ids.join(", ")));
		title = "Signal " + text(id);
		signallingInfoWidget->show();
	} else {
		return;
	}
	infoDockWidget->setWindowTitle("Preview: " + title + " (not running)");
	infoDockWidget->show();

	// effect on clicked item
	if (!m_presenter->effect) {
		m_presenter->effect = new HighlightEffect(Qt::blue, 1);
	}
	item->setGraphicsEffect(m_presenter->effect);
	m_host.inspectionChanged();
	// Do not recolor boundary dots/squares or neutral signal heads on inspection.
}

// shows Node info on dock widget
void InspectorPanel::displayNodeInfo(NodeItem* el) {
	if (!el || !el->node)
		return;
	handleCloseInfoDockWidget();

	// update Node info displayed on widget
	nodeIDText->setText(QString::fromStdString(to_string_precision(el->node->ID, 0)));
	nodeXText->setText(QString::fromStdString(to_string_precision(1000 * el->node->X, 2))); // km to m
	nodeYText->setText(QString::fromStdString(to_string_precision(1000 * el->node->Y, 2))); // km to m
	nodeTrackIDText->setText(QString::fromStdString(to_string_precision(el->track, 0)));
	nodeStationNameText->clear();
	nodeRegionText->clear();
	nodeConnectedTracksText->clear();
	nodeSignalledText->clear();

	// hide other widgets
	arcInfoWidget->hide();
	connectionInfoWidget->hide();
	signallingInfoWidget->hide();
	trainInfoWidget->hide();
	m_presenter->removeTrainPaxInfoIcon();
	m_presenter->removePaxInfoIcon();

	// show widget
	infoDockWidget->setWindowTitle("Node Info");
	infoDockWidget->show();
	nodeInfoWidget->show();

	// effect on clicked item
	if (!m_presenter->effect) {
		m_presenter->effect = new HighlightEffect(Qt::blue, 1);
	}
	el->setGraphicsEffect(m_presenter->effect);
}

// shows station Node info on dock widget
void InspectorPanel::displayStationNodeInfo(StationNodeItem* re) {
	if (!re || !re->node)
		return;
	handleCloseInfoDockWidget();
	m_selectedStationName = QString::fromStdString(re->node->stationName);
	m_hasSelectedStationIdentity = true;
	m_selectedStationNodeId = re->node->ID;
	m_selectedStationTrack = re->track;
	for (auto* overlay : m_presenter->m_stationOverlays)
		if (overlay)
			overlay->setSelected(overlay->hasSourceIdentity()
					? overlay->matchesSourceIdentity(m_selectedStationNodeId, m_selectedStationTrack)
					: !m_hasSelectedStationIdentity && overlay->stationName() == m_selectedStationName);
	m_host.inspectionChanged();

	// update Node info displayed on widget
	nodeIDText->setText(QString::fromStdString(to_string_precision(re->node->ID, 0)));
	nodeXText->setText(QString::fromStdString(to_string_precision(1000 * re->node->X, 2))); // km to m
	nodeYText->setText(QString::fromStdString(to_string_precision(1000 * re->node->Y, 2))); // km to m
	nodeTrackIDText->setText(QString::fromStdString(to_string_precision(re->track, 0)));
	nodeStationNameText->setText(QString::fromStdString(re->node->stationName));
	if (re->track >= 0 && re->track < 268)
		nodeRegionText->setText(QString::number(m_host.regionOfTrack(re->track)));
	else
		nodeRegionText->setText("None");
	QStringList connectedTracks;
	if (re->track >= 0 && re->track < 268)
		connectedTracks << QString::number(re->track);
	const int connectionCount = std::max(0, std::min(re->node->numConnections, 6));
	for (int i = 0; i < connectionCount; ++i) {
		const int connectedTrack = re->node->connectIdBlockSet[i];
		if (connectedTrack >= 0 && connectedTrack < 268
			&& !connectedTracks.contains(QString::number(connectedTrack)))
			connectedTracks << QString::number(connectedTrack);
	}
	nodeConnectedTracksText->setText(connectedTracks.join(", "));
	nodeSignalledText->setText(re->node->isSignalled ? "Yes" : "No");

	// hide other widgets
	arcInfoWidget->hide();
	connectionInfoWidget->hide();
	signallingInfoWidget->hide();
	trainInfoWidget->hide();
	m_presenter->removeTrainPaxInfoIcon();
	m_presenter->removePaxInfoIcon();

	// show widget
	infoDockWidget->setWindowTitle("Station Node Info");
	infoDockWidget->show();
	nodeInfoWidget->show();

	// effect on clicked item
	if (!m_presenter->effect) {
		m_presenter->effect = new HighlightEffect(Qt::blue, 1);
	}
	re->setGraphicsEffect(m_presenter->effect);
}

// shows Arc info on dock widget
void InspectorPanel::displayArcInfo(TrackLineItem* line) {
	if (!line || !line->arc)
		return;
	handleCloseInfoDockWidget();

	// update Arc info displayed on widget
	arcIDText->setText(QString::fromStdString(to_string_precision(line->arc->ID, 0)));
	arcFirstNodeIDText->setText(QString::fromStdString(to_string_precision(line->arc->startNode.ID, 0)));
	arcSecondNodeIDText->setText(QString::fromStdString(to_string_precision(line->arc->endNode.ID, 0)));
	arcTrackIDText->setText(QString::fromStdString(to_string_precision(line->track, 0)));
	arcLengthText->setText(QString::fromStdString(to_string_precision(line->arc->length, 2)));
	arcCurvatureText->setText(QString::fromStdString(to_string_precision(line->arc->curvature, 2)));
	arcGradientText->setText(QString::fromStdString(to_string_precision(line->arc->gradient, 3)));
	arcSpeedLimitText->setText(QString::fromStdString(to_string_precision(line->arc->speedLimit, 2)));
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
	arcOperationalStateText->setText(operationalStateName(line->operationalState()));
	QStringList sectionIds;
	const double arcStart = std::min(line->arc->startNode.X, line->arc->endNode.X);
	const double arcEnd = std::max(line->arc->startNode.X, line->arc->endNode.X);
	const auto appendSection = [&sectionIds](const std::string& sectionId) {
		if (!sectionId.empty() && !sectionIds.contains(QString::fromStdString(sectionId)))
			sectionIds << QString::fromStdString(sectionId);
	};
	const std::string startSection = line->arc->startNode.tdsbId;
	const std::string endSection = line->arc->endNode.tdsbId;
	const auto sectionTouchesArc = [&startSection, &endSection](const std::string& sectionId) {
		return !sectionId.empty() && (sectionId == startSection || sectionId == endSection);
	};
	for (auto* candidate : m_presenter->allSignals) {
		if (!candidate || candidate->trackID != line->track)
			continue;
		const bool positionMatches = candidate->X >= arcStart - 1e-9 && candidate->X <= arcEnd + 1e-9;
		const bool sectionMatches = sectionTouchesArc(candidate->sectionAheadId)
			|| sectionTouchesArc(candidate->sectionBehindId);
		if (!positionMatches && !sectionMatches)
			continue;
		appendSection(candidate->sectionAheadId);
		appendSection(candidate->sectionBehindId);
	}
	arcConnectedSignalsText->setText(sectionIds.isEmpty() ? QStringLiteral("None") : sectionIds.join(", "));

	// hide other widgets
	nodeInfoWidget->hide();
	connectionInfoWidget->hide();
	signallingInfoWidget->hide();
	trainInfoWidget->hide();
	m_presenter->removeTrainPaxInfoIcon();
	m_presenter->removePaxInfoIcon();

	// show widget
	infoDockWidget->setWindowTitle("Arc Info");
	infoDockWidget->show();
	arcInfoWidget->show();

	// effect on clicked item
	if (!m_presenter->effect) {
		m_presenter->effect = new HighlightEffect(Qt::blue, 1);
	}
	line->setGraphicsEffect(m_presenter->effect);
}

// shows connection info on dock widget
void InspectorPanel::displayConnectionInfo(ConnectionItem* line) {
	if (!line || !line->connection)
		return;
	handleCloseInfoDockWidget();

	// update connection info displayed on widget
	connectionFirstTrackIDText->setText(QString::fromStdString(to_string_precision(line->connection->idFirstTrackLine, 0)));
	connectionSecondTrackIDText->setText(QString::fromStdString(to_string_precision(line->connection->idSecondTrackLine, 0)));
	connectionXFirstNodeText->setText(QString::fromStdString(to_string_precision(1000 * line->connection->xFirstNode, 2)));	  // km to m
	connectionXSecondNodeText->setText(QString::fromStdString(to_string_precision(1000 * line->connection->xSecondNode, 2))); // km to m

	// hide other widgets
	arcInfoWidget->hide();
	nodeInfoWidget->hide();
	signallingInfoWidget->hide();
	trainInfoWidget->hide();
	m_presenter->removeTrainPaxInfoIcon();
	m_presenter->removePaxInfoIcon();

	// show widget
	infoDockWidget->setWindowTitle("Connection Info");
	infoDockWidget->show();
	connectionInfoWidget->show();

	// effect on clicked item
	if (!m_presenter->effect) {
		m_presenter->effect = new HighlightEffect(Qt::blue, 1);
	}
	line->setGraphicsEffect(m_presenter->effect);
}

// shows signalling info on dock widget
void InspectorPanel::displaySignallingInfo(SignalItem* signal) {
	if (!signal)
		return;
	handleCloseInfoDockWidget();
	m_presenter->m_inspectedSignal = signal;

	// update signalling info displayed on widget
	signallingTrackIDText->setText(QString::fromStdString(to_string_precision(signal->trackID, 0)));
	signallingXText->setText(QString::fromStdString(to_string_precision(1000 * signal->X, 2))); // km to m
	signallingAspectText->setText(signalAspectLabel(signal));
	signallingGroupDetails->setPlainText(signal->toolTip());
	const std::string protectedSection = !signal->sectionAheadId.empty()
		? signal->sectionAheadId
		: signal->sectionBehindId;
	const int nextTrack = signal->sectionAheadTrackId >= 0
		? signal->sectionAheadTrackId
		: signal->sectionBehindTrackId;
	signallingProtectedSectionText->setText(protectedSection.empty()
			? QStringLiteral("None")
			: QString::fromStdString(protectedSection));
	signallingNextTrackText->setText(nextTrack < 0 ? QStringLiteral("None") : QString::number(nextTrack));

	// check if section ahead exists
	if (!signal->sectionAheadId.empty()) {
		signallingIDSectionAheadText->setText(QString::fromStdString(signal->sectionAheadId));
		signallingLengthSectionAheadText->setText(QString::fromStdString(to_string_precision(1000 * signal->sectionAheadLength, 2))); // km to m
	} else {
		signallingIDSectionAheadText->setText("None");
		signallingLengthSectionAheadText->setText("None");
	}

	// hide other widgets
	arcInfoWidget->hide();
	connectionInfoWidget->hide();
	nodeInfoWidget->hide();
	trainInfoWidget->hide();
	m_presenter->removeTrainPaxInfoIcon();
	m_presenter->removePaxInfoIcon();

	// show widget
	infoDockWidget->setWindowTitle("Signalling Info");
	infoDockWidget->show();
	signallingInfoWidget->show();

	// effect on clicked item
	if (!m_presenter->effect) {
		m_presenter->effect = new HighlightEffect(Qt::blue, 1);
	}
	signal->setGraphicsEffect(m_presenter->effect);
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
	trainNameText->setText(QString::fromStdString(followTrainName(groupItem->trainDescription, groupItem->index)));
	trainIDText->setText(QString::fromStdString(to_string_precision(groupItem->trainId, 0)));
	trainTypeText->setText(QString::fromStdString(groupItem->trainType));
	trainLengthText->setText(QString::fromStdString(to_string_precision(groupItem->trainLength, 0)));
	trainWagonsText->setText(QString::fromStdString(to_string_precision(groupItem->wagonCount, 0)));

	// hide other widgets
	arcInfoWidget->hide();
	connectionInfoWidget->hide();
	signallingInfoWidget->hide();
	nodeInfoWidget->hide();
	m_presenter->removeTrainPaxInfoIcon();
	m_presenter->removePaxInfoIcon();

	// show pax info only while its layer is enabled
	if (m_host.passengerGui() && m_presenter->m_passengerLayerVisible)
		m_presenter->paintTrainPassengerInfo(groupItem);

	// show widget
	infoDockWidget->setWindowTitle("Train Info");
	infoDockWidget->show();
	trainInfoWidget->show();
	if (changeFollowMode)
		m_host.followTrain(trainItem->index);
	m_host.inspectionChanged();

	// effect on clicked item
	if (!m_presenter->effect) {
		m_presenter->effect = new HighlightEffect(Qt::blue, 1);
	}
	if (trainItem->parentItem()) {
		trainItem->parentItem()->setGraphicsEffect(m_presenter->effect);
	} // effect on entire train
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
	// hide other widgets
	infoDockWidget->hide();
	arcInfoWidget->hide();
	nodeInfoWidget->hide();
	connectionInfoWidget->hide();
	signallingInfoWidget->hide();
	trainInfoWidget->hide();
	m_presenter->removeTrainPaxInfoIcon();
	m_presenter->removePaxInfoIcon();

	// show pax info
	m_presenter->paintPassengerInfoIcon(paxItem, m_host.passengerState(paxItem->passengerId), m_host.replayActive());

	// effect on clicked item
	if (!m_presenter->effect) {
		m_presenter->effect = new HighlightEffect(Qt::blue, 1);
	}
	paxItem->setGraphicsEffect(m_presenter->effect);
}

void InspectorPanel::handleDisableHighlight() {
	handleCloseInfoDockWidget();
	if (infoDockWidget)
		infoDockWidget->hide();
}

// double to string with precision
std::string InspectorPanel::to_string_precision(double value, int precision) {
	std::ostringstream out;
	out.precision(precision);
	out << std::fixed << value;
	return out.str();
}
