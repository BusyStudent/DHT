#include "fetchmanager.hpp"
#include "metafetcher.hpp"
#include "log.hpp"
#include "bt.hpp"

FetchManager::~FetchManager() {
    mScope.cancel(); // Cancel all the tasks
    mScope.wait();
}

auto FetchManager::addHash(const InfoHash& hash, const IPEndpoint& endpoint)
    -> void {
    if (mFetched.contains(hash)) {
        return;
    }
    mPending[hash].insert(endpoint);
    // Add the fetching task into it
    if (mScope.runningTasks() < mMaxCocurrent && mPending.size() > 0 && !mWorkers.contains(hash)) {
        // Spawn a new worker to fetch the first hash
        mScope.spawn(doFetch(hash));
        mWorkers.insert(hash);
    }
}

auto FetchManager::setOnFetched(
    std::function<void(InfoHash hash, std::vector<std::byte> data)> fn)
    -> void 
{
    mOnFetched = std::move(fn);
}

auto FetchManager::markFetched(InfoHash hash) -> void {
    mFetched.insert(hash);
    mPending.erase(hash); // Remove the hash from pending
    mWorkers.erase(hash); // Remove the hash from workers
    BT_LOG("Hash {} marked as fetched", hash);
}

auto FetchManager::setUtpContext(UtpContext &utp) -> void {
    mUtp = &utp;
}

auto FetchManager::doFetch(InfoHash hash) -> Task<void> {
    auto self = co_await currentTask();
    while (!mPending[hash].empty()) {
        // Got the pending hash we try to fetch
        auto endpoint = *mPending[hash].begin();
        mPending[hash].erase(endpoint);

        BT_LOG("Worker connect to {}", endpoint);
        DynStreamClient client;
        if (mUtp) { // Try UTP First
            if (auto res = co_await utpConnect(endpoint); res) {
                client = std::move(*res);
            }
        }
#if 1
        if (!client) { // Try TCP
            if (auto res = co_await tcpConnect(endpoint); res) {
                client = std::move(*res);
            }
            else {
                BT_LOG("Failed to connect to {}: {}", endpoint, res.error());
                continue; // Try next endpoint
            }
        }
#else
        if (!client) {
            continue; // Try next endpoint
        }
#endif

        MetadataFetcher fetcher{std::move(client), hash};
        if (auto meta = co_await fetcher.fetch(); !meta) {
            BT_LOG("Failed to fetch metadata: {}", meta.error());
            continue;
        }
        else {
            BT_LOG("Got metadata from hash {} :", hash, endpoint);
            mFetched.insert(hash);
            mPending.erase(hash);  // We are done with this hash

        if (mOnFetched) {
            mOnFetched(hash, std::move(*meta));
        }
            break;
        }
    }
    mPending.erase(hash);
    mWorkers.erase(hash);

    if (!self.isCancellationRequested()) {
        // No in cancel state?
        if (mScope.runningTasks() < mMaxCocurrent && mPending.size() > 0) {
            // Spawn a new worker to fetch the first hash
            mScope.spawn(doFetch(mPending.begin()->first));
        }
    }
    BT_LOG("Fetch worker quit, {} left", mScope.runningTasks() - 1);
}

auto FetchManager::tcpConnect(const IPEndpoint &endpoint) -> IoTask<TcpClient> {
    auto client = co_await TcpClient::make(endpoint.family());
    if (!client) {
        BT_LOG("Failed to build client: {}", client.error());
        co_return unexpected(client.error());
    }
    if (auto res = co_await client->connect(endpoint); !res) {
        BT_LOG("Failed to tcp connect to {}: {}", endpoint, res.error());
        co_return unexpected(res.error());
    }
    co_return std::move(*client);
}

auto FetchManager::utpConnect(const IPEndpoint &endpoint) -> IoTask<UtpClient> {
    UtpClient client {*mUtp};
    if (auto res = co_await client.connect(endpoint); !res) {
        BT_LOG("Failed to utp connect to {}: {}", endpoint, res.error());
        co_return unexpected(res.error());
    }
    co_return client;
}
