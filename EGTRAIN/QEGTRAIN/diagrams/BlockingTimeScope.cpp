#include "diagrams/BlockingTimeScope.h"

#include "util/TimeFormat.h"
#include "widgets/ChoiceComboBox.h"
#include "widgets/DialogLayout.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QObject>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QString>
#include <QValidator>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <tuple>
#include <utility>

const char* blockingSegmentTypeName(BlockingTimeSegmentStyle style) {
	switch (style) {
		case BlockingTimeSegmentStyle::Station:
			return "station";
		case BlockingTimeSegmentStyle::Switch:
			return "switch";
		case BlockingTimeSegmentStyle::SwitchStation:
			return "switch/station";
		case BlockingTimeSegmentStyle::Critical:
			return "conflict";
		case BlockingTimeSegmentStyle::CriticalStation:
			return "conflict/station";
		case BlockingTimeSegmentStyle::Default:
		default:
			return "block";
	}
}

const char* blockingSegmentTypeName(const BlockingTimeDiagramSegment& segment) {
	return segment.capacityCritical ? "capacity critical block" : blockingSegmentTypeName(segment.style);
}

std::vector<RouteStation> routeStationsFrom(const std::vector<BlockSectionInput>& blocks, const RouteDiagramPath& path) {
	// A comparison with NaN is false, so a block or a node without a position holds nothing.
	const auto holds = [](const BlockSectionInput& block, double km) {
		return km >= std::min(block.startKm, block.endKm) && km <= std::max(block.startKm, block.endKm);
	};
	std::vector<RouteStation> stations;
	std::string lastStation;
	// The path lists the nodes block after block, so the block of a node is never before the block of the node before it.
	std::size_t cursor = 0;
	for (const RouteDiagramNode& node : path.nodes) {
		std::size_t first = cursor;
		while (first < blocks.size() && !holds(blocks[first], node.positionKm))
			++first;
		if (first == blocks.size())
			continue;
		cursor = first;
		const std::string station = node.stationId.empty() ? node.stationName : node.stationId;
		if (station.empty())
			continue;
		// A node at the end of a block lies at the start of the next block too.
		const std::size_t last = first + 1 < blocks.size() && holds(blocks[first + 1], node.positionKm) ? first + 1 : first;
		const auto at = [&blocks, &node](std::size_t index, bool end) {
			return node.positionKm == (end ? blocks[index].endKm : blocks[index].startKm);
		};
		const int after = static_cast<int>(!at(first, true) ? first : last);
		const int before = static_cast<int>(!at(last, false) ? last : first);
		if (!stations.empty() && station == lastStation) {
			stations.back().lastBlock = std::max(stations.back().lastBlock, static_cast<int>(last));
			stations.back().toBlock = std::max(stations.back().toBlock, before);
			continue;
		}
		lastStation = station;
		stations.push_back({node.stationName.empty() ? node.stationId : node.stationName, static_cast<int>(first),
			static_cast<int>(last), after, before});
	}
	return stations;
}

BlockingTimeScopeChoices::Route blockingTimeScopeRouteFrom(const CompletedRoute& route) {
	BlockingTimeScopeChoices::Route choice;
	choice.id = route.id;
	choice.corridor = route.corridor;
	for (const BlockSectionInput& block : route.blocks)
		choice.blockIds.push_back(block.id);
	choice.stations = routeStationsFrom(route.blocks, route.path);
	return choice;
}

std::pair<int, int> stationStretchBlocks(const BlockingTimeScopeChoices::Route& route, int from, int to) {
	const int count = static_cast<int>(route.stations.size());
	if (count == 0)
		return {0, static_cast<int>(route.blockIds.size()) - 1};
	const int low = std::clamp(std::min(from, to), 0, count - 1);
	const int high = std::clamp(std::max(from, to), 0, count - 1);
	if (low == high)
		return {route.stations[static_cast<std::size_t>(low)].firstBlock, route.stations[static_cast<std::size_t>(low)].lastBlock};
	const int first = low == 0 ? 0 : route.stations[static_cast<std::size_t>(low)].fromBlock;
	const int last = high == count - 1 ? static_cast<int>(route.blockIds.size()) - 1 : route.stations[static_cast<std::size_t>(high)].toBlock;
	return first <= last ? std::make_pair(first, last) : std::make_pair(last, first);
}

std::optional<std::pair<int, int>> stationsHoldingBlocks(const BlockingTimeScopeChoices::Route& route, int first, int last) {
	const int count = static_cast<int>(route.stations.size());
	const int low = std::min(first, last);
	const int high = std::max(first, last);
	std::optional<std::pair<int, int>> best;
	int bestLength = 0;
	for (int from = 0; from < count; ++from)
		for (int to = count - 1; to >= from; --to) {
			const auto [start, end] = stationStretchBlocks(route, from, to);
			if (start <= low && end >= high && (!best || end - start < bestLength)) {
				best = std::make_pair(from, to);
				bestLength = end - start;
			}
		}
	return best;
}

QString blockingTimeScopeRouteText(const BlockingTimeScopeChoices::Route& route) {
	QString text = QString::fromStdString(route.id);
	if (!route.stations.empty()) {
		text += ": " + QString::fromStdString(route.stations.front().name);
		if (route.stations.size() > 1)
			text += " to " + QString::fromStdString(route.stations.back().name);
	}
	if (!route.corridor.empty())
		text += QString("  (%1)").arg(QString::fromStdString(route.corridor));
	return text;
}

namespace {

// Calls `closed` each time the open list of a combo box closes. It belongs to `owner`, which goes before the list does.
class ListCloseWatch : public QObject {
public:
	ListCloseWatch(QComboBox* list, QObject* owner, std::function<void()> closed) : QObject(owner), m_closed(std::move(closed)) {
		list->view()->installEventFilter(this);
	}
	bool eventFilter(QObject*, QEvent* event) override {
		if (event->type() == QEvent::Hide)
			m_closed();
		return false;
	}

private:
	std::function<void()> m_closed;
};

} // namespace

ScopeStretchChoice::ScopeStretchChoice(std::vector<BlockingTimeScopeChoices::Route> routes, SceneSectionLabels labels,
	bool allRoutes, QFormLayout* form, QWidget* parent, const QString& name)
	: QObject(parent), m_routes(std::move(routes)), m_labels(std::move(labels)) {
	m_route = new ChoiceComboBox(parent);
	m_route->setObjectName(name + "Route");
	if (allRoutes)
		m_route->addItem("All routes", -1);
	for (std::size_t index = 0; index < m_routes.size(); ++index)
		m_route->addItem(blockingTimeScopeRouteText(m_routes[index]), static_cast<int>(index));
	auto* kind = new QWidget(parent);
	auto* kindRow = new QHBoxLayout(kind);
	kindRow->setContentsMargins(0, 0, 0, 0);
	m_stations = new QRadioButton("Stations", kind);
	m_stations->setObjectName(name + "Stations");
	m_blocks = new QRadioButton("Blocks", kind);
	m_blocks->setObjectName(name + "Blocks");
	kindRow->addWidget(m_stations);
	kindRow->addWidget(m_blocks);
	kindRow->addStretch(1);
	// A long entry is cut in the middle.
	m_from = new ChoiceComboBox(parent);
	m_from->setObjectName(name + "FromBlock");
	m_to = new ChoiceComboBox(parent);
	m_to->setObjectName(name + "ToBlock");
	m_fromLabel = new QLabel(parent);
	m_toLabel = new QLabel(parent);
	form->addRow("Route / corridor:", m_route);
	form->addRow("Stretch:", kind);
	form->addRow(m_fromLabel, m_from);
	form->addRow(m_toLabel, m_to);
	choose(-1, {});

	connect(m_route, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { chooseRoute(); });
	connect(m_stations, &QRadioButton::clicked, this, [this]() { chooseKind(true); });
	connect(m_blocks, &QRadioButton::clicked, this, [this]() { chooseKind(false); });
	connect(m_from, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) { chooseEntry(true, index); });
	connect(m_to, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) { chooseEntry(false, index); });
	// A combo box highlights its new entry when the program chooses one too, so only an open list counts. A list that closes
	// leaves no entry under the pointer.
	for (QComboBox* list : {m_route, m_from, m_to}) {
		connect(list, qOverload<int>(&QComboBox::highlighted), this, [this, list](int index) {
			if (list->view()->isVisible())
				hover(list, index);
		});
		new ListCloseWatch(list, this, [this]() { emit entryHovered(routeId(), sectionIds(), {}); });
	}
}

int ScopeStretchChoice::routeIndex() const {
	const int index = m_route->currentData().toInt();
	return index >= 0 && index < static_cast<int>(m_routes.size()) ? index : -1;
}

const BlockingTimeScopeChoices::Route* ScopeStretchChoice::route() const {
	const int index = routeIndex();
	return index < 0 ? nullptr : &m_routes[static_cast<std::size_t>(index)];
}

QString ScopeStretchChoice::routeId() const {
	const BlockingTimeScopeChoices::Route* chosen = route();
	return chosen ? QString::fromStdString(chosen->id) : QString();
}

std::vector<int> ScopeStretchChoice::sectionIndices() const {
	const BlockingTimeScopeChoices::Route* chosen = route();
	return chosen ? blockSectionRangeIndices(m_firstBlock, m_lastBlock, static_cast<int>(chosen->blockIds.size()))
				  : std::vector<int>();
}

QStringList ScopeStretchChoice::sectionIds() const {
	return route() ? idsOf(m_firstBlock, m_lastBlock) : QStringList();
}

QStringList ScopeStretchChoice::idsOf(int first, int last) const {
	QStringList ids;
	const BlockingTimeScopeChoices::Route* chosen = route();
	if (chosen)
		for (const int index : blockSectionRangeIndices(first, last, static_cast<int>(chosen->blockIds.size())))
			ids << QString::fromStdString(chosen->blockIds[static_cast<std::size_t>(index)]);
	return ids;
}

namespace {

// A stretch between stations needs two of them; a route with fewer is chosen by its blocks.
bool hasStationStretch(const BlockingTimeScopeChoices::Route* route) {
	return route && route->stations.size() > 1;
}

} // namespace

bool ScopeStretchChoice::showsStations() const {
	return m_preferStations && hasStationStretch(route());
}

void ScopeStretchChoice::choose(int index, const std::vector<int>& indices) {
	{
		const QSignalBlocker blocker(m_route);
		m_route->setCurrentIndex(std::max(0, m_route->findData(index)));
	}
	const BlockingTimeScopeChoices::Route* chosen = route();
	const int count = chosen ? static_cast<int>(chosen->blockIds.size()) : 0;
	m_firstBlock = 0;
	m_lastBlock = count - 1;
	if (count > 0 && !indices.empty()) {
		const auto [low, high] = std::minmax_element(indices.begin(), indices.end());
		m_firstBlock = std::clamp(*low, 0, count - 1);
		m_lastBlock = std::clamp(*high, 0, count - 1);
	}
	// Stations when they give exactly this stretch, else blocks.
	const auto held = chosen ? stationsHoldingBlocks(*chosen, m_firstBlock, m_lastBlock) : std::nullopt;
	const bool exact = held && stationStretchBlocks(*chosen, held->first, held->second) == std::make_pair(m_firstBlock, m_lastBlock);
	if (hasStationStretch(chosen))
		m_preferStations = exact;
	if (exact) {
		m_fromStation = held->first;
		m_toStation = held->second;
	}
	refill();
}

void ScopeStretchChoice::chooseRoute() {
	const BlockingTimeScopeChoices::Route* chosen = route();
	m_firstBlock = 0;
	m_lastBlock = chosen ? static_cast<int>(chosen->blockIds.size()) - 1 : -1;
	if (showsStations()) {
		m_fromStation = 0;
		m_toStation = static_cast<int>(chosen->stations.size()) - 1;
		std::tie(m_firstBlock, m_lastBlock) = stationStretchBlocks(*chosen, m_fromStation, m_toStation);
	}
	refill();
	emit stretchChanged(routeId(), sectionIds());
}

void ScopeStretchChoice::chooseKind(bool stations) {
	const QString routeBefore = routeId();
	const QStringList idsBefore = sectionIds();
	m_preferStations = stations;
	if (showsStations()) {
		const BlockingTimeScopeChoices::Route& chosen = *route();
		const auto held = stationsHoldingBlocks(chosen, m_firstBlock, m_lastBlock);
		m_fromStation = held ? held->first : 0;
		m_toStation = held ? held->second : static_cast<int>(chosen.stations.size()) - 1;
		std::tie(m_firstBlock, m_lastBlock) = stationStretchBlocks(chosen, m_fromStation, m_toStation);
	}
	refill();
	// The same blocks in the other kind of entry are the same stretch.
	if (routeId() != routeBefore || sectionIds() != idsBefore)
		emit stretchChanged(routeId(), sectionIds());
}

void ScopeStretchChoice::chooseEntry(bool from, int index) {
	const BlockingTimeScopeChoices::Route* chosen = route();
	if (!chosen || index < 0)
		return;
	const QString routeBefore = routeId();
	const QStringList idsBefore = sectionIds();
	const bool stations = showsStations();
	int& first = stations ? m_fromStation : m_firstBlock;
	int& last = stations ? m_toStation : m_lastBlock;
	if (from) {
		first = index;
		last = std::max(last, index);
	} else {
		last = index;
		first = std::min(first, index);
	}
	if (stations)
		std::tie(m_firstBlock, m_lastBlock) = stationStretchBlocks(*chosen, m_fromStation, m_toStation);
	const QSignalBlocker blockFrom(m_from);
	const QSignalBlocker blockTo(m_to);
	m_from->setCurrentIndex(first);
	m_to->setCurrentIndex(last);
	// Another station can hold the same blocks.
	if (routeId() != routeBefore || sectionIds() != idsBefore)
		emit stretchChanged(routeId(), sectionIds());
}

void ScopeStretchChoice::refill() {
	const QSignalBlocker blockFrom(m_from);
	const QSignalBlocker blockTo(m_to);
	m_from->clear();
	m_to->clear();
	const BlockingTimeScopeChoices::Route* chosen = route();
	// All routes keeps the kind that the user chose for the next route.
	const bool stations = chosen ? showsStations() : m_preferStations;
	m_stations->setChecked(stations);
	m_blocks->setChecked(!stations);
	m_stations->setEnabled(hasStationStretch(chosen));
	m_blocks->setEnabled(chosen != nullptr);
	m_fromLabel->setText(stations ? "From station:" : "From block:");
	m_toLabel->setText(stations ? "To station:" : "To block:");
	if (!chosen) {
		for (QComboBox* list : {m_from, m_to}) {
			list->addItem(stations ? "All stations" : "All blocks");
			list->setEnabled(false);
		}
		emit entriesChanged();
		return;
	}
	if (stations) {
		for (const RouteStation& station : chosen->stations) {
			const QString blocks = station.firstBlock == station.lastBlock
				? QString("Block %1").arg(station.firstBlock + 1)
				: QString("Blocks %1 to %2").arg(station.firstBlock + 1).arg(station.lastBlock + 1);
			for (QComboBox* list : {m_from, m_to}) {
				list->addItem(QString::fromStdString(station.name));
				list->setItemData(list->count() - 1, blocks, Qt::ToolTipRole);
			}
		}
	} else {
		for (std::size_t index = 0; index < chosen->blockIds.size(); ++index) {
			const std::string& blockId = chosen->blockIds[index];
			const QString text = QString("%1: %2").arg(index + 1).arg(QString::fromStdString(readableSectionLabel(blockId, m_labels)));
			for (QComboBox* list : {m_from, m_to}) {
				list->addItem(text);
				list->setItemData(list->count() - 1, QString::fromStdString(shortBlockId(blockId)), Qt::ToolTipRole);
			}
		}
	}
	const bool any = m_from->count() > 0;
	m_from->setEnabled(any);
	m_to->setEnabled(any);
	m_from->setCurrentIndex(stations ? m_fromStation : m_firstBlock);
	m_to->setCurrentIndex(stations ? m_toStation : m_lastBlock);
	emit entriesChanged();
}

void ScopeStretchChoice::hover(const QComboBox* list, int index) {
	QStringList entry;
	if (list == m_route) {
		const int routeAt = m_route->itemData(index).toInt();
		if (routeAt >= 0 && routeAt < static_cast<int>(m_routes.size()))
			for (const std::string& id : m_routes[static_cast<std::size_t>(routeAt)].blockIds)
				entry << QString::fromStdString(id);
	} else if (showsStations()) {
		const std::vector<RouteStation>& stations = route()->stations;
		if (index >= 0 && index < static_cast<int>(stations.size()))
			entry = idsOf(stations[static_cast<std::size_t>(index)].firstBlock, stations[static_cast<std::size_t>(index)].lastBlock);
	} else if (route()) {
		entry = idsOf(index, index);
	}
	emit entryHovered(routeId(), sectionIds(), entry);
}

BlockingTimeScope defaultBlockingTimeScope(double caseEndSeconds) {
	BlockingTimeScope scope;
	scope.endTime = std::max(0.0, caseEndSeconds);
	return scope;
}

namespace {

// A time of the run in seconds that shows the clock time that the time axis of the diagram gives it (formatSimTime),
// such as "07:15:00", and also accepts "07:15". Typed text takes the second of the range with that clock time that lies
// nearest the value before, so in a run that passes midnight 00:10:00 reads as the time after midnight.
class ClockTimeSpinBox : public QDoubleSpinBox {
public:
	ClockTimeSpinBox(long long startOffsetSeconds, QWidget* parent)
		: QDoubleSpinBox(parent), m_startOffset(startOffsetSeconds) {
		setDecimals(0);
	}

protected:
	QString textFromValue(double value) const override {
		return QString::fromStdString(formatSimTime(static_cast<long long>(std::floor(value)), m_startOffset));
	}

	double valueFromText(const QString& text) const override {
		return secondsOf(text).value_or(value());
	}

	QValidator::State validate(QString& text, int& /*position*/) const override {
		if (secondsOf(text))
			return QValidator::Acceptable;
		static const QRegularExpression partial(QStringLiteral("^\\s*\\d{0,2}(:\\d{0,2}(:\\d{0,2})?)?\\s*$"));
		return partial.match(text).hasMatch() ? QValidator::Intermediate : QValidator::Invalid;
	}

private:
	std::optional<double> secondsOf(const QString& text) const {
		const QStringList parts = text.trimmed().split(':');
		if (parts.size() < 2 || parts.size() > 3)
			return std::nullopt;
		long long clock = 0;
		for (int index = 0; index < 3; ++index) {
			bool ok = index >= static_cast<int>(parts.size());
			const int part = ok ? 0 : parts[index].toInt(&ok);
			if (!ok || (index < static_cast<int>(parts.size()) && parts[index].size() > 2) || part < 0 || part > (index == 0 ? 23 : 59))
				return std::nullopt;
			clock = clock * 60 + part;
		}
		constexpr long long kDay = 86400;
		const long long last = static_cast<long long>(std::floor(maximum()));
		std::optional<double> nearest;
		for (long long seconds = ((clock - m_startOffset) % kDay + kDay) % kDay; seconds <= last; seconds += kDay) {
			const auto candidate = static_cast<double>(seconds);
			if (candidate >= minimum() && (!nearest || std::abs(candidate - value()) < std::abs(*nearest - value())))
				nearest = candidate;
		}
		return nearest;
	}

	long long m_startOffset = 0;
};

} // namespace

bool chooseBlockingTimeScope(QWidget* parent, const BlockingTimeScopeChoices& choices, BlockingTimeScope& scope,
	const ScopeStretchObserver& observer) {
	QDialog dialog(parent);
	dialog.setWindowTitle("Blocking time");
	auto* body = new QWidget;
	auto* form = new QFormLayout(body);
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	auto* stretch = new ScopeStretchChoice(choices.routes, choices.sectionLabels, true, form, &dialog, "blockingTime");
	const bool givenRoute = scope.routeIndex >= 0 && scope.routeIndex < static_cast<int>(choices.routes.size());
	stretch->choose(givenRoute ? scope.routeIndex : -1, givenRoute ? scope.sectionIndices : std::vector<int>());
	auto* startTime = new ClockTimeSpinBox(choices.startOffsetSeconds, &dialog);
	auto* endTime = new ClockTimeSpinBox(choices.startOffsetSeconds, &dialog);
	const double caseEnd = std::max(0.0, choices.caseEndSeconds);
	const double step = choices.stepSeconds > 0.0 ? choices.stepSeconds : 1.0;
	for (QDoubleSpinBox* spinBox : {startTime, endTime}) {
		spinBox->setRange(0.0, caseEnd);
		spinBox->setSingleStep(step);
	}
	startTime->setValue(scope.startTime);
	endTime->setValue(scope.endTime);
	form->addRow("From time:", startTime);
	form->addRow("To time:", endTime);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	DialogLayout::install(dialog, "Blocking time", QString(), body, buttons);
	DialogLayout::fitWidthToContent(dialog);
	// Another route or kind of stretch fills the lists again, so the dialog widens for their entries.
	QObject::connect(stretch, &ScopeStretchChoice::entriesChanged, &dialog, [&dialog]() { DialogLayout::fitWidthToContent(dialog); });
	if (observer)
		observer(*stretch);
	if (dialog.exec() != QDialog::Accepted)
		return false;

	scope.startTime = std::min(startTime->value(), endTime->value());
	scope.endTime = std::max(startTime->value(), endTime->value());
	if (scope.endTime <= scope.startTime) {
		if (scope.startTime + step <= caseEnd)
			scope.endTime = scope.startTime + step;
		else
			scope.startTime = std::max(0.0, scope.endTime - step);
	}
	if (scope.endTime <= scope.startTime)
		return false;
	scope.trainIds.clear();
	scope.blockIds.clear();
	scope.routeIndex = stretch->routeIndex();
	scope.sectionIndices = stretch->sectionIndices();
	if (scope.routeIndex < 0)
		return true;
	const BlockingTimeScopeChoices::Route& route = choices.routes[static_cast<std::size_t>(scope.routeIndex)];
	for (int blockIndex : scope.sectionIndices)
		scope.blockIds.push_back(route.blockIds[static_cast<std::size_t>(blockIndex)]);
	return true;
}

// A segment whose ends do not both map is counted in `omitted` and, when `unmapped` is given, kept there unchanged.
std::vector<BlockingTimeDiagramSegment> projectBlockingSegments(
	const std::vector<BlockingTimeDiagramSegment>& segments,
	const std::map<std::string, RouteDiagramProjection>& projections, int& omitted,
	std::vector<BlockingTimeDiagramSegment>* unmapped) {
	std::vector<BlockingTimeDiagramSegment> mapped;
	for (auto segment : segments) {
		const auto projection = projections.find(segment.trainName);
		const auto start = projection == projections.end() ? std::optional<double>()
														   : projection->second.map(segment.startPositionKm);
		const auto end = projection == projections.end() ? std::optional<double>()
														 : projection->second.map(segment.endPositionKm);
		if (!start || !end) {
			++omitted;
			if (unmapped)
				unmapped->push_back(std::move(segment));
			continue;
		}
		segment.startPositionKm = *start;
		segment.endPositionKm = *end;
		segment.midPositionKm = (*start + *end) / 2.0;
		mapped.push_back(std::move(segment));
	}
	return mapped;
}

// The moc code of ScopeStretchChoice is compiled here and not with the other classes of the library, so that a program
// that uses the scope functions without the result windows does not link the moc code of the windows, which needs the
// telemetry library.
#include "moc_BlockingTimeScope.cpp" // NOLINT(bugprone-suspicious-include): the moc code of this file, see above
