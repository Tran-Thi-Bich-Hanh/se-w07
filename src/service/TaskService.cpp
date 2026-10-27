#include "TaskService.h"
#include <chrono>
#include <sstream>
#include <iostream>
#include <random>
#include <algorithm>

TaskService::TaskService(std::shared_ptr<ITaskRepository> repository, const std::string& nodeId)
    : m_repository(repository), m_nodeId(nodeId) {
}

long long TaskService::getCurrentTimeMillis() {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

std::string TaskService::generateTaskId() {
    long long ms = getCurrentTimeMillis();
    static std::mt19937 rng(static_cast<unsigned int>(ms));
    std::uniform_int_distribution<int> dist(1000, 9999);
    
    std::ostringstream ss;
    ss << "task_" << ms << "_" << dist(rng);
    return ss.str();
}

std::vector<Task> TaskService::getAllTasks(bool includeDeleted) {
    return m_repository->getAll(includeDeleted);
}

Task TaskService::getTaskById(const std::string& id) {
    return m_repository->getById(id);
}

Task TaskService::createTask(const std::string& title, const std::string& description) {
    long long now = getCurrentTimeMillis();

    Task t;
    t.id = generateTaskId();
    t.title = title;
    t.description = description;
    t.done = false;
    t.isDeleted = false;
    t.createdAt = now;
    t.updatedAt = now;
    t.originNodeId = m_nodeId;

    m_repository->save(t);

    if (m_onTaskChanged) {
        m_onTaskChanged(t);
    }

    return t;
}

bool TaskService::updateTaskStatus(const std::string& id, bool done) {
    Task existing = m_repository->getById(id);
    if (existing.id.empty()) {
        return false;
    }

    long long now = getCurrentTimeMillis();
    existing.done = done;
    existing.updatedAt = now;
    existing.originNodeId = m_nodeId;

    bool ok = m_repository->save(existing);
    if (ok && m_onTaskChanged) {
        m_onTaskChanged(existing);
    }
    return ok;
}

bool TaskService::deleteTask(const std::string& id) {
    Task existing = m_repository->getById(id);
    if (existing.id.empty()) {
        return false;
    }

    long long now = getCurrentTimeMillis();
    existing.isDeleted = true;
    existing.updatedAt = now;
    existing.originNodeId = m_nodeId;

    // Use soft delete so tombstone can propagate over P2P network
    bool ok = m_repository->softDelete(id, now);
    if (ok && m_onTaskChanged) {
        m_onTaskChanged(existing);
    }
    return ok;
}

bool TaskService::mergeIncomingTask(const Task& incoming) {
    return m_repository->mergeTaskLWW(incoming);
}

int TaskService::mergeIncomingTasks(const std::vector<Task>& incomingList) {
    int updatedCount = 0;
    for (const auto& task : incomingList) {
        if (m_repository->mergeTaskLWW(task)) {
            updatedCount++;
        }
    }
    return updatedCount;
}

void TaskService::setOnTaskChanged(TaskChangeCallback callback) {
    m_onTaskChanged = callback;
}

// JSON Serialization Helpers
static std::string escapeJson(const std::string& s) {
    std::ostringstream o;
    for (char c : s) {
        switch (c) {
            case '"':  o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\b': o << "\\b";  break;
            case '\f': o << "\\f";  break;
            case '\n': o << "\\n";  break;
            case '\r': o << "\\r";  break;
            case '\t': o << "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) <= 0x1f) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    o << buf;
                } else {
                    o << c;
                }
        }
    }
    return o.str();
}

static std::string unescapeJson(const std::string& s) {
    std::string res;
    res.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char next = s[++i];
            switch (next) {
                case '"':  res += '"';  break;
                case '\\': res += '\\'; break;
                case '/':  res += '/';  break;
                case 'b':  res += '\b'; break;
                case 'f':  res += '\f'; break;
                case 'n':  res += '\n'; break;
                case 'r':  res += '\r'; break;
                case 't':  res += '\t'; break;
                default:   res += next; break;
            }
        } else {
            res += s[i];
        }
    }
    return res;
}

std::string TaskService::taskToJson(const Task& task) {
    std::ostringstream oss;
    oss << "{"
        << "\"id\":\"" << escapeJson(task.id) << "\","
        << "\"title\":\"" << escapeJson(task.title) << "\","
        << "\"description\":\"" << escapeJson(task.description) << "\","
        << "\"done\":" << (task.done ? "true" : "false") << ","
        << "\"isDeleted\":" << (task.isDeleted ? "true" : "false") << ","
        << "\"createdAt\":" << task.createdAt << ","
        << "\"updatedAt\":" << task.updatedAt << ","
        << "\"originNodeId\":\"" << escapeJson(task.originNodeId) << "\""
        << "}";
    return oss.str();
}

std::string TaskService::tasksToJson(const std::vector<Task>& tasks) {
    std::ostringstream oss;
    oss << "[\n";
    for (size_t i = 0; i < tasks.size(); ++i) {
        oss << "  " << taskToJson(tasks[i]);
        if (i + 1 < tasks.size()) {
            oss << ",";
        }
        oss << "\n";
    }
    oss << "]\n";
    return oss.str();
}

static std::string extractField(const std::string& src, const std::string& key) {
    std::string searchKey = "\"" + key + "\"";
    size_t keyPos = src.find(searchKey);
    size_t keyLen = searchKey.length();
    if (keyPos == std::string::npos) {
        keyPos = src.find(key);
        keyLen = key.length();
        if (keyPos == std::string::npos) return "";
    }

    size_t colonPos = src.find(':', keyPos + keyLen);
    if (colonPos == std::string::npos) return "";

    size_t quoteStart = src.find('"', colonPos + 1);
    if (quoteStart == std::string::npos) {
        size_t valStart = src.find_first_not_of(" \t\r\n", colonPos + 1);
        if (valStart == std::string::npos) return "";
        size_t valEnd = src.find_first_of(",}\r\n", valStart);
        if (valEnd == std::string::npos) valEnd = src.size();
        std::string rawVal = src.substr(valStart, valEnd - valStart);
        while (!rawVal.empty() && (rawVal.back() == ' ' || rawVal.back() == '\r' || rawVal.back() == '\t')) {
            rawVal.pop_back();
        }
        return rawVal;
    }

    size_t quoteEnd = quoteStart + 1;
    while (quoteEnd < src.size()) {
        if (src[quoteEnd] == '"' && src[quoteEnd - 1] != '\\') break;
        quoteEnd++;
    }

    if (quoteEnd >= src.size()) return "";
    return src.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
}

bool TaskService::parseTaskFromJson(const std::string& json, Task& outTask) {
    outTask.id = unescapeJson(extractField(json, "id"));
    outTask.title = unescapeJson(extractField(json, "title"));
    outTask.description = unescapeJson(extractField(json, "description"));
    outTask.originNodeId = unescapeJson(extractField(json, "originNodeId"));

    std::string doneStr = extractField(json, "done");
    outTask.done = (doneStr == "true" || doneStr == "1");

    std::string delStr = extractField(json, "isDeleted");
    outTask.isDeleted = (delStr == "true" || delStr == "1");

    std::string createdStr = extractField(json, "createdAt");
    if (!createdStr.empty()) {
        try { outTask.createdAt = std::stoll(createdStr); } catch (...) { outTask.createdAt = 0; }
    }

    std::string updatedStr = extractField(json, "updatedAt");
    if (!updatedStr.empty()) {
        try { outTask.updatedAt = std::stoll(updatedStr); } catch (...) { outTask.updatedAt = 0; }
    }

    return !outTask.title.empty() || !outTask.id.empty();
}

std::vector<Task> TaskService::parseTaskListFromJson(const std::string& json) {
    std::vector<Task> tasks;
    size_t pos = 0;

    while (pos < json.size()) {
        size_t startObj = json.find('{', pos);
        if (startObj == std::string::npos) break;

        size_t endObj = std::string::npos;
        bool inQuotes = false;
        int depth = 0;
        for (size_t i = startObj; i < json.size(); ++i) {
            if (json[i] == '"' && (i == 0 || json[i - 1] != '\\')) {
                inQuotes = !inQuotes;
            } else if (!inQuotes) {
                if (json[i] == '{') depth++;
                else if (json[i] == '}') {
                    depth--;
                    if (depth == 0) {
                        endObj = i;
                        break;
                    }
                }
            }
        }

        if (endObj == std::string::npos) break;
        std::string objStr = json.substr(startObj, endObj - startObj + 1);

        Task t;
        if (parseTaskFromJson(objStr, t)) {
            tasks.push_back(t);
        }

        pos = endObj + 1;
    }

    return tasks;
}
