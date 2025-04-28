/**
 * @file fetchmanager.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Fetching the torrent file from the whole network
 * @version 0.1
 * @date 2025-04-28
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#pragma once
#include <ilias/sync.hpp>
#include <functional>
#include "nodeid.hpp"
#include "net.hpp"
#include <map>
#include <set>

class FetchManager {
public:
    FetchManager() = default;
    ~FetchManager();

    auto addHash(const InfoHash &hash, const IPEndpoint &endpoint) -> void;
    /**
     * @brief Set the on the data was fetched
     * 
     * @param fn 
     */
    auto setOnFetched(std::function<void (InfoHash hash, std::vector<std::byte> data)>  fn) -> void;
private:
    auto doFetch(InfoHash hash) -> Task<void>;

    std::map<
        InfoHash,
        std::set<IPEndpoint>
    > mPending; //< The pending hashs we are fetching

    std::set<InfoHash> mFetched; //< The hashs we have fetched
    TaskScope mScope;

    size_t mMaxCocurrent = 5;
    std::function<void (InfoHash hash, std::vector<std::byte> data)> mOnFetched;
};