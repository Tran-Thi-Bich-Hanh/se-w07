#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "TaskStorage.h"
#include <string>
#include <map>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

struct HttpRequest {
    std::string method;
    std::string path;
    std::string query;
    std::map<std::string, std::string> headers;
    std::string body;
};

struct HttpResponse {
    int statusCode = 200;
    std::string statusText = "OK";
    std::string contentType = "text/plain; charset=utf-8";
    std::string body;
};

class HttpServer {
public:
    HttpServer(int port = 8080, const std::string& webRoot = "web");
    ~HttpServer();

    bool start();
    void stop();

private:
    int m_port;
    std::string m_webRoot;
    SOCKET m_listenSocket;
    bool m_running;
    TaskStorage m_storage;

    bool initWinsock();
    void cleanupWinsock();
    void handleClient(SOCKET clientSocket);
    
    HttpRequest parseRequest(const std::string& rawRequest);
    HttpResponse routeRequest(const HttpRequest& req);
    
    HttpResponse handleStaticFile(const std::string& relativePath);
    HttpResponse handleApiTasks(const HttpRequest& req);

    std::string getMimeType(const std::string& path);
    std::string readFileContent(const std::string& fullPath);
};

#endif // HTTP_SERVER_H
