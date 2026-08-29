#include "util/PlaybackProfiler.h"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <memory>

class PlaybackProfilerTestAccess {
public:
	static PlaybackProfiler& reset() {
		auto& profiler = PlaybackProfiler::instance();
		profiler.m_enabled = true;
		profiler.m_conflict = false;
		profiler.m_armed.store(false);
		profiler.m_measuring.store(false);
		profiler.m_frozen.store(false);
		profiler.m_epoch.fetch_add(1);
		profiler.m_aggregates.clear();
		auto& pending = profiler.pendingTree();
		pending.active = false;
		pending.records.clear();
		pending.scopes.clear();
		return profiler;
	}

	static void begin(PlaybackProfiler& profiler) {
		profiler.arm(1);
		profiler.beginAfterPaint(1.0);
	}

	static bool pathsAre(PlaybackProfiler& profiler, std::initializer_list<const char*> paths) {
		if (profiler.m_aggregates.size() != paths.size())
			return false;
		for (const char* path : paths) {
			bool found = false;
			for (const auto& entry : profiler.m_aggregates)
				found |= entry.second.path == path;
			if (!found)
				return false;
		}
		return true;
	}

	static bool reservoirCoversFullWindow() {
		PlaybackProfiler::Aggregate aggregate;
		for (int index = 0; index < 4096; ++index)
			aggregate.add(1);
		for (int index = 0; index < 12288; ++index)
			aggregate.add(1000);

		auto samples = aggregate.samples;
		std::sort(samples.begin(), samples.end());
		const auto quantile = [&samples](double fraction) {
			return samples[static_cast<std::size_t>((samples.size() - 1) * fraction + 0.5)];
		};
		return aggregate.calls == 16384
			&& aggregate.total == 4096ULL + 12288ULL * 1000ULL
			&& aggregate.minimum == 1 && aggregate.maximum == 1000
			&& samples.size() == 4096
			&& quantile(0.5) == 1000 && quantile(0.95) == 1000;
	}
};

namespace {
bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << message << '\n';
	return condition;
}
}

int main() {
	bool ok = expect(PlaybackProfilerTestAccess::reservoirCoversFullWindow(),
		"deterministic reservoir did not represent the full observation window");

	{
		auto& profiler = PlaybackProfilerTestAccess::reset();
		PlaybackProfilerTestAccess::begin(profiler);
		{
			PlaybackProfiler::Scope root("root", "gui", "");
			PlaybackProfiler::Scope child("root/child", "gui", "root");
		}
		profiler.freeze(2);
		ok &= expect(PlaybackProfilerTestAccess::pathsAre(profiler, {"root", "root/child"}),
			"a complete same-epoch root tree was not committed atomically");
	}
	{
		auto& profiler = PlaybackProfilerTestAccess::reset();
		{
			PlaybackProfiler::Scope root("cross-begin", "gui", "");
			PlaybackProfilerTestAccess::begin(profiler);
			PlaybackProfiler::Scope child("cross-begin/child", "gui", "cross-begin");
		}
		profiler.freeze(2);
		ok &= expect(PlaybackProfilerTestAccess::pathsAre(profiler, {}),
			"begin retained a root tree that was already live");
	}
	{
		auto& profiler = PlaybackProfilerTestAccess::reset();
		PlaybackProfilerTestAccess::begin(profiler);
		{
			PlaybackProfiler::Scope root("cross-freeze", "worker", "");
			{
				PlaybackProfiler::Scope child("cross-freeze/child", "worker", "cross-freeze");
			}
			profiler.freeze(2);
		}
		ok &= expect(PlaybackProfilerTestAccess::pathsAre(profiler, {}),
			"freeze retained part of a live root tree");

		PlaybackProfilerTestAccess::begin(profiler);
		{ PlaybackProfiler::Scope root("next-epoch", "worker", ""); }
		profiler.freeze(3);
		ok &= expect(PlaybackProfilerTestAccess::pathsAre(profiler, {"next-epoch"}),
			"the next measurement epoch was not independent");
	}
	{
		auto& profiler = PlaybackProfilerTestAccess::reset();
		PlaybackProfilerTestAccess::begin(profiler);
		auto oldRoot = std::make_unique<PlaybackProfiler::Scope>("old-root", "gui", "");
		auto oldChild = std::make_unique<PlaybackProfiler::Scope>(
			"old-root/child", "gui", "old-root");
		oldRoot.reset();
		{
			PlaybackProfiler::Scope newRoot("new-root", "gui", "");
			oldChild.reset();
		}
		profiler.freeze(2);
		ok &= expect(PlaybackProfilerTestAccess::pathsAre(profiler, {"new-root"}),
			"a stale scope invalidated a newer root tree");
	}
	{
		auto& profiler = PlaybackProfilerTestAccess::reset();
		PlaybackProfilerTestAccess::begin(profiler);
		{ PlaybackProfiler::Scope child("orphan", "render", "missing-root"); }
		profiler.freeze(2);
		ok &= expect(PlaybackProfilerTestAccess::pathsAre(profiler, {}),
			"a child committed without an active root");
	}
	{
		auto& profiler = PlaybackProfilerTestAccess::reset();
		PlaybackProfilerTestAccess::begin(profiler);
		{
			PlaybackProfiler::Scope root("root", "gui", "");
			{
				PlaybackProfiler::Scope malformed("root/missing/child", "gui", "root/missing");
				PlaybackProfiler::Scope descendant("root/missing/child/descendant", "gui",
					"root/missing/child");
			}
			PlaybackProfiler::Scope sibling("root/sibling", "gui", "root");
		}
		profiler.freeze(2);
		ok &= expect(PlaybackProfilerTestAccess::pathsAre(profiler, {"root", "root/sibling"}),
			"a child activated without its declared direct parent");
	}
	return ok ? 0 : 1;
}
