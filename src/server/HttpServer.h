#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "../service/TaskService.h"
#include "../p2p/P2PManager.h"
#include <string>
#include <map>
#include <memory>
#include <atomic>

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
    HttpServer(std::shared_ptr<TaskService> taskService,
               std::shared_ptr<P2PManager> p2pManager,
               int port = 8080,
               const std::string& webRoot = "web");
    ~HttpServer();

    bool start();
    void stop();

private:
    std::shared_ptr<TaskService> m_taskService;
    std::shared_ptr<P2PManager> m_p2pManager;
    int m_port;
    std::string m_webRoot;
    SOCKET m_listenSocket;
    std::atomic<bool> m_running;

    bool initWinsock();
    void cleanupWinsock();
    void handleClient(SOCKET clientSocket);
    
    HttpRequest parseRequest(const std::string& rawRequest);
    HttpResponse routeRequest(const HttpRequest& req);
    
    HttpResponse handleStaticFile(const std::string& relativePath);
    HttpResponse handleApiTasks(const HttpRequest& req);
    HttpResponse handleApiP2p(const HttpRequest& req);

    std::string getMimeType(const std::string& path);
    std::string readFileContent(const std::string& fullPath);
};

#endif // HTTP_SERVER_H
