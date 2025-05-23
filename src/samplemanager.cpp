#include "samplemanager.hpp"

#include <ilias/task/when_any.hpp>

#define MAX_PARALLEL_SAMPLE 30
#define MAX_SAMPLE_INTERVAL (6 * 60 * 60)  // 6 hours
#define MIN_SAMPLE_INTERVAL (10 * 60)      // 10 minutes
#define RESAMPLE_INTERVAL 60               // 1 minutes
#define RANDOM_DIFFUSION_INTERVAL (5 * 60) // 5 minutes
#define SAMPLE_EXECUTION_DELAY 50          // 50 milliseconds
#define MAX_ALLOWED_SAMPLE_FAILURES 10
#define MAX_SAMPLE_TASKS 1000

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
        mSampleNodes.emplace_back(std::make_shared<SampleNode>(SampleNode {.endpoint = endpoint}));
        mSampleEvent.set();
        return true;
    }
    return false;
}

void SampleManager::removeSample(const IPEndpoint &endpoint) {
    mIpEndpoints.erase(endpoint);
    if (auto it = std::find_if(mSampleNodes.begin(), mSampleNodes.end(),
                               [&endpoint](const auto &node) { return node->endpoint == endpoint; });
        it != mSampleNodes.end()) {
        mSampleNodes.erase(it);
    }
}

void SampleManager::clearSamples() {
    mIpEndpoints.clear();
    mSampleNodes.clear();
}

auto SampleManager::getSampleIpEndpoints() const -> std::vector<IPEndpoint> {
    std::vector<IPEndpoint> ret;
    for (const auto &node : mSampleNodes) {
        ret.push_back(node->endpoint);
    }
    return ret;
}

auto SampleManager::getSampleNodes() const -> std::vector<SampleNode> {
    std::vector<SampleNode> ret;
    for (const auto &node : mSampleNodes) {
        ret.push_back(*node);
    }
    return ret;
}

auto SampleManager::excludeIpEndpoints() -> std::vector<IPEndpoint> {
    auto ips = mIpEndpoints;
    for (const auto &node : mSampleNodes) {
        ips.erase(node->endpoint);
    }
    return {ips.begin(), ips.end()};
}

void SampleManager::excludeIpEndpoint(const IPEndpoint &endpoint) {
    mIpEndpoints.insert(endpoint);
    if (auto it = std::find_if(mSampleNodes.begin(), mSampleNodes.end(),
                               [&endpoint](const auto &node) { return node->endpoint == endpoint; });
        it != mSampleNodes.end()) {
        mSampleNodes.erase(it);
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
    for (auto &node : mSampleNodes) {
        if (node->status == SampleNode::Sampling) {
            node->status = SampleNode::NoStatus;
        }
    }
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
    SAMPLE_LOG("SampleManager dump:");
    SAMPLE_LOG("  AutoSample: {}", mAutoSample);
    SAMPLE_LOG("  RandomDiffusion: {}", mRandomDiffusion);
    SAMPLE_LOG("Sample Nodes:");
    SAMPLE_LOG("  | {:<{}} | {:<{}} | {:<{}} | {:<{}} | {:<{}} | {:<{}}", "IpEndpoint", IP_WIDTH, "Status",
               STATUS_WIDTH, "Timeout", TIMEOUT_WIDTH, "HashsCount", COUNT_WIDTH, "SuccessCount", COUNT_WIDTH,
               "Failure", COUNT_WIDTH);
    SAMPLE_LOG("  | {:<{}} | {:<{}} | {:<{}} | {:<{}} | {:<{}} | {:<{}}", "---------", IP_WIDTH, "------", STATUS_WIDTH,
               "-------", TIMEOUT_WIDTH, "---------", COUNT_WIDTH, "----------", COUNT_WIDTH, "----------",
               COUNT_WIDTH);
    for (const auto &node : mSampleNodes) {
        SAMPLE_LOG("  | {:<{}} | {:<{}} | {:<{}} | {:<{}} | {:<{}} | {:<{}}", node->endpoint.toString(), IP_WIDTH,
                   node->status, STATUS_WIDTH, std::max(0, (int)(node->timeout - now)), TIMEOUT_WIDTH, node->hashsCount,
                   COUNT_WIDTH, node->successCount, COUNT_WIDTH, node->failure, COUNT_WIDTH);
    }
    SAMPLE_LOG("exclude IpEndpoints:");
    SAMPLE_LOG("  | IpEndpoint");
    SAMPLE_LOG("  | ---------");
    auto excludeIps = excludeIpEndpoints();
    for (const auto &ip : excludeIps) {
        SAMPLE_LOG("  | {}", ip);
    }
    SAMPLE_LOG("total: sample nodes {}, exclude ip endpoints {}", mSampleNodes.size(), excludeIps.size());
}

auto SampleManager::randomDiffusion(uint64_t &nextTime) -> Task<void> {
    auto id  = NodeId::rand();
    auto res = co_await mSession.findNode(id, DhtSession::FindAlgo::AStar);
    if (!res) {
        SAMPLE_LOG("Failed to random diffusion, error: {}", res.error());
        co_return;
    }
    for (auto &node : *res) {
        if (addSampleIpEndpoint(node.ip)) {
            nextTime = 0; // sample immediately
        }
    }
}

auto SampleManager::sample(std::shared_ptr<SampleNode> node, uint64_t &nextTime) -> Task<> {
    while (mSamplingCount > MAX_PARALLEL_SAMPLE) {
        mSampleEvent.clear();
        if (auto ret = co_await mSampleEvent; !ret) { // wait for sampling count to decrease
            node->status = SampleNode::NoStatus;
            co_return;
        }
    }
    mSamplingCount++;
    SAMPLE_LOG("Sample {}", node->endpoint);
    auto res = co_await mSession.sampleInfoHashes(node->endpoint, NodeId::rand());
    if (!res) {
        if (res.error() == KrpcError::RpcErrorMessage) {
            node->timeout = MAX_SAMPLE_INTERVAL + mLastSampleTime;
            node->status  = SampleNode::BlackList;
            node->failure = 114514;
        }
        if (res.error() != Error::Canceled) {
            if (auto ret = co_await mSession.ping(node->endpoint); !ret) {
                node->timeout = MAX_SAMPLE_INTERVAL + mLastSampleTime;
                node->status  = SampleNode::BlackList;
                node->failure = 114514;
            }
            else {
                node->timeout = RESAMPLE_INTERVAL + mLastSampleTime;
                node->status  = SampleNode::Retry;
                node->failure += 5;
            }
        }
        SAMPLE_LOG("Failed to sample {}, error: {}", node->endpoint, res.error());
    }
    else if (std::any_of(res->samples.begin(), res->samples.end(),
                         [](const InfoHash &hash) { return hash == InfoHash::zero(); })) {
        SAMPLE_LOG("Failed to sample {}, error: zero hash", node->endpoint);
        node->status  = SampleNode::BlackList;
        node->failure = 114514;
    }
    else {
        node->timeout = std::clamp(res->interval, res->samples.size() < res->num ? MIN_SAMPLE_INTERVAL : (60 * 60),
                                   MAX_SAMPLE_INTERVAL) +
                        mLastSampleTime; // at least 10 min, at most 6 hours
        node->successCount++;
        node->failure    = 0;
        node->status     = SampleNode::NoStatus;
        int newHashCount = 0;
        if (mOnInfoHashs) {
            newHashCount += mOnInfoHashs(res->samples);
            if (newHashCount == 0) {
                node->timeout = MAX_SAMPLE_INTERVAL + mLastSampleTime;
            }
        }
        node->hashsCount += newHashCount;
        if (mRandomDiffusion) {
            for (auto &hash : res->nodes) {
                addSampleIpEndpoint(hash.ip);
            }
        }
    }
    nextTime = std::min(nextTime, node->timeout - mLastSampleTime);
    mSamplingCount--;
    mSampleEvent.set();
}

auto SampleManager::autoSample() -> Task<void> {
    while (mAutoSample) {
        if (mIpEndpoints.size() == 0) {
            mSampleEvent.clear();
            if (auto ret = co_await mSampleEvent; !ret) {
                break;
            }
        }
        while (mTaskScope.runningTasks() > MAX_SAMPLE_TASKS) {
            if (auto ret = co_await mSamplingEvent; !ret) {
                break;
            }
        }
        mLastSampleTime =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();
        uint64_t nextTime = MAX_SAMPLE_INTERVAL;
        SAMPLE_LOG("Sample nodes: {}, time: {}", mSampleNodes.size(), mLastSampleTime);
        for (auto it = mSampleNodes.begin(); it != mSampleNodes.end();) {
            auto node = *it;
            if (node->timeout <= mLastSampleTime) {
                if (node->status == SampleNode::BlackList) {
                    mIpEndpoints.erase(node->endpoint);
                    it = mSampleNodes.erase(it);
                    continue;
                }
                if (node->status != SampleNode::Sampling) {
                    node->status = SampleNode::Sampling;
                    mTaskScope.spawn(sample(node, nextTime));
                }
            }
            else {
                nextTime = std::min(nextTime, node->timeout - mLastSampleTime);
            }
            it = std::next(it);
        }
        if (nextTime == MAX_SAMPLE_INTERVAL) {
            nextTime = MIN_SAMPLE_INTERVAL;
        }
        if (mTaskScope.runningTasks() <= 1 && mRandomDiffusion) {
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