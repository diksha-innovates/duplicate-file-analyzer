#include "dfa/glob.hpp"

#include <fnmatch.h>

namespace dfa {

bool GlobMatch(const std::string& pattern, const std::string& text) {
    return fnmatch(pattern.c_str(), text.c_str(), 0) == 0;
}

}  // namespace dfa
