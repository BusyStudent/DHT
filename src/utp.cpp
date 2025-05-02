#include <ilias/io/stream.hpp>
#include "utp.hpp"
#include "log.hpp"

using namespace std::chrono_literals;

/**
 * @brief The internal data of the utp client
 * 
 */
class UtpClientImpl {
public:
    enum {
        NoError = 114514
    };

    UtpClientImpl(utp_socket *s) : sock(s) {
        utp_set_userdata(sock, this);
        onStateChanged.setAutoClear(true);
        onReadable.setAutoClear(true);
    }
    UtpClientImpl(const UtpClientImpl &) = delete;
    ~UtpClientImpl() {
        utp_close(sock);
    }

    auto stateChange(int s) -> void {
        state = s;
        onStateChanged.set();
        if (state == UTP_STATE_EOF) {
            onReadable.set();
        }
    }

    auto readable(std::span<const std::byte> buffer) -> void {
        auto span = readBuffer.prepare(buffer.size());
        assert(span.size() == buffer.size());
        ::memcpy(span.data(), buffer.data(), buffer.size());
        readBuffer.commit(buffer.size());
        
        onReadable.set();
    }

    auto connected() -> void {
        onConnected.set();
    }

    auto errored(int err) -> void {
        error = err;
        onConnected.set();
        onStateChanged.set();
    }

    static auto from(utp_socket *sock) -> UtpClientImpl * {
        return static_cast<UtpClientImpl *>(utp_get_userdata(sock));
    }

    // State
    int state = 0;
    Event onStateChanged;

    // Read
    Event onReadable;
    StreamBuffer readBuffer;

    // Connected
    Event onConnected;

    // Error
    int error = NoError; // No error

    utp_socket *sock = nullptr;
};

UtpContext::UtpContext(UdpClient &client) : mClient(client) {
    mCtxt = utp_init(2);
    utp_context_set_userdata(mCtxt, this);
    utp_set_callback(mCtxt, UTP_SENDTO, [](utp_callback_arguments *args) -> uint64 {
        auto self = static_cast<UtpContext *>(utp_context_get_userdata(args->context));
        auto endpoint = IPEndpoint::fromRaw(args->address, args->address_len);
        auto buffer = makeBuffer(args->buf, args->len);
        auto data = std::pmr::vector<std::byte>(buffer.begin(), buffer.end(), &self->mBufferResource);

        // Then spawn a task to send the data to the target endpoint
        self->mScope.spawn(self->onSendto(std::move(data), *endpoint));
        return 0;
    });
    utp_set_callback(mCtxt, UTP_ON_STATE_CHANGE, [](utp_callback_arguments *args) -> uint64 {
        auto self = static_cast<UtpContext *>(utp_context_get_userdata(args->context));
        auto sock = UtpClientImpl::from(args->socket);
        sock->stateChange(args->state);
        return 0;
    });
    utp_set_callback(mCtxt, UTP_ON_READ, [](utp_callback_arguments *args) -> uint64 {
        auto self = static_cast<UtpContext *>(utp_context_get_userdata(args->context));
        auto sock = UtpClientImpl::from(args->socket);
        auto buffer = makeBuffer(args->buf, args->len);
        sock->readable(buffer);
        UTP_LOG("num {} readable", buffer.size());
        return 0;
    });
    utp_set_callback(mCtxt, UTP_ON_CONNECT, [](utp_callback_arguments *args) -> uint64 {
        auto self = static_cast<UtpContext *>(utp_context_get_userdata(args->context));
        auto sock = UtpClientImpl::from(args->socket);
        UTP_LOG("Connected done");
        sock->connected();
        return 0;
    });
    utp_set_callback(mCtxt, UTP_ON_ERROR, [](utp_callback_arguments *args) -> uint64 {
        auto self = static_cast<UtpContext *>(utp_context_get_userdata(args->context));
        auto sock = UtpClientImpl::from(args->socket);
        sock->errored(args->error_code);
        return 0;
    });
    mScope.spawn([this]() -> Task<void> {
        while (co_await sleep(500ms)) {
            UTP_LOG("Check timeouts");
            utp_check_timeouts(mCtxt);
            utp_issue_deferred_acks(mCtxt);
        }
    });
}

UtpContext::~UtpContext() {
    mScope.cancel();
    mScope.wait();
    if (mCtxt) {
        utp_destroy(mCtxt);
    }
}

auto UtpContext::processUdp(std::span<const std::byte> buffer, const IPEndpoint &endpoint) -> bool {
    utp_issue_deferred_acks(mCtxt);
    return utp_process_udp(mCtxt, (byte*) buffer.data(), buffer.size(), &endpoint.cast<::sockaddr>(), endpoint.length()) == 1;
}

auto UtpContext::onSendto(std::pmr::vector<std::byte> buffer, IPEndpoint target) -> Task<void> {
    UTP_LOG("Send data to {}", target);
    co_await mClient.sendto(buffer, target);
}


// UtpClient
UtpClient::UtpClient(UtpContext &session) {
    mImpl = std::make_shared<UtpClientImpl>(utp_create_socket(session.mCtxt));
}

UtpClient::~UtpClient() {

}

auto UtpClient::write(std::span<const std::byte> buffer) -> IoTask<size_t> {
    if (!mImpl || !mImpl->sock) {
        co_return unexpected(Error::InvalidArgument);
    }
    while (true) {
        while (mImpl->state == 0 && mImpl->error != mImpl->NoError) { // Wait for the state to be changed
            if (auto res = co_await mImpl->onStateChanged; !res) {
                co_return unexpected(res.error());
            }
        }
        if (mImpl->state == UTP_STATE_EOF) {
            co_return 0; // No more data can be sent
        }
        switch (mImpl->error) { // Check for errors
            case UTP_ECONNREFUSED: co_return unexpected(Error::ConnectionRefused); break;
            case UTP_ECONNRESET: co_return unexpected(Error::ConnectionReset); break;
            case UTP_ETIMEDOUT: co_return unexpected(Error::ConnectionAborted); break;
            default: break;
        }

        // Emm. I check the code of libutp, it may return 0 when the buffer is full, so we need to check writiable and co_await when it is writable
        auto ret = utp_write(mImpl->sock, (void*) buffer.data(), buffer.size());
        if (ret < 0) {
            co_return unexpected(Error::InvalidArgument);
        }
        co_return ret;
    }
}

auto UtpClient::read(std::span<std::byte> buffer) -> IoTask<size_t> {
    if (!mImpl || !mImpl->sock) {
        co_return unexpected(Error::InvalidArgument);
    }
    while (true) {
        if (mImpl->readBuffer.size() == 0) {
            if (mImpl->state == UTP_STATE_EOF) {
                co_return 0; // No more data can be read
            }
            if (auto res = co_await mImpl->onReadable; !res) {
                co_return unexpected(res.error());
            }
        }
        auto span = mImpl->readBuffer.data();
        auto left = std::min(buffer.size(), span.size());
        ::memcpy(buffer.data(), span.data(), left);
        mImpl->readBuffer.consume(left);
        if (mImpl->readBuffer.empty()) { // All data has been read
            utp_read_drained(mImpl->sock); // Notify the socket that we have read all data
        }
        co_return left;
    }
}

auto UtpClient::shutdown() -> IoTask<void> {
    if (!mImpl || !mImpl->sock) {
        co_return unexpected(Error::InvalidArgument);
    }
    utp_shutdown(mImpl->sock, Shutdown::Both);
    co_return {};
}

auto UtpClient::connect(const IPEndpoint &endpoint) -> IoTask<void> {
    utp_connect(mImpl->sock, &endpoint.cast<::sockaddr>(), endpoint.length());
    co_await mImpl->onConnected;
    switch (mImpl->error) {
        case UTP_ECONNREFUSED: co_return unexpected(Error::ConnectionRefused); break;
        case UTP_ECONNRESET: co_return unexpected(Error::ConnectionReset); break;
        case UTP_ETIMEDOUT: co_return unexpected(Error::ConnectionAborted); break;
        default: co_return {};
    }
}

auto UtpClient::remoteEndpoint() const -> Result<IPEndpoint> {
    if (!mImpl || !mImpl->sock) {
        return unexpected(Error::InvalidArgument);
    }
    IPEndpoint endpoint;
    socklen_t len = endpoint.bufsize();
    if (utp_getpeername(mImpl->sock, &endpoint.cast<::sockaddr>(), &len) == 0) {
        return unexpected(Error::InvalidArgument);
    }
    utp_read_drained(mImpl->sock);
    return endpoint;
}