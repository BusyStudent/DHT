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
    bool addSampleIpEndpoint(const IPEndpoint &endpoint);
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
    auto randomDiffusion(int &nextTime) -> Task<void>;
    auto autoSample() -> Task<void>;
    auto sample(SampleNode node, int &nextTime) -> Task<>;
    auto onQuery(const BenObject &object, const IPEndpoint &ipendpoint) -> void;

private:
    TaskScope                        mTaskScope;
    DhtSession                      &mSession;
    std::set<IPEndpoint>             mIpEndpoints;
    int                              mLastSampleTime = 0;
    Event                            mSampleEvent;
    bool                             mAutoSample      = false;
    bool                             mRandomDiffusion = true;
    std::map<IPEndpoint, SampleNode> mSampleNodes;

    std::function<int(const std::vector<InfoHash> &)> mOnInfoHashs;
};

// Specialization of std::formatter for SampleManager::SampleNode::Status
template <>
struct std::formatter<SampleManager::SampleNode::Status> {
private:
    // Member to hold an actual formatter for string_view.
    // This underlying formatter will handle parsing and applying
    // fill, alignment, width, precision, and type 's'.
    std::formatter<std::string_view> underlying_formatter;

public:
    // Parses the format string for status.
    // We delegate this to the underlying_formatter for string_view.
    template <class ParseContext>
    constexpr ParseContext::iterator parse(ParseContext &ctx) {
        // Example: if format string is "{:<10}", ctx will contain "<10"
        // The underlying_formatter.parse will consume these and store them internally.
        return underlying_formatter.parse(ctx);
    }

    // Formats the Status 's' using the FmtContext 'ctx'.
    template <class FmtContext>
    FmtContext::iterator format(SampleManager::SampleNode::Status s, FmtContext &ctx) const {
        std::string_view sv;
        switch (s) {
            case SampleManager::SampleNode::NoStatus:
                sv = "NoStatus";
                break;
            case SampleManager::SampleNode::Retry:
                sv = "Retry";
                break;
            case SampleManager::SampleNode::BlackList:
                sv = "BlackList";
                break;
            default:
                sv = "Unknown"; // Or SampleManager::SampleNode::UnknownStatus
                break;
        }
        // Now, use the underlying_formatter to format the string_view 'sv'.
        // It will use the format specifiers parsed earlier (width, alignment, etc.).
        return underlying_formatter.format(sv, ctx);
    }
};