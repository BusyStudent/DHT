#pragma once

#include <ilias/sync.hpp>
#include <ilias/net.hpp>
#include <functional>
#include <random>
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
     * @brief Save the routing table to the file
     * 
     * @param file 
     */
    auto saveFile(const char *file) const -> void;

    /**
     * @brief Load the routing table fromt the file
     * 
     * @param file 
     */
    auto loadFile(const char *file) -> void;

    /**
     * @brief Try to find the node by target
     * 
     * @param target 
     * @return IoTask<std::vector<NodeEndpoint> > (The nodes found, max is KBUCKET_SIZE, sorted by distance) 
     */
    auto findNode(const NodeId &target) -> 
        IoTask<std::vector<NodeEndpoint> >;

    /**
     * @brief Try to ping a node by ip
     * 
     * @param nodeIp 
     * @return IoTask<NodeId> The nodeid of the pinged node
     */
    auto ping(const IPEndpoint &nodeIp) -> IoTask<NodeId>;

    /**
     * @brief Get the routing table
     * 
     * @return const RoutingTable& 
     */
    auto routingTable() const -> const RoutingTable &;

    /**
     * @brief Get the routing table
     * 
     * @return RoutingTable& 
     */
    auto routingTable() -> RoutingTable &;

    /**
     * @brief Get the peers that announced
     * 
     * @return const std::map<InfoHash, std::set<IPEndpoint> >& 
     */
    auto peers() const -> const std::map<InfoHash, std::set<IPEndpoint> > &;

    /**
     * @brief Set the callback triggered when a peer is announced
     * 
     * @param callback 
     */
    auto setOnAnouncePeer(std::function<void(const InfoHash &hash, const IPEndpoint &peer)> callback) -> void;

    /**
     * @brief Set the Skip Bootstrap object
     * 
     * @param skip Skip the bootstrap?
     */
    auto setSkipBootstrap(bool skip) -> void;

    /**
     * @brief Get the sample info hashes from the routing table
     * 
     * @param nodeIp The node ip to sample
     * @return IoTask<std::vector<InfoHash> >  The sampled info hashes
     */
    auto sampleInfoHashes(const IPEndpoint &nodeIp) -> IoTask<std::vector<InfoHash> >;
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
     * @param target The target node id we are looking for
     * @param id The id of the node we query for
     * @param endpoint The endpoint of the node we query for
     * @param depth The current search depth
     * @param visited The visited nodes
     * @return IoTask<std::vector<NodeEndpoint> > 
     */
    auto findNodeImpl(const NodeId &target, std::optional<NodeId> id, const IPEndpoint &endpoint, size_t depth, FindNodeEnv &env) -> 
        IoTask<std::vector<NodeEndpoint> >;

    /**
     * @brief Init the session, bootstrap the node
     * 
     * @param nodeIp 
     * @return IoTask<void> 
     */
    auto bootstrap(const IPEndpoint &nodeIp) -> IoTask<void>;

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

    /**
     * @brief A user thread, to cleanup the peers
     * 
     * @return Task<void> 
     */
    auto cleanupPeersThread() -> Task<void>;

    /**
     * @brief A user thread, to refresh the routing table
     * 
     * @return Task<void> 
     */
    auto refreshTableThread() -> Task<void>;

    /**
     * @brief A user thread, to do random search for expand the routing table
     * 
     * @return Task<void> 
     */
    auto randomSearchThread() -> Task<void>;

    IoContext &mCtxt;
    TaskScope  mScope;
    UdpClient  mClient;
    IPEndpoint mEndpoint;
    NodeId     mId;
    RoutingTable mRoutingTable;
    std::chrono::milliseconds mTimeout = std::chrono::seconds(2);
    std::chrono::milliseconds mRefreshInterval = std::chrono::minutes(5); // Refresh the routing table every 5 minute
    std::chrono::milliseconds mCleanupInterval = std::chrono::minutes(15); // Cleanup the peers every 15 minute
    std::chrono::milliseconds mRandomSearchInterval = std::chrono::minutes(10);
    std::mt19937 mRandom { std::random_device{}() };

    std::map<
        std::string, 
        oneshot::Sender<std::pair<BenObject, IPEndpoint> > 
    > mPendingQueries; //< The pending queries, we sent, waiting for reply
    uint16_t mTransactionId = 0; //< The transaction id

    std::map<
        InfoHash,
        std::set<IPEndpoint> //< Use set to avoid duplicate
    > mPeers; //< The peers they announced
    std::function<void(const InfoHash &hash, const IPEndpoint &peer)> mOnAnnouncePeer; //< The callback when we got a announce peer

    // Config
    bool mSkipBootstrap = false;
};

enum class KrpcError {
    BadReply,
    BadQuery,
    TargetNotFound, // The target node is not found
    RpcErrorMessage, // The per send error message
};

class KrpcErrorCategory final : public ErrorCategory {
public:
    auto name() const  -> std::string_view override {
        return "krpc";
    }

    auto message(int64_t ev) const -> std::string override {
        switch (KrpcError(ev)) {
            case KrpcError::BadQuery: return "Bad Query";
            case KrpcError::BadReply: return "Bad Reply";
            case KrpcError::RpcErrorMessage: return "The remote send rpc error message";
            default: return "Unknown Error";
        }
    }

    static auto instance() -> const KrpcErrorCategory & {
        static KrpcErrorCategory c;
        return c;
    }
};

ILIAS_DECLARE_ERROR(KrpcError, KrpcErrorCategory);