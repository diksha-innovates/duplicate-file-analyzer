#include "dfa/cache.hpp"

#include <gtest/gtest.h>

#include "test_support.hpp"

using namespace dfa;

namespace {
FileRecord MakeRecord(const std::string& path, std::uint64_t size, std::int64_t mtime,
                       std::uint64_t inode, std::uint64_t dev) {
    FileRecord r;
    r.path = path;
    r.size = size;
    r.mtime = mtime;
    r.inode = inode;
    r.dev = dev;
    return r;
}
}  // namespace

TEST(Cache, MissOnEmptyCache) {
    dfa_test::TempDir dir;
    HashCache cache((dir.path() / "cache.sqlite").string());
    FileRecord r = MakeRecord("/a.txt", 100, 1000, 5, 1);
    EXPECT_FALSE(cache.Get(r, HashKind::Partial).has_value());
}

TEST(Cache, HitAfterPut) {
    dfa_test::TempDir dir;
    HashCache cache((dir.path() / "cache.sqlite").string());
    FileRecord r = MakeRecord("/a.txt", 100, 1000, 5, 1);
    cache.Put(r, HashKind::Full, "abc123");
    auto hit = cache.Get(r, HashKind::Full);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(*hit, "abc123");
}

TEST(Cache, MissIfMetadataChanged) {
    dfa_test::TempDir dir;
    HashCache cache((dir.path() / "cache.sqlite").string());
    FileRecord original = MakeRecord("/a.txt", 100, 1000, 5, 1);
    cache.Put(original, HashKind::Full, "abc123");

    FileRecord changed = MakeRecord("/a.txt", 200 /* different size */, 1000, 5, 1);
    EXPECT_FALSE(cache.Get(changed, HashKind::Full).has_value());
}

TEST(Cache, ChangedMetadataInvalidatesTheOtherHashKindToo) {
    // Regression test for a real bug class: writing a new partial_hash for
    // a path whose metadata changed must not let a stale full_hash from
    // the file's *previous* content keep being served.
    dfa_test::TempDir dir;
    HashCache cache((dir.path() / "cache.sqlite").string());

    FileRecord v1 = MakeRecord("/a.txt", 100, 1000, 5, 1);
    cache.Put(v1, HashKind::Full, "full-hash-of-v1");

    FileRecord v2 = MakeRecord("/a.txt", 200, 2000, 5, 1);  // same path, changed content
    cache.Put(v2, HashKind::Partial, "partial-hash-of-v2");

    // The full hash for v2's metadata must be a miss — it was never
    // computed for v2's content, and must not resolve to v1's stale value.
    EXPECT_FALSE(cache.Get(v2, HashKind::Full).has_value());
    auto partial = cache.Get(v2, HashKind::Partial);
    ASSERT_TRUE(partial.has_value());
    EXPECT_EQ(*partial, "partial-hash-of-v2");
}

TEST(Cache, BothHashKindsCoexistForSameUnchangedMetadata) {
    dfa_test::TempDir dir;
    HashCache cache((dir.path() / "cache.sqlite").string());
    FileRecord r = MakeRecord("/a.txt", 100, 1000, 5, 1);

    cache.Put(r, HashKind::Partial, "partial-xyz");
    cache.Put(r, HashKind::Full, "full-xyz");

    EXPECT_EQ(*cache.Get(r, HashKind::Partial), "partial-xyz");
    EXPECT_EQ(*cache.Get(r, HashKind::Full), "full-xyz");
}

TEST(Cache, PersistsAcrossReopenOfSameDbFile) {
    dfa_test::TempDir dir;
    auto db_path = (dir.path() / "cache.sqlite").string();
    FileRecord r = MakeRecord("/a.txt", 100, 1000, 5, 1);
    {
        HashCache cache(db_path);
        cache.Put(r, HashKind::Full, "persisted-hash");
    }
    {
        HashCache reopened(db_path);
        auto hit = reopened.Get(r, HashKind::Full);
        ASSERT_TRUE(hit.has_value());
        EXPECT_EQ(*hit, "persisted-hash");
    }
}

TEST(Cache, ThrowsOnUnwritableDbPath) {
    EXPECT_THROW(HashCache("/nonexistent_dir_xyz/cache.sqlite"), std::runtime_error);
}
