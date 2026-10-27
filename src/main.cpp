#include "repository/SqliteTaskRepository.h"
#include "service/TaskService.h"
#include "p2p/P2PManager.h"
#include "server/HttpServer.h"
#include <iostream>
#include <sstream>
#include <windows.h>
#include <fstream>
#include <chrono>
#include <random>

static HttpServer* g_httpServer = nullptr;
static P2PManager* g_p2pManager = nullptr;

BOOL WINAPI ConsoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT || signal == CTRL_BREAK_EVENT) {
        std::cout << "\n[Main] Shutdown signal received. Stopping P2P and HTTP server..." << std::endl;
        if (g_httpServer) g_httpServer->stop();
        if (g_p2pManager) g_p2pManager->stop();
        return TRUE;
    }
    return FALSE;
}

std::string getOrCreateNodeId() {
    std::ifstream file(".node_id");
    if (file.is_open()) {
        std::string id;
        file >> id;
        if (!id.empty()) return id;
    }

    auto now = std::chrono::system_clock::now().time_since_epoch();
    long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    std::mt19937 rng(static_cast<unsigned int>(ms));
    std::uniform_int_distribution<int> dist(1000, 9999);

    std::ostringstream ss;
    ss << "node_" << (ms % 1000000) << "_" << dist(rng);
    std::string newId = ss.str();

    std::ofstream outFile(".node_id");
    if (outFile.is_open()) {
        outFile << newId;
    }
    return newId;
}

int main(int argc, char* argv[]) {
    // UTF-8 console output for Windows
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // Initialize Winsock for the process
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[Main] WSAStartup failed: " << WSAGetLastError() << std::endl;
        return 1;
    }

    // Default configuration
    int httpPort = 8080;
    int p2pPort = 9000;
    int udpPort = 8888;
    std::string dbPath = "tasks.db";

    if (argc > 1) {
        try { httpPort = std::stoi(argv[1]); } catch (...) {}
    }
    if (argc > 2) {
        try { p2pPort = std::stoi(argv[2]); } catch (...) {}
    }
    if (argc > 3) {
        try { udpPort = std::stoi(argv[3]); } catch (...) {}
    }
    if (argc > 4) {
        dbPath = argv[4];
    } else if (httpPort != 8080) {
        // Automatic separation for testing multiple instances on 1 PC
        dbPath = "tasks_" + std::to_string(httpPort) + ".db";
    }

    std::string nodeId = getOrCreateNodeId() + "_" + std::to_string(httpPort);

    std::cout << "=====================================================" << std::endl;
    std::cout << "      TASKMASTER V4: P2P DECENTRALIZED ARCHITECTURE   " << std::endl;
    std::cout << "=====================================================" << std::endl;
    std::cout << " Local Node ID:        " << nodeId << std::endl;
    std::cout << " HTTP Web Port:        " << httpPort << std::endl;
    std::cout << " TCP P2P Sync Port:    " << p2pPort << std::endl;
    std::cout << " UDP Discovery Port:   " << udpPort << std::endl;
    std::cout << " SQLite Database:      " << dbPath << std::endl;
    std::cout << "=====================================================" << std::endl;

    // 1. Data Layer: SQLite Repository with migration from tasks.json
    auto repository = std::make_shared<SqliteTaskRepository>(dbPath, "tasks.json");
    if (!repository->initialize()) {
        std::cerr << "[Main] Failed to initialize SQLite database repository!" << std::endl;
        return 1;
    }

    // 2. Business Layer: TaskService with LWW Conflict Resolution
    auto taskService = std::make_shared<TaskService>(repository, nodeId);

    // 3. Network P2P Layer: UDP Discovery + TCP Direct Sync
    auto p2pManager = std::make_shared<P2PManager>(taskService, nodeId, httpPort, p2pPort, udpPort);
    g_p2pManager = p2pManager.get();

    if (!p2pManager->start()) {
        std::cerr << "[Main] Warning: Failed to start P2P Manager (UDP/TCP ports may be in use)." << std::endl;
    }

    // 4. Presentation Server: HTTP Server
    HttpServer httpServer(taskService, p2pManager, httpPort, "web");
    g_httpServer = &httpServer;

    SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    if (!httpServer.start()) {
        std::cerr << "[Main] Failed to start HTTP Server on port " << httpPort << std::endl;
        p2pManager->stop();
        return 1;
    }

    p2pManager->stop();
    WSACleanup();
    return 0;
}
