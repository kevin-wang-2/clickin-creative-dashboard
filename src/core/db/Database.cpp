#include "core/db/Database.h"
#include "sqlite3.h"

#include <array>
#include <random>
#include <sstream>
#include <iomanip>

namespace clickin {

// ── Impl ─────────────────────────────────────────────────────────────────────

struct Database::Impl {
    sqlite3* db = nullptr;
};

// ── Statement ────────────────────────────────────────────────────────────────

Database::Statement::Statement(sqlite3_stmt* stmt, sqlite3* db)
    : stmt_(stmt), db_(db) {}

Database::Statement::~Statement() {
    if (stmt_) sqlite3_finalize(stmt_);
}

Database::Statement::Statement(Statement&& o) noexcept
    : stmt_(o.stmt_), db_(o.db_) { o.stmt_ = nullptr; o.db_ = nullptr; }

Database::Statement& Database::Statement::operator=(Statement&& o) noexcept {
    if (this != &o) {
        if (stmt_) sqlite3_finalize(stmt_);
        stmt_ = o.stmt_; db_ = o.db_;
        o.stmt_ = nullptr; o.db_ = nullptr;
    }
    return *this;
}

Database::Statement& Database::Statement::bindText(int idx, std::string_view value) {
    sqlite3_bind_text(stmt_, idx, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
    return *this;
}

Database::Statement& Database::Statement::bindInt64(int idx, int64_t value) {
    sqlite3_bind_int64(stmt_, idx, value);
    return *this;
}

Database::Statement& Database::Statement::bindBlob(int idx, std::span<const std::byte> value) {
    sqlite3_bind_blob(stmt_, idx, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
    return *this;
}

Database::Statement& Database::Statement::bindNull(int idx) {
    sqlite3_bind_null(stmt_, idx);
    return *this;
}

bool Database::Statement::step() {
    int rc = sqlite3_step(stmt_);
    return rc == SQLITE_ROW;
}

void Database::Statement::reset() {
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
}

std::string Database::Statement::columnText(int col) const {
    const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt_, col));
    return txt ? txt : "";
}

int64_t Database::Statement::columnInt64(int col) const {
    return sqlite3_column_int64(stmt_, col);
}

std::vector<std::byte> Database::Statement::columnBlob(int col) const {
    const void* data = sqlite3_column_blob(stmt_, col);
    int bytes = sqlite3_column_bytes(stmt_, col);
    if (!data || bytes <= 0) return {};
    const auto* ptr = static_cast<const std::byte*>(data);
    return {ptr, ptr + bytes};
}

bool Database::Statement::columnIsNull(int col) const {
    return sqlite3_column_type(stmt_, col) == SQLITE_NULL;
}

// ── Database ──────────────────────────────────────────────────────────────────

Database::Database(const std::string& path) : path_(path), impl_(new Impl{}) {}

Database::~Database() {
    close();
    delete impl_;
}

bool Database::open() {
    int rc = sqlite3_open(path_.c_str(), &impl_->db);
    if (rc != SQLITE_OK) return false;
    // Enable WAL mode and foreign keys for all connections.
    sqlite3_exec(impl_->db, "PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON;",
                 nullptr, nullptr, nullptr);
    return true;
}

void Database::close() {
    if (impl_->db) {
        sqlite3_close(impl_->db);
        impl_->db = nullptr;
    }
}

bool Database::isOpen() const { return impl_->db != nullptr; }

bool Database::execute(std::string_view sql) {
    char* errmsg = nullptr;
    int rc = sqlite3_exec(impl_->db, std::string(sql).c_str(), nullptr, nullptr, &errmsg);
    if (errmsg) sqlite3_free(errmsg);
    return rc == SQLITE_OK;
}

std::optional<Database::Statement> Database::prepare(std::string_view sql) {
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql.data(), static_cast<int>(sql.size()),
                                 &stmt, nullptr);
    if (rc != SQLITE_OK || !stmt) return std::nullopt;
    return Statement{stmt, impl_->db};
}

bool Database::beginTransaction() {
    return execute("BEGIN");
}

bool Database::commit() {
    return execute("COMMIT");
}

bool Database::rollback() {
    return execute("ROLLBACK");
}

int64_t Database::lastInsertRowId() const {
    return sqlite3_last_insert_rowid(impl_->db);
}

int Database::changes() const {
    return sqlite3_changes(impl_->db);
}

std::string Database::lastError() const {
    if (!impl_->db) return "database not open";
    return sqlite3_errmsg(impl_->db);
}

std::string Database::generateId() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist;

    uint64_t hi = dist(rng);
    uint64_t lo = dist(rng);

    // Set UUID v4 version and variant bits.
    hi = (hi & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    lo = (lo & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(8)  << (hi >> 32)
        << '-' << std::setw(4) << ((hi >> 16) & 0xFFFF)
        << '-' << std::setw(4) << (hi & 0xFFFF)
        << '-' << std::setw(4) << (lo >> 48)
        << '-' << std::setw(12) << (lo & 0x0000FFFFFFFFFFFFULL);
    return oss.str();
}

} // namespace clickin
