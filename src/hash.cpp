#include "dfa/hash.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

#if defined(DFA_USE_COMMONCRYPTO)
#include <CommonCrypto/CommonDigest.h>
#elif defined(DFA_USE_OPENSSL)
#include <openssl/evp.h>
#else
#error "dfa/hash.cpp requires either DFA_USE_COMMONCRYPTO or DFA_USE_OPENSSL"
#endif

namespace dfa {

std::string Sha256Digest::ToHex() const {
    static const char kHexDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (std::uint8_t b : bytes) {
        out += kHexDigits[b >> 4];
        out += kHexDigits[b & 0x0F];
    }
    return out;
}

#if defined(DFA_USE_COMMONCRYPTO)

struct Sha256Hasher::Impl {
    CC_SHA256_CTX ctx;
    Impl() { CC_SHA256_Init(&ctx); }
};

Sha256Hasher::Sha256Hasher() : impl_(std::make_unique<Impl>()) {}
Sha256Hasher::~Sha256Hasher() = default;
Sha256Hasher::Sha256Hasher(Sha256Hasher&&) noexcept = default;
Sha256Hasher& Sha256Hasher::operator=(Sha256Hasher&&) noexcept = default;

void Sha256Hasher::Update(const void* data, std::size_t len) {
    CC_SHA256_Update(&impl_->ctx, data, static_cast<CC_LONG>(len));
}

Sha256Digest Sha256Hasher::Finalize() {
    Sha256Digest digest;
    CC_SHA256_Final(digest.bytes.data(), &impl_->ctx);
    return digest;
}

#elif defined(DFA_USE_OPENSSL)

struct Sha256Hasher::Impl {
    EVP_MD_CTX* ctx;
    Impl() : ctx(EVP_MD_CTX_new()) {
        if (!ctx || EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
            throw std::runtime_error("failed to initialize OpenSSL SHA-256 context");
        }
    }
    ~Impl() {
        if (ctx) EVP_MD_CTX_free(ctx);
    }
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
};

Sha256Hasher::Sha256Hasher() : impl_(std::make_unique<Impl>()) {}
Sha256Hasher::~Sha256Hasher() = default;
Sha256Hasher::Sha256Hasher(Sha256Hasher&&) noexcept = default;
Sha256Hasher& Sha256Hasher::operator=(Sha256Hasher&&) noexcept = default;

void Sha256Hasher::Update(const void* data, std::size_t len) {
    if (EVP_DigestUpdate(impl_->ctx, data, len) != 1) {
        throw std::runtime_error("OpenSSL SHA-256 update failed");
    }
}

Sha256Digest Sha256Hasher::Finalize() {
    Sha256Digest digest;
    unsigned int out_len = 0;
    if (EVP_DigestFinal_ex(impl_->ctx, digest.bytes.data(), &out_len) != 1 ||
        out_len != kSha256DigestSize) {
        throw std::runtime_error("OpenSSL SHA-256 finalize failed");
    }
    return digest;
}

#endif

namespace {

constexpr std::size_t kStreamChunkSize = 64 * 1024;

std::ifstream OpenBinaryOrThrow(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open file for hashing: " + path);
    }
    return in;
}

}  // namespace

Sha256Digest FullFileHash(const std::string& path) {
    std::ifstream in = OpenBinaryOrThrow(path);
    Sha256Hasher hasher;
    std::vector<char> buffer(kStreamChunkSize);
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        std::streamsize got = in.gcount();
        if (got > 0) {
            hasher.Update(buffer.data(), static_cast<std::size_t>(got));
        }
    }
    if (in.bad()) {
        throw std::runtime_error("I/O error while hashing file: " + path);
    }
    return hasher.Finalize();
}

Sha256Digest PartialFileHash(const std::string& path, std::uint64_t file_size,
                              std::size_t window) {
    std::ifstream in = OpenBinaryOrThrow(path);
    Sha256Hasher hasher;

    if (file_size <= static_cast<std::uint64_t>(window) * 2) {
        // Small file: partial hash degrades to a full hash, streamed the
        // same bounded-chunk way as FullFileHash.
        std::vector<char> buffer(kStreamChunkSize);
        while (in) {
            in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            std::streamsize got = in.gcount();
            if (got > 0) {
                hasher.Update(buffer.data(), static_cast<std::size_t>(got));
            }
        }
        if (in.bad()) {
            throw std::runtime_error("I/O error while hashing file: " + path);
        }
        return hasher.Finalize();
    }

    std::vector<char> buffer(std::min(window, kStreamChunkSize));

    // First `window` bytes, read in bounded chunks.
    std::size_t remaining = window;
    while (remaining > 0) {
        std::streamsize to_read =
            static_cast<std::streamsize>(std::min(remaining, buffer.size()));
        in.read(buffer.data(), to_read);
        std::streamsize got = in.gcount();
        if (got <= 0) break;
        hasher.Update(buffer.data(), static_cast<std::size_t>(got));
        remaining -= static_cast<std::size_t>(got);
    }

    // Last `window` bytes.
    in.seekg(static_cast<std::streamoff>(file_size - window), std::ios::beg);
    if (!in) {
        throw std::runtime_error("seek failed while hashing file: " + path);
    }
    remaining = window;
    while (remaining > 0) {
        std::streamsize to_read =
            static_cast<std::streamsize>(std::min(remaining, buffer.size()));
        in.read(buffer.data(), to_read);
        std::streamsize got = in.gcount();
        if (got <= 0) break;
        hasher.Update(buffer.data(), static_cast<std::size_t>(got));
        remaining -= static_cast<std::size_t>(got);
    }

    if (in.bad()) {
        throw std::runtime_error("I/O error while hashing file: " + path);
    }
    return hasher.Finalize();
}

}  // namespace dfa
