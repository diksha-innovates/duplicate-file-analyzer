#ifndef DFA_CACHE_HPP
#define DFA_CACHE_HPP

#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "dfa/scanner.hpp"

struct sqlite3;

namespace dfa {

enum class HashKind { Partial, Full };

// A SQLite-backed cache keyed by path, invalidated by (size, mtime, inode,
// dev): a cache hit requires the file's current metadata to exactly match
// what was recorded when the hash was computed. This means an edited file
// (different mtime/size) is a guaranteed cache miss — we never have to
// explicitly invalidate anything, staleness is detected structurally.
//
// Thread safety: every public method takes an internal mutex, so one
// HashCache instance can safely be shared across the worker pool's
// threads. This serializes cache access entirely (simple and correct);
// see README's Design decisions for why that's an acceptable trade-off
// here (hashing, not cache I/O, dominates wall-clock time).
class HashCache {
public:
    explicit HashCache(const std::string& db_path);
    ~HashCache();
    HashCache(const HashCache&) = delete;
    HashCache& operator=(const HashCache&) = delete;

    // Returns the cached hash (hex string) iff a row exists for this path
    // with metadata exactly matching `record`, and that hash kind was
    // previously recorded. Otherwise std::nullopt (a cache miss).
    std::optional<std::string> Get(const FileRecord& record, HashKind kind) const;

    // Records `hash_hex` for `record`'s current metadata. If the existing
    // row's metadata differs from `record` (the file changed since it was
    // last cached), the *other* hash kind is implicitly invalidated too —
    // it was computed for different file content and must not be served
    // for this file's new content.
    void Put(const FileRecord& record, HashKind kind, const std::string& hash_hex);

private:
    mutable std::mutex mutex_;
    sqlite3* db_ = nullptr;
};

}  // namespace dfa

#endif
