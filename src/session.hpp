#pragma once

#include <vector>
#include "net.hpp"

/**
 * @brief Session for provide dht service
 * 
 */
class DhtSession {
public:
    DhtSession(IoContext *ctxt);
    DhtSession(const DhtSession &) = delete;
    ~DhtSession();
private:
    auto _run() -> Task<>;
    IoContext *mCtxt;
};