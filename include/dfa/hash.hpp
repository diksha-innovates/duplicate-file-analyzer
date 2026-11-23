#ifndef DFA_HASH_HPP
#define DFA_HASH_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace dfa {

constexpr std::size_t kSha256DigestSize = 32;

struct Sha256Digest {
    std::array<std::uint8_t, kSha256DigestSize> bytes{};

    std::string ToHex() const;
    bool operator==(const Sha256Digest& other) const { return bytes == other.bytes; }
    bool operator!=(const Sha256Digest& other) const { return !(*this == other); }
};

// Streaming SHA-256 hasher. One interface, two backends selected at compile
// time (CommonCrypto on macOS, OpenSSL's EVP API on Linux) via the pimpl
// pointer below — callers never see which backend is active, and neither
// backend's headers leak into this public header. We never implement the
// hash algorithm ourselves.
class Sha256Hasher {
public:
    Sha256Hasher();
    ~Sha256Hasher();
    Sha256Hasher(const Sha256Hasher&) = delete;
    Sha256Hasher& operator=(const Sha256Hasher&) = delete;
    Sha256Hasher(Sha256Hasher&&) noexcept;
    Sha256Hasher& operator=(Sha256Hasher&&) noexcept;

    void Update(const void* data, std::size_t len);
    Sha256Digest Finalize();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Convenience: hashes the first `window` bytes and the last `window` bytes
// of the file at `path` (if the file is smaller than 2*window, the whole
// file is hashed once). This is the "partial hash" used as a cheap filter
// before committing to a full-file SHA-256. Reads happen in bounded-size
// chunks (streaming), never loading the whole file into memory. Throws
// std::runtime_error on I/O failure.
Sha256Digest PartialFileHash(const std::string& path, std::uint64_t file_size,
                              std::size_t window = 64 * 1024);

// Hashes the entire file at `path` by streaming it in bounded-size chunks.
// Throws std::runtime_error on I/O failure.
Sha256Digest FullFileHash(const std::string& path);

}  // namespace dfa

#endif
