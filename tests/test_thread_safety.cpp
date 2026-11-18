// =============================================================================
// 线程安全契约测试（B0-2）
// =============================================================================
//
// 背景：全库无 mutex/atomic（grep 确认 0 处）。这不代表"不需要线程安全"，
// 而是"没有保护"。真实并发场景是：
//   · UI 线程：用户拖动片段
//   · 导出线程：遍历 project 渲染（可能持续数分钟）
//
// 若不处理，导出线程遍历 tracks/clips 时 UI 线程 erase 元素 →
// 迭代器失效 → 随机崩溃（这类崩溃极难复现，因为依赖时序）。
//
// 本测试验证的**不是** TimelineEditor 可以并发调用（它明确不是线程安全的），
// 而是验证采用的方案成立：**Project 可安全深拷贝，快照与原件互不影响**。
// 这是 nc_export_run 快照机制的前提。

#include "test_framework.h"

#include "nairoclip/core/editor.h"
#include "nairoclip/core/project.h"
#include "nairoclip/core/rational.h"
#include "nairoclip/core/thread_pool.h"

#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <vector>

using namespace nairoclip::core;

namespace {

Project makeProject(int clipCount) {
    Project project;
    project.name = "并发测试工程";
    Sequence seq;
    seq.id = SequenceId{project.ids.next()};
    seq.frameRate = fps30();
    Track track;
    track.id = TrackId{project.ids.next()};
    track.type = TrackType::Video;
    track.name = "V1";
    for (int i = 0; i < clipCount; ++i) {
        Clip c;
        c.id = ClipId{project.ids.next()};
        c.start = Rational::make(i * 2);
        c.duration = Rational::make(2);
        c.sourceIn = Rational::make(0);
        c.speed = Rational::make(1);
        track.clips.push_back(c);
    }
    seq.tracks.push_back(track);
    project.sequences.push_back(seq);
    return project;
}

}  // namespace

NC_TEST(ThreadSafety, ProjectDeepCopyIsIndependent) {
    Project original = makeProject(10);
    NC_CHECK(original.sequences.size() == 1);
    NC_CHECK(original.sequences[0].tracks[0].clips.size() == 10);

    // 深拷贝
    Project snapshot = original;

    // 修改原件（模拟用户在导出期间继续编辑）
    original.sequences[0].tracks[0].clips.erase(
        original.sequences[0].tracks[0].clips.begin());

    // 快照必须不受影响 —— 这是导出快照方案的核心前提
    NC_CHECK(original.sequences[0].tracks[0].clips.size() == 9);
    NC_CHECK(snapshot.sequences[0].tracks[0].clips.size() == 10);

    // 快照内容也必须独立（不只是 vector 大小）：
    // 原件 erase 了首元素，故原件首元素 id 已变，快照首元素仍是原值。
    NC_CHECK(snapshot.sequences[0].tracks[0].clips.back().id ==
             original.sequences[0].tracks[0].clips.back().id);  // 尾部相同
    NC_CHECK(snapshot.sequences[0].tracks[0].clips[0].id !=
             original.sequences[0].tracks[0].clips[0].id);      // 首部已不同
}

NC_TEST(ThreadSafety, SnapshotUnaffectedByConcurrentEdits) {
    Project original = makeProject(50);
    // 模拟"点下导出那一刻"的快照
    const Project snapshot = original;
    const std::size_t snapshotClipCount = snapshot.sequences[0].tracks[0].clips.size();
    NC_CHECK(snapshotClipCount == 50);

    // 后台线程反复读取快照（模拟导出渲染），同时主线程疯狂改原件。
    // 若快照与原件共享存储，这里会崩溃或读到不一致数据。
    std::atomic<bool> stop{false};
    std::atomic<int> reads{0};
    std::thread reader([&] {
        while (!stop.load()) {
            // 遍历快照的片段（导出线程做的事）
            std::size_t count = 0;
            for (const auto& clip : snapshot.sequences[0].tracks[0].clips) {
                if (clip.duration.isPositive()) {
                    ++count;
                }
            }
            if (count != snapshotClipCount) {
                // 数量变化说明快照被污染
                std::printf("    [FAIL] 快照被并发修改污染\n");
                break;
            }
            ++reads;
        }
    });

    // 先等读取线程真正跑起来（否则主线程可能瞬间跑完 200 次循环，
    // 读取线程一次都没轮到 —— 这是测试自身的时序问题，不是代码缺陷）。
    for (int spin = 0; spin < 1000 && reads.load() == 0; ++spin) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // 主线程：反复删除/插入片段（模拟 UI 编辑）
    for (int i = 0; i < 200; ++i) {
        auto& clips = original.sequences[0].tracks[0].clips;
        if (!clips.empty()) {
            clips.pop_back();
        }
        Clip c;
        c.id = ClipId{100000 + i};
        c.start = Rational::make(1000 + i * 2);
        c.duration = Rational::make(2);
        clips.push_back(c);
    }

    stop.store(true);
    reader.join();

    // 快照必须完好无损
    NC_CHECK(snapshot.sequences[0].tracks[0].clips.size() == snapshotClipCount);
    NC_CHECK(reads.load() > 0);  // 确认读取线程真的跑过
    // 原件已被修改（证明两个对象确实独立）
    NC_CHECK(original.sequences[0].tracks[0].clips.size() != snapshotClipCount ||
             original.sequences[0].tracks[0].clips.back().id !=
                 snapshot.sequences[0].tracks[0].clips.back().id);
}

NC_TEST(ThreadSafety, RationalIsSafeForConcurrentReadOnly) {
    // Rational 是纯值类型，只读并发天然安全。
    // 这条用例锁定"不引入可变全局状态"这一前提
    // （rational.cpp 的溢出标志是 thread_local，不跨线程污染）。
    const Rational fps = Rational::make(30000, 1001);
    std::atomic<bool> stop{false};
    std::atomic<int> ok{0};

    std::vector<std::thread> workers;
    for (int t = 0; t < 4; ++t) {
        workers.emplace_back([&] {
            while (!stop.load()) {
                const Rational frames = Rational::make(3600) * fps;
                if (frames == Rational::make(108000000, 1001)) {
                    ++ok;
                }
            }
        });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    stop.store(true);
    for (auto& w : workers) w.join();
    NC_CHECK(ok.load() > 0);
}


NC_TEST(ThreadPool, NonDivisibleWorkItemsAreAllProcessedExactlyOnce) {
    ThreadPool& pool = ThreadPool::instance();
    std::array<std::atomic<int>, 10> visits{};
    for (auto& visit : visits) visit.store(0);
    std::atomic<int> processed{0};

    // 10 项 / 最多 8 个 worker 会形成不能被固定 ceil-chunk 整除的分段。
    pool.parallelFor(visits.size(), 1, [&](std::size_t begin, std::size_t end) {
        NC_CHECK(begin < end);
        for (std::size_t i = begin; i < end; ++i) {
            visits[i].fetch_add(1);
            processed.fetch_add(1);
        }
    });

    NC_CHECK_EQ(processed.load(), 10);
    for (const auto& visit : visits) NC_CHECK_EQ(visit.load(), 1);
}

NC_TEST(ThreadPool, ReentrantParallelForFallsBackWithoutDeadlock) {
    ThreadPool& pool = ThreadPool::instance();
    std::atomic<bool> entered{false};
    std::atomic<int> nestedItems{0};

    pool.parallelFor(128, 1, [&](std::size_t, std::size_t) {
        if (!entered.exchange(true)) {
            // 外层批次仍在运行时重入；应在当前线程同步执行，而不是持锁回调。
            pool.parallelFor(7, 1, [&](std::size_t begin, std::size_t end) {
                nestedItems.fetch_add(static_cast<int>(end - begin));
            });
        }
    });

    NC_CHECK(entered.load());
    NC_CHECK_EQ(nestedItems.load(), 7);
}


NC_TEST(ThreadPool, WorkerExceptionIsRethrownAndPoolRemainsUsable) {
    ThreadPool& pool = ThreadPool::instance();
    bool caught = false;

    try {
        pool.parallelFor(64, 1, [](std::size_t, std::size_t) {
            throw std::runtime_error("worker callback failure");
        });
    } catch (const std::runtime_error&) {
        caught = true;
    }

    NC_CHECK(caught);

    // 异常批次必须完整收尾并清理状态，线程池随后仍可接受新任务。
    std::atomic<int> processed{0};
    pool.parallelFor(4, 1, [&](std::size_t begin, std::size_t end) {
        processed.fetch_add(static_cast<int>(end - begin));
    });
    NC_CHECK_EQ(processed.load(), 4);
}
