#ifndef DFA_REPORT_HPP
#define DFA_REPORT_HPP

#include <string>
#include <vector>

#include "dfa/grouping.hpp"

namespace dfa {

std::string RenderText(const std::vector<DuplicateGroup>& groups);
std::string RenderCsv(const std::vector<DuplicateGroup>& groups);
std::string RenderJson(const std::vector<DuplicateGroup>& groups);

std::uint64_t TotalReclaimableBytes(const std::vector<DuplicateGroup>& groups);

}  // namespace dfa

#endif
