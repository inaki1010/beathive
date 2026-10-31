#pragma once
#include <QString>
#include <filesystem>
#include <string>

#include "core/util/paths.hpp"

namespace bh::ui {

inline QString qs(const std::string& s) { return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size())); }
inline std::string stdstr(const QString& s) { return s.toUtf8().toStdString(); }
inline QString from_path(const std::filesystem::path& p) { return qs(to_utf8(p)); }
inline std::filesystem::path to_path(const QString& s) { return path_from_utf8(stdstr(s)); }

}  // namespace bh::ui
