#pragma once

#include "session.hpp"

class SampleManager {
public:
    struct SampleNode {
        enum Status {
            NoStatus,
            Retry,
            BlackList,
            Sampling,
        };
        IPEndpoint endpoint     = {};
        uint64_t   timeout      = 0;
        Status     status       = NoStatus;
        int        failure      = 0; // weight of failure
        int        successCount = 0;
        int        hashsCount   = 0;

        bool operator==(const SampleNode &other) const { return endpoint == other.endpoint; }
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
    auto randomDiffusion(uint64_t &nextTime) -> Task<void>;
    auto autoSample() -> Task<void>;
    auto sample(std::shared_ptr<SampleNode> node, uint64_t &nextTime) -> Task<>;
    auto onQuery(const BenObject &object, const IPEndpoint &ipendpoint) -> void;

private:
    TaskScope                                mTaskScope;
    DhtSession                              &mSession;
    uint64_t                                 mLastSampleTime = 0;
    Event                                    mSampleEvent;
    bool                                     mAutoSample      = false;
    bool                                     mRandomDiffusion = true;
    std::set<IPEndpoint>                     mIpEndpoints;
    std::vector<std::shared_ptr<SampleNode>> mSampleNodes;
    int                                      mSamplingCount = 0;
    Event                                    mSamplingEvent;

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
            case SampleManager::SampleNode::Sampling:
                sv = "Sampling";
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