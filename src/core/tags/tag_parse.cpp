#include "core/tags/tag_parse.hpp"

#include <charconv>
#include <string>

namespace bh::tags {

namespace {

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
  return s;
}

std::optional<int> parse_int_prefix(std::string_view s) {
  s = trim(s);
  int v = 0;
  auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
  if (ec != std::errc{} || p == s.data()) return std::nullopt;
  return v;
}

std::optional<double> parse_double_prefix(std::string_view s) {
  s = trim(s);
  if (!s.empty() && s.front() == '+') s.remove_prefix(1);  // from_chars rejects a leading '+'
  double v = 0;
  auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
  if (ec != std::errc{} || p == s.data()) return std::nullopt;
  return v;
}

}  // namespace

std::pair<std::optional<int>, std::optional<int>> parse_number_pair(std::string_view s) {
  auto slash = s.find('/');
  auto first = parse_int_prefix(s.substr(0, slash));
  if (!first || *first < 0) return {};
  std::optional<int> total;
  if (slash != std::string_view::npos) {
    total = parse_int_prefix(s.substr(slash + 1));
    if (total && *total <= 0) total.reset();
  }
  return {first, total};
}

std::optional<int> parse_year(std::string_view s) {
  s = trim(s);
  if (s.size() < 4) return std::nullopt;
  auto y = parse_int_prefix(s.substr(0, 4));
  if (!y || *y < 1000 || *y > 9999) return std::nullopt;
  return y;
}

std::optional<double> parse_gain_db(std::string_view s) { return parse_double_prefix(s); }

std::optional<double> parse_peak(std::string_view s) {
  auto v = parse_double_prefix(s);
  return v && *v > 0 ? v : std::nullopt;
}

}  // namespace bh::tags
