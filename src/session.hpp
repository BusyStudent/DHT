#pragma once

#include <ilias/sync.hpp>
#include <ilias/net.hpp>
#include <functional>
#include <random>
#include <chrono>
#include <vector>
#include <set>
#include <map>
#include <queue>

#include "route.hpp"
#include "krpc.hpp"
#include "net.hpp"

class DhtSession {
public:
    enum FindAlgo {
        AStar  = 0,
        BfsDfs = 1,
        Dfs    = 2,
    };

public:
    DhtSession(IoContext &ctxt, const NodeId &id, UdpClient &client);
    ~DhtSession();

    /**
     * @brief Execute the session, doing the bootstrap and spawn background task, it will return if the init is done
     *
     * @return Task<void>
     */
    auto start() -> Task<void>;

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
    auto findNode(const NodeId &target, FindAlgo algo = AStar) -> IoTask<std::vector<NodeEndpoint>>;

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
    auto peers() const -> const std::map<InfoHash, std::set<IPEndpoint>> &;

    /**
     * @brief Set the callback triggered when a peer is announced
     *
     * @param callback
     */
    auto setOnAnouncePeer(std::function<void(const InfoHash &hash, const IPEndpoint &peer)> callback) -> void;

    /**
     * @brief Set the callback triggered when a peer is query
     *
     * @param callback
     */
    auto setOnQuery(std::function<void(const BenObject &object, const IPEndpoint &peer)> callback) -> void;

    /**
     * @brief Set the Skip Bootstrap object
     *
     * @param skip Skip the bootstrap?
     */
    auto setSkipBootstrap(bool skip) -> void;

    /**
     * @brief enable/disable the random search
     *
     * @param enable
     */
    auto setRandomSearch(bool enable) -> void;

    /**
     * @brief Sample info hashes from the given node
     *
     * @param nodeIp The node ip to sample
     * @param target The target id to find (see the sample_info_hash request in bep)
     * @return IoTask<SampleInfoHashesReply>  The sampled info hashes
     */
    auto sampleInfoHashes(const IPEndpoint &nodeIp, NodeId target = NodeId::rand()) -> IoTask<SampleInfoHashesReply>;

    /**
     * @brief Get the Peers from the remote tagrte
     *
     * @param endpoint
     * @param target
     * @return IoTask<GetPeersReply>
     */
    auto getPeers(const IPEndpoint &endpoint, const InfoHash &target) -> IoTask<GetPeersReply>;

    /**
     * @brief Process the udp input from the socket
     *
     * @return Task<void>
     */
    auto processUdp(std::span<const std::byte> buffer, const IPEndpoint &from) -> Task<void>;

private:
    struct FindNodeEnv {
        std::set<NodeEndpoint>      visited;
        std::optional<NodeEndpoint> closest; // The closest node to the target
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
    auto sendKrpc(const BenObject &message, const IPEndpoint &endpoint) -> IoTask<std::pair<BenObject, IPEndpoint>>;

    /**
     * @brief Try to find the node by target, using the endpoint
     *
     * @param target
     * @param endpoint
     * @return IoTask<std::vector<NodeEndpoint> >
     */
    auto findNode(const NodeId &target, const IPEndpoint &endpoint, FindAlgo algo = AStar)
        -> IoTask<std::vector<NodeEndpoint>>;

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
    auto bfsDfsFind(const NodeId &target, std::optional<NodeId> id, const IPEndpoint &endpoint, size_t depth,
                    FindNodeEnv &env) -> IoTask<std::vector<NodeEndpoint>>;

    auto aStarFind(const NodeId &target, std::optional<NodeId> id, const IPEndpoint &endpoint, FindNodeEnv &env,
                   int max_parallel = 8, int max_step = 20) -> IoTask<std::vector<NodeEndpoint>>;
    auto findNearNodes(const NodeId &target, std::optional<NodeId> id, const IPEndpoint &endpoint, FindNodeEnv &env)
        -> IoTask<std::vector<NodeEndpoint>>;

    /**
     * @brief Init the session, bootstrap the node
     *
     * @param nodeIp
     * @return IoTask<void>
     */
    auto bootstrap(const IPEndpoint &nodeIp) -> IoTask<void>;

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

    IoContext                &mCtxt;
    TaskScope                 mScope;
    UdpClient                &mClient;
    IPEndpoint                mEndpoint;
    NodeId                    mId;
    RoutingTable              mRoutingTable;
    std::chrono::milliseconds mTimeout         = std::chrono::seconds(10);
    std::chrono::milliseconds mRefreshInterval = std::chrono::minutes(5);  // Refresh the routing table every 5 minute
    std::chrono::milliseconds mCleanupInterval = std::chrono::minutes(15); // Cleanup the peers every 15 minute
    std::chrono::milliseconds mRandomSearchInterval = std::chrono::minutes(10);
    std::mt19937              mRandom {std::random_device {}()};

    std::map<std::string, oneshot::Sender<std::pair<BenObject, IPEndpoint>>>
             mPendingQueries;    //< The pending queries, we sent, waiting for reply
    uint16_t mTransactionId = 0; //< The transaction id

    std::map<InfoHash,
             std::set<IPEndpoint> //< Use set to avoid duplicate
             >
        mPeers; //< The peers they announced
    std::function<void(const InfoHash &hash, const IPEndpoint &peer)>
        mOnAnnouncePeer; //< The callback when we got a announce peer
    std::function<void(const BenObject &message, const IPEndpoint &from)> mOnQuery; // The callback when we got a query

    // Config
    bool  mSkipBootstrap = false;
    bool  mRetryBootstrap = true;
    bool  mRandomSearch = true;
};

enum class KrpcError {
    BadReply,
    BadQuery,
    TargetNotFound,  // The target node is not found
    RpcErrorMessage, // The per send error message
};

class KrpcErrorCategory final : public ErrorCategory {
public:
    auto name() const -> std::string_view override { return "krpc"; }

    auto message(int64_t ev) const -> std::string override {
        switch (KrpcError(ev)) {
            case KrpcError::BadQuery:
                return "Bad Query";
            case KrpcError::BadReply:
                return "Bad Reply";
            case KrpcError::RpcErrorMessage:
                return "The remote send rpc error message";
            case KrpcError::TargetNotFound:
                return "Target Not Found";
            default:
                return "Unknown Error";
        }
    }

    static auto instance() -> const KrpcErrorCategory & {
        static KrpcErrorCategory c;
        return c;
    }
};

ILIAS_DECLARE_ERROR(KrpcError, KrpcErrorCategory);