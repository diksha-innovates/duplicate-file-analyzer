#include "dfa/glob.hpp"

#include <gtest/gtest.h>

using namespace dfa;

TEST(Glob, ExactMatch) {
    EXPECT_TRUE(GlobMatch("foo.txt", "foo.txt"));
    EXPECT_FALSE(GlobMatch("foo.txt", "bar.txt"));
}

TEST(Glob, StarWildcard) {
    EXPECT_TRUE(GlobMatch("*.tmp", "a.tmp"));
    EXPECT_TRUE(GlobMatch("*.tmp", "anything.tmp"));
    EXPECT_FALSE(GlobMatch("*.tmp", "a.txt"));
}

TEST(Glob, QuestionMarkWildcard) {
    EXPECT_TRUE(GlobMatch("a?c", "abc"));
    EXPECT_FALSE(GlobMatch("a?c", "ac"));
}

TEST(Glob, PathPatterns) {
    EXPECT_TRUE(GlobMatch("*/node_modules/*", "/x/node_modules/y"));
}
