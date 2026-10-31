#pragma once
#include <chrono>
#include <string>
#include <string_view>
#include <vector>

namespace bh::text {

// ASCII-only case folding. Good enough for the MVP; swap for ICU if needed.
std::string to_lower(std::string_view s);
bool contains_ci(std::string_view haystack, std::string_view needle);
bool equals_ci(std::string_view a, std::string_view b);
bool starts_with_ci(std::string_view s, std::string_view prefix);

// Splits a multi-value tag ("Rock; Pop") on any of `seps`, trimming and
// dropping empty parts.
std::vector<std::string> split_multi(std::string_view s, std::string_view seps = ";");
std::string join(const std::vector<std::string>& parts, std::string_view sep = "; ");

// 3:07 / 1:02:03
std::string format_duration(std::chrono::milliseconds d);

}  // namespace bh::text
