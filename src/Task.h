#ifndef TASK_H
#define TASK_H

#include <string>

struct Task {
    std::string id;
    std::string title;
    std::string description;
    bool done;
    long long createdAt; // Unix timestamp in seconds
    long long updatedAt; // Unix timestamp in seconds (ready for V4 LWW conflict resolution)

    Task() : done(false), createdAt(0), updatedAt(0) {}
};

#endif // TASK_H
