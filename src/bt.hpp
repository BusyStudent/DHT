/**
 * @file bt.hp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief The Bittorrent peer protocol
 * @version 0.1
 * @date 2025-03-25
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#include "nodeid.hpp"
#include "net.hpp"
#include <array>

using PeerId = std::array<std::byte, 20>;

/**
 * @brief Bittorrent handshake messsage "Bittorrent protocol" + reserved bytes + infohash + peerid
 * 
 */
struct alignas (1) BtHandshakeMessage {
    std::byte pstrlen;
    std::array<std::byte, 19> pstr;
    std::array<std::byte, 8> reserved;
    InfoHash infoHash;
    PeerId peerId;
};
static_assert(sizeof(BtHandshakeMessage) == 68);

enum class BtMessageId : uint8_t {
    KeepAlive = 0,
    Choke = 1,
    Unchoke = 2,
    Interested = 3,
    NotInterested = 4,
    Have = 5,
    Bitfield = 6,
    Request = 7,
    Piece = 8,
    Cancel = 9,
    Port = 10,
    Extended = 20, // The message from extension
};

/**
 * @brief Using the Bittorrent peer protocol
 * 
 */
class BtClient {
public:
    BtClient(TcpClient client);

    auto handshake(const InfoHash &hash, const PeerId &peerId) -> IoTask<void>;
private:
    PeerId mRemotePeerId;
    TcpClient mClient;
};