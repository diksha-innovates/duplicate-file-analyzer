#include "dfa/grouping.hpp"

#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dfa/hash.hpp"
#include "dfa/worker_pool.hpp"

namespace dfa {

std::uint64_t DuplicateGroup::ReclaimableBytes() const {
    std::set<std::pair<std::uint64_t, std::uint64_t>> distinct_storage;
    for (const auto& m : members) {
        distinct_storage.emplace(m.dev, m.inode);
    }
    if (distinct_storage.size() <= 1) {
        return 0;
    }
    return static_cast<std::uint64_t>(distinct_storage.size() - 1) * size;
}

namespace {

std::string ComputeOrCacheHash(const FileRecord& record, HashKind kind, HashCache* cache,
                                std::size_t window) {
    if (cache) {
        if (auto cached = cache->Get(record, kind)) {
            return *cached;
        }
    }
    Sha256Digest digest = kind == HashKind::Partial
                              ? PartialFileHash(record.path, record.size, window)
                              : FullFileHash(record.path);
    std::string hex = digest.ToHex();
    if (cache) {
        cache->Put(record, kind, hex);
    }
    return hex;
}

// Groups `items` by a key computed per-item (in parallel, bounded by
// num_workers), dropping singleton groups, and returns (key, group) pairs
// in a deterministic order (sorted by key) — callers that need the key
// afterward (e.g. the full-hash stage, which wants to record the hash it
// just grouped by) get it for free instead of recomputing it.
std::vector<std::pair<std::string, std::vector<FileRecord>>> GroupByComputedKey(
    const std::vector<FileRecord>& items, std::size_t num_workers,
    const std::function<std::string(const FileRecord&)>& key_fn) {
    std::vector<std::string> keys(items.size());
    ParallelFor(items.size(), num_workers,
                [&](std::size_t i) { keys[i] = key_fn(items[i]); });

    std::map<std::string, std::vector<FileRecord>> buckets;
    for (std::size_t i = 0; i < items.size(); i++) {
        buckets[keys[i]].push_back(items[i]);
    }

    std::vector<std::pair<std::string, std::vector<FileRecord>>> result;
    for (auto& [key, group] : buckets) {
        if (group.size() >= 2) {
            result.emplace_back(key, std::move(group));
        }
    }
    return result;
}

bool FilesByteEqual(const std::string& path_a, const std::string& path_b) {
    std::ifstream a(path_a, std::ios::binary);
    std::ifstream b(path_b, std::ios::binary);
    if (!a || !b) {
        return false;
    }
    constexpr std::size_t kChunk = 64 * 1024;
    std::vector<char> buf_a(kChunk), buf_b(kChunk);
    while (a && b) {
        a.read(buf_a.data(), static_cast<std::streamsize>(kChunk));
        b.read(buf_b.data(), static_cast<std::streamsize>(kChunk));
        std::streamsize got_a = a.gcount();
        std::streamsize got_b = b.gcount();
        if (got_a != got_b) {
            return false;
        }
        if (got_a > 0 && std::memcmp(buf_a.data(), buf_b.data(), static_cast<std::size_t>(got_a)) != 0) {
            return false;
        }
        if (got_a == 0) {
            break;
        }
    }
    return a.eof() == b.eof();
}

// Splits `members` (already confirmed equal by full SHA-256) into
// sub-groups of files that are also byte-for-byte identical, comparing
// each member against the first member of whichever bucket it matches.
std::vector<std::vector<FileRecord>> VerifyByteCompare(const std::vector<FileRecord>& members) {
    std::vector<std::vector<FileRecord>> buckets;
    for (const auto& member : members) {
        bool placed = false;
        for (auto& bucket : buckets) {
            if (FilesByteEqual(bucket.front().path, member.path)) {
                bucket.push_back(member);
                placed = true;
                break;
            }
        }
        if (!placed) {
            buckets.push_back({member});
        }
    }
    std::vector<std::vector<FileRecord>> result;
    for (auto& bucket : buckets) {
        if (bucket.size() >= 2) {
            result.push_back(std::move(bucket));
        }
    }
    return result;
}

}  // namespace

std::vector<DuplicateGroup> FindDuplicates(const std::vector<FileRecord>& files,
                                            const AnalyzeOptions& options) {
    std::vector<DuplicateGroup> result;

    // Stage 1: group by size.
    std::map<std::uint64_t, std::vector<FileRecord>> by_size;
    for (const auto& f : files) {
        by_size[f.size].push_back(f);
    }

    for (auto& [size, size_group] : by_size) {
        if (size_group.size() < 2) {
            continue;
        }

        // Stage 2: partial hash within this size group (skippable for
        // benchmarking — see AnalyzeOptions::skip_partial_hash). The
        // partial-hash-confirmed buckets are flattened back into one list
        // of candidates for stage 3 rather than processed one bucket at
        // a time — see the note on kMinItemsToParallelize in
        // worker_pool.cpp: running stage 3 once per (often small)
        // partial-hash bucket serialized the dominant full-hash work
        // across buckets, which a benchmark run caught directly (it made
        // partial hashing measurably *slower* than skipping it, despite
        // reading less total data). One flat stage-3 call over every
        // surviving candidate keeps that work properly parallelized.
        std::vector<FileRecord> full_hash_candidates;
        if (options.skip_partial_hash) {
            full_hash_candidates = size_group;
        } else {
            auto partial_groups = GroupByComputedKey(
                size_group, options.num_workers, [&](const FileRecord& record) {
                    return ComputeOrCacheHash(record, HashKind::Partial, options.cache,
                                               options.partial_hash_window);
                });
            for (auto& [partial_hash, partial_group] : partial_groups) {
                (void)partial_hash;
                for (auto& member : partial_group) {
                    full_hash_candidates.push_back(std::move(member));
                }
            }
        }

        if (full_hash_candidates.size() < 2) {
            continue;
        }

        // Stage 3: full hash across every surviving candidate for this
        // size, in one parallelized pass.
        auto full_groups = GroupByComputedKey(
            full_hash_candidates, options.num_workers, [&](const FileRecord& record) {
                return ComputeOrCacheHash(record, HashKind::Full, options.cache, 0);
            });

        for (auto& [full_hash, full_group] : full_groups) {
            if (options.verify_byte_compare) {
                for (auto& verified_group : VerifyByteCompare(full_group)) {
                    DuplicateGroup dup;
                    dup.size = size;
                    dup.full_hash_hex = full_hash;
                    dup.members = std::move(verified_group);
                    result.push_back(std::move(dup));
                }
            } else {
                DuplicateGroup dup;
                dup.size = size;
                dup.full_hash_hex = full_hash;
                dup.members = std::move(full_group);
                result.push_back(std::move(dup));
            }
        }
    }

    return result;
}

}  // namespace dfa
