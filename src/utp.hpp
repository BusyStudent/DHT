#pragma once

#include <memory_resource>
#include <memory>
#include <ilias/sync.hpp>
#include "net.hpp"
#include "../libutp/utp.h"

class UtpContext {
public:
    UtpContext(UdpClient &client);
    UtpContext(const UtpContext &) = delete;
    ~UtpContext();

    /**
     * @brief Process the UDP Packet
     * 
     * @param endpoint 
     * @return true On valid utp packet
     * @return false 
     */
    auto processUdp(std::span<const std::byte>, const IPEndpoint &endpoint) -> bool;
private:
    auto onSendto(std::pmr::vector<std::byte> buffer, IPEndpoint target) -> Task<void>;

    std::pmr::unsynchronized_pool_resource mBufferResource;
    utp_context *mCtxt = nullptr;
    UdpClient   &mClient;
    TaskScope    mScope;
friend class UtpClient;
};

class UtpClientImpl;
class UtpClient {
public:
    UtpClient() = default;
    UtpClient(UtpContext &session);
    UtpClient(const UtpClient &) = default;
    UtpClient(UtpClient &&) = default;
    ~UtpClient();

    auto connect(const IPEndpoint &endpoint) -> IoTask<void>;
    auto write(std::span<const std::byte>) -> IoTask<size_t>;
    auto read(std::span<std::byte>) -> IoTask<size_t>;
    auto shutdown() -> IoTask<void>;
    auto remoteEndpoint() const -> Result<IPEndpoint>;

    auto operator =(const UtpClient &) -> UtpClient & = default;
    auto operator =(UtpClient &&) -> UtpClient & = default;
private:
    std::shared_ptr<UtpClientImpl> mImpl;
};