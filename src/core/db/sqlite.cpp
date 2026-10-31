#include "core/db/sqlite.hpp"

#include <sqlite3.h>

namespace bh::db {

namespace detail {
void DbCloser::operator()(sqlite3* d) const { sqlite3_close_v2(d); }
void StmtFinalizer::operator()(sqlite3_stmt* s) const { sqlite3_finalize(s); }
}  // namespace detail

namespace {
Error make_err(sqlite3* db) { return {ErrorCode::Database, sqlite3_errmsg(db)}; }
}  // namespace

// ---- Statement ----

Error Statement::err() const { return make_err(db_); }

Statement& Statement::bind(int idx, std::nullptr_t) { sqlite3_bind_null(stmt_.get(), idx); return *this; }
Statement& Statement::bind(int idx, std::int64_t v) { sqlite3_bind_int64(stmt_.get(), idx, v); return *this; }
Statement& Statement::bind(int idx, double v) { sqlite3_bind_double(stmt_.get(), idx, v); return *this; }
Statement& Statement::bind(int idx, std::string_view v) {
  sqlite3_bind_text64(stmt_.get(), idx, v.data(), v.size(), SQLITE_TRANSIENT, SQLITE_UTF8);
  return *this;
}

int Statement::index_of(std::string_view name) const {
  return sqlite3_bind_parameter_index(stmt_.get(), std::string(name).c_str());
}

Result<bool> Statement::step() {
  switch (sqlite3_step(stmt_.get())) {
    case SQLITE_ROW: return true;
    case SQLITE_DONE: return false;
    default: return std::unexpected(err());
  }
}

Result<void> Statement::run() {
  for (;;) {
    auto r = step();
    if (!r) return std::unexpected(std::move(r.error()));
    if (!*r) return {};
  }
}

void Statement::reset() {
  sqlite3_reset(stmt_.get());
  sqlite3_clear_bindings(stmt_.get());
}

bool Statement::is_null(int c) const { return sqlite3_column_type(stmt_.get(), c) == SQLITE_NULL; }
std::int64_t Statement::i64(int c) const { return sqlite3_column_int64(stmt_.get(), c); }
double Statement::f64(int c) const { return sqlite3_column_double(stmt_.get(), c); }
std::string Statement::text(int c) const {
  auto* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt_.get(), c));
  return p ? std::string(p, static_cast<std::size_t>(sqlite3_column_bytes(stmt_.get(), c))) : std::string{};
}
std::optional<int> Statement::opt_int(int c) const {
  return is_null(c) ? std::nullopt : std::optional<int>(i32(c));
}
std::optional<double> Statement::opt_f64(int c) const {
  return is_null(c) ? std::nullopt : std::optional<double>(f64(c));
}

// ---- Database ----

Result<Database> Database::open_raw(const std::string& name) {
  sqlite3* raw = nullptr;
  int rc = sqlite3_open_v2(name.c_str(), &raw,
                           SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
  std::unique_ptr<sqlite3, detail::DbCloser> owned(raw);
  if (rc != SQLITE_OK)
    return std::unexpected(Error{ErrorCode::Database, raw ? sqlite3_errmsg(raw) : "out of memory"});

  Database db(std::move(owned));
  sqlite3_busy_timeout(db.handle(), 5000);
  BH_TRY(db.exec("PRAGMA foreign_keys = ON; PRAGMA journal_mode = WAL; PRAGMA synchronous = NORMAL;"));
  BH_TRY(migrate(db));
  return db;
}

// Plain path (not a file: URI) so Windows drive letters and spaces need no escaping.
Result<Database> Database::open(const std::filesystem::path& file) { return open_raw(file.string()); }
Result<Database> Database::open_memory() { return open_raw(":memory:"); }

Result<void> Database::exec(std::string_view sql) {
  char* msg = nullptr;
  int rc = sqlite3_exec(db_.get(), std::string(sql).c_str(), nullptr, nullptr, &msg);
  if (rc != SQLITE_OK) {
    Error e{ErrorCode::Database, msg ? msg : sqlite3_errmsg(db_.get())};
    sqlite3_free(msg);
    return std::unexpected(std::move(e));
  }
  return {};
}

Result<Statement> Database::prepare(std::string_view sql) {
  sqlite3_stmt* raw = nullptr;
  if (sqlite3_prepare_v2(db_.get(), sql.data(), static_cast<int>(sql.size()), &raw, nullptr) != SQLITE_OK)
    return std::unexpected(make_err(db_.get()));
  return Statement(std::unique_ptr<sqlite3_stmt, detail::StmtFinalizer>(raw), db_.get());
}

std::int64_t Database::last_insert_rowid() const { return sqlite3_last_insert_rowid(db_.get()); }
int Database::changes() const { return sqlite3_changes(db_.get()); }

// ---- Transaction ----

Result<Transaction> Transaction::begin(Database& db) {
  BH_TRY(db.exec("BEGIN IMMEDIATE"));
  return Transaction(&db);
}

Result<void> Transaction::commit() {
  if (!db_) return {};
  auto r = db_->exec("COMMIT");
  if (r) db_ = nullptr;
  return r;
}

Transaction::~Transaction() {
  if (db_) (void)db_->exec("ROLLBACK");
}

}  // namespace bh::db
