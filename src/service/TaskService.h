#ifndef TASK_SERVICE_H
#define TASK_SERVICE_H

#include "../repository/ITaskRepository.h"
#include <memory>
#include <functional>
#include <vector>
#include <string>

class TaskService {
public:
    using TaskChangeCallback = std::function<void(const Task& task)>;

    TaskService(std::shared_ptr<ITaskRepository> repository, const std::string& nodeId);

    std::vector<Task> getAllTasks(bool includeDeleted = false);
    Task getTaskById(const std::string& id);
    Task createTask(const std::string& title, const std::string& description);
    bool updateTaskStatus(const std::string& id, bool done);
    bool deleteTask(const std::string& id);

    // P2P LWW Merge
    bool mergeIncomingTask(const Task& incoming);
    int mergeIncomingTasks(const std::vector<Task>& incomingList);

    // Callback when local changes happen (to broadcast to peers)
    void setOnTaskChanged(TaskChangeCallback callback);

    std::string getNodeId() const { return m_nodeId; }

    // JSON serialization utilities
    static std::string tasksToJson(const std::vector<Task>& tasks);
    static std::string taskToJson(const Task& task);
    static bool parseTaskFromJson(const std::string& json, Task& outTask);
    static std::vector<Task> parseTaskListFromJson(const std::string& json);

private:
    std::shared_ptr<ITaskRepository> m_repository;
    std::string m_nodeId;
    TaskChangeCallback m_onTaskChanged;

    static long long getCurrentTimeMillis();
    std::string generateTaskId();
};

#endif // TASK_SERVICE_H
