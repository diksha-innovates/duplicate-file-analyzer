#ifndef DFA_TEST_SUPPORT_HPP
#define DFA_TEST_SUPPORT_HPP

#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>

namespace dfa_test {

// RAII temp directory for tests. Test-only code: the production library
// (src/, include/) never deletes anything (that invariant is enforced by
// test_no_deletion.cpp scanning the source tree), but tests cleaning up
// their own scratch directories afterward is normal and expected.
class TempDir {
public:
    TempDir() {
        auto base = std::filesystem::temp_directory_path();
        for (int attempt = 0; attempt < 100; attempt++) {
            auto candidate = base / ("dfa_test_" + std::to_string(std::random_device{}()));
            std::error_code ec;
            if (std::filesystem::create_directory(candidate, ec)) {
                path_ = candidate;
                return;
            }
        }
        throw std::runtime_error("could not create a temp directory for testing");
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const { return path_; }

    std::filesystem::path WriteFile(const std::string& relative_name, const std::string& content) {
        auto full = path_ / relative_name;
        std::filesystem::create_directories(full.parent_path());
        std::ofstream out(full, std::ios::binary);
        out << content;
        out.close();
        return full;
    }

private:
    std::filesystem::path path_;
};

}  // namespace dfa_test

#endif
