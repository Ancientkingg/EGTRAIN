#include "util/PlaybackProfiler.h"

#include <QTest>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
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

	// Checks with QCOMPARE, so a slot calls it as its last statement.
	static void reservoirCoversFullWindow() {
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
		// The deterministic reservoir represents the full observation window.
		QCOMPARE(aggregate.calls, std::uint64_t(16384));
		QCOMPARE(aggregate.total, std::uint64_t(4096ULL + 12288ULL * 1000ULL));
		QCOMPARE(aggregate.minimum, std::uint64_t(1));
		QCOMPARE(aggregate.maximum, std::uint64_t(1000));
		QCOMPARE(samples.size(), std::size_t(4096));
		QCOMPARE(quantile(0.5), std::uint64_t(1000));
		QCOMPARE(quantile(0.95), std::uint64_t(1000));
	}
};

// The slots that time scopes start from a reset profiler, so only the root trees of their own window count.
class TestPlaybackProfiler : public QObject {
	Q_OBJECT

private slots:
	void reservoirCoversFullWindow();
	void commitsCompleteRootTree();
	void dropsRootTreeLiveAtBegin();
	void dropsRootTreeLiveAtFreeze();
	void keepsNewerRootTreeAfterStaleScope();
	void dropsChildWithoutRoot();
	void dropsChildWithoutDirectParent();
};

void TestPlaybackProfiler::reservoirCoversFullWindow() {
	PlaybackProfilerTestAccess::reservoirCoversFullWindow();
}

void TestPlaybackProfiler::commitsCompleteRootTree() {
	auto& profiler = PlaybackProfilerTestAccess::reset();
	PlaybackProfilerTestAccess::begin(profiler);
	{
		PlaybackProfiler::Scope root("root", "gui", "");
		PlaybackProfiler::Scope child("root/child", "gui", "root");
	}
	profiler.freeze(2);
	QVERIFY2(PlaybackProfilerTestAccess::pathsAre(profiler, {"root", "root/child"}),
		"a complete same-epoch root tree was not committed atomically");
}

void TestPlaybackProfiler::dropsRootTreeLiveAtBegin() {
	auto& profiler = PlaybackProfilerTestAccess::reset();
	{
		PlaybackProfiler::Scope root("cross-begin", "gui", "");
		PlaybackProfilerTestAccess::begin(profiler);
		PlaybackProfiler::Scope child("cross-begin/child", "gui", "cross-begin");
	}
	profiler.freeze(2);
	QVERIFY2(PlaybackProfilerTestAccess::pathsAre(profiler, {}),
		"begin retained a root tree that was already live");
}

void TestPlaybackProfiler::dropsRootTreeLiveAtFreeze() {
	auto& profiler = PlaybackProfilerTestAccess::reset();
	PlaybackProfilerTestAccess::begin(profiler);
	{
		PlaybackProfiler::Scope root("cross-freeze", "worker", "");
		{
			PlaybackProfiler::Scope child("cross-freeze/child", "worker", "cross-freeze");
		}
		profiler.freeze(2);
	}
	QVERIFY2(PlaybackProfilerTestAccess::pathsAre(profiler, {}),
		"freeze retained part of a live root tree");

	PlaybackProfilerTestAccess::begin(profiler);
	{ PlaybackProfiler::Scope root("next-epoch", "worker", ""); }
	profiler.freeze(3);
	QVERIFY2(PlaybackProfilerTestAccess::pathsAre(profiler, {"next-epoch"}),
		"the next measurement epoch was not independent");
}

void TestPlaybackProfiler::keepsNewerRootTreeAfterStaleScope() {
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
	QVERIFY2(PlaybackProfilerTestAccess::pathsAre(profiler, {"new-root"}),
		"a stale scope invalidated a newer root tree");
}

void TestPlaybackProfiler::dropsChildWithoutRoot() {
	auto& profiler = PlaybackProfilerTestAccess::reset();
	PlaybackProfilerTestAccess::begin(profiler);
	{ PlaybackProfiler::Scope child("orphan", "render", "missing-root"); }
	profiler.freeze(2);
	QVERIFY2(PlaybackProfilerTestAccess::pathsAre(profiler, {}),
		"a child committed without an active root");
}

void TestPlaybackProfiler::dropsChildWithoutDirectParent() {
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
	QVERIFY2(PlaybackProfilerTestAccess::pathsAre(profiler, {"root", "root/sibling"}),
		"a child activated without its declared direct parent");
}

QTEST_APPLESS_MAIN(TestPlaybackProfiler)
#include "test_playbackprofiler.moc"
