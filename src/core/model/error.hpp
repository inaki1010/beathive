#pragma once
#include <expected>
#include <string>
#include <utility>

// Propagate an error out of a function that returns Result<...>.
#define BH_TRY(expr)                                      \
  if (auto&& bh_try_r_ = (expr); !bh_try_r_) {            \
    return std::unexpected(std::move(bh_try_r_.error())); \
  }

namespace bh {

enum class ErrorCode {
  Io,
  Unsupported,
  Corrupt,
  NotFound,
  Database,
  InvalidArgument,
  Device,
};

struct Error {
  ErrorCode code;
  std::string message;
};

template <typename T = void>
using Result = std::expected<T, Error>;

}  // namespace bh
