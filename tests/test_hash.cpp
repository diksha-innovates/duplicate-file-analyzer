#include "dfa/hash.hpp"

#include <gtest/gtest.h>

#include "test_support.hpp"

using namespace dfa;

TEST(Hash, KnownVectorEmptyString) {
    Sha256Hasher hasher;
    Sha256Digest digest = hasher.Finalize();
    EXPECT_EQ(digest.ToHex(),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(Hash, KnownVectorHello) {
    Sha256Hasher hasher;
    hasher.Update("hello", 5);
    Sha256Digest digest = hasher.Finalize();
    EXPECT_EQ(digest.ToHex(),
              "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824");
}

TEST(Hash, UpdateInMultipleChunksMatchesSingleUpdate) {
    Sha256Hasher a;
    a.Update("hello world", 11);
    Sha256Digest digest_a = a.Finalize();

    Sha256Hasher b;
    b.Update("hello", 5);
    b.Update(" world", 6);
    Sha256Digest digest_b = b.Finalize();

    EXPECT_EQ(digest_a, digest_b);
}

TEST(Hash, FullFileHashMatchesKnownVector) {
    dfa_test::TempDir dir;
    auto path = dir.WriteFile("hello.txt", "hello");
    Sha256Digest digest = FullFileHash(path.string());
    EXPECT_EQ(digest.ToHex(),
              "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824");
}

TEST(Hash, FullFileHashThrowsOnMissingFile) {
    EXPECT_THROW(FullFileHash("/nonexistent/path/xyz"), std::runtime_error);
}

TEST(Hash, PartialHashOfSmallFileEqualsFullHash) {
    dfa_test::TempDir dir;
    auto path = dir.WriteFile("small.txt", "hello");
    Sha256Digest partial = PartialFileHash(path.string(), 5, /*window=*/1024);
    Sha256Digest full = FullFileHash(path.string());
    EXPECT_EQ(partial, full);
}

TEST(Hash, PartialHashDependsOnlyOnFirstAndLastWindow) {
    // Two files identical in their first+last `window` bytes but
    // different in the middle must produce the same partial hash (that's
    // the whole point of a partial hash as a cheap pre-filter) while
    // producing different full hashes.
    dfa_test::TempDir dir;
    std::string prefix(100, 'A');
    std::string suffix(100, 'B');
    auto path1 = dir.WriteFile("f1.bin", prefix + "middle-one" + suffix);
    auto path2 = dir.WriteFile("f2.bin", prefix + "middle-two-different" + suffix);

    std::uint64_t size1 = std::filesystem::file_size(path1);
    std::uint64_t size2 = std::filesystem::file_size(path2);

    Sha256Digest partial1 = PartialFileHash(path1.string(), size1, /*window=*/100);
    Sha256Digest partial2 = PartialFileHash(path2.string(), size2, /*window=*/100);
    EXPECT_EQ(partial1, partial2);

    Sha256Digest full1 = FullFileHash(path1.string());
    Sha256Digest full2 = FullFileHash(path2.string());
    EXPECT_NE(full1, full2);
}

TEST(Hash, LargeFileHashesCorrectlyWithBoundedReadChunks) {
    // Hashing streams in fixed 64 KiB chunks (see kStreamChunkSize in
    // hash.cpp) regardless of file size — this test exercises a file far
    // bigger than any single chunk to catch any accidental "read whole
    // file into one buffer" regression, and cross-checks against an
    // independent incremental hash computed the same way the production
    // code does it, so a correctness bug in chunking would show up as a
    // mismatch here. (Peak-RSS bounded-memory verification itself lives
    // in benchmarks/, not here — see README.)
    dfa_test::TempDir dir;
    constexpr std::size_t kSize = 20 * 1024 * 1024;  // 20 MiB, >> 64 KiB chunk size
    std::string content(kSize, 'x');
    for (std::size_t i = 0; i < kSize; i += 4096) {
        content[i] = static_cast<char>('a' + (i / 4096) % 26);  // avoid a trivially-uniform file
    }
    auto path = dir.WriteFile("large.bin", content);

    Sha256Digest streamed = FullFileHash(path.string());

    Sha256Hasher reference;
    reference.Update(content.data(), content.size());
    Sha256Digest expected = reference.Finalize();

    EXPECT_EQ(streamed, expected);
}

TEST(Hash, IdenticalFilesProduceIdenticalFullHash) {
    dfa_test::TempDir dir;
    auto path1 = dir.WriteFile("a.txt", "the exact same content");
    auto path2 = dir.WriteFile("b.txt", "the exact same content");
    EXPECT_EQ(FullFileHash(path1.string()), FullFileHash(path2.string()));
}
