#include "dfa/scanner.hpp"

#include <sys/stat.h>

#include <cerrno>
#include <cstring>
#include <system_error>

#include "dfa/glob.hpp"

namespace dfa {

namespace fs = std::filesystem;

namespace {

bool IsExcluded(const fs::path& path, const ScanOptions& options) {
    const std::string full = path.string();
    const std::string name = path.filename().string();
    for (const auto& pattern : options.exclude_globs) {
        if (GlobMatch(pattern, full) || GlobMatch(pattern, name)) {
            return true;
        }
    }
    return false;
}

}  // namespace

ScanResult ScanDirectory(const fs::path& root, const ScanOptions& options) {
    ScanResult result;

    std::error_code ec;
    fs::recursive_directory_iterator it(
        root, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        result.errors.push_back("cannot open directory " + root.string() + ": " + ec.message());
        return result;
    }
    fs::recursive_directory_iterator end;

    while (it != end) {
        const fs::directory_entry& entry = *it;

        std::error_code symlink_ec;
        bool is_symlink = entry.is_symlink(symlink_ec);
        if (symlink_ec) {
            result.errors.push_back(entry.path().string() + ": " + symlink_ec.message());
            it.increment(ec);
            continue;
        }
        if (is_symlink) {
            // Policy: never follow symlinks, to files or directories. For
            // a directory symlink this also means recursive_directory_
            // iterator won't descend into it (we never set
            // follow_directory_symlink), so this `continue` is really
            // just "don't record it as a file" for the file-symlink case.
            it.increment(ec);
            if (ec) {
                result.errors.push_back(entry.path().string() + ": " + ec.message());
                ec.clear();
            }
            continue;
        }

        std::error_code is_reg_ec;
        bool is_regular = entry.is_regular_file(is_reg_ec);
        if (is_reg_ec) {
            result.errors.push_back(entry.path().string() + ": " + is_reg_ec.message());
            it.increment(ec);
            continue;
        }

        if (is_regular && !IsExcluded(entry.path(), options)) {
            struct stat st{};
            if (::stat(entry.path().c_str(), &st) != 0) {
                result.errors.push_back(entry.path().string() + ": " + std::string(strerror(errno)));
            } else {
                FileRecord record;
                record.path = entry.path().string();
                record.size = static_cast<std::uint64_t>(st.st_size);
                record.mtime = static_cast<std::int64_t>(st.st_mtime);
                record.inode = static_cast<std::uint64_t>(st.st_ino);
                record.dev = static_cast<std::uint64_t>(st.st_dev);
                result.files.push_back(std::move(record));
            }
        }

        it.increment(ec);
        if (ec) {
            result.errors.push_back(entry.path().string() + ": " + ec.message());
            ec.clear();
        }
    }

    return result;
}

}  // namespace dfa
