#include "HttpServer.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>

#pragma comment(lib, "ws2_32.lib")

HttpServer::HttpServer(int port, const std::string& webRoot)
    : m_port(port), m_webRoot(webRoot), m_listenSocket(INVALID_SOCKET), m_running(false), m_storage("tasks.json") {
}

HttpServer::~HttpServer() {
    stop();
}

bool HttpServer::initWinsock() {
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "[HttpServer] WSAStartup failed: " << result << std::endl;
        return false;
    }
    return true;
}

void HttpServer::cleanupWinsock() {
    WSACleanup();
}

bool HttpServer::start() {
    if (!initWinsock()) return false;

    m_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listenSocket == INVALID_SOCKET) {
        std::cerr << "[HttpServer] socket creation failed: " << WSAGetLastError() << std::endl;
        cleanupWinsock();
        return false;
    }

    // Set socket to allow address reuse
    int opt = 1;
    setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(static_cast<u_short>(m_port));

    if (bind(m_listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "[HttpServer] bind failed: " << WSAGetLastError() << std::endl;
        closesocket(m_listenSocket);
        cleanupWinsock();
        return false;
    }

    if (listen(m_listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "[HttpServer] listen failed: " << WSAGetLastError() << std::endl;
        closesocket(m_listenSocket);
        cleanupWinsock();
        return false;
    }

    m_running = true;
    std::cout << "=====================================================" << std::endl;
    std::cout << " [TaskManager V1 Local] HTTP Server is running!" << std::endl;
    std::cout << " Local Web UI: http://localhost:" << m_port << std::endl;
    std::cout << " API Tasks:    http://localhost:" << m_port << "/api/tasks" << std::endl;
    std::cout << " Storage:      tasks.json" << std::endl;
    std::cout << " Press Ctrl+C in console to stop server." << std::endl;
    std::cout << "=====================================================" << std::endl;

    while (m_running) {
        sockaddr_in clientAddr;
        int clientAddrLen = sizeof(clientAddr);
        SOCKET clientSocket = accept(m_listenSocket, (sockaddr*)&clientAddr, &clientAddrLen);

        if (clientSocket == INVALID_SOCKET) {
            if (m_running) {
                std::cerr << "[HttpServer] accept failed: " << WSAGetLastError() << std::endl;
            }
            break;
        }

        handleClient(clientSocket);
    }

    return true;
}

void HttpServer::stop() {
    if (m_running) {
        m_running = false;
        if (m_listenSocket != INVALID_SOCKET) {
            closesocket(m_listenSocket);
            m_listenSocket = INVALID_SOCKET;
        }
        cleanupWinsock();
        std::cout << "[HttpServer] Server stopped." << std::endl;
    }
}

void HttpServer::handleClient(SOCKET clientSocket) {
    char buffer[4096];
    std::string rawData;
    int bytesReceived = 0;

    // Read initial request chunk
    bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    if (bytesReceived > 0) {
        buffer[bytesReceived] = '\0';
        rawData.append(buffer, bytesReceived);

        // Find header-body separator
        size_t headerEnd = rawData.find("\r\n\r\n");
        size_t delimLen = 4;
        if (headerEnd == std::string::npos) {
            headerEnd = rawData.find("\n\n");
            delimLen = 2;
        }

        if (headerEnd != std::string::npos) {
            // Check Content-Length for complete body reading
            std::string headerPart = rawData.substr(0, headerEnd);
            size_t clPos = headerPart.find("Content-Length:");
            if (clPos == std::string::npos) {
                clPos = headerPart.find("content-length:");
            }

            if (clPos != std::string::npos) {
                size_t valStart = headerPart.find_first_not_of(" \t", clPos + 15);
                size_t lineEnd = headerPart.find_first_of("\r\n", valStart);
                if (lineEnd == std::string::npos) lineEnd = headerPart.length();
                try {
                    int contentLength = std::stoi(headerPart.substr(valStart, lineEnd - valStart));
                    size_t currentBodyLen = rawData.length() - (headerEnd + delimLen);
                    while (static_cast<int>(currentBodyLen) < contentLength) {
                        int extra = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
                        if (extra <= 0) break;
                        buffer[extra] = '\0';
                        rawData.append(buffer, extra);
                        currentBodyLen += extra;
                    }
                } catch (...) {
                    // Ignore parse error
                }
            }
        }

        HttpRequest req = parseRequest(rawData);
        HttpResponse res = routeRequest(req);

        std::ostringstream responseStream;
        responseStream << "HTTP/1.1 " << res.statusCode << " " << res.statusText << "\r\n";
        responseStream << "Content-Type: " << res.contentType << "\r\n";
        responseStream << "Content-Length: " << res.body.size() << "\r\n";
        responseStream << "Access-Control-Allow-Origin: *\r\n";
        responseStream << "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n";
        responseStream << "Access-Control-Allow-Headers: Content-Type\r\n";
        responseStream << "Connection: close\r\n\r\n";
        responseStream << res.body;

        std::string responseStr = responseStream.str();
        send(clientSocket, responseStr.c_str(), static_cast<int>(responseStr.size()), 0);
    }

    closesocket(clientSocket);
}

HttpRequest HttpServer::parseRequest(const std::string& rawRequest) {
    HttpRequest req;
    std::istringstream stream(rawRequest);
    std::string line;

    if (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream lineStream(line);
        std::string fullUrl;
        lineStream >> req.method >> fullUrl;

        size_t questionPos = fullUrl.find('?');
        if (questionPos != std::string::npos) {
            req.path = fullUrl.substr(0, questionPos);
            req.query = fullUrl.substr(questionPos + 1);
        } else {
            req.path = fullUrl;
        }
    }

    while (std::getline(stream, line) && line != "\r" && !line.empty()) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string headerName = line.substr(0, colon);
            std::string headerValue = line.substr(colon + 1);
            while (!headerValue.empty() && (headerValue.front() == ' ' || headerValue.front() == '\t')) {
                headerValue.erase(headerValue.begin());
            }
            req.headers[headerName] = headerValue;
        }
    }

    size_t bodyPos = rawRequest.find("\r\n\r\n");
    if (bodyPos != std::string::npos) {
        req.body = rawRequest.substr(bodyPos + 4);
    } else {
        bodyPos = rawRequest.find("\n\n");
        if (bodyPos != std::string::npos) {
            req.body = rawRequest.substr(bodyPos + 2);
        }
    }

    return req;
}

HttpResponse HttpServer::routeRequest(const HttpRequest& req) {
    if (req.method == "OPTIONS") {
        HttpResponse res;
        res.statusCode = 204;
        res.statusText = "No Content";
        return res;
    }

    // API Routes
    if (req.path.rfind("/api/tasks", 0) == 0) {
        return handleApiTasks(req);
    }

    // Static Web UI Routes
    std::string filePath = req.path;
    if (filePath == "/" || filePath.empty()) {
        filePath = "/index.html";
    }

    return handleStaticFile(filePath);
}

HttpResponse HttpServer::handleStaticFile(const std::string& relativePath) {
    HttpResponse res;
    std::string cleanPath = relativePath;
    if (cleanPath.front() == '/') {
        cleanPath = cleanPath.substr(1);
    }

    std::string fullPath = m_webRoot + "/" + cleanPath;
    std::string content = readFileContent(fullPath);

    if (content.empty()) {
        res.statusCode = 404;
        res.statusText = "Not Found";
        res.contentType = "text/plain; charset=utf-8";
        res.body = "404 Not Found: " + cleanPath;
        return res;
    }

    res.statusCode = 200;
    res.statusText = "OK";
    res.contentType = getMimeType(cleanPath);
    res.body = content;
    return res;
}

HttpResponse HttpServer::handleApiTasks(const HttpRequest& req) {
    HttpResponse res;
    res.contentType = "application/json; charset=utf-8";

    if (req.method == "GET") {
        // Return all tasks
        auto tasks = m_storage.getAllTasks();
        res.statusCode = 200;
        res.body = TaskStorage::tasksToJson(tasks);
        return res;
    }

    if (req.method == "POST") {
        // Create new task
        std::string title, desc, id;
        bool done = false;
        if (TaskStorage::parseTaskFromJson(req.body, title, desc, done, id)) {
            if (title.empty()) {
                res.statusCode = 400;
                res.body = "{\"error\": \"Title cannot be empty\"}";
                return res;
            }
            Task created = m_storage.addTask(title, desc);
            res.statusCode = 201;
            res.statusText = "Created";
            res.body = TaskStorage::taskToJson(created);
            return res;
        } else {
            res.statusCode = 400;
            res.body = "{\"error\": \"Invalid JSON request body\"}";
            return res;
        }
    }

    if (req.method == "PUT") {
        // Update task status
        std::string title, desc, id;
        bool done = false;
        
        // Check query param if id not in body
        if (!req.query.empty() && req.query.find("id=") != std::string::npos) {
            size_t idStart = req.query.find("id=") + 3;
            size_t idEnd = req.query.find('&', idStart);
            id = req.query.substr(idStart, idEnd == std::string::npos ? std::string::npos : idEnd - idStart);
        }

        TaskStorage::parseTaskFromJson(req.body, title, desc, done, id);

        if (id.empty()) {
            res.statusCode = 400;
            res.body = "{\"error\": \"Task ID is required for update\"}";
            return res;
        }

        if (m_storage.updateTaskStatus(id, done)) {
            res.statusCode = 200;
            res.body = "{\"success\": true, \"id\": \"" + id + "\", \"done\": " + (done ? "true" : "false") + "}";
        } else {
            res.statusCode = 404;
            res.body = "{\"error\": \"Task not found\"}";
        }
        return res;
    }

    if (req.method == "DELETE") {
        std::string id;
        // Extract id from query: ?id=task_xxx
        if (!req.query.empty()) {
            size_t pos = req.query.find("id=");
            if (pos != std::string::npos) {
                size_t end = req.query.find('&', pos);
                id = req.query.substr(pos + 3, end == std::string::npos ? std::string::npos : end - (pos + 3));
            }
        }

        // If not in query, check body
        if (id.empty() && !req.body.empty()) {
            std::string t, d;
            bool dn;
            TaskStorage::parseTaskFromJson(req.body, t, d, dn, id);
        }

        if (id.empty()) {
            res.statusCode = 400;
            res.body = "{\"error\": \"Task ID is required for deletion\"}";
            return res;
        }

        if (m_storage.deleteTask(id)) {
            res.statusCode = 200;
            res.body = "{\"success\": true, \"deletedId\": \"" + id + "\"}";
        } else {
            res.statusCode = 404;
            res.body = "{\"error\": \"Task not found\"}";
        }
        return res;
    }

    res.statusCode = 405;
    res.statusText = "Method Not Allowed";
    res.body = "{\"error\": \"Method not allowed\"}";
    return res;
}

std::string HttpServer::getMimeType(const std::string& path) {
    size_t dotPos = path.rfind('.');
    if (dotPos == std::string::npos) return "text/plain; charset=utf-8";

    std::string ext = path.substr(dotPos);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".js") return "application/javascript; charset=utf-8";
    if (ext == ".json") return "application/json; charset=utf-8";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".ico") return "image/x-icon";
    return "text/plain; charset=utf-8";
}

std::string HttpServer::readFileContent(const std::string& fullPath) {
    std::ifstream file(fullPath, std::ios::binary);
    if (!file.is_open()) return "";
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}
