#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "dfa/cache.hpp"
#include "dfa/grouping.hpp"
#include "dfa/report.hpp"
#include "dfa/scanner.hpp"

namespace {

constexpr int kExitOk = 0;
constexpr int kExitUsageOrIoError = 2;

void PrintUsage() {
    std::cerr <<
        "Usage: duplicate-file-analyzer ROOT [options]\n"
        "\n"
        "Options:\n"
        "  --exclude GLOB      Exclude files whose path or name matches GLOB "
        "(repeatable)\n"
        "  --jobs N            Worker threads for hashing (default 4)\n"
        "  --cache PATH        SQLite cache file (speeds up repeated scans)\n"
        "  --verify            Byte-compare files within each confirmed group\n"
        "  --no-partial-hash   Skip the partial-hash pre-filter (benchmarking only)\n"
        "  --format FORMAT     text (default) | csv | json\n"
        "  --output PATH       Write the report to PATH instead of stdout\n"
        "\n"
        "This tool never deletes, moves, or modifies any file. It only reports.\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        PrintUsage();
        return kExitUsageOrIoError;
    }

    std::string root_arg = argv[1];
    std::vector<std::string> exclude_globs;
    std::size_t num_workers = 4;
    std::string cache_path;
    bool verify = false;
    bool no_partial_hash = false;
    std::string format = "text";
    std::string output_path;

    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        auto next_value = [&]() -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error("missing value for " + arg);
            }
            return argv[++i];
        };
        try {
            if (arg == "--exclude") {
                exclude_globs.push_back(next_value());
            } else if (arg == "--jobs") {
                num_workers = static_cast<std::size_t>(std::stoul(next_value()));
            } else if (arg == "--cache") {
                cache_path = next_value();
            } else if (arg == "--verify") {
                verify = true;
            } else if (arg == "--no-partial-hash") {
                no_partial_hash = true;
            } else if (arg == "--format") {
                format = next_value();
            } else if (arg == "--output") {
                output_path = next_value();
            } else {
                std::cerr << "unknown option: " << arg << "\n";
                PrintUsage();
                return kExitUsageOrIoError;
            }
        } catch (const std::exception& ex) {
            std::cerr << "argument error: " << ex.what() << "\n";
            PrintUsage();
            return kExitUsageOrIoError;
        }
    }

    if (format != "text" && format != "csv" && format != "json") {
        std::cerr << "--format must be text, csv, or json\n";
        return kExitUsageOrIoError;
    }

    std::filesystem::path root(root_arg);
    std::error_code ec;
    if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) {
        std::cerr << "not a directory: " << root_arg << "\n";
        return kExitUsageOrIoError;
    }

    dfa::ScanOptions scan_options;
    scan_options.exclude_globs = exclude_globs;
    dfa::ScanResult scan_result = dfa::ScanDirectory(root, scan_options);
    for (const auto& err : scan_result.errors) {
        std::cerr << "warning: " << err << "\n";
    }

    std::unique_ptr<dfa::HashCache> cache;
    if (!cache_path.empty()) {
        try {
            cache = std::make_unique<dfa::HashCache>(cache_path);
        } catch (const std::exception& ex) {
            std::cerr << "cache error: " << ex.what() << "\n";
            return kExitUsageOrIoError;
        }
    }

    dfa::AnalyzeOptions analyze_options;
    analyze_options.num_workers = num_workers;
    analyze_options.verify_byte_compare = verify;
    analyze_options.cache = cache.get();
    analyze_options.skip_partial_hash = no_partial_hash;

    std::vector<dfa::DuplicateGroup> groups;
    try {
        groups = dfa::FindDuplicates(scan_result.files, analyze_options);
    } catch (const std::exception& ex) {
        std::cerr << "error during analysis: " << ex.what() << "\n";
        return kExitUsageOrIoError;
    }

    std::string report = format == "csv" ? dfa::RenderCsv(groups)
                          : format == "json" ? dfa::RenderJson(groups)
                                              : dfa::RenderText(groups);

    if (output_path.empty()) {
        std::cout << report;
    } else {
        std::ofstream out(output_path);
        if (!out) {
            std::cerr << "cannot write output file: " << output_path << "\n";
            return kExitUsageOrIoError;
        }
        out << report;
    }

    return kExitOk;
}
