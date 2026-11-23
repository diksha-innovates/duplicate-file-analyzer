#include "dfa/scanner.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <gtest/gtest.h>

#include "test_support.hpp"

using namespace dfa;

namespace {
bool ContainsPath(const std::vector<FileRecord>& files, const std::filesystem::path& p) {
    return std::any_of(files.begin(), files.end(),
                        [&](const FileRecord& f) { return f.path == p.string(); });
}
}  // namespace

TEST(Scanner, FindsRegularFilesRecursively) {
    dfa_test::TempDir dir;
    auto a = dir.WriteFile("a.txt", "A");
    auto b = dir.WriteFile("sub/b.txt", "B");

    ScanResult result = ScanDirectory(dir.path(), {});
    EXPECT_EQ(result.files.size(), 2u);
    EXPECT_TRUE(ContainsPath(result.files, a));
    EXPECT_TRUE(ContainsPath(result.files, b));
}

TEST(Scanner, RecordsSizeAndMetadata) {
    dfa_test::TempDir dir;
    auto a = dir.WriteFile("a.txt", "hello");

    ScanResult result = ScanDirectory(dir.path(), {});
    ASSERT_EQ(result.files.size(), 1u);
    EXPECT_EQ(result.files[0].size, 5u);
    EXPECT_GT(result.files[0].inode, 0u);
}

TEST(Scanner, ExcludeGlobByFilename) {
    dfa_test::TempDir dir;
    dir.WriteFile("keep.txt", "A");
    dir.WriteFile("skip.tmp", "B");

    ScanOptions options;
    options.exclude_globs = {"*.tmp"};
    ScanResult result = ScanDirectory(dir.path(), options);

    ASSERT_EQ(result.files.size(), 1u);
    EXPECT_EQ(std::filesystem::path(result.files[0].path).filename(), "keep.txt");
}

TEST(Scanner, ExcludeGlobByDirectoryPattern) {
    dfa_test::TempDir dir;
    dir.WriteFile("keep/a.txt", "A");
    dir.WriteFile("node_modules/pkg/b.txt", "B");

    ScanOptions options;
    options.exclude_globs = {"*/node_modules/*"};
    ScanResult result = ScanDirectory(dir.path(), options);

    ASSERT_EQ(result.files.size(), 1u);
    EXPECT_EQ(std::filesystem::path(result.files[0].path).filename(), "a.txt");
}

TEST(Scanner, SymlinkToFileIsNotFollowedByDefault) {
    dfa_test::TempDir dir;
    auto target = dir.WriteFile("real.txt", "content");
    auto link = dir.path() / "link.txt";
    ASSERT_EQ(::symlink(target.c_str(), link.c_str()), 0);

    ScanResult result = ScanDirectory(dir.path(), {});

    EXPECT_TRUE(ContainsPath(result.files, target));
    EXPECT_FALSE(ContainsPath(result.files, link));
}

TEST(Scanner, SymlinkToDirectoryIsNotDescendedByDefault) {
    dfa_test::TempDir dir;
    dir.WriteFile("realdir/inside.txt", "content");
    auto link = dir.path() / "linkdir";
    ASSERT_EQ(::symlink((dir.path() / "realdir").c_str(), link.c_str()), 0);

    ScanResult result = ScanDirectory(dir.path(), {});

    ASSERT_EQ(result.files.size(), 1u);
    EXPECT_EQ(std::filesystem::path(result.files[0].path).filename(), "inside.txt");
}

TEST(Scanner, HardlinksShareDevAndInode) {
    dfa_test::TempDir dir;
    auto original = dir.WriteFile("orig.txt", "shared content");
    auto hardlink = dir.path() / "hardlink.txt";
    ASSERT_EQ(::link(original.c_str(), hardlink.c_str()), 0);

    ScanResult result = ScanDirectory(dir.path(), {});
    ASSERT_EQ(result.files.size(), 2u);
    EXPECT_TRUE(result.files[0].IsHardlinkOf(result.files[1]));
}

TEST(Scanner, PermissionDeniedSubdirectoryDoesNotAbortScan) {
    if (::geteuid() == 0) {
        GTEST_SKIP() << "running as root: permission checks don't apply";
    }
    dfa_test::TempDir dir;
    dir.WriteFile("visible.txt", "A");
    auto locked_dir = dir.path() / "locked";
    std::filesystem::create_directory(locked_dir);
    dir.WriteFile("locked/hidden.txt", "B");
    ASSERT_EQ(::chmod(locked_dir.c_str(), 0), 0);

    ScanResult result = ScanDirectory(dir.path(), {});

    // The scan must complete and still find the file outside the
    // unreadable directory, rather than throwing/aborting entirely.
    EXPECT_TRUE(ContainsPath(result.files, dir.path() / "visible.txt"));

    ::chmod(locked_dir.c_str(), 0700);  // restore so TempDir's cleanup can remove it
}
