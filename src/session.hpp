#pragma once

#include <ilias/sync.hpp>
#include <ilias/net.hpp>
#include <functional>
#include <chrono>
#include <vector>
#include "route.hpp"
#include "krpc.hpp"
#include "net.hpp"

class DhtSession {
public:
    DhtSession(IoContext &ctxt, const IPEndpoint &listen);

    /**
     * @brief Execute the session, doing the bootstrap and 
     * 
     * @return Task<void> 
     */
    auto run() -> Task<void>;
private:
    /**
     * @brief Send a krpc to the remote, waiting for the reply
     * 
     * @param peer 
     * @param message 
     * @return IoTask<std::pair<BenObject, IPEndpoint> > 
     */
    auto sendKrpc(const BenObject &message, const IPEndpoint &endpoint) -> IoTask<std::pair<BenObject, IPEndpoint> >;

    IoContext &mCtxt;
    TaskScope  mScope;
    IPEndpoint mEndpoint; //< The Endpoint for listen
    UdpClient mClient;

    std::map<
        std::string, 
        oneshot::Sender<std::pair<BenObject, IPEndpoint> > 
    > mPendingQueries; //< The pending queries, we sent, waiting for reply
};