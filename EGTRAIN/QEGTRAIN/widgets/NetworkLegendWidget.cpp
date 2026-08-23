#include "widgets/NetworkLegendWidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

#include <set>

namespace {

class LegendSwatch : public QWidget {
public:
	explicit LegendSwatch(const NetworkLegendEntry& entry, QWidget* parent = nullptr)
		: QWidget(parent), m_entry(entry) {
		setFixedSize(46, 18);
		setAttribute(Qt::WA_TransparentForMouseEvents);
	}

protected:
	void paintEvent(QPaintEvent*) override {
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);
		const QRectF cueRect(11.0, 3.0, 24.0, 12.0);
		if (m_entry.kind == NetworkLegendEntryKind::Track) {
			QPen pen(m_entry.color, qMax(2, m_entry.lineWidth));
			pen.setStyle(m_entry.penStyle);
			painter.setPen(pen);
			const QLineF line(3.0, 9.0, 43.0, 9.0);
			painter.drawLine(line);
			if (m_entry.trackState != TrackOperationalState::Free) {
				const TrackVisual base = freeTrackVisual();
				painter.setPen(QPen(base.color, base.width));
				painter.drawLine(line);
			}
			return;
		}

		if (m_entry.kind == NetworkLegendEntryKind::Train) {
			const QRectF body(2.0, 2.0, 42.0, 14.0);
			painter.setPen(QPen(m_entry.outlineColor, 1.2));
			painter.setBrush(QColor("#26313B"));
			const qreal radius = trainBadgeCornerRadius(m_entry.trainShape);
			painter.drawRoundedRect(body, radius, radius);

			QPolygonF direction;
			direction << QPointF(body.right() - 3.0, body.center().y())
					  << QPointF(body.right() - 8.0, body.center().y() - 3.0)
					  << QPointF(body.right() - 8.0, body.center().y() + 3.0);
			painter.setPen(Qt::NoPen);
			painter.setBrush(QColor("#F2F5F7"));
			painter.drawPolygon(direction);

			const QRectF plate(5.0, 3.0, 12.0, 12.0);
			painter.setPen(QPen(m_entry.outlineColor, 0.8));
			painter.setBrush(m_entry.color);
			painter.drawRoundedRect(plate, 2.0, 2.0);
			const QPixmap icon(m_entry.iconResource);
			if (!icon.isNull())
				painter.drawPixmap(plate.adjusted(1.0, 1.0, -1.0, -1.0).toRect(), icon);
			return;
		}

		if (m_entry.kind == NetworkLegendEntryKind::Station) {
			const QColor markerColor(210, 215, 220);
			const QRectF symbol(15.0, 1.0, 16.0, 16.0);
			painter.setPen(QPen(markerColor, 1.0));
			if (m_entry.stationKind == StationVisualKind::StopMarker) {
				painter.setBrush(markerColor);
				painter.drawEllipse(symbol.adjusted(5.0, 5.0, -5.0, -5.0));
			} else {
				painter.setBrush(Qt::NoBrush);
				const QRectF platform = symbol.adjusted(3.0, 5.0, -3.0, -5.0);
				painter.drawRect(platform);
				if (m_entry.stationKind == StationVisualKind::Interchange)
					painter.drawRect(platform.adjusted(-2.0, -2.0, 2.0, 2.0));
			}
			return;
		}

		if (m_entry.kind == NetworkLegendEntryKind::Signal) {
			const QColor outline("#0D131A");
			const QRectF lamp(17.0, 3.0, 12.0, 12.0);
			painter.setPen(QPen(outline, 1.0));
			painter.setBrush(m_entry.color);
			painter.drawEllipse(lamp);
			const QPointF center = lamp.center();
			QPolygonF direction;
			direction << center + QPointF(0.3 * lamp.width(), 0.0)
					  << center + QPointF(-0.1 * lamp.width(), -0.25 * lamp.height())
					  << center + QPointF(-0.1 * lamp.width(), 0.25 * lamp.height());
			painter.setBrush(outline);
			painter.drawPolygon(direction);
			return;
		}

		if (!m_entry.iconResource.isEmpty()) {
			const QPixmap icon(m_entry.iconResource);
			if (!icon.isNull()) {
				painter.drawPixmap(cueRect.toRect(), icon);
				return;
			}
		}

		QPen outline(m_entry.color.darker(150), 1.0);
		painter.setPen(outline);
		painter.setBrush(m_entry.color);
		painter.drawRect(cueRect);
	}

private:
	NetworkLegendEntry m_entry;
};

QString trainLabel(TrainVisualKind kind) {
	switch (kind) {
	case TrainVisualKind::Sprinter:
		return "Sprinter";
	case TrainVisualKind::Intercity:
		return "Intercity";
	case TrainVisualKind::HighSpeed:
		return "High-speed train";
	case TrainVisualKind::Freight:
		return "Freight";
	case TrainVisualKind::Passenger:
	default:
		return "Passenger train";
	}
}

QString stationLabel(StationVisualKind kind) {
	switch (kind) {
	case StationVisualKind::Interchange:
		return "Interchange";
	case StationVisualKind::Platform:
		return "Station platform";
	case StationVisualKind::StopMarker:
	default:
		return "Station stop";
	}
}

NetworkLegendEntry trackEntry(const QString& label, TrackOperationalState state) {
	NetworkLegendEntry entry;
	entry.kind = NetworkLegendEntryKind::Track;
	entry.label = label;
	entry.trackState = state;
	if (state == TrackOperationalState::Free) {
		const TrackVisual visual = freeTrackVisual();
		entry.color = visual.color;
		entry.lineWidth = visual.width;
		entry.penStyle = Qt::SolidLine;
	} else {
		const TrackStateVisual visual = classifyTrackState(state);
		entry.color = visual.color;
		entry.lineWidth = visual.width;
		entry.penStyle = visual.style;
	}
	return entry;
}

NetworkLegendEntry trainEntry(const TrainVisual& visual) {
	NetworkLegendEntry entry;
	entry.kind = NetworkLegendEntryKind::Train;
	entry.label = trainLabel(visual.kind);
	entry.color = visual.fill;
	entry.outlineColor = visual.outline;
	entry.trainKind = visual.kind;
	entry.trainShape = visual.shape;
	entry.iconResource = visual.iconResource;
	return entry;
}

NetworkLegendEntry stationEntry(const StationVisual& visual) {
	NetworkLegendEntry entry;
	entry.kind = NetworkLegendEntryKind::Station;
	entry.label = stationLabel(visual.kind);
	entry.color = visual.fill;
	entry.stationKind = visual.kind;
	entry.iconResource = visual.iconResource;
	return entry;
}

NetworkLegendEntry signalEntry(const QString& label, int aspect) {
	const SignalVisual visual = classifySignalAspect(aspect);
	NetworkLegendEntry entry;
	entry.kind = NetworkLegendEntryKind::Signal;
	entry.label = label;
	entry.color = visual.lamp;
	entry.signalCue = visual.cue;
	entry.iconResource = visual.iconResource;
	return entry;
}

} // namespace

NetworkLegendWidget::NetworkLegendWidget(QWidget* parent)
	: QWidget(parent) {
	setObjectName("mapKeySection");
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(2);
	m_toggle = new QToolButton(this);
	m_toggle->setObjectName("mapKeyToggle");
	m_toggle->setText("Map key");
	m_toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	m_toggle->setCheckable(true);
	m_toggle->setChecked(true);
	m_toggle->setArrowType(Qt::DownArrow);
	m_toggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	layout->addWidget(m_toggle);

	m_body = new QWidget(this);
	m_body->setObjectName("mapKeyBody");
	auto* bodyLayout = new QVBoxLayout(m_body);
	bodyLayout->setContentsMargins(0, 0, 0, 0);
	bodyLayout->setSpacing(1);
	layout->addWidget(m_body);

	connect(m_toggle, &QToolButton::toggled, this, [this](bool checked) {
		setExpanded(checked);
	});
	setExpanded(true);
}

void NetworkLegendWidget::setCaseContent(const NetworkLegendContent& content) {
	m_entries.clear();
	if (content.hasTracks) {
		if (content.showOperationalTrackStates) {
			m_entries << trackEntry("Free track", TrackOperationalState::Free)
					  << trackEntry("Prepared route", TrackOperationalState::Prepared)
					  << trackEntry("Occupied section", TrackOperationalState::Occupied)
					  << trackEntry("Blocked section", TrackOperationalState::Blocked);
		} else {
			m_entries << trackEntry("Track", TrackOperationalState::Free);
			if (content.hasSelectedTrack) {
				NetworkLegendEntry selected = trackEntry("Selected track", TrackOperationalState::Free);
				selected.color = QColor(242, 170, 70);
				selected.lineWidth = 4;
				m_entries << selected;
			}
		}
	}

	std::set<int> trainKinds;
	for (const TrainVisual& visual : content.trainVisuals) {
		if (trainKinds.insert(static_cast<int>(visual.kind)).second)
			m_entries << trainEntry(visual);
	}

	std::set<int> stationKinds;
	for (const StationVisual& visual : content.stationVisuals) {
		if (stationKinds.insert(static_cast<int>(visual.kind)).second)
			m_entries << stationEntry(visual);
	}

	if (content.hasSignals) {
		m_entries << signalEntry("Stop signal", 0)
				  << signalEntry("Caution signal", 75)
				  << signalEntry("Proceed signal", 180);
	}
	if (content.hasPassengers) {
		NetworkLegendEntry entry;
		entry.kind = NetworkLegendEntryKind::Passenger;
		entry.label = "Passenger count";
		entry.iconResource = ":/icons/passenger.svg";
		m_entries << entry;
	}
	rebuildRows();
}

QStringList NetworkLegendWidget::entryLabels() const {
	QStringList labels;
	for (const NetworkLegendEntry& entry : m_entries)
		labels << entry.label;
	return labels;
}

void NetworkLegendWidget::setExpanded(bool expanded) {
	m_expanded = expanded;
	if (m_body)
		m_body->setVisible(expanded);
	if (m_toggle) {
		m_toggle->setChecked(expanded);
		m_toggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
		m_toggle->setToolTip(expanded ? "Collapse the map key" : "Expand the map key");
	}
}

void NetworkLegendWidget::rebuildRows() {
	auto* layout = qobject_cast<QVBoxLayout*>(m_body->layout());
	while (QLayoutItem* item = layout->takeAt(0)) {
		delete item->widget();
		delete item;
	}

	for (int i = 0; i < m_entries.size(); ++i) {
		const NetworkLegendEntry& entry = m_entries.at(i);
		auto* row = new QWidget(m_body);
		row->setObjectName(QString("mapKeyRow%1").arg(i));
		row->setMaximumWidth(160);
		auto* rowLayout = new QHBoxLayout(row);
		rowLayout->setContentsMargins(2, 1, 2, 1);
		rowLayout->setSpacing(5);
		auto* swatch = new LegendSwatch(entry, row);
		swatch->setObjectName(QString("mapKeySwatch%1").arg(i));
		rowLayout->addWidget(swatch);
		auto* label = new QLabel(entry.label, row);
		label->setObjectName(QString("mapKeyEntry%1").arg(i));
		label->setMaximumWidth(121);
		label->setToolTip(entry.label);
		rowLayout->addWidget(label, 1);
		layout->addWidget(row);
	}
	setExpanded(m_expanded);
}
