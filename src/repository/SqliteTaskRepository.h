#ifndef SQLITE_TASK_REPOSITORY_H
#define SQLITE_TASK_REPOSITORY_H

#include "ITaskRepository.h"
#include "../sqlite3.h"
#include <mutex>

class SqliteTaskRepository : public ITaskRepository {
public:
    SqliteTaskRepository(const std::string& dbPath = "tasks.db", const std::string& legacyJsonPath = "tasks.json");
    virtual ~SqliteTaskRepository();

    bool initialize() override;
    std::vector<Task> getAll(bool includeDeleted = false) override;
    Task getById(const std::string& id) override;
    bool save(const Task& task) override;
    bool remove(const std::string& id) override;
    bool softDelete(const std::string& id, long long updatedAt) override;
    bool mergeTaskLWW(const Task& incoming) override;

    bool migrateFromJson(const std::string& jsonFilePath);

private:
    std::string m_dbPath;
    std::string m_legacyJsonPath;
    sqlite3* m_db;
    std::recursive_mutex m_mutex;

    bool execute(const std::string& sql);
};

#endif // SQLITE_TASK_REPOSITORY_H
