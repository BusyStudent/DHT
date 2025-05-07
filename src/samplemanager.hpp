#pragma once

#include "session.hpp"

class SampleManager {
public:
    struct SampleNode {
        enum Status {
            NoStatus,
            Retry,
            BlackList,
        };
        IPEndpoint endpoint = {};
        int        timeout  = -1; // The time in seconds the client should wait before sampleing the node
        Status     status   = NoStatus;

        auto operator<=>(const SampleNode &rhs) const { return timeout <=> rhs.timeout; }
    };

public:
    SampleManager(DhtSession &session);
    ~SampleManager();
    void addSampleIpEndpoint(const IPEndpoint &endpoint);
    void removeSample(const IPEndpoint &endpoint);
    void clearSamples();
    // auto sample(const IPEndpoint &endpoint) -> Task<>;
    // auto sample(const NodeId &nodeId) -> Task<>;
    auto getSamples() const -> std::vector<IPEndpoint>;
    auto start() -> Task<>;
    auto stop() -> Task<>;
    void setOnInfoHashs(std::function<void(const std::vector<InfoHash> &)>);

private:
    auto autoSample() -> Task<void>;
    auto onQuery(const BenObject &object, const IPEndpoint &ipendpoint) -> void;

private:
    DhtSession             &mSession;
    std::set<IPEndpoint>    mIpEndpoints;
    int                     mLastSampleTime = 0;
    Event                   mSampleEvent;
    bool                    mAutoSample = false;
    std::vector<SampleNode> mSampleNodes;
    TaskScope               mTaskScope;

    std::function<void(const std::vector<InfoHash> &)> mOnInfoHashs;
};