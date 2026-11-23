#include "dfa/report.hpp"

#include <cstdio>
#include <sstream>

namespace dfa {

std::uint64_t TotalReclaimableBytes(const std::vector<DuplicateGroup>& groups) {
    std::uint64_t total = 0;
    for (const auto& g : groups) {
        total += g.ReclaimableBytes();
    }
    return total;
}

std::string RenderText(const std::vector<DuplicateGroup>& groups) {
    std::ostringstream oss;
    if (groups.empty()) {
        oss << "No duplicate files found.\n";
        return oss.str();
    }
    std::size_t group_num = 1;
    for (const auto& g : groups) {
        oss << "Group " << group_num++ << ": " << g.members.size() << " files, " << g.size
            << " bytes each, sha256=" << g.full_hash_hex << ", reclaimable="
            << g.ReclaimableBytes() << " bytes\n";
        for (const auto& m : g.members) {
            oss << "  " << m.path << "\n";
        }
    }
    oss << "\nTotal reclaimable: " << TotalReclaimableBytes(groups) << " bytes across "
        << groups.size() << " group(s)\n";
    return oss.str();
}

namespace {

std::string CsvField(const std::string& field) {
    bool needs_quoting =
        field.find(',') != std::string::npos || field.find('"') != std::string::npos ||
        field.find('\n') != std::string::npos;
    if (!needs_quoting) {
        return field;
    }
    std::string out = "\"";
    for (char c : field) {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    out += "\"";
    return out;
}

std::string JsonString(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    out += "\"";
    return out;
}

}  // namespace

std::string RenderCsv(const std::vector<DuplicateGroup>& groups) {
    std::ostringstream oss;
    oss << "group,size,reclaimable_bytes,sha256,path\n";
    std::size_t group_num = 1;
    for (const auto& g : groups) {
        for (const auto& m : g.members) {
            oss << group_num << "," << g.size << "," << g.ReclaimableBytes() << ","
                << g.full_hash_hex << "," << CsvField(m.path) << "\n";
        }
        group_num++;
    }
    return oss.str();
}

std::string RenderJson(const std::vector<DuplicateGroup>& groups) {
    std::ostringstream oss;
    oss << "{\n  \"total_reclaimable_bytes\": " << TotalReclaimableBytes(groups) << ",\n";
    oss << "  \"groups\": [\n";
    for (std::size_t i = 0; i < groups.size(); i++) {
        const auto& g = groups[i];
        oss << "    {\n";
        oss << "      \"size\": " << g.size << ",\n";
        oss << "      \"sha256\": " << JsonString(g.full_hash_hex) << ",\n";
        oss << "      \"reclaimable_bytes\": " << g.ReclaimableBytes() << ",\n";
        oss << "      \"paths\": [\n";
        for (std::size_t j = 0; j < g.members.size(); j++) {
            oss << "        " << JsonString(g.members[j].path)
                << (j + 1 < g.members.size() ? ",\n" : "\n");
        }
        oss << "      ]\n";
        oss << "    }" << (i + 1 < groups.size() ? ",\n" : "\n");
    }
    oss << "  ]\n}\n";
    return oss.str();
}

}  // namespace dfa
