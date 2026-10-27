#ifndef TASK_STORAGE_H
#define TASK_STORAGE_H

#include "Task.h"
#include <vector>
#include <string>

class TaskStorage {
public:
    TaskStorage(const std::string& filePath = "tasks.json");

    bool load();
    bool save();

    std::vector<Task> getAllTasks() const;
    Task addTask(const std::string& title, const std::string& description);
    bool updateTaskStatus(const std::string& id, bool done);
    bool deleteTask(const std::string& id);

    // Serialization utilities
    static std::string tasksToJson(const std::vector<Task>& tasks);
    static std::string taskToJson(const Task& task);
    static bool parseTaskFromJson(const std::string& json, std::string& outTitle, std::string& outDesc, bool& outDone, std::string& outId);

private:
    std::string m_filePath;
    std::vector<Task> m_tasks;

    static std::string generateId();
    static std::string escapeJson(const std::string& s);
    static std::string unescapeJson(const std::string& s);
    static std::vector<Task> parseJsonToTasks(const std::string& jsonStr);
};

#endif // TASK_STORAGE_H
