#include "widgets/NetworkLegendWidget.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <map>
#include <vector>

namespace {

class LegendLabel : public QLabel {
public:
	using QLabel::QLabel;

protected:
	void resizeEvent(QResizeEvent* event) override {
		QLabel::resizeEvent(event);
		updateWrappedHeight();
	}

	void changeEvent(QEvent* event) override {
		QLabel::changeEvent(event);
		if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
			updateWrappedHeight();
	}

private:
	void updateWrappedHeight() {
		// A narrow rail must reserve every wrapped line, including after a font change.
		setMinimumHeight(qMax(0, heightForWidth(width())));
	}
};

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
			QPolygonF body;
			body << QPointF(4, 5) << QPointF(38, 5) << QPointF(43, 9)
				 << QPointF(38, 13) << QPointF(4, 13);
			painter.setPen(QPen(m_entry.outlineColor, 1.2));
			painter.setBrush(m_entry.color);
			painter.drawPolygon(body);
			return;
		}

		if (m_entry.kind == NetworkLegendEntryKind::Station) {
			const QPixmap icon(m_entry.iconResource);
			if (!icon.isNull())
				painter.drawPixmap(QRect(15, 1, 16, 16), icon);
			return;
		}

		if (m_entry.kind == NetworkLegendEntryKind::Signal) {
			// Same marks as SignalItem draws on the canvas.
			const QRectF head(17.0, 3.0, 12.0, 12.0);
			const QPointF c = head.center();
			const qreal r = head.width() / 2.0;
			if (m_entry.signalLook == NetworkLegendSignalLook::Unavailable) {
				painter.setPen(QPen(m_entry.color, 1.5));
				painter.setBrush(Qt::NoBrush);
				painter.drawEllipse(head);
				painter.drawLine(QLineF(c.x() - 0.4 * r, c.y(), c.x() + 0.4 * r, c.y()));
				return;
			}
			painter.setPen(QPen(Qt::white, 1.0));
			painter.setBrush(m_entry.color);
			painter.drawEllipse(head);
			if (m_entry.signalLook == NetworkLegendSignalLook::Failed) {
				painter.setPen(QPen(Qt::white, 1.5));
				painter.drawLine(QLineF(c.x() - 0.5 * r, c.y() - 0.5 * r, c.x() + 0.5 * r, c.y() + 0.5 * r));
				painter.drawLine(QLineF(c.x() - 0.5 * r, c.y() + 0.5 * r, c.x() + 0.5 * r, c.y() - 0.5 * r));
			} else if (m_entry.signalCue == SignalCueKind::Stop) {
				painter.setPen(QPen(QColor(30, 30, 30), 1.5));
				painter.drawLine(QLineF(c.x() - 0.6 * r, c.y(), c.x() + 0.6 * r, c.y()));
			} else if (m_entry.signalCue == SignalCueKind::Caution) {
				painter.setPen(QPen(QColor(30, 30, 30), 1.5));
				painter.setBrush(QColor(30, 30, 30));
				painter.drawEllipse(c, 0.2 * r, 0.2 * r);
			}
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

QString stationLabel() {
	return "Station";
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

NetworkLegendEntry trainEntry(const QString& label, const QString& toolTip, const QColor& fill, const QColor& outline) {
	NetworkLegendEntry entry;
	entry.kind = NetworkLegendEntryKind::Train;
	entry.label = label;
	entry.toolTip = toolTip;
	entry.color = fill;
	entry.outlineColor = outline;
	return entry;
}

// Natural order: a run of digits compares by value, so "S2" comes before "S10".
bool serviceIdLess(const QString& a, const QString& b) {
	int i = 0;
	int j = 0;
	while (i < a.size() && j < b.size()) {
		if (!a.at(i).isDigit() || !b.at(j).isDigit()) {
			if (a.at(i) != b.at(j))
				return a.at(i) < b.at(j);
			++i;
			++j;
			continue;
		}
		int iEnd = i;
		int jEnd = j;
		while (iEnd < a.size() && a.at(iEnd).isDigit())
			++iEnd;
		while (jEnd < b.size() && b.at(jEnd).isDigit())
			++jEnd;
		// the longer number is the larger one once leading zeros are skipped
		while (i < iEnd - 1 && a.at(i) == QLatin1Char('0'))
			++i;
		while (j < jEnd - 1 && b.at(j) == QLatin1Char('0'))
			++j;
		if (iEnd - i != jEnd - j)
			return iEnd - i < jEnd - j;
		const int order = a.mid(i, iEnd - i).compare(b.mid(j, jEnd - j));
		if (order != 0)
			return order < 0;
		i = iEnd;
		j = jEnd;
	}
	if (i == a.size() && j == b.size())
		return a < b;
	return i == a.size();
}

// One "Train" row for the default colour, then one row per other fill colour,
// labelled with the services that use it and ordered by the smallest service
// id of each row. A long list of services is shortened in the label; the
// tooltip has all of them.
QVector<NetworkLegendEntry> trainEntries(const QVector<NetworkLegendTrain>& trains) {
	constexpr int maxListedServices = 3;
	struct CustomColour {
		QColor fill;
		QColor outline;
		QStringList serviceIds;
	};
	bool hasDefault = false;
	std::map<QRgb, CustomColour> custom;
	for (const NetworkLegendTrain& train : trains) {
		if (train.fill.rgb() == defaultTrainFill().rgb()) {
			hasDefault = true;
			continue;
		}
		CustomColour& group = custom[train.fill.rgb()];
		group.fill = train.fill;
		group.outline = train.outline;
		if (!train.serviceId.isEmpty() && !group.serviceIds.contains(train.serviceId))
			group.serviceIds << train.serviceId;
	}

	std::vector<CustomColour> groups;
	for (auto& item : custom) {
		std::sort(item.second.serviceIds.begin(), item.second.serviceIds.end(), serviceIdLess);
		groups.push_back(std::move(item.second));
	}
	std::sort(groups.begin(), groups.end(), [](const CustomColour& a, const CustomColour& b) {
		return serviceIdLess(a.serviceIds.value(0), b.serviceIds.value(0));
	});

	QVector<NetworkLegendEntry> entries;
	if (hasDefault)
		entries << trainEntry("Train", QString(), defaultTrainFill(), defaultTrainOutline());
	for (const CustomColour& group : groups) {
		QString label = group.serviceIds.mid(0, maxListedServices).join(", ");
		if (group.serviceIds.size() > maxListedServices)
			label += QString(" and %1 more").arg(group.serviceIds.size() - maxListedServices);
		entries << trainEntry(label, group.serviceIds.join(", "), group.fill, group.outline);
	}
	return entries;
}

NetworkLegendEntry stationEntry(const StationVisual& visual) {
	NetworkLegendEntry entry;
	entry.kind = NetworkLegendEntryKind::Station;
	entry.label = stationLabel();
	entry.color = visual.fill;
	entry.iconResource = ":/icons/station-dark.svg";
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

NetworkLegendEntry unavailableSignalEntry() {
	NetworkLegendEntry entry = signalEntry("Unavailable signal", -1);
	entry.color = QColor(150, 150, 150);
	entry.signalLook = NetworkLegendSignalLook::Unavailable;
	return entry;
}

NetworkLegendEntry failedSignalEntry() {
	NetworkLegendEntry entry = signalEntry("Failed signal", 0);
	entry.signalLook = NetworkLegendSignalLook::Failed;
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
		NetworkLegendEntry high = trackEntry("High speed track (200+ km/h)", TrackOperationalState::Free);
		high.color = classifyTrackSpeed(200.0 / 3.6).color;
		high.lineWidth = 4;
		NetworkLegendEntry main = trackEntry("Mainline track (120+ km/h)", TrackOperationalState::Free);
		main.color = classifyTrackSpeed(120.0 / 3.6).color;
		main.lineWidth = 3;
		m_entries << high << main << trackEntry("Local track", TrackOperationalState::Free);
		if (content.hasSelectedTrack) {
			NetworkLegendEntry selected = trackEntry("Selected track", TrackOperationalState::Free);
			selected.color = Qt::blue;
			selected.lineWidth = 4;
			m_entries << selected;
		}
	}

	m_entries += trainEntries(content.trains);

	if (!content.stationVisuals.isEmpty())
		m_entries << stationEntry(content.stationVisuals.first());

	if (content.hasSignals) {
		m_entries << signalEntry("Stop signal", 0)
				  << signalEntry("Caution signal", 75)
				  << signalEntry("Proceed signal", 180)
				  << unavailableSignalEntry()
				  << failedSignalEntry();
	}
	if (content.hasPassengers) {
		NetworkLegendEntry entry;
		entry.kind = NetworkLegendEntryKind::Passenger;
		entry.label = "Passenger count";
		entry.iconResource = ":/icons/pax_icon.png";
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
		auto* label = new LegendLabel(entry.label, row);
		label->setObjectName(QString("mapKeyEntry%1").arg(i));
		label->setMaximumWidth(121);
		label->setWordWrap(true);
		label->setToolTip(entry.toolTip.isEmpty() ? entry.label : entry.toolTip);
		rowLayout->addWidget(label, 1);
		layout->addWidget(row);
	}
	setExpanded(m_expanded);
}
