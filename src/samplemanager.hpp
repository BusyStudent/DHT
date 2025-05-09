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
        IPEndpoint endpoint     = {};
        int        timeout      = -1; // The time in seconds the client should wait before sampleing the node
        Status     status       = NoStatus;
        int        successCount = 0;
        int        failureCount = 0;
        int        hashsCount   = 0;
    };

public:
    SampleManager(DhtSession &session);
    ~SampleManager();
    void addSampleIpEndpoint(const IPEndpoint &endpoint);
    void removeSample(const IPEndpoint &endpoint);
    void clearSamples();
    auto getSampleIpEndpoints() const -> std::vector<IPEndpoint>;
    auto getSampleNodes() const -> std::vector<SampleNode>;
    auto excludeIpEndpoints() -> std::vector<IPEndpoint>;
    void excludeIpEndpoint(const IPEndpoint &endpoint);
    auto start() -> Task<>;
    auto stop() -> Task<>;
    void setOnInfoHashs(std::function<int(const std::vector<InfoHash> &)>);
    void setRandomDiffusion(bool enable);
    void dump();

private:
    auto randomDiffusion() -> Task<void>;
    auto autoSample() -> Task<void>;
    auto sample(SampleNode node, int &nextTime) -> Task<>;
    auto onQuery(const BenObject &object, const IPEndpoint &ipendpoint) -> void;

private:
    TaskScope                        mTaskScope;
    DhtSession                      &mSession;
    std::set<IPEndpoint>             mIpEndpoints;
    int                              mLastSampleTime = 0;
    Event                            mSampleEvent;
    bool                             mAutoSample = false;
    Event                            mRandomDiffusionEvent;
    bool                             mRandomDiffusion = true;
    std::map<IPEndpoint, SampleNode> mSampleNodes;

    std::function<int(const std::vector<InfoHash> &)> mOnInfoHashs;
};

template <>
struct std::formatter<SampleManager::SampleNode::Status> {
    template <class ParseContext>
    constexpr ParseContext::iterator parse(ParseContext &ctx) {
        return ctx.begin();
    }

    template <class FmtContext>
    FmtContext::iterator format(SampleManager::SampleNode::Status s, FmtContext &ctx) const {
        std::string_view sv;
        switch (s) {
            case SampleManager::SampleNode::NoStatus: sv = "NoStatus"; break;
            case SampleManager::SampleNode::Retry: sv = "Retry"; break;
            case SampleManager::SampleNode::BlackList: sv = "BlackList"; break;
            default: sv = "Unknown"; break;
        }
        return std::format_to(ctx.out(), "{}", sv);
    }
};