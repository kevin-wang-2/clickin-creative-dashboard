#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace clickin {

class Database {
public:
    explicit Database(const std::string& path);
    ~Database();

    Database(const Database&)            = delete;
    Database& operator=(const Database&) = delete;

    bool open();
    void close();
    bool isOpen() const;

    // Run arbitrary SQL (DDL or multi-statement DML). No result rows.
    bool execute(std::string_view sql);

    // Thin RAII wrapper around a prepared statement.
    class Statement {
    public:
        ~Statement();
        Statement(Statement&&) noexcept;
        Statement& operator=(Statement&&) noexcept;
        Statement(const Statement&)            = delete;
        Statement& operator=(const Statement&) = delete;

        Statement& bindText (int idx, std::string_view value);
        Statement& bindInt64(int idx, int64_t value);
        Statement& bindBlob (int idx, std::span<const std::byte> value);
        Statement& bindNull (int idx);

        bool step();   // returns true while a row is available
        void reset();

        std::string            columnText (int col) const;
        int64_t                columnInt64(int col) const;
        std::vector<std::byte> columnBlob (int col) const;
        bool                   columnIsNull(int col) const;

    private:
        friend class Database;
        Statement(sqlite3_stmt* stmt, sqlite3* db);
        sqlite3_stmt* stmt_ = nullptr;
        sqlite3*      db_   = nullptr;
    };

    std::optional<Statement> prepare(std::string_view sql);

    bool beginTransaction();
    bool commit();
    bool rollback();

    int64_t     lastInsertRowId() const;
    int         changes()         const;  // rows affected by the last INSERT/UPDATE/DELETE
    std::string lastError()       const;

    // Generates a random UUID v4 string.
    static std::string generateId();

private:
    std::string path_;
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace clickin
