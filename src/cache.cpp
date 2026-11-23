#include "dfa/cache.hpp"

#include <sqlite3.h>

#include <stdexcept>

namespace dfa {

namespace {

void CheckOk(int rc, sqlite3* db, const std::string& what) {
    if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW) {
        throw std::runtime_error(what + ": " + sqlite3_errmsg(db));
    }
}

struct StmtGuard {
    sqlite3_stmt* stmt = nullptr;
    ~StmtGuard() {
        if (stmt) sqlite3_finalize(stmt);
    }
};

}  // namespace

HashCache::HashCache(const std::string& db_path) {
    int rc = sqlite3_open(db_path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::string msg = db_ ? sqlite3_errmsg(db_) : "unknown error";
        if (db_) sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("cannot open cache database " + db_path + ": " + msg);
    }

    static const char* kCreateTable =
        "CREATE TABLE IF NOT EXISTS file_hashes ("
        "  path TEXT PRIMARY KEY,"
        "  size INTEGER NOT NULL,"
        "  mtime INTEGER NOT NULL,"
        "  inode INTEGER NOT NULL,"
        "  dev INTEGER NOT NULL,"
        "  partial_hash TEXT,"
        "  full_hash TEXT"
        ");";
    char* err = nullptr;
    rc = sqlite3_exec(db_, kCreateTable, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("cannot create cache table: " + msg);
    }
}

HashCache::~HashCache() {
    if (db_) sqlite3_close(db_);
}

std::optional<std::string> HashCache::Get(const FileRecord& record, HashKind kind) const {
    std::lock_guard<std::mutex> lock(mutex_);

    static const char* kSelect =
        "SELECT size, mtime, inode, dev, partial_hash, full_hash "
        "FROM file_hashes WHERE path = ?;";
    StmtGuard guard;
    CheckOk(sqlite3_prepare_v2(db_, kSelect, -1, &guard.stmt, nullptr), db_, "prepare select");
    sqlite3_bind_text(guard.stmt, 1, record.path.c_str(), -1, SQLITE_STATIC);

    int rc = sqlite3_step(guard.stmt);
    if (rc != SQLITE_ROW) {
        CheckOk(rc, db_, "step select");
        return std::nullopt;
    }

    auto size = static_cast<std::uint64_t>(sqlite3_column_int64(guard.stmt, 0));
    auto mtime = static_cast<std::int64_t>(sqlite3_column_int64(guard.stmt, 1));
    auto inode = static_cast<std::uint64_t>(sqlite3_column_int64(guard.stmt, 2));
    auto dev = static_cast<std::uint64_t>(sqlite3_column_int64(guard.stmt, 3));

    if (size != record.size || mtime != record.mtime || inode != record.inode ||
        dev != record.dev) {
        return std::nullopt;  // metadata changed since this row was cached: structural miss
    }

    int col = kind == HashKind::Partial ? 4 : 5;
    if (sqlite3_column_type(guard.stmt, col) == SQLITE_NULL) {
        return std::nullopt;
    }
    const unsigned char* text = sqlite3_column_text(guard.stmt, col);
    return std::string(reinterpret_cast<const char*>(text));
}

void HashCache::Put(const FileRecord& record, HashKind kind, const std::string& hash_hex) {
    std::lock_guard<std::mutex> lock(mutex_);

    // First check whether the existing row (if any) has matching
    // metadata — if not, the other hash column must be reset to NULL
    // since it was computed for different file content.
    bool metadata_matches = false;
    {
        static const char* kSelect = "SELECT size, mtime, inode, dev FROM file_hashes WHERE path = ?;";
        StmtGuard guard;
        CheckOk(sqlite3_prepare_v2(db_, kSelect, -1, &guard.stmt, nullptr), db_, "prepare select");
        sqlite3_bind_text(guard.stmt, 1, record.path.c_str(), -1, SQLITE_STATIC);
        int rc = sqlite3_step(guard.stmt);
        if (rc == SQLITE_ROW) {
            auto size = static_cast<std::uint64_t>(sqlite3_column_int64(guard.stmt, 0));
            auto mtime = static_cast<std::int64_t>(sqlite3_column_int64(guard.stmt, 1));
            auto inode = static_cast<std::uint64_t>(sqlite3_column_int64(guard.stmt, 2));
            auto dev = static_cast<std::uint64_t>(sqlite3_column_int64(guard.stmt, 3));
            metadata_matches = (size == record.size && mtime == record.mtime &&
                                 inode == record.inode && dev == record.dev);
        } else {
            CheckOk(rc, db_, "step select");
        }
    }

    if (metadata_matches) {
        const char* sql = kind == HashKind::Partial
                               ? "UPDATE file_hashes SET partial_hash = ? WHERE path = ?;"
                               : "UPDATE file_hashes SET full_hash = ? WHERE path = ?;";
        StmtGuard guard;
        CheckOk(sqlite3_prepare_v2(db_, sql, -1, &guard.stmt, nullptr), db_, "prepare update");
        sqlite3_bind_text(guard.stmt, 1, hash_hex.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(guard.stmt, 2, record.path.c_str(), -1, SQLITE_STATIC);
        CheckOk(sqlite3_step(guard.stmt), db_, "step update");
        return;
    }

    static const char* kUpsert =
        "INSERT INTO file_hashes (path, size, mtime, inode, dev, partial_hash, full_hash) "
        "VALUES (?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(path) DO UPDATE SET "
        "  size = excluded.size, mtime = excluded.mtime, inode = excluded.inode, "
        "  dev = excluded.dev, partial_hash = excluded.partial_hash, "
        "  full_hash = excluded.full_hash;";
    StmtGuard guard;
    CheckOk(sqlite3_prepare_v2(db_, kUpsert, -1, &guard.stmt, nullptr), db_, "prepare upsert");
    sqlite3_bind_text(guard.stmt, 1, record.path.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(guard.stmt, 2, static_cast<sqlite3_int64>(record.size));
    sqlite3_bind_int64(guard.stmt, 3, static_cast<sqlite3_int64>(record.mtime));
    sqlite3_bind_int64(guard.stmt, 4, static_cast<sqlite3_int64>(record.inode));
    sqlite3_bind_int64(guard.stmt, 5, static_cast<sqlite3_int64>(record.dev));
    if (kind == HashKind::Partial) {
        sqlite3_bind_text(guard.stmt, 6, hash_hex.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_null(guard.stmt, 7);
    } else {
        sqlite3_bind_null(guard.stmt, 6);
        sqlite3_bind_text(guard.stmt, 7, hash_hex.c_str(), -1, SQLITE_STATIC);
    }
    CheckOk(sqlite3_step(guard.stmt), db_, "step upsert");
}

}  // namespace dfa
