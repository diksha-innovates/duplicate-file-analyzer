#include "dfa/worker_pool.hpp"

#include <atomic>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

using namespace dfa;

TEST(WorkerPool, RunsEveryIndexExactlyOnce) {
    constexpr std::size_t kCount = 1000;
    std::vector<std::atomic<int>> seen(kCount);
    for (auto& s : seen) s = 0;

    ParallelFor(kCount, 8, [&](std::size_t i) { seen[i]++; });

    for (std::size_t i = 0; i < kCount; i++) {
        EXPECT_EQ(seen[i].load(), 1) << "index " << i;
    }
}

TEST(WorkerPool, NeverUsesMoreThreadsThanRequested) {
    constexpr std::size_t kWorkers = 4;
    std::mutex mutex;
    std::set<std::thread::id> thread_ids;

    ParallelFor(500, kWorkers, [&](std::size_t) {
        std::lock_guard<std::mutex> lock(mutex);
        thread_ids.insert(std::this_thread::get_id());
    });

    EXPECT_LE(thread_ids.size(), kWorkers);
}

TEST(WorkerPool, HandlesEmptyInput) {
    bool called = false;
    ParallelFor(0, 4, [&](std::size_t) { called = true; });
    EXPECT_FALSE(called);
}

TEST(WorkerPool, SingleWorkerRunsSynchronouslyWithoutSpawningThreads) {
    std::thread::id main_id = std::this_thread::get_id();
    bool ran_on_main_thread = true;
    ParallelFor(10, 1, [&](std::size_t) {
        if (std::this_thread::get_id() != main_id) ran_on_main_thread = false;
    });
    EXPECT_TRUE(ran_on_main_thread);
}

TEST(WorkerPool, ExceptionFromOneItemPropagatesAfterAllItemsRun) {
    constexpr std::size_t kCount = 50;
    std::atomic<int> completed{0};

    EXPECT_THROW(
        ParallelFor(kCount, 4,
                    [&](std::size_t i) {
                        completed++;
                        if (i == 10) {
                            throw std::runtime_error("boom");
                        }
                    }),
        std::runtime_error);

    EXPECT_EQ(completed.load(), static_cast<int>(kCount));
}

TEST(WorkerPool, SmallWorkloadsRunOnCallingThreadToAvoidSpawnOverhead) {
    // Regression test for a real, benchmark-discovered perf issue: tiny
    // workloads (e.g. a 5-file duplicate group) must not pay OS
    // thread-spawn/join cost even when num_workers > 1.
    std::thread::id main_id = std::this_thread::get_id();
    bool ran_on_main_thread = true;
    ParallelFor(5, 8, [&](std::size_t) {
        if (std::this_thread::get_id() != main_id) ran_on_main_thread = false;
    });
    EXPECT_TRUE(ran_on_main_thread);
}

TEST(WorkerPool, RequestingMoreWorkersThanItemsIsFine) {
    int sum = 0;
    std::mutex m;
    ParallelFor(3, 100, [&](std::size_t i) {
        std::lock_guard<std::mutex> lock(m);
        sum += static_cast<int>(i);
    });
    EXPECT_EQ(sum, 0 + 1 + 2);
}
