#include "getpeersmanager.hpp"
#include <ilias/task/when_all.hpp>

GetPeersManager::GetPeersManager(DhtSession &session) : mSession(session) {
    // auto [sender, receiver] = mpmc::channel<InfoHash>();
    // mSender = sender;
    // for (size_t i = 0; i < mMaxCoCurrent; i++) {
    //     mScope.spawn(getPeersWorker(receiver));
    // }
}

GetPeersManager::~GetPeersManager() {
    mScope.cancel();
    mScope.wait();
}

auto GetPeersManager::addHash(const InfoHash &hash) -> void {
    if (mFinished.contains(hash)) {
        return;
    }
    auto [it, emplace] = mHashes.emplace(hash);
    if (emplace) {
        mScope.spawn(getPeersWorker(hash));
    }
}

auto GetPeersManager::setOnPeerGot(std::function<void(const InfoHash &hash, const IPEndpoint &peer)> fn) -> void {
    mOnPeerGot = std::move(fn);
}

auto GetPeersManager::getPeers(const InfoHash &target) -> Task<void> {
    // Prepare 8 nodes for init finding
    constexpr size_t MAX_ITERATION = 10;
    constexpr size_t MAX_ITERATION_WITHOUT_CLOSEST = 3;
    constexpr size_t BATCH_SIZE = 8;
    std::vector<NodeEndpoint> nodes = mSession.routingTable().findClosestNodes(target, KBUCKET_SIZE);
    std::set<NodeEndpoint> visisted;
    std::vector<IPEndpoint> peers;
    std::optional<NodeEndpoint> closest;
    size_t iterationCount = 0;
    size_t iterationWithoutClosest = 0; // The iteration count without new node replace the current closest node

    while (!nodes.empty() && iterationCount < MAX_ITERATION && iterationWithoutClosest < MAX_ITERATION_WITHOUT_CLOSEST && peers.size() < 8) {
        bool closestChanged = false; // Did the closest node changed ?
        ++iterationCount;

        // Prepare query from the closest node
        std::vector<NodeEndpoint> batch;
        std::vector<IoTask<GetPeersReply> > tasks;
        while (batch.size() < BATCH_SIZE && !nodes.empty()) {
            batch.push_back(nodes.front());
            nodes.erase(nodes.begin());
        }
        for (auto &endpoint : batch) {
            GET_PEERS_LOG("iteration[{}] Try get peer {} to {}", iterationCount, target, endpoint);
            tasks.push_back(mSession.getPeers(endpoint.ip, endpoint.id));
            visisted.insert(endpoint);
        }
        for (auto &reply : co_await whenAll(std::move(tasks))) {
            if (!reply) {
                if (reply.error() == Error::Canceled) {
                    co_return;
                }
                continue;
            }
            auto &values = reply->values;
            auto &rnodes = reply->nodes;
            // Insert the peers we got
            peers.insert(peers.end(), values.begin(), values.end());

            // Collect the node
            for (auto &node : rnodes) {
                if (!closest || closest->id.distance(target) > node.id.distance(target)) {
                    closest = node;
                    // Set it
                    closestChanged = true;
                    iterationWithoutClosest = 0;
                }
                if (!visisted.contains(node)) {
                    nodes.push_back(node);
                }
            }
            // Sort it
            std::sort(nodes.begin(), nodes.end(), [&target](const NodeEndpoint &a, const NodeEndpoint &b) {
                return a.id.distance(target) < b.id.distance(target);
            });
            nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
        }
        if (!closestChanged) {
            ++iterationWithoutClosest;
        }
    }
    GET_PEERS_LOG("Done, {} peers found, iteration {}, iterationWithoutClosest {}", peers.size(), iterationCount, iterationWithoutClosest);
    // Notify the peers
    if (mOnPeerGot && !peers.empty()) {
        for (auto &peer : peers) {
            mOnPeerGot(target, peer);
        }
    }
    co_return;
}

// TODO: Replace the Event to Sem
auto GetPeersManager::getPeersWorker(InfoHash hash) -> Task<void> {
    while (mCoCurrent >= mMaxCoCurrent) {
        if (auto val = co_await mEvent; !val) { // Wait the event or cancel requests
            co_return;
        }
    }
    // Got one 
    mCoCurrent += 1;
    if (mCoCurrent >= mMaxCoCurrent) {
        mEvent.clear();
    }
    GET_PEERS_LOG("Worker of {} start get peers", hash);
    co_await getPeers(hash);

    // Cleanup ....
    mHashes.erase(hash);
    mCoCurrent -= 1;
    if (mCoCurrent < mMaxCoCurrent) {
        mEvent.set();
    }
}