#include "P2PManager.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <algorithm>

P2PManager::P2PManager(std::shared_ptr<TaskService> taskService,
                       const std::string& nodeId,
                       int httpPort,
                       int p2pPort,
                       int udpBroadcastPort)
    : m_taskService(taskService)
    , m_nodeId(nodeId)
    , m_httpPort(httpPort)
    , m_p2pPort(p2pPort)
    , m_udpBroadcastPort(udpBroadcastPort)
    , m_running(false)
    , m_udpSocket(INVALID_SOCKET)
    , m_tcpListenSocket(INVALID_SOCKET) {

    char hostname[256] = {0};
    gethostname(hostname, sizeof(hostname));
    m_nodeName = std::string("Node-") + hostname;
}

P2PManager::~P2PManager() {
    stop();
}

bool P2PManager::start() {
    if (m_running) return true;
    m_running = true;

    // 1. Setup UDP Broadcast Discovery Socket
    m_udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_udpSocket == INVALID_SOCKET) {
        std::cerr << "[P2P] Failed to create UDP socket: " << WSAGetLastError() << std::endl;
        return false;
    }

    // Enable Broadcast
    int broadcastOpt = 1;
    setsockopt(m_udpSocket, SOL_SOCKET, SO_BROADCAST, (const char*)&broadcastOpt, sizeof(broadcastOpt));

    // Enable Address Reuse
    int reuseOpt = 1;
    setsockopt(m_udpSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuseOpt, sizeof(reuseOpt));

    // Set receive timeout (1s) so thread can exit cleanly
    DWORD timeoutMs = 1000;
    setsockopt(m_udpSocket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeoutMs, sizeof(timeoutMs));

    sockaddr_in bindAddr;
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_addr.s_addr = INADDR_ANY;
    bindAddr.sin_port = htons(static_cast<u_short>(m_udpBroadcastPort));

    if (bind(m_udpSocket, (sockaddr*)&bindAddr, sizeof(bindAddr)) == SOCKET_ERROR) {
        std::cerr << "[P2P] Failed to bind UDP broadcast port " << m_udpBroadcastPort << ": " << WSAGetLastError() << std::endl;
        closesocket(m_udpSocket);
        m_udpSocket = INVALID_SOCKET;
        return false;
    }

    // 2. Setup TCP P2P Listener Socket
    m_tcpListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_tcpListenSocket == INVALID_SOCKET) {
        std::cerr << "[P2P] Failed to create TCP socket: " << WSAGetLastError() << std::endl;
        closesocket(m_udpSocket);
        return false;
    }

    setsockopt(m_tcpListenSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuseOpt, sizeof(reuseOpt));

    sockaddr_in tcpAddr;
    tcpAddr.sin_family = AF_INET;
    tcpAddr.sin_addr.s_addr = INADDR_ANY;
    tcpAddr.sin_port = htons(static_cast<u_short>(m_p2pPort));

    if (bind(m_tcpListenSocket, (sockaddr*)&tcpAddr, sizeof(tcpAddr)) == SOCKET_ERROR) {
        std::cerr << "[P2P] Failed to bind TCP P2P port " << m_p2pPort << ": " << WSAGetLastError() << std::endl;
        closesocket(m_tcpListenSocket);
        closesocket(m_udpSocket);
        return false;
    }

    if (listen(m_tcpListenSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "[P2P] TCP Listen failed: " << WSAGetLastError() << std::endl;
        closesocket(m_tcpListenSocket);
        closesocket(m_udpSocket);
        return false;
    }

    // Connect TaskService change listener to push local updates to peers
    m_taskService->setOnTaskChanged([this](const Task& changedTask) {
        this->pushTaskToPeers(changedTask);
    });

    // 3. Start background threads
    m_udpBroadcastThread = std::thread(&P2PManager::runUdpBroadcaster, this);
    m_udpListenThread = std::thread(&P2PManager::runUdpListener, this);
    m_tcpListenThread = std::thread(&P2PManager::runTcpListener, this);

    std::cout << "[P2P] P2P Module started on Node [" << m_nodeId << "]" << std::endl;
    std::cout << "      UDP Broadcast Port: " << m_udpBroadcastPort << std::endl;
    std::cout << "      TCP P2P Sync Port:  " << m_p2pPort << std::endl;

    return true;
}

void P2PManager::stop() {
    if (!m_running) return;
    m_running = false;

    if (m_udpSocket != INVALID_SOCKET) {
        closesocket(m_udpSocket);
        m_udpSocket = INVALID_SOCKET;
    }

    if (m_tcpListenSocket != INVALID_SOCKET) {
        closesocket(m_tcpListenSocket);
        m_tcpListenSocket = INVALID_SOCKET;
    }

    if (m_udpBroadcastThread.joinable()) m_udpBroadcastThread.join();
    if (m_udpListenThread.joinable()) m_udpListenThread.join();
    if (m_tcpListenThread.joinable()) m_tcpListenThread.join();

    std::cout << "[P2P] P2P Module stopped." << std::endl;
}

void P2PManager::runUdpBroadcaster() {
    sockaddr_in broadcastAddr;
    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_addr.s_addr = inet_addr("255.255.255.255");
    broadcastAddr.sin_port = htons(static_cast<u_short>(m_udpBroadcastPort));

    while (m_running) {
        // Construct broadcast payload
        std::ostringstream ss;
        ss << "DISCOVER|" << m_nodeId << "|" << m_httpPort << "|" << m_p2pPort << "|" << m_nodeName;
        std::string msg = ss.str();

        sendto(m_udpSocket, msg.c_str(), static_cast<int>(msg.size()), 0,
               (sockaddr*)&broadcastAddr, sizeof(broadcastAddr));

        cleanStalePeers();

        // Sleep 3 seconds
        for (int i = 0; i < 30 && m_running; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void P2PManager::runUdpListener() {
    char buffer[1024];

    while (m_running) {
        sockaddr_in senderAddr;
        int senderLen = sizeof(senderAddr);

        int bytesReceived = recvfrom(m_udpSocket, buffer, sizeof(buffer) - 1, 0,
                                     (sockaddr*)&senderAddr, &senderLen);

        if (bytesReceived > 0) {
            buffer[bytesReceived] = '\0';
            std::string payload(buffer, bytesReceived);

            if (payload.rfind("DISCOVER|", 0) == 0) {
                std::istringstream iss(payload);
                std::string token, prefix, remoteNodeId, httpPortStr, p2pPortStr, remoteName;

                std::getline(iss, prefix, '|');
                std::getline(iss, remoteNodeId, '|');
                std::getline(iss, httpPortStr, '|');
                std::getline(iss, p2pPortStr, '|');
                std::getline(iss, remoteName, '|');

                // Don't register ourselves
                if (remoteNodeId != m_nodeId && !remoteNodeId.empty()) {
                    char senderIp[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &(senderAddr.sin_addr), senderIp, INET_ADDRSTRLEN);

                    PeerNode peer;
                    peer.nodeId = remoteNodeId;
                    peer.ip = senderIp;
                    // If received from 127.0.0.1 or local machine
                    if (peer.ip == "0.0.0.0") peer.ip = "127.0.0.1";
                    try { peer.httpPort = std::stoi(httpPortStr); } catch (...) { peer.httpPort = 8080; }
                    try { peer.p2pPort = std::stoi(p2pPortStr); } catch (...) { peer.p2pPort = 9000; }
                    peer.nodeName = remoteName.empty() ? ("Peer-" + remoteNodeId.substr(0, 6)) : remoteName;
                    peer.lastSeen = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();

                    addOrUpdatePeer(peer);
                }
            }
        }
    }
}

void P2PManager::addOrUpdatePeer(const PeerNode& peer) {
    std::lock_guard<std::mutex> lock(m_peersMutex);
    bool found = false;
    for (auto& p : m_peers) {
        if (p.nodeId == peer.nodeId) {
            p.ip = peer.ip;
            p.httpPort = peer.httpPort;
            p.p2pPort = peer.p2pPort;
            p.nodeName = peer.nodeName;
            p.lastSeen = peer.lastSeen;
            found = true;
            break;
        }
    }

    if (!found) {
        m_peers.push_back(peer);
        std::cout << "\n[P2P] Discovered new Peer: " << peer.nodeName 
                  << " [" << peer.nodeId << "] at " << peer.ip << ":" << peer.p2pPort << std::endl;
        
        // Auto-trigger background sync with newly discovered peer
        std::thread([this, peer]() {
            this->syncWithPeer(peer);
        }).detach();
    }
}

void P2PManager::cleanStalePeers() {
    std::lock_guard<std::mutex> lock(m_peersMutex);
    long long now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // Consider peer stale if no broadcast received for 10 seconds
    m_peers.erase(std::remove_if(m_peers.begin(), m_peers.end(), [now](const PeerNode& p) {
        return (now - p.lastSeen) > 10;
    }), m_peers.end());
}

std::vector<PeerNode> P2PManager::getActivePeers() {
    std::lock_guard<std::mutex> lock(m_peersMutex);
    return m_peers;
}

// TCP P2P Listener & Sync Server
void P2PManager::runTcpListener() {
    while (m_running) {
        sockaddr_in clientAddr;
        int clientAddrLen = sizeof(clientAddr);
        SOCKET peerSocket = accept(m_tcpListenSocket, (sockaddr*)&clientAddr, &clientAddrLen);

        if (peerSocket == INVALID_SOCKET) {
            if (m_running) {
                // error or closed
            }
            break;
        }

        // Handle each peer connection in a separate detached thread for concurrency
        std::thread(&P2PManager::handleTcpPeerConnection, this, peerSocket).detach();
    }
}

static std::string readLineFromSocket(SOCKET s) {
    std::string line;
    char ch;
    while (recv(s, &ch, 1, 0) > 0) {
        if (ch == '\n') break;
        if (ch != '\r') line += ch;
    }
    return line;
}

static std::string readExactBytes(SOCKET s, size_t length) {
    std::string data;
    data.reserve(length);
    char buf[2048];
    size_t remaining = length;
    while (remaining > 0) {
        int chunkSize = static_cast<int>(std::min(remaining, sizeof(buf)));
        int recvd = recv(s, buf, chunkSize, 0);
        if (recvd <= 0) break;
        data.append(buf, recvd);
        remaining -= recvd;
    }
    return data;
}

void P2PManager::handleTcpPeerConnection(SOCKET peerSocket) {
    std::string cmd = readLineFromSocket(peerSocket);

    if (cmd == "REQ_SYNC") {
        // Peer is sending its tasks and asking for our tasks
        std::string lenStr = readLineFromSocket(peerSocket);
        size_t len = 0;
        try { len = std::stoul(lenStr); } catch (...) {}

        if (len > 0) {
            std::string peerTasksJson = readExactBytes(peerSocket, len);
            auto peerTasks = TaskService::parseTaskListFromJson(peerTasksJson);
            int merged = m_taskService->mergeIncomingTasks(peerTasks);
            std::cout << "[P2P] Received SYNC from peer, merged " << merged << " tasks via LWW." << std::endl;
        }

        // Send our local tasks back to peer
        auto localTasks = m_taskService->getAllTasks(true); // include tombstones for sync!
        std::string responseJson = TaskService::tasksToJson(localTasks);
        std::ostringstream oss;
        oss << "RESP_SYNC\n" << responseJson.size() << "\n" << responseJson;
        std::string resp = oss.str();
        send(peerSocket, resp.c_str(), static_cast<int>(resp.size()), 0);

    } else if (cmd == "PUSH_TASK") {
        // Peer is pushing a single newly created/updated/deleted task
        std::string lenStr = readLineFromSocket(peerSocket);
        size_t len = 0;
        try { len = std::stoul(lenStr); } catch (...) {}

        if (len > 0) {
            std::string taskJson = readExactBytes(peerSocket, len);
            Task incoming;
            if (TaskService::parseTaskFromJson(taskJson, incoming)) {
                bool updated = m_taskService->mergeIncomingTask(incoming);
                if (updated) {
                    std::cout << "[P2P] Applied PUSH_TASK update from peer via LWW: [" 
                              << incoming.title << "]" << std::endl;
                }
            }
        }
        std::string ack = "OK\n";
        send(peerSocket, ack.c_str(), static_cast<int>(ack.size()), 0);
    }

    closesocket(peerSocket);
}

bool P2PManager::syncWithPeer(const PeerNode& peer) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return false;

    // 2-second connection timeout
    DWORD timeout = 2000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

    sockaddr_in peerAddr;
    peerAddr.sin_family = AF_INET;
    peerAddr.sin_addr.s_addr = inet_addr(peer.ip.c_str());
    peerAddr.sin_port = htons(static_cast<u_short>(peer.p2pPort));

    if (connect(sock, (sockaddr*)&peerAddr, sizeof(peerAddr)) == SOCKET_ERROR) {
        closesocket(sock);
        return false;
    }

    // Send REQ_SYNC with our current tasks
    auto localTasks = m_taskService->getAllTasks(true);
    std::string tasksJson = TaskService::tasksToJson(localTasks);

    std::ostringstream oss;
    oss << "REQ_SYNC\n" << tasksJson.size() << "\n" << tasksJson;
    std::string payload = oss.str();
    send(sock, payload.c_str(), static_cast<int>(payload.size()), 0);

    // Read peer's response
    std::string respHeader = readLineFromSocket(sock);
    if (respHeader == "RESP_SYNC") {
        std::string lenStr = readLineFromSocket(sock);
        size_t len = 0;
        try { len = std::stoul(lenStr); } catch (...) {}
        if (len > 0) {
            std::string peerData = readExactBytes(sock, len);
            auto peerTasks = TaskService::parseTaskListFromJson(peerData);
            int merged = m_taskService->mergeIncomingTasks(peerTasks);
            std::cout << "[P2P] Completed sync with " << peer.nodeName << " (" << peer.ip << "): merged " << merged << " tasks." << std::endl;
        }
    }

    closesocket(sock);
    return true;
}

int P2PManager::syncWithAllPeers() {
    auto peers = getActivePeers();
    int successCount = 0;
    for (const auto& peer : peers) {
        if (syncWithPeer(peer)) {
            successCount++;
        }
    }
    return successCount;
}

void P2PManager::pushTaskToPeers(const Task& task) {
    auto peers = getActivePeers();
    if (peers.empty()) return;

    std::string taskJson = TaskService::taskToJson(task);
    std::ostringstream oss;
    oss << "PUSH_TASK\n" << taskJson.size() << "\n" << taskJson;
    std::string payload = oss.str();

    // Broadcast update to all peers in background thread
    std::thread([peers, payload]() {
        for (const auto& peer : peers) {
            SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (sock == INVALID_SOCKET) continue;

            DWORD timeout = 1500;
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

            sockaddr_in peerAddr;
            peerAddr.sin_family = AF_INET;
            peerAddr.sin_addr.s_addr = inet_addr(peer.ip.c_str());
            peerAddr.sin_port = htons(static_cast<u_short>(peer.p2pPort));

            if (connect(sock, (sockaddr*)&peerAddr, sizeof(peerAddr)) != SOCKET_ERROR) {
                send(sock, payload.c_str(), static_cast<int>(payload.size()), 0);
            }
            closesocket(sock);
        }
    }).detach();
}
