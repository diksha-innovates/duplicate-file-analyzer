#include "dfa/worker_pool.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace dfa {

namespace {
// Spawning and joining OS threads costs real, measurable time (observed
// directly: benchmarking this tool's own duplicate-grouping pipeline
// showed the partial-hash stage, which calls ParallelFor once per small
// subgroup, running *slower* than skipping it entirely — not because
// partial hashing does more I/O, but because thread spawn/join overhead,
// repeated once per subgroup, dominated the actual (sub-millisecond)
// hashing work for small groups). Below this many items, running on the
// calling thread avoids that overhead entirely rather than parallelizing
// work too small to amortize it.
constexpr std::size_t kMinItemsToParallelize = 32;
}  // namespace

void ParallelFor(std::size_t count, std::size_t num_workers,
                  const std::function<void(std::size_t)>& work) {
    if (count == 0) {
        return;
    }
    num_workers = std::max<std::size_t>(1, std::min(num_workers, count));
    if (count < kMinItemsToParallelize) {
        num_workers = 1;
    }

    std::atomic<std::size_t> next_index{0};
    std::mutex exceptions_mutex;
    std::vector<std::pair<std::size_t, std::exception_ptr>> exceptions;

    auto worker = [&]() {
        for (;;) {
            std::size_t i = next_index.fetch_add(1, std::memory_order_relaxed);
            if (i >= count) {
                break;
            }
            try {
                work(i);
            } catch (...) {
                std::lock_guard<std::mutex> lock(exceptions_mutex);
                exceptions.emplace_back(i, std::current_exception());
            }
        }
    };

    if (num_workers == 1) {
        worker();
    } else {
        std::vector<std::thread> threads;
        threads.reserve(num_workers);
        for (std::size_t t = 0; t < num_workers; t++) {
            threads.emplace_back(worker);
        }
        for (auto& th : threads) {
            th.join();
        }
    }

    if (!exceptions.empty()) {
        std::sort(exceptions.begin(), exceptions.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        std::rethrow_exception(exceptions.front().second);
    }
}

}  // namespace dfa
