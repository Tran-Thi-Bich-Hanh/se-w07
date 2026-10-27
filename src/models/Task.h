#ifndef TASK_H
#define TASK_H

#include <string>

struct Task {
    std::string id;
    std::string title;
    std::string description;
    bool done;
    bool isDeleted;       // Tombstone for P2P synchronization
    long long createdAt;  // Unix timestamp (ms)
    long long updatedAt;  // Unix timestamp (ms) - Used for Last Writer Wins (LWW)
    std::string originNodeId; // ID of the node that created/last updated the task

    Task() 
        : done(false)
        , isDeleted(false)
        , createdAt(0)
        , updatedAt(0) {}
};

#endif // TASK_H
