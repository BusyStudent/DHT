#include "bencode.hpp"
#include "log.hpp"
#include "bt.hpp"

// TODO: Replace the Error::Unknown with our own error
BtClient::BtClient(DynStreamClient c) : mClient(std::move(c)) {

}

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
        BT_LOG("InfoHash mismatch {}", rmessage.infoHash);
        co_return unexpected(Error::Unknown);
    }
    mRemotePeerId = rmessage.peerId;
    // Check supported extensions
    if (!uint8_t(rmessage.reserved[5] & std::byte {0x10})) {
        BT_LOG("Unsupported extensions");
        co_return {};
    }
    // Begin the extension handshake
    auto dict = BenObject::makeDict();
    dict["m"] = BenObject::makeDict();
    dict["m"]["ut_metadata"] = MetadataExtId; // We need metadata
    dict["v"] = "DHT Indexer https://github.com/BusyStudent/DHT";
    // dict["m"]["ut_pex"] = PexExtId; // We need pex
    if (auto res = co_await sendMessageExt(0, dict); !res) {
        co_return unexpected(res.error());
    }
    auto msg = co_await recvMessage();
    if (!msg) {
        co_return unexpected(msg.error());
    }
    auto &[id, len] = *msg;
    if (id != BtMessageId::Extended) {
        // Emm we are not in the right state
        BT_LOG("Unexpected message id {}", int(id));
        co_return unexpected(Error::Unknown);
    }
    std::string ext;
    ext.resize(len);
    if (auto res = co_await recvMessagePayload(makeBuffer(ext)); res != ext.size()) {
        co_return unexpected(res.error());
    }
    mRemoteExtension = BenObject::decode(std::string_view(ext).substr(1)); // Skip the id
    if (mRemoteExtension.isNull()) {
        BT_LOG("Invalid extended message");
        co_return unexpected(Error::Unknown);
    }
    BT_LOG("Remote extension: {}", mRemoteExtension);
    co_return {};
}

auto BtClient::sendMessage(BtMessageId id, std::span<const std::byte> data) -> IoTask<void> {
    std::array<std::byte, 5> header;
    uint32_t len = hostToNetwork(uint32_t(data.size() + 1)); // +1 for the id
    ::memcpy(&header[0], &len, 4);
    header[4] = std::byte(id);

    if (auto res = co_await mClient.writeAll(header); res != sizeof(header)) {
        co_return unexpected(res.error_or(Error::ConnectionAborted));
    }
    if (auto res = co_await mClient.writeAll(data); res != data.size()) {
        co_return unexpected(res.error_or(Error::ConnectionAborted));
    }
    co_return {};
}

auto BtClient::sendMessageExt(int extId, const BenObject &message) -> IoTask<void> {
    std::string payload;
    payload.push_back(extId);
    payload += message.encode();
    co_return co_await sendMessage(BtMessageId::Extended, makeBuffer(payload));
}

auto BtClient::recvMessage() -> IoTask<std::pair<BtMessageId, size_t> > {
    if (mPayloadLeft) {
        // Program error, we should not be here
        assert(false &&
            "Paylaod left, user should call recvMessagePayload until it return "
            "zero");
    }
    mPayloadLeft = std::nullopt;
    while (true) {
        // Message is len(include id + payload) ... id ... payload
        std::array<std::byte, 4> rawLen;
        std::array<std::byte, 1> rawId;
        if (auto res = co_await mClient.readAll(rawLen); res != sizeof(rawLen)) {
            co_return unexpected(res.error_or(Error::ConnectionAborted));
        }
        auto len = networkToHost(std::bit_cast<uint32_t>(rawLen));
        if (len == 0) { // Is Keepalive
            continue;
        }
        if (auto res = co_await mClient.readAll(rawId); res != sizeof(rawId)) {
            co_return unexpected(res.error_or(Error::ConnectionAborted));
        }
        auto id = BtMessageId(rawId[0]);
        len -= 1;  // -1 for the id
        if (len != 0) {
            mPayloadLeft = len;
        }
        BT_LOG("Recv message id {}, len {}", id, len);
        co_return std::pair{id, len};
    }
}

auto BtClient::recvMessagePayload(std::span<std::byte> buffer) -> IoTask<size_t> {
    if (!mPayloadLeft) {
        co_return 0;
    }
    size_t len = std::min(buffer.size(), *mPayloadLeft);
    if (auto res = co_await mClient.readAll(makeBuffer(buffer.data(), len)); res != len) {
        co_return unexpected(res.error_or(Error::ConnectionAborted));
    }
    *mPayloadLeft -= len;
    if (mPayloadLeft == 0) {
        mPayloadLeft = std::nullopt;
    }
    co_return len;
}

auto BtClient::dropMessagePayload() -> IoTask<void> { 
    if (!mPayloadLeft) {
        co_return {};
    }
    std::vector<std::byte> buffer(*mPayloadLeft, std::byte(0));
    if (auto res = co_await mClient.readAll(makeBuffer(buffer)); res != buffer.size()) {
        co_return unexpected(res.error_or(Error::ConnectionAborted));
    }
    mPayloadLeft = std::nullopt;
    co_return {};
}
