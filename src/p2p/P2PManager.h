#ifndef P2P_MANAGER_H
#define P2P_MANAGER_H

#include "PeerNode.h"
#include "../service/TaskService.h"
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

class P2PManager {
public:
    P2PManager(std::shared_ptr<TaskService> taskService,
               const std::string& nodeId,
               int httpPort,
               int p2pPort = 9000,
               int udpBroadcastPort = 8888);
    ~P2PManager();

    bool start();
    void stop();

    // Trigger explicit sync with all discovered peers
    int syncWithAllPeers();
    bool syncWithPeer(const PeerNode& peer);

    // Push local update to all discovered peers
    void pushTaskToPeers(const Task& task);

    // Get list of active peers
    std::vector<PeerNode> getActivePeers();
    std::string getNodeId() const { return m_nodeId; }
    int getP2pPort() const { return m_p2pPort; }
    int getUdpPort() const { return m_udpBroadcastPort; }

private:
    std::shared_ptr<TaskService> m_taskService;
    std::string m_nodeId;
    std::string m_nodeName;
    int m_httpPort;
    int m_p2pPort;
    int m_udpBroadcastPort;

    std::atomic<bool> m_running;
    std::vector<PeerNode> m_peers;
    std::mutex m_peersMutex;

    SOCKET m_udpSocket;
    SOCKET m_tcpListenSocket;

    std::thread m_udpBroadcastThread;
    std::thread m_udpListenThread;
    std::thread m_tcpListenThread;

    // UDP Broadcast Discovery
    void runUdpBroadcaster();
    void runUdpListener();

    // TCP P2P Sync Server
    void runTcpListener();
    void handleTcpPeerConnection(SOCKET peerSocket);

    // Helpers
    void addOrUpdatePeer(const PeerNode& peer);
    void cleanStalePeers();
    std::string sendTcpMessage(const std::string& ip, int port, const std::string& message);
};

#endif // P2P_MANAGER_H
