#include "SqliteTaskRepository.h"
#include <iostream>
#include <fstream>
#include <sstream>

SqliteTaskRepository::SqliteTaskRepository(const std::string& dbPath, const std::string& legacyJsonPath)
    : m_dbPath(dbPath), m_legacyJsonPath(legacyJsonPath), m_db(nullptr) {
}

SqliteTaskRepository::~SqliteTaskRepository() {
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

bool SqliteTaskRepository::execute(const std::string& sql) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "[SqliteTaskRepository] SQL Error: " << (errMsg ? errMsg : "Unknown") << " | SQL: " << sql << std::endl;
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool SqliteTaskRepository::initialize() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    int rc = sqlite3_open(m_dbPath.c_str(), &m_db);
    if (rc != SQLITE_OK) {
        std::cerr << "[SqliteTaskRepository] Cannot open database: " << sqlite3_errmsg(m_db) << std::endl;
        return false;
    }

    // Enable WAL mode for better concurrent performance
    execute("PRAGMA journal_mode=WAL;");

    // Create Tasks table
    std::string createTableSql = 
        "CREATE TABLE IF NOT EXISTS tasks ("
        "  id TEXT PRIMARY KEY,"
        "  title TEXT NOT NULL,"
        "  description TEXT,"
        "  done INTEGER NOT NULL DEFAULT 0,"
        "  isDeleted INTEGER NOT NULL DEFAULT 0,"
        "  createdAt INTEGER NOT NULL,"
        "  updatedAt INTEGER NOT NULL,"
        "  originNodeId TEXT"
        ");";

    if (!execute(createTableSql)) {
        return false;
    }

    std::cout << "[SqliteTaskRepository] SQLite database initialized: " << m_dbPath << std::endl;

    // Check if migration from tasks.json is needed
    sqlite3_stmt* countStmt = nullptr;
    int count = 0;
    if (sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM tasks;", -1, &countStmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(countStmt) == SQLITE_ROW) {
            count = sqlite3_column_int(countStmt, 0);
        }
        sqlite3_finalize(countStmt);
    }

    if (count == 0 && !m_legacyJsonPath.empty()) {
        migrateFromJson(m_legacyJsonPath);
    }

    return true;
}

bool SqliteTaskRepository::migrateFromJson(const std::string& jsonFilePath) {
    std::ifstream file(jsonFilePath);
    if (!file.is_open()) return false;

    std::ostringstream ss;
    ss << file.rdbuf();
    file.close();
    std::string jsonStr = ss.str();

    std::cout << "[SqliteTaskRepository] Found legacy " << jsonFilePath << ", migrating data to SQLite..." << std::endl;

    // Simple parsing to extract tasks
    size_t pos = 0;
    int migratedCount = 0;
    while (pos < jsonStr.size()) {
        size_t startObj = jsonStr.find('{', pos);
        if (startObj == std::string::npos) break;

        size_t endObj = std::string::npos;
        bool inQuotes = false;
        int depth = 0;
        for (size_t i = startObj; i < jsonStr.size(); ++i) {
            if (jsonStr[i] == '"' && (i == 0 || jsonStr[i - 1] != '\\')) {
                inQuotes = !inQuotes;
            } else if (!inQuotes) {
                if (jsonStr[i] == '{') depth++;
                else if (jsonStr[i] == '}') {
                    depth--;
                    if (depth == 0) {
                        endObj = i;
                        break;
                    }
                }
            }
        }

        if (endObj == std::string::npos) break;
        std::string objStr = jsonStr.substr(startObj, endObj - startObj + 1);

        Task t;
        // Helper lambda
        auto extractStr = [](const std::string& src, const std::string& key) -> std::string {
            std::string sk = "\"" + key + "\"";
            size_t p = src.find(sk);
            if (p == std::string::npos) return "";
            size_t col = src.find(':', p + sk.size());
            if (col == std::string::npos) return "";
            size_t q1 = src.find('"', col + 1);
            if (q1 == std::string::npos) return "";
            size_t q2 = q1 + 1;
            while (q2 < src.size() && (src[q2] != '"' || src[q2 - 1] == '\\')) q2++;
            if (q2 >= src.size()) return "";
            return src.substr(q1 + 1, q2 - q1 - 1);
        };

        t.id = extractStr(objStr, "id");
        t.title = extractStr(objStr, "title");
        t.description = extractStr(objStr, "description");
        t.done = (objStr.find("\"done\": true") != std::string::npos || objStr.find("\"done\":true") != std::string::npos);
        t.isDeleted = false;
        t.createdAt = 1000LL * time(nullptr);
        t.updatedAt = t.createdAt;
        t.originNodeId = "migrated_json";

        if (!t.id.empty() && !t.title.empty()) {
            save(t);
            migratedCount++;
        }

        pos = endObj + 1;
    }

    std::cout << "[SqliteTaskRepository] Migration completed! Migrated " << migratedCount << " tasks to SQLite." << std::endl;
    return true;
}

std::vector<Task> SqliteTaskRepository::getAll(bool includeDeleted) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<Task> result;

    std::string sql = "SELECT id, title, description, done, isDeleted, createdAt, updatedAt, originNodeId FROM tasks";
    if (!includeDeleted) {
        sql += " WHERE isDeleted = 0";
    }
    sql += " ORDER BY createdAt DESC;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "[SqliteTaskRepository] Error prepare getAll: " << sqlite3_errmsg(m_db) << std::endl;
        return result;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Task t;
        const unsigned char* idText = sqlite3_column_text(stmt, 0);
        const unsigned char* titleText = sqlite3_column_text(stmt, 1);
        const unsigned char* descText = sqlite3_column_text(stmt, 2);
        const unsigned char* nodeText = sqlite3_column_text(stmt, 7);

        t.id = idText ? reinterpret_cast<const char*>(idText) : "";
        t.title = titleText ? reinterpret_cast<const char*>(titleText) : "";
        t.description = descText ? reinterpret_cast<const char*>(descText) : "";
        t.done = sqlite3_column_int(stmt, 3) != 0;
        t.isDeleted = sqlite3_column_int(stmt, 4) != 0;
        t.createdAt = sqlite3_column_int64(stmt, 5);
        t.updatedAt = sqlite3_column_int64(stmt, 6);
        t.originNodeId = nodeText ? reinterpret_cast<const char*>(nodeText) : "";

        result.push_back(t);
    }

    sqlite3_finalize(stmt);
    return result;
}

Task SqliteTaskRepository::getById(const std::string& id) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    Task t;

    const char* sql = "SELECT id, title, description, done, isDeleted, createdAt, updatedAt, originNodeId FROM tasks WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return t;
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* idText = sqlite3_column_text(stmt, 0);
        const unsigned char* titleText = sqlite3_column_text(stmt, 1);
        const unsigned char* descText = sqlite3_column_text(stmt, 2);
        const unsigned char* nodeText = sqlite3_column_text(stmt, 7);

        t.id = idText ? reinterpret_cast<const char*>(idText) : "";
        t.title = titleText ? reinterpret_cast<const char*>(titleText) : "";
        t.description = descText ? reinterpret_cast<const char*>(descText) : "";
        t.done = sqlite3_column_int(stmt, 3) != 0;
        t.isDeleted = sqlite3_column_int(stmt, 4) != 0;
        t.createdAt = sqlite3_column_int64(stmt, 5);
        t.updatedAt = sqlite3_column_int64(stmt, 6);
        t.originNodeId = nodeText ? reinterpret_cast<const char*>(nodeText) : "";
    }

    sqlite3_finalize(stmt);
    return t;
}

bool SqliteTaskRepository::save(const Task& task) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    const char* sql = 
        "INSERT INTO tasks (id, title, description, done, isDeleted, createdAt, updatedAt, originNodeId) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET "
        "title = excluded.title, "
        "description = excluded.description, "
        "done = excluded.done, "
        "isDeleted = excluded.isDeleted, "
        "updatedAt = excluded.updatedAt, "
        "originNodeId = excluded.originNodeId;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "[SqliteTaskRepository] Error prepare save: " << sqlite3_errmsg(m_db) << std::endl;
        return false;
    }

    sqlite3_bind_text(stmt, 1, task.id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, task.title.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, task.description.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, task.done ? 1 : 0);
    sqlite3_bind_int(stmt, 5, task.isDeleted ? 1 : 0);
    sqlite3_bind_int64(stmt, 6, task.createdAt);
    sqlite3_bind_int64(stmt, 7, task.updatedAt);
    sqlite3_bind_text(stmt, 8, task.originNodeId.c_str(), -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return (rc == SQLITE_DONE);
}

bool SqliteTaskRepository::remove(const std::string& id) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    const char* sql = "DELETE FROM tasks WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return (rc == SQLITE_DONE);
}

bool SqliteTaskRepository::softDelete(const std::string& id, long long updatedAt) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    const char* sql = "UPDATE tasks SET isDeleted = 1, updatedAt = ? WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int64(stmt, 1, updatedAt);
    sqlite3_bind_text(stmt, 2, id.c_str(), -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return (rc == SQLITE_DONE);
}

bool SqliteTaskRepository::mergeTaskLWW(const Task& incoming) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    // Query existing task without lock re-entrance
    const char* querySql = "SELECT updatedAt FROM tasks WHERE id = ?;";
    sqlite3_stmt* queryStmt = nullptr;
    long long existingUpdatedAt = -1;
    bool exists = false;

    if (sqlite3_prepare_v2(m_db, querySql, -1, &queryStmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(queryStmt, 1, incoming.id.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(queryStmt) == SQLITE_ROW) {
            exists = true;
            existingUpdatedAt = sqlite3_column_int64(queryStmt, 0);
        }
        sqlite3_finalize(queryStmt);
    }

    // Last Writer Wins check
    if (exists && existingUpdatedAt >= incoming.updatedAt) {
        // Local version is newer or equal, skip
        return false;
    }

    // Incoming version is strictly newer or new task, persist it
    const char* insertSql = 
        "INSERT INTO tasks (id, title, description, done, isDeleted, createdAt, updatedAt, originNodeId) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET "
        "title = excluded.title, "
        "description = excluded.description, "
        "done = excluded.done, "
        "isDeleted = excluded.isDeleted, "
        "updatedAt = excluded.updatedAt, "
        "originNodeId = excluded.originNodeId;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, insertSql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, incoming.id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, incoming.title.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, incoming.description.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, incoming.done ? 1 : 0);
    sqlite3_bind_int(stmt, 5, incoming.isDeleted ? 1 : 0);
    sqlite3_bind_int64(stmt, 6, incoming.createdAt);
    sqlite3_bind_int64(stmt, 7, incoming.updatedAt);
    sqlite3_bind_text(stmt, 8, incoming.originNodeId.c_str(), -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return (rc == SQLITE_DONE);
}
