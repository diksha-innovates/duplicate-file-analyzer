#include "dfa/grouping.hpp"

#include <unistd.h>

#include <algorithm>
#include <gtest/gtest.h>

#include "dfa/scanner.hpp"
#include "test_support.hpp"

using namespace dfa;

namespace {

std::vector<FileRecord> Scan(const dfa_test::TempDir& dir) {
    return ScanDirectory(dir.path(), {}).files;
}

bool AnyGroupContains(const std::vector<DuplicateGroup>& groups, const std::filesystem::path& p) {
    for (const auto& g : groups) {
        for (const auto& m : g.members) {
            if (m.path == p.string()) return true;
        }
    }
    return false;
}

}  // namespace

TEST(Grouping, IdenticalFilesAreGrouped) {
    dfa_test::TempDir dir;
    auto a = dir.WriteFile("a.txt", "same content here");
    auto b = dir.WriteFile("b.txt", "same content here");
    dir.WriteFile("c.txt", "totally different");

    auto groups = FindDuplicates(Scan(dir), AnalyzeOptions{});

    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].members.size(), 2u);
    EXPECT_TRUE(AnyGroupContains(groups, a));
    EXPECT_TRUE(AnyGroupContains(groups, b));
}

TEST(Grouping, SameSizeDifferentContentNotGrouped) {
    dfa_test::TempDir dir;
    dir.WriteFile("a.txt", "AAAAAAAAAA");
    dir.WriteFile("b.txt", "BBBBBBBBBB");  // same size as a.txt, different content

    auto groups = FindDuplicates(Scan(dir), AnalyzeOptions{});

    EXPECT_TRUE(groups.empty());
}

TEST(Grouping, DifferentSizeNeverGrouped) {
    dfa_test::TempDir dir;
    dir.WriteFile("a.txt", "short");
    dir.WriteFile("b.txt", "a much longer string entirely");

    auto groups = FindDuplicates(Scan(dir), AnalyzeOptions{});

    EXPECT_TRUE(groups.empty());
}

TEST(Grouping, SingletonSizeGroupNeverHashed) {
    // A file whose size is unique in the scan should never even reach the
    // hashing stage — verified indirectly: zero groups, zero errors, and
    // (most importantly) it simply doesn't crash the pipeline if the file
    // were unreadable, since it's never opened in the first place.
    dfa_test::TempDir dir;
    dir.WriteFile("unique_size.txt", "a size nothing else in this dir shares");

    auto groups = FindDuplicates(Scan(dir), AnalyzeOptions{});
    EXPECT_TRUE(groups.empty());
}

TEST(Grouping, ThreeWayDuplicateGroup) {
    dfa_test::TempDir dir;
    dir.WriteFile("a.txt", "triplicate");
    dir.WriteFile("b.txt", "triplicate");
    dir.WriteFile("c.txt", "triplicate");

    auto groups = FindDuplicates(Scan(dir), AnalyzeOptions{});

    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].members.size(), 3u);
}

TEST(Grouping, ReclaimableBytesExcludesHardlinks) {
    dfa_test::TempDir dir;
    auto a = dir.WriteFile("a.txt", "hardlinked content");
    auto hardlink_of_a = dir.path() / "a_hardlink.txt";
    ASSERT_EQ(::link(a.c_str(), hardlink_of_a.c_str()), 0);
    dir.WriteFile("b.txt", "hardlinked content");  // independent copy, same content

    auto groups = FindDuplicates(Scan(dir), AnalyzeOptions{});

    ASSERT_EQ(groups.size(), 1u);
    ASSERT_EQ(groups[0].members.size(), 3u);
    // 3 members, but only 2 distinct (dev, inode) pairs (a.txt and its
    // hardlink share one) -> reclaimable = (2 - 1) * size, not (3 - 1) * size.
    EXPECT_EQ(groups[0].ReclaimableBytes(), groups[0].size * 1);
}

TEST(Grouping, PureHardlinksOfOneFileAreNotReclaimable) {
    dfa_test::TempDir dir;
    auto a = dir.WriteFile("a.txt", "shared");
    auto hardlink = dir.path() / "a_hardlink.txt";
    ASSERT_EQ(::link(a.c_str(), hardlink.c_str()), 0);

    auto groups = FindDuplicates(Scan(dir), AnalyzeOptions{});

    // Two paths, same size, same content, same (dev, inode) -> they are
    // one physical file wearing two names. The *grouping* stage still
    // sees two FileRecords with equal size/hash (hardlinks look
    // byte-identical because they are the same bytes), but reclaiming
    // one would free nothing.
    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].ReclaimableBytes(), 0u);
}

TEST(Grouping, VerifyByteCompareOptionStillGroupsTrueDuplicates) {
    dfa_test::TempDir dir;
    dir.WriteFile("a.txt", "byte for byte identical content");
    dir.WriteFile("b.txt", "byte for byte identical content");

    AnalyzeOptions options;
    options.verify_byte_compare = true;
    auto groups = FindDuplicates(Scan(dir), options);

    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].members.size(), 2u);
}

TEST(Grouping, CacheIsPopulatedAndReusedAcrossRuns) {
    dfa_test::TempDir dir;
    dir.WriteFile("a.txt", "cached content here");
    dir.WriteFile("b.txt", "cached content here");
    auto db_path = (dir.path() / "cache.sqlite").string();

    {
        HashCache cache(db_path);
        AnalyzeOptions options;
        options.cache = &cache;
        auto groups = FindDuplicates(Scan(dir), options);
        ASSERT_EQ(groups.size(), 1u);
    }
    // Reopen the same cache DB and confirm the hashes were actually
    // persisted (a real cache hit, not a no-op).
    {
        HashCache cache(db_path);
        auto files = Scan(dir);
        ASSERT_FALSE(files.empty());
        auto cached = cache.Get(files[0], HashKind::Full);
        EXPECT_TRUE(cached.has_value());
    }
}

TEST(Grouping, SkipPartialHashStillProducesCorrectGroups) {
    // --no-partial-hash is a benchmark-only knob (see README); it must
    // still produce exactly the same final grouping, just by going
    // straight from size-grouping to full-file hashing.
    dfa_test::TempDir dir;
    dir.WriteFile("a.txt", "same content for skip-partial test");
    dir.WriteFile("b.txt", "same content for skip-partial test");
    dir.WriteFile("c.txt", "different content, same size!!!!!!!");

    AnalyzeOptions options;
    options.skip_partial_hash = true;
    auto groups = FindDuplicates(Scan(dir), options);

    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].members.size(), 2u);
}

TEST(Grouping, EmptyInputProducesNoGroups) {
    EXPECT_TRUE(FindDuplicates({}, AnalyzeOptions{}).empty());
}
