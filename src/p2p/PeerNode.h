#ifndef PEER_NODE_H
#define PEER_NODE_H

#include <string>

struct PeerNode {
    std::string nodeId;
    std::string ip;
    int httpPort;
    int p2pPort;
    std::string nodeName;
    long long lastSeen; // Unix timestamp in seconds

    PeerNode() : httpPort(0), p2pPort(0), lastSeen(0) {}
};

#endif // PEER_NODE_H
