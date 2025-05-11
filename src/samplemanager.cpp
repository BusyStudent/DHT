#include "samplemanager.hpp"

#include <ilias/task/when_any.hpp>

#define MAX_PARALLEL_SAMPLE 30
#define MAX_SAMPLE_INTERVAL (6 * 60 * 60)  // 6 hours
#define MIN_SAMPLE_INTERVAL (10 * 60)      // 10 minutes
#define RESAMPLE_INTERVAL 60               // 1 minutes
#define RANDOM_DIFFUSION_INTERVAL (5 * 60) // 5 minutes
#define SAMPLE_EXECUTION_DELAY 50          // 50 milliseconds
#define MAX_ALLOWED_SAMPLE_FAILURES 10

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

bool SampleManager::addSampleIpEndpoint(const IPEndpoint &endpoint) {
    if (mIpEndpoints.find(endpoint) == mIpEndpoints.end()) {
        mIpEndpoints.insert(endpoint);
        mSampleNodes.emplace(SampleNode {.endpoint = endpoint});
        mSampleEvent.set();
        return true;
    }
    return false;
}

void SampleManager::removeSample(const IPEndpoint &endpoint) {
    mIpEndpoints.erase(endpoint);
    for (auto it = mSampleNodes.begin(); it != mSampleNodes.end(); ++it) {
        if (it->endpoint == endpoint) {
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
    for (const auto &node : mSampleNodes) {
        ret.push_back(node.endpoint);
    }
    return ret;
}

auto SampleManager::getSampleNodes() const -> std::vector<SampleNode> {
    std::vector<SampleNode> ret;
    for (const auto &node : mSampleNodes) {
        ret.push_back(node);
    }
    return ret;
}

auto SampleManager::excludeIpEndpoints() -> std::vector<IPEndpoint> {
    auto ips = mIpEndpoints;
    for (const auto &node : mSampleNodes) {
        ips.erase(node.endpoint);
    }
    return {ips.begin(), ips.end()};
}

void SampleManager::excludeIpEndpoint(const IPEndpoint &endpoint) {
    mIpEndpoints.insert(endpoint);
    for (auto it = mSampleNodes.begin(); it != mSampleNodes.end(); ++it) {
        if (it->endpoint == endpoint) {
            mSampleNodes.erase(it);
            break;
        }
    }
}

auto SampleManager::start() -> Task<> {
    mAutoSample = true;
    mSampleEvent.set();
    mTaskScope.spawn(autoSample());
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
    mRandomDiffusion = enable;
    mSampleEvent.set();
    mSession.setRandomSearch(!enable);
}

void SampleManager::dump() {
    // Define column widths
    const int IP_WIDTH      = 48; // Increased for IPv6
    const int STATUS_WIDTH  = 10;
    const int TIMEOUT_WIDTH = 8;  // Right-aligned
    const int COUNT_WIDTH   = 12; // Right-aligned (for Hashs, Success, Failure)
    auto      now =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    DHT_LOG("SampleManager dump:");
    DHT_LOG("  AutoSample: {}", mAutoSample);
    DHT_LOG("  RandomDiffusion: {}", mRandomDiffusion);
    DHT_LOG("Sample Nodes:");
    DHT_LOG("  | {:<{}} | {:<{}} | {:<{}} | {:<{}} | {:<{}} | {:<{}}", "IpEndpoint", IP_WIDTH, "Status", STATUS_WIDTH,
            "Timeout", TIMEOUT_WIDTH, "HashsCount", COUNT_WIDTH, "SuccessCount", COUNT_WIDTH, "FailureCount",
            COUNT_WIDTH);
    DHT_LOG("  | {:<{}} | {:<{}} | {:<{}} | {:<{}} | {:<{}} | {:<{}}", "---------", IP_WIDTH, "------", STATUS_WIDTH,
            "-------", TIMEOUT_WIDTH, "---------", COUNT_WIDTH, "----------", COUNT_WIDTH, "----------", COUNT_WIDTH);
    for (const auto &node : mSampleNodes) {
        DHT_LOG("  | {:<{}} | {:<{}} | {:<{}} | {:<{}} | {:<{}} | {:<{}}", node.endpoint.toString(), IP_WIDTH,
                node.status, STATUS_WIDTH, std::max(0, (int)(node.timeout - now)), TIMEOUT_WIDTH, node.hashsCount,
                COUNT_WIDTH, node.successCount, COUNT_WIDTH, node.failureCount, COUNT_WIDTH);
    }
    DHT_LOG("exclude IpEndpoints:");
    DHT_LOG("  | IpEndpoint");
    DHT_LOG("  | ---------");
    auto excludeIps = excludeIpEndpoints();
    for (const auto &ip : excludeIps) {
        DHT_LOG("  | {}", ip);
    }
    DHT_LOG("total: sample nodes {}, exclude ip endpoints {}", mSampleNodes.size(), excludeIps.size());
}

auto SampleManager::randomDiffusion(uint64_t &nextTime) -> Task<void> {
    auto id  = NodeId::rand();
    auto res = co_await mSession.findNode(id, DhtSession::FindAlgo::AStar);
    if (!res) {
        DHT_LOG("Failed to random diffusion, error: {}", res.error());
        co_return;
    }
    for (auto &node : *res) {
        if (addSampleIpEndpoint(node.ip)) {
            nextTime = 0; // sample immediately
        }
    }
}

auto SampleManager::sample(SampleNode node, uint64_t &nextTime) -> Task<> {
    DHT_LOG("Sample {}", node.endpoint);
    auto res = co_await mSession.sampleInfoHashes(node.endpoint, NodeId::rand());
    if (!res) {
        if (res.error() == KrpcError::RpcErrorMessage) {
            node.failureCount = 114514;
        }
        if (res.error() != Error::Canceled) {
            if (node.status == SampleNode::BlackList || node.status == SampleNode::Retry) {
                node.timeout = MAX_SAMPLE_INTERVAL + mLastSampleTime;
                node.status  = SampleNode::BlackList;
            }
            else {
                node.timeout = RESAMPLE_INTERVAL + mLastSampleTime;
                node.status  = SampleNode::Retry;
            }
            node.failureCount++;
        }
        DHT_LOG("Failed to sample {}, error: {}", node.endpoint, res.error());
    }
    else if (std::any_of(res->samples.begin(), res->samples.end(),
                         [](const InfoHash &hash) { return hash == InfoHash::zero(); })) {
        DHT_LOG("Failed to sample {}, error: zero hash", node.endpoint);
        node.failureCount = 114514;
    }
    else {
        node.timeout = std::clamp(res->interval, res->samples.size() < res->num ? MIN_SAMPLE_INTERVAL : (60 * 60),
                                  MAX_SAMPLE_INTERVAL) +
                       mLastSampleTime; // at least 10 min, at most 6 hours
        node.successCount++;
        node.status      = SampleNode::NoStatus;
        int newHashCount = 0;
        if (mOnInfoHashs) {
            newHashCount += mOnInfoHashs(res->samples);
            if (newHashCount == 0) {
                node.timeout = MAX_SAMPLE_INTERVAL + mLastSampleTime;
            }
        }
        node.hashsCount += newHashCount;
        if (mRandomDiffusion) {
            for (auto &hash : res->nodes) {
                addSampleIpEndpoint(hash.ip);
            }
        }
    }
    nextTime = std::min(nextTime, node.timeout - mLastSampleTime);
    if (auto it = std::find_if(mSampleNodes.begin(), mSampleNodes.end(),
                               [&node](const SampleNode &nd) { return nd.endpoint == node.endpoint; });
        it != mSampleNodes.end()) {
        mSampleNodes.erase(it);
        if (node.failureCount <= MAX_ALLOWED_SAMPLE_FAILURES) {
            mSampleNodes.emplace(std::move(node));
        }
    }
}

auto SampleManager::autoSample() -> Task<void> {
    while (mAutoSample) {
        if (mIpEndpoints.size() == 0) {
            mSampleEvent.clear();
            if (auto ret = co_await mSampleEvent; !ret) {
                break;
            }
        }
        mLastSampleTime =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();
        uint64_t  nextTime = MAX_SAMPLE_INTERVAL;
        TaskScope scope;
        DHT_LOG("Sample nodes: {}, time: {}", mSampleNodes.size(), mLastSampleTime);
        for (auto it = mSampleNodes.begin(); it != mSampleNodes.end();) {
            auto &node = *it;
            if (node.failureCount > MAX_ALLOWED_SAMPLE_FAILURES) {
                it = mSampleNodes.erase(it);
                continue;
            }
            else {
                it = std::next(it);
            }
            if (node.timeout <= mLastSampleTime && scope.runningTasks() < MAX_PARALLEL_SAMPLE) {
                scope.spawn(sample(node, nextTime));
            }
            else {
                nextTime = std::min(nextTime, node.timeout - mLastSampleTime);
            }
        }
        if (scope.runningTasks()) {
            co_await scope;
        }
        else if (mRandomDiffusion) {
            co_await randomDiffusion(nextTime);
        }
        if (mAutoSample) {
            if (mRandomDiffusion) {
                nextTime = std::min(nextTime, (uint64_t)(RANDOM_DIFFUSION_INTERVAL));
            }
            nextTime = std::max(0ULL, nextTime);
            mSampleEvent.clear();
            co_await whenAny(sleep(std::chrono::milliseconds(nextTime * 1000 + SAMPLE_EXECUTION_DELAY)), mSampleEvent);
        }
    }
    co_return;
}

auto SampleManager::onQuery(const BenObject &object, const IPEndpoint &ipendpoint) -> void {
    if (mAutoSample) {
        addSampleIpEndpoint(ipendpoint);
    }
}