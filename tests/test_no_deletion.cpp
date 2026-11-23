// Regression test enforcing a hard product invariant: this tool reports
// duplicates, it never deletes, moves, or renames anything. Rather than
// trusting that invariant to code review alone, this test greps the
// actual production source (src/, include/ — NOT tests/, which legitimately
// cleans up its own temp directories) for the syscalls/stdlib calls that
// would delete or modify a file, and fails if any are found.
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include <gtest/gtest.h>

namespace {

std::vector<std::string> ReadAllSourceFiles() {
    std::vector<std::string> contents;
    for (const char* dir : {DFA_SOURCE_ROOT "/src", DFA_SOURCE_ROOT "/include"}) {
        if (!std::filesystem::exists(dir)) continue;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension();
            if (ext != ".cpp" && ext != ".hpp" && ext != ".h" && ext != ".cc") continue;
            std::ifstream in(entry.path());
            std::ostringstream buf;
            buf << in.rdbuf();
            contents.push_back(buf.str());
        }
    }
    return contents;
}

const char* kForbiddenTokens[] = {
    "unlink(",
    "remove(",
    "remove_all(",
    "::remove(",
    "fs::remove",
    "std::filesystem::remove",
    "rename(",
    "truncate(",
    "O_TRUNC",
    "ftruncate",
};

}  // namespace

TEST(NoDeletionCodePath, ProductionSourceContainsNoDeletionOrMutationCalls) {
    auto files = ReadAllSourceFiles();
    ASSERT_FALSE(files.empty()) << "expected to find source files under src/ and include/";

    for (const auto& content : files) {
        for (const char* token : kForbiddenTokens) {
            EXPECT_EQ(content.find(token), std::string::npos)
                << "found forbidden token '" << token
                << "' in production source — this tool must never delete/move/truncate files";
        }
    }
}
