#ifndef DFA_WORKER_POOL_HPP
#define DFA_WORKER_POOL_HPP

#include <cstddef>
#include <functional>

namespace dfa {

// Runs work(i) for every i in [0, count), distributed across exactly
// `num_workers` threads (never more — "bounded worker pool"), blocking
// until every index has been processed. If any call to `work` throws,
// the first exception encountered (by index order, for determinism) is
// rethrown from ParallelFor after every worker has finished — later
// work items still run to completion rather than being abandoned
// mid-flight, since hashing one file's failure shouldn't silently skip
// hashing the rest.
void ParallelFor(std::size_t count, std::size_t num_workers,
                  const std::function<void(std::size_t)>& work);

}  // namespace dfa

#endif
