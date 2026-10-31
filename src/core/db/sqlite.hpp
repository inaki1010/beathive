#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "core/model/error.hpp"

struct sqlite3;
struct sqlite3_stmt;

namespace bh::db {

namespace detail {
struct DbCloser { void operator()(sqlite3*) const; };
struct StmtFinalizer { void operator()(sqlite3_stmt*) const; };
}  // namespace detail

// A prepared statement. Must not outlive the Database that created it.
// Parameters bind by 1-based index or by name (":name").
class Statement {
 public:
  Statement(std::unique_ptr<sqlite3_stmt, detail::StmtFinalizer> s, sqlite3* db)
      : stmt_(std::move(s)), db_(db) {}

  Statement& bind(int idx, std::nullptr_t);
  Statement& bind(int idx, std::int64_t v);
  Statement& bind(int idx, int v) { return bind(idx, static_cast<std::int64_t>(v)); }
  Statement& bind(int idx, double v);
  Statement& bind(int idx, std::string_view v);
  template <typename T>
  Statement& bind(int idx, const std::optional<T>& v) {
    return v ? bind(idx, *v) : bind(idx, nullptr);
  }
  template <typename T>
  Statement& bind(std::string_view name, const T& v) { return bind(index_of(name), v); }

  // true = a row is available, false = done.
  Result<bool> step();
  // Steps to completion, ignoring any rows.
  Result<void> run();
  void reset();

  bool is_null(int col) const;
  std::int64_t i64(int col) const;
  int i32(int col) const { return static_cast<int>(i64(col)); }
  double f64(int col) const;
  std::string text(int col) const;
  std::optional<int> opt_int(int col) const;
  std::optional<double> opt_f64(int col) const;

 private:
  int index_of(std::string_view name) const;
  Error err() const;

  std::unique_ptr<sqlite3_stmt, detail::StmtFinalizer> stmt_;
  sqlite3* db_;
};

class Database {
 public:
  // Opens (creating if needed), enables WAL + foreign keys, and migrates to the latest schema.
  static Result<Database> open(const std::filesystem::path& file);
  static Result<Database> open_memory();

  Result<void> exec(std::string_view sql);
  Result<Statement> prepare(std::string_view sql);
  std::int64_t last_insert_rowid() const;
  int changes() const;
  sqlite3* handle() const { return db_.get(); }

 private:
  explicit Database(std::unique_ptr<sqlite3, detail::DbCloser> db) : db_(std::move(db)) {}
  static Result<Database> open_raw(const std::string& name);
  std::unique_ptr<sqlite3, detail::DbCloser> db_;
};

// RAII write transaction. Rolls back unless commit() is called.
class Transaction {
 public:
  static Result<Transaction> begin(Database& db);
  Transaction(Transaction&& o) noexcept : db_(std::exchange(o.db_, nullptr)) {}
  ~Transaction();
  Result<void> commit();

 private:
  explicit Transaction(Database* db) : db_(db) {}
  Database* db_;
};

// Applies pending migrations (tracked in PRAGMA user_version).
Result<void> migrate(Database& db);
int latest_schema_version();

}  // namespace bh::db
