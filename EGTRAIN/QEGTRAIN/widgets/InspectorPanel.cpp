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
	arcFormLayout->addRow("Arc", arcIDText);
	arcFormLayout->addRow("From node", arcFirstNodeIDText);
	arcFormLayout->addRow("To node", arcSecondNodeIDText);
	arcFormLayout->addRow("Track", arcTrackIDText);
	arcFormLayout->addRow("Length (m)", arcLengthText);
	arcFormLayout->addRow("Curve radius (m)", arcCurvatureText);
	arcFormLayout->addRow("Gradient (rise/length)", arcGradientText);
	arcFormLayout->addRow("Speed limit (m/s)", arcSpeedLimitText);
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
	nodeFormLayout->addRow("Node", nodeIDText);
	nodeFormLayout->addRow("Track", nodeTrackIDText);
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
	connectionFormLayout->addRow("First track", connectionFirstTrackIDText);
	connectionFormLayout->addRow("Second track", connectionSecondTrackIDText);
	connectionFormLayout->addRow("First node X (m)", connectionXFirstNodeText);
	connectionFormLayout->addRow("Second node X (m)", connectionXSecondNodeText);
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
	signallingFormLayout->addRow("Track", signallingTrackIDText);
	signallingFormLayout->addRow("X (m)", signallingXText);
	signallingFormLayout->addRow("Block section", signallingIDSectionAheadText);
	signallingFormLayout->addRow("Block section length (m)", signallingLengthSectionAheadText);
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
	trainFormLayout->addRow("Wagons", trainWagonsText);
	trainInfoWidget->setLayout(trainFormLayout);

	// The button under the form of a signal or a train that adds an incident for it.
	incidentButton = new QPushButton(infoWidget);
	incidentButton->setObjectName("inspectorIncidentButton");
	connect(incidentButton, &QPushButton::clicked, this, &InspectorPanel::incidentRequested);

	// main layout
	auto* infoWidgetMainLayout = new QVBoxLayout();
	infoWidgetMainLayout->addWidget(arcInfoWidget);
	infoWidgetMainLayout->addWidget(nodeInfoWidget);
	infoWidgetMainLayout->addWidget(connectionInfoWidget);
	infoWidgetMainLayout->addWidget(signallingInfoWidget);
	infoWidgetMainLayout->addWidget(trainInfoWidget);
	infoWidgetMainLayout->addWidget(incidentButton, 0, Qt::AlignLeft);
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
	infoDockWidget = new InfoDockWidget("Details");
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
	incidentButton->hide();

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
	m_previewSelectionKind.clear();
	m_previewSelectionId.clear();
	m_presenter->m_inspectedSignal = nullptr;
	// Only a preview item marks the fields that need a run.
	for (auto* field : infoDockWidget->findChildren<QLineEdit*>())
		field->setPlaceholderText(QString());
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
	incidentButton->hide();
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

void InspectorPanel::showIncidentAction(const QString& text, const QString& blocked) {
	incidentButton->setText(text);
	incidentButton->setAccessibleName(text);
	incidentButton->setEnabled(blocked.isEmpty());
	incidentButton->setToolTip(blocked);
	incidentButton->setVisible(!text.isEmpty());
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
	// A field that only a run fills stays empty, with a placeholder that says so.
	const auto afterRun = [](QLineEdit* field) {
		field->clear();
		field->setPlaceholderText(QStringLiteral("After a run"));
	};
	// Resolve the values of the case at interaction; no pointers survive an edit/rebuild.
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
		nodeIDText->setText(node ? text(node->id) : QStringLiteral("None"));
		if (node)
			nodeXText->setText(number(node->xKm * 1000));
		else if (station && station->hasPosition)
			nodeXText->setText(number(station->positionKm * 1000));
		else
			afterRun(nodeXText);
		if (node)
			nodeYText->setText(number(node->yKm * 1000));
		else
			afterRun(nodeYText);
		nodeTrackIDText->setText(node ? text(node->trackId) : QStringLiteral("None"));
		nodeStationNameText->setText(station
				? text(station->name.empty() ? station->id : station->name) + " [" + text(station->id) + "]"
				: QString());
		afterRun(nodeRegionText);
		afterRun(nodeSignalledText);
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
			title += ", platform " + text(membership->second.platformId);
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
			if (from && to)
				arcLengthText->setText(number(std::hypot(to->xKm - from->xKm, to->yKm - from->yKm) * 1000));
			else
				afterRun(arcLengthText);
			arcCurvatureText->setText(number(arc->curvatureRadiusM));
			arcGradientText->setText(number(arc->gradientPercent));
			arcSpeedLimitText->setText(number(arc->speedLimitMs));
		} else {
			if (std::none_of(model.tracks.begin(), model.tracks.end(),
					[&](const SceneTrack& track) { return track.id == id; }))
				return;
			arcIDText->setText("None");
			arcTrackIDText->setText(text(id));
		}
		afterRun(arcOperationalStateText);
		afterRun(arcConnectedSignalsText);
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
		const TrackPreviewSignal* head = m_presenter->previewSignal(track, id);
		if (!head)
			return;
		signallingTrackIDText->setText(text(track));
		signallingXText->setText(number(head->rawX * 1000));
		afterRun(signallingAspectText);
		setSectionText(signallingProtectedSectionText, id, head->sectionLabel);
		afterRun(signallingNextTrackText);
		setSectionText(signallingIDSectionAheadText, id, head->sectionLabel);
		afterRun(signallingLengthSectionAheadText);
		QStringList ids;
		for (const auto& signal : sceneSignals(model))
			if (signal.protectedSection == id)
				ids << text(signal.id);
		QString details = QStringLiteral("Track %1, section %2, %3")
							  .arg(text(track), text(id), item->data(PreviewGraphics::Reversed).toBool() ? "reverse" : "forward");
		if (!ids.isEmpty())
			details += QStringLiteral("\nSignals: %1").arg(ids.join(", "));
		signallingGroupDetails->setPlainText(details);
		title = "Signal " + text(head->label);
		signallingInfoWidget->show();
	} else {
		return;
	}
	infoDockWidget->setWindowTitle(title);
	infoDockWidget->show();
	m_previewSelectionKind = kind;
	m_previewSelectionId = text(id);

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
	setTrackText(nodeTrackIDText, *m_presenter, {el->track});
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
	infoDockWidget->setWindowTitle(QStringLiteral("Node %1").arg(nodeIDText->text()));
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
	setTrackText(nodeTrackIDText, *m_presenter, {re->track});
	nodeStationNameText->setText(QString::fromStdString(re->node->stationName));
	if (re->track >= 0 && re->track < 268)
		nodeRegionText->setText(QString::number(m_host.regionOfTrack(re->track)));
	else
		nodeRegionText->setText("None");
	std::vector<int> connectedTracks;
	if (re->track >= 0 && re->track < 268)
		connectedTracks.push_back(re->track);
	const int connectionCount = std::max(0, std::min(re->node->numConnections, 6));
	for (int i = 0; i < connectionCount; ++i) {
		const int connectedTrack = re->node->connectIdBlockSet[i];
		if (connectedTrack >= 0 && connectedTrack < 268
			&& std::find(connectedTracks.begin(), connectedTracks.end(), connectedTrack) == connectedTracks.end())
			connectedTracks.push_back(connectedTrack);
	}
	setTrackText(nodeConnectedTracksText, *m_presenter, connectedTracks);
	nodeSignalledText->setText(re->node->isSignalled ? "Yes" : "No");

	// hide other widgets
	arcInfoWidget->hide();
	connectionInfoWidget->hide();
	signallingInfoWidget->hide();
	trainInfoWidget->hide();
	m_presenter->removeTrainPaxInfoIcon();
	m_presenter->removePaxInfoIcon();

	// show widget
	infoDockWidget->setWindowTitle(QStringLiteral("Station %1").arg(nodeStationNameText->text()).trimmed());
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
	setTrackText(arcTrackIDText, *m_presenter, {line->track});
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
	for (auto* candidate : m_presenter->allSignals) {
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
		setTextWithTip(arcConnectedSignalsText, QStringLiteral("None"), QString());
	else
		setTextWithTip(arcConnectedSignalsText, sectionLabels.join(QStringLiteral(", ")),
			(sectionIds.size() == 1 ? QStringLiteral("Section ID: %1") : QStringLiteral("Section IDs: %1")).arg(sectionIds.join(QStringLiteral(", "))));

	// hide other widgets
	nodeInfoWidget->hide();
	connectionInfoWidget->hide();
	signallingInfoWidget->hide();
	trainInfoWidget->hide();
	m_presenter->removeTrainPaxInfoIcon();
	m_presenter->removePaxInfoIcon();

	// show widget
	infoDockWidget->setWindowTitle(QStringLiteral("Arc %1").arg(arcIDText->text()));
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
	setTrackText(connectionFirstTrackIDText, *m_presenter, {line->connection->idFirstTrackLine});
	setTrackText(connectionSecondTrackIDText, *m_presenter, {line->connection->idSecondTrackLine});
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
	infoDockWidget->setWindowTitle(QStringLiteral("Connection"));
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
	setTrackText(signallingTrackIDText, *m_presenter, {signal->trackID});
	signallingXText->setText(QString::fromStdString(to_string_precision(1000 * signal->X, 2))); // km to m
	signallingAspectText->setText(signalAspectLabel(signal));
	signallingGroupDetails->setPlainText(signal->toolTip());
	const bool hasAhead = !signal->sectionAheadId.empty();
	const int nextTrack = signal->sectionAheadTrackId >= 0
		? signal->sectionAheadTrackId
		: signal->sectionBehindTrackId;
	setSectionText(signallingProtectedSectionText, hasAhead ? signal->sectionAheadId : signal->sectionBehindId,
		hasAhead ? signal->sectionAheadLabel : signal->sectionBehindLabel);
	if (nextTrack < 0)
		setTextWithTip(signallingNextTrackText, QStringLiteral("None"), QString());
	else
		setTrackText(signallingNextTrackText, *m_presenter, {nextTrack});

	// check if section ahead exists
	setSectionText(signallingIDSectionAheadText, signal->sectionAheadId, signal->sectionAheadLabel);
	signallingLengthSectionAheadText->setText(hasAhead
			? QString::fromStdString(to_string_precision(1000 * signal->sectionAheadLength, 2)) // km to m
			: QStringLiteral("None"));

	// hide other widgets
	arcInfoWidget->hide();
	connectionInfoWidget->hide();
	nodeInfoWidget->hide();
	trainInfoWidget->hide();
	m_presenter->removeTrainPaxInfoIcon();
	m_presenter->removePaxInfoIcon();

	// show widget
	infoDockWidget->setWindowTitle(QStringLiteral("Signal"));
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
	infoDockWidget->setWindowTitle(QStringLiteral("Train %1").arg(trainNameText->text()));
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
