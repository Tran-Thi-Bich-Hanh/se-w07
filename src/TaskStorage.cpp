#include "TaskStorage.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>
#include <random>
#include <cctype>
#include <algorithm>

TaskStorage::TaskStorage(const std::string& filePath)
    : m_filePath(filePath) {
    load();
}

std::string TaskStorage::generateId() {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    
    static std::mt19937 rng(static_cast<unsigned int>(ms));
    std::uniform_int_distribution<int> dist(1000, 9999);
    
    std::ostringstream ss;
    ss << "task_" << ms << "_" << dist(rng);
    return ss.str();
}

std::string TaskStorage::escapeJson(const std::string& s) {
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

std::string TaskStorage::unescapeJson(const std::string& s) {
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

std::string TaskStorage::taskToJson(const Task& task) {
    std::ostringstream oss;
    oss << "{\n"
        << "  \"id\": \"" << escapeJson(task.id) << "\",\n"
        << "  \"title\": \"" << escapeJson(task.title) << "\",\n"
        << "  \"description\": \"" << escapeJson(task.description) << "\",\n"
        << "  \"done\": " << (task.done ? "true" : "false") << ",\n"
        << "  \"createdAt\": " << task.createdAt << ",\n"
        << "  \"updatedAt\": " << task.updatedAt << "\n"
        << "}";
    return oss.str();
}

std::string TaskStorage::tasksToJson(const std::vector<Task>& tasks) {
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

static std::string extractJsonStringField(const std::string& objStr, const std::string& key) {
    std::string searchKey = "\"" + key + "\"";
    size_t keyPos = objStr.find(searchKey);
    size_t keyLen = searchKey.length();
    if (keyPos == std::string::npos) {
        keyPos = objStr.find(key);
        keyLen = key.length();
        if (keyPos == std::string::npos) return "";
    }

    size_t colonPos = objStr.find(':', keyPos + keyLen);
    if (colonPos == std::string::npos) return "";

    size_t quoteStart = objStr.find('"', colonPos + 1);
    if (quoteStart == std::string::npos) {
        // Support unquoted string value
        size_t valStart = objStr.find_first_not_of(" \t\r\n", colonPos + 1);
        if (valStart == std::string::npos) return "";
        size_t valEnd = objStr.find_first_of(",}\r\n", valStart);
        if (valEnd == std::string::npos) valEnd = objStr.size();
        std::string rawVal = objStr.substr(valStart, valEnd - valStart);
        while (!rawVal.empty() && (rawVal.back() == ' ' || rawVal.back() == '\r' || rawVal.back() == '\t')) {
            rawVal.pop_back();
        }
        return rawVal;
    }

    size_t quoteEnd = quoteStart + 1;
    while (quoteEnd < objStr.size()) {
        if (objStr[quoteEnd] == '"' && objStr[quoteEnd - 1] != '\\') {
            break;
        }
        quoteEnd++;
    }

    if (quoteEnd >= objStr.size()) return "";
    return objStr.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
}

static long long extractJsonNumberField(const std::string& objStr, const std::string& key, long long defaultVal = 0) {
    std::string searchKey = "\"" + key + "\"";
    size_t keyPos = objStr.find(searchKey);
    if (keyPos == std::string::npos) return defaultVal;

    size_t colonPos = objStr.find(':', keyPos + searchKey.length());
    if (colonPos == std::string::npos) return defaultVal;

    size_t start = colonPos + 1;
    while (start < objStr.size() && (std::isspace(objStr[start]) || objStr[start] == ':')) {
        start++;
    }

    size_t end = start;
    while (end < objStr.size() && (std::isdigit(objStr[end]) || objStr[end] == '-')) {
        end++;
    }

    if (start == end) return defaultVal;
    try {
        return std::stoll(objStr.substr(start, end - start));
    } catch (...) {
        return defaultVal;
    }
}

static bool extractJsonBoolField(const std::string& objStr, const std::string& key, bool defaultVal = false) {
    std::string searchKey = "\"" + key + "\"";
    size_t keyPos = objStr.find(searchKey);
    if (keyPos == std::string::npos) return defaultVal;

    size_t colonPos = objStr.find(':', keyPos + searchKey.length());
    if (colonPos == std::string::npos) return defaultVal;

    size_t tPos = objStr.find("true", colonPos);
    size_t fPos = objStr.find("false", colonPos);
    size_t commaPos = objStr.find(',', colonPos);
    size_t bracePos = objStr.find('}', colonPos);
    size_t endPos = std::min(commaPos, bracePos);

    if (tPos != std::string::npos && (endPos == std::string::npos || tPos < endPos)) return true;
    if (fPos != std::string::npos && (endPos == std::string::npos || fPos < endPos)) return false;
    return defaultVal;
}

std::vector<Task> TaskStorage::parseJsonToTasks(const std::string& jsonStr) {
    std::vector<Task> tasks;
    size_t pos = 0;

    while (pos < jsonStr.size()) {
        size_t startObj = jsonStr.find('{', pos);
        if (startObj == std::string::npos) break;

        // Find closing bracket while honoring nested quotes
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
        t.id = unescapeJson(extractJsonStringField(objStr, "id"));
        t.title = unescapeJson(extractJsonStringField(objStr, "title"));
        t.description = unescapeJson(extractJsonStringField(objStr, "description"));
        t.done = extractJsonBoolField(objStr, "done", false);
        t.createdAt = extractJsonNumberField(objStr, "createdAt", 0);
        t.updatedAt = extractJsonNumberField(objStr, "updatedAt", t.createdAt);

        if (!t.id.empty() || !t.title.empty()) {
            tasks.push_back(t);
        }

        pos = endObj + 1;
    }

    return tasks;
}

bool TaskStorage::parseTaskFromJson(const std::string& json, std::string& outTitle, std::string& outDesc, bool& outDone, std::string& outId) {
    outId = unescapeJson(extractJsonStringField(json, "id"));
    outTitle = unescapeJson(extractJsonStringField(json, "title"));
    outDesc = unescapeJson(extractJsonStringField(json, "description"));
    outDone = extractJsonBoolField(json, "done", false);
    return !outTitle.empty() || !outId.empty();
}

bool TaskStorage::load() {
    std::ifstream file(m_filePath);
    if (!file.is_open()) {
        std::cout << "[TaskStorage] File " << m_filePath << " not found, will create on save." << std::endl;
        m_tasks.clear();
        return false;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    file.close();

    m_tasks = parseJsonToTasks(ss.str());
    std::cout << "[TaskStorage] Loaded " << m_tasks.size() << " tasks from " << m_filePath << std::endl;
    return true;
}

bool TaskStorage::save() {
    std::ofstream file(m_filePath, std::ios::trunc);
    if (!file.is_open()) {
        std::cerr << "[TaskStorage] Error: Failed to open " << m_filePath << " for writing!" << std::endl;
        return false;
    }

    file << tasksToJson(m_tasks);
    file.close();
    return true;
}

std::vector<Task> TaskStorage::getAllTasks() const {
    return m_tasks;
}

Task TaskStorage::addTask(const std::string& title, const std::string& description) {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    long long timestamp = std::chrono::duration_cast<std::chrono::seconds>(now).count();

    Task t;
    t.id = generateId();
    t.title = title;
    t.description = description;
    t.done = false;
    t.createdAt = timestamp;
    t.updatedAt = timestamp;

    m_tasks.push_back(t);
    save();
    return t;
}

bool TaskStorage::updateTaskStatus(const std::string& id, bool done) {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    long long timestamp = std::chrono::duration_cast<std::chrono::seconds>(now).count();

    for (auto& task : m_tasks) {
        if (task.id == id) {
            task.done = done;
            task.updatedAt = timestamp;
            save();
            return true;
        }
    }
    return false;
}

bool TaskStorage::deleteTask(const std::string& id) {
    auto it = std::remove_if(m_tasks.begin(), m_tasks.end(), [&id](const Task& t) {
        return t.id == id;
    });

    if (it != m_tasks.end()) {
        m_tasks.erase(it, m_tasks.end());
        save();
        return true;
    }
    return false;
}
