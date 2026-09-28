#include "util/PlaybackProfiler.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <nlohmann/json.hpp>

namespace {
bool gate(const char* name) {
	const char* value = std::getenv(name);
	return value && std::strcmp(value, "1") == 0;
}

std::uint64_t percentile(const std::vector<std::uint64_t>& sorted, double fraction) {
	if (sorted.empty())
		return 0;
	const std::size_t index = static_cast<std::size_t>((sorted.size() - 1) * fraction + 0.5);
	return sorted[std::min(index, sorted.size() - 1)];
}

void output(const nlohmann::json& record) {
	const std::string text = record.dump();
	std::fprintf(stdout, "\nQEGTRAIN_PLAYBACK_PROFILE %s\n", text.c_str());
}
}

PlaybackProfiler& PlaybackProfiler::instance() {
	static PlaybackProfiler profiler;
	return profiler;
}

PlaybackProfiler::PlaybackProfiler()
	: m_enabled(gate("QEGTRAIN_PLAYBACK_PROFILE")),
	  m_conflict(m_enabled && (gate("QEGTRAIN_STARTUP_TIMING")
		  || gate("QEGTRAIN_STARTUP_NATIVE_DETAIL"))) {
}

bool PlaybackProfiler::enabled() {
	return instance().m_enabled;
}

bool PlaybackProfiler::startupTimingConflict() {
	return instance().m_conflict;
}

PlaybackProfiler::PendingTree& PlaybackProfiler::pendingTree() {
	thread_local PendingTree tree;
	return tree;
}

PlaybackProfiler::Scope::Scope(const char* path, const char* lane, const char* parent)
	: m_path(path), m_lane(lane), m_parent(parent) {
	auto& profiler = PlaybackProfiler::instance();
	if (!profiler.m_measuring.load(std::memory_order_acquire))
		return;
	const auto epoch = profiler.m_epoch.load(std::memory_order_acquire);
	auto& tree = profiler.pendingTree();
	m_root = parent[0] == '\0';
	if (m_root) {
		if (tree.active)
			return;
		tree.active = true;
		tree.epoch = epoch;
		m_rootGeneration = ++tree.rootGeneration;
		tree.records.clear();
		tree.scopes.clear();
	} else if (!tree.active || tree.epoch != epoch || tree.scopes.empty()
			|| std::strcmp(parent, tree.scopes.back()) != 0) {
		return;
	} else {
		m_rootGeneration = tree.rootGeneration;
	}
	if (!profiler.m_measuring.load(std::memory_order_acquire)
			|| profiler.m_epoch.load(std::memory_order_acquire) != epoch) {
		if (m_root) {
			tree.active = false;
			tree.scopes.clear();
		}
		return;
	}
	m_epoch = epoch;
	m_active = true;
	tree.scopes.push_back(path);
	m_started = std::chrono::steady_clock::now();
}

PlaybackProfiler::Scope::~Scope() {
	if (!m_active)
		return;
	const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::steady_clock::now() - m_started).count();
	auto& profiler = PlaybackProfiler::instance();
	auto& tree = profiler.pendingTree();
	if (!tree.active || tree.epoch != m_epoch || tree.rootGeneration != m_rootGeneration)
		return;
	if (tree.scopes.empty() || tree.scopes.back() != m_path) {
		tree.active = false;
		tree.records.clear();
		tree.scopes.clear();
		return;
	}
	if (elapsed >= 0)
		tree.records.push_back({m_path, m_lane, m_parent, static_cast<std::uint64_t>(elapsed)});
	tree.scopes.pop_back();
	if (!m_root)
		return;
	tree.active = false;
	profiler.commitTree(m_epoch, tree.records);
	tree.records.clear();
}

void PlaybackProfiler::configure(const RunConfig& config) {
	if (!m_enabled)
		return;
	std::lock_guard<std::mutex> lock(m_mutex);
	m_config = config;
	m_durationMs.store(config.durationMs, std::memory_order_relaxed);
}

void PlaybackProfiler::arm(int timestep) {
	if (!m_enabled || m_conflict)
		return;
	std::lock_guard<std::mutex> lock(m_mutex);
	m_startTimestep = timestep;
	m_armed.store(true, std::memory_order_release);
}

bool PlaybackProfiler::beginAfterPaint(double actualZoom) {
	if (!m_enabled || m_conflict || !m_armed.exchange(false, std::memory_order_acq_rel))
		return false;
	std::lock_guard<std::mutex> lock(m_mutex);
	m_config.actualZoom = actualZoom;
	m_aggregates.clear();
	m_observedNs = 0;
	m_endTimestep = -1;
	m_timesteps = 0;
	m_deliveries = 0;
	m_renderedUpdates = 0;
	m_paints = 0;
	m_frozen.store(false, std::memory_order_relaxed);
	m_started = std::chrono::steady_clock::now();
	m_epoch.fetch_add(1, std::memory_order_release);
	m_measuring.store(true, std::memory_order_release);
	return true;
}

bool PlaybackProfiler::freeze(int timestep) {
	if (!m_measuring.exchange(false, std::memory_order_acq_rel))
		return false;
	m_epoch.fetch_add(1, std::memory_order_acq_rel);
	const auto stopped = std::chrono::steady_clock::now();
	std::lock_guard<std::mutex> lock(m_mutex);
	m_observedNs = static_cast<std::uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(stopped - m_started).count());
	m_endTimestep = timestep;
	m_frozen.store(true, std::memory_order_release);
	return true;
}

void PlaybackProfiler::Aggregate::add(std::uint64_t nanoseconds) {
	constexpr std::uint64_t sampleLimit = 4096;
	++calls;
	total += nanoseconds;
	minimum = calls == 1 ? nanoseconds : std::min(minimum, nanoseconds);
	maximum = std::max(maximum, nanoseconds);
	if (samples.size() < sampleLimit) {
		samples.push_back(nanoseconds);
		return;
	}
	// Deterministic Algorithm R reservoir: every observation has equal inclusion probability.
	reservoirState ^= reservoirState >> 12;
	reservoirState ^= reservoirState << 25;
	reservoirState ^= reservoirState >> 27;
	const std::uint64_t selected = (reservoirState * 2685821657736338717ULL) % calls;
	if (selected < sampleLimit)
		samples[static_cast<std::size_t>(selected)] = nanoseconds;
}

void PlaybackProfiler::commitTree(std::uint64_t epoch,
		const std::vector<PendingRecord>& records) {
	if (!m_measuring.load(std::memory_order_acquire)
			|| m_epoch.load(std::memory_order_acquire) != epoch)
		return;
	std::lock_guard<std::mutex> lock(m_mutex);
	if (!m_measuring.load(std::memory_order_relaxed)
			|| m_epoch.load(std::memory_order_relaxed) != epoch)
		return;
	for (const auto& record : records) {
		const std::string key = std::string(record.lane) + '\n' + record.path;
		auto& aggregate = m_aggregates[key];
		if (aggregate.calls == 0) {
			aggregate.path = record.path;
			aggregate.lane = record.lane;
			aggregate.parent = record.parent;
		}
		aggregate.add(record.nanoseconds);
	}
}

void PlaybackProfiler::noteTimestep(int timestep) {
	if (!measuring())
		return;
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_measuring.load(std::memory_order_relaxed)) {
		m_endTimestep = timestep;
		++m_timesteps;
	}
}

void PlaybackProfiler::noteDelivery() {
	if (!measuring()) return;
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_measuring.load(std::memory_order_relaxed)) ++m_deliveries;
}
void PlaybackProfiler::noteRenderedUpdate() {
	if (!measuring()) return;
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_measuring.load(std::memory_order_relaxed)) ++m_renderedUpdates;
}
void PlaybackProfiler::notePaint() {
	if (!measuring()) return;
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_measuring.load(std::memory_order_relaxed)) ++m_paints;
}

void PlaybackProfiler::emitRecords(bool cleanStop) {
	if (!m_enabled || m_emitted.exchange(true) || !m_frozen.load())
		return;
	std::lock_guard<std::mutex> lock(m_mutex);
	output({
		{"type", "run"}, {"schema", 1}, {"trial", m_config.trial}, {"view", m_config.view},
		{"platform", m_config.platform}, {"architecture", m_config.architecture},
		{"qt_platform", m_config.qtPlatform}, {"build_type", m_config.buildType},
		{"case", m_config.caseName}, {"requested_scenario", m_config.requestedScenario},
		{"scenario", m_config.scenario}, {"default_scenario", m_config.defaultScenario},
		{"target_zoom", m_config.targetZoom}, {"actual_zoom", m_config.actualZoom},
		{"duration_ms", m_config.durationMs}, {"delay_ms", m_config.delayMs},
		{"passenger_gui", m_config.passengerGui}, {"tsm", m_config.tsm},
		{"route_choice", m_config.routeChoice}, {"horizon", m_config.horizon},
		{"clock", "steady_clock_ns"}, {"scope", "post_startup_playback"},
		{"mode", m_config.structural ? "structural" : "recorded"}
	});
	std::vector<const Aggregate*> ordered;
	for (const auto& entry : m_aggregates)
		ordered.push_back(&entry.second);
	std::sort(ordered.begin(), ordered.end(), [](const Aggregate* left, const Aggregate* right) {
		return left->path < right->path;
	});
	for (const Aggregate* aggregate : ordered) {
		auto samples = aggregate->samples;
		std::sort(samples.begin(), samples.end());
		output({
			{"type", "aggregate"}, {"path", aggregate->path}, {"lane", aggregate->lane},
			{"parent", aggregate->parent}, {"calls", aggregate->calls},
			{"total_ns", aggregate->total}, {"min_ns", aggregate->minimum},
			{"median_ns", percentile(samples, 0.5)}, {"p95_ns", percentile(samples, 0.95)},
			{"max_ns", aggregate->maximum}
		});
	}
	output({
		{"type", "completion"}, {"observed_ns", m_observedNs},
		{"start_timestep", m_startTimestep}, {"end_timestep", m_endTimestep},
		{"timesteps", m_timesteps}, {"deliveries", m_deliveries},
		{"rendered_updates", m_renderedUpdates}, {"paints", m_paints},
		{"clean_stop", cleanStop}, {"validation", "complete"}, {"post_freeze_records", 0}
	});
	std::fflush(stdout);
}
