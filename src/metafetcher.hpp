/**
 * @file metafetcher.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief Fetch the metadata from the infohash
 * @version 0.1
 * @date 2025-04-18
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#pragma once
#include "net.hpp"
#include "bt.hpp"

/**
 * @brief The helper class to fetch the metadata by infohash from the peer
 * 
 */
class MetadataFetcher {
public:
    static constexpr size_t ChunkSize = 16384; // 16KB
    
    MetadataFetcher(DynStreamClient client, const InfoHash &hash) : mClient(std::move(client)), mHash(hash) {
        // Do nothing
    }

    /**
     * @brief Do the fetching, return the metadata
     * 
     * @return IoTask<std::vector<std::byte> > 
     */
    auto fetch() -> IoTask<std::vector<std::byte> >;
private:
    BtClient mClient;
    InfoHash mHash;
};