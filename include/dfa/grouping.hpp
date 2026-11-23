#ifndef DFA_GROUPING_HPP
#define DFA_GROUPING_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "dfa/cache.hpp"
#include "dfa/scanner.hpp"

namespace dfa {

struct DuplicateGroup {
    std::uint64_t size = 0;
    std::string full_hash_hex;
    std::vector<FileRecord> members;

    // Bytes that could actually be freed by removing all-but-one copy.
    // Hardlinks (same dev+inode) already share one on-disk allocation, so
    // only the count of *distinct* (dev, inode) pairs among members minus
    // one contributes reclaimable space — a hardlinked twin is not
    // reclaimable, by definition (removing it frees nothing).
    std::uint64_t ReclaimableBytes() const;
};

struct AnalyzeOptions {
    std::size_t partial_hash_window = 64 * 1024;
    std::size_t num_workers = 4;
    // Optional final byte-for-byte comparison pass within each
    // full-hash-confirmed group, for callers who want certainty beyond a
    // SHA-256 match (practically unnecessary — a collision is not a
    // realistic concern — but included because it's cheap insurance and
    // was explicitly requested as an optional feature).
    bool verify_byte_compare = false;
    // When non-null, partial/full hashes are looked up and recorded here
    // instead of being unconditionally recomputed.
    HashCache* cache = nullptr;
    // Benchmark/comparison knob: when true, skips the partial-hash
    // pre-filter stage entirely and goes straight from size-grouping to
    // full-file hashing. Exists so benchmarks/ can measure the partial
    // hash's actual benefit rather than asserting it; not something a
    // normal run should ever need to set.
    bool skip_partial_hash = false;
};

// Pipeline: group by size -> partial hash -> full hash -> (optional)
// byte-compare. Only groups that survive every stage with 2+ members are
// returned. This function never deletes, renames, or modifies any file —
// see README for why that's a hard invariant, not just a current
// limitation.
std::vector<DuplicateGroup> FindDuplicates(const std::vector<FileRecord>& files,
                                            const AnalyzeOptions& options);

}  // namespace dfa

#endif
