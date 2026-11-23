#ifndef DFA_GLOB_HPP
#define DFA_GLOB_HPP

#include <string>

namespace dfa {

// Shell-style glob match (`*`, `?`, `[...]`) via POSIX fnmatch — we never
// hand-roll glob matching, same "don't reimplement what libc already
// gives you correctly" philosophy as never hand-rolling SHA-256.
bool GlobMatch(const std::string& pattern, const std::string& text);

}  // namespace dfa

#endif
