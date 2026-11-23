#ifndef DFA_SCANNER_HPP
#define DFA_SCANNER_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace dfa {

struct FileRecord {
    std::string path;    // absolute (or root-relative-resolved) path
    std::uint64_t size = 0;
    std::int64_t mtime = 0;   // seconds since epoch (st_mtime)
    std::uint64_t inode = 0;  // st_ino
    std::uint64_t dev = 0;    // st_dev

    // Two records are hardlinks of the same underlying data iff both
    // dev and inode match — freeing one would not reclaim any space.
    bool IsHardlinkOf(const FileRecord& other) const {
        return dev == other.dev && inode == other.inode;
    }
};

struct ScanOptions {
    // Shell-style glob patterns (see glob.hpp). A path is excluded if
    // either its full path or its bare filename matches any pattern.
    std::vector<std::string> exclude_globs;
};

struct ScanResult {
    std::vector<FileRecord> files;
    // Human-readable messages for entries that couldn't be scanned
    // (permission denied, broken symlink target, race with a deleted
    // file, etc.) — collected rather than aborting the whole scan.
    std::vector<std::string> errors;
};

// Recursively scans `root`. Symlinks (to files or directories) are never
// followed — they are skipped entirely and do not appear in the result,
// by design (see README's "Design decisions" for why this is the safer
// default). Regular files matching any `exclude_globs` pattern are
// skipped. Permission errors on individual entries are recorded in
// `errors` and do not abort the scan.
ScanResult ScanDirectory(const std::filesystem::path& root, const ScanOptions& options);

}  // namespace dfa

#endif
