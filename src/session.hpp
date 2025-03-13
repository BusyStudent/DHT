#pragma once

#include <ilias/sync.hpp>
#include <ilias/net.hpp>
#include <functional>
#include <chrono>
#include <vector>
#include <set>
#include <map>
#include "route.hpp"
#include "krpc.hpp"
#include "net.hpp"

class DhtSession {
public:
    DhtSession(IoContext &ctxt, const NodeId &id, const IPEndpoint &listen);
    ~DhtSession();

    /**
     * @brief Execute the session, doing the bootstrap and 
     * 
     * @return Task<void> 
     */
    auto run() -> Task<void>;


    /**
     * @brief Try to find the node by target
     * 
     * @param target 
     * @return IoTask<std::vector<NodeEndpoint> > (The nodes found, max is KBUCKET_SIZE, sorted by distance) 
     */
    auto findNode(const NodeId &target) -> 
        IoTask<std::vector<NodeEndpoint> >;
private:
    struct FindNodeEnv {
        std::set<NodeEndpoint> visited;
        std::optional<NodeId> closest; // The closest node to the target
    };

    /**
     * @brief The incoming query
     * 
     * @param message 
     * @param from 
     * @return IoTask<void> 
     */
    auto onQuery(const BenObject &message, const IPEndpoint &from) -> IoTask<void>;

    /**
     * @brief Send a krpc to the remote, waiting for the reply
     * 
     * @param peer 
     * @param message 
     * @return IoTask<std::pair<BenObject, IPEndpoint> > 
     */
    auto sendKrpc(const BenObject &message, const IPEndpoint &endpoint) -> 
        IoTask<std::pair<BenObject, IPEndpoint> >;

    /**
     * @brief Try to find the node by target, using the endpoint
     * 
     * @param target 
     * @param endpoint 
     * @return IoTask<std::vector<NodeEndpoint> > 
     */
    auto findNode(const NodeId &target, const IPEndpoint &endpoint) -> 
        IoTask<std::vector<NodeEndpoint> >;

    /**
     * @brief Try to find the node by target, using the endpoint
     * 
     * @param target 
     * @param endpoint 
     * @param depth The current search depth
     * @param visited The visited nodes
     * @return IoTask<std::vector<NodeEndpoint> > 
     */
    auto findNodeImpl(const NodeId &target, const IPEndpoint &endpoint, size_t depth, FindNodeEnv &env) -> 
        IoTask<std::vector<NodeEndpoint> >;

    /**
     * @brief Init the session, bootstrap the node
     * 
     * @param nodeIp 
     * @return IoTask<void> 
     */
    auto bootstrap(const IPEndpoint &nodeIp) -> IoTask<void>;

    /**
     * @brief Try to ping a node by ip
     * 
     * @param nodeIp 
     * @return IoTask<void> 
     */
    auto ping(const IPEndpoint &nodeIp) -> IoTask<void>;

    /**
     * @brief Process the input from the socket
     * 
     * @return Task<void> 
     */
    auto processInput() -> Task<void>;

    /**
     * @brief Allocate a transaction id
     * 
     * @return std::string 
     */
    auto allocateTransactionId() -> std::string;

    IoContext &mCtxt;
    TaskScope  mScope;
    UdpClient  mClient;
    IPEndpoint mEndpoint;
    NodeId     mId;
    RoutingTable mRoutingTable;
    std::chrono::milliseconds mTimeout = std::chrono::seconds(2);

    std::map<
        std::string, 
        oneshot::Sender<std::pair<BenObject, IPEndpoint> > 
    > mPendingQueries; //< The pending queries, we sent, waiting for reply
    uint16_t mTransactionId = 0; //< The transaction id
};