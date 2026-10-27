#ifndef ITASK_REPOSITORY_H
#define ITASK_REPOSITORY_H

#include "../models/Task.h"
#include <vector>
#include <string>

class ITaskRepository {
public:
    virtual ~ITaskRepository() = default;

    virtual bool initialize() = 0;
    virtual std::vector<Task> getAll(bool includeDeleted = false) = 0;
    virtual Task getById(const std::string& id) = 0;
    virtual bool save(const Task& task) = 0;
    virtual bool remove(const std::string& id) = 0;
    virtual bool softDelete(const std::string& id, long long updatedAt) = 0;
    virtual bool mergeTaskLWW(const Task& incoming) = 0; // Last Writer Wins merge
};

#endif // ITASK_REPOSITORY_H
