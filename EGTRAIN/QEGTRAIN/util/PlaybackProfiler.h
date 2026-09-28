#ifndef PLAYBACKPROFILER_H
#define PLAYBACKPROFILER_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class PlaybackProfiler {
public:
	struct RunConfig {
		int trial = 0;
		std::string view;
		std::string platform;
		std::string architecture;
		std::string qtPlatform;
		std::string buildType;
		std::string caseName;
		std::string requestedScenario;
		std::string scenario;
		std::string defaultScenario;
		double targetZoom = 1.0;
		double actualZoom = 1.0;
		int durationMs = 0;
		int delayMs = 0;
		bool passengerGui = false;
		bool tsm = false;
		bool routeChoice = false;
		int horizon = 0;
		bool structural = false;
	};

	class Scope {
	public:
		Scope(const char* path, const char* lane, const char* parent);
		~Scope();
		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;
	private:
		const char* m_path = nullptr;
		const char* m_lane = nullptr;
		const char* m_parent = nullptr;
		std::chrono::steady_clock::time_point m_started;
		std::uint64_t m_epoch = 0;
		std::uint64_t m_rootGeneration = 0;
		bool m_active = false;
		bool m_root = false;
	};

	static PlaybackProfiler& instance();
	static bool enabled();
	static bool startupTimingConflict();

	void configure(const RunConfig& config);
	void arm(int timestep);
	bool armed() const { return m_armed.load(std::memory_order_relaxed); }
	bool beginAfterPaint(double actualZoom);
	bool freeze(int timestep);
	bool frozen() const { return m_frozen.load(std::memory_order_relaxed); }
	bool measuring() const { return m_measuring.load(std::memory_order_relaxed); }
	int durationMs() const { return m_durationMs.load(std::memory_order_relaxed); }

	void noteTimestep(int timestep);
	void noteDelivery();
	void noteRenderedUpdate();
	void notePaint();
	void emitRecords(bool cleanStop);

private:
	friend class Scope;
	friend class PlaybackProfilerTestAccess;
	struct Aggregate {
		std::string path;
		std::string lane;
		std::string parent;
		std::uint64_t calls = 0;
		std::uint64_t total = 0;
		std::uint64_t minimum = 0;
		std::uint64_t maximum = 0;
		std::uint64_t reservoirState = 0x9e3779b97f4a7c15ULL;
		std::vector<std::uint64_t> samples;

		void add(std::uint64_t nanoseconds);
	};

	struct PendingRecord {
		const char* path;
		const char* lane;
		const char* parent;
		std::uint64_t nanoseconds;
	};
	struct PendingTree {
		std::uint64_t epoch = 0;
		std::uint64_t rootGeneration = 0;
		bool active = false;
		std::vector<PendingRecord> records;
		std::vector<const char*> scopes;
	};

	PlaybackProfiler();
	static PendingTree& pendingTree();
	void commitTree(std::uint64_t epoch, const std::vector<PendingRecord>& records);

	bool m_enabled = false;
	bool m_conflict = false;
	std::atomic<bool> m_armed{false};
	std::atomic<bool> m_measuring{false};
	std::atomic<bool> m_frozen{false};
	std::atomic<bool> m_emitted{false};
	std::atomic<int> m_durationMs{0};
	std::atomic<std::uint64_t> m_epoch{0};
	mutable std::mutex m_mutex;
	RunConfig m_config;
	std::unordered_map<std::string, Aggregate> m_aggregates;
	std::chrono::steady_clock::time_point m_started;
	std::uint64_t m_observedNs = 0;
	int m_startTimestep = -1;
	int m_endTimestep = -1;
	std::uint64_t m_timesteps = 0;
	std::uint64_t m_deliveries = 0;
	std::uint64_t m_renderedUpdates = 0;
	std::uint64_t m_paints = 0;
};

#define QEGTRAIN_PROFILE_SCOPE_NAME2(line) playbackProfilerScope##line
#define QEGTRAIN_PROFILE_SCOPE_NAME(line) QEGTRAIN_PROFILE_SCOPE_NAME2(line)
#define QEGTRAIN_PROFILE_SCOPE(path, lane, parent) \
	PlaybackProfiler::Scope QEGTRAIN_PROFILE_SCOPE_NAME(__LINE__)(path, lane, parent)

#endif
