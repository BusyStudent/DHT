#include "samplemanager.hpp"

#include <ilias/task/when_any.hpp>

SampleManager::SampleManager(DhtSession &session) : mSession(session) {
    mSession.setOnQuery(std::bind(&SampleManager::onQuery, this, std::placeholders::_1, std::placeholders::_2));
    mTaskScope.setAutoCancel(true);
}

SampleManager::~SampleManager() {
    mAutoSample      = false;
    mRandomDiffusion = false;
    mSession.setOnQuery(nullptr);
    mTaskScope.cancel();
    mTaskScope.wait();
}

void SampleManager::addSampleIpEndpoint(const IPEndpoint &endpoint) {
    if (mIpEndpoints.find(endpoint) == mIpEndpoints.end()) {
        mIpEndpoints.insert(endpoint);
        mSampleNodes.emplace(endpoint, SampleNode {.endpoint = endpoint, .timeout = -1});
        mSampleEvent.set();
    }
}

void SampleManager::removeSample(const IPEndpoint &endpoint) {
    mIpEndpoints.erase(endpoint);
    for (auto it = mSampleNodes.begin(); it != mSampleNodes.end(); ++it) {
        if (it->first == endpoint) {
            mSampleNodes.erase(it);
            break;
        }
    }
}

void SampleManager::clearSamples() {
    mIpEndpoints.clear();
    mSampleNodes.clear();
}

auto SampleManager::getSampleIpEndpoints() const -> std::vector<IPEndpoint> {
    std::vector<IPEndpoint> ret;
    for (const auto &[ip, node] : mSampleNodes) {
        ret.push_back(ip);
    }
    return ret;
}

auto SampleManager::getSampleNodes() const -> std::vector<SampleNode> {
    std::vector<SampleNode> ret;
    for (const auto &[ip, node] : mSampleNodes) {
        ret.push_back(node);
    }
    return ret;
}

auto SampleManager::excludeIpEndpoints() -> std::vector<IPEndpoint> {
    auto ips = mIpEndpoints;
    for (const auto &[ip, node] : mSampleNodes) {
        ips.erase(ip);
    }
    return {ips.begin(), ips.end()};
}

void SampleManager::excludeIpEndpoint(const IPEndpoint &endpoint) {
    mIpEndpoints.insert(endpoint);
    for (auto it = mSampleNodes.begin(); it != mSampleNodes.end(); ++it) {
        if (it->first == endpoint) {
            mSampleNodes.erase(it);
            break;
        }
    }
}

auto SampleManager::start() -> Task<> {
    mAutoSample      = true;
    mRandomDiffusion = true;
    mSampleEvent.set();
    mTaskScope.spawn(autoSample());
    mTaskScope.spawn(randomDiffusion());
    co_return;
}

auto SampleManager::stop() -> Task<> {
    mAutoSample      = false;
    mRandomDiffusion = false;
    mTaskScope.cancel();
    co_await mTaskScope;
    co_return;
}

void SampleManager::setOnInfoHashs(std::function<int(const std::vector<InfoHash> &)> callback) {
    mOnInfoHashs = callback;
}

void SampleManager::setRandomDiffusion(bool enable) {
    if (enable) {
        mRandomDiffusionEvent.set();
    }
    else {
        mRandomDiffusionEvent.clear();
    }
}

void SampleManager::dump() {
    DHT_LOG("SampleManager dump:");
    DHT_LOG("  AutoSample: {}", mAutoSample);
    DHT_LOG("  RandomDiffusion: {}", mRandomDiffusionEvent.isSet() && mRandomDiffusion);
    DHT_LOG("Sample Nodes:");
    DHT_LOG("  IpEndpoint Status Timeout SuccessCount FailureCount");
    for (const auto &[ip, node] : mSampleNodes) {
        DHT_LOG("  {} {} {} {} {}", ip, node.status, node.timeout, node.successCount, node.failureCount);
    }
    DHT_LOG("exclude IpEndpoints:");
    DHT_LOG("  IpEndpoint");
    for (const auto &ip : excludeIpEndpoints()) {
        DHT_LOG("  {}", ip);
    }
}

auto SampleManager::randomDiffusion() -> Task<void> {
    while (mRandomDiffusion) {
        if (auto ret = co_await mRandomDiffusionEvent; !ret) {
            break;
        }
        auto id  = NodeId::rand();
        auto res = co_await mSession.findNode(id, DhtSession::FindAlgo::AStar);
        if (res) {
            for (auto &node : *res) {
                addSampleIpEndpoint(node.ip);
            }
        }
        co_await sleep(std::chrono::seconds(10));
    }
    co_return;
}

auto SampleManager::sample(SampleNode node, int &nextTime) -> Task<> {
    DHT_LOG("Sample {}", node.endpoint);
    auto res = co_await mSession.sampleInfoHashes(node.endpoint);
    if (!res) {
        if (node.status == SampleNode::BlackList || node.status == SampleNode::Retry) {
            node.timeout = 6 * 60 * 60;
            node.status  = SampleNode::BlackList;
        }
        else {
            node.timeout = 60;
            node.status  = SampleNode::Retry;
        }
        if (res.error() == KrpcError::RpcErrorMessage) {
            node.failureCount = 114514;
        }
        node.failureCount++;
        DHT_LOG("Failed to sample {}, error: {}", node.endpoint, res.error());
    }
    else if (std::any_of(res->samples.begin(), res->samples.end(),
                         [](const InfoHash &hash) { return hash == InfoHash::zero(); })) {
        node.timeout      = 6 * 60 * 60;
        node.failureCount = 114514;
        DHT_LOG("Failed to sample {}, error: zero hash", node.endpoint);
    }
    else {
        node.timeout = std::clamp(res->interval, res->samples.size() < res->num ? 10 * 60 : 60 * 60,
                                  6 * 60 * 60); // at least 10 min, at most 6 hours
        node.successCount++;
        node.status = SampleNode::NoStatus;
        if (mOnInfoHashs) {
            node.hashsCount += mOnInfoHashs(res->samples);
        }
    }
    nextTime = std::min(nextTime, node.timeout);
    if (auto it = mSampleNodes.find(node.endpoint); it != mSampleNodes.end()) {
        std::swap(it->second, node);
    }
}

auto SampleManager::autoSample() -> Task<void> {
    while (mAutoSample) {
        while (mIpEndpoints.size() == 0) {
            mSampleEvent.clear();
            if (auto ret = co_await mSampleEvent; !ret) {
                break;
            }
        }
        if (mLastSampleTime == 0) {
            mLastSampleTime =
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                    .count();
        }
        auto dtime =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                .count() -
            mLastSampleTime;
        int nextTime = 6 * 60 * 60;
        DHT_LOG("Sample nodes: {}, dtime: {}", mSampleNodes.size(), dtime);
        TaskScope scope;
        for (auto it = mSampleNodes.begin(); it != mSampleNodes.end();) {
            auto node = it->second;
            if (node.failureCount > 10) {
                it = mSampleNodes.erase(it);
                continue;
            }
            else {
                ++it;
            }
            if (node.timeout - dtime <= 0) {
                scope.spawn(sample(node, nextTime));
            }
            else {
                node.timeout -= dtime;
                nextTime = std::min(nextTime, node.timeout);
            }
        }
        co_await scope;
        mLastSampleTime =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();
        if (mAutoSample) {
            mSampleEvent.clear();
            co_await whenAny(sleep(std::chrono::milliseconds(nextTime * 1000 + 50)), mSampleEvent);
        }
    }
    co_return;
}

auto SampleManager::onQuery(const BenObject &object, const IPEndpoint &ipendpoint) -> void {
    DHT_LOG("Query from {}", ipendpoint);
    addSampleIpEndpoint(ipendpoint);
}