#include "core/util/text.hpp"

#include <algorithm>
#include <cstdio>

namespace bh::text {

namespace {
char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }
std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
  return s;
}
}  // namespace

std::string to_lower(std::string_view s) {
  std::string out(s);
  std::ranges::transform(out, out.begin(), lower);
  return out;
}

bool equals_ci(std::string_view a, std::string_view b) {
  return std::ranges::equal(a, b, [](char x, char y) { return lower(x) == lower(y); });
}

bool starts_with_ci(std::string_view s, std::string_view prefix) {
  return s.size() >= prefix.size() && equals_ci(s.substr(0, prefix.size()), prefix);
}

bool contains_ci(std::string_view haystack, std::string_view needle) {
  if (needle.empty()) return true;
  auto it = std::ranges::search(haystack, needle, [](char x, char y) { return lower(x) == lower(y); });
  return !it.empty();
}

std::vector<std::string> split_multi(std::string_view s, std::string_view seps) {
  std::vector<std::string> out;
  while (!s.empty()) {
    auto pos = s.find_first_of(seps);
    auto part = trim(s.substr(0, pos));
    if (!part.empty()) out.emplace_back(part);
    if (pos == std::string_view::npos) break;
    s.remove_prefix(pos + 1);
  }
  return out;
}

std::string join(const std::vector<std::string>& parts, std::string_view sep) {
  std::string out;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i) out += sep;
    out += parts[i];
  }
  return out;
}

std::string format_duration(std::chrono::milliseconds d) {
  auto total = std::max<long long>(0, d.count() / 1000);
  auto h = total / 3600, m = (total / 60) % 60, s = total % 60;
  char buf[32];
  if (h > 0)
    std::snprintf(buf, sizeof buf, "%lld:%02lld:%02lld", h, m, s);
  else
    std::snprintf(buf, sizeof buf, "%lld:%02lld", m, s);
  return buf;
}

}  // namespace bh::text
