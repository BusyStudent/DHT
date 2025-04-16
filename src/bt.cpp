#include "bencode.hpp"
#include "log.hpp"
#include "bt.hpp"

// TODO: Replace the Error::Unknown with our own error

auto BtClient::handshake(const InfoHash &hash, const PeerId &peerId) -> IoTask<void> {
    BtHandshakeMessage message {};
    message.pstrlen = std::byte {19};
    ::memcpy(&message.pstr, "BitTorrent protocol", 19);
    message.infoHash = hash;
    message.peerId = peerId;
    message.reserved[5] = std::byte {0x10};

    if (auto res = co_await mClient.writeAll(makeBuffer(&message, sizeof(message))); res != sizeof(message)) {
        co_return unexpected(res.error_or(Error::ConnectionAborted));
    }
    BtHandshakeMessage rmessage;
    if (auto res = co_await mClient.readAll(makeBuffer(&rmessage, sizeof(rmessage))); res != sizeof(rmessage)) {
        co_return unexpected(res.error_or(Error::ConnectionAborted));
    }
    if (rmessage.pstrlen != message.pstrlen || ::memcmp(&rmessage.pstr, &message.pstr, 19) != 0) {
        // co_return unexpected(Error::InvalidHandshake);
        co_return unexpected(Error::Unknown);
    }
    // Get the info hash
    if (rmessage.infoHash != hash) {
        DHT_LOG("InfoHash mismatch {}", rmessage.infoHash);
        co_return unexpected(Error::Unknown);
    }
    mRemotePeerId = rmessage.peerId;
    // Check supported extensions
    if (rmessage.reserved[5] != std::byte {0x10}) {
        DHT_LOG("Unsupported extensions");
        co_return {};
    }
    // Begin the extension handshake
    // auto dict = BenObject::makeDict();
    // dict["m"] = BenObject::makeDict();
    // dict["m"]["ut_metadata"] = 1;

    co_return {};
}
