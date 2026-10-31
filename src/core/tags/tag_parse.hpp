#pragma once
#include <optional>
#include <string_view>
#include <utility>

namespace bh::tags {

// "3" -> {3, nullopt}, "3/12" -> {3, 12}, junk -> {nullopt, nullopt}
std::pair<std::optional<int>, std::optional<int>> parse_number_pair(std::string_view s);

// "2001", "2001-05-06", "2001-05" -> 2001. Years outside 1000..9999 are rejected.
std::optional<int> parse_year(std::string_view s);

// "-6.50 dB", "+3.1dB", "-6.5" -> dB value
std::optional<double> parse_gain_db(std::string_view s);

// "0.987654" -> linear peak (> 0)
std::optional<double> parse_peak(std::string_view s);

}  // namespace bh::tags
