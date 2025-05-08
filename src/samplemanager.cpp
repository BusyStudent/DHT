#include "samplemanager.hpp"

#include <ilias/task/when_any.hpp>

SampleManager::SampleManager(DhtSession &session) : mSession(session) {
    mSession.setOnQuery(std::bind(&SampleManager::onQuery, this, std::placeholders::_1, std::placeholders::_2));
    mTaskScope.setAutoCancel(true);
}

SampleManager::~SampleManager() {
    mAutoSample = false;
    mSession.setOnQuery(nullptr);
}

void SampleManager::addSampleIpEndpoint(const IPEndpoint &endpoint) {
    if (mIpEndpoints.find(endpoint) == mIpEndpoints.end()) {
        mIpEndpoints.insert(endpoint);
        mSampleNodes.emplace_back(SampleNode {.endpoint = endpoint, .timeout = -1});
    }
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

auto SampleManager::getSamples() const -> std::vector<IPEndpoint> {
    return {mIpEndpoints.begin(), mIpEndpoints.end()};
}

auto SampleManager::start() -> Task<> {
    mAutoSample = true;
    mSampleEvent.set();
    mTaskScope.spawn(autoSample());
    co_return;
}

auto SampleManager::stop() -> Task<> {
    mAutoSample = false;
    mTaskScope.cancel();
    co_await mTaskScope;
    co_return;
}

void SampleManager::setOnInfoHashs(std::function<void(const std::vector<InfoHash> &)> callback) {
    mOnInfoHashs = callback;
}

auto SampleManager::autoSample() -> Task<void> {
    while (mAutoSample) {
        while (mIpEndpoints.size() == 0) {
            mSampleEvent.clear();
            co_await mSampleEvent;
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
        for (auto &node : mSampleNodes) {
            if (node.timeout - dtime <= 0) {
                scope.spawn([&node, &nextTime, this]() -> Task<> {
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
                        DHT_LOG("Failed to sample {}, error: {}", node.endpoint, res.error());
                        co_return;
                    }
                    node.timeout = std::max(res->interval, 10 * 60); // at least 10 min
                    node.status  = SampleNode::NoStatus;
                    if (mOnInfoHashs) {
                        mOnInfoHashs(res->samples);
                    }
                    nextTime = std::min(nextTime, node.timeout);
                });
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
    mSampleEvent.set();
}