/**
 * @file getpeersmanager.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2025-05-10
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#pragma once

#include "session.hpp"
#include "nodeid.hpp"
#include "krpc.hpp"
#include <deque>
#include <map>

class GetPeersManager {
public:
    GetPeersManager(DhtSession &session);
    ~GetPeersManager();

    auto addHash(const InfoHash &hash) -> void;
    auto setOnPeerGot(std::function<void(const InfoHash &hash, const IPEndpoint &peer)> fn) -> void;
private:
    auto getPeers(const InfoHash &target) -> Task<void>;
    auto getPeersWorker(InfoHash hash) -> Task<void>;

    DhtSession &mSession;

    std::set<InfoHash> mFinished;
    std::set<InfoHash> mHashes; // The hash we are waiting or doing the get peers
    TaskScope mScope;
    size_t mMaxCoCurrent = 5;
    size_t mCoCurrent = 0; // The current cocurrent tasks
    Event  mEvent; // The event of the 

    std::function<void(const InfoHash &hash, const IPEndpoint &peer)> mOnPeerGot;
};