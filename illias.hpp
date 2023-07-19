#pragma once

// Import std
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <utility>
#include <string>

#define ILLIAS_NAMESPACE Illias

#if !defined(ILLIAS_NAMESPACE)
    #define ILLIAS_NS_BEGIN
    #define ILLIAS_NS_END
#else
    #define ILLIAS_NS_BEGIN namespace ILLIAS_NAMESPACE {
    #define ILLIAS_NS_END }
#endif

#if defined(_WIN32)
    #include <WinSock2.h>
    #include <WS2tcpip.h>

    #define ILLIAS_SOCKET_T       SOCKET
    #define ILLIAS_BYTE_T         char
    #define ILLIAS_SSIZE_T        int
    #define ILLIAS_ERROR_T        DWORD
    #define ILLIAS_INVALID_SOCKET INVALID_SOCKET
    #define ILLIAS_POLL           WSAPoll

    #ifdef _MSC_VER
        #pragma comment(lib, "Ws2_32.lib")
    #endif
#else //< POSIX
    #define closesocket           close
#endif


ILLIAS_NS_BEGIN

using socket_t = ILLIAS_SOCKET_T;
using ssize_t  = ILLIAS_SSIZE_T;
using error_t  = ILLIAS_ERROR_T;
using byte_t   = ILLIAS_BYTE_T;
using pollfd_t = ::pollfd;

enum AddressFamily : int {
    V4 = AF_INET,
    V6 = AF_INET6
};

enum SocketType : int {
    Tcp = SOCK_STREAM,
    Udp = SOCK_DGRAM,
};

class IPAddress {
    public:
        IPAddress() = default;
        IPAddress(AddressFamily family, const char *addr) noexcept : family(family) {
            if (::inet_pton(int(family), addr, &storage) != 1) {
                clear();
            }
        }
        IPAddress(const char *addr) noexcept {
            family = ::strlen(addr) >= INET6_ADDRSTRLEN ? AddressFamily::V6 : AddressFamily::V4;
            if (::inet_pton(int(family), addr, &storage) != 1) {
                clear();
            }
        }
        IPAddress(::in_addr addr) noexcept : family(AddressFamily::V4) {
            storage.v4 = addr;
        }
        IPAddress(::in6_addr addr) noexcept : family(AddressFamily::V6) {
            storage.v6 = addr;
        }
        IPAddress(const IPAddress &) = default;
        ~IPAddress() = default;

        bool compare(const IPAddress &addr) const noexcept {
            return ::memcmp(this, &addr, sizeof(IPAddress)) == 0;
        }
        bool empty() const noexcept {
            return family != AddressFamily::V4 && family != AddressFamily::V6;
        }
        void clear() {
            ::memset(this, 0, sizeof(IPAddress));
        }
        socklen_t size() const noexcept {
            switch (family) {
                case AddressFamily::V4: return sizeof(::in_addr);
                case AddressFamily::V6: return sizeof(::in6_addr);
                default : return 0;
            }
        }

        template <typename T>
        T &as() noexcept {
            return reinterpret_cast<T&>(storage);
        }
        template <typename T>
        const T &as() const noexcept {
            return reinterpret_cast<const T&>(storage);
        }

        std::string to_string() const {
            char buffer[INET6_ADDRSTRLEN];
            auto p = ::inet_ntop(int(family), &storage, buffer, sizeof(buffer));
            if (p) {
                return p;
            }
            return std::string();
        }
        std::string hostname() const {
            auto ent = ::gethostbyaddr(&as<byte_t>(), size(), family);
            if (!ent) {
                return std::string();
            }
            return ent->h_name;
        }

        static IPAddress Parse(const char *name) {
            auto ent = ::gethostbyname(name);
            if (!ent) {
                return IPAddress();
            }
            IPAddress addr;
            addr.family = AddressFamily(ent->h_addrtype);
            ::memcpy(&addr.storage, ent->h_addr, ent->h_length);
            return addr;
        }
    private:
        union Storage {
            ::in_addr v4;
            ::in6_addr v6;
        } storage { };
        AddressFamily family { };
    friend class IPEndpoint;
};

class IPEndpoint {
    public:
        IPEndpoint() = default;
        IPEndpoint(const IPAddress &addr, uint16_t port) {
            storage.ss_family = int(addr.family);
            switch (addr.family) {
                case AddressFamily::V4: {
                    auto &i4 = as<::sockaddr_in>();
                    i4.sin_addr = addr.as<::in_addr>();
                    i4.sin_port = ::htons(port);
                    break;
                }
                case AddressFamily::V6: {
                    auto &i6 = as<::sockaddr_in6>();
                    i6.sin6_addr = addr.as<::in6_addr>();
                    i6.sin6_port = ::htons(port);
                    break;
                }
                default: {
                    clear();
                    break;
                }
            }
        }
        IPEndpoint(const IPEndpoint &) = default;
        ~IPEndpoint() = default;

        template <typename T>
        T &as() noexcept {
            return reinterpret_cast<T&>(storage);
        }
        template <typename T>
        const T &as() const noexcept {
            return reinterpret_cast<const T&>(storage);
        }
        void *data() noexcept {
            return &storage;
        }
        const void *data() const noexcept {
            return &storage;
        }
        bool empty() const noexcept {
            IPEndpoint ep;
            return ::memcmp(this, &ep, sizeof(IPEndpoint)) == 0;
        }
        void clear() {
            ::memset(this, 0, sizeof(IPEndpoint));
        }

        AddressFamily family() const noexcept {
            return AddressFamily(storage.ss_family);
        }
        IPAddress address() const noexcept {
            switch (family()) {
                case AddressFamily::V4: return IPAddress(as<::sockaddr_in>().sin_addr);
                case AddressFamily::V6: return IPAddress(as<::sockaddr_in6>().sin6_addr);
                default: return IPAddress();
            }
        }
        uint16_t  port() const noexcept {
            switch (family()) {
                case AddressFamily::V4: return ::ntohs(as<::sockaddr_in>().sin_port);
                case AddressFamily::V6: return ::ntohs(as<sockaddr_in6>().sin6_port);
                default : return 0;
            }
        }
        socklen_t size() const noexcept {
            switch (family()) {
                case AddressFamily::V4: return sizeof(sockaddr_in);
                case AddressFamily::V6: return sizeof(sockaddr_in6);
                default : return 0;
            }
        }
        std::string to_string() const noexcept {
            if (!empty()) {
                return address().to_string() + ':' + std::to_string(port());
            }
            return std::string();
        }

        static IPEndpoint Parse(const char *name, uint16_t port) {
            return IPEndpoint(IPAddress::Parse(name), port);
        }
    private:
        ::sockaddr_storage storage { };
};

class SocketView {
    public:
        SocketView() = default;
        SocketView(socket_t sockfd) : sockfd(sockfd) { }
        SocketView(const SocketView &) = default;
        ~SocketView() = default;

        [[nodiscard]]
        ssize_t recv(void *buf, size_t size, int flags = 0) {
            return ::recv(sockfd, static_cast<byte_t*>(buf), size, flags);
        }
        [[nodiscard]]
        ssize_t send(const void *buf, size_t size, int flags = 0) {
            return ::send(sockfd, static_cast<const byte_t*>(buf), size, flags);
        }
        [[nodiscard]]
        ssize_t send(std::string_view view, int flags = 0) {
            return send(view.data(), view.size(), flags);
        }
        [[nodiscard]]
        ssize_t sendto(const void *buf, size_t size, int flags = 0, const IPEndpoint *ep = nullptr) {
            const ::sockaddr *addr = nullptr;
            socklen_t addrlen = 0;
            if (ep) {
                addr = &ep->as<sockaddr>();
                addrlen = ep->size();
            }

            return ::sendto(sockfd, static_cast<const byte_t*>(buf), size, flags, addr, addrlen);
        }
        [[nodiscard]]
        ssize_t sendto(const void *buf, size_t size, int flags, const IPEndpoint &ep) {
            return sendto(buf, size, flags, &ep);
        }

        [[nodiscard]]
        bool listen(size_t backlog) {
            return ::listen(sockfd, backlog) == 0;
        }
        [[nodiscard]]
        bool bind(const IPEndpoint &endpoint) {
            return ::bind(sockfd, &endpoint.as<sockaddr>(), endpoint.size()) == 0;
        }
        [[nodiscard]]
        bool connect(const IPEndpoint &endpoint) {
            return ::connect(sockfd, &endpoint.as<sockaddr>(), endpoint.size()) == 0;
        }
        [[nodiscard]]
        bool close() {
            int code = 0;
            if (sockfd != ILLIAS_INVALID_SOCKET) {
                code = ::closesocket(sockfd);
                sockfd = ILLIAS_INVALID_SOCKET;
            }
            return code == 0;
        }
        [[nodiscard]]
        bool setblocking(bool v) {
            u_long arg = !v;
            return ::ioctlsocket(sockfd, FIONBIO, &arg) == 0;
        }
        [[nodiscard]]
        bool valid() const noexcept {
            return sockfd != ILLIAS_INVALID_SOCKET;
        }
        [[nodiscard]]
        bool bad() const noexcept {
            return sockfd == ILLIAS_INVALID_SOCKET;
        }

        [[nodiscard]]
        std::pair<ssize_t, IPEndpoint> recvform(void *buf, size_t size, int flags = 0) {
            IPEndpoint endpoint;
            socklen_t addrlen = sizeof(::sockaddr_storage);
            ssize_t n;

            n = ::recvfrom(sockfd, static_cast<byte_t*>(buf), size, flags, &endpoint.as<::sockaddr>(), &addrlen);
            return std::make_pair(n, endpoint);
        }
        template <typename T = SocketView>
        [[nodiscard]]
        std::pair<T, IPEndpoint> accept() {
            IPEndpoint endpoint;
            socklen_t size = sizeof(::sockaddr_storage);

            socket_t fd = ::accept(sockfd, &endpoint.as<::sockaddr>(), &size);
            return std::make_pair(T(fd), endpoint);
        }

        [[nodiscard]]
        IPEndpoint local_endpoint() const {
            IPEndpoint endpoint;
            socklen_t size = sizeof(::sockaddr_storage);

            if (::getsockname(sockfd, &endpoint.as<::sockaddr>(), &size) != 0) {
                return IPEndpoint();
            }
            return endpoint;
        }

        [[nodiscard]]
        IPEndpoint remote_endpoint() const {
            IPEndpoint endpoint;
            socklen_t size = sizeof(::sockaddr_storage);

            if (::getpeername(sockfd, &endpoint.as<::sockaddr>(), &size) != 0) {
                return IPEndpoint();
            }
            return endpoint;
        }

        [[nodiscard]]
        socket_t fd() const noexcept {
            return sockfd;
        }
    protected:
        socket_t sockfd = ILLIAS_INVALID_SOCKET;
};

class Socket : public SocketView {
    public:
        explicit Socket(socket_t sockfd) : SocketView(sockfd) { }
        explicit Socket(int family, int type, int pro = 0) 
            : SocketView(::socket(family, type, pro)) { }
        Socket() = default;
        Socket(const Socket &) = delete;
        Socket(Socket &&sock) {
            sockfd = sock.release();
        }
        ~Socket() {
            [[may_unused]] auto m = close();
        }

        [[nodiscard]]
        socket_t release(socket_t nsock = ILLIAS_INVALID_SOCKET) {
            socket_t fd = sockfd;
            sockfd = nsock;
            return fd;
        }
        void     reset(socket_t nsock = ILLIAS_INVALID_SOCKET) {
            [[may_unused]] auto m = close();
            sockfd = nsock;
        }

        [[nodiscard]]
        std::pair<Socket, IPEndpoint> accept() {
            return SocketView::accept<Socket>();
        }

        Socket &operator =(Socket &&s) {
            reset(s.release());
            return *this;
        }
};

class FDSet : public ::fd_set {
    public:
        FDSet() {
            clear();
        }

        void clear() {
            FD_ZERO(this);
        }
        void add(SocketView view) {
            FD_SET(view.fd(), this);
        }
        void erase(SocketView view) {
            FD_CLR(view.fd(), this);
        }
        bool isset(SocketView view) {
            return FD_ISSET(view.fd(), this);
        }
};

class Error {
    public:
        Error(error_t err) : err(err) { }
        Error(const Error &) = default;
        ~Error() = default;

        [[nodiscard]]
        error_t code() const noexcept {
            return err;
        }
        [[nodiscard]]
        std::string message() const noexcept {
#ifdef      _WIN32
            char *buffer;
            ::FormatMessageA(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                nullptr,
                err,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                (char*)&buffer,
                0,
                nullptr
            );
            std::string result(buffer);
            ::LocalFree(buffer);
            return result;
#else
            return ::strerror(errcode);
#endif
        }

        operator error_t() const noexcept {
            return err;
        }
    private:
        error_t err { };
};

inline Error GetLastError() {
    return ::WSAGetLastError();
}

inline int  Poll(pollfd_t *pfds, uint32_t npfd, int timeout = -1) {
    return ::ILLIAS_POLL(pfds, npfd, timeout);
}

inline bool InitSocket() {
    ::WSADATA wsa;
    return ::WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
}
inline bool QuitSocket() {
    return ::WSACleanup();
}

ILLIAS_NS_END

#undef closesocket