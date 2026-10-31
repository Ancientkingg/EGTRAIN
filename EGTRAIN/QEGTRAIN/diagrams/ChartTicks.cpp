#include "diagrams/ChartTicks.h"

#include "util/TimeFormat.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kStripLabelGap = 6; // between two ids in a row of the strip

} // namespace

double roundTickStep(double span, int steps) {
	const double power = std::pow(10.0, std::floor(std::log10(span / steps)));
	double step = power;
	for (const double factor : {2.0, 5.0, 10.0})
		if (span / step > steps) step = factor * power;
	return step;
}

int roundTickDecimals(double step) {
	return std::max(0, static_cast<int>(-std::floor(std::log10(step) + 1e-9)));
}

std::optional<RoundTickPlan> roundTickPlan(double low, double high) {
	const double span = high - low;
	if (!(span > 0.0) || !std::isfinite(span)) return std::nullopt;
	const double step = roundTickStep(span, 8);
	RoundTickPlan plan;
	// Qt reaches each tick by adding the interval to the one before. The interval is the step cut to 31 significant bits,
	// a hair shorter, so the sums from an anchor at zero are exact and zero reads "0.0", not "-0.0", and each tick lies a
	// hair nearer the anchor than its round value, so an end of the range on a round value keeps its tick. The anchor is
	// zero when the range holds it, else a round value inside the range.
	const int exponent = std::ilogb(step) - 30;
	plan.interval = std::ldexp(std::floor(std::ldexp(step, -exponent)), exponent);
	plan.anchor = low <= 0 && high >= 0 ? 0.0 : std::round((low + high) / 2 / step) * step;
	plan.decimals = roundTickDecimals(step);
	return plan;
}

std::optional<std::pair<double, double>> widenedTickRange(double low, double high) {
	const double span = high - low;
	if (!(span > 0.0) || !std::isfinite(span)) return std::nullopt;
	const double hair = span / 1000;
	const double power = std::pow(10.0, std::floor(std::log10(span / 8)));
	for (const double factor : {1.0, 2.0, 5.0, 10.0, 20.0}) {
		const double step = factor * power;
		double start = std::floor((low - hair) / step) * step;
		if (low >= -std::max(hair, step / 10)) start = std::max(start, 0.0);
		const double end = std::ceil((high + hair) / step) * step;
		if (std::lround((end - start) / step) <= 8) return std::make_pair(start, end);
	}
	return std::nullopt;
}

std::vector<std::pair<double, QString>> labelledStations(const std::vector<StationTick>& stations, double low, double high, bool reverse,
	const QRectF& plot, double labelsLeft, double chartWidth, const ChartTextWidth& textWidth,
	const std::function<QString(const QString& text, double width)>& elide) {
	constexpr double edge = 4.0, gap = 8.0, slack = 16.0;
	// A pixel for the rounding of the label position.
	const double leftEdge = std::max(edge, labelsLeft + 1.0);
	struct Candidate {
		double x;
		const StationTick* station;
	};
	const double span = high - low;
	if (!(span > 0.0) || plot.width() <= 0.0) return {};
	std::vector<Candidate> inRange;
	for (const StationTick& station : stations) {
		if (station.spellings.isEmpty() || station.position < low || station.position > high) continue;
		const double fraction = (station.position - low) / span;
		inRange.push_back({plot.left() + (reverse ? 1.0 - fraction : fraction) * plot.width(), &station});
	}
	std::sort(inRange.begin(), inRange.end(), [](const Candidate& a, const Candidate& b) { return a.x < b.x; });
	struct Placed {
		double x;
		double width;
		std::pair<double, QString> label;
	};
	const auto keepApart = [&](const Placed& left, const Placed& right) { return right.x - left.x - (right.width + left.width) / 2.0 >= gap; };
	const bool anyCalls = std::any_of(inRange.begin(), inRange.end(), [](const Candidate& one) { return one.station->calls > 0; });
	for (std::size_t stride = 1; stride <= inRange.size(); ++stride) {
		const std::size_t named = (inRange.size() + stride - 1) / stride;
		const double share = plot.width() / static_cast<double>(named + 1) - slack;
		// The label of each station: its longest spelling that fits its room and its share, if one does.
		std::vector<std::optional<Placed>> fitting(inRange.size());
		for (std::size_t i = 0; i < inRange.size(); ++i) {
			const double x = inRange[i].x;
			const double room = std::min(share, 2.0 * std::min(x - leftEdge, chartWidth - edge - x));
			const QStringList& spellings = inRange[i].station->spellings;
			const auto spelling = std::find_if(spellings.begin(), spellings.end(),
				[&](const QString& text) { return textWidth(text) <= room; });
			if (spelling != spellings.end())
				fitting[i] = Placed{x, textWidth(*spelling), {inRange[i].station->position, *spelling}};
		}
		std::vector<Placed> placed;
		bool fits = true;
		for (std::size_t i = 0; i < inRange.size() && fits; i += stride) {
			const std::optional<Placed>& label = fitting[i];
			if (label) {
				placed.push_back(*label);
				continue;
			}
			// A station near the edge of the chart may stay unlabelled, one that is too wide for its share may not.
			fits = textWidth(inRange[i].station->spellings.last()) <= share;
		}
		bool apart = fits && !placed.empty();
		for (std::size_t i = 1; i < placed.size(); ++i) apart = apart && keepApart(placed[i - 1], placed[i]);
		if (!apart) continue;
		if (anyCalls) {
			// The stations that can have a label. best[c][a] is the highest score of c + 1 labels that keep apart and end
			// at option a, from[c][a] the option before a. A call outweighs any number of stations of the stride.
			std::vector<std::pair<std::size_t, Placed>> options;
			for (std::size_t i = 0; i < inRange.size(); ++i) {
				const std::optional<Placed>& label = fitting[i];
				if (label) options.emplace_back(i, *label);
			}
			const auto score = [&](std::size_t a) {
				const std::size_t i = options[a].first;
				return static_cast<long long>(inRange[i].station->calls) * static_cast<long long>(inRange.size() + 1) + (i % stride == 0 ? 1 : 0);
			};
			const std::size_t count = placed.size();
			std::vector<std::vector<long long>> best(count, std::vector<long long>(options.size(), -1));
			std::vector<std::vector<std::size_t>> from(count, std::vector<std::size_t>(options.size(), 0));
			for (std::size_t a = 0; a < options.size(); ++a) {
				best[0][a] = score(a);
				for (std::size_t c = 1; c < count; ++c)
					for (std::size_t b = 0; b < a; ++b)
						if (best[c - 1][b] >= 0 && keepApart(options[b].second, options[a].second) && best[c - 1][b] + score(a) > best[c][a]) {
							best[c][a] = best[c - 1][b] + score(a);
							from[c][a] = b;
						}
			}
			std::size_t last = static_cast<std::size_t>(std::max_element(best.back().begin(), best.back().end()) - best.back().begin());
			for (std::size_t c = count; c > 0; --c) {
				placed[c - 1] = options[last].second;
				last = from[c - 1][last];
			}
		}
		std::vector<std::pair<double, QString>> labels;
		for (const Placed& one : placed) labels.push_back(one.label);
		return labels;
	}
	// No label fits whole: the middle station keeps the start of its shortest spelling.
	if (inRange.empty()) return {};
	const StationTick& middle = *inRange[inRange.size() / 2].station;
	return {{middle.position, elide(middle.spellings.last(), plot.width() / 2.0 - slack)}};
}

double clockTickInterval(double low, double high) {
	const double raw = (high - low) / 6;
	const double power = std::pow(10.0, std::floor(std::log10(std::max(1.0, raw))));
	double interval = std::max(1.0, std::ceil(raw / power) * power);
	for (double candidate : {1., 5., 10., 30., 60., 120., 300., 600., 900., 1800., 3600., 7200., 14400., 21600., 43200.})
		if ((high - low) / candidate <= 8) {
			interval = candidate;
			break;
		}
	return interval;
}

std::vector<std::pair<double, QString>> clockTicks(double low, double high, double interval, long long startOffset, double pixelsPerSecond,
	const ChartTextWidth& labelRoom) {
	// A tick on a whole minute reads HH:MM while the ticks are a minute apart or more, so that no two ticks read the
	// same minute; every other tick keeps its seconds.
	const bool minutes = interval >= 60;
	const auto clockLabel = [startOffset, minutes](double tick) {
		const auto elapsed = static_cast<long long>(tick);
		QString text = QString::fromStdString(formatSimTime(elapsed, startOffset));
		if (minutes && (elapsed + startOffset) % 60 == 0) text.truncate(5);
		return text;
	};
	const bool runStart = low <= 0 && high >= 0;
	const double offset = static_cast<double>(startOffset);
	std::vector<std::pair<double, QString>> ticks;
	if (runStart) ticks.emplace_back(0, clockLabel(0));
	// NOLINTNEXTLINE(bugprone-float-loop-counter,clang-analyzer-security.FloatLoopCounter): ticks are placed at multiples of the interval
	for (double tick = std::ceil((low + offset) / interval) * interval - offset; tick <= high; tick += interval) {
		const QString label = clockLabel(tick);
		const double room = labelRoom(label);
		if (!runStart || std::abs(tick) >= interval || std::abs(tick) * pixelsPerSecond >= room) ticks.emplace_back(tick, label);
	}
	std::sort(ticks.begin(), ticks.end());
	return ticks;
}

std::vector<StripLabel> placedStripLabels(const std::vector<BlockSectionExtent>& sections, const std::vector<BlockSectionMarks>& marks,
	const QRectF& plot, double top, double rowHeight, int rows, const std::function<double(double km)>& xAt,
	const std::function<double(const BlockSectionExtent& section, bool strong)>& idWidth, std::size_t* candidates) {
	std::vector<StripLabel> placed;
	if (candidates) *candidates = 0;
	if (plot.width() <= 8) return placed;
	struct Candidate {
		int priority;
		std::size_t index;
		double centre;
		double width;
		bool strong;
	};
	std::vector<Candidate> found;
	std::size_t first = sections.size(), last = 0;
	for (std::size_t i = 0; i < sections.size(); ++i)
		if (sections[i].geometryValid) {
			first = std::min(first, i);
			last = i;
		}
	for (std::size_t i = 0; i < sections.size(); ++i) {
		const BlockSectionExtent& section = sections[i];
		if (!section.geometryValid) continue;
		const double left = std::max(plot.left(), std::min(xAt(section.startKm), xAt(section.endKm)));
		const double right = std::min(plot.right(), std::max(xAt(section.startKm), xAt(section.endKm)));
		if (right <= left) continue;
		const bool strong = marks[i].critical || marks[i].conflict;
		const double width = idWidth(section, strong);
		if (width > plot.width()) continue;
		int priority = 4;
		if (strong) priority = 0;
		else if (i == first || i == last) priority = 1;
		else if (section.isSwitch) priority = 2;
		else if (marks[i].station) priority = 3;
		found.push_back({priority, i, std::clamp((left + right) / 2, plot.left() + width / 2, plot.right() - width / 2), width, strong});
	}
	if (candidates) *candidates = found.size();
	const auto place = [&] {
		placed.clear();
		std::vector<std::vector<std::pair<double, double>>> taken(static_cast<std::size_t>(std::max(1, rows)));
		for (const Candidate& candidate : found) {
			const double from = candidate.centre - candidate.width / 2, to = candidate.centre + candidate.width / 2;
			for (std::size_t row = 0; row < taken.size(); ++row) {
				const bool clear = std::all_of(taken[row].begin(), taken[row].end(), [&](const std::pair<double, double>& other) {
					return to + kStripLabelGap <= other.first || from - kStripLabelGap >= other.second;
				});
				if (!clear) continue;
				taken[row].emplace_back(from, to);
				placed.push_back({candidate.index, QRectF(from, top + static_cast<double>(row) * rowHeight, candidate.width, rowHeight), candidate.strong});
				break;
			}
		}
	};
	place();
	if (placed.size() == found.size()) return placed;
	std::stable_sort(found.begin(), found.end(), [](const Candidate& a, const Candidate& b) { return a.priority < b.priority; });
	place();
	return placed;
}
